# Foundation Pass — Batch 1 Design (unit orders + event channel + view feel)

**Status:** design / brainstorming output. Approved at the shape level (SC2 classic keys; B generates synth SFX placeholders; batch-1 split = sim commands+events ‖ view hotkeys/zoom/SFX). Awaiting written-spec review before plans.

**Goal:** Turn the playable-but-bare M0 into something that *feels* like an RTS — real SC2-style unit commands (move / attack-move / hold / patrol / stop), a sim→view **event channel** that drives combat audio (and later VFX/UI), SC2 hotkeys, zoom-to-cursor, and placeholder sound effects. Greybox throughout.

**Architecture:** Same two-lane split as M0. **B (sim core)** owns the deterministic *behaviors* + the event-channel ABI contract. **T (view)** owns the *feel* — hotkeys, audio playback, camera. The seam stays the C ABI (`sim_abi.h`); this pass extends it additively (new command enum values + a drainable event API). B generates the placeholder WAV assets; T wires playback.

---

## Foundational roadmap (the full list; this spec details Batch 1 only)

- **Batch 1 — control + combat feel** *(this spec)*
  - `[B]` Unit orders: move (passive), attack-move, hold, patrol, stop — a `COrder` stance system
  - `[B]` Sim event channel (attack-hit / trained / died) over the ABI
  - `[B]` Placeholder SFX assets (synthesized WAVs)
  - `[T]` SC2 hotkeys (A/M/S/H/P + click flow), zoom-to-cursor, SFX playback wired to events + command-issue
- **Batch 2 — control depth & polish** *(later)*: control groups (Ctrl+1–9 / 1–9 / double-tap center), shift-queued waypoints, double-click select-type, command-feedback markers, hit-flash VFX.
- **Batch 3 — macro & game-feel** *(later)*: richer starting scenario (real army, multiple nodes, enemy base), `CMD_BUILD` behavior, smarter enemy AI, soft unit collision, minimap.

---

## Batch 1 — Sim lane (B)

### 1. Unit orders / stance (`COrder`)

**Problem:** today every armed unit is *always aggressive* — `sys_combat` auto-acquires any enemy in `ACQUIRE_RANGE` (7) and chases it, for every unit with a `CWeapon`, every tick. There is no passive move, no hold, no patrol. So `CMD_MOVE` is effectively attack-move, and you can't tell a unit to ignore enemies or hold ground.

**Design:** introduce an explicit per-unit order that `sys_combat` and `sys_movement` read. New component:

```
enum OrderKind : uint8_t { ORD_STOP, ORD_MOVE, ORD_ATTACK_MOVE, ORD_HOLD, ORD_PATROL, ORD_ATTACK_TARGET };
struct COrder {
    OrderKind kind = ORD_STOP;
    GridPos   dest{};        // move / attack-move / current patrol leg target
    GridPos   anchor{};      // patrol: the other endpoint
    bool      to_dest = true;// patrol: which endpoint we're heading toward
    EntityId  target = 0;    // ORD_ATTACK_TARGET: the forced target
};
```

`COrder` becomes the single source of unit intent and **replaces `CWeapon.home_target`** (the old standing-attack field).

**Behavior matrix** — each order defines an *engagement* rule (used by `sys_combat`) and a *movement* rule (used by `sys_movement`):

| Order | Engagement (sys_combat) | Movement |
|---|---|---|
| `ORD_MOVE` | **passive** — ignore enemies | path to `dest`; on arrival → `ORD_STOP` |
| `ORD_ATTACK_MOVE` | **aggressive** — acquire in `ACQUIRE_RANGE`, chase + attack | when no live target, (re)path to `dest`; on arrival → `ORD_STOP` |
| `ORD_HOLD` | **defensive** — attack only what's already in weapon `range_cells`, never chase | never self-move |
| `ORD_STOP` (default/idle) | **defensive** (same as hold) | never self-move |
| `ORD_PATROL` | **aggressive** | path to current endpoint; on arrival flip `to_dest` and path to the other; loop. After combat, resume toward current endpoint |
| `ORD_ATTACK_TARGET` | chase + attack `target` | chase `target`; when it dies/vanishes → `ORD_STOP` |

