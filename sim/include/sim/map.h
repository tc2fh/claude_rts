#pragma once
#include <cstdint>
#include <vector>
#include "sim/fixed.h"

namespace sim {

struct GridPos { int x, y; };
inline bool operator==(GridPos a, GridPos b) { return a.x == b.x && a.y == b.y; }

class Map {
public:
    explicit Map(std::uint32_t map_id);   // map_id 0 == the fixed M0 map

    int width()  const { return w_; }
    int height() const { return h_; }
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    bool passable(int x, int y) const { return in_bounds(x, y) && passable_[y * w_ + x] != 0; }

    static fix cell_to_world(int c) { return fix_from_int(c); }   // 1 cell == 1 world unit
    static int world_to_cell(fix p) { return static_cast<int>(p >> FIX_FRAC); }

private:
    int w_, h_;
    std::vector<char> passable_;
};

} // namespace sim
