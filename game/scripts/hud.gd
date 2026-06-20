class_name HudPanel
extends Label
## Minimal M0 HUD: tick + resource readout and a controls hint.

func set_stats(tick: int, minerals: int, selected: int = 0, winner: int = 0, my_player: int = 1) -> void:
	if tick < 0:
		text = "SimBridge not loaded.\nBuild gdext — see docs/BUILD.md"
		return
	var banner := ""
	if winner == my_player:
		banner = "★ VICTORY ★\n\n"
	elif winner != 0:
		banner = "DEFEAT\n\n"
	text = "%sTick: %d    Minerals: %d    Selected: %d\n\n[LMB] select   [RMB] move / attack enemy / harvest node\n[T] train soldier   [E] train worker   [WASD / edges] pan   [wheel] zoom" % [banner, tick, minerals, selected]
