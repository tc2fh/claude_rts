# Codebase Index

Per-file map of claude_rts. **Read this instead of scanning the tree.** Line numbers are as of `main @ 5d768da` — treat them as anchors, not gospel; roles and contracts change far slower than lines. Update this file when the ABI, tuning constants, golden hashes, or file roles change.

Repo layout: `sim/` (B: deterministic C++ sim core + tests) · `gdext/` (T: GDExtension bridge) · `game/` (T: Godot 4 project) · `tools/` (asset generators) · `docs/` (architecture, build, specs/plans) · `.github/workflows/ci.yml` (CI). Vendored, do not index: `gdext/godot-cpp`, `gdext/entt` (submodules).

---

## sim/ — deterministic simulation core (lane B)

### sim/include/sim/sim_abi.h — THE model↔view seam (C ABI, contract owner: B)

Include as `<sim/sim_abi.h>` with `sim/include` on the path. Additive-only changes; T consumes, never edits.

**Types**

| Symbol | Meaning |
|---|---|
| `fix64_t` (`SIM_FIX_SHIFT`=32) | Q32.32 fixed-point int64; positions across the ABI. View converts to float for rendering ONLY |
| `SIM_STATE_*` bitflags | IDLE=0, MOVING=1, ATTACKING=2, HARVESTING=4, CARRYING=8, BUILDING=16, DEAD=32 — combinable; test bits, never `==` (IDLE is absence of bits). Sim currently only ever writes IDLE/MOVING |
| `SimEntitySnapshot` | `{u32 id; u16 type; u8 owner; u8 state; fix64_t x,y; u16 facing(0..65535=full turn, unwritten yet); i32 hp,hp_max}` |
| `SimSnapshot` | `{u64 tick; const SimEntitySnapshot* entities; u32 count; i32 resources[8]}` — **pointer valid only until next `sim_advance`; copy it** |
| `SimCommandType` | CMD_MOVE=0, ATTACK=1, HARVEST=2, BUILD=3 *(enum exists, NO handler — silently dropped)*, TRAIN=4, STOP=5, ATTACK_MOVE=6, HOLD=7, PATROL=8. Append only |
| `SimUnitType` | WORKER=1, HQ=2, RESOURCE=3, SOLDIER=4 (also `SimCommand.param` for TRAIN) |
| `SimCommand` | `{type; u8 player; u32 unit; u32 target; fix64_t tx,ty; u16 param}` — 0 = none for unit/target |
| `SimMapInfo` | `{u16 w,h; const u8* passable}` row-major `y*w+x`, 1=open; sim-owned, static per match |
| `SimEventType` / `SimEvent` | ATTACK=1 (a=attacker,b=victim), TRAINED=2 (a=producer,b=new unit), DIED=3 (a=dead,b=0); `{u16 type; u32 a,b; u64 tick}` |

**Functions:** `sim_create(seed,map_id)` (snapshot valid at tick 0) · `sim_destroy` · `sim_advance(n)` (invalidates snapshot pointers) · `sim_current_tick` · `sim_push_command(cmd, exec_tick)` (executes when exec_tick `==` dispatch tick; stale = silent no-op) · `sim_get_snapshot` · `sim_state_hash` (desync oracle) · `sim_get_map_info` · `sim_winner` (0=ongoing/1/2) · `sim_drain_events(out,max)` (destructive FIFO; queue caps at 4096, drops beyond).

### sim/include/sim/constants.h — gameplay tuning table (ticks @ ~24 Hz, Chebyshev cells, integer minerals)

| Constant | Value | | Constant | Value |
|---|---|---|---|---|
| TYPE_WORKER/HQ/RESOURCE/SOLDIER | 1/2/3/4 | | SOLDIER_HP / DMG | 50 / 10 |
| MINE_TIME | 16 ticks | | SOLDIER_RANGE | 4 cells |
| LOAD | 5 minerals/trip | | SOLDIER_CD | 12 ticks |
| WORKER_COST / BUILD_TIME | 50 / 48 ticks | | ACQUIRE_RANGE | 7 cells |
| SOLDIER_COST / SOLDIER_BUILD_TIME | 75 / 72 ticks | | HQ_HP | 200 |
| NODE_AMOUNT | 500 | | worker/soldier speed | fix_one/8 = 3 cells/s |

