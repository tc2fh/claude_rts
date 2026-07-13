extends Camera2D
## RTS camera: arrow-key / screen-edge pan, zoom-to-cursor mouse wheel.
## No WASD - those keys are SC2-style command hotkeys (see main.gd).
## Operates in the same world (pixel) space the units are drawn in.

const PAN_SPEED := 800.0    ## px/sec at zoom 1
const EDGE_MARGIN := 24.0   ## px from a window edge that starts panning
const ZOOM_STEP := 1.1
const ZOOM_MIN := 0.4
const ZOOM_MAX := 3.0
const TOP_HUD_SAFE_PX := 122.0

var _world_bounds := Rect2()

func _ready() -> void:
	make_current()
	zoom = Vector2.ONE

func configure_bounds(bounds: Rect2) -> void:
	_world_bounds = bounds
	position = bounds.get_center() - Vector2(0.0, TOP_HUD_SAFE_PX * 0.5 / zoom.y)
	_clamp_to_world()

func _process(delta: float) -> void:
	var dir := Vector2.ZERO
	if Input.is_key_pressed(KEY_LEFT):
		dir.x -= 1.0
	if Input.is_key_pressed(KEY_RIGHT):
		dir.x += 1.0
	if Input.is_key_pressed(KEY_UP):
		dir.y -= 1.0
	if Input.is_key_pressed(KEY_DOWN):
		dir.y += 1.0

	# Screen-edge pan (only while the mouse is actually inside the window,
	# else an unfocused window pans itself off the map).
	var vp := get_viewport().get_visible_rect().size
	var m := get_viewport().get_mouse_position()
	if Rect2(Vector2.ZERO, vp).has_point(m):
		if m.x <= EDGE_MARGIN:
			dir.x -= 1.0
		elif m.x >= vp.x - EDGE_MARGIN:
			dir.x += 1.0
		if m.y <= EDGE_MARGIN:
			dir.y -= 1.0
		elif m.y >= vp.y - EDGE_MARGIN:
			dir.y += 1.0

	if dir != Vector2.ZERO:
		# Divide by zoom so pan speed feels constant on screen across zoom levels.
		position += dir.normalized() * PAN_SPEED * delta / zoom.x
		_clamp_to_world()

func center_on(world_px: Vector2) -> void:
	position = world_px
	_clamp_to_world()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_apply_zoom(ZOOM_STEP)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_apply_zoom(1.0 / ZOOM_STEP)

func _apply_zoom(factor: float) -> void:
	var z := clampf(zoom.x * factor, ZOOM_MIN, ZOOM_MAX)
	if z == zoom.x:
		return
	# Keep the world point under the cursor fixed across the zoom change.
	var mouse_world := get_global_mouse_position()
	zoom = Vector2(z, z)
	var vp := get_viewport()
	position = mouse_world - (vp.get_mouse_position() - vp.get_visible_rect().size * 0.5) / z
	_clamp_to_world()

func _clamp_to_world() -> void:
	if _world_bounds.size == Vector2.ZERO:
		return
	var half_view := get_viewport().get_visible_rect().size * 0.5 / zoom
	position.x = _clamp_axis(position.x, _world_bounds.position.x, _world_bounds.end.x, half_view.x)
	position.y = _clamp_axis(
		position.y,
		_world_bounds.position.y,
		_world_bounds.end.y,
		half_view.y,
		TOP_HUD_SAFE_PX / zoom.y
	)

func _clamp_axis(
	value: float,
	minimum: float,
	maximum: float,
	half_view: float,
	start_inset: float = 0.0
) -> float:
	var low := minimum + half_view - start_inset
	var high := maximum - half_view
	if low > high:
		return (low + high) * 0.5
	return clampf(value, low, high)
