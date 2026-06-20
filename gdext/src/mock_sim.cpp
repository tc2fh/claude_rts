// mock_sim.cpp — stand-in implementation of sim_abi.h so the gdext/view pipeline
// builds and runs before B's real deterministic sim lands. A few entities drift
// and bounce; MOVE/STOP commands steer them, so the whole render -> select ->
// command path is exercisable today.
//
// Self-disables when built with SIM_RTS_USE_REAL_SIM (see gdext/SConstruct
// use_real_sim=yes), so it can coexist with the real sim during the handoff.
#ifndef SIM_RTS_USE_REAL_SIM

#include "sim_abi.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr fix64_t FIX_ONE = (fix64_t)1 << SIM_FIX_SHIFT;
inline fix64_t fx_from_int(int64_t v) {
	return v << SIM_FIX_SHIFT;
}
inline void fnv1a(uint64_t &h, const void *p, size_t n) {
	const uint8_t *b = (const uint8_t *)p;
	for (size_t i = 0; i < n; ++i) {
		h ^= b[i];
		h *= 1099511628211ULL;
	}
}
} // namespace

struct SimWorld {
	uint64_t tick = 0;
	std::vector<SimEntitySnapshot> ents; // canonical state
	std::vector<SimEntitySnapshot> published; // stable buffer returned by sim_get_snapshot
	std::vector<fix64_t> vx, vy; // drift velocity (Q32.32 / tick)
	std::vector<fix64_t> tx, ty; // move target
	std::vector<uint8_t> seeking;
	int32_t resources[8] = { 0 };
};

extern "C" {

SimWorld *sim_create(uint64_t seed, uint32_t map_id) {
	(void)seed;
	(void)map_id;
	SimWorld *w = new SimWorld();
	const int N = 3;
	for (int i = 0; i < N; ++i) {
		SimEntitySnapshot e = {};
		e.id = (uint32_t)(i + 1);
		e.type = 1;
		e.owner = 1;
		e.state = SIM_STATE_MOVING;
		e.x = fx_from_int(8 + i * 4);
		e.y = fx_from_int(8 + i * 3);
		e.facing = (uint16_t)(i * 8000);
		e.hp = e.hp_max = 100;
		w->ents.push_back(e);
		w->vx.push_back(FIX_ONE / (24 * (i + 2)));
		w->vy.push_back(FIX_ONE / (24 * (i + 3)));
		w->tx.push_back(0);
		w->ty.push_back(0);
		w->seeking.push_back(0);
	}
	w->resources[1] = 50;
	w->published = w->ents;
	return w;
}

void sim_destroy(SimWorld *w) {
	delete w;
}

void sim_advance(SimWorld *w, uint32_t ticks) {
	const fix64_t lo = 0;
	const fix64_t hi = fx_from_int(32);
	const fix64_t step = FIX_ONE / 6; // ~0.17 unit/tick when seeking a target
	for (uint32_t t = 0; t < ticks; ++t) {
		for (size_t i = 0; i < w->ents.size(); ++i) {
			SimEntitySnapshot &e = w->ents[i];
			if (w->seeking[i]) {
				fix64_t dx = w->tx[i] - e.x;
				fix64_t dy = w->ty[i] - e.y;
				e.x += (dx > step) ? step : (dx < -step ? -step : dx);
				e.y += (dy > step) ? step : (dy < -step ? -step : dy);
				if (e.x == w->tx[i] && e.y == w->ty[i]) {
					w->seeking[i] = 0;
					e.state = SIM_STATE_IDLE;
				} else {
					e.state = SIM_STATE_MOVING;
				}
			} else {
				e.x += w->vx[i];
				e.y += w->vy[i];
				if (e.x < lo || e.x > hi) {
					w->vx[i] = -w->vx[i];
				}
				if (e.y < lo || e.y > hi) {
					w->vy[i] = -w->vy[i];
				}
				e.state = SIM_STATE_MOVING;
			}
			e.facing = (uint16_t)(e.facing + 220);
		}
		w->tick++;
		if ((w->tick % 24) == 0) {
			w->resources[1] += 1; // ~1 mineral/sec
		}
	}
	w->published = w->ents; // publish atomically; pointer valid until the next advance
}

uint64_t sim_current_tick(const SimWorld *w) {
	return w->tick;
}

void sim_push_command(SimWorld *w, const SimCommand *c, uint64_t exec_tick) {
	(void)exec_tick; // mock applies immediately; the real sim queues to exec_tick
	if (!c) {
		return;
	}
	for (size_t i = 0; i < w->ents.size(); ++i) {
		if (w->ents[i].id != c->unit) {
			continue;
		}
		if (c->type == CMD_MOVE) {
			w->tx[i] = c->tx;
			w->ty[i] = c->ty;
			w->seeking[i] = 1;
		} else if (c->type == CMD_STOP) {
			w->seeking[i] = 0;
		}
	}
}

SimSnapshot sim_get_snapshot(const SimWorld *w) {
	SimSnapshot s = {};
	s.tick = w->tick;
	s.entities = w->published.data();
	s.count = (uint32_t)w->published.size();
	for (int i = 0; i < 8; ++i) {
		s.resources[i] = w->resources[i];
	}
	return s;
}

uint64_t sim_state_hash(const SimWorld *w) {
	uint64_t h = 1469598103934665603ULL; // FNV-1a/64
	fnv1a(h, &w->tick, sizeof(w->tick));
	for (size_t k = 0; k < w->ents.size(); ++k) {
		const SimEntitySnapshot &e = w->ents[k];
		fnv1a(h, &e.x, sizeof(e.x));
		fnv1a(h, &e.y, sizeof(e.y));
		fnv1a(h, &e.facing, sizeof(e.facing));
		fnv1a(h, &e.hp, sizeof(e.hp));
	}
	return h;
}

} // extern "C"

#endif // SIM_RTS_USE_REAL_SIM
