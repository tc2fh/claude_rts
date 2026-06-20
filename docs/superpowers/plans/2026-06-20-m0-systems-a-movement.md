# M0 Systems 2a — Map, Pathfinding & Movement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the foundation's deterministic "drift" stand-in with real units that **pathfind and move on a fixed grid map**, driven by `CMD_MOVE` — the movement substrate the economy (2b) and combat (2c) plans build on.

**Architecture:** Adds a fixed grid `Map` (passability), a deterministic grid **A\*** (8-directional, integer octile heuristic, no `sqrt`/float, deterministic tie-break), and a `CMobile` component + `sys_movement` system (axis-clamped stepping toward grid-cell waypoints — no `sqrt`/division). `apply_commands_for` gains a `CMD_MOVE` handler that A\*-paths the unit to the target cell. All of it preserves the foundation's determinism contract (same seed + same command log ⇒ identical `state_hash`, batching-invariant).

**Tech Stack:** C++17, EnTT, doctest, CMake — unchanged from the foundation. No new dependencies.

**Prerequisite:** Builds on the M0 sim foundation (PR #1 / branch `feat/m0-sim-foundation`). Start this on a branch off the **merged `main`** (after PR #1 lands) — or stacked on `feat/m0-sim-foundation` if iterating before merge. The foundation's `World`, `step()` order (`++tick → apply_commands → systems → publish`), `state_hash`, double-buffered snapshot, and ABI are reused as-is.

**Determinism rules (binding, from spec §4.3):** fixed-point only (no `float`/`double` in sim state); **no `sqrt`/trig** — A\* uses integer 10/14 costs + octile heuristic, movement uses axis-clamped steps; stable id-sorted iteration; seeded RNG only.

---

## Design decisions (this plan locks these for M0)
- **Grid:** fixed **24×24**, cell `(x,y)` 0-indexed. M0 map (id 0) is open except a vertical wall at `x=12`, `y∈[4,20)`, with a 1-cell gap at `y=10` — enough to make A\* path around something.
- **Coordinates:** **1 cell = 1 world unit** (`fix_from_int(1)`). Cell center in world = `(fix_from_int(x), fix_from_int(y))`. A position's cell = integer part (`pos >> FIX_FRAC`).
- **Movement:** axis-clamped step toward the next waypoint center; reach it when both `|dx|,|dy| ≤ speed` (snap + advance). Diagonal movement is slightly faster than cardinal — an accepted M0 simplification (true-diagonal scaling needs `fix` multiply, deferred). Unit `speed = fix_from_int(1)/8` (0.125 cell/tick ≈ 3 cells/s at 24 Hz).
- **A\*:** 8-directional, integer cost 10 (cardinal) / 14 (diagonal), octile heuristic, **no corner-cutting** through blocked diagonals, deterministic open-set ordering (by `f`, tie-break by cell index). Returns cells `[start..goal]` inclusive, or empty if unreachable. Behind a **narrow `find_path` interface** so M1 can swap in flow-fields.
- **Supersedes the drift stand-in:** `sys_drift` → `sys_movement`; `CVel` (drift) is removed; `spawn_initial` now places movable units. **The foundation's drift tests in `test_world.cpp` and the golden hash in `test_determinism.cpp` are updated here** (the golden is re-pinned for the new scenario — Tien re-confirms it on Windows CI when 2a lands).

---

## File Structure
```
sim/include/sim/map.h        Create — GridPos + Map (24×24 passability, cell<->world)   [B]
sim/src/map.cpp              Create — the fixed M0 map data                              [B]
sim/include/sim/pathfind.h   Create — find_path(map, start, goal) interface              [B]
sim/src/pathfind.cpp         Create — deterministic grid A*                              [B]
sim/include/sim/components.h Modify — remove CVel, add CMobile                           [B]
sim/include/sim/world.h      Modify — add Map member; sys_drift -> sys_movement decl     [B]
sim/src/world.cpp            Modify — sys_movement, CMD_MOVE handler, spawn_initial       [B]
sim/tests/test_map.cpp       Create                                                      [B]
sim/tests/test_pathfind.cpp  Create                                                      [B]
sim/tests/test_world.cpp     Modify — replace drift tests with movement tests            [B]
sim/tests/test_move.cpp      Create — CMD_MOVE end-to-end                                [B]
sim/tests/test_determinism.cpp Modify — movement scenario + re-pinned golden hash        [B]
```

---

## Task 1: The fixed grid Map

**Files:** Create `sim/include/sim/map.h`, `sim/src/map.cpp`, `sim/tests/test_map.cpp`.

- [ ] **Step 1: Write the failing test `sim/tests/test_map.cpp`**
```cpp
#include <doctest/doctest.h>
#include "sim/map.h"
using namespace sim;

TEST_CASE("M0 map is 24x24, mostly open, with a wall + gap") {
    Map m(0);
    CHECK(m.width() == 24);
    CHECK(m.height() == 24);
    CHECK(m.passable(0, 0));
    CHECK_FALSE(m.passable(12, 5));     // wall
    CHECK(m.passable(12, 10));          // gap in the wall
    CHECK_FALSE(m.passable(-1, 0));     // out of bounds
    CHECK_FALSE(m.passable(24, 0));
}

TEST_CASE("cell <-> world round-trips on cell centers") {
    CHECK(Map::world_to_cell(Map::cell_to_world(7)) == 7);
    CHECK(Map::cell_to_world(0) == 0);
}
```

- [ ] **Step 2: Run — verify it FAILS** (`map.h` not found).

- [ ] **Step 3: Write `sim/include/sim/map.h`**
```cpp
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

    // 1 cell == 1 world unit (Q32.32). Cell center <-> world.
    static fix cell_to_world(int c) { return fix_from_int(c); }
    static int world_to_cell(fix p) { return static_cast<int>(p >> FIX_FRAC); }

private:
    int w_, h_;
    std::vector<char> passable_;
};

} // namespace sim
```

- [ ] **Step 4: Write `sim/src/map.cpp`**
```cpp
#include "sim/map.h"

namespace sim {

Map::Map(std::uint32_t /*map_id*/) : w_(24), h_(24), passable_(24 * 24, 1) {
    // M0 fixed map: open 24x24 with a vertical wall at x=12, y in [4,20), gap at y=10.
    for (int y = 4; y < 20; ++y) passable_[y * w_ + 12] = 0;
    passable_[10 * w_ + 12] = 1;   // the gap
}

} // namespace sim
```

- [ ] **Step 5: Run — verify GREEN.** `cmake --build sim/build && ctest --test-dir sim/build --output-on-failure`.

- [ ] **Step 6: Commit**
```bash
git add sim/include/sim/map.h sim/src/map.cpp sim/tests/test_map.cpp
git commit -m "feat: fixed 24x24 grid Map with passability + cell/world conversion"
```

---

## Task 2: Deterministic grid A*

**Files:** Create `sim/include/sim/pathfind.h`, `sim/src/pathfind.cpp`, `sim/tests/test_pathfind.cpp`.

- [ ] **Step 1: Write the failing test `sim/tests/test_pathfind.cpp`**
```cpp
#include <doctest/doctest.h>
#include "sim/pathfind.h"
using namespace sim;

TEST_CASE("straight path on open ground starts and ends correctly") {
    Map m(0);
    auto p = find_path(m, {0, 0}, {5, 0});
    REQUIRE_FALSE(p.empty());
    CHECK(p.front() == GridPos{0, 0});
    CHECK(p.back()  == GridPos{5, 0});
}

TEST_CASE("path routes around the wall through the gap") {
    Map m(0);
    auto p = find_path(m, {11, 10}, {13, 10});  // straight through would cross x=12...
    REQUIRE_FALSE(p.empty());
    CHECK(p.front() == GridPos{11, 10});
    CHECK(p.back()  == GridPos{13, 10});
    for (auto c : p) CHECK(m.passable(c.x, c.y));  // never steps on the wall
}

TEST_CASE("unreachable goal returns empty") {
    Map m(0);
    // Box a fake target: goal on the wall itself is impassable.
    auto p = find_path(m, {0, 0}, {12, 5});
    CHECK(p.empty());
}

TEST_CASE("start == goal returns a single cell") {
    Map m(0);
    auto p = find_path(m, {3, 3}, {3, 3});
    REQUIRE(p.size() == 1);
    CHECK(p.front() == GridPos{3, 3});
}

TEST_CASE("A* is deterministic") {
    Map m(0);
    CHECK(find_path(m, {1, 1}, {20, 18}).size() == find_path(m, {1, 1}, {20, 18}).size());
    auto a = find_path(m, {1, 1}, {20, 18});
    auto b = find_path(m, {1, 1}, {20, 18});
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
}
```

- [ ] **Step 2: Run — verify it FAILS** (`pathfind.h` not found).

- [ ] **Step 3: Write `sim/include/sim/pathfind.h`**
```cpp
#pragma once
#include <vector>
#include "sim/map.h"

namespace sim {

// Cells from start to goal inclusive, or empty if unreachable. 8-directional,
// integer-cost, deterministic. Narrow interface — M1 may swap the implementation.
std::vector<GridPos> find_path(const Map& map, GridPos start, GridPos goal);

} // namespace sim
```

- [ ] **Step 4: Write `sim/src/pathfind.cpp`**
```cpp
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
            // No corner-cutting: a diagonal needs both orthogonal cells open.
            if (d[0] != 0 && d[1] != 0 &&
                (!map.passable(cx + d[0], cy) || !map.passable(cx, cy + d[1]))) continue;
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
```

- [ ] **Step 5: Run — verify GREEN.**

- [ ] **Step 6: Commit**
```bash
git add sim/include/sim/pathfind.h sim/src/pathfind.cpp sim/tests/test_pathfind.cpp
git commit -m "feat: deterministic grid A* (octile, no corner-cutting, stable tie-break)"
```

---

## Task 3: `CMobile` + `sys_movement` (replace drift)

**Files:** Modify `sim/include/sim/components.h`, `sim/include/sim/world.h`, `sim/src/world.cpp`; replace the drift tests in `sim/tests/test_world.cpp`.

- [ ] **Step 1: Update `sim/include/sim/components.h`** — remove `CVel`, add `CMobile`:
```cpp
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
```

- [ ] **Step 2: Update `sim/include/sim/world.h`** — add the `Map` member, swap the drift system for movement. Apply these edits:
  - Add includes after the EnTT include: `#include "sim/map.h"` and `#include "sim/pathfind.h"`.
  - Replace the private declaration `void sys_drift();` with `void sys_movement();`.
  - Add a private member: `Map map_;` (place it right after `entt::registry reg_;`).
  - The constructor signature is unchanged (`World(std::uint64_t seed, std::uint32_t map_id)`).

The resulting private section should read:
```cpp
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
```

- [ ] **Step 3: Replace the drift tests in `sim/tests/test_world.cpp`** with movement tests (keep the tick/command-timing tests; replace the two drift TEST_CASEs — "drift moves entities deterministically" and "same seed, same end state regardless of step batching" — with the versions below, and update the entity-count expectation):
```cpp
TEST_CASE("world spawns the two movable M0 units at tick 0") {
    World w(7, 0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 2);
}

TEST_CASE("a unit with a path advances toward its goal deterministically") {
    World a(7, 0), b(7, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0;
    // move unit 0 to cell (10, 2)
    mv.tx = (fix64_t)10 << 32; mv.ty = (fix64_t)2 << 32;
    a.push_command(mv, 1); b.push_command(mv, 1);
    a.advance(40); b.advance(40);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("movement is batching-invariant") {
    World a(9, 0), b(9, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0;
    mv.tx = (fix64_t)15 << 32; mv.ty = (fix64_t)15 << 32;
    a.push_command(mv, 1); b.push_command(mv, 1);
    a.advance(60);
    for (int i = 0; i < 60; ++i) b.advance(1);
    CHECK(a.state_hash() == b.state_hash());
}
```

- [ ] **Step 4: Update `sim/src/world.cpp`** — new includes, spawn, movement system. Apply:
  - Add includes at the top (after existing): `#include "sim/pathfind.h"` (and `world.h` now pulls in `map.h`).
  - Initialize the map in the constructor initializer list: `World::World(... ) : map_(map_id), rng_(seed) {` (note: `map_` must be initialized before `rng_` to match the member declaration order in `world.h`).
  - Replace `spawn` to take `CMobile` instead of `CVel`:
```cpp
EntityId World::spawn(CPos pos, CMobile mob, CUnit unit) {
    auto e = reg_.create();
    EntityId id = next_id_++;
    reg_.emplace<CId>(e, CId{id});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CMobile>(e, std::move(mob));
    reg_.emplace<CUnit>(e, unit);
    return id;
}
```
  - Replace `spawn_initial` (place two movable units; no rng draw needed, but keep the seeded `rng_` member for later plans):
```cpp
void World::spawn_initial() {
    const fix speed = fix_one / 8;   // 0.125 cell/tick
    CUnit u{/*type*/1, /*owner*/1, /*state*/SIM_STATE_IDLE, /*facing*/0, /*hp*/100, /*hp_max*/100};
    spawn(CPos{Map::cell_to_world(2), Map::cell_to_world(2)}, CMobile{speed, {}, 0}, u);
    spawn(CPos{Map::cell_to_world(3), Map::cell_to_world(3)}, CMobile{speed, {}, 0}, u);
}
```
  - Add `#include <utility>` if not present (for `std::move`).
  - Replace `step()`'s `sys_drift()` call with `sys_movement()`.
  - Replace the whole `sys_drift()` function with `sys_movement()`:
```cpp
void World::sys_movement() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CMobile>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& m = reg_.get<CMobile>(e);
        auto& p = reg_.get<CPos>(e);
        auto& u = reg_.get<CUnit>(e);
        if (m.next >= m.path.size()) { u.state = SIM_STATE_IDLE; continue; }
        const fix tx = Map::cell_to_world(m.path[m.next].x);
        const fix ty = Map::cell_to_world(m.path[m.next].y);
        const fix dx = tx - p.x, dy = ty - p.y;
        if (fix_abs(dx) <= m.speed && fix_abs(dy) <= m.speed) {
            p.x = tx; p.y = ty;                 // snap to the waypoint
            ++m.next;
            if (m.next >= m.path.size()) u.state = SIM_STATE_IDLE;
        } else {
            p.x += fix_clamp(dx, -m.speed, m.speed);
            p.y += fix_clamp(dy, -m.speed, m.speed);
            u.state = SIM_STATE_MOVING;
        }
    }
}
```

- [ ] **Step 5: Build + run.** The CMD_MOVE handler doesn't exist yet (Task 4), so the new `test_world.cpp` movement tests will produce a path-less unit (it stays put) — `state_hash` equality + batching tests still pass (deterministic no-op), and `entity_count == 2` passes. Confirm GREEN, then commit. (Full CMD_MOVE behavior is verified in Task 4.)

- [ ] **Step 6: Commit**
```bash
git add sim/include/sim/components.h sim/include/sim/world.h sim/src/world.cpp sim/tests/test_world.cpp
git commit -m "feat: CMobile + sys_movement (axis-clamped); replace drift stand-in"
```

---

## Task 4: `CMD_MOVE` handler (pathfind on command)

**Files:** Modify `sim/src/world.cpp`; create `sim/tests/test_move.cpp`.

- [ ] **Step 1: Write the failing test `sim/tests/test_move.cpp`**
```cpp
#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace { fix64_t W(int c) { return (fix64_t)c << 32; } int C(fix64_t p) { return (int)(p >> 32); } }

TEST_CASE("CMD_MOVE walks a unit to the target cell") {
    SimWorld* s = sim_create(7, 0);                 // unit 0 starts at cell (2,2)
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0; mv.tx = W(10); mv.ty = W(2);
    sim_push_command(s, &mv, 1);
    sim_advance(s, 200);                            // plenty of ticks to arrive
    SimSnapshot snap = sim_get_snapshot(s);
    // entity 0 is first (id-sorted); confirm it reached cell (10,2)
    CHECK(C(snap.entities[0].x) == 10);
    CHECK(C(snap.entities[0].y) == 2);
    CHECK(snap.entities[0].state == SIM_STATE_IDLE);   // arrived -> idle
    sim_destroy(s);
}

TEST_CASE("CMD_MOVE around the wall reaches the far side") {
    SimWorld* s = sim_create(7, 0);
    SimCommand mv{}; mv.type = CMD_MOVE; mv.player = 1; mv.unit = 0; mv.tx = W(20); mv.ty = W(10);
    sim_push_command(s, &mv, 1);
    sim_advance(s, 600);
    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(C(snap.entities[0].x) == 20);
    CHECK(C(snap.entities[0].y) == 10);
    sim_destroy(s);
}
```

- [ ] **Step 2: Run — verify it FAILS** (unit never moves — no CMD_MOVE handler yet; positions stay at cell 2).

- [ ] **Step 3: Add the `CMD_MOVE` branch in `world.cpp`'s `apply_commands_for`** (alongside the existing `CMD_STOP` branch):
```cpp
        else if (c.type == CMD_MOVE) {
            for (auto e : reg_.view<CId, CMobile, CPos>()) {
                if (reg_.get<CId>(e).id != c.unit) continue;
                const auto& p = reg_.get<CPos>(e);
                GridPos start{ Map::world_to_cell(p.x), Map::world_to_cell(p.y) };
                GridPos goal { Map::world_to_cell(c.tx), Map::world_to_cell(c.ty) };
                auto& m = reg_.get<CMobile>(e);
                m.path = find_path(map_, start, goal);
                m.next = m.path.size() > 1 ? 1 : m.path.size();  // skip the start cell
                break;
            }
        }
```
Also update the existing `CMD_STOP` branch to clear the path (stop = halt movement):
```cpp
        if (c.type == CMD_STOP) {
            for (auto e : reg_.view<CId, CMobile>())
                if (reg_.get<CId>(e).id == c.unit) {
                    auto& m = reg_.get<CMobile>(e);
                    m.path.clear(); m.next = 0;
                }
        }
```

- [ ] **Step 4: Run — verify GREEN** (unit pathfinds and arrives at the target cell in both tests).

- [ ] **Step 5: Commit**
```bash
git add sim/src/world.cpp sim/tests/test_move.cpp
git commit -m "feat: CMD_MOVE handler — A* path to target cell; CMD_STOP halts"
```

---

## Task 5: Re-pin the determinism golden for the movement scenario

The foundation's golden hash described the drift world, which no longer exists. Replace the scenario with a movement one and re-pin.

**Files:** Modify `sim/tests/test_determinism.cpp`.

- [ ] **Step 1: Replace `run_scenario` and its tests in `sim/tests/test_determinism.cpp`** with a movement scenario:
```cpp
#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
fix64_t W(int c) { return (fix64_t)c << 32; }
// Seed + CMD_MOVE orders for both units, advanced via a chunk plan summing to 200.
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);
    struct Mv { std::uint64_t t; std::uint32_t unit; int cx, cy; };
    const Mv log[] = {{1, 0, 18, 4}, {1, 1, 5, 20}, {30, 0, 2, 22}};
    for (const auto& e : log) {
        SimCommand c{}; c.type = CMD_MOVE; c.player = 1; c.unit = e.unit; c.tx = W(e.cx); c.ty = W(e.cy);
        w.push_command(c, e.t);
    }
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("movement replay is reproducible") {
    CHECK(run_scenario({200}) == run_scenario({200}));
}

TEST_CASE("movement replay is batching-invariant") {
    const std::uint64_t ref = run_scenario({200});
    CHECK(run_scenario({50, 70, 80}) == ref);
    CHECK(run_scenario({100, 100})   == ref);
    std::vector<std::uint32_t> ones(200, 1);
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("movement golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({200});
    std::printf("[determinism] movement scenario hash = 0x%016llx\n", (unsigned long long)h);
    // GOLDEN: pin after first green run; MUST match on macOS-arm64 and Windows-x64.
    CHECK(h == 0x0ull);   // <-- replace 0x0 with the observed hash, then it passes
}
```

- [ ] **Step 2: Build + run, read the printed hash.** The golden case fails (0x0 != real); the reproducible + batching cases must pass (if a batching case fails, STOP — that's a determinism bug).

- [ ] **Step 3: Pin the real hash** (replace `0x0ull`), rebuild, confirm all green.

- [ ] **Step 4: Commit**
```bash
git add sim/tests/test_determinism.cpp
git commit -m "test: re-pin determinism golden for the movement scenario (batching-invariant)"
```

- [ ] **Step 5: Mailbox heads-up (B-N, FYI/AWAIT).** In `../claude_rts-mailbox`, append to `from-B.md`: 2a (map + A* + movement + CMD_MOVE) landed; the determinism **golden hash changed** (drift stand-in replaced by real movement) to `<new hash>` — ask T to re-confirm it matches on Windows CI. Fetch-guard, push `origin mailbox`.

---

## Self-Review

**1. Spec coverage (spec §4.6 / §9 movement slice):** grid A* pathfinding (Task 2), command-driven movement (Tasks 3–4), the fixed map (Task 1), determinism preserved + re-guarded (Task 5). Harvest/production/combat/win are **2b/2c** (out of scope here, by design). ✅

**2. Placeholder scan:** No "TBD"/"handle edge cases". The golden `0x0ull` is an explicit pin-after-first-run step, not a silent gap. ✅

**3. Type consistency:** `GridPos` defined once (`map.h`), used by `pathfind.h`/`components.h`. `find_path(const Map&, GridPos, GridPos)` signature matches across header/impl/tests/world. `CMobile{speed, path, next}` fields consistent. `Map::cell_to_world`/`world_to_cell` used consistently. `World::sys_movement` replaces `sys_drift` everywhere (decl in `world.h`, def + call in `world.cpp`). `spawn` signature updated to take `CMobile` in `world.h` decl + `world.cpp` def + `spawn_initial`. ✅

**4. Determinism:** A\* uses integer costs/heuristic + deterministic open-set order; `sys_movement` is id-sorted, fix-only, no `sqrt`/division; the golden + batching tests guard it (Task 5). The `CMD_MOVE` handler iterates `view<CId,CMobile,CPos>` only to *find one entity by id* (order-independent — sets that one unit's path), so it doesn't need sorting. ✅

**5. Member init order:** `map_` is declared before `rng_` in `world.h`; the constructor initializer list must list `map_(map_id)` before `rng_(seed)` to avoid `-Wreorder`. Noted in Task 3 Step 4. ✅

---

## Notes for 2b / 2c
- **2b (Economy):** `CResource{amount}` on a node; `CHarvester{phase,carried,resource_id,hq_id,timer}`; `CMD_HARVEST` (worker: path to node → mine → path to HQ → deposit → repeat); `CProducer{queue,timer}` on the HQ; `CMD_TRAIN` (debit `resources[]`, spawn after build time at the HQ). Populate `SimSnapshot.resources[]`.
- **2c (Combat & Win):** `CWeapon{damage,range_cells,cooldown,timer,target}`; auto-acquire nearest enemy (Chebyshev range, integer); `CMD_ATTACK` / attack-move; death/cleanup (id-recycle-safe); win-check (HQ destroyed → game-over flag in the snapshot); trivial enemy AI (scripted attack-move). Also fold in the foundation's carried-forward notes (command-queue pruning, id→entity map, `Rng::range(0)` guard, `hp_max` in the hash).
