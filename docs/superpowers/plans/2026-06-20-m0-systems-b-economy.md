# M0 Systems 2b — Economy (Harvest + Production) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Add the M0 economy on top of movement — a **worker** that harvests a **resource node** and deposits at the **HQ** (raising the player's resource count), and an **HQ** that **trains** new workers for a cost.

**Architecture:** New components `CResource` (node), `CHarvester` (worker state machine), `CProducer` (HQ). New systems `sys_harvest` (drives the worker's existing `CMobile` path through TO_NODE → MINING → TO_HQ → repeat) and `sys_production` (countdown → spawn). New commands `CMD_HARVEST` and `CMD_TRAIN`. The World gains a per-player `resources_[8]` (published in `SimSnapshot.resources[]`). All systems are id-sorted and integer-only — determinism preserved; the golden hash is re-pinned for the new economy scenario.

**Tech Stack:** C++17, EnTT, doctest, CMake — unchanged. Reuses 2a's `Map`, A* (`find_path`), and `CMobile`/`sys_movement`.

**Prerequisite:** Builds on **PR #2 (movement)** merged to `main`. Branch off the merged `main` as `feat/m0-systems-b-economy`. Reuses `World`'s `step()` order, snapshot, `state_hash`, and ABI.

**Determinism rules (binding):** fixed-point/integer only (no `float`/`double` in sim state), no `sqrt`/trig, **id-sorted iteration in every system**, seeded RNG. Entity spawns during a tick get sequential ids (deterministic) and are processed from the next tick.

---

## Design decisions (M0 economy — minimal, not balanced)
- **Entity layout (replaces 2a's two generic movers):** **HQ** at cell (4,4) [owner 1, `CProducer`, hp 500], **worker** at cell (5,5) [owner 1, `CMobile`+`CHarvester`, hp 40], **resource node** at cell (8,8) [owner 0/neutral, `CResource{500}`]. Spawn order fixes ids: HQ=0, worker=1, node=2.
- **Unit type ids** (shared with the view for rendering): `TYPE_WORKER=1`, `TYPE_HQ=2`, `TYPE_RESOURCE=3` (`TYPE_SOLDIER=4` reserved for 2c). Defined in a new `sim/constants.h`; communicated to T via mailbox.
- **Harvest cycle:** worker paths to the node (A*), mines `MINE_TIME=16` ticks, picks up `LOAD=5`, paths to the HQ, deposits into `resources_[owner]`, repeats until the node is empty (then idles). Movement reuses `CMobile`/`sys_movement` — `sys_harvest` only sets paths and advances the phase on arrival. The unit `state` field (for the view) is owned by `sys_movement` (MOVING/IDLE); richer HARVESTING/CARRYING animation states are a post-M0 polish item (would need `sys_movement` to compose bits).
- **Production:** `CMD_TRAIN` on the HQ — if nothing is building and `resources_[owner] >= WORKER_COST=50`, debit 50 and start a `BUILD_TIME=48`-tick build; on completion spawn a worker at HQ-cell+(1,1). One build at a time (no queue) for M0.
- **`resources_[8]`** lives in `World`, published every tick into `SimSnapshot.resources[]`, and is **added to `state_hash`** so an economy divergence is caught directly.
- **Tick order:** `++tick → apply_commands → sys_harvest → sys_production → sys_movement → publish`. (Harvest/production set paths + spawn; movement then advances. Arrival is detected as `CMobile.next >= path.size()`.)

---

## File Structure
```
sim/include/sim/constants.h   Create — type ids + economy constants                       [B]
sim/include/sim/components.h  Modify — add CResource, CHarvester, CProducer               [B]
sim/include/sim/world.h       Modify — resources_[8], find_by_id, sys_harvest/production,
                                        spawn signature -> spawn(CPos, CUnit)             [B]
sim/src/world.cpp             Modify — spawn rework, spawn_initial, systems, commands,
                                        publish resources_, hash resources_              [B]
sim/tests/test_world.cpp      Modify — spawn layout changed (HQ/worker/node); move tests
                                        now drive the worker (id 1)                       [B]
sim/tests/test_move.cpp       Modify — drive the worker (id 1), not id 0 (now the HQ)     [B]
sim/tests/test_economy.cpp    Create — harvest + production                               [B]
sim/tests/test_determinism.cpp Modify — economy scenario + re-pinned golden               [B]
```

---

## Task 1: Economy setup — constants, resources, node, HQ, worker spawn

**Files:** create `sim/include/sim/constants.h`; modify `components.h`, `world.h`, `world.cpp`, `test_world.cpp`, `test_move.cpp`.

- [ ] **Step 1: Create `sim/include/sim/constants.h`**
```cpp
#pragma once
#include <cstdint>
namespace sim {
// Unit type ids (shared with the view for rendering).
inline constexpr std::uint16_t TYPE_WORKER   = 1;
inline constexpr std::uint16_t TYPE_HQ       = 2;
inline constexpr std::uint16_t TYPE_RESOURCE = 3;
inline constexpr std::uint16_t TYPE_SOLDIER  = 4;   // reserved for 2c
// Economy (ticks @ ~24 Hz).
inline constexpr std::uint32_t MINE_TIME   = 16;
inline constexpr std::int32_t  LOAD        = 5;
inline constexpr std::int32_t  WORKER_COST = 50;
inline constexpr std::uint32_t BUILD_TIME  = 48;
inline constexpr std::int32_t  NODE_AMOUNT = 500;
}
```

- [ ] **Step 2: Add components to `sim/include/sim/components.h`** (append inside `namespace sim`, after `CUnit`):
```cpp
struct CResource { std::int32_t amount; };

enum HarvPhase : std::uint8_t { HARV_IDLE, HARV_TO_NODE, HARV_MINING, HARV_TO_HQ };
struct CHarvester {
    std::uint8_t  phase   = HARV_IDLE;
    std::int32_t  carried = 0;
    EntityId      node    = 0;     // target resource node id
    EntityId      hq      = 0;     // home HQ id
    std::uint32_t timer   = 0;     // mining countdown
};

struct CProducer {
    std::uint16_t train_type = 0;  // 0 = idle
    std::uint32_t timer      = 0;  // build countdown
};
```

- [ ] **Step 3: Modify `sim/include/sim/world.h`** — apply these edits:
  - Add member `std::int32_t resources_[8] = {0};` after `front_{};` (in the private members).
  - Add private declarations: `entt::entity find_by_id(EntityId id);`, `void sys_harvest();`, `void sys_production();`.
  - Change the `spawn` declaration to: `entt::entity spawn(CPos pos, CUnit unit);` (it no longer takes `CMobile` — callers emplace extras).

- [ ] **Step 4: Update the failing tests.** The world no longer spawns two generic movers; id 0 is now the HQ (no `CMobile`), id 1 is the worker. Update `sim/tests/test_world.cpp` (the move tests must drive the **worker, id 1**) and `sim/tests/test_move.cpp` (drive id 1) — replace each `mv.unit = 0` with `mv.unit = 1`, and change the `entity_count` expectation to **3**. In `test_world.cpp` replace the spawn test:
```cpp
TEST_CASE("world spawns the M0 economy layout at tick 0") {
    World w(7, 0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 3);   // HQ, worker, resource node
}
```
(Keep the two movement tests but set `mv.unit = 1` in each; the worker at cell (5,5) still moves deterministically.)

- [ ] **Step 5: Modify `sim/src/world.cpp`** — includes, spawn rework, spawn_initial, find_by_id, publish + hash resources. Apply:
  - Add includes: `#include "sim/constants.h"`.
  - Replace `spawn` with the 2-arg form:
```cpp
entt::entity World::spawn(CPos pos, CUnit unit) {
    auto e = reg_.create();
    reg_.emplace<CId>(e, CId{next_id_++});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CUnit>(e, unit);
    return e;
}
```
  - Replace `spawn_initial` with the economy layout:
```cpp
void World::spawn_initial() {
    auto hq = spawn(CPos{Map::cell_to_world(4), Map::cell_to_world(4)},
                    CUnit{TYPE_HQ, 1, SIM_STATE_IDLE, 0, 500, 500});
    const EntityId hq_id = reg_.get<CId>(hq).id;
    reg_.emplace<CProducer>(hq, CProducer{});

    auto wk = spawn(CPos{Map::cell_to_world(5), Map::cell_to_world(5)},
                    CUnit{TYPE_WORKER, 1, SIM_STATE_IDLE, 0, 40, 40});
    reg_.emplace<CMobile>(wk, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CHarvester>(wk, CHarvester{HARV_IDLE, 0, 0, hq_id, 0});

    auto node = spawn(CPos{Map::cell_to_world(8), Map::cell_to_world(8)},
                      CUnit{TYPE_RESOURCE, 0, SIM_STATE_IDLE, 0, 1, 1});
    reg_.emplace<CResource>(node, CResource{NODE_AMOUNT});
}
```
  - Add `find_by_id`:
```cpp
entt::entity World::find_by_id(EntityId id) {
    for (auto e : reg_.view<CId>()) if (reg_.get<CId>(e).id == id) return e;
    return entt::null;
}
```
  - In `publish_snapshot`, replace the `resources[i] = 0` loop with: `for (int i = 0; i < 8; ++i) front_.resources[i] = resources_[i];`.
  - In `state_hash`, after the per-entity loop (before `return h.value;`), add: `for (int i = 0; i < 8; ++i) h.add_i32(resources_[i]);`.

- [ ] **Step 6: Build + run.** `find_by_id`, `sys_harvest`, `sys_production` are declared but only `find_by_id` is defined; declare the two systems as empty stubs in `world.cpp` for now so it links (they're filled in Tasks 2–3), and do NOT call them from `step()` yet:
```cpp
void World::sys_harvest() {}      // filled in Task 2
void World::sys_production() {}   // filled in Task 3
```
Confirm GREEN (entity_count==3; the worker-driven move tests pass).

- [ ] **Step 7: Commit**
```bash
git add sim/include/sim/constants.h sim/include/sim/components.h sim/include/sim/world.h sim/src/world.cpp sim/tests/test_world.cpp sim/tests/test_move.cpp
git commit -m "feat: economy setup — constants, CResource, HQ/worker/node spawn, resources_[8]"
```

---

## Task 2: Harvest (`sys_harvest` + `CMD_HARVEST`)

**Files:** modify `sim/src/world.cpp`; create `sim/tests/test_economy.cpp`.

- [ ] **Step 1: Write the failing test `sim/tests/test_economy.cpp`**
```cpp
#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace { fix64_t W(int c) { return (fix64_t)c << 32; } }

TEST_CASE("a worker harvests the node and deposits at the HQ") {
    SimWorld* s = sim_create(7, 0);          // HQ=0, worker=1, node=2
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 1);
    CHECK(sim_get_snapshot(s).resources[1] == 0);   // nothing deposited yet
    sim_advance(s, 1000);                            // enough for several round trips
    CHECK(sim_get_snapshot(s).resources[1] > 0);     // deposits happened
    sim_destroy(s);
}

TEST_CASE("harvest is deterministic and batching-invariant") {
    auto run = [](int chunk) {
        SimWorld* s = sim_create(9, 0);
        SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
        sim_push_command(s, &h, 1);
        int total = 600;
        for (int t = 0; t < total; t += chunk) sim_advance(s, chunk);
        uint64_t hash = sim_state_hash(s);
        sim_destroy(s);
        return hash;
    };
    CHECK(run(600) == run(1));
    CHECK(run(600) == run(7));
}
```

- [ ] **Step 2: Run — verify it FAILS** (`sys_harvest` is an empty stub + `CMD_HARVEST` unhandled → `resources[1]` stays 0).

- [ ] **Step 3: Implement `sys_harvest` in `world.cpp`** (replace the empty stub):
```cpp
void World::sys_harvest() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CHarvester, CMobile, CPos, CUnit>())
        order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& h = reg_.get<CHarvester>(e);
        auto& m = reg_.get<CMobile>(e);
        auto& p = reg_.get<CPos>(e);
        auto& u = reg_.get<CUnit>(e);
        const bool arrived = (m.next >= m.path.size());
        auto path_to = [&](EntityId target) {
            auto te = find_by_id(target);
            if (te == entt::null) return;
            const auto& tp = reg_.get<CPos>(te);
            m.path = find_path(map_, {Map::world_to_cell(p.x), Map::world_to_cell(p.y)},
                                     {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});
            m.next = m.path.size() > 1 ? 1 : m.path.size();
        };
        switch (h.phase) {
            case HARV_TO_NODE:
                if (arrived) { h.phase = HARV_MINING; h.timer = MINE_TIME; }
                break;
            case HARV_MINING:
                if (h.timer > 0) --h.timer;
                if (h.timer == 0) {
                    auto ne = find_by_id(h.node);
                    if (ne != entt::null && reg_.all_of<CResource>(ne)) {
                        auto& res = reg_.get<CResource>(ne);
                        const std::int32_t take = res.amount < LOAD ? res.amount : LOAD;
                        res.amount -= take; h.carried = take;
                    }
                    path_to(h.hq);
                    h.phase = HARV_TO_HQ;
                }
                break;
            case HARV_TO_HQ:
                if (arrived) {
                    resources_[u.owner] += h.carried; h.carried = 0;
                    auto ne = find_by_id(h.node);
                    const std::int32_t remaining =
                        (ne != entt::null && reg_.all_of<CResource>(ne)) ? reg_.get<CResource>(ne).amount : 0;
                    if (remaining > 0) { path_to(h.node); h.phase = HARV_TO_NODE; }
                    else { h.phase = HARV_IDLE; }
                }
                break;
            default: break;
        }
    }
}
```

- [ ] **Step 4: Add the `CMD_HARVEST` branch** in `apply_commands_for` (after the `CMD_MOVE` block):
```cpp
        else if (c.type == CMD_HARVEST) {
            auto we = find_by_id(c.unit);
            auto ne = find_by_id(c.target);
            if (we != entt::null && ne != entt::null &&
                reg_.all_of<CHarvester, CMobile, CPos>(we) && reg_.all_of<CResource, CPos>(ne)) {
                auto& h = reg_.get<CHarvester>(we);
                auto& m = reg_.get<CMobile>(we);
                const auto& p  = reg_.get<CPos>(we);
                const auto& np = reg_.get<CPos>(ne);
                h.node = c.target; h.phase = HARV_TO_NODE;
                m.path = find_path(map_, {Map::world_to_cell(p.x), Map::world_to_cell(p.y)},
                                         {Map::world_to_cell(np.x), Map::world_to_cell(np.y)});
                m.next = m.path.size() > 1 ? 1 : m.path.size();
            }
        }
```

- [ ] **Step 5: Wire `sys_harvest` into `step()`** — change the order to `++tick_; apply_commands_for(tick_); sys_harvest(); sys_movement(); publish_snapshot();` (production is added in Task 3).

- [ ] **Step 6: Run — verify GREEN** (resources rise; harvest is batching-invariant). If the batching-invariance check fails, STOP — determinism bug.

- [ ] **Step 7: Commit**
```bash
git add sim/src/world.cpp sim/tests/test_economy.cpp
git commit -m "feat: sys_harvest state machine + CMD_HARVEST (worker gathers + deposits)"
```

---

## Task 3: Production (`sys_production` + `CMD_TRAIN`)

**Files:** modify `sim/src/world.cpp`; append to `sim/tests/test_economy.cpp`.

- [ ] **Step 1: Append the failing test to `sim/tests/test_economy.cpp`**
```cpp
TEST_CASE("CMD_TRAIN debits resources and spawns a worker after the build time") {
    SimWorld* s = sim_create(7, 0);
    // harvest a while to afford a worker (cost 50)
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    sim_push_command(s, &h, 1);
    sim_advance(s, 1500);
    int32_t before = sim_get_snapshot(s).resources[1];
    REQUIRE(before >= 50);
    uint32_t count_before = sim_get_snapshot(s).count;

    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;  // HQ id 0
    sim_push_command(s, &tr, sim_current_tick(s) + 1);
    sim_advance(s, 2);
    int32_t after_train = sim_get_snapshot(s).resources[1];
    CHECK(after_train < before);                                     // train debited (cost 50 >> any 2-tick deposit)
    CHECK(after_train >= before - 50);                               // debited at most the cost (rest is harvest income)
    sim_advance(s, 60);                                              // > BUILD_TIME (48)
    CHECK(sim_get_snapshot(s).count == count_before + 1);            // a new worker spawned
    sim_destroy(s);
}

TEST_CASE("CMD_TRAIN with insufficient resources does nothing") {
    SimWorld* s = sim_create(7, 0);                                   // resources start at 0
    uint32_t count_before = sim_get_snapshot(s).count;
    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;
    sim_push_command(s, &tr, 1);
    sim_advance(s, 60);
    CHECK(sim_get_snapshot(s).resources[1] == 0);
    CHECK(sim_get_snapshot(s).count == count_before);                // no spawn
    sim_destroy(s);
}
```

- [ ] **Step 2: Run — verify it FAILS** (`sys_production` is an empty stub + `CMD_TRAIN` unhandled).

- [ ] **Step 3: Implement `sys_production` in `world.cpp`** (replace the empty stub):
```cpp
void World::sys_production() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CProducer, CPos, CUnit>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& pr = reg_.get<CProducer>(e);
        if (pr.train_type == 0) continue;
        if (pr.timer > 0) --pr.timer;
        if (pr.timer == 0) {
            const auto& hp = reg_.get<CPos>(e);
            const auto& hu = reg_.get<CUnit>(e);
            const int cx = Map::world_to_cell(hp.x) + 1, cy = Map::world_to_cell(hp.y) + 1;
            auto w = spawn(CPos{Map::cell_to_world(cx), Map::cell_to_world(cy)},
                           CUnit{TYPE_WORKER, hu.owner, SIM_STATE_IDLE, 0, 40, 40});
            reg_.emplace<CMobile>(w, CMobile{fix_one / 8, {}, 0});
            reg_.emplace<CHarvester>(w, CHarvester{HARV_IDLE, 0, 0, id, 0});
            pr.train_type = 0;
        }
    }
}
```

- [ ] **Step 4: Add the `CMD_TRAIN` branch** in `apply_commands_for` (after `CMD_HARVEST`):
```cpp
        else if (c.type == CMD_TRAIN) {
            auto he = find_by_id(c.unit);
            if (he != entt::null && reg_.all_of<CProducer, CUnit>(he)) {
                auto& pr = reg_.get<CProducer>(he);
                const auto& hu = reg_.get<CUnit>(he);
                if (pr.train_type == 0 && resources_[hu.owner] >= WORKER_COST) {
                    resources_[hu.owner] -= WORKER_COST;
                    pr.train_type = TYPE_WORKER;
                    pr.timer = BUILD_TIME;
                }
            }
        }
```

- [ ] **Step 5: Wire `sys_production` into `step()`** — final order: `++tick_; apply_commands_for(tick_); sys_harvest(); sys_production(); sys_movement(); publish_snapshot();`.

- [ ] **Step 6: Run — verify GREEN** (train debits + spawns; poor HQ can't train).

- [ ] **Step 7: Commit**
```bash
git add sim/src/world.cpp sim/tests/test_economy.cpp
git commit -m "feat: sys_production + CMD_TRAIN (HQ trains workers for a cost)"
```

---

## Task 4: Re-pin the determinism golden for the economy scenario

**Files:** modify `sim/tests/test_determinism.cpp`.

- [ ] **Step 1: Replace `run_scenario` + tests in `sim/tests/test_determinism.cpp`** with an economy scenario:
```cpp
#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);                       // HQ=0, worker=1, node=2
    SimCommand h{}; h.type = CMD_HARVEST; h.player = 1; h.unit = 1; h.target = 2;
    w.push_command(h, 1);
    SimCommand tr{}; tr.type = CMD_TRAIN; tr.player = 1; tr.unit = 0;
    w.push_command(tr, 1000);                      // affordable by then (node is close; ~80-tick cycles)
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("economy replay is reproducible") {
    CHECK(run_scenario({1600}) == run_scenario({1600}));
}

TEST_CASE("economy replay is batching-invariant") {
    const std::uint64_t ref = run_scenario({1600});
    CHECK(run_scenario({400, 600, 600}) == ref);   // sums to 1600
    CHECK(run_scenario({800, 800})      == ref);
    std::vector<std::uint32_t> ones(1600, 1);
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("economy golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({1600});
    std::printf("[determinism] economy scenario hash = 0x%016llx\n", (unsigned long long)h);
    CHECK(h == 0x0ull);   // <-- replace 0x0 with the observed hash
}
```

- [ ] **Step 2: Build + run, read the printed hash.** Reproducible + batching-invariant cases MUST pass (if any batching case fails, STOP — determinism bug). Note the printed hash.

- [ ] **Step 3: Pin the real hash** (replace `0x0ull`), rebuild, confirm all green.

- [ ] **Step 4: Commit**
```bash
git add sim/tests/test_determinism.cpp
git commit -m "test: re-pin determinism golden for the economy scenario (batching-invariant)"
```

- [ ] **Step 5: Mailbox heads-up (B-N, AWAIT).** In `../claude_rts-mailbox`, append to `from-B.md`: 2b (economy) PR'd; the determinism **golden changed again** to `<new hash>` (re-confirm on Windows CI); the new **type ids** (TYPE_WORKER=1/HQ=2/RESOURCE=3) for the view's render mapping; `resources[]` is now populated. Fetch-guard, push `origin mailbox`.

---

## Self-Review
**1. Spec coverage (spec §9 economy):** worker harvest → deposit → resources rise (Task 2), HQ trains for a cost (Task 3), `resources[]` populated + snapshot (Task 1), determinism re-guarded (Task 4). Combat/win + enemy = **2c**. ✅
**2. Placeholder scan:** golden `0x0ull` is the explicit pin-after-run step; `sys_harvest`/`sys_production` empty stubs in Task 1 are explicitly filled in Tasks 2–3 (not silent gaps). ✅
**3. Type consistency:** `spawn(CPos, CUnit)` signature updated in `world.h` decl + `world.cpp` def + both `spawn_initial`/`sys_production` callers. `CResource`/`CHarvester`/`CProducer`/`HarvPhase` defined once. `find_by_id` returns `entt::entity` (or `entt::null`). `resources_[8]` used in publish + hash + commands. Type ids + econ constants single-sourced in `constants.h`. ✅
**4. Determinism:** every system id-sorted; integer-only (resources, timers, LOAD); spawns get sequential ids; `resources_` added to the hash; batching-invariance tested with multi-command economy scenarios (Tasks 2 + 4). The `find_by_id` linear scan is order-independent (matches one id). ✅
**5. Cascading test churn (expected):** the spawn layout changed, so 2a's move tests are updated to drive the worker (id 1) and `entity_count`→3 (Task 1 Step 4); the golden is re-pinned (Task 4). Flagged, not silent.

---

## Notes for 2c (Combat & Win)
- Add enemy HQ + enemy soldier(s) + a player soldier to `spawn_initial` (ids shift again → re-pin golden once more, or establish the full layout here to avoid a 2d re-pin).
- `CWeapon{damage, range_cells, cooldown, timer, target}`; auto-acquire nearest enemy (Chebyshev range, integer); `CMD_ATTACK` / attack-move (reuse `CMobile`); death/cleanup (id not recycled); win-check (a `game_over`/`winner` field in the snapshot when an HQ dies); trivial enemy AI (scripted attack-move toward the player). Fold in the carried-forward notes (command-queue pruning, id→entity map to replace `find_by_id` scans, `Rng::range(0)` guard, `hp_max` in the hash, `CMD_STOP` test, `state_hash` contract doc).
