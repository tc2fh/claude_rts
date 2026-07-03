extends Node2D
## claude_rts — M0 view harness.
##
## Drives B's deterministic sim through the SimBridge GDExtension at a fixed
## 24 Hz, renders the static map terrain + interpolated snapshot entities, and
## turns mouse input into box-selection + SC2-style commands (smart RMB, verb
## hotkeys, control groups). All sim access goes through SimBridge — the view
## never touches sim internals or fixed-point math.

const TICK_HZ := 24.0
const TICK_DT := 1.0 / TICK_HZ
const PX_PER_UNIT := 32.0   ## world units -> pixels
const UNIT_RADIUS := 10.0   ## draw radius (px)
const SEED := 1234
const MAP_ID := 0

# Entity type ids (from B's sim — mailbox B-16). TYPE_SOLDIER arrives in 2c.
const TYPE_WORKER := 1
const TYPE_HQ := 2
const TYPE_RESOURCE := 3
const TYPE_SOLDIER := 4
const MY_PLAYER := 1   # player owns owner==1; enemy is owner==2

# Sim event types (drain_events()).
const EV_ATTACK := 1
const EV_TRAINED := 2
const EV_DIED := 3

const SPRITE_DIR := "res://assets/sprites/"
const SFX_DIR := "res://assets/sfx/"
const SFX_POOL_SIZE := 8
const SFX_MIN_INTERVAL := 0.06   ## per-sound throttle (s)
const HIT_FLASH_TIME := 0.15
const TARGET_FLASH_TIME := 0.3
const MARKER_TTL := 0.6
const DEATH_PUFF_TTL := 0.5
const GROUP_DOUBLE_TAP := 0.4    ## recall twice within this (s) to center camera
const MARKER_COLORS := {"move": Color(0.3, 0.9, 0.4), "attack": Color(0.95, 0.3, 0.25), "patrol": Color(0.3, 0.9, 0.95)}

var _sim = null             ## untyped: methods are GDExtension-provided (dynamic dispatch)
var _accum := 0.0
var _alpha := 0.0

# Render caches, refreshed each frame from the bridge.
var _ids: PackedInt32Array = PackedInt32Array()
var _render: PackedFloat32Array = PackedFloat32Array()  # [x, y, facing_rad] * n (world units)
var _meta: PackedInt32Array = PackedInt32Array()        # [type, owner, state, hp, hp_max] * n

# Static map geometry (queried once from the sim after create()).
var _map_w := 0
var _map_h := 0
var _passable: PackedByteArray = PackedByteArray()

var _selected := {}                 # id -> true
var _dragging := false
var _drag_start := Vector2.ZERO     # world-space (camera-correct)
var _drag_now := Vector2.ZERO

var _pending_verb := ""             # "" | "attack" | "move" | "patrol"
var _groups := {}                   # digit -> Array of ids
var _last_group_digit := 0
var _last_group_time := 0.0

var _textures := {}                 # Vector2i(type, owner) -> Texture2D (null if unimported)
var _sfx := {}                      # name -> AudioStream (null if unimported)
var _sfx_pool: Array[AudioStreamPlayer] = []
var _sfx_next := 0
var _sfx_last := {}                 # name -> last play time (s)

var _last_pos := {}                 # id -> px Vector2 (kept so corpses have a position)
var _hit_flash := {}                # victim id -> seconds left
var _target_flash := {}             # target id -> seconds left
var _markers: Array = []            # {pos, ttl, kind: "move"|"attack"|"patrol"}
var _puffs: Array = []              # {pos, ttl}

@onready var _hud: HudPanel = $HUD/Panel
@onready var _camera = $Camera     ## untyped: center_on() is script-provided

func _ready() -> void:
	if not ClassDB.class_exists("SimBridge"):
		push_error("SimBridge GDExtension not loaded — build gdext (see docs/BUILD.md).")
		set_process(false)
		set_process_input(false)
		_hud.set_stats(-1, -1)
		return
	_sim = ClassDB.instantiate("SimBridge")
	_sim.create(SEED, MAP_ID)
	var msz: Vector2i = _sim.map_size()
	_map_w = msz.x
	_map_h = msz.y
	_passable = _sim.map_passable()
	_load_textures()
	_setup_sfx()

