# M0 Sim Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the deterministic C++ simulation substrate for claude_rts and the `sim_abi.h` C-ABI that Godot loads via GDExtension — proven by a golden-replay determinism test.

**Architecture:** A headless `sim` static library (lane B) with zero engine dependencies. Game state lives in an EnTT registry advanced by a fixed-timestep tick loop. All sim state uses fixed-point (`fix` = Q32.32 in `int64`); inputs arrive as POD `SimCommand`s stamped to an execution tick; the view reads a double-buffered POD `SimSnapshot`. Determinism is the contract: same seed + same command log ⇒ bit-identical `state_hash()` across macOS (arm64) and Windows (x64). This plan builds the substrate with a trivial "drift" system as a stand-in; **Plan 2 replaces drift with real M0 gameplay systems.**

**Tech Stack:** C++17, [EnTT](https://github.com/skypjack/entt) (ECS), [doctest](https://github.com/doctest/doctest) (tests), CMake + FetchContent. SplitMix64 PRNG, FNV-1a hashing.

**Prerequisites / repo state:** Work in `sim/` on a branch `feat/m0-sim-foundation`. If `main` exists (Tien scaffolds it), branch from it and PR onto it. If `main` does not exist yet, create the initial commit on `main` first (this `sim/` tree + a `.gitignore` for `build/`), then branch — and post a one-line heads-up in the mailbox so Tien builds on top rather than racing a second root commit. **Determinism rules (from the spec §4.3) are binding in every task: fixed-point only, no `float`/`double` in sim state, no `sqrt`/trig, stable id-sorted iteration, seeded RNG only, no wall-clock.**

---

## File Structure

```
sim/
  CMakeLists.txt              builds the `sim` static lib + `sim_tests` exe (FetchContent: EnTT, doctest)
  include/sim/
    sim_abi.h                 the C ABI — the contract Tien's GDExtension links against (PUBLIC)
    fixed.h                   `fix` (Q32.32) type + add/sub/abs/clamp/sign/to_float helpers (header-only)
    rng.h                     SplitMix64 deterministic PRNG (header-only)
    hash.h                    FNV-1a accumulator (header-only)
    components.h              EnTT components: CId, CPos, CVel, CUnit
    world.h                   `sim::World` — registry, tick loop, command queue, snapshot, state hash
  src/
    world.cpp                 World implementation (tick, drift system, snapshot, hash)
    sim_abi.cpp               extern "C" wrappers over sim::World
  tests/
    doctest_main.cpp          doctest entry point (#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    test_fixed.cpp            fixed-point behavior
    test_rng.cpp              PRNG reproducibility
    test_hash.cpp             FNV-1a stability
    test_world.cpp            spawn/tick/drift/command behavior
    test_abi.cpp              C-ABI lifecycle + snapshot + command path
    test_determinism.cpp      golden-replay: same seed+commands ⇒ identical hash, run twice
```

**Responsibilities:** `fixed.h`/`rng.h`/`hash.h` are dependency-free primitives. `components.h` is plain data. `world.h`/`world.cpp` own all simulation logic. `sim_abi.*` is the only boundary to the outside world. Tests mirror each unit.

---

## Task 1: Project scaffold + build/test loop

**Files:**
- Create: `sim/CMakeLists.txt`
- Create: `sim/tests/doctest_main.cpp`
- Create: `sim/tests/test_fixed.cpp` (smoke test only for now)
- Create: `sim/src/world.cpp` (minimal, so the lib has a translation unit)
- Create: `sim/include/sim/world.h` (minimal)
- Create: `sim/.gitignore`

- [ ] **Step 1: Write `sim/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(claude_rts_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2)
FetchContent_MakeAvailable(entt)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

file(GLOB SIM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(sim STATIC ${SIM_SOURCES})
target_include_directories(sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sim PUBLIC EnTT::EnTT)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(sim_tests ${TEST_SOURCES})
target_link_libraries(sim_tests PRIVATE sim doctest::doctest)

enable_testing()
add_test(NAME sim_tests COMMAND sim_tests)
```

- [ ] **Step 2: Write `sim/.gitignore`**

```gitignore
/build/
```

- [ ] **Step 3: Write `sim/include/sim/world.h` (minimal placeholder)**

```cpp
#pragma once
namespace sim { inline int abi_version() { return 0; } }
```

- [ ] **Step 4: Write `sim/src/world.cpp` (minimal translation unit)**

```cpp
#include "sim/world.h"
// Implementation grows in later tasks.
namespace sim { /* abi_version is inline in the header */ }
```

- [ ] **Step 5: Write `sim/tests/doctest_main.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

- [ ] **Step 6: Write `sim/tests/test_fixed.cpp` (smoke test)**

```cpp
#include <doctest/doctest.h>
#include "sim/world.h"

TEST_CASE("build smoke: abi_version is callable") {
    CHECK(sim::abi_version() == 0);
}
```

- [ ] **Step 7: Configure, build, and run — verify GREEN**

Run:
```bash
cmake -S sim -B sim/build && cmake --build sim/build && ctest --test-dir sim/build --output-on-failure
```
Expected: configure downloads EnTT + doctest, build succeeds, `1 test from sim_tests` passes.

- [ ] **Step 8: Commit**

```bash
git add sim/
git commit -m "build: scaffold sim lib with EnTT + doctest"
```

---

## Task 2: Fixed-point type (`fix`, Q32.32)

**Files:**
- Create: `sim/include/sim/fixed.h`
- Replace: `sim/tests/test_fixed.cpp`

**Note on scope:** M0 uses only add/sub/abs/clamp/sign/compare on `fix` (movement is axis-clamped steps; ranges are Chebyshev). Multiply/divide need a 128-bit intermediate (`__int128` on Clang/GCC, `_mul128` on MSVC) and are **deferred to Plan 2** when real distances arrive. This validates the spec's open "fixed-point format" question: **Q32.32, no mul/div in M0.**

- [ ] **Step 1: Write the failing tests in `sim/tests/test_fixed.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/fixed.h"

using namespace sim;

TEST_CASE("fix_from_int and back") {
    CHECK(fix_from_int(0) == 0);
    CHECK(fix_from_int(5) == (fix(5) << 32));
    CHECK(fix_to_float(fix_from_int(3)) == doctest::Approx(3.0f));
    CHECK(fix_to_float(fix_one / 2) == doctest::Approx(0.5f));
}

TEST_CASE("add/sub are exact") {
    fix a = fix_from_int(2), b = fix_from_int(3);
    CHECK(a + b == fix_from_int(5));
    CHECK(b - a == fix_from_int(1));
}

TEST_CASE("abs/clamp/sign") {
    CHECK(fix_abs(fix_from_int(-4)) == fix_from_int(4));
    CHECK(fix_clamp(fix_from_int(10), fix_from_int(0), fix_from_int(5)) == fix_from_int(5));
    CHECK(fix_clamp(fix_from_int(-1), fix_from_int(0), fix_from_int(5)) == fix_from_int(0));
    CHECK(fix_sign(fix_from_int(-7)) == -1);
    CHECK(fix_sign(fix_from_int(7)) == 1);
    CHECK(fix_sign(fix(0)) == 0);
}
```

- [ ] **Step 2: Run — verify it FAILS** (`fixed.h` not found)

Run: `cmake --build sim/build && ctest --test-dir sim/build` → Expected: compile error / FAIL.

- [ ] **Step 3: Write `sim/include/sim/fixed.h`**

```cpp
#pragma once
#include <cstdint>

namespace sim {

// Q32.32 signed fixed-point. One whole unit == (1 << 32).
using fix = std::int64_t;

inline constexpr int  FIX_FRAC = 32;
inline constexpr fix  fix_one  = fix(1) << FIX_FRAC;

inline constexpr fix fix_from_int(std::int64_t i) { return i << FIX_FRAC; }

// Float conversion is for the VIEW / debug only — never feed it back into the sim.
inline float fix_to_float(fix f) {
    return static_cast<float>(static_cast<double>(f) / 4294967296.0);
}

inline constexpr fix fix_abs(fix f)  { return f < 0 ? -f : f; }
inline constexpr int fix_sign(fix f) { return (f > 0) - (f < 0); }
inline constexpr fix fix_clamp(fix v, fix lo, fix hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace sim
```

- [ ] **Step 4: Run — verify GREEN**

Run: `cmake --build sim/build && ctest --test-dir sim/build --output-on-failure` → Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add sim/include/sim/fixed.h sim/tests/test_fixed.cpp
git commit -m "feat: Q32.32 fixed-point type (add/sub/abs/clamp/sign)"
```

---

## Task 3: Deterministic PRNG (SplitMix64)

**Files:**
- Create: `sim/include/sim/rng.h`
- Create: `sim/tests/test_rng.cpp`

- [ ] **Step 1: Write the failing test `sim/tests/test_rng.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/rng.h"

using namespace sim;

TEST_CASE("same seed yields same sequence") {
    Rng a{1234}, b{1234};
    for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
}

TEST_CASE("different seeds diverge") {
    Rng a{1}, b{2};
    CHECK(a.next() != b.next());
}

TEST_CASE("range is bounded and reproducible") {
    Rng a{42}, b{42};
    CHECK(a.range(10) == b.range(10));
    CHECK(a.range(10) < 10u);
}
```

- [ ] **Step 2: Run — verify it FAILS.**

- [ ] **Step 3: Write `sim/include/sim/rng.h`**

```cpp
#pragma once
#include <cstdint>

namespace sim {

// SplitMix64 — small, fast, fully deterministic. The ONLY source of randomness in the sim.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed = 0) : state(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Unbiased-enough for gameplay; deterministic. Returns [0, n).
    std::uint32_t range(std::uint32_t n) {
        return static_cast<std::uint32_t>(next() % n);
    }
};

} // namespace sim
```

- [ ] **Step 4: Run — verify GREEN.**

- [ ] **Step 5: Commit**

```bash
git add sim/include/sim/rng.h sim/tests/test_rng.cpp
git commit -m "feat: deterministic SplitMix64 PRNG"
```

---

## Task 4: FNV-1a hash accumulator

**Files:**
- Create: `sim/include/sim/hash.h`
- Create: `sim/tests/test_hash.cpp`

- [ ] **Step 1: Write the failing test `sim/tests/test_hash.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/hash.h"
#include <cstdint>

using namespace sim;

TEST_CASE("fnv1a is order-sensitive and stable") {
    Hasher h1; h1.add_u64(1); h1.add_u64(2);
    Hasher h2; h2.add_u64(1); h2.add_u64(2);
    Hasher h3; h3.add_u64(2); h3.add_u64(1);
    CHECK(h1.value == h2.value);
    CHECK(h1.value != h3.value);
}

TEST_CASE("empty hash is the offset basis") {
    Hasher h;
    CHECK(h.value == 0xcbf29ce484222325ULL);
}
```

- [ ] **Step 2: Run — verify it FAILS.**

- [ ] **Step 3: Write `sim/include/sim/hash.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace sim {

// FNV-1a, 64-bit. Used to fingerprint sim state for replay/desync checks.
struct Hasher {
    std::uint64_t value = 0xcbf29ce484222325ULL;

    void add_bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            value ^= b[i];
            value *= 0x100000001b3ULL;
        }
    }
    void add_u64(std::uint64_t v) { add_bytes(&v, sizeof(v)); }
    void add_i64(std::int64_t v)  { add_bytes(&v, sizeof(v)); }
    void add_u32(std::uint32_t v) { add_bytes(&v, sizeof(v)); }
    void add_i32(std::int32_t v)  { add_bytes(&v, sizeof(v)); }
};

} // namespace sim
```

- [ ] **Step 4: Run — verify GREEN.**

- [ ] **Step 5: Commit**

```bash
git add sim/include/sim/hash.h sim/tests/test_hash.cpp
git commit -m "feat: FNV-1a state hasher"
```

---

## Task 5: Components + World (spawn, tick loop, drift system)

This task brings the world to life with a deterministic stand-in system (`drift`: every entity moves by its velocity each tick). The seed deterministically spawns 3 entities. **Plan 2 replaces `spawn_initial` and `sys_drift` with the real M0 map + systems.**

**Files:**
- Create: `sim/include/sim/components.h`
- Replace: `sim/include/sim/world.h`
- Replace: `sim/src/world.cpp`
- Create: `sim/tests/test_world.cpp`

- [ ] **Step 1: Write `sim/include/sim/components.h`**

```cpp
#pragma once
#include <cstdint>
#include "sim/fixed.h"

namespace sim {

using EntityId = std::uint32_t;

struct CId   { EntityId id; };                 // stable, never recycled within a match
struct CPos  { fix x, y; };
struct CVel  { fix x, y; };                    // foundation drift; repurposed in Plan 2
struct CUnit {
    std::uint16_t type;
    std::uint8_t  owner;                        // 0 = neutral
    std::uint8_t  state;                        // bitflags (idle/move/... — defined in Plan 2)
    std::uint16_t facing;                       // 0..65535 = full turn
    std::int32_t  hp, hp_max;
};

} // namespace sim
```

- [ ] **Step 2: Write the failing test `sim/tests/test_world.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/world.h"

using namespace sim;

TEST_CASE("world starts at tick 0 with deterministic entities") {
    World w(/*seed*/7, /*map_id*/0);
    CHECK(w.tick() == 0);
    CHECK(w.entity_count() == 3);
}

TEST_CASE("advance increments the tick counter") {
    World w(7, 0);
    w.advance(10);
    CHECK(w.tick() == 10);
}

TEST_CASE("drift moves entities deterministically") {
    World a(7, 0), b(7, 0);
    a.advance(5);
    b.advance(5);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("same seed, same end state regardless of step batching") {
    World a(9, 0), b(9, 0);
    a.advance(12);
    for (int i = 0; i < 12; ++i) b.advance(1);
    CHECK(a.state_hash() == b.state_hash());
}
```

- [ ] **Step 3: Run — verify it FAILS** (no `World`).

- [ ] **Step 4: Write `sim/include/sim/world.h`**

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <entt/entt.hpp>
#include "sim/rng.h"
#include "sim/sim_abi.h"   // SimCommand, SimSnapshot, SimEntitySnapshot

namespace sim {

class World {
public:
    World(std::uint64_t seed, std::uint32_t map_id);

    void advance(std::uint32_t ticks);
    std::uint64_t tick() const { return tick_; }
    std::size_t entity_count() const {
        return const_cast<entt::registry&>(reg_).view<CId>().size();
    }

    void push_command(const SimCommand& cmd, std::uint64_t exec_tick);
    const SimSnapshot& snapshot() const { return front_; }
    std::uint64_t state_hash() const;

private:
    void step();
    void apply_commands_for(std::uint64_t t);
    void sys_drift();
    void publish_snapshot();
    EntityId spawn(CPos pos, CVel vel, CUnit unit);
    void spawn_initial();      // replaced by the real map in Plan 2

    entt::registry reg_;
    std::uint64_t  tick_ = 0;
    Rng            rng_;
    EntityId       next_id_ = 0;

    std::vector<std::pair<std::uint64_t, SimCommand>> commands_;  // (exec_tick, cmd)

    std::vector<SimEntitySnapshot> buf_[2];
    int        active_ = 0;
    SimSnapshot front_{};
};

} // namespace sim
```

- [ ] **Step 5: Write `sim/src/world.cpp`**

```cpp
#include "sim/world.h"
#include "sim/components.h"
#include "sim/hash.h"
#include <algorithm>

namespace sim {

World::World(std::uint64_t seed, std::uint32_t /*map_id*/) : rng_(seed) {
    spawn_initial();
    publish_snapshot();
}

EntityId World::spawn(CPos pos, CVel vel, CUnit unit) {
    auto e = reg_.create();
    EntityId id = next_id_++;
    reg_.emplace<CId>(e, CId{id});
    reg_.emplace<CPos>(e, pos);
    reg_.emplace<CVel>(e, vel);
    reg_.emplace<CUnit>(e, unit);
    return id;
}

void World::spawn_initial() {
    // Foundation stand-in: 3 entities with rng-derived velocities. Replaced in Plan 2.
    for (int i = 0; i < 3; ++i) {
        fix vx = static_cast<fix>(rng_.next() & 0xFFFF);   // small positive drift
        fix vy = static_cast<fix>(rng_.next() & 0xFFFF);
        spawn(CPos{fix_from_int(i), fix_from_int(i)},
              CVel{vx, vy},
              CUnit{/*type*/1, /*owner*/1, /*state*/0, /*facing*/0, /*hp*/100, /*hp_max*/100});
    }
}

void World::advance(std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) step();
}

void World::step() {
    ++tick_;                    // tick 0 = initial state; advance(N) processes ticks 1..N
    apply_commands_for(tick_);  // command at exec_tick=N applies before tick N's systems (exec_tick >= 1)
    sys_drift();
    publish_snapshot();
}

void World::sys_drift() {
    // Iterate in stable id order so any future order-dependent logic is deterministic.
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        auto& p = reg_.get<CPos>(e);
        const auto& v = reg_.get<CVel>(e);
        p.x += v.x;
        p.y += v.y;
    }
}

void World::push_command(const SimCommand& cmd, std::uint64_t exec_tick) {
    commands_.push_back({exec_tick, cmd});
}

void World::apply_commands_for(std::uint64_t t) {
    // Stable order: by exec_tick already filtered to t; tie-break by player then unit.
    std::vector<SimCommand> due;
    for (auto& [et, c] : commands_) if (et == t) due.push_back(c);
    std::sort(due.begin(), due.end(), [](const SimCommand& a, const SimCommand& b) {
        if (a.player != b.player) return a.player < b.player;
        return a.unit < b.unit;
    });
    for (const auto& c : due) {
        if (c.type == CMD_STOP) {
            for (auto e : reg_.view<CId, CVel>())
                if (reg_.get<CId>(e).id == c.unit) reg_.get<CVel>(e) = CVel{0, 0};
        }
        // Other command types are handled in Plan 2.
    }
}

void World::publish_snapshot() {
    int next = 1 - active_;
    auto& out = buf_[next];
    out.clear();
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg_.view<CId>()) order.push_back({reg_.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    for (auto& [id, e] : order) {
        const auto& p = reg_.get<CPos>(e);
        const auto& u = reg_.get<CUnit>(e);
        out.push_back(SimEntitySnapshot{
            id, u.type, u.owner, u.state, p.x, p.y, u.facing, u.hp, u.hp_max});
    }
    front_.tick = tick_;
    front_.entities = out.data();
    front_.count = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 8; ++i) front_.resources[i] = 0;  // populated in Plan 2
    active_ = next;
}

std::uint64_t World::state_hash() const {
    auto& reg = const_cast<entt::registry&>(reg_);   // EnTT view()/get() are non-const
    std::vector<std::pair<EntityId, entt::entity>> order;
    for (auto e : reg.view<CId>()) order.push_back({reg.get<CId>(e).id, e});
    std::sort(order.begin(), order.end());
    Hasher h;
    h.add_u64(tick_);
    for (auto& [id, e] : order) {
        const auto& p = reg.get<CPos>(e);
        const auto& u = reg.get<CUnit>(e);
        h.add_u32(id);
        h.add_i64(p.x); h.add_i64(p.y);
        h.add_u32(u.type); h.add_u32(u.owner); h.add_u32(u.state); h.add_u32(u.facing);
        h.add_i32(u.hp);
    }
    return h.value;
}

} // namespace sim
```

> **Dependency note:** `world.h` includes `sim_abi.h` for the POD types, which are defined in **Task 7**. Pull **Task 7 Step 1** forward — create `sim/include/sim/sim_abi.h` before this task's build step; it is dependency-free. This reordering resolves the only forward dependency in the plan.

- [ ] **Step 6: Run — verify GREEN.** (Requires `sim_abi.h` from **Task 7** — create that header first, per the dependency note above.)

- [ ] **Step 7: Commit**

```bash
git add sim/include/sim/components.h sim/include/sim/world.h sim/src/world.cpp sim/tests/test_world.cpp
git commit -m "feat: World with deterministic spawn, tick loop, drift, stable-order hash"
```

---

## Task 6: Command queue execution timing

Verifies commands fire on their exact `exec_tick` and not before — the basis for lockstep input-delay.

**Files:**
- Modify: `sim/tests/test_world.cpp` (append)

- [ ] **Step 1: Append the failing test to `sim/tests/test_world.cpp`**

```cpp
TEST_CASE("CMD_STOP applies exactly at its exec_tick") {
    World w(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    w.push_command(stop, /*exec_tick*/3);

    std::uint64_t before = (w.advance(3), w.state_hash());  // entity 0 stopped at tick 3
    World ref(7, 0); ref.advance(3);                        // no command
    CHECK(before != ref.state_hash());                      // command changed state
}

TEST_CASE("a command in the future does not apply early") {
    World w(7, 0), ref(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    w.push_command(stop, /*exec_tick*/100);
    w.advance(5); ref.advance(5);
    CHECK(w.state_hash() == ref.state_hash());              // not applied yet
}
```

- [ ] **Step 2: Run — verify GREEN** (logic already implemented in Task 5; this locks the behavior). If red, fix `apply_commands_for` to filter strictly on `et == t`.

- [ ] **Step 3: Commit**

```bash
git add sim/tests/test_world.cpp
git commit -m "test: command queue fires on exact exec_tick"
```

---

## Task 7: The C ABI surface (`sim_abi.h` + `sim_abi.cpp`)

**Files:**
- Create: `sim/include/sim/sim_abi.h`
- Create: `sim/src/sim_abi.cpp`
- Create: `sim/tests/test_abi.cpp`

- [ ] **Step 1: Write `sim/include/sim/sim_abi.h` (the contract — matches mailbox B-4/B-5)**

```c
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// World units are fixed-point Q32.32 in int64 across the ABI. The view converts to
// float for rendering ONLY — it never feeds float back into the sim.
typedef int64_t sim_fix;

typedef struct {            // one per live entity per published frame (POD, AoS)
    uint32_t id;            // stable; not recycled within a match
    uint16_t type;
    uint8_t  owner;         // 0 = neutral
    uint8_t  state;         // bitflags
    sim_fix  x, y;
    uint16_t facing;        // 0..65535 = full turn
    int32_t  hp, hp_max;
} SimEntitySnapshot;

// SimEntitySnapshot.state — combinable bitflags (e.g., MOVING|CARRYING = worker hauling).
enum {
    SIM_STATE_IDLE       = 0,
    SIM_STATE_MOVING     = 1u << 0,
    SIM_STATE_ATTACKING  = 1u << 1,
    SIM_STATE_HARVESTING = 1u << 2,
    SIM_STATE_CARRYING   = 1u << 3,
    SIM_STATE_BUILDING   = 1u << 4,   // structure under construction / worker constructing
    SIM_STATE_DEAD       = 1u << 5
};

typedef struct {            // LIFETIME: valid only until the next sim_advance().
    uint64_t tick;          // The view MUST copy what it wants to keep (see mailbox B-5).
    const SimEntitySnapshot* entities;
    uint32_t count;
    int32_t  resources[8];  // per-player primary resource
} SimSnapshot;

typedef enum { CMD_MOVE, CMD_ATTACK, CMD_HARVEST, CMD_BUILD, CMD_TRAIN, CMD_STOP } SimCommandType;
typedef struct {            // POD, view -> sim
    SimCommandType type;
    uint8_t  player;
    uint32_t unit;          // primary actor (0 = none)
    uint32_t target;        // target entity (0 = none)
    sim_fix  tx, ty;        // target position (move/build)
    uint16_t param;         // unit/structure type for train/build
} SimCommand;

typedef struct SimWorld SimWorld;   // opaque handle

SimWorld*   sim_create(uint64_t seed, uint32_t map_id);
void        sim_destroy(SimWorld*);
void        sim_advance(SimWorld*, uint32_t ticks);
uint64_t    sim_current_tick(const SimWorld*);
void        sim_push_command(SimWorld*, const SimCommand*, uint64_t exec_tick);
SimSnapshot sim_get_snapshot(const SimWorld*);
uint64_t    sim_state_hash(const SimWorld*);

#ifdef __cplusplus
}
#endif
```

> The internal `World` uses `fix` (== `int64_t`, == `sim_fix`); these are layout-identical. `SimEntitySnapshot`/`SimSnapshot`/`SimCommand` are the single source of truth for those POD types, included by `world.h`.

- [ ] **Step 2: Write `sim/src/sim_abi.cpp`**

```cpp
#include "sim/sim_abi.h"
#include "sim/world.h"

using sim::World;

static World*       w(SimWorld* h)        { return reinterpret_cast<World*>(h); }
static const World* w(const SimWorld* h)  { return reinterpret_cast<const World*>(h); }

extern "C" {

SimWorld* sim_create(uint64_t seed, uint32_t map_id) {
    return reinterpret_cast<SimWorld*>(new World(seed, map_id));
}
void sim_destroy(SimWorld* h) { delete w(h); }
void sim_advance(SimWorld* h, uint32_t ticks) { w(h)->advance(ticks); }
uint64_t sim_current_tick(const SimWorld* h) { return w(h)->tick(); }
void sim_push_command(SimWorld* h, const SimCommand* c, uint64_t exec_tick) {
    w(h)->push_command(*c, exec_tick);
}
SimSnapshot sim_get_snapshot(const SimWorld* h) { return w(h)->snapshot(); }
uint64_t sim_state_hash(const SimWorld* h) { return w(h)->state_hash(); }

}
```

- [ ] **Step 3: Write `sim/tests/test_abi.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/sim_abi.h"

TEST_CASE("ABI lifecycle + advance") {
    SimWorld* s = sim_create(7, 0);
    CHECK(sim_current_tick(s) == 0);
    sim_advance(s, 4);
    CHECK(sim_current_tick(s) == 4);
    sim_destroy(s);
}

TEST_CASE("ABI snapshot reflects entities") {
    SimWorld* s = sim_create(7, 0);
    SimSnapshot snap = sim_get_snapshot(s);
    CHECK(snap.count == 3);
    CHECK(snap.entities[0].hp_max == 100);
    CHECK(snap.tick == 0);
    sim_destroy(s);
}

TEST_CASE("ABI command path changes the hash deterministically") {
    SimWorld* a = sim_create(7, 0);
    SimWorld* b = sim_create(7, 0);
    SimCommand stop{}; stop.type = CMD_STOP; stop.player = 1; stop.unit = 0;
    sim_push_command(a, &stop, 2);
    sim_advance(a, 5); sim_advance(b, 5);
    CHECK(sim_state_hash(a) != sim_state_hash(b));
    sim_destroy(a); sim_destroy(b);
}
```

- [ ] **Step 4: Run — verify GREEN.**

- [ ] **Step 5: Commit**

```bash
git add sim/include/sim/sim_abi.h sim/src/sim_abi.cpp sim/tests/test_abi.cpp
git commit -m "feat: sim_abi.h C ABI + extern C wrappers over World"
```

---

## Task 8: Golden-replay determinism harness (the cross-platform contract)

This is the payoff: a recorded seed + command log must reproduce an identical `state_hash`. The same test, green on macOS and Windows CI, is our lockstep guarantee. Tien's CI (mailbox B-4) invokes the `sim_tests` binary on both runners.

**Files:**
- Create: `sim/tests/test_determinism.cpp`

- [ ] **Step 1: Write `sim/tests/test_determinism.cpp`**

```cpp
#include <doctest/doctest.h>
#include "sim/world.h"
#include <vector>
#include <cstdio>

using namespace sim;

namespace {
// A fixed scenario: seed + a command log of (exec_tick, command).
std::uint64_t run_scenario() {
    World w(20260620ull, 0);
    struct Entry { std::uint64_t t; SimCommand c; };
    std::vector<Entry> log;
    auto stop = [](std::uint32_t unit){ SimCommand c{}; c.type = CMD_STOP; c.player = 1; c.unit = unit; return c; };
    log.push_back({2, stop(0)});
    log.push_back({5, stop(1)});
    log.push_back({9, stop(2)});
    for (auto& e : log) w.push_command(e.c, e.t);
    w.advance(50);
    return w.state_hash();
}
} // namespace

TEST_CASE("replay is reproducible within a process") {
    CHECK(run_scenario() == run_scenario());
}

TEST_CASE("golden hash is stable across platforms") {
    std::uint64_t h = run_scenario();
    std::printf("[determinism] scenario hash = 0x%016llx\n", (unsigned long long)h);
    // GOLDEN: pin this after the first green run; it MUST match on macOS-arm64 and Windows-x64.
    // Uncomment and set once observed (a mismatch across OSes is a determinism bug to fix, not to re-pin):
    // CHECK(h == 0x0000000000000000ull);
}
```

- [ ] **Step 2: Run — verify GREEN** and note the printed hash.

Run: `ctest --test-dir sim/build --output-on-failure` → Expected: PASS; capture the `[determinism] scenario hash = 0x...` line.

- [ ] **Step 3: Pin the golden hash**

Set the `CHECK(h == 0x...)` line to the observed value and un-comment it. Rebuild + rerun → PASS.

- [ ] **Step 4: Commit**

```bash
git add sim/tests/test_determinism.cpp
git commit -m "test: golden-replay determinism harness with pinned hash"
```

- [ ] **Step 5: Post a mailbox heads-up to Tien (B-N, FYI)**

In the `../claude_rts-mailbox` worktree, append to `mailbox/from-B.md`: the `sim` lib + `sim_abi.h` are ready to link, the `sim_tests` determinism binary is wired (point CI at `ctest --test-dir sim/build`), and the golden hash is pinned — ask him to confirm it matches on the Windows runner. Fetch-guard, then push `origin mailbox`.

---

## Self-Review

**1. Spec coverage (foundation slice of spec §4):**
- §4.1 ECS → Task 5 (EnTT, components). ✅
- §4.2 fixed tick loop → Task 5 (`advance`/`step`). ✅
- §4.3 determinism rules: fixed-point (Task 2), seeded RNG only (Task 3), stable id-sorted iteration (Task 5 `sys_drift`/`publish`/`state_hash`), no wall-clock (no time calls anywhere), no `sqrt`/trig (none used) ✅; **format validated** = Q32.32, no mul/div in M0 (Task 2 note) ✅.
- §4.4 commands + replay + per-tick hash → Tasks 6, 8; `state_hash` Task 5. ✅
- §4.5 seam ABI (snapshot copy semantics, stable ids) → Task 7 (`sim_abi.h` lifetime comment), Task 5 (CId never recycled). ✅
- §6 golden-replay in CI on both OSes → Task 8. ✅
- Real M0 systems (map, A*, movement, harvest, production, combat, death, win) → **Plan 2** (out of scope here, by design). ✅

**2. Placeholder scan:** No "TBD"/"handle edge cases"/"similar to". The only deferrals (`fix` mul/div, real systems, `resources[]` population, `state` bitflags) are explicitly labeled "Plan 2" — not silent gaps. ✅

**3. Type consistency:** `fix`==`int64_t`==`sim_fix` (layout-identical, noted). `World` methods (`advance`,`tick`,`push_command`,`snapshot`,`state_hash`,`entity_count`) match across `world.h`, `world.cpp`, `sim_abi.cpp`, and tests. `CId/CPos/CVel/CUnit` field names consistent. `SimCommand`/`SimSnapshot`/`SimEntitySnapshot` defined once in `sim_abi.h`, used everywhere. ✅

**Ordering fix applied:** Task 5 depends on the POD types from `sim_abi.h` (Task 7). Resolution: when executing, create `sim/include/sim/sim_abi.h` (Task 7 Step 1) **before** Task 5's build step — it is dependency-free. The plan notes this at Task 5 Step 5/6.

---

## Notes for Plan 2 (M0 Game Systems)
Replaces `spawn_initial`/`sys_drift` with: the fixed M0 map; grid A* (behind a narrow interface per spec Risks); axis-clamped movement; harvest; production (debits `resources[]`); combat (Chebyshev range, integer damage); death/cleanup; win-check. Adds `fix` mul/div (portable per-compiler 128-bit) only if real distances require it; populates `resources[]` and `CUnit.state` bitflags.

### Carried-forward review notes (from the M0-foundation final review — fix in Plan 2)
- **Command queue scaling:** `commands_` is never pruned and `apply_commands_for` linear-scans all history every tick (O(ticks×commands)). Index commands by `exec_tick` (e.g., a map or min-heap) and drop them once executed.
- **Command target lookup:** `CMD_STOP` does a full `view<CId,CVel>` scan per command to find one entity by id. Maintain an `id → entt::entity` map.
- **`Rng::range(0)` is modulo-by-zero UB** — guard it before any caller uses it.
- **`state_hash` omits `hp_max`** (hashes `hp` only) — add `hp_max` so a `hp_max` desync is caught. Also: add a one-line comment on `Hasher`'s little-endian assumption (fine for macOS-arm64 + Windows-x64), and add `-Wall -Wextra` to the `sim` CMake target.
