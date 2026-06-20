# Foundation Batch 1 — Sim Lane Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) tracking. Spec: `docs/superpowers/specs/2026-06-20-foundation-batch1-design.md`.

**Goal:** Add SC2-style unit orders (passive move / attack-move / hold / patrol / stop) and a sim→view event channel (attack-hit / trained / died) to the deterministic sim, plus generate placeholder SFX WAVs. View-lane (hotkeys/zoom/SFX playback) is Tien's, tracked separately.

**Architecture:** New `COrder` component is the single source of unit intent; `sys_combat` gates acquisition on stance (passive/defensive/aggressive); `sys_movement` drives movement from `COrder`. A `World` event queue drained over a new ABI call. All deterministic (no float/trig, id-sorted iteration, orders in `state_hash`).

**Tech:** C++17, EnTT, doctest. Build: `cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release && cmake --build sim/build -j8 && ctest --test-dir sim/build --output-on-failure`. Branch: `feat/foundation-batch1-sim` (already checked out, spec committed).

**Determinism:** Existing goldens that depend on combat/movement (`0xec6d7413e5a86926` M0 in `test_determinism.cpp`, `0x805b7bda1d922368` soldier in `test_train.cpp`, plus any in `test_move.cpp`/`test_combat.cpp`) **will change** because move becomes passive and idle becomes defensive. Re-pin each by reading the actual hash from a failing run. Events are NOT in `state_hash`. Every re-pinned scenario keeps a batching-invariance check.

---

### Task 1: Sim event channel (additive, independent — do first so the ABI lands for Tien)

**Files:** Modify `sim/include/sim/sim_abi.h`, `sim/include/sim/world.h`, `sim/src/world.cpp`, `sim/src/sim_abi.cpp`. Create `sim/tests/test_events.cpp`.

**ABI (`sim_abi.h`)** — add after `SimMapInfo`:
```c
typedef enum { SIM_EVT_ATTACK = 1, SIM_EVT_TRAINED = 2, SIM_EVT_DIED = 3 } SimEventType;
typedef struct { uint16_t type; uint32_t a, b; uint64_t tick; } SimEvent; /* a,b = entity ids; see drain */
uint32_t sim_drain_events(SimWorld*, SimEvent* out, uint32_t max); /* copies <=max accumulated events, clears queue, returns count */
```
`World` (`world.h`): add `std::vector<SimEvent> events_;`, `void emit_event(std::uint16_t type, std::uint32_t a, std::uint32_t b);` (private), `std::uint32_t drain_events(SimEvent* out, std::uint32_t max);` (public). `emit_event` pushes `{type, a, b, tick_}` and caps `events_` at 4096 (drop new beyond cap). `drain_events` copies up to `max`, erases copied, returns count.

**Emit points (`world.cpp`):**
- `sys_combat`, right after `reg_.get<CUnit>(te).hp -= w.damage;` → `emit_event(SIM_EVT_ATTACK, id, w.target);`
- `sys_production`, right after spawning a unit → `emit_event(SIM_EVT_TRAINED, id, reg_.get<CId>(<spawned>).id);`
- `sys_death`, when a unit is marked dead → `emit_event(SIM_EVT_DIED, id, 0);` (killer attribution = 0 for now).

ABI wrapper (`sim_abi.cpp`): `uint32_t sim_drain_events(SimWorld* h, SimEvent* out, uint32_t max){ return w(h)->drain_events(out, max); }`.

