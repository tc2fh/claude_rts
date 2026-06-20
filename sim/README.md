# sim/ — deterministic simulation core (owner: B)

The headless, engine-agnostic C++ simulation. **No Godot, and no floats in sim state** — all sim math is fixed-point Q32.32 for cross-platform determinism.

- **`include/sim/sim_abi.h`** — the **canonical model↔view seam** (C ABI), included as `#include <sim/sim_abi.h>`. Owned here, consumed by `gdext/`. Source of truth for the snapshot/command PODs and lifecycle calls.
- *(B's PR adds)* the EnTT-based sim and its systems (production → harvest → pathfinding/movement → combat → death → win), seeded PRNG, per-N-tick state hash, golden-replay harness, and a `CMakeLists.txt` that builds a standalone lib plus the headless tests under [`../tests/`](../tests).

Determinism rules, the tick model, and the lockstep/command design are in [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md).

> Until the real sim lands, `gdext/src/mock_sim.cpp` implements `sim_abi.h` so the view builds and runs against a stand-in.
