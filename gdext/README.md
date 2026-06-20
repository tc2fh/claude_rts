# gdext/ — GDExtension bridge (owner: T)

Wraps the C-ABI sim (`../sim/include/sim/sim_abi.h`, `#include <sim/sim_abi.h>`) as a Godot class, **`SimBridge`**, that the `game/` project drives. Built with **godot-cpp + SCons**.

- `src/register_types.{h,cpp}` — GDExtension entry point + class registration.
- `src/sim_bridge.{h,cpp}` — `SimBridge`: owns the `prev`/`curr` snapshot copies (the sim's pointer dies on each `advance`), interpolates between ticks, and exposes typed accessors (`PackedFloat32Array` / `PackedInt32Array`) plus command calls to GDScript.
- `src/mock_sim.cpp` — lightweight stand-in implementation of `sim_abi.h` for isolated view dev (`use_real_sim=no`); self-disables when the real sim is built.
- `godot-cpp/` — submodule, pinned to branch `4.5`.
- `entt/` — submodule, pinned to `v3.13.2` (header-only ECS the real sim needs).
- `SConstruct` — build script; **links B's real sim by default** (mock via `use_real_sim=no`); output to `../game/bin/` to match `sim_rts.gdextension`.

Build commands and the editor-vs-release note are in [`../docs/BUILD.md`](../docs/BUILD.md).
