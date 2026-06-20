# claude_rts

A real-time strategy game inspired by **StarCraft 2**, built to **ship on macOS + Windows**. Free to diverge with novel ideas; the tiebreaker is always *"would a player enjoy this?"*

> **Status: early scaffold.** Foundation is locked; **M0** (single-player against a mock sim — render → select → command) is being built.

## Foundation (locked)

|  |  |
|---|---|
| **Engine** | Godot 4 (Forward+ renderer) |
| **Sim language** | C++ via **GDExtension** (godot-cpp + SCons) |
| **Netcode** | Deterministic **lockstep** (M0 is single-player; transport added in M3) |
| **Determinism** | Fixed-point **Q32.32** for all sim state, seeded PRNG, fixed ~24 Hz tick → bit-identical on macOS-arm64 + Windows-x64 |
| **Model↔view seam** | C-ABI [`sim/include/sim/sim_abi.h`](sim/include/sim/sim_abi.h): the sim publishes a double-buffered POD snapshot; the view copies it and interpolates |

Full detail in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Repo layout

| Path | Owner | Contents |
|---|---|---|
| `sim/`   | **B** | Deterministic C++ simulation core + the canonical seam header (`sim/include/sim/sim_abi.h`) |
| `tests/` | **B** | Headless determinism / state-hash / golden-replay tests |
| `gdext/` | **T** | GDExtension bridge (godot-cpp) wrapping `sim_abi.h` for Godot |
| `game/`  | **T** | Godot 4 project — rendering, camera/input, selection, HUD |
| `assets/`, `tools/`, `docs/` | **T** | Content, tooling, documentation |

**Lanes:** **B** owns Simulation & Game Core; **T** owns Presentation, Platform & Tooling. They meet at the `sim_abi.h` seam.

## Build & run

See [`docs/BUILD.md`](docs/BUILD.md). TL;DR:

```bash
git clone --recurse-submodules https://github.com/tc2fh/claude_rts
cd claude_rts/gdext
python -m pip install "scons>=4.0"
scons platform=macos arch=universal target=template_debug      # or: platform=windows arch=x86_64
# then open game/ in Godot 4.5+ and press Play.
```

The current simulation is a **mock** (`gdext/src/mock_sim.cpp`) so the whole view pipeline runs before the real sim lands.

## Contributing

Game code & docs flow to `main` via feature-branch PRs. (Two-Claude build coordination happens on a separate orphan `mailbox` branch and is **not** needed to contribute code; never merge `mailbox` into `main`.)
