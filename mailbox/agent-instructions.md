# Mailbox protocol — instructions for your Claude

> **claude_rts assignment:** **B = Benjamin's Claude**, **T = Tien's Claude.**
> Set the placeholders per party: Benjamin's side → **YOU = `B`, PEER = `T`**; Tien's side → **YOU = `T`, PEER = `B`**.
> All mailbox files live in **`mailbox/`** on the **`mailbox` branch** of the shared repo (its own worktree — never merge it into `main`).

---

You are **party `YOU`** in a git-mailbox shared with another Claude (**party `PEER`**) working in parallel.
You coordinate — ideas, decisions, questions, hand-offs — through append-only files in this repo. You are not
the same agent; treat `PEER` as a capable collaborator whose messages you must read and answer.

## Reconcile on every wake (do this FIRST, before any other work)

1. `git fetch` and ff-merge `origin/mailbox` (or rebase your own un-pushed commits — never push onto a diverged remote).
2. Read **`mailbox/from-PEER.md`** for every entry with an ID newer than the last one you processed.
3. **Act on anything addressed to you** (a `>>> AWAIT (YOU: …)`, or a question/decision that needs you)
   **before** you start other work. Unanswered `AWAIT`s from `PEER` are your highest priority.

## To send a message

1. Append a new entry to **`mailbox/from-YOU.md` only** (never edit `from-PEER.md` or any prior entry —
   corrections go in a new entry).
2. Format:
   ```
   ## YOU-<N> <date> — <short topic, takeaway first>
   <body: terse, conclusion first, detail after>
   >>> AWAIT (PEER: <the specific thing you need>)   ← or  >>> FYI   or  >>> DONE
   ```
   - **`<N>`** = (max entry-id seen across ALL prefixes) + 1. Ties across prefixes are harmless; never renumber.
   - **Sentinel** (last line): `AWAIT` if you need a response; `FYI` if not; `DONE` to close a thread.
3. **Fetch-guard, then push:** `git fetch` + ff-merge/rebase → `git add mailbox/from-YOU.md` → commit → `git push origin mailbox`.
   If rejected (diverged), fetch + ff-merge/rebase and push again. Never force-push.

## What to put in the mailbox

- Ideas + proposals, questions, decisions you're making, hand-offs ("finished X, ready for you to Y"),
  blockers, and status when it affects the other party.
- **Lead with the takeaway.** Keep routine chatter out — message when it affects `PEER` or needs their input.

## Decisions are recorded, not just messaged

When you reach a durable decision (an approach, a contract, a convention), record it in **`mailbox/decisions.md`**
with a one-line rationale — append-only. The mailbox transports the discussion; `decisions.md` is the lasting record.

## Code & docs vs coordination

- **Coordination** (this mailbox) lives on the **`mailbox`** branch only.
- **Game code & docs** flow to **`main`** via normal feature-branch PRs. **Never merge `mailbox` into `main`** (or vice-versa) — `mailbox` is an orphan branch and shares no history with the code.

## Hard floors

- **No secrets ever** in any entry or commit — this is git history both sides read (and the repo is **public**).
- **Append-only**, own file only. Don't rewrite history; don't edit the peer.
- If you and `PEER` reach a genuine disagreement you can't resolve, **surface it to the humans** (Benjamin & Tien).

## (Optional) run unattended

If you're a long-running/headless agent, set up a recurring self-wake that runs `reconcile-wake.sh` each tick:
it fetches, surfaces any new entries addressed to you (or says "END SILENTLY"), and you process + push. The script
defaults to **`MY=B`, `PEERS=T`**, ff-merges **`origin/mailbox`**, and expects to run from the **`mailbox/`** dir
(or set `MAILBOX_REPO` to it). Tien's side: run it with `MAILBOX_ME=T MAILBOX_PEERS=B`.
