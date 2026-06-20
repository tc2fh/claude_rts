#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <entt/entt.hpp>
#include "sim/rng.h"
#include "sim/sim_abi.h"
#include "sim/components.h"
#include "sim/map.h"

namespace sim {

class World {
public:
    World(std::uint64_t seed, std::uint32_t map_id);

    void advance(std::uint32_t ticks);
    std::uint64_t tick() const { return tick_; }
    std::size_t entity_count() const {
        return const_cast<entt::registry&>(reg_).view<CId>().size();
    }

    void push_command(const SimCommand& cmd, std::uint64_t exec_tick);
    const SimSnapshot& snapshot() const { return front_; }
    std::uint64_t state_hash() const;

private:
    void step();
    void apply_commands_for(std::uint64_t t);
    void sys_movement();
    void publish_snapshot();
    EntityId spawn(CPos pos, CMobile mob, CUnit unit);
    void spawn_initial();

    entt::registry reg_;
    Map            map_;
    std::uint64_t  tick_ = 0;
    Rng            rng_;
    EntityId       next_id_ = 0;

    std::vector<std::pair<std::uint64_t, SimCommand>> commands_;
    std::vector<SimEntitySnapshot> buf_[2];
    int        active_ = 0;
    SimSnapshot front_{};
};

} // namespace sim