Changing any of these re-pins the golden hashes (see Tests).

### Other headers

| File | Role / key points |
|---|---|
| `components.h` | ECS components. **Hashed:** `CId.id`, `CPos.x/y`, `CUnit.{type,owner,state,facing,hp}`, `COrder.{kind,target}`. **NOT hashed:** `CMobile` (speed/path/next), `CHarvester` (phase/carried/node/hq/timer), `CProducer` (train_type/timer, queue depth 1), `CWeapon` (damage/range_cells/cooldown/timer/target), `hp_max`, `COrder.dest/anchor/to_dest`. `OrderKind`: ORD_STOP/MOVE/ATTACK_MOVE/HOLD/PATROL/ATTACK_TARGET |
| `fixed.h` | `fix` = int64 Q32.32; `fix_one`=1<<32=1 cell; `fix_from_int/to_float/abs/sign/clamp`. `fix_to_float` render/debug only |
| `hash.h` | FNV-1a 64 incremental `Hasher` (offset basis `0xcbf29ce484222325`); order-sensitive; native-LE byte hashing |
| `map.h` | `GridPos{int x,y}`; `Map(map_id)`; `passable(x,y)` bounds-checked (OOB=blocked); `cell_to_world` (cell low corner) / `world_to_cell` (`>>32`); `passable_data()` backs SimMapInfo |
| `pathfind.h` | `find_path(map, start, goal)` → cells start..goal INCLUSIVE; empty = unreachable; deliberately narrow (M1 may swap in flow fields) |
| `rng.h` | SplitMix64 `Rng`; `range(n)` modulo (bias accepted). The ONLY permitted randomness; currently unused by any system |
| `world.h` | `World`: registry + map + tick + command queue + double-buffered snapshot + resources_[8] + winner_ + events_. Private per-tick pipeline listed below |

### sim/src/

**world.cpp — the entire game model.** Key anchors (line:symbol):

- `:17 World()` — seeds rng (unused), `spawn_initial()`, publishes tick-0 snapshot.
- `:30 spawn_initial()` — canonical layout, ids by spawn order: **0**=player HQ (4,4, CProducer, hp 200) · **1**=worker (5,5) · **2**=node (8,8, 500) · **3**=player soldier (10,10) · **4**=enemy HQ (20,20) · **5**=enemy scout (14,10, hp 20, standing ORD_ATTACK_TARGET on player HQ) — uncommanded games are NOT quiescent. Trained units get ids ≥ 6.
- `:74 step()` — **fixed order:** ++tick → apply_commands_for(tick) → sys_harvest → sys_production → sys_combat → sys_movement → sys_death → publish_snapshot. Damage lands in combat but corpses are removed after movement — a unit killed this tick still moves this tick.
- `:137 apply_commands_for` — sorts by (player, unit); shared `set_order` lambda resets `CHarvester.phase`→HARV_IDLE (**any order cancels harvest**). CMD_TRAIN: param 0→worker; single slot; cost deducted up front. CMD_HARVEST does NOT touch COrder (parallel machine). CMD_BUILD unhandled.
- `:102 sys_movement` — per-axis clamped stepping (diagonals ~√2 faster in world distance); sole writer of `u.state` (IDLE/MOVING only).
- `:400 sys_combat` — attackers need `{CWeapon,CMobile,...}` (immobile weapons invisible); Chebyshev cell distances; stance-gated: MOVE passive · ATTACK_TARGET chase+repath-every-tick · ATTACK_MOVE/PATROL acquire ≤ ACQUIRE_RANGE (nearest, lowest-id tiebreak) then resume · STOP/HOLD fire-in-range-never-move. **`acquired != 0` means "none" → enemy auto-acquire can never target player HQ (id 0).**
- `:278 state_hash` — exact order: tick; per entity (id-sorted): id, pos.x/y, type/owner/state/facing, hp, [COrder kind,target]; resources_[0..7]; winner_. Necessary-not-sufficient (timers/paths/node amounts unhashed).
- `:303 emit_event / :307 drain_events` — cap 4096, destructive FIFO drain; events never desync.
- Systems always pre-sort entities by EntityId into a vector — **never iterate entt views directly**. `find_by_id` is O(N) linear scan (known cleanup: id→entity map). `commands_` never pruned.

