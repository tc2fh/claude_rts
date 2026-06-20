#include "sim/sim_abi.h"
#include "sim/world.h"

using sim::World;

static World*       w(SimWorld* h)        { return reinterpret_cast<World*>(h); }
static const World* w(const SimWorld* h)  { return reinterpret_cast<const World*>(h); }

extern "C" {

SimWorld* sim_create(uint64_t seed, uint32_t map_id) {
    return reinterpret_cast<SimWorld*>(new World(seed, map_id));
}
void sim_destroy(SimWorld* h) { delete w(h); }
void sim_advance(SimWorld* h, uint32_t ticks) { w(h)->advance(ticks); }
uint64_t sim_current_tick(const SimWorld* h) { return w(h)->tick(); }
void sim_push_command(SimWorld* h, const SimCommand* c, uint64_t exec_tick) {
    w(h)->push_command(*c, exec_tick);
}
SimSnapshot sim_get_snapshot(const SimWorld* h) { return w(h)->snapshot(); }
uint64_t sim_state_hash(const SimWorld* h) { return w(h)->state_hash(); }
SimMapInfo sim_get_map_info(const SimWorld* h) {
    const auto& m = w(h)->map();
    SimMapInfo info;
    info.w = static_cast<uint16_t>(m.width());
    info.h = static_cast<uint16_t>(m.height());
    info.passable = m.passable_data();
    return info;
}

}
