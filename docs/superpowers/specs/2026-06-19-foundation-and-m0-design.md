# claude_rts — Foundation Architecture & Milestone 0 (Walking Skeleton)

**Status:** Draft for review · **Date:** 2026-06-19
**Authors:** B (Benjamin's Claude) with Benjamin · **Team:** Benjamin (party B) + Tien (party T) — see the `mailbox` branch.

---

## 1. Goal & context

claude_rts is a real-time strategy game inspired by **StarCraft 2** (free to diverge with novel mechanics), shipping on **macOS and Windows**. The objective is to **ship a genuine, tightly-scoped indie game**, not a tech demo — "would a player enjoy this?" is the tiebreaker on design calls.

This document specifies the **technical foundation** and the **first milestone, M0**. It deliberately does *not* spec the whole game: an RTS is decomposed into a milestone ladder (§8), each milestone getting its own spec → plan → build cycle.

**Non-goals for this spec:** multiplayer/netcode (designed *for*, deferred to M3); fog of war, flow-field pathfinding, control groups (M1); factions, the novel hook, art direction, balance (M2); final UI/UX, audio design, shippable content (later).

## 2. Constraints & decisions

| Area | Decision | Rationale |
|---|---|---|
| Goal | Ship a tight indie game | Product over technical purity |
| Team | 2 people, C++ / systems | Plays to the team's strength |
| Platforms | macOS + Windows | Cross-platform from day one |
| Presentation | 2D isometric, **Godot 4** | Free cross-platform export + render/UI/audio/scene tooling |
| Engine (view) | **Godot 4**, C++ via **GDExtension** | Sim stays a native lib; no engine fork; lanes build independently |
| Language | C++ (17/20) throughout (sim + GDExtension) | One language; team strength |
| Sim model | Deterministic, fixed-timestep, lockstep-ready | Enables lockstep MP, replays, and saves |

**Engine/stack is LOCKED** jointly via the mailbox (B-1 → T-2 → B-3): **Godot 4 + C++ (GDExtension) + deterministic lockstep.** T owns the GDExtension build + CI matrix and the export pipeline.

## 3. Architecture overview

```
   ┌────────────────────────────────────────────────┐
   │  PRESENTATION   (Tien / T) — Godot 4             │
   │  scenes · 2D iso render · UI/HUD · input · audio  │
   └─────────────┬────────────────────▲───────────────┘
                 │ commands            │ state snapshots
                 │ (POD structs)       │ (double-buffered)
   ╎······· GDExtension boundary (godot-cpp / C-ABI) ·······╎
   ┌─────────────▼────────────────────┴───────────────┐
   │  DETERMINISTIC SIM CORE  (Benjamin / B) — C++     │
   │  native lib · fixed timestep · ECS (EnTT)         │
   │  headless · same inputs ⇒ same state, bit-for-bit │
   └────────────────────────────────────────────────┘
                 ▲
                 │  (later, M3) lockstep netcode = exchange only commands
```

The core principle is a **strict separation** between a deterministic simulation and a presentation layer:

- The **sim** owns all game state and rules. It advances in fixed ticks, takes only *commands* as input, and is fully deterministic and headless (no graphics, no wall-clock, no OS entropy). It ships as a **native C++ library** with no Godot dependency.
- The **presentation** (Godot 4) owns everything the player sees and does. It loads the sim as a **GDExtension**, reads read-only *snapshots* of sim state and renders them, and translates input into *commands* it sends to the sim. It never mutates game state.

This seam is non-negotiable for an RTS: it is what makes lockstep multiplayer (exchange commands, not state), replays (re-run commands), saves, and automated testing possible. Retrofitting it later is a rewrite.

## 4. Simulation core (lane B)

### 4.1 Model — ECS
EnTT. Entities are units/structures/resources; components are data (`Position`, `Velocity`, `Health`, `Owner`, `Harvester`, `Producer`, `Weapon`, …); systems operate over component sets each tick. The sim is a standalone native library — **zero engine/Godot dependencies** — so it builds and tests headless.

### 4.2 The tick
Fixed timestep, **~24 Hz to start** (exact rate finalized in the plan), decoupled from render FPS. Each tick:
1. Drain the command queue (intents issued since the last tick); validate and apply.
2. Run systems in a **fixed order**: production → harvesting → pathfinding/movement → combat/targeting → death/cleanup → win-check.
3. Publish a double-buffered snapshot for the view; (optionally) emit a state hash for desync detection.

### 4.3 Determinism rules (hard requirements)
- **Fixed-point math** for all sim quantities (positions, velocities, health). No raw `float`/`double` in sim state or logic, and **no reliance on Godot/GDScript floats** for sim state. The exact format (Q16.16 / Q32.32 / Q40.24) is **validated in the plan** against map size + precision budget — note Q32.32 × Q32.32 needs a 128-bit intermediate (`__int128`).
- **No `sqrt` or trig in the sim** — the classic cross-platform desync source. Use **squared distances** for range/proximity, move along **grid-cell waypoints** (8 discrete directions, no normalization), and compute **cosmetic facing in the view** (float is fine there). Any in-sim Euclidean length uses a deterministic fixed-point routine, never `std::sqrt`.
- **Stable iteration order** — never iterate in hash/pointer order; use stable entity ordering.
- **Seeded, deterministic RNG** — one explicit PRNG seeded per match; no `rand()`, no hidden global state.
- **No wall-clock, no OS entropy** in the sim — time is counted in ticks.
- Same initial state + same command log ⇒ **bit-identical** final state on macOS (arm64) and Windows (x86_64).

### 4.4 Commands & replays
Player intents are *commands* (`MoveTo`, `Harvest`, `Build`, `Train`, `Attack`, …) stamped to a tick. A **replay** is the initial seed plus the full command log; re-simulating reproduces the match exactly. A cheap **per-N-tick state hash** detects divergence — invaluable when M3 multiplayer arrives, and a strong test oracle now.

### 4.5 The seam — snapshot + command ABI
The sim↔view boundary is the one place the two lanes physically meet, so it is co-defined first (B drafts the C-ABI header; T wraps it in the GDExtension):
- **Snapshot (sim → view):** a read-only, POD view of render-relevant state per entity (id, interpolation pose, hp, owner, anim/state flags). The returned buffer is **valid only until the next `sim_advance()`**, so to interpolate between the two most recent ticks the **view copies each snapshot into its own prev/curr buffers** — it must never hold the sim's pointer across an advance. (The sim double-buffers internally to publish atomically; the view owns its interpolation copies.)
- **Commands (view → sim):** small POD command structs enqueued across the boundary, stamped to an execution tick.
- **Entity ids are stable and not recycled within a match** (or carry a generation counter), so a held selection in the view never commands a since-replaced entity.

### 4.6 M0 systems (minimum)
Movement; single-resource harvesting; production (HQ → worker / combat unit); grid **A\*** pathfinding; melee/ranged combat with auto-acquire; death; win-check (HQ destroyed). (Selection is presentation state, not sim state.)

## 5. Presentation (lane T)
- **Godot 4**, loading the C++ sim as a **GDExtension** (native shared lib; no engine fork; hot-reloadable; builds independently of the sim).
- Godot owns scenes, 2D isometric rendering, UI/HUD, input, audio, and **cross-platform export** (macOS universal arm64+x86_64, Windows x86_64).
- Reads **double-buffered sim snapshots** across the GDExtension boundary and **interpolates between the two most recent ticks** for smooth rendering at display FPS.
- Translates input → **commands** (POD structs) sent back across the boundary (box-select, right-click move/harvest/attack, hotkeys to train/build).
- Holds **no authoritative game state** — purely a view + input mapper; selection and camera are presentation-only.
- T owns the **GDExtension build + CI matrix** and the export pipeline.

## 6. Determinism & testing strategy
The headless sim is the primary test target:
- **Unit tests** per system (a harvester returns N resource; combat applies D damage; production debits cost; …).
- **Golden-replay tests** — a recorded command log must reproduce a known final-state hash. Run in **CI on macOS and Windows**; a hash mismatch is a cross-platform determinism break, caught immediately rather than in M3.
- Presentation (Godot) is smoke-tested (launches, renders, sends commands); it carries no game logic to unit-test.

## 7. Repository layout (on `main`)
```
sim/        headless C++ sim core library — zero engine deps          [B]
gdext/      GDExtension binding: wraps sim for Godot (godot-cpp)       [T, seam co-defined with B]
game/       Godot 4 project (scenes, iso render, UI, input, audio)     [T]
tests/      unit + golden-replay tests (headless)                      [B]
assets/     sprites, audio (placeholder in M0)                         [T]
tools/      dev tooling (map editor, later)                            [T]
docs/       specs, design notes, ADRs
```
Build: `sim` builds standalone + headless (CI determinism tests); `gdext` links `sim` + `godot-cpp` into the GDExtension library; the Godot `game/` project loads it. **Repo bootstrap:** T scaffolds `main` (top-level build, Godot project, `gdext` stub, CI matrix); B lands `sim/` + `tests/` via PRs against it — so the two lanes don't race to create the first commit.

## 8. Milestone ladder
- **M0 — Walking Skeleton** *(this spec)* — the smallest *complete* RTS loop on the real architecture.
- **M1 — RTS Fundamentals** — fog of war, flow-field pathfinding at scale, control groups, real economy/tech, HUD pass, a genuine AI.
- **M2 — Game Identity** — factions and the **novel hook** (its own brainstorm), maps, art pass, balance.
- **M3 — Multiplayer** — lockstep netcode over reliable-UDP, lobby, desync detection, replay UI.
- **M4 — Ship** — content, polish, settings, packaging/distribution, playtesting.

## 9. M0 — Walking Skeleton (detailed scope)
Single-player vs. a trivial AI, on one small fixed isometric map.

**The player can:**
- Select units (click + drag-box), move (right-click) with grid A\* pathfinding.
- Command a **worker** to harvest one resource type → return to HQ → resource count rises.
- Spend resources at the **HQ** to train a **worker** and a **combat unit**.
- Command a **combat unit** to attack-move; it auto-acquires and attacks enemies in range; units die at 0 HP.

**The world has:**
- The player's HQ + a starting worker; one resource node.
- A trivial enemy: an HQ plus a unit or two that attack-move toward the player.
- **Win:** destroy the enemy HQ. **Lose:** lose your HQ.

**Technical acceptance:**
- Runs on the deterministic fixed-timestep sim; Godot renders interpolated snapshots across the GDExtension boundary.
- A recorded command log reproduces an **identical final-state hash on macOS and Windows**.
- Clean build + export + run on both OSes.

**Explicitly out of M0:** fog of war, multiple resource types, tech tree, advanced AI, multiplayer, control groups, polished art/UI, and **local collision/avoidance** — units may overlap on a cell in M0; flocking/avoidance is M1.

## 10. Open questions (resolve in the plan or via mailbox)
Exact tick rate + fixed-point format · grid A\* implementation details · the **snapshot/command ABI** at the sim↔GDExtension seam (co-defined next; B drafts the header) · placeholder-art source · CI specifics for the cross-platform determinism check.

## 11. Collaboration
Two Claudes work in parallel — Benjamin (B) and Tien (T) — coordinating via the `mailbox` branch (append-only; never merged into `main`). Provisional work split: **B owns the sim core + tests; T owns the Godot view, the GDExtension build, CI/export, and tooling.** The clean sim↔view seam — a double-buffered snapshot plus POD command structs — is the one place the lanes meet, and is co-defined first. The engine/stack is **locked** (B-1 / T-2 / B-3): Godot 4 + C++ (GDExtension) + deterministic lockstep.

## 12. Risks
- **Cross-platform fixed-point determinism** — the core technical risk. Mitigated by the §4.3 rules (fixed-point, no `sqrt`/trig, stable order, seeded RNG) and the **golden-replay hash test in CI on both OSes** (§6), which surfaces any divergence immediately.
- **GDExtension build & packaging** (T's lane) — native lib × Godot × two OSes is fiddly. Mitigated by T owning the build/CI matrix from day one and the view building against a mock first.
- **Pathfinding lock-in** — M0's grid A\* must stay behind a narrow interface so it doesn't bake in assumptions (e.g., per-unit independent paths) that block M1's flow-field rework at scale.
- **M0 scope creep** — the walking skeleton exercises the architecture end-to-end; it is not about fun or content. Anything past §9 waits for M1+.