func _load_textures() -> void:
	_textures[Vector2i(TYPE_HQ, 1)] = _load_asset(SPRITE_DIR + "hq_blue.png")
	_textures[Vector2i(TYPE_HQ, 2)] = _load_asset(SPRITE_DIR + "hq_red.png")
	_textures[Vector2i(TYPE_WORKER, 1)] = _load_asset(SPRITE_DIR + "worker_blue.png")
	_textures[Vector2i(TYPE_WORKER, 2)] = _load_asset(SPRITE_DIR + "worker_red.png")
	_textures[Vector2i(TYPE_SOLDIER, 1)] = _load_asset(SPRITE_DIR + "soldier_blue.png")
	_textures[Vector2i(TYPE_SOLDIER, 2)] = _load_asset(SPRITE_DIR + "soldier_red.png")
	_textures[Vector2i(TYPE_RESOURCE, 0)] = _load_asset(SPRITE_DIR + "node.png")

func _setup_sfx() -> void:
	for snd in ["cmd_move", "cmd_attack", "cmd_build", "hit", "train_done", "death"]:
		_sfx[snd] = _load_asset(SFX_DIR + snd + ".wav")
	for _i in SFX_POOL_SIZE:
		var player := AudioStreamPlayer.new()
		add_child(player)
		_sfx_pool.append(player)

func _load_asset(path: String) -> Resource:
	return load(path) if ResourceLoader.exists(path) else null

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
	for i in _ids.size():
		_last_pos[_ids[i]] = _world_px(i)
	_tick_fx(delta)
	_drain_sim_events()
	_hud.set_stats(int(_sim.tick()), int(_sim.get_resource(1)), _selected.size(), int(_sim.winner()), MY_PLAYER, _pending_verb)
	queue_redraw()

func _drain_sim_events() -> void:
	for ev in _sim.drain_events():
		match int(ev["type"]):
			EV_ATTACK:
				_hit_flash[int(ev["b"])] = HIT_FLASH_TIME
				_play_sfx("hit")
			EV_TRAINED:
				_play_sfx("train_done")
			EV_DIED:
				var id := int(ev["a"])
				if _last_pos.has(id):
					_puffs.append({"pos": _last_pos[id], "ttl": DEATH_PUFF_TTL})
					_last_pos.erase(id)
				_selected.erase(id)
				_play_sfx("death")

func _tick_fx(delta: float) -> void:
	for id in _hit_flash.keys():
		_hit_flash[id] -= delta
		if _hit_flash[id] <= 0.0:
			_hit_flash.erase(id)
	for id in _target_flash.keys():
		_target_flash[id] -= delta
		if _target_flash[id] <= 0.0:
			_target_flash.erase(id)
	for m in _markers:
		m["ttl"] -= delta
	_markers = _markers.filter(func(m): return m["ttl"] > 0.0)
	for p in _puffs:
		p["ttl"] -= delta
	_puffs = _puffs.filter(func(p): return p["ttl"] > 0.0)

func _world_px(i: int) -> Vector2:
	return Vector2(_render[i * 3 + 0], _render[i * 3 + 1]) * PX_PER_UNIT

func _draw() -> void:
	_draw_terrain()

	# Selection marquee (world space; get_global_mouse_position is camera-correct).
	if _dragging:
		var r := _rect_from(_drag_start, _drag_now)
		draw_rect(r, Color(0.3, 0.8, 1.0, 0.15), true)
		draw_rect(r, Color(0.4, 0.9, 1.0, 0.9), false, 1.5)

	var n := _ids.size()
	for i in n:
		var p := _world_px(i)
		var type_id := _meta[i * 5 + 0]
		var owner_id := _meta[i * 5 + 1]
		_draw_entity(p, type_id, owner_id, _render[i * 3 + 2])
		if _selected.has(_ids[i]):
			draw_arc(p, UNIT_RADIUS + 4.0, 0.0, TAU, 24, Color(1.0, 1.0, 0.25, 0.9), 2.0)
		if _target_flash.has(_ids[i]):
			var tf: float = _target_flash[_ids[i]] / TARGET_FLASH_TIME
			draw_arc(p, UNIT_RADIUS + 6.0, 0.0, TAU, 24, Color(1.0, 1.0, 1.0, 0.8 * tf), 2.0)
		if _hit_flash.has(_ids[i]):
			var hf: float = _hit_flash[_ids[i]] / HIT_FLASH_TIME
			draw_circle(p, UNIT_RADIUS + 2.0, Color(1.0, 1.0, 1.0, 0.45 * hf))
		if owner_id != 0:   # health bar for owned units (skip neutral resource nodes)
			_draw_health_bar(p, _meta[i * 5 + 3], _meta[i * 5 + 4])
	_draw_markers()
	_draw_puffs()

