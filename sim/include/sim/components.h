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

struct CResource { std::int32_t amount; };

enum HarvPhase : std::uint8_t { HARV_IDLE, HARV_TO_NODE, HARV_MINING, HARV_TO_HQ };
struct CHarvester {
    std::uint8_t  phase   = HARV_IDLE;
    std::int32_t  carried = 0;
    EntityId      node    = 0;
    EntityId      hq      = 0;
    std::uint32_t timer   = 0;
};

struct CProducer {
    std::uint16_t train_type = 0;   // 0 = idle
    std::uint32_t timer      = 0;
};

} // namespace sim
