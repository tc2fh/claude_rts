#ifndef SIM_RTS_SIM_BRIDGE_H
#define SIM_RTS_SIM_BRIDGE_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <vector>

#include <sim/sim_abi.h>

namespace godot {

// Godot-facing wrapper around the C-ABI deterministic sim (sim_abi.h).
//
// Owns the prev/curr snapshot copies: the sim's published pointer is only valid
// until the next advance (ABI lifetime rule), so SimBridge copies each snapshot
// and interpolates between the two newest ticks. Exposes typed, batch accessors
// (no per-entity Variants) plus the command path.
class SimBridge : public RefCounted {
	GDCLASS(SimBridge, RefCounted)

	SimWorld *world = nullptr;
	std::vector<SimEntitySnapshot> prev_;
	std::vector<SimEntitySnapshot> curr_;
	uint64_t curr_tick_ = 0;
	int input_delay_ = 2; // lockstep input delay (ticks); exercised even single-player

protected:
	static void _bind_methods();

public:
	SimBridge();
	~SimBridge();

	void create(int64_t seed, int64_t map_id);
	void destroy();
	bool is_ready() const { return world != nullptr; }

	void advance(int ticks); // run N sim ticks, then refresh prev/curr
	int64_t tick() const;
	int entity_count() const;

	PackedInt32Array entity_ids() const; // [id] * n
	PackedInt32Array entity_meta() const; // [type, owner, state, hp, hp_max] * n
	PackedFloat32Array render_state(double alpha) const; // [x, y, facing_rad] * n, world units

	int get_resource(int player) const;

	void command_move(int unit_id, double tx, double ty);
	void command_stop(int unit_id);

	int64_t state_hash() const;

	void set_input_delay(int n) { input_delay_ = n < 0 ? 0 : n; }
	int get_input_delay() const { return input_delay_; }
};

} // namespace godot

#endif // SIM_RTS_SIM_BRIDGE_H
