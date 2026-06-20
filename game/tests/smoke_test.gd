extends SceneTree
## Headless CI smoke test — validates the GDScript/scene side the way CI already
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
		sim.advance(1)
	print("[smoke] sim OK — tick=%d entities=%d" % [int(sim.tick()), ids.size()])

	# 3. Game scripts compile + the main scene loads (parse / structure validation).
	for path in ["res://scripts/main.gd", "res://scripts/camera_rig.gd", "res://scripts/hud.gd", "res://main.tscn"]:
		if ResourceLoader.load(path) == null:
			push_error("[smoke] failed to load %s" % path); fail += 1

	if fail == 0:
		print("[smoke] PASS")
	else:
		push_error("[smoke] FAIL — %d check(s)" % fail)
	quit(0 if fail == 0 else 1)
