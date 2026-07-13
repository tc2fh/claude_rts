class_name HudPanel
extends PanelContainer
## Structured match HUD with live economy, selection, status, and command hints.

const VERB_LABELS := {
	"attack": "ATTACK-MOVE",
	"move": "MOVE",
	"patrol": "PATROL",
}

@onready var _tick_label: Label = $Margin/Rows/Header/Tick
@onready var _minerals_label: Label = $Margin/Rows/Header/Minerals
@onready var _selected_label: Label = $Margin/Rows/Header/Selected
@onready var _status_label: Label = $Margin/Rows/Status

func set_stats(
	tick: int,
	minerals: int,
	selected: int = 0,
	winner: int = 0,
	my_player: int = 1,
	pending_verb: String = ""
) -> void:
	if not is_node_ready():
		await ready
	if tick < 0:
		_tick_label.text = "SIM OFFLINE"
		_minerals_label.text = "MINERALS ----"
		_selected_label.text = "SELECTED --"
		_status_label.text = "BUILD GDEXT  //  SEE DOCS/BUILD.MD"
		_status_label.add_theme_color_override("font_color", Color("ff7b72"))
		return

	_tick_label.text = "TICK %06d" % tick
	_minerals_label.text = "MINERALS %04d" % minerals
	_selected_label.text = "SELECTED %02d" % selected

	if winner == my_player:
		_status_label.text = "MATCH COMPLETE  //  VICTORY"
		_status_label.add_theme_color_override("font_color", Color("6ee7a8"))
	elif winner != 0:
		_status_label.text = "MATCH COMPLETE  //  DEFEAT"
		_status_label.add_theme_color_override("font_color", Color("ff7b72"))
	elif pending_verb != "":
		var verb := str(VERB_LABELS.get(pending_verb, pending_verb.to_upper()))
		_status_label.text = "COMMAND  //  %s  //  CHOOSE TARGET  //  ESC TO CANCEL" % verb
		_status_label.add_theme_color_override("font_color", Color("ffd166"))
	else:
		_status_label.text = "COMMAND  //  READY"
		_status_label.add_theme_color_override("font_color", Color("83d9ff"))
