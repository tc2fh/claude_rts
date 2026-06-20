#include "sim/pathfind.h"
#include <queue>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace sim {
namespace {

struct Node { int cell; std::int32_t f; };
struct NodeCmp {
    bool operator()(const Node& a, const Node& b) const {
        if (a.f != b.f) return a.f > b.f;   // min-heap by f
        return a.cell > b.cell;             // deterministic tie-break by cell id
    }
};

std::int32_t octile(int dx, int dy) {
    dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
    int mn = dx < dy ? dx : dy;
    int mx = dx < dy ? dy : dx;
    return 14 * mn + 10 * (mx - mn);
}

} // namespace

std::vector<GridPos> find_path(const Map& map, GridPos start, GridPos goal) {
    const int W = map.width(), H = map.height(), N = W * H;
    if (!map.passable(start.x, start.y) || !map.passable(goal.x, goal.y)) return {};
    auto idx = [W](int x, int y) { return y * W + x; };
    const int s = idx(start.x, start.y), t = idx(goal.x, goal.y);

    std::vector<std::int32_t> g(N, INT32_MAX);
    std::vector<int>  came(N, -1);
    std::vector<char> closed(N, 0);
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    g[s] = 0;
    open.push({s, octile(start.x - goal.x, start.y - goal.y)});

    static const int D[8][3] = {
        {1,0,10},{-1,0,10},{0,1,10},{0,-1,10},
        {1,1,14},{1,-1,14},{-1,1,14},{-1,-1,14}
    };

    while (!open.empty()) {
        Node cur = open.top(); open.pop();
        if (cur.cell == t) break;
        if (closed[cur.cell]) continue;
        closed[cur.cell] = 1;
        const int cx = cur.cell % W, cy = cur.cell / W;
        for (auto& d : D) {
            const int nx = cx + d[0], ny = cy + d[1];
            if (!map.passable(nx, ny)) continue;
            if (d[0] != 0 && d[1] != 0 &&
                (!map.passable(cx + d[0], cy) || !map.passable(cx, cy + d[1]))) continue;  // no corner-cutting
            const int ni = idx(nx, ny);
            if (closed[ni]) continue;
            const std::int32_t ng = g[cur.cell] + d[2];
            if (ng < g[ni]) {
                g[ni] = ng;
                came[ni] = cur.cell;
                open.push({ni, ng + octile(nx - goal.x, ny - goal.y)});
            }
        }
    }

    if (s != t && came[t] == -1) return {};   // unreachable
    std::vector<GridPos> path;
    for (int c = t; c != -1; c = came[c]) {
        path.push_back({c % W, c / W});
        if (c == s) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace sim
