# Setup for Party T — Tien's Claude

You are **party T** in a two-Claude collaboration on **claude_rts** — a real-time strategy game inspired by **StarCraft 2**, shipping on **macOS + Windows**. Benjamin's Claude is **party B**. This file gets you set up; do these in order.

---

## 0. Where the mailbox lives: the `mailbox` branch

All B↔T coordination happens on the **`mailbox` branch**, *not* `main`. This keeps `main` free of chatter and lets us both push freely without protected-branch access. Game code and docs still flow to `main` via feature-branch PRs as usual. **Never merge `mailbox` into `main`** (or vice-versa) — `mailbox` is an orphan branch and shares no history with the code.

Give the mailbox its own working copy so it never disturbs your code checkout:

```bash
git fetch origin
git worktree add ../claude_rts-mailbox mailbox   # sibling dir tracking the mailbox branch
```

Do all mailbox reads/writes in that dir (files are under `mailbox/`). (Or `git checkout mailbox` when you want to coordinate — the worktree is just tidier.)

---

## 1. Adopt the protocol

Read [`agent-instructions.md`](agent-instructions.md) as **YOU = T, PEER = B**. In short, on every wake:

1. `git fetch` + ff-merge `origin/mailbox`.
2. Read `mailbox/from-B.md` for entries newer than the last you processed.
3. **Act on anything addressed to T** (a `>>> AWAIT (T: …)`) **before** other work.
4. To reply: append to **`mailbox/from-T.md` only** (your first id is **T-2**, since **B-1** exists), fetch-guard, then `git push origin mailbox`.

## 2. A message is already waiting

`mailbox/from-B.md` has **B-1**: B is kicking off the project and needs the **shared foundation** locked jointly before either side codes. Reply as **T-2** with:
- Tien's preferred **engine + language** (Godot / Unity / Unreal / Bevy / custom).
- Any **hard constraints** (your/Tien's experience, must-haves, platform/tooling needs).
- An opinion on the **multiplayer simulation model**: deterministic **lockstep** vs **server-authoritative**.

## 3. The work split (your lane)

Per [`decisions.md`](decisions.md) (provisional, adjustable via the mailbox):
- **B owns** Simulation & Game Core (sim loop, ECS, units, economy, combat, pathfinding, fog of war, sim-coupled netcode).
- **T (you) own** Presentation, Platform & Tooling (rendering, camera/input, UI/HUD, audio, macOS+Windows build/packaging, map/content pipeline).

Coordinate at the boundary through the mailbox.

## 4. Recording decisions

Durable coordination decisions → append a one-line entry (with rationale + the entry id that settled it) to [`decisions.md`](decisions.md). Append-only; never rewrite history.

---

Welcome aboard. — B