func _draw_terrain() -> void:
	if _map_w <= 0 or _passable.size() < _map_w * _map_h:
		return
	var cell := PX_PER_UNIT
	var bounds := Rect2(Vector2.ZERO, Vector2(_map_w, _map_h) * cell)
	draw_rect(bounds, Color(0.10, 0.12, 0.16), true)                                       # ground
	for y in _map_h:
		for x in _map_w:
			if _passable[y * _map_w + x] == 0:
				draw_rect(Rect2(Vector2(x, y) * cell, Vector2(cell, cell)), Color(0.30, 0.32, 0.40), true)  # wall
	var grid := Color(1, 1, 1, 0.05)
	for gx in _map_w + 1:
		draw_line(Vector2(gx * cell, 0.0), Vector2(gx * cell, _map_h * cell), grid, 1.0)
	for gy in _map_h + 1:
		draw_line(Vector2(0.0, gy * cell), Vector2(_map_w * cell, gy * cell), grid, 1.0)
	draw_rect(bounds, Color(1, 1, 1, 0.15), false, 1.5)                                     # border

func _draw_entity(p: Vector2, type_id: int, owner_id: int, facing: float) -> void:
	var tex := _texture_for(type_id, owner_id)
	if tex != null:
		draw_texture(tex, p - tex.get_size() * 0.5)
	else:
		_draw_entity_shape(p, type_id, _owner_color(owner_id))   # fallback: unimported textures
	if type_id == TYPE_WORKER or type_id == TYPE_SOLDIER:
		draw_line(p, p + Vector2(cos(facing), sin(facing)) * UNIT_RADIUS, Color.WHITE, 1.5)

func _draw_entity_shape(p: Vector2, type_id: int, col: Color) -> void:
	match type_id:
		TYPE_HQ:
			var hs := Vector2(UNIT_RADIUS, UNIT_RADIUS) * 1.5
			draw_rect(Rect2(p - hs, hs * 2.0), col, true)
			draw_rect(Rect2(p - hs, hs * 2.0), Color(0, 0, 0, 0.4), false, 2.0)
		TYPE_RESOURCE:
			draw_colored_polygon(PackedVector2Array([
				p + Vector2(0.0, -UNIT_RADIUS), p + Vector2(UNIT_RADIUS, 0.0),
				p + Vector2(0.0, UNIT_RADIUS), p + Vector2(-UNIT_RADIUS, 0.0)]),
				Color(0.95, 0.82, 0.25))
		_:
			draw_circle(p, UNIT_RADIUS, col)

func _texture_for(type_id: int, owner_id: int) -> Texture2D:
	if type_id == TYPE_RESOURCE:
		return _textures.get(Vector2i(TYPE_RESOURCE, 0)) as Texture2D
	return _textures.get(Vector2i(type_id, 1 if owner_id == 1 else 2)) as Texture2D

func _draw_health_bar(p: Vector2, hp: int, hp_max: int) -> void:
	if hp_max <= 0:
		return
	var frac := clampf(float(hp) / float(hp_max), 0.0, 1.0)
	var w := UNIT_RADIUS * 2.0
	var tl := p + Vector2(-UNIT_RADIUS, -UNIT_RADIUS - 7.0)
	draw_rect(Rect2(tl, Vector2(w, 3.0)), Color(0, 0, 0, 0.6), true)
	var bar := Color(0.3, 0.9, 0.35) if frac > 0.3 else Color(0.9, 0.3, 0.25)
	draw_rect(Rect2(tl, Vector2(w * frac, 3.0)), bar, true)

