#include "sim_bridge.h"

#include <godot_cpp/core/class_db.hpp>

#include <cmath>
#include <unordered_map>

using namespace godot;

// Q32.32 <-> double. Conversions happen ONLY at this view boundary; floats never
// flow back into the sim (which is fixed-point for determinism).
static inline double fix_to_d(fix64_t v) {
	return (double)v / 4294967296.0; // 2^32
}
static inline fix64_t d_to_fix(double v) {
	return (fix64_t)llround(v * 4294967296.0);
}

SimBridge::SimBridge() {}

SimBridge::~SimBridge() {
	destroy();
}

void SimBridge::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create", "seed", "map_id"), &SimBridge::create);
	ClassDB::bind_method(D_METHOD("destroy"), &SimBridge::destroy);
	ClassDB::bind_method(D_METHOD("is_ready"), &SimBridge::is_ready);
	ClassDB::bind_method(D_METHOD("advance", "ticks"), &SimBridge::advance);
	ClassDB::bind_method(D_METHOD("tick"), &SimBridge::tick);
	ClassDB::bind_method(D_METHOD("entity_count"), &SimBridge::entity_count);
	ClassDB::bind_method(D_METHOD("entity_ids"), &SimBridge::entity_ids);
	ClassDB::bind_method(D_METHOD("entity_meta"), &SimBridge::entity_meta);
	ClassDB::bind_method(D_METHOD("render_state", "alpha"), &SimBridge::render_state);
	ClassDB::bind_method(D_METHOD("get_resource", "player"), &SimBridge::get_resource);
	ClassDB::bind_method(D_METHOD("map_size"), &SimBridge::map_size);
	ClassDB::bind_method(D_METHOD("map_passable"), &SimBridge::map_passable);
	ClassDB::bind_method(D_METHOD("command_move", "unit_id", "tx", "ty"), &SimBridge::command_move);
	ClassDB::bind_method(D_METHOD("command_stop", "unit_id"), &SimBridge::command_stop);
	ClassDB::bind_method(D_METHOD("command_train", "hq_id", "unit_type"), &SimBridge::command_train);
	ClassDB::bind_method(D_METHOD("command_harvest", "unit_id", "node_id"), &SimBridge::command_harvest);
	ClassDB::bind_method(D_METHOD("command_attack", "unit_id", "target_id"), &SimBridge::command_attack);
	ClassDB::bind_method(D_METHOD("winner"), &SimBridge::winner);
	ClassDB::bind_method(D_METHOD("state_hash"), &SimBridge::state_hash);
	ClassDB::bind_method(D_METHOD("set_input_delay", "n"), &SimBridge::set_input_delay);
	ClassDB::bind_method(D_METHOD("get_input_delay"), &SimBridge::get_input_delay);
}

void SimBridge::create(int64_t seed, int64_t map_id) {
	destroy();
	world = sim_create((uint64_t)seed, (uint32_t)map_id);
	advance(0); // prime prev_/curr_ from the initial snapshot
}

void SimBridge::destroy() {
	if (world) {
		sim_destroy(world);
		world = nullptr;
	}
	prev_.clear();
	curr_.clear();
	curr_tick_ = 0;
}

void SimBridge::advance(int ticks) {
	if (!world) {
		return;
	}
	if (ticks > 0) {
		sim_advance(world, (uint32_t)ticks);
	}
	prev_ = curr_; // last frame's curr becomes prev
	SimSnapshot s = sim_get_snapshot(world);
	// COPY out of the sim's buffer — its pointer is invalid after the next advance.
	curr_.assign(s.entities, s.entities + s.count);
	curr_tick_ = s.tick;
	if (prev_.empty()) {
		prev_ = curr_;
	}
}

int64_t SimBridge::tick() const {
	return (int64_t)curr_tick_;
}

int SimBridge::entity_count() const {
	return (int)curr_.size();
}

PackedInt32Array SimBridge::entity_ids() const {
	PackedInt32Array out;
	const int n = (int)curr_.size();
	out.resize(n);
	for (int i = 0; i < n; ++i) {
		out.set(i, (int)curr_[i].id);
	}
	return out;
}

PackedInt32Array SimBridge::entity_meta() const {
	PackedInt32Array out;
	const int n = (int)curr_.size();
	out.resize(n * 5);
	for (int i = 0; i < n; ++i) {
		const SimEntitySnapshot &e = curr_[i];
		out.set(i * 5 + 0, (int)e.type);
		out.set(i * 5 + 1, (int)e.owner);
		out.set(i * 5 + 2, (int)e.state);
		out.set(i * 5 + 3, (int)e.hp);
		out.set(i * 5 + 4, (int)e.hp_max);
	}
	return out;
}

