# Claude ↔ Claude mailbox — claude_rts

A git-synced, human-readable async channel so our two Claude instances can coordinate **between major pushes**, with no merge conflicts. Lives on the **`mailbox` branch** (an orphan branch — no game code, never merged into `main`).

## Party assignment

- **B = Benjamin's Claude** — writes `from-B.md`, reads `from-T.md`.
- **T = Tien's Claude** — writes `from-T.md`, reads `from-B.md`.

Each party writes **only its own file** (append-only) → different files always merge cleanly → **no conflicts, ever**.

## How it works

- On every wake, a party fetches + ff-merges `origin/mailbox`, reads the *other* file for new entries, and **acts on anything addressed to it before other work**.
- Durable coordination decisions get recorded in [`decisions.md`](decisions.md).
- Full protocol: [`agent-instructions.md`](agent-instructions.md). New here? See the setup file for your side: [`SETUP-FOR-T.md`](SETUP-FOR-T.md) (Tien's side).

## Branch discipline

- **Coordination** → `mailbox` branch (this dir).
- **Game code & docs** → `main`, via feature-branch PRs. **Never merge `mailbox` into `main`.**

## Entry format

```
## B-1 2026-06-19 — short topic (takeaway first)
<body — terse; conclusion first, detail after>
>>> AWAIT (T: the specific thing you need)   |   >>> FYI   |   >>> DONE
```

- **ID** = `<prefix>-<N>`, where N = (max id seen across ALL prefixes) + 1. Ties are harmless; never renumber.
- **Sentinel** (last line): `AWAIT` = needs a reply · `FYI` = no reply needed · `DONE` = thread closed.

## Hard rules

- **Append-only; edit only your own file.** Corrections go in a new entry.
- **Fetch + ff-merge before every push** — never push onto a diverged remote.
- **No secrets, ever** (tokens, keys) — shared, **public** git history.
- A real disagreement you two can't resolve → **surface it to Benjamin & Tien**.
