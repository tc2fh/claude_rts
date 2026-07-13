extends SceneTree
## Headless CI smoke test - validates the GDScript/scene side the way CI already
## validates C++: the gdext must load (SimBridge registers), the mock sim must
## drive a few ticks through the bridge, and the game scripts + main scene must
## parse/load. Exits 0 on pass, non-zero on failure.
##
## Run: godot --headless --path game -s res://tests/smoke_test.gd

func _initialize() -> void:
	var fail := 0

	# 1. GDExtension registered (the C++ <-> Godot bridge actually loaded).
	if not ClassDB.class_exists("SimBridge"):
		push_error("[smoke] SimBridge GDExtension not loaded")
		quit(1)
		return

	# 2. Drive the sim through the bridge (the mock implementation for now).
	var sim = ClassDB.instantiate("SimBridge")  # untyped: methods are GDExtension-provided
	sim.create(1234, 0)
	if not sim.is_ready():
		push_error("[smoke] sim.is_ready() == false"); fail += 1
	for _i in 50:
		sim.advance(1)
	if int(sim.tick()) != 50:
		push_error("[smoke] tick != 50 (got %d)" % int(sim.tick())); fail += 1
	var ids: PackedInt32Array = sim.entity_ids()
	if ids.is_empty():
		push_error("[smoke] snapshot has no entities"); fail += 1
	var rs: PackedFloat32Array = sim.render_state(0.5)
	if rs.size() != ids.size() * 3:
		push_error("[smoke] render_state size %d, expected %d" % [rs.size(), ids.size() * 3]); fail += 1
	var meta: PackedInt32Array = sim.entity_meta()
	if meta.size() != ids.size() * 5:
		push_error("[smoke] entity_meta size %d, expected %d" % [meta.size(), ids.size() * 5]); fail += 1
	if not ids.is_empty():
		sim.command_move(int(ids[0]), 5.0, 5.0)  # must not crash
		sim.command_train(int(ids[0]), 4)        # CMD_TRAIN soldier (no-op if not an HQ)
		sim.command_harvest(int(ids[0]), int(ids[0]))
		sim.command_attack(int(ids[0]), int(ids[0]))
		sim.command_attack_move(int(ids[0]), 6.0, 6.0)
		sim.command_hold(int(ids[0]))
		sim.command_patrol(int(ids[0]), 3.0, 3.0)
		sim.advance(1)
	var drained = sim.drain_events()
	if not (drained is Array):
		push_error("[smoke] drain_events() did not return an Array"); fail += 1
	# Event channel (real-sim only; CI smoke builds the real sim by default).
	# Use a fresh world: the enemy scout spawns inside the idle soldier's weapon
	# range, so ATTACK fires from the first ticks and the scout dies by ~tick 40.
	var evsim = ClassDB.instantiate("SimBridge")
	evsim.create(1234, 0)
	evsim.advance(120)
	var events = evsim.drain_events()
	if not (events is Array) or events.is_empty():
		push_error("[smoke] expected sim events within 120 ticks of a fresh world"); fail += 1
	else:
		var e0: Dictionary = events[0]
		for k in ["type", "a", "b", "tick"]:
			if not e0.has(k):
				push_error("[smoke] event missing key '%s': %s" % [k, str(e0)]); fail += 1
		var saw_attack := false
		for e in events:
			if int(e["type"]) == 1:   # ATTACK
				saw_attack = true
				break
		if not saw_attack:
			push_error("[smoke] no ATTACK (type 1) event among %d event(s)" % events.size()); fail += 1
		else:
			print("[smoke] events OK - %d event(s), first=%s" % [events.size(), str(e0)])
	var win := int(sim.winner())
	if win < 0 or win > 2:
		push_error("[smoke] winner() out of range: %d" % win); fail += 1
	print("[smoke] sim OK - tick=%d entities=%d winner=%d" % [int(sim.tick()), ids.size(), win])

	# 3. Map query (B's 2b) - verify the grid flows sim -> gdext -> GDScript.
	var msz: Vector2i = sim.map_size()
	var passable: PackedByteArray = sim.map_passable()
	var blocked := 0
	for b in passable:
		if b == 0:
			blocked += 1
	if msz.x <= 0 or msz.y <= 0 or passable.size() != msz.x * msz.y:
		push_error("[smoke] map invalid: size %s, passable %d" % [str(msz), passable.size()]); fail += 1
	elif blocked == 0:
		push_error("[smoke] expected the M0 map to have a wall (some blocked cells)"); fail += 1
	else:
		print("[smoke] map OK - %dx%d, %d blocked cells" % [msz.x, msz.y, blocked])

	# 4. Game scripts compile + the main scene loads (parse / structure validation).
	for path in ["res://scripts/main.gd", "res://scripts/camera_rig.gd", "res://scripts/hud.gd", "res://scripts/minimap.gd", "res://main.tscn"]:
		if ResourceLoader.load(path) == null:
			push_error("[smoke] failed to load %s" % path); fail += 1
	var packed := ResourceLoader.load("res://main.tscn") as PackedScene
	if packed != null:
		var scene := packed.instantiate()
		for node_path in [
			"Camera",
			"HUD/TopBar",
			"HUD/TopBar/Margin/Rows/Header/Minerals",
			"HUD/MinimapFrame/Margin/Rows/Minimap",
		]:
			if scene.get_node_or_null(node_path) == null:
				push_error("[smoke] main scene missing %s" % node_path); fail += 1
		scene.free()

	if fail == 0:
		print("[smoke] PASS")
	else:
		push_error("[smoke] FAIL - %d check(s)" % fail)
	quit(0 if fail == 0 else 1)
