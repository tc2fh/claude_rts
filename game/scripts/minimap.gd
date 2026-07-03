extends Control
## claude_rts — SC2-style minimap (bottom-left HUD overlay).
##
## Redraws every frame from the caches main.gd already keeps: terrain blocked
## cells, entity dots colored by owner, and the camera's visible world rect.
## LMB press/drag jumps the camera; RMB issues the smart context command at
## the corresponding world pixel. Owns no sim state — pure view + input relay.

signal camera_jump(world_px: Vector2)
signal ground_command(world_px: Vector2)

const SIZE_PX := 168.0

const COL_GROUND := Color(0.10, 0.12, 0.16)
const COL_BLOCKED := Color(0.30, 0.32, 0.40)
const COL_BORDER := Color(0.35, 0.40, 0.50)
const COL_VIEW := Color(1.0, 1.0, 1.0, 0.9)
const COL_PLAYER := Color(0.3, 0.7, 1.0)
const COL_ENEMY := Color(1.0, 0.4, 0.3)
const COL_NEUTRAL := Color(0.95, 0.82, 0.25)
const DOT := 3.0   ## entity dot size (minimap px)

var _map_w := 0
var _map_h := 0
var _passable: PackedByteArray = PackedByteArray()
var _px_per_unit := 32.0
var _cell := 0.0   ## minimap px per map cell (fit the larger dimension)

# Per-frame caches (shared layout with main.gd: _render stride 3, _meta stride 5).
var _ids: PackedInt32Array = PackedInt32Array()
var _render: PackedFloat32Array = PackedFloat32Array()
var _meta: PackedInt32Array = PackedInt32Array()
var _view_rect := Rect2()

var _lmb_down := false

func setup(map_w: int, map_h: int, passable: PackedByteArray, px_per_unit: float) -> void:
	_map_w = map_w
	_map_h = map_h
	_passable = passable
	_px_per_unit = px_per_unit
	_cell = SIZE_PX / float(maxi(maxi(map_w, map_h), 1))
	custom_minimum_size = Vector2(SIZE_PX, SIZE_PX)
	size = Vector2(SIZE_PX, SIZE_PX)
	queue_redraw()

func refresh(ids: PackedInt32Array, render: PackedFloat32Array, meta: PackedInt32Array, view_rect_world_px: Rect2) -> void:
	_ids = ids
	_render = render
	_meta = meta
	_view_rect = view_rect_world_px
	queue_redraw()

func _draw() -> void:
	var bounds := Rect2(Vector2.ZERO, Vector2(SIZE_PX, SIZE_PX))
	draw_rect(bounds, COL_GROUND, true)
	if _map_w > 0 and _passable.size() >= _map_w * _map_h:
		for y in _map_h:
			for x in _map_w:
				if _passable[y * _map_w + x] == 0:
					draw_rect(Rect2(Vector2(x, y) * _cell, Vector2(_cell, _cell)), COL_BLOCKED, true)
	for i in _ids.size():
		var mp := Vector2(_render[i * 3 + 0], _render[i * 3 + 1]) * _cell
		var half := DOT * 0.5
		draw_rect(Rect2(mp - Vector2(half, half), Vector2(DOT, DOT)), _dot_color(_meta[i * 5 + 1]), true)
	if _cell > 0.0 and _view_rect.size != Vector2.ZERO:
		var s := _cell / _px_per_unit
		var r := Rect2(_view_rect.position * s, _view_rect.size * s).intersection(bounds)
		if r.size.x > 0.0 and r.size.y > 0.0:
			draw_rect(r, COL_VIEW, false, 1.0)
	draw_rect(bounds, COL_BORDER, false, 1.0)

func _dot_color(owner_id: int) -> Color:
	match owner_id:
		1: return COL_PLAYER
		2: return COL_ENEMY
		_: return COL_NEUTRAL   # neutral / resource

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_lmb_down = event.pressed
			if event.pressed:
				camera_jump.emit(_to_world_px(event.position))
			accept_event()
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			ground_command.emit(_to_world_px(event.position))
			accept_event()
	elif event is InputEventMouseMotion and _lmb_down:
		camera_jump.emit(_to_world_px(event.position))
		accept_event()

func _to_world_px(local: Vector2) -> Vector2:
	if _cell <= 0.0:
		return Vector2.ZERO
	# minimap px -> world units (clamped to the map) -> world px.
	var wu := local / _cell
	wu.x = clampf(wu.x, 0.0, float(_map_w))
	wu.y = clampf(wu.y, 0.0, float(_map_h))
	return wu * _px_per_unit
