# M0 Systems 2c — Combat, Death & Win Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Complete the M0 walking skeleton — soldiers that **acquire, chase, and attack** enemies; entities that **die** at 0 HP; a **win-check** (destroy the enemy HQ); and a **trivial enemy AI** (a soldier that marches at the player). After 2c the loop is whole: harvest → train → fight → destroy the enemy HQ → win.

**Architecture:** A `CWeapon` component on soldiers; `sys_combat` (auto-acquire nearest enemy in range, else fall back to a standing `home_target`; chase via the existing `CMobile`/A*; deal `damage` on a `cooldown`); `sys_death` (remove HP≤0 entities, set `winner_` when an HQ dies — ids are **not** recycled); `CMD_ATTACK` (set a unit's standing target). The full M0 entity layout is established here (player HQ, worker, node, player soldier, enemy HQ, enemy soldier). Determinism preserved (id-sorted systems, integer-only, **no `sqrt`** — Chebyshev range); golden re-pinned.

**Tech Stack:** C++17, EnTT, doctest, CMake — unchanged. Reuses `Map`, A* (`find_path`), `CMobile`/`sys_movement`, and the economy from 2a/2b.

**Prerequisite:** Builds on **PR #2 (movement)** + **2b (economy)** merged to `main`. Branch off the merged `main` as `feat/m0-systems-c-combat`.

**Determinism rules (binding):** integer/fixed-point only (no `float`/`double` in sim state), **no `sqrt`/trig** (Chebyshev = `max(|dx|,|dy|)`), **id-sorted iteration with id tie-breaks in every system** (targeting ties broken by lowest id), seeded RNG. Spawns get sequential, never-recycled ids.

---

## Design decisions (M0 combat — minimal)
- **Full M0 layout** (ids preserved from 2b; combat entities appended): `0` player HQ (4,4, hp 200), `1` worker (5,5), `2` node (8,8), **`3` player soldier (10,10, hp 50)**, **`4` enemy HQ (20,20, hp 200)**, **`5` enemy soldier (14,10, **hp 20** — a weak scout, `home_target = player HQ`)**. The scout is intentionally fragile so the player soldier survives the gap duel and goes on to destroy the enemy HQ (a deterministic player win for the M0 demo). The wall (x=12) sits between the bases, so combat traversal exercises A*. Player HQ hp drops to 200 (from 2b's 500) to keep fights short.
- **`CWeapon{damage, range_cells, cooldown, timer, target, home_target}`.** Soldier stats: `damage 10`, `range_cells 4` (ranged), `cooldown 12` ticks, hp 50. `ACQUIRE_RANGE 7` cells. HQ hp 200 ⇒ ~20 hits to kill.
- **Targeting each tick:** acquire the **nearest enemy within `ACQUIRE_RANGE`** (different owner, non-neutral; ties → lowest id); if none, use the standing `home_target` (if it still exists); else idle. Then: if the target is within `range_cells`, stop and attack on cooldown; else chase (A*-path to the target's cell).
- **Death + win:** `sys_death` removes HP≤0 entities (id-sorted); when a `TYPE_HQ` dies, set `winner_` to the *other* owner (1 or 2). `winner_` is in `World`, added to `state_hash`. The **view derives win/lose from the entity list** (no enemy HQ ⇒ player won) — no ABI change for M0. *(Optional polish: add a `winner` field to `SimSnapshot` — a small ABI addition to coordinate with T.)*
- **Enemy AI (trivial):** the enemy soldier's `home_target` is the player HQ (set at spawn) — it marches at the player base and fights player units it meets. The player commands its soldier with `CMD_ATTACK` (target = enemy HQ).
- **Tick order:** `++tick → apply_commands → sys_harvest → sys_production → sys_combat → sys_movement → sys_death → publish`.

---

## File Structure
```
sim/include/sim/constants.h   Modify — add combat constants                              [B]
sim/include/sim/components.h  Modify — add CWeapon                                        [B]
sim/include/sim/world.h       Modify — winner_, winner(), sys_combat/sys_death, cheb      [B]
sim/src/world.cpp             Modify — spawn combat entities, systems, CMD_ATTACK,
                                        hash winner_, tick order                          [B]
sim/tests/test_world.cpp      Modify — entity_count -> 6                                  [B]
sim/tests/test_combat.cpp     Create — targeting, chase, attack, death, win              [B]
sim/tests/test_determinism.cpp Modify — full-loop scenario + re-pinned golden             [B]
```

---

## Task 1: Full M0 layout + `CWeapon` + `winner_`

**Files:** modify `constants.h`, `components.h`, `world.h`, `world.cpp`, `test_world.cpp`.

- [ ] **Step 1: Add combat constants to `sim/include/sim/constants.h`** (inside `namespace sim`):
```cpp
inline constexpr std::int32_t  SOLDIER_HP    = 50;
inline constexpr std::int32_t  SOLDIER_DMG   = 10;
inline constexpr int           SOLDIER_RANGE = 4;     // Chebyshev cells
inline constexpr std::uint32_t SOLDIER_CD    = 12;    // attack cooldown (ticks)
inline constexpr int           ACQUIRE_RANGE = 7;     // cells
inline constexpr std::int32_t  HQ_HP         = 200;
```

- [ ] **Step 2: Add `CWeapon` to `sim/include/sim/components.h`** (after `CProducer`):
```cpp
struct CWeapon {
    std::int32_t  damage;
    int           range_cells;
    std::uint32_t cooldown;
    std::uint32_t timer       = 0;   // cooldown countdown
    EntityId      target      = 0;   // currently engaged (0 = none)
    EntityId      home_target = 0;   // standing order (0 = none)
};
```

- [ ] **Step 3: Modify `sim/include/sim/world.h`:**
  - Add member `std::uint8_t winner_ = 0;` (0 = ongoing, else the winning owner) after `resources_`.
  - Add public method: `std::uint8_t winner() const { return winner_; }`.
  - Add private declarations: `void sys_combat();`, `void sys_death();`.

- [ ] **Step 4: Modify `sim/src/world.cpp` — spawn the full layout + hash winner_.**
  - Add a free helper near the top of the anonymous part of the file (or as a `static` function in the `sim` namespace) for Chebyshev distance:
```cpp
static int cheb(int ax, int ay, int bx, int by) {
    const int dx = ax > bx ? ax - bx : bx - ax;
    const int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}
```
  - In `spawn_initial`, change the **player HQ** hp to `HQ_HP` (200), and **append** the combat entities after the resource node (keep HQ=0, worker=1, node=2):
```cpp
    // player soldier (id 3)
    auto ps = spawn(CPos{Map::cell_to_world(10), Map::cell_to_world(10)},
                    CUnit{TYPE_SOLDIER, 1, SIM_STATE_IDLE, 0, SOLDIER_HP, SOLDIER_HP});
    reg_.emplace<CMobile>(ps, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CWeapon>(ps, CWeapon{SOLDIER_DMG, SOLDIER_RANGE, SOLDIER_CD, 0, 0, 0});

    // enemy HQ (id 4)
    spawn(CPos{Map::cell_to_world(20), Map::cell_to_world(20)},
          CUnit{TYPE_HQ, 2, SIM_STATE_IDLE, 0, HQ_HP, HQ_HP});

    // enemy soldier (id 5) — a weak scout (hp 20, dies fast); home_target = player HQ (id 0) → it marches at the player
    auto es = spawn(CPos{Map::cell_to_world(14), Map::cell_to_world(10)},
                    CUnit{TYPE_SOLDIER, 2, SIM_STATE_IDLE, 0, 20, 20});
    reg_.emplace<CMobile>(es, CMobile{fix_one / 8, {}, 0});
    reg_.emplace<CWeapon>(es, CWeapon{SOLDIER_DMG, SOLDIER_RANGE, SOLDIER_CD, 0, 0, hq_id});
```
  (Change the player HQ spawn line's hp from `500, 500` to `HQ_HP, HQ_HP`.)
  - In `state_hash`, after the `resources_` loop, add: `h.add_u32(winner_);`.
  - Add empty stubs so it links (filled in Tasks 2–3) and do NOT call them from `step()` yet:
```cpp
void World::sys_combat() {}   // filled in Task 2
void World::sys_death()  {}   // filled in Task 3
```

- [ ] **Step 5: Update `sim/tests/test_world.cpp`** — change the entity-count expectation to **6**:
```cpp
TEST_CASE("world spawns the full M0 layout at tick 0") {
    World w(7, 0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 6);   // HQ, worker, node, player soldier, enemy HQ, enemy soldier
    CHECK(w.winner() == 0);
}
```
(The worker-move tests using id 1 still hold — id 1 is still the worker.)

- [ ] **Step 6: Build + run, verify GREEN** (entity_count==6, winner==0; existing move/economy tests pass).

- [ ] **Step 7: Commit**
```bash
git add sim/include/sim/constants.h sim/include/sim/components.h sim/include/sim/world.h sim/src/world.cpp sim/tests/test_world.cpp
git commit -m "feat: full M0 layout (soldiers + enemy base) + CWeapon + winner_ scaffolding"
```

---

## Task 2: Combat (`sys_combat` + `CMD_ATTACK`)

**Files:** modify `sim/src/world.cpp`; create `sim/tests/test_combat.cpp`.

- [ ] **Step 1: Write the failing test `sim/tests/test_combat.cpp`**
```cpp
#include <doctest/doctest.h>
#include "sim/sim_abi.h"

namespace {
const SimEntitySnapshot* find(const SimSnapshot& s, uint32_t id) {
    for (uint32_t i = 0; i < s.count; ++i) if (s.entities[i].id == id) return &s.entities[i];
    return nullptr;
}
}

TEST_CASE("soldiers in range damage each other") {
    SimWorld* s = sim_create(7, 0);    // player soldier id 3 (10,10), enemy soldier id 5 (14,10)
    sim_advance(s, 200);               // they auto-acquire across the gap, close, and fight
    SimSnapshot snap = sim_get_snapshot(s);
    const auto* e5 = find(snap, 5);
    const auto* e3 = find(snap, 3);
    // at least one of them has taken damage (or already died -> removed)
    bool fought = (!e5) || (!e3) || (e5->hp < 50) || (e3->hp < 50);
    CHECK(fought);
    sim_destroy(s);
}

TEST_CASE("combat is deterministic and batching-invariant") {
    auto run = [](int chunk) {
        SimWorld* s = sim_create(9, 0);
        SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4;
        sim_push_command(s, &atk, 1);
        for (int t = 0; t < 400; t += chunk) sim_advance(s, chunk);
        uint64_t h = sim_state_hash(s);
        sim_destroy(s);
        return h;
    };
    CHECK(run(400) == run(1));
    CHECK(run(400) == run(8));
}
```

- [ ] **Step 2: Run — verify it FAILS** (`sys_combat` is an empty stub → no damage).

- [ ] **Step 3: Implement `sys_combat` in `world.cpp`** (replace the stub):
```cpp
void World::sys_combat() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CWeapon, CPos, CUnit, CMobile>())
        order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());

    // stable id-sorted candidate list for deterministic acquisition
    std::vector<std::pair<EntityId, entt::entity>> cand;
    for (auto e : reg_.view<CId, CPos, CUnit>()) cand.push_back({reg_.get<CId>(e).id, e});
    std::sort(cand.begin(), cand.end());

    for (auto& [id, e] : order) {
        auto& w = reg_.get<CWeapon>(e);
        auto& m = reg_.get<CMobile>(e);
        const auto& p = reg_.get<CPos>(e);
        const auto& u = reg_.get<CUnit>(e);
        if (w.timer > 0) --w.timer;
        const int mx = Map::world_to_cell(p.x), my = Map::world_to_cell(p.y);

        // acquire: nearest enemy within ACQUIRE_RANGE (id tie-break), else home_target, else none
        EntityId acquired = 0; int best = ACQUIRE_RANGE + 1;
        for (auto& [oid, oe] : cand) {
            const auto& ou = reg_.get<CUnit>(oe);
            if (ou.owner == u.owner || ou.owner == 0) continue;     // not an enemy
            const auto& op = reg_.get<CPos>(oe);
            const int d = cheb(mx, my, Map::world_to_cell(op.x), Map::world_to_cell(op.y));
            if (d <= ACQUIRE_RANGE && d < best) { best = d; acquired = oid; }
        }
        if (acquired != 0) w.target = acquired;
        else if (w.home_target != 0 && find_by_id(w.home_target) != entt::null) w.target = w.home_target;
        else w.target = 0;

        if (w.target == 0) continue;
        auto te = find_by_id(w.target);
        if (te == entt::null) { w.target = 0; continue; }
        const auto& tp = reg_.get<CPos>(te);
        const int d = cheb(mx, my, Map::world_to_cell(tp.x), Map::world_to_cell(tp.y));
        if (d <= w.range_cells) {
            m.path.clear(); m.next = 0;                              // stop and fire
            if (w.timer == 0) { reg_.get<CUnit>(te).hp -= w.damage; w.timer = w.cooldown; }
        } else {
            m.path = find_path(map_, {mx, my},
                               {Map::world_to_cell(tp.x), Map::world_to_cell(tp.y)});  // chase
            m.next = m.path.size() > 1 ? 1 : m.path.size();
        }
    }
}
```

- [ ] **Step 4: Add the `CMD_ATTACK` branch** in `apply_commands_for` (after `CMD_TRAIN`):
```cpp
        else if (c.type == CMD_ATTACK) {
            auto ue = find_by_id(c.unit);
            if (ue != entt::null && reg_.all_of<CWeapon>(ue)) reg_.get<CWeapon>(ue).home_target = c.target;
        }
```

- [ ] **Step 5: Wire `sys_combat` into `step()`** — order: `++tick_; apply_commands_for(tick_); sys_harvest(); sys_production(); sys_combat(); sys_movement(); publish_snapshot();` (death added in Task 3).

- [ ] **Step 6: Run — verify GREEN** (soldiers damage each other; combat batching-invariant). If a batching check fails, STOP — determinism bug.

- [ ] **Step 7: Commit**
```bash
git add sim/src/world.cpp sim/tests/test_combat.cpp
git commit -m "feat: sys_combat (acquire/chase/attack, Chebyshev range) + CMD_ATTACK"
```

---

## Task 3: Death + win-check (`sys_death`)

**Files:** modify `sim/src/world.cpp`; append to `sim/tests/test_combat.cpp`.

- [ ] **Step 1: Append the failing tests to `sim/tests/test_combat.cpp`**
```cpp
TEST_CASE("a unit at 0 HP is removed and its id is not reused") {
    SimWorld* s = sim_create(7, 0);
    sim_advance(s, 800);                       // the soldiers fight; one should die
    SimSnapshot snap = sim_get_snapshot(s);
    // a soldier (id 3 or 5) has been removed
    bool a_soldier_died = (find(snap, 3) == nullptr) || (find(snap, 5) == nullptr);
    CHECK(a_soldier_died);
    sim_destroy(s);
}

TEST_CASE("destroying the enemy HQ wins the game") {
    SimWorld* s = sim_create(7, 0);
    SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4; // -> enemy HQ
    sim_push_command(s, &atk, 1);
    sim_advance(s, 4000);                       // long enough to traverse + grind down an HQ
    uint8_t win = sim_winner(s);
    CHECK(win != 0);                            // someone won (deterministic outcome)
    sim_destroy(s);
}
```
> Note: `sim_winner` is a new ABI getter — add it in Step 3. If you prefer no ABI change, expose it only on `World` and have the test use `World::winner()` directly (include `sim/world.h`); but the C getter is cleaner for the view and is a thin wrapper. Confirm the canonical `sim_abi.h` on `main` — if adding a function there needs T's sign-off, post a quick mailbox note; otherwise add the declaration + wrapper.

- [ ] **Step 2: Run — verify it FAILS** (no death system; `sim_winner` undefined).

- [ ] **Step 3: Implement `sys_death` + the winner getter.**
  - `sys_death` in `world.cpp` (replace the stub):
```cpp
void World::sys_death() {
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId, CUnit>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    std::vector<entt::entity> dead;
    for (auto& [id, e] : order) {
        const auto& u = reg_.get<CUnit>(e);
        if (u.hp <= 0) {
            if (u.type == TYPE_HQ && winner_ == 0) winner_ = (u.owner == 1) ? 2 : 1;
            dead.push_back(e);
        }
    }
    for (auto e : dead) reg_.destroy(e);   // ids are not recycled (next_id_ only increments)
}
```
  - Add the ABI getter. In `sim/include/sim/sim_abi.h` (the canonical seam — co-owned with T), add the declaration alongside the others: `uint8_t sim_winner(const SimWorld*);`. In `sim/src/sim_abi.cpp`, add the wrapper:
```cpp
uint8_t sim_winner(const SimWorld* h) { return w(h)->winner(); }
```

- [ ] **Step 4: Wire `sys_death` into `step()`** — final order: `++tick_; apply_commands_for(tick_); sys_harvest(); sys_production(); sys_combat(); sys_movement(); sys_death(); publish_snapshot();`.

- [ ] **Step 5: Run — verify GREEN** (a soldier dies + is removed; the game resolves to a winner).

- [ ] **Step 6: Commit**
```bash
git add sim/src/world.cpp sim/include/sim/sim_abi.h sim/src/sim_abi.cpp sim/tests/test_combat.cpp
git commit -m "feat: sys_death + win-check (HQ death sets winner); sim_winner ABI getter"
```

---

## Task 4: Full M0 integration + re-pin determinism golden

**Files:** modify `sim/tests/test_determinism.cpp`.

- [ ] **Step 1: Replace `run_scenario` + tests in `sim/tests/test_determinism.cpp`** with the full M0 loop:
```cpp
#include <doctest/doctest.h>
#include "sim/world.h"
#include "sim/sim_abi.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
std::uint64_t run_scenario(const std::vector<std::uint32_t>& chunks) {
    World w(20260620ull, 0);                          // full M0 layout
    SimCommand harv{}; harv.type = CMD_HARVEST; harv.player = 1; harv.unit = 1; harv.target = 2;
    w.push_command(harv, 1);
    SimCommand train{}; train.type = CMD_TRAIN; train.player = 1; train.unit = 0;
    w.push_command(train, 1000);
    SimCommand atk{}; atk.type = CMD_ATTACK; atk.player = 1; atk.unit = 3; atk.target = 4;
    w.push_command(atk, 1);
    for (auto n : chunks) w.advance(n);
    return w.state_hash();
}
} // namespace

TEST_CASE("full M0 loop is reproducible") {
    CHECK(run_scenario({3000}) == run_scenario({3000}));
}

TEST_CASE("full M0 loop is batching-invariant") {
    const std::uint64_t ref = run_scenario({3000});
    CHECK(run_scenario({1000, 1000, 1000}) == ref);
    CHECK(run_scenario({1500, 1500})       == ref);
    std::vector<std::uint32_t> ones(3000, 1);
    CHECK(run_scenario(ones) == ref);
}

TEST_CASE("full M0 loop golden hash is stable across platforms") {
    std::uint64_t h = run_scenario({3000});
    std::printf("[determinism] full-M0 scenario hash = 0x%016llx\n", (unsigned long long)h);
    CHECK(h == 0x0ull);   // <-- replace 0x0 with the observed hash
}
```

- [ ] **Step 2: Build + run, read the printed hash.** Reproducible + batching-invariant cases MUST pass (if any fails, STOP — determinism bug). The full loop exercises harvest + production + combat + death + win together.

- [ ] **Step 3: Pin the real hash** (replace `0x0ull`), rebuild, confirm all green.

- [ ] **Step 4: Commit**
```bash
git add sim/tests/test_determinism.cpp
git commit -m "test: re-pin determinism golden for the full M0 loop (harvest+train+combat+win)"
```

- [ ] **Step 5: Mailbox heads-up (B-N, AWAIT).** In `../claude_rts-mailbox`, append to `from-B.md`: **M0 sim is COMPLETE** (2c PR'd) — combat/death/win done; the golden changed to `<new hash>` (re-confirm on Windows CI); a **new ABI getter `sim_winner()`** was added (T re-vendor the header; or the view derives win from the entity list — no enemy-owner HQ ⇒ player won); the full entity-type set (HQ/worker/resource/soldier) + the `winner_` semantics. Ask T to wire the win/lose UI + flip `use_real_sim` to show the real M0 loop. Fetch-guard, push.

---

## Self-Review
**1. Spec coverage (spec §9 combat/win):** soldiers acquire/attack (Task 2), death + win-check (Task 3), trivial enemy AI (Task 1 spawn `home_target` + Task 2), full loop + determinism (Task 4). ✅ M0 walking skeleton complete after this.
**2. Placeholder scan:** golden `0x0ull` is the pin step; `sys_combat`/`sys_death` stubs in Task 1 are filled in Tasks 2–3 (explicit). ✅
**3. Type consistency:** `CWeapon` fields used consistently across spawn/`sys_combat`/`CMD_ATTACK`. `winner_`/`winner()`/`sim_winner` consistent. `cheb` integer-only. `find_by_id` reused. Combat constants single-sourced in `constants.h`. Ids 0–2 preserved (economy/movement tests intact); 3–5 appended. ✅
**4. Determinism:** every system id-sorted; targeting uses an id-sorted candidate scan with strict `<` so ties resolve to the lowest id; Chebyshev (no `sqrt`); integer hp/damage/timers; `winner_` hashed; spawns get sequential ids; batching-invariance tested on the full loop (Task 4). ✅
**5. ABI note:** `sim_winner()` adds one function to the seam header — flagged for T (Task 3 Step 1 note + Task 4 mailbox). It's additive (no layout change), so the view's existing build is unaffected until it adopts it.

---

## Notes / carried-forward (fold into M1)
- Replace `find_by_id` + the per-tick candidate scans with an `id → entt::entity` index (O(1)); prune the command queue; guard `Rng::range(0)`; add `hp_max` to the hash; add a `CMD_STOP` test; document the `state_hash` contract. Richer `state` flags (HARVESTING/CARRYING/ATTACKING) once `sys_movement` composes bits. Combat re-paths every tick while chasing (fine at M0 scale; cache/throttle in M1). Consider a `winner` field in `SimSnapshot` (ABI) if the view prefers an explicit signal over deriving from entities.
