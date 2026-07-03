# Batch 2 — Idle auto-acquire with leash / return-to-post

**Status:** implemented (branch `feat/batch2-idle-leash`). Frozen decisions below; deferred from Batch 1 ("idle unit steps forward a little to engage, then returns").

**Goal:** SC2 stop-vs-hold feel. An idle (`ORD_STOP`) armed unit steps out to engage enemies that come near, chases only up to a leash distance from its post, then returns to the post. `ORD_HOLD` keeps the strict never-move guarantee — that is the SC2 distinction and why Batch 1 kept them separate.

## Frozen decisions

- **Stop = leash, hold = strict.** `sys_combat` `ORD_STOP` branch mirrors the `ORD_ATTACK_MOVE` acquire/chase machinery (same strict `d < best` id-sorted acquisition skipping own units and neutrals, same chase/path code, same `SIM_EVT_ATTACK` on fire), gated by the leash:
  - acquire nearest enemy within `ACQUIRE_RANGE` (7);
  - if acquired AND Chebyshev(cell, anchor) <= `LEASH_RANGE` → fire in weapon range (path cleared) else chase (repath toward the target);
  - otherwise → path back to the anchor if away from it (repath only when the path isn't already heading there), stand when on it.
  - `ORD_HOLD` (and weapon-bearing entities without a `COrder`) keep the Batch-1 defensive rule: fire only within weapon range, never self-move.
- **`LEASH_RANGE = 10`** (Chebyshev cells from the anchor; `sim/include/sim/constants.h`). Max observed excursion is `LEASH_RANGE + 1` — the unit crosses into the 11th cell before the leash check sees it.
- **Anchor rules.** `COrder.anchor` (previously patrol-only) doubles as the `ORD_STOP` post. Every site that assigns `ORD_STOP` sets it to the unit's current/arrival cell: `spawn_initial()` and `sys_production()` spawns, the `CMD_STOP` handler (**re-anchors** — a stop mid-chase makes that spot the new post), `maybe_complete_order()` `ORD_MOVE`/`ORD_ATTACK_MOVE` → `ORD_STOP` arrivals, and the `sys_combat` `ORD_ATTACK_TARGET` target-gone transition.
- **Anchor stays OUT of `state_hash`** — like `dest`/`to_dest` it is derived deterministically from command history, not independent state.
- **Both sides leash.** The enemy scout leash-chases too once its standing `ORD_ATTACK_TARGET` is replaced; tests neutralize it (HOLD) or keep it passive/parked outside acquire range.

## Goldens

None changed: `test_determinism` full-M0 `0xdd58708ee3f85ad4`, `test_orders` order-scenario `0xbbf4a1ef823f7504`, `test_train` soldier-training `0x7dabe2bc54f61b64` all still pass unmodified — in those scenarios idle armed units only ever see enemies already inside weapon range (where leash behavior coincides with the old fire-in-place rule) or none at all. New pinned print: `[leash] engage-and-return hash = 0x73e40f22386bd3c0` (batching-invariance checked in-test, not hardcoded).

## Tests

`sim/tests/test_leash.cpp`: engage-and-return to the exact post center; leash cutoff (fleeing enemy survives, excursion in `[LEASH_RANGE, LEASH_RANGE+1]`); HOLD bit-frozen under the same bait; CMD_STOP mid-walk re-anchor; 300-tick chunking invariance (1/3/5/300) + twin-world reproducibility.
