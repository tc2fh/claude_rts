#pragma once
#include <cstdint>
#include <vector>
#include "sim/fixed.h"
#include "sim/map.h"   // GridPos

namespace sim {

using EntityId = std::uint32_t;

struct CId   { EntityId id; };
struct CPos  { fix x, y; };
struct CMobile {                    // a unit that can be ordered to move
    fix speed;
    std::vector<GridPos> path;      // waypoints, cell coords
    std::size_t next = 0;           // index of the next waypoint to reach
};
struct CUnit {
    std::uint16_t type;
    std::uint8_t  owner;            // 0 = neutral
    std::uint8_t  state;            // SIM_STATE_* bitflags
    std::uint16_t facing;
    std::int32_t  hp, hp_max;
};

} // namespace sim
