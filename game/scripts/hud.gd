class_name HudPanel
extends Label
## Minimal M0 HUD: tick + resource readout, pending-verb prompt, controls hint.

const VERB_LABELS := {"attack": "ATTACK-MOVE", "move": "MOVE", "patrol": "PATROL"}

func set_stats(tick: int, minerals: int, selected: int = 0, winner: int = 0, my_player: int = 1, pending_verb: String = "") -> void:
	if tick < 0:
		text = "SimBridge not loaded.\nBuild gdext — see docs/BUILD.md"
		return
	var banner := ""
	if winner == my_player:
		banner = "★ VICTORY ★\n\n"
	elif winner != 0:
		banner = "DEFEAT\n\n"
	var verb_line := ""
	if pending_verb != "":
		verb_line = ">>> %s — click target (Esc cancels)\n" % VERB_LABELS.get(pending_verb, pending_verb.to_upper())
	text = "%sTick: %d    Minerals: %d    Selected: %d\n%s\n[LMB] select  [RMB] smart command  [A]ttack-move [M]ove [P]atrol [S]top [H]old\n[T]/[E] train  [Ctrl+1-9]/[1-9] groups  [arrows/edges] pan  [wheel] zoom" % [banner, tick, minerals, selected, verb_line]
