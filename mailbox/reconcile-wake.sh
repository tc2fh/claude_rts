#!/usr/bin/env bash
# Mailbox reconcile-wake (claude_rts). OPTIONAL — only for long-running/unattended agents.
#
# Run this on each self-scheduled wake (your agent's recurring scheduler, e.g. every ~2 min). It:
#   1. fetches + ff-merges origin/mailbox (always integrate; safe if your local is ahead),
#   2. scans each peer's file for entries newer than a per-prefix marker (so a cross-prefix id tie can't
#      make you skip a message — tracking a single max number would),
#   3. prints the new entries (so your agent processes them) OR "END SILENTLY" (nothing for you).
# After your agent processes + pushes its reply, it runs the printed `echo N > marker` lines to advance.
#
# Config via env (or edit the defaults). The mailbox files live in mailbox/ on the `mailbox` branch;
# set MAILBOX_REPO to that dir (e.g. MAILBOX_REPO=/path/to/claude_rts-mailbox/mailbox) or run from there.
REPO="${MAILBOX_REPO:-$(pwd)}"                 # the dir holding from-*.md (e.g. .../claude_rts-mailbox/mailbox)
MY="${MAILBOX_ME:-B}"                          # YOUR prefix (Benjamin's side = B; Tien's side: set MAILBOX_ME=T)
PEERS="${MAILBOX_PEERS:-T}"                     # space-separated peer prefixes you read (Tien's side: set MAILBOX_PEERS=B)
ADDR="${MAILBOX_ADDR:-}"                        # optional regex: only surface entries matching it (N-party
                                               # routing, e.g. 'YOU|→ ?B'); EMPTY = surface ALL peer entries (2-party)
MARK_DIR="${MAILBOX_MARKS:-$HOME/.local/state/mailbox-$MY-markers}"

cd "$REPO" || exit 0
git fetch origin -q 2>/dev/null
git merge --ff-only origin/mailbox -q 2>/dev/null
mkdir -p "$MARK_DIR"

FILES=""; for P in $PEERS; do FILES="$FILES from-$P.md"; done
newest(){ grep -hoE "^## $1-[0-9]+" $FILES 2>/dev/null | grep -oE '[0-9]+' | sort -n | tail -1; }

NEW=""
for P in $PEERS; do
  MARK="$(cat "$MARK_DIR/$P" 2>/dev/null || echo 0)"; MARK="${MARK:-0}"
  while IFS= read -r line; do
    id="$(printf '%s' "$line" | grep -oE "^## $P-[0-9]+" | grep -oE '[0-9]+')"
    [ -z "$id" ] && continue
    if [ "$id" -gt "$MARK" ] && { [ -z "$ADDR" ] || printf '%s' "$line" | grep -qiE "$ADDR"; }; then
      NEW="${NEW}${line}"$'\n'
    fi
  done < <(grep -hE "^## $P-[0-9]+ " $FILES 2>/dev/null)
done

if [ -n "$NEW" ]; then
  echo "RECONCILE: NEW ENTRIES FOR $MY:"
  printf '%s' "$NEW"
  echo "AFTER you process + push your reply, advance markers:"
  for P in $PEERS; do n="$(newest $P)"; [ -n "$n" ] && echo "  echo $n > $MARK_DIR/$P"; done
else
  for P in $PEERS; do n="$(newest $P)"; [ -n "$n" ] && echo "$n" > "$MARK_DIR/$P"; done
  echo "RECONCILE: nothing new for $MY. Markers advanced. END SILENTLY."
fi
