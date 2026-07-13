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

enum OrderKind : std::uint8_t {
    ORD_STOP, ORD_MOVE, ORD_ATTACK_MOVE, ORD_HOLD, ORD_PATROL, ORD_ATTACK_TARGET
};
struct COrder {
    OrderKind kind   = ORD_STOP;
    GridPos   dest{};          // move/attack-move/patrol leg target cell
    GridPos   anchor{};        // patrol other endpoint; ORD_STOP post (leash/return point)
    bool      to_dest = true;  // patrol leg direction (unused this task)
    EntityId  target = 0;      // ORD_ATTACK_TARGET: forced target id
};

struct CWeapon {
    std::int32_t  damage;
    int           range_cells;
    std::uint32_t cooldown;
    std::uint32_t timer = 0;   // cooldown countdown
    EntityId      target = 0;  // currently engaged (0 = none)
};

} // namespace sim