func _draw_markers() -> void:
	for m in _markers:
		var frac: float = m["ttl"] / MARKER_TTL
		var col: Color = MARKER_COLORS[m["kind"]]
		col.a = frac
		draw_arc(m["pos"], 4.0 + 14.0 * frac, 0.0, TAU, 20, col, 2.0)

func _draw_puffs() -> void:
	for p in _puffs:
		var frac: float = p["ttl"] / DEATH_PUFF_TTL
		draw_arc(p["pos"], UNIT_RADIUS * (1.0 + 1.5 * (1.0 - frac)), 0.0, TAU, 20, Color(1.0, 1.0, 1.0, 0.7 * frac), 2.0)

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
				if _pending_verb != "":
					_execute_verb(get_global_mouse_position())
				elif event.double_click:
					_select_same_type(get_global_mouse_position())
				else:
					_dragging = true
					_drag_start = get_global_mouse_position()
					_drag_now = _drag_start
			elif _dragging:
				_drag_now = get_global_mouse_position()
				_apply_selection(event.shift_pressed)
				_dragging = false
				queue_redraw()
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			if _pending_verb != "":
				_pending_verb = ""
			else:
				_issue_context_command(get_global_mouse_position())
	elif event is InputEventMouseMotion and _dragging:
		_drag_now = get_global_mouse_position()
		queue_redraw()
	elif event is InputEventKey and event.pressed and not event.echo:
		_handle_key(event)

func _handle_key(event: InputEventKey) -> void:
	match event.keycode:
		KEY_A: _pending_verb = "attack"
		KEY_M: _pending_verb = "move"
		KEY_P: _pending_verb = "patrol"
		KEY_S: _stop_or_hold(true)
		KEY_H: _stop_or_hold(false)
		KEY_T: _train_at_selected_hqs(TYPE_SOLDIER)
		KEY_E: _train_at_selected_hqs(TYPE_WORKER)
		KEY_ESCAPE: _pending_verb = ""
		_:
			if event.keycode >= KEY_1 and event.keycode <= KEY_9:
				_handle_group_key(event.keycode - KEY_0, event.ctrl_pressed)

func _handle_group_key(digit: int, assign: bool) -> void:
	if assign:
		_groups[digit] = _selected.keys()
		return
	if not _groups.has(digit):
		return
	var live := {}
	for id in _ids:
		live[id] = true
	var members: Array = _groups[digit].filter(func(id): return live.has(id))
	_groups[digit] = members
	_selected.clear()
	for id in members:
		_selected[id] = true
	var now := Time.get_ticks_msec() / 1000.0
	if digit == _last_group_digit and now - _last_group_time <= GROUP_DOUBLE_TAP and not members.is_empty():
		var sum := Vector2.ZERO
		for id in members:
			sum += _last_pos.get(id, Vector2.ZERO)
		_camera.center_on(sum / members.size())
	_last_group_digit = digit
	_last_group_time = now

func _apply_selection(additive: bool) -> void:
	if not additive:
		_selected.clear()
	if _drag_start.distance_to(_drag_now) < 6.0:
		# Treat as a click: pick the nearest entity under the cursor.
		var best := _entity_index_at(_drag_now)
		if best >= 0:
			_selected[_ids[best]] = true
	else:
		var r := _rect_from(_drag_start, _drag_now)
		var hits := []
		var mine := false
		for i in _ids.size():
			if r.has_point(_world_px(i)):
				hits.append(i)
				if _meta[i * 5 + 1] == MY_PLAYER:
					mine = true
		for i in hits:
			# Prefer my units; keep any-owner boxes for inspection clicks.
			if not mine or _meta[i * 5 + 1] == MY_PLAYER:
				_selected[_ids[i]] = true

func _select_same_type(click_px: Vector2) -> void:
	var ci := _entity_index_at(click_px)
	if ci < 0 or _meta[ci * 5 + 1] != MY_PLAYER:
		return
	var want_type := _meta[ci * 5 + 0]
	var view := get_viewport().get_canvas_transform().affine_inverse() * get_viewport().get_visible_rect()
	_selected.clear()
	for i in _ids.size():
		if _meta[i * 5 + 0] == want_type and _meta[i * 5 + 1] == MY_PLAYER and view.has_point(_world_px(i)):
			_selected[_ids[i]] = true

