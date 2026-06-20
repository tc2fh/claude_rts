# claude_rts — Architecture

This is the durable engineering picture. Coordination decisions are logged on the `mailbox` branch (`mailbox/decisions.md`); this file is the human-readable synthesis.

## 1. Foundation (locked)

- **Engine:** Godot 4, Forward+ renderer. Free, royalty-free, first-class macOS + Windows export.
- **Sim language:** C++, shipped as a **GDExtension** (godot-cpp + SCons) — a native shared library Godot loads at runtime. No engine fork.
- **Netcode model:** deterministic **lockstep** (SC2/AoE-style). Clients exchange *only commands* and run identical simulations. M0 is single-player with no transport, but the command/tick/replay shape is identical, so multiplayer (M3) is *additive*, not a rewrite.

## 2. The two lanes

| Lane | Owner | Scope |
|---|---|---|
| **Simulation & Game Core** | **B** | tick/sim loop, ECS (EnTT), units, economy, combat, pathfinding, fog of war, sim-coupled netcode |
| **Presentation, Platform & Tooling** | **T** | rendering, camera/input, UI/HUD, audio, macOS+Windows build & packaging, content pipeline |

They are decoupled by the seam (§4). This lets both lanes build in parallel with one hard dependency: the ABI.

## 3. Determinism (hard requirement)

Lockstep desyncs if any client computes a different result, so the sim is held to strict rules:

- **Fixed-point only.** All sim state uses `fix64_t` = **Q32.32** in `int64`. No `float`/`double` in the sim, and no Godot-side floating point feeding back into sim state.
- **Seeded PRNG.** One seeded generator per match; no OS entropy, no wall-clock.
- **Fixed tick.** ~24 Hz to start, decoupled from render FPS. Time is counted in ticks, never seconds.
- **Stable ordering.** Deterministic system + entity iteration order (fixed system order: production → harvest → pathfinding/movement → combat/targeting → death → win-check).
- **Oracle.** A cheap per-N-tick **state hash** over canonical state is the test oracle now and the desync detector in M3. Replay = `seed + command log`; re-simulating reproduces the match exactly.

The view may convert `fix64 → float` for rendering, but that float is **render-only** and never re-enters the sim.

## 4. The model↔view seam — `sim/include/sim/sim_abi.h`

A small **C ABI** is the only cross-lane coupling. The sim owns the contract; the view owns the marshaling.

- **Snapshot (sim → view):** each published tick the sim exposes a **double-buffered, read-only POD snapshot** — an AoS array of `SimEntitySnapshot { id, type, owner, state, x, y, facing, hp, hp_max }` plus `tick`, `count`, and `resources[8]`.
- **Lifetime (important):** the snapshot pointer is **valid only until the next `sim_advance()`**. The view **copies** each snapshot into its own `prev`/`curr` buffers and **interpolates** between the two newest ticks (alpha = render time within the tick). Entity `id`s are **stable and not recycled within a match**, so a held selection is always safe to command.
- **Commands (view → sim):** POD `SimCommand` (`MOVE/ATTACK/HARVEST/BUILD/TRAIN/STOP`), each pushed with an `exec_tick` = current tick + input delay (N≈2–4) — the lockstep latency-hiding mechanism, exercised even in single-player.
- **Marshaling (T's decision):** the bridge reads the contiguous `entities` pointer directly in C++ (zero-copy), copies to `prev`/`curr`, and exposes typed accessors (`PackedFloat32Array`/`PackedInt32Array`) to GDScript — never per-entity Variants.

See `gdext/src/sim_bridge.{h,cpp}` for the consuming side and `gdext/src/mock_sim.cpp` for the stand-in producer.

## 5. Data flow (per render frame)

```
            push_command(exec_tick)                 advance(1) @ ~24Hz
  input ───────────────────────────►  SimWorld  ◄────────────────────────  fixed-tick accumulator
                                          │
                                sim_get_snapshot()  (ptr valid until next advance)
                                          │  copy → prev/curr
                                          ▼
                          interpolate(prev, curr, alpha) → draw + HUD
```

## 6. Milestones

- **M0 — view harness (now):** Godot project renders interpolated snapshot entities from a mock sim; camera pan/zoom; box-select; right-click move; resource HUD. GDExtension builds cross-platform in CI.
- **M1 — real sim core:** B's EnTT sim replaces the mock through the same ABI: harvest economy, movement (grid A*), basic combat, win check.
- **M3 — multiplayer:** add transport + input exchange + desync detection (state-hash compare). No view rewrite — interpolation and input-delay are already in place.

## 7. Toolchain & versions

- **godot-cpp:** pinned to branch `4.5` (submodule at `gdext/godot-cpp`) — the latest *stable* bindings. `compatibility_minimum = "4.5"`.
- **Godot editor:** 4.5 or newer (a 4.5-built extension is forward-compatible through 4.6/4.7).
- **Build:** SCons (godot-cpp). **CI:** GitHub Actions, macOS-universal (arm64+x86_64) + Windows-x64.