**map.cpp** — `map_id` IGNORED: always 24×24, open except wall column x=12 for y∈[4,20) with gap at (12,10).

**pathfind.cpp** — 8-connected A*, costs 10/14, octile heuristic, no corner-cutting (both orthogonals must be open), tie-break = smaller cell index `y*W+x`; neighbor order E,W,S,N,SE,NE,SW,NW is determinism-relevant — don't "optimize". Ignores units entirely (no unit blocking; stacking legal; production spawns at HQ+(1,1) unchecked).

**sim_abi.cpp** — one-line forwards to World; nothing lives here.

### sim/tests/ + sim/CMakeLists.txt

CMake: `sim` static lib + `sim_tests` doctest exe (FetchContent EnTT v3.13.2 + doctest 2.4.11; `CMAKE_POLICY_VERSION_MINIMUM=3.5`). Sources globbed — new files need no CMake edits. One `doctest_main.cpp` owns main().

**Pinned goldens (re-pin ALL THREE on any sim behavior change; each prints its computed hash to stdout — run once, copy):**

| Golden | Where | Scenario |
|---|---|---|
| `0xdd58708ee3f85ad4` | `test_determinism.cpp:39` | full-M0: harvest+train+attack, seed 20260620, 3000 ticks |
| `0xbbf4a1ef823f7504` | `test_orders.cpp:368` | move+attack order scenario, seed 13, 300 ticks |
| `0x7dabe2bc54f61b64` | `test_train.cpp:219` | harvest + train soldier, seed 11, 2100 ticks |

Batching-invariance pattern: total ticks divisible by every chunk size (595=5·7·17, 2100, 3000…), all runs must hash equal. Per-file coverage: `test_abi` (lifecycle/snapshot shape/cross-instance hash) · `test_combat` (mutual damage, death+no-id-reuse, HQ-kill win — kill scout id5 first or lose the race) · `test_determinism` (golden) · `test_economy` (harvest loop, train worker, insufficient-funds no-op) · `test_events` (emission contracts, destructive drain, stream batching-invariance) · `test_fixed`/`test_hash`/`test_rng` (primitives) · `test_map`/`test_mapinfo` (geometry + ABI grid) · `test_move` (arrival+IDLE, wall routing, scheduled-tick semantics) · `test_orders` (stance matrix: passive move, defensive idle/hold, stop bit-exact freeze, harvest-cancel, attack-move engage-then-resume, patrol flip; + golden; spawn-layout doc comment at top) · `test_pathfind` (endpoints inclusive, gap routing, unreachable=empty, determinism) · `test_train` (param dispatch, soldier economics, trained-soldier win, golden) · `test_world` (spawn count 6, batching).

---

## gdext/ — GDExtension bridge (lane T)

**SConstruct** — wraps godot-cpp's SConstruct. `scons platform={macos|windows|linux} arch={arm64|universal|x86_64} target={template_debug|template_release}` from `gdext/` (or `-C gdext`). **Default `use_real_sim=yes`** compiles `../sim/src/*.cpp` + EnTT into the lib and defines `SIM_RTS_USE_REAL_SIM`; `use_real_sim=no` builds the mock instead — compile-time switch, a stale mock build in `game/bin` is silent. Output: `game/bin/libsim_rts.*` (macOS framework layout) matching `game/bin/sim_rts.gdextension` (entry symbol `sim_rts_library_init`, compat min 4.5).

