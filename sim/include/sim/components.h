#pragma once
#include <cstdint>
#include "sim/fixed.h"

namespace sim {

using EntityId = std::uint32_t;

struct CId   { EntityId id; };                 // stable, never recycled within a match
struct CPos  { fix x, y; };
struct CVel  { fix x, y; };                    // foundation drift; repurposed later
struct CUnit {
    std::uint16_t type;
    std::uint8_t  owner;                        // 0 = neutral
    std::uint8_t  state;                        // bitflags
    std::uint16_t facing;                       // 0..65535 = full turn
    std::int32_t  hp, hp_max;
};

} // namespace sim