Key change vs today: **acquisition is gated by stance** — `sys_combat` skips acquisition entirely for `ORD_MOVE`, restricts it to weapon range for `ORD_HOLD`/`ORD_STOP`, and (the current behavior) chases for the aggressive orders. After an aggressive unit loses its target it must **resume its order's movement** (re-path to `dest`/patrol endpoint) rather than just halting.

**New command types** (append to `SimCommandType`, preserving existing ordinals):
`CMD_ATTACK_MOVE` (uses `tx,ty`), `CMD_HOLD` (no args), `CMD_PATROL` (uses `tx,ty`). Existing commands re-map onto orders: `CMD_MOVE`→`ORD_MOVE` (now **passive**), `CMD_ATTACK`(target)→`ORD_ATTACK_TARGET`, `CMD_STOP`→`ORD_STOP`. The enemy-AI scout's old `home_target` becomes `ORD_ATTACK_TARGET(player HQ)`.

**Determinism note:** this changes combat/movement behavior, so the M0 golden `0xec6d7413e5a86926`, the soldier golden `0x805b7bda1d922368`, and the movement/combat scenario hashes **will change and be re-pinned**. Existing integration tests that relied on aggressive-move (e.g. "player wins" by moving a soldier near the enemy) must issue explicit `CMD_ATTACK_MOVE`/`CMD_ATTACK`. No float/trig; id-sorted iteration preserved; orders are deterministic state and enter `state_hash`.

### 2. Sim event channel

Discrete per-tick events the view drains to drive audio (now) and VFX/damage-numbers/combat-log (later). Output only — not game state, but generated deterministically.

```
typedef enum { SIM_EVT_ATTACK = 1, SIM_EVT_TRAINED = 2, SIM_EVT_DIED = 3 } SimEventType;
typedef struct { uint16_t type; uint32_t a, b; uint64_t tick; } SimEvent;
uint32_t sim_drain_events(SimWorld*, SimEvent* out, uint32_t max);  // copies <= max, clears queue, returns count
```

- `SIM_EVT_ATTACK` — `a` = attacker, `b` = target (a hit landed; instant-hit model, so "fired" == "hit"). Drives the attack/hit SFX.
- `SIM_EVT_TRAINED` — `a` = producer (HQ), `b` = newly spawned unit. Drives the train-complete SFX.
- `SIM_EVT_DIED` — `a` = dead unit, `b` = killer (0 if none). Drives the death SFX.

`World` accumulates events into a vector each tick (emitted from `sys_combat` on damage, `sys_production` on spawn, `sys_death` on death). `sim_drain_events` copies out and clears. The view drains once per frame (after its advance loop). Queue is bounded (cap ~4096; drop excess — single-player view always drains, so this is just a memory backstop). **Events are excluded from `state_hash`** (transient output), but a test asserts the event *stream* is reproducible + batching-invariant for a fixed scenario.

### 3. Placeholder SFX assets (B generates)

Synthesize a handful of short WAVs (pure-stdlib Python: `wave` + `math`, no deps) into `game/assets/sfx/` for T to load via `res://assets/sfx/`. Greybox tones/noise:
- `cmd_move.wav` — soft blip (move order issued)
- `cmd_attack.wav` — sharper blip (attack/attack-move order issued)
- `cmd_build.wav` — two-tone confirm (train/build order issued)
- `hit.wav` — short noise thud (per `SIM_EVT_ATTACK`)
- `train_done.wav` — rising chime (per `SIM_EVT_TRAINED`)
- `death.wav` — descending blip (per `SIM_EVT_DIED`)

---

## Batch 1 — View lane (T)