func _entity_index_at(px: Vector2) -> int:
	var best := -1
	var best_d := UNIT_RADIUS * 1.6
	for i in _ids.size():
		var d := _world_px(i).distance_to(px)
		if d < best_d:
			best_d = d
			best = i
	return best

func _execute_verb(target_px: Vector2) -> void:
	var verb := _pending_verb
	_pending_verb = ""
	var units := _selected_my_units()
	if units.is_empty():
		return
	var wx := target_px.x / PX_PER_UNIT
	var wy := target_px.y / PX_PER_UNIT
	match verb:
		"attack":
			var ti := _entity_index_at(target_px)
			if ti >= 0 and _meta[ti * 5 + 1] != 0 and _meta[ti * 5 + 1] != MY_PLAYER:
				for id in units:
					_sim.command_attack(id, int(_ids[ti]))
				_flash_target(int(_ids[ti]))
			else:
				for id in units:
					_sim.command_attack_move(id, wx, wy)
				_add_marker(target_px, "attack")
			_play_sfx("cmd_attack")
		"move":
			for id in units:
				_sim.command_move(id, wx, wy)
			_add_marker(target_px, "move")
			_play_sfx("cmd_move")
		"patrol":
			for id in units:
				_sim.command_patrol(id, wx, wy)
			_add_marker(target_px, "patrol")
			_play_sfx("cmd_move")

func _issue_context_command(target_px: Vector2) -> void:
	var units := _selected_my_units()
	if units.is_empty():
		return
	var ti := _entity_index_at(target_px)
	if ti >= 0:
		var t_id := int(_ids[ti])
		var t_owner := _meta[ti * 5 + 1]
		var t_type := _meta[ti * 5 + 0]
		if t_owner != 0 and t_owner != MY_PLAYER:   # enemy -> attack
			for id in units:
				_sim.command_attack(id, t_id)
			_flash_target(t_id)
			_play_sfx("cmd_attack")
			return
		if t_type == TYPE_RESOURCE:                 # resource node -> harvest
			for id in units:
				_sim.command_harvest(id, t_id)
			_flash_target(t_id)
			_play_sfx("cmd_move")
			return
	# ground -> passive move
	for id in units:
		_sim.command_move(id, target_px.x / PX_PER_UNIT, target_px.y / PX_PER_UNIT)
	_add_marker(target_px, "move")
	_play_sfx("cmd_move")

func _stop_or_hold(stop: bool) -> void:
	var units := _selected_my_units()
	if units.is_empty():
		return
	for id in units:
		if stop:
			_sim.command_stop(id)
		else:
			_sim.command_hold(id)
	_play_sfx("cmd_move")

func _train_at_selected_hqs(unit_type: int) -> void:
	var trained := false
	for i in _ids.size():
		if _selected.has(_ids[i]) and _meta[i * 5 + 0] == TYPE_HQ and _meta[i * 5 + 1] == MY_PLAYER:
			_sim.command_train(int(_ids[i]), unit_type)
			trained = true
	if trained:
		_play_sfx("cmd_build")

func _selected_my_units() -> Array:
	var out := []
	for i in _ids.size():
		if _selected.has(_ids[i]) and _meta[i * 5 + 1] == MY_PLAYER:
			out.append(int(_ids[i]))
	return out

func _add_marker(pos: Vector2, kind: String) -> void:
	_markers.append({"pos": pos, "ttl": MARKER_TTL, "kind": kind})

func _flash_target(id: int) -> void:
	_target_flash[id] = TARGET_FLASH_TIME

func _play_sfx(snd: String) -> void:
	var stream = _sfx.get(snd)
	if stream == null:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - float(_sfx_last.get(snd, -1.0)) < SFX_MIN_INTERVAL:
		return
	_sfx_last[snd] = now
	var player: AudioStreamPlayer = _sfx_pool[_sfx_next]
	_sfx_next = (_sfx_next + 1) % _sfx_pool.size()
	player.stream = stream
	player.play()