PackedFloat32Array SimBridge::render_state(double alpha) const {
	PackedFloat32Array out;
	const int n = (int)curr_.size();
	out.resize(n * 3);

	std::unordered_map<uint32_t, const SimEntitySnapshot *> pmap;
	pmap.reserve(prev_.size());
	for (const SimEntitySnapshot &e : prev_) {
		pmap[e.id] = &e;
	}

	const double TAU_D = 6.283185307179586;
	for (int i = 0; i < n; ++i) {
		const SimEntitySnapshot &c = curr_[i];
		double cx = fix_to_d(c.x), cy = fix_to_d(c.y);
		double px = cx, py = cy, pf = (double)c.facing;

		auto it = pmap.find(c.id);
		if (it != pmap.end()) {
			px = fix_to_d(it->second->x);
			py = fix_to_d(it->second->y);
			pf = (double)it->second->facing;
		}

		double x = px + (cx - px) * alpha;
		double y = py + (cy - py) * alpha;

		// shortest-arc interpolation of facing on the 0..65535 ring
		double d = (double)c.facing - pf;
		if (d > 32768.0) {
			d -= 65536.0;
		} else if (d < -32768.0) {
			d += 65536.0;
		}
		double frad = ((pf + d * alpha) / 65536.0) * TAU_D;

		out.set(i * 3 + 0, (float)x);
		out.set(i * 3 + 1, (float)y);
		out.set(i * 3 + 2, (float)frad);
	}
	return out;
}

int SimBridge::get_resource(int player) const {
	if (!world || player < 0 || player >= 8) {
		return 0;
	}
	SimSnapshot s = sim_get_snapshot(world);
	return (int)s.resources[player];
}

Vector2i SimBridge::map_size() const {
	if (!world) {
		return Vector2i(0, 0);
	}
	SimMapInfo mi = sim_get_map_info(world);
	return Vector2i((int)mi.w, (int)mi.h);
}

PackedByteArray SimBridge::map_passable() const {
	PackedByteArray out;
	if (!world) {
		return out;
	}
	SimMapInfo mi = sim_get_map_info(world);
	const int n = (int)mi.w * (int)mi.h;
	out.resize(n);
	for (int i = 0; i < n; ++i) {
		out.set(i, mi.passable[i]);
	}
	return out;
}

void SimBridge::command_move(int unit_id, double tx, double ty) {
	if (!world) {
		return;
	}
	SimCommand c = {};
	c.type = CMD_MOVE;
	c.player = 1;
	c.unit = (uint32_t)unit_id;
	c.tx = d_to_fix(tx);
	c.ty = d_to_fix(ty);
	sim_push_command(world, &c, sim_current_tick(world) + (uint64_t)input_delay_);
}

void SimBridge::command_stop(int unit_id) {
	if (!world) {
		return;
	}
	SimCommand c = {};
	c.type = CMD_STOP;
	c.player = 1;
	c.unit = (uint32_t)unit_id;
	sim_push_command(world, &c, sim_current_tick(world) + (uint64_t)input_delay_);
}

void SimBridge::command_train(int hq_id, int unit_type) {
	if (!world) {
		return;
	}
	SimCommand c = {};
	c.type = CMD_TRAIN;
	c.player = 1;
	c.unit = (uint32_t)hq_id;
	c.param = (uint16_t)unit_type;
	sim_push_command(world, &c, sim_current_tick(world) + (uint64_t)input_delay_);
}

void SimBridge::command_harvest(int unit_id, int node_id) {
	if (!world) {
		return;
	}
	SimCommand c = {};
	c.type = CMD_HARVEST;
	c.player = 1;
	c.unit = (uint32_t)unit_id;
	c.target = (uint32_t)node_id;
	sim_push_command(world, &c, sim_current_tick(world) + (uint64_t)input_delay_);
}

void SimBridge::command_attack(int unit_id, int target_id) {
	if (!world) {
		return;
	}
	SimCommand c = {};
	c.type = CMD_ATTACK;
	c.player = 1;
	c.unit = (uint32_t)unit_id;
	c.target = (uint32_t)target_id;
	sim_push_command(world, &c, sim_current_tick(world) + (uint64_t)input_delay_);
}

int SimBridge::winner() const {
	return world ? (int)sim_winner(world) : 0;
}

int64_t SimBridge::state_hash() const {
	return world ? (int64_t)sim_state_hash(world) : 0;
}
