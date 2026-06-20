extends Node2D
## claude_rts — M0 view harness.
##
## Drives the (currently mock) sim through the SimBridge GDExtension at a fixed
## 24 Hz, renders interpolated snapshot entities, and turns mouse input into
## box-selection + move commands. All sim access goes through SimBridge — the
## view never touches sim internals or fixed-point math directly.

const TICK_HZ := 24.0
const TICK_DT := 1.0 / TICK_HZ
const PX_PER_UNIT := 32.0   ## world units -> pixels
const UNIT_RADIUS := 10.0   ## draw radius (px)
const SEED := 1234
const MAP_ID := 0

var _sim = null             ## untyped: methods are GDExtension-provided (dynamic dispatch)
var _accum := 0.0
var _alpha := 0.0

# Render caches, refreshed each frame from the bridge.
var _ids: PackedInt32Array = PackedInt32Array()
var _render: PackedFloat32Array = PackedFloat32Array()  # [x, y, facing_rad] * n (world units)
var _meta: PackedInt32Array = PackedInt32Array()        # [type, owner, state, hp, hp_max] * n

var _selected := {}                 # id -> true
var _dragging := false
var _drag_start := Vector2.ZERO     # world-space (camera-correct)
var _drag_now := Vector2.ZERO

@onready var _hud: HudPanel = $HUD/Panel

func _ready() -> void:
	if not ClassDB.class_exists("SimBridge"):
		push_error("SimBridge GDExtension not loaded — build gdext (see docs/BUILD.md).")
		set_process(false)
		set_process_input(false)
		_hud.set_stats(-1, -1)
		return
	_sim = ClassDB.instantiate("SimBridge")
	_sim.create(SEED, MAP_ID)

func _process(delta: float) -> void:
	if _sim == null:
		return
	_accum += delta
	while _accum >= TICK_DT:
		_sim.advance(1)
		_accum -= TICK_DT
	_alpha = clampf(_accum / TICK_DT, 0.0, 1.0)

	_ids = _sim.entity_ids()
	_render = _sim.render_state(_alpha)
	_meta = _sim.entity_meta()
	_hud.set_stats(int(_sim.tick()), int(_sim.get_resource(1)))
	queue_redraw()

func _world_px(i: int) -> Vector2:
	return Vector2(_render[i * 3 + 0], _render[i * 3 + 1]) * PX_PER_UNIT

func _draw() -> void:
	# Selection marquee (drawn in world space; get_global_mouse_position is camera-correct).
	if _dragging:
		var r := _rect_from(_drag_start, _drag_now)
		draw_rect(r, Color(0.3, 0.8, 1.0, 0.15), true)
		draw_rect(r, Color(0.4, 0.9, 1.0, 0.9), false, 1.5)

	var n := _ids.size()
	for i in n:
		var p := _world_px(i)
		var col := _owner_color(_meta[i * 5 + 1])
		draw_circle(p, UNIT_RADIUS, col)
		# Facing tick.
		var f: float = _render[i * 3 + 2]
		draw_line(p, p + Vector2(cos(f), sin(f)) * UNIT_RADIUS, Color.WHITE, 1.5)
		# Selection ring.
		if _selected.has(_ids[i]):
			draw_arc(p, UNIT_RADIUS + 3.0, 0.0, TAU, 24, Color(1.0, 1.0, 0.25, 0.9), 2.0)

func _owner_color(owner_id: int) -> Color:
	match owner_id:
		0: return Color(0.7, 0.7, 0.7)   # neutral
		1: return Color(0.3, 0.7, 1.0)   # player
		_: return Color(1.0, 0.4, 0.3)   # enemy

func _rect_from(a: Vector2, b: Vector2) -> Rect2:
	var tl := Vector2(minf(a.x, b.x), minf(a.y, b.y))
	var br := Vector2(maxf(a.x, b.x), maxf(a.y, b.y))
	return Rect2(tl, br - tl)

func _input(event: InputEvent) -> void:
	if _sim == null:
		return
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed:
				_dragging = true
				_drag_start = get_global_mouse_position()
				_drag_now = _drag_start
			else:
				_drag_now = get_global_mouse_position()
				_apply_selection()
				_dragging = false
				queue_redraw()
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			_issue_move(get_global_mouse_position())
	elif event is InputEventMouseMotion and _dragging:
		_drag_now = get_global_mouse_position()
		queue_redraw()

func _apply_selection() -> void:
	_selected.clear()
	var n := _ids.size()
	if _drag_start.distance_to(_drag_now) < 6.0:
		# Treat as a click: pick the nearest entity under the cursor.
		var best := -1
		var best_d := UNIT_RADIUS * 1.6
		for i in n:
			var d := _world_px(i).distance_to(_drag_now)
			if d < best_d:
				best_d = d
				best = i
		if best >= 0:
			_selected[_ids[best]] = true
	else:
		var r := _rect_from(_drag_start, _drag_now)
		for i in n:
			if r.has_point(_world_px(i)):
				_selected[_ids[i]] = true

func _issue_move(target_px: Vector2) -> void:
	if _selected.is_empty():
		return
	var wx := target_px.x / PX_PER_UNIT
	var wy := target_px.y / PX_PER_UNIT
	for id in _selected.keys():
		_sim.command_move(int(id), wx, wy)
