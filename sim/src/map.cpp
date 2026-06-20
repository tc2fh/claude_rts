#include "sim/map.h"

namespace sim {

Map::Map(std::uint32_t /*map_id*/) : w_(24), h_(24), passable_(24 * 24, 1) {
    // M0 fixed map: open 24x24 with a vertical wall at x=12, y in [4,20), gap at y=10.
    for (int y = 4; y < 20; ++y) passable_[y * w_ + 12] = 0;
    passable_[10 * w_ + 12] = 1;   // the gap
}

} // namespace sim
