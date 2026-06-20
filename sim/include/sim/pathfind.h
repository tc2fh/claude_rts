#pragma once
#include <vector>
#include "sim/map.h"

namespace sim {

// Cells from start to goal inclusive, or empty if unreachable. 8-directional,
// integer-cost, deterministic. Narrow interface — M1 may swap the implementation.
std::vector<GridPos> find_path(const Map& map, GridPos start, GridPos goal);

} // namespace sim
