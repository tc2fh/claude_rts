extends Camera2D
## RTS camera: WASD / arrows / screen-edge pan, mouse-wheel zoom.
## Operates in the same world (pixel) space the units are drawn in.

const PAN_SPEED := 800.0    ## px/sec at zoom 1
const EDGE_MARGIN := 24.0   ## px from a window edge that starts panning
const ZOOM_STEP := 1.1
const ZOOM_MIN := 0.4
const ZOOM_MAX := 3.0

func _ready() -> void:
	make_current()
	zoom = Vector2.ONE
	# Center on the ~32x32-unit mock world (PX_PER_UNIT = 32 in main.gd).
	position = Vector2(16.0 * 32.0, 16.0 * 32.0)

func _process(delta: float) -> void:
	var dir := Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		dir.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		dir.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		dir.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		dir.y += 1.0

	# Screen-edge pan.
	var vp := get_viewport().get_visible_rect().size
	var m := get_viewport().get_mouse_position()
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

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_apply_zoom(1.0 / ZOOM_STEP)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_apply_zoom(ZOOM_STEP)

func _apply_zoom(factor: float) -> void:
	var z := clampf(zoom.x * factor, ZOOM_MIN, ZOOM_MAX)
	zoom = Vector2(z, z)
