extends SceneTree

# Headless test runner. Loads every res://tests/test_*.gd, runs each test_*
# method and quits with exit code 1 on any failure. A test script exposes a
# `failures: Array` member that its assert helpers append to.


func _init() -> void:
	var passed: int = 0
	var failed: int = 0
	var files: PackedStringArray = []
	var dir: DirAccess = DirAccess.open("res://tests")
	if dir != null:
		for file_name: String in dir.get_files():
			if file_name.begins_with("test_") and file_name.ends_with(".gd"):
				files.append(file_name)
	files.sort()
	for file_name: String in files:
		var script: GDScript = load("res://tests/" + file_name) as GDScript
		if script == null:
			print("FAIL  %s: could not load" % file_name)
			failed += 1
			continue
		var names: Array[String] = []
		for info: Dictionary in script.get_script_method_list():
			var method_name: String = str(info["name"])
			if method_name.begins_with("test_"):
				names.append(method_name)
		names.sort()
		for method_name: String in names:
			var instance: Object = script.new()
			instance.call(method_name)
			var failures: Array = instance.get("failures")
			var label: String = "%s::%s" % [file_name, method_name]
			if failures.is_empty():
				print("PASS  %s" % label)
				passed += 1
			else:
				print("FAIL  %s" % label)
				for message: Variant in failures:
					print("      " + str(message))
				failed += 1
	print("%d passed, %d failed" % [passed, failed])
	quit(1 if failed > 0 else 0)