- [ ] **Step 1 — failing test** (`test_events.cpp`): a scenario that trains a worker + runs combat to a kill, draining each tick into a buffer; assert at least one `SIM_EVT_ATTACK`, one `SIM_EVT_TRAINED`, one `SIM_EVT_DIED` appear; assert a second drain immediately after returns 0 (queue cleared). Build → fails (no `sim_drain_events`).
- [ ] **Step 2 — implement** the queue + emits + ABI. Build → test passes.
- [ ] **Step 3 — determinism test:** run the same fixed scenario twice draining all events into a list of `(type,a,b,tick)`; assert the two lists are identical; assert the list is identical whether advanced in 1-tick or 7-tick batches (batching-invariant event stream). 
- [ ] **Step 4 — regression:** full `ctest` green; **existing goldens unchanged** (`0xec6d7413e5a86926` etc. still pass — events aren't hashed).
- [ ] **Step 5 — commit:** `feat: sim event channel (attack/trained/died) + sim_drain_events ABI`.

---

### Task 2: `COrder` foundation — passive move, defensive idle/stop, orders-cancel-harvest, CMD_ATTACK→target

**Files:** Modify `sim/include/sim/components.h` (add `COrder`/`OrderKind`; drop `CWeapon.home_target`), `world.h`, `world.cpp`. Update `sim/tests/test_move.cpp`, `test_combat.cpp`, `test_determinism.cpp`, `test_train.cpp` (re-pin). Create `sim/tests/test_orders.cpp`.

**Component (`components.h`):**
```cpp
enum OrderKind : std::uint8_t { ORD_STOP, ORD_MOVE, ORD_ATTACK_MOVE, ORD_HOLD, ORD_PATROL, ORD_ATTACK_TARGET };
struct COrder {
    OrderKind kind = ORD_STOP;
    GridPos   dest{};         // move/attack-move/current patrol leg
    GridPos   anchor{};       // patrol other endpoint
    bool      to_dest = true; // patrol leg direction
    EntityId  target = 0;     // ORD_ATTACK_TARGET / current forced target
};
```
Remove `home_target` from `CWeapon` (its role moves to `COrder.target` + `kind`). Attach `COrder{}` (default `ORD_STOP`) to every unit that gets a `CMobile` in `spawn_initial` (workers, soldiers). The enemy scout's old `home_target = player HQ` becomes `COrder{ORD_ATTACK_TARGET, .target = hq_id}`.

**Command remap (`apply_commands_for`):** add a helper `set_order(EntityId, COrder)` that also **cancels harvest** (`if has<CHarvester>: phase = HARV_IDLE`). 
- `CMD_MOVE` → `ORD_MOVE` (dest = goal cell), path via A* (as today), cancel harvest.
- `CMD_STOP` → `ORD_STOP`, clear path + `COrder.target`, cancel harvest.
- `CMD_ATTACK` (target) → `ORD_ATTACK_TARGET` (target = c.target), cancel harvest.
- `CMD_HARVEST` unchanged behavior, but set `COrder.kind = ORD_MOVE`-equivalent off (harvest drives its own movement; leave order `ORD_STOP` and let the harvester own the path — verify no conflict; the harvest state machine should run only when `phase != HARV_IDLE`).

**`sys_combat` stance gating** — replace the unconditional acquire/chase with:
- Compute `aggressive = (kind==ORD_ATTACK_MOVE || kind==ORD_PATROL || kind==ORD_ATTACK_TARGET)`, `passive = (kind==ORD_MOVE)`.
- `passive` → `w.target = 0; continue;` (ignore enemies; movement handled by sys_movement).
- `ORD_ATTACK_TARGET` → target = `order.target` (if alive), chase + attack (as today's home_target path).
- `aggressive` (attack-move/patrol) → acquire nearest enemy in `ACQUIRE_RANGE` (today's logic); if found, chase+attack; **if none, do NOT clear the move path** (let sys_movement continue toward `dest`).
- defensive (`ORD_STOP`/`ORD_HOLD`) → acquire nearest enemy but **only attack if within `w.range_cells`**; never set a chase path (never self-move).

**`sys_movement`** — when a unit's path is exhausted: if `kind==ORD_MOVE||ORD_ATTACK_MOVE`, set `ORD_STOP` (arrived). (Patrol handled in Task 4.) When aggressive and no combat target, ensure a path toward `dest` exists (re-path if empty).

- [ ] **Step 1 — `test_orders.cpp` failing tests:** (a) passive move past an adjacent enemy: a soldier `CMD_MOVE`-ing past the enemy scout does NOT attack it (scout hp unchanged) and reaches dest; (b) defensive idle: an idle soldier with an enemy at distance 2 (≤ range 4) DOES damage it, but an idle soldier with an enemy at distance 6 (> range 4, ≤ acquire 7) does NOT move toward it; (c) `CMD_STOP` on a moving unit halts it and clears target; (d) `CMD_MOVE` on a harvesting worker cancels harvest (worker goes to dest, stops depositing). Build → fail.
- [ ] **Step 2 — implement** `COrder` + remaps + `sys_combat` gating + `sys_movement`. Build → `test_orders` passes.
- [ ] **Step 3 — fold orders into `state_hash`:** hash `COrder.kind` + `target` (NOT transient pathing) so order state is part of determinism. Keep id-sorted.
- [ ] **Step 4 — update + re-pin existing tests:** `test_move.cpp`/`test_combat.cpp` assertions that assumed aggressive-move must issue `CMD_ATTACK`/(Task 3's `CMD_ATTACK_MOVE`) explicitly; re-pin `test_determinism.cpp` and `test_train.cpp` goldens (read actual from failing run). Keep each scenario's batching-invariance check. Full `ctest` green.
- [ ] **Step 5 — commit:** `feat: COrder unit orders — passive move, defensive idle, stop clears target, orders cancel harvest`.

---

### Task 3: Attack-move + Hold commands

**Files:** `sim_abi.h` (append `CMD_ATTACK_MOVE`, `CMD_HOLD` to `SimCommandType`), `world.cpp` (handlers), `test_orders.cpp` (add cases).

- `CMD_ATTACK_MOVE` → `set_order(ORD_ATTACK_MOVE, dest = goal cell)`, path toward dest; sys_combat aggressive-acquires en route, resumes to dest after kills; on arrival with no target → `ORD_STOP`.
- `CMD_HOLD` → `set_order(ORD_HOLD)`, clear path; unit attacks in weapon range, never moves.

- [ ] **Step 1 — failing tests:** attack-move: a soldier `CMD_ATTACK_MOVE` toward a point past the enemy scout engages + kills it, then continues to the point. Hold: a soldier on `CMD_HOLD` with an enemy approaching attacks it only within range and never steps toward it.
- [ ] **Step 2 — implement.** Tests pass.
- [ ] **Step 3 — re-pin** any golden touched; full `ctest` green.
- [ ] **Step 4 — commit:** `feat: CMD_ATTACK_MOVE + CMD_HOLD orders`.

---

### Task 4: Patrol command

**Files:** `sim_abi.h` (append `CMD_PATROL`), `world.cpp` (handler + patrol leg-flip in `sys_movement`), `test_orders.cpp`.

- `CMD_PATROL` → `set_order(ORD_PATROL, dest = goal cell, anchor = current cell, to_dest = true)`, path to dest. In `sys_movement`, when an `ORD_PATROL` unit's path is exhausted (and not in combat), flip `to_dest` and re-path to the other endpoint; loop forever. sys_combat aggressive-acquires en route; after a kill, resume toward the current leg endpoint.

- [ ] **Step 1 — failing test:** a unit on `CMD_PATROL` between A and B oscillates (reaches B, heads back toward A) over enough ticks; and engages an enemy placed on the route. Assert position returns near A after a full loop (batching-invariant).
- [ ] **Step 2 — implement.** Test passes.
- [ ] **Step 3 — re-pin** if touched; full `ctest` green.
- [ ] **Step 4 — commit:** `feat: CMD_PATROL order (loop endpoints, engage en route)`.

---

### Task 5: Placeholder SFX WAVs (independent; can run anytime)

**Files:** Create `tools/gen_sfx.py` (pure-stdlib `wave`+`math`, no deps) and the generated `game/assets/sfx/{cmd_move,cmd_attack,cmd_build,hit,train_done,death}.wav`.

Synthesize short (~0.1–0.3s, 22050 Hz, 16-bit mono) greybox sounds: `cmd_move` soft sine blip; `cmd_attack` sharper square blip; `cmd_build` two-tone rising; `hit` short noise burst with fast decay; `train_done` rising two-note chime; `death` descending blip. Keep amplitudes modest (avoid clipping). Commit both the script and the WAVs (so CI/headless and Tien have them without running Python).

- [ ] **Step 1 — write `tools/gen_sfx.py`** (a function per sound writing a WAV via the `wave` module).
- [ ] **Step 2 — run it** (`python3 tools/gen_sfx.py`), verify 6 WAVs exist in `game/assets/sfx/` and are non-empty/valid (e.g. `python3 -c "import wave; [print(f, wave.open('game/assets/sfx/'+f).getnframes()) for f in [...]]"`).
- [ ] **Step 3 — commit:** `assets: synth placeholder SFX (move/attack/build/hit/train/death) + generator`.

---

## After all tasks
- Full `ctest` green (all goldens re-pinned + batching-invariant); push branch; open PR to `main`; CI matrix confirms cross-platform. Then mailbox T: new commands + events + WAVs are on `main`, re-vendor `sim_abi.h`, wire the bridge methods + SFX. Update journal + memory.
