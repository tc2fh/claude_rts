class_name HudPanel
extends Label
## Minimal M0 HUD: tick + resource readout and a controls hint.

func set_stats(tick: int, minerals: int, selected: int = 0) -> void:
	if tick < 0:
		text = "SimBridge not loaded.\nBuild gdext — see docs/BUILD.md"
		return
	text = "Tick: %d    Minerals: %d    Selected: %d\n\n[LMB] drag-select / click   [RMB] move\n[WASD / arrows / edges] pan   [wheel] zoom" % [tick, minerals, selected]