**src/sim_bridge.{h,cpp}** — `SimBridge` (RefCounted), the ONLY registered class and the only fixed↔float boundary. Owns `prev_`/`curr_` snapshot copies (interp rotates only inside `advance()`; `advance(0)` = legal resync). All commands hard-code `player=1` and schedule at `now + input_delay_` (default 2, settable). Bound methods (the complete GDScript surface as of 5d768da):

> `create(seed,map_id)` · `destroy()` · `is_ready()` · `advance(ticks)` · `tick()` · `entity_count()` · `entity_ids() → PackedInt32Array` · `entity_meta() → PackedInt32Array` stride 5 `[type,owner,state,hp,hp_max]` · `render_state(alpha) → PackedFloat32Array` stride 3 `[x,y,facing_rad]` world units, prev→curr lerp matched by id · `get_resource(player)` (reads a FRESH snapshot — can be newer than tick()) · `map_size() → Vector2i` · `map_passable() → PackedByteArray` · `command_move(id,x,y)` · `command_stop(id)` · `command_train(hq_id,unit_type)` · `command_harvest(id,node_id)` · `command_attack(id,target_id)` · `winner()` · `state_hash()` (int64, may be negative) · `set_input_delay(n)`/`get_input_delay()`

> plus (PR #13): `command_attack_move(id,x,y)` · `command_hold(id)` · `command_patrol(id,x,y)` · `drain_events() → Array` of `{type,a,b,tick}` Dictionaries

All methods are safe no-ops with no world. The mock carries a zero-event `sim_drain_events` stub (PR #13) so `use_real_sim=no` keeps linking.

**src/mock_sim.cpp** — entire file `#ifndef SIM_RTS_USE_REAL_SIM`. 3 drifting units, 0..32-unit box (but reports a 24×24 map — don't infer consistency), only MOVE/STOP, **applies commands immediately** (ignores exec_tick), winner always 0.

**src/register_types.cpp** — registers SimBridge at SCENE level; add new bridge classes here.

---

## game/ — Godot 4 view (lane T)

Single 4-node scene `main.tscn`: Main (Node2D, `main.gd`) + Camera2D (`camera_rig.gd`) + HUD CanvasLayer→Label (`hud.gd`). **Everything is drawn immediate-mode in `Main._draw()`** — no per-entity nodes. Entities draw as generated sprites from `assets/sprites/` (PR #13), falling back to flat primitives (HQ=square, resource=diamond, units=circles) when textures are unimported. No InputMap in `project.godot` — all input is raw keycodes.

| File | Contents |
|---|---|
| `project.godot` | Godot 4.5 features, Forward+, 1280×720 canvas_items stretch; minimal (no input/audio sections) |
| `scripts/main.gd` | `TICK_HZ=24` accumulator → `advance(1)` per tick, `render_state(alpha)` interp; `PX_PER_UNIT=32`; `SEED=1234`/`MAP_ID=0` hardcoded. Input (PR #13): LMB box-select (prefers own units; shift additive; double-click = same type on screen), RMB smart command (enemy→attack, resource→harvest, ground→passive move), verbs `A`/`M`/`P` then click (attack-move/move/patrol, Esc cancels), `S` stop / `H` hold, `T`/`E` train, `Ctrl+1-9`/`1-9` control groups (double-tap centers camera). Drains sim events each frame → hit-flash/death-puff VFX + throttled 8-player SFX pool; command markers |
| `scripts/camera_rig.gd` | Arrows/edge pan only — WASD are hotkeys (800 px/s ÷ zoom; edge pan only while the mouse is inside the window), zoom-to-cursor wheel 1.1× clamped [0.4,3.0], `center_on(px)`; start position hardcodes map center (doesn't query map_size) |
| `scripts/hud.gd` | `HudPanel` Label; `set_stats(tick, minerals, selected, winner, my_player, pending_verb)`; tick<0 → "build gdext" error text; victory/defeat banner; pending-verb prompt |
| `tests/smoke_test.gd` | Headless CI gate: SimBridge exists → create/advance 50 → stride asserts (ids×3 floats, ids×5 ints) → command no-crash calls (incl. attack-move/hold/patrol) → event-channel assert on a fresh world (ATTACK+DIED land by ~tick 40) → map has ≥1 wall → scripts+scene load. Breaks first on snapshot-layout changes |
| `export_presets.cfg` | macOS (universal, unsigned, `../build/macos/claude_rts.zip`) + Windows x86_64; no Linux preset |
| `assets/sfx/` | 6 generated WAVs: `cmd_move/cmd_attack/cmd_build` (view plays on input) + `hit/train_done/death` (driven by sim events) |
| `assets/sprites/` | 7 generated PNGs (PR #13, from `tools/gen_sprites.py`): `hq/worker/soldier` × `blue/red` (48² / 32²) + `node.png`; `.import` sidecars are NOT committed — CI's `--import` step / any editor open regenerates them |

---

## tools/, CI, docs

- `tools/gen_sfx.py` — stdlib synth for the 6 WAVs, 22050 Hz mono, seeded noise → byte-reproducible. Rerun after tweaking envelopes.
- `tools/gen_sprites.py` (PR #13) — stdlib PNG writer (hand-rolled encoder, deterministic) for the 7 placeholder sprites → `game/assets/sprites/`.
- `.github/workflows/ci.yml` — push/PR to main, concurrency-cancel per ref. **Merge gate:** `gdext` (macOS universal + Windows x86_64 release compile), `sim-tests` (ubuntu/macos/windows cmake+ctest — the per-OS golden assertion IS the cross-platform determinism proof), `smoke` (linux template_debug build + headless Godot 4.5 runs smoke_test.gd). `export-macos` (unsigned .app artifact) only on push or `feat/export*` branches. Godot `4.5-stable` hardcoded in two download steps.
- `docs/ARCHITECTURE.md` — durable picture: lanes, determinism rules, seam contract, data-flow diagram, toolchain pins. `docs/BUILD.md` — per-platform build + troubleshooting (editor needs template_debug; "SimBridge missing" = wrong target built).
- `docs/superpowers/specs/2026-06-19-foundation-and-m0-design.md` — founding spec; milestone ladder M0→M4; M0 scope + explicit non-goals.
- `docs/superpowers/specs/2026-06-20-foundation-batch1-design.md` — **the current roadmap.** Batch 1 (done, sim half): stance orders + event channel + SFX ‖ (view half) SC2 hotkeys A/M/S/H/P, zoom-to-cursor, SFX playback. Batch 2: control groups, shift-queued waypoints, double-click select-same-type, command markers, hit-flash, idle leash [B], command card. Batch 3: richer scenario, CMD_BUILD, smarter AI, soft collision, minimap. COrder behavior matrix + event ABI contract live here.
- `docs/superpowers/plans/` — executed implementation plans (m0-sim-foundation, systems a/b/c, foundation-batch1-sim); historical record. Note: 2c's `CWeapon.home_target` was replaced by `COrder.target` in Batch 1.

## Cross-cutting traps

1. **id 0 = player HQ = "none" sentinel** — enemy auto-acquire skips the player HQ; avoid new `== 0` id checks (cleanup: start `next_id_` at 1, touches id-based tests + goldens).
2. Snapshot pointer dies on next `sim_advance` — copy (SimBridge already does).
3. `exec_tick ==` match only — late commands silently never fire.
4. Mock ≠ real sim: immediate commands, no events, MOVE/STOP only, never ends.
5. GitHub default branch is `mailbox` → `gh pr create --base main`, always.
6. Godot 4.6.x `--headless --import` may crash (engine bug) — harmless; smoke test is the real check. `.uid`/`.import` files appear untracked when a newer editor opens the project.
7. `README.md:43` ("current simulation is a mock") is stale — real sim has been the default since PR #4.
