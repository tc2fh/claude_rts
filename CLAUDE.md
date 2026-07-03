# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**claude_rts** — an SC2-inspired 2D-isometric RTS for macOS + Windows: a headless, deterministic C++17 sim core (`sim/`) behind a C ABI, loaded into a Godot 4 view (`game/`) via a GDExtension bridge (`gdext/`). Built lockstep-ready (M3 = multiplayer); currently single-player.

**Start here:** [docs/INDEX.md](docs/INDEX.md) is the per-file codebase index (every file's role, key symbols, the full ABI surface, tuning constants, pinned golden hashes). Read it instead of scanning the tree.

## Commands

```bash
# One-time: submodules (godot-cpp 4.5 + entt v3.13.2)
git submodule update --init --recursive

# Build the GDExtension (from repo root; output → game/bin/)
scons -C gdext platform=macos arch=arm64 target=template_debug -j8   # local dev (editor needs template_debug)
scons -C gdext platform=macos arch=universal target=template_release # CI/dist shape
scons -C gdext ... use_real_sim=no                                   # mock sim backend (view-only dev)

# Run the game / headless smoke test
godot --path game
godot --headless --path game -s res://tests/smoke_test.gd

# Sim tests (doctest via CMake; first configure needs network for FetchContent)
cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release
cmake --build sim/build -j8
ctest --test-dir sim/build --output-on-failure
./sim/build/sim_tests --test-case='*patrol*'     # single test (doctest wildcard; --list-test-cases to enumerate)

# Placeholder assets
python3 tools/gen_sfx.py        # regenerates game/assets/sfx/*.wav (deterministic)
```

New `sim/src|tests/*.cpp` files are picked up by CMake glob automatically; new test files just `#include <doctest/doctest.h>` (no main).

## Architecture

Two lanes meeting at one seam:

- `sim/` — the entire game model. EnTT ECS `World` (`sim/src/world.cpp` holds all systems), Q32.32 fixed-point math, ~24 Hz ticks. No Godot dependency; built standalone by CMake for tests and compiled into the gdext by SCons for the game.
- `sim/include/sim/sim_abi.h` — **the contract** (owner: lane B). Opaque `SimWorld*`, POD snapshot/command/event structs, ~13 C functions. ABI changes are additive-only: append enum values, never reorder fields.
- `gdext/src/sim_bridge.cpp` — the only registered Godot class (`SimBridge`, RefCounted). Marshals the ABI to GDScript as batch packed arrays (never per-entity Variants); the ONLY place fixed-point↔float conversion happens. `gdext/src/mock_sim.cpp` is a stand-in backend selected at build time (`use_real_sim=no`) — mutually exclusive with the real sim, no runtime indication of which is loaded.
- `game/` — view only. `main.gd` steps the sim on a fixed 24 Hz accumulator, renders immediate-mode in `_draw()` (no per-entity nodes), and issues commands from input. All game logic lives in the sim; the view derives everything from snapshots.

Data flow per frame: input → `SimBridge.command_*()` → `sim_push_command(cmd, exec_tick = now + input_delay)` … accumulator → `advance(1)` per tick → bridge copies the published snapshot (the sim's pointer dies on the next advance) → `render_state(alpha)` interpolates prev→curr for drawing. Sim→view feedback (SFX/VFX) uses the drainable event channel `sim_drain_events` (ATTACK/TRAINED/DIED) — deliberately outside the state hash.

### Determinism (the load-bearing constraint)

Cross-OS bit-identical replay is CI-enforced (same golden hash asserted on Linux/macOS/Windows). When touching `sim/`:

- Q32.32 fixed-point (`fix`) only; no float/double/sqrt/trig in sim state or logic. `fix_to_float` is render/debug-only — float-derived values never flow back in. Distances are Chebyshev cells; time is ticks (~24 Hz).
- Never iterate EnTT views directly in a system — every system pre-sorts entities by `EntityId`. Commands sort by (player, unit). The `step()` pipeline order is fixed: ++tick → apply_commands → harvest → production → combat → movement → death → publish.
- The only RNG is the seeded SplitMix64 in `World` (currently unused); no wall-clock or OS entropy.
- **Golden-hash discipline:** any sim behavior change re-pins all three pinned goldens — `sim/tests/test_determinism.cpp` (full-M0), `test_orders.cpp` (order scenario), `test_train.cpp` (soldier training). Run `sim_tests` once, copy the printed hashes, and keep each scenario's batching-invariance check (N ticks in chunks must hash identically).

## Conventions & traps

- **Lanes:** B owns `sim/` + tests + the ABI contract; T owns `gdext/`, `game/`, CI/export, tools. Coordination happens on the orphan `mailbox` branch (worktree `../claude_rts-mailbox`) — never merge it into `main`.
- **PRs:** code goes to `main` via feature-branch PRs, but the GitHub default branch is `mailbox` — always pass `--base main` to `gh pr create`.
- Entity id 0 is the *player HQ* and also the ABI's "0 = none" sentinel (`SimCommand.target`, `COrder.target`, `CWeapon.target`) — auto-acquire can never target the player HQ; don't write new `id == 0` checks. (Known cleanup: start ids at 1.)
- Commands execute only when `exec_tick ==` the dispatch tick — a stale exec_tick silently never fires.
- The state hash is a desync tripwire, not a proof: weapon cooldowns, paths, harvester/producer timers, node amounts, and pending commands are NOT hashed.
- Post-Batch-1 order semantics: plain move is **passive**; idle/stop/hold are **defensive** (fire in weapon range, never chase). Aggression requires attack-move/attack. Any order cancels an in-progress harvest.
- Godot: project targets 4.5 (`compatibility_minimum`); CI downloads `4.5-stable` in two hardcoded places in `ci.yml`. Locally newer 4.x works (`--headless --import` may crash on 4.6 — known engine bug, harmless). Editor requires a local `template_debug` build.
- There is no InputMap — all input is raw keycode/mouse checks in `main.gd`/`camera_rig.gd`; new hotkeys must avoid WASD/arrows (camera pan).

Current project state, open threads, and machine-local paths live in `.claude/journal.md` (gitignored), not here.
