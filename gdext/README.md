# gdext/ — GDExtension bridge (owner: T)

Wraps the C-ABI sim (`../sim/include/sim_abi.h`) as a Godot class, **`SimBridge`**, that the `game/` project drives. Built with **godot-cpp + SCons**.

- `src/register_types.{h,cpp}` — GDExtension entry point + class registration.
- `src/sim_bridge.{h,cpp}` — `SimBridge`: owns the `prev`/`curr` snapshot copies (the sim's pointer dies on each `advance`), interpolates between ticks, and exposes typed accessors (`PackedFloat32Array` / `PackedInt32Array`) plus command calls to GDScript.
- `src/mock_sim.cpp` — temporary stand-in implementation of `sim_abi.h` (self-disables when built with `use_real_sim=yes`).
- `godot-cpp/` — submodule, pinned to branch `4.5`.
- `SConstruct` — build script; output goes to `../game/bin/` to match `sim_rts.gdextension`.

Build commands and the editor-vs-release note are in [`../docs/BUILD.md`](../docs/BUILD.md).