### 1. SC2 classic hotkeys (`main.gd`)
Two-step "verb then click" for targeted commands, immediate for the rest. Operates on the current selection.
- **A** → attack: next left-click on a unit = `command_attack(target)`; on ground = `command_attack_move(x,y)`.
- **M** → move: next left-click = `command_move(x,y)` (now passive).
- **P** → patrol: next left-click = `command_patrol(x,y)`.
- **S** → `command_stop()` (immediate). **H** → `command_hold()` (immediate).
- **Right-click** keeps the existing smart/context behavior: enemy = `command_attack(target)`, resource node = `command_harvest`, **ground = `command_move` (passive)** — SC2-faithful (right-click ground is a plain Move; the aggressive variant is A-click attack-move). `M`-click is the same passive move with explicit verb.
- Train stays **E** = worker, **T** = soldier (greybox; a real command card is Batch 2).
- Add the three new bridge methods: `command_attack_move(unit, x, y)`, `command_hold(unit)`, `command_patrol(unit, x, y)` → push `CMD_ATTACK_MOVE`/`CMD_HOLD`/`CMD_PATROL`.

### 2. Zoom-to-cursor (`camera_rig.gd`)
`_apply_zoom` currently only scales `zoom`, so it pins to screen center. Change it to keep the world point under the cursor fixed: capture the world position under the mouse before the zoom change, apply the new zoom, then offset `position` so that same world point is under the cursor again (standard `Camera2D` zoom-to-point math using `get_global_mouse_position()` / viewport center).

### 3. Audio playback
- An `AudioStreamPlayer` pool (or a couple of players) loading the `res://assets/sfx/*.wav` placeholders.
- **Command-issue sounds** (pure view, no sim needed): play `cmd_move`/`cmd_attack`/`cmd_build` when the player issues the corresponding command in `_input`.
- **Event sounds:** each frame, after advancing, call `sim_drain_events` (new bridge method `drain_events()` returning a typed array) and play `hit` / `train_done` / `death` per event type. Throttle/cap simultaneous plays so a big fight doesn't machine-gun the mixer.

---

## Lane split & parallelization

| Item | Lane | Depends on |
|---|---|---|
| `COrder` + order behaviors + new `CMD_*` | B | — |
| Event channel (`sim_drain_events`) | B | — (additive ABI) |
| Placeholder WAVs | B | — |
| `command_attack_move`/`hold`/`patrol` + `drain_events()` in bridge | T | the additive ABI contract (below) — **available now from this spec** |
| SC2 hotkeys | T | bridge methods above |
| Zoom-to-cursor | T | nothing (pure view) — can start immediately |
| SFX playback | T | the WAVs (B) + `drain_events` (B); can stub silently first |

T can start **zoom-to-cursor** and the **hotkey input scaffolding** immediately against the contract; B delivers the sim behaviors + events + WAVs in parallel. Integration point: B's PR lands the new commands/events/assets; T's PR consumes them. Determinism goldens re-pin on B's side (T's CI matrix auto-confirms cross-platform).

**Seam contract handed to T now (additive to `sim_abi.h`):**
- `SimCommandType` gains `CMD_ATTACK_MOVE`, `CMD_HOLD`, `CMD_PATROL` (appended).
- `SimEvent { uint16_t type; uint32_t a, b; uint64_t tick; }`, `SimEventType { SIM_EVT_ATTACK=1, SIM_EVT_TRAINED=2, SIM_EVT_DIED=3 }`, `uint32_t sim_drain_events(SimWorld*, SimEvent* out, uint32_t max)`.

---

## Testing
- **Sim (B):** TDD per order — passive move ignores an adjacent enemy; attack-move engages then resumes to dest; hold attacks in-range but never moves; patrol loops + engages; stop halts + defends. Event tests: a fight emits `SIM_EVT_ATTACK`s, training emits `SIM_EVT_TRAINED`, a death emits `SIM_EVT_DIED`; event stream is reproducible + batching-invariant. Re-pin all affected determinism goldens; keep batching-invariance.
- **View (T):** headless smoke covers the new bridge methods load + drain returns; manual playtest for feel.

## Risks / open questions
- **Move-semantics change is player-visible:** today armed units auto-fight on any move; after this, plain Move (right-click ground / M) is **passive** and you must A-click (attack-move) to engage en route. This is SC2-faithful but changes how the current build plays — worth confirming on playtest.
- **Hold/Stop are functionally identical** in this MVP (defensive, no-chase); kept as separate orders for SC2 parity + future divergence (hold-fire, leash).
- **Event throttling** is a view concern; the sim just reports truthfully.
