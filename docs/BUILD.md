# Building claude_rts

Two pieces build separately: the **GDExtension** (`gdext/`, native C++) and the **Godot project** (`game/`). You build the extension, then open the project in Godot — Godot loads the compiled library via `game/bin/sim_rts.gdextension`.

## Prerequisites

- **Godot 4.5+** (editor) — <https://godotengine.org/download>. A 4.5-built extension is forward-compatible through 4.6/4.7.
- **Python 3.8+** and **SCons 4+**: `python -m pip install "scons>=4.0"`
- A C++17 compiler:
  - **macOS:** Xcode command-line tools (`xcode-select --install`)
  - **Windows:** Visual Studio 2022 (Desktop C++ workload) / MSVC
- **Git** (the build uses the `gdext/godot-cpp` submodule)

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/tc2fh/claude_rts
# already cloned?
git submodule update --init --recursive
```

`gdext/godot-cpp` is pinned to the **4.5** branch (matches `compatibility_minimum`); `gdext/entt` is pinned to **v3.13.2** (header-only ECS, matching the sim's CMake version) — needed for the default real-sim build.

## 2. Build the GDExtension

```bash
cd gdext

# macOS (universal: arm64 + x86_64)
scons platform=macos   arch=universal target=template_debug     # editor/dev
scons platform=macos   arch=universal target=template_release   # exported game

# Windows (x86_64)
scons platform=windows arch=x86_64    target=template_debug
scons platform=windows arch=x86_64    target=template_release
```

Output lands in `game/bin/` with names matching `game/bin/sim_rts.gdextension`. The **first** build compiles godot-cpp (slow, minutes); later builds are incremental.

> To run inside the **editor** you need a `target=template_debug` build; exported games use `target=template_release`. CI builds release on both platforms (cross-platform compile gate); build debug locally for editor work.

### Sim backend (real sim is the default)

The gdext links **B's real deterministic sim by default** — `sim/src` + the pinned `gdext/entt` submodule — so the game runs real movement (and economy/combat as they land). For isolated view work, build the lightweight **mock** instead (no EnTT; `gdext/src/mock_sim.cpp`, which self-disables when the real sim is built):

```bash
scons platform=macos arch=universal target=template_debug use_real_sim=no   # mock backend
```

## 3. Run the game

Open `game/` in Godot 4.5+ and press **Play** (`main.tscn`). Controls:

- **LMB drag** — box-select · **LMB click** — pick one
- **RMB** — move selected units
- **WASD / arrows / screen edges** — pan · **mouse wheel** — zoom

If the HUD says *"SimBridge not loaded,"* the extension isn't built for your platform/target — redo step 2.

## CI

`.github/workflows/ci.yml` builds the GDExtension (`target=template_release`) on **macOS-universal** and **Windows-x64** on every push/PR to `main`, and runs `sim/`'s headless tests once a `sim/CMakeLists.txt` exists.

## Troubleshooting

- **`scons: command not found`** → `python -m pip install scons` (ensure the pip scripts dir is on `PATH`).
- **Windows: MSVC not found** → run from a *Developer Command Prompt*, or let CI's `msvc-dev-cmd` set it up.
- **Editor: class `SimBridge` missing** → you built `template_release` only; build `template_debug` for the editor.
