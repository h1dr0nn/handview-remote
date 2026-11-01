@tool
extends EditorPlugin
## Spike plugin: runs the editor control experiments unattended and quits the editor.
##
## Only active when HANDVIEW_SPIKE_RUN is set to a list of experiments, for example "1,2" or "all".
## Results are appended to res://results.txt, the game writes its state to res://logs/*.txt.

const GAME_LOG_DIR := "res://logs/"
const MAX_RUNTIME_S := 56.0

var run_list: Array = []
var results_file: FileAccess
var t_start := 0.0
var dbg: SpikeDebugger
var finished := false

# Event timeline seen by the editor: [unix_time, name]
var events: Array = []
var last_playing := false
var frame_dts: Array = []
var last_frame_t := 0.0
var orig_embed_mode: Variant = null
var orig_embed_meta: Variant = null
var restore_items: Array = []


class SpikeDebugger extends EditorDebuggerPlugin:
	var host: Node

	func _has_capture(capture: String) -> bool:
		return capture == "handview_spike"

	func _capture(message: String, data: Array, session_id: int) -> bool:
		host.on_dbg("cap_" + message.get_slice(":", message.get_slice_count(":") - 1), session_id)
		return true

	func _setup_session(session_id: int) -> void:
		var s := get_session(session_id)
		s.started.connect(func(): host.on_dbg("session_started", session_id))
		s.stopped.connect(func(): host.on_dbg("session_stopped", session_id))
		s.breaked.connect(func(can_debug: bool): host.on_dbg("session_breaked", session_id))
		s.continued.connect(func(): host.on_dbg("session_continued", session_id))


func now() -> float:
	return Time.get_unix_time_from_system()


func R(line: String) -> void:
	print("[spike] ", line)
	if results_file != null:
		results_file.store_line(line)
		results_file.flush()


func on_dbg(name: String, session_id: int) -> void:
	events.append([now(), name])


func _enter_tree() -> void:
	var run := OS.get_environment("HANDVIEW_SPIKE_RUN")
	if run.is_empty():
		return
	run_list = [1, 2, 3, 4] if run == "all" else Array(run.split(",")).map(func(s): return int(s))
	dbg = SpikeDebugger.new()
	dbg.host = self
	add_debugger_plugin(dbg)
	set_process(true)
	t_start = now()
	var path := ProjectSettings.globalize_path("res://results.txt")
	if FileAccess.file_exists(path):
		results_file = FileAccess.open(path, FileAccess.READ_WRITE)
		results_file.seek_end()
	else:
		results_file = FileAccess.open(path, FileAccess.WRITE)
	_start.call_deferred()


func _exit_tree() -> void:
	if dbg != null:
		remove_debugger_plugin(dbg)


func _process(_delta: float) -> void:
	if run_list.is_empty():
		return
	var t := now()
	if last_frame_t > 0.0:
		frame_dts.append(t - last_frame_t)
	last_frame_t = t
	var playing := EditorInterface.is_playing_scene()
	if playing != last_playing:
		last_playing = playing
		events.append([t, "playing_true" if playing else "playing_false"])
	if not finished and t - t_start > MAX_RUNTIME_S:
		R("WATCHDOG: runtime limit reached, aborting")
		_finish()


# ---------------------------------------------------------------- helpers

func wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func wait_until(cond: Callable, timeout: float) -> float:
	var t := now()
	while not cond.call():
		if now() - t > timeout:
			return -1.0
		await get_tree().process_frame
	return now() - t


func ev_time(name: String, after: float) -> float:
	for e in events:
		if e[1] == name and e[0] >= after:
			return e[0]
	return -1.0


func fmt_ms(t_event: float, t_ref: float) -> String:
	if t_event < 0.0:
		return "never"
	return "%.0f ms" % ((t_event - t_ref) * 1000.0)


func log_path(tag: String) -> String:
	return ProjectSettings.globalize_path(GAME_LOG_DIR + "game_" + tag + ".txt")


func parse_log(path: String) -> Dictionary:
	var out := {"E": [], "P": [], "M": []}
	if not FileAccess.file_exists(path):
		return out
	var f: FileAccess = null
	for attempt in 100:
		f = FileAccess.open(path, FileAccess.READ)
		if f != null:
			break
		OS.delay_msec(3)
	if f == null:
		return out
	var text := f.get_as_text()
	for line in text.split("\n"):
		var p := line.split("\t")
		if p.size() < 3:
			continue
		match p[0]:
			"E":
				out["E"].append({"t": float(p[1]), "name": p[2], "detail": p[3] if p.size() > 3 else ""})
			"P", "M":
				if p.size() >= 6:
					out[p[0]].append([float(p[1]), int(p[2]), int(p[3]), int(p[4]), p[5]])
	return out


func game_event(log: Dictionary, name: String) -> Dictionary:
	for e in log["E"]:
		if e["name"] == name:
			return e
	return {}


func last_state(path: String) -> Array:
	for attempt in 10:
		var log := parse_log(path)
		var m: Array = log["M"]
		if m.size() > 0:
			return m[m.size() - 1]
		OS.delay_msec(5)
	R("WARNING: could not read game log %s" % path)
	return [0.0, 0, 0, 0, "?"]


func set_env(key: String, value: String) -> void:
	if value.is_empty():
		OS.unset_environment(key)
	else:
		OS.set_environment(key, value)


func play(tag: String, action: String = "") -> float:
	## Sets the per-play spike variables and starts the main scene. Returns the call time.
	set_env("HANDVIEW_SPIKE", "1")
	set_env("HANDVIEW_SPIKE_LOG", log_path(tag))
	set_env("HANDVIEW_SPIKE_ACTION", action)
	var path := log_path(tag)
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	var t := now()
	EditorInterface.play_main_scene()
	return t


func stop_and_wait() -> void:
	if EditorInterface.is_playing_scene():
		EditorInterface.stop_playing_scene()
		await wait_until(func(): return not EditorInterface.is_playing_scene(), 5.0)
	await wait(0.6)


func wait_game_ready(tag: String, timeout: float = 10.0) -> Dictionary:
	var t := now()
	while now() - t < timeout:
		var g := game_event(parse_log(log_path(tag)), "ready")
		if not g.is_empty():
			return g
		await get_tree().process_frame
	return {}


func active_session() -> EditorDebuggerSession:
	for s in dbg.get_sessions():
		if s.is_active():
			return s
	return null


func frame_stats(from_idx: int) -> String:
	var a := frame_dts.slice(from_idx)
	if a.is_empty():
		return "n/a"
	var mx := 0.0
	var sum := 0.0
	for d in a:
		mx = maxf(mx, d)
		sum += d
	return "editor _process interval mean %.1f ms, max %.1f ms over %d frames" % [sum / a.size() * 1000.0, mx * 1000.0, a.size()]


func win_list(game_pid: int) -> PackedStringArray:
	var script := ProjectSettings.globalize_path("res://winlist.ps1")
	var out: Array = []
	var code := OS.execute("powershell", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-ProcId", str(game_pid)], out, true)
	var lines := PackedStringArray()
	lines.append("(powershell exit %d)" % code)
	for chunk in out:
		for l in String(chunk).split("\n"):
			if l.contains("class=Engine"):
				lines.append(l.strip_edges())
	return lines


# ---------------------------------------------------------------- driver

func _start() -> void:
	# Wait for the filesystem scan, then a little more so the editor settles.
	await wait(1.5)
	var fs := EditorInterface.get_resource_filesystem()
	await wait_until(func(): return not fs.is_scanning(), 20.0)
	await wait(1.0)
	R("=== run %s | experiments %s | %s | editor feature enabled ===" % [
		Time.get_datetime_string_from_system(), str(run_list), Engine.get_version_info()["string"]])
	R("clock: unix time resolution probe: %s" % clock_probe())
	var es := EditorInterface.get_editor_settings()
	orig_embed_mode = es.get_setting("run/window_placement/game_embed_mode")
	orig_embed_meta = es.get_project_metadata("game_view", "embed_on_play", "__absent__")
	for n in run_list:
		match n:
			1: await exp1()
			2: await exp2()
			3: await exp3()
			4: await exp4()
	await _finish()


func clock_probe() -> String:
	var steps: Array = []
	var last := Time.get_unix_time_from_system()
	var deadline := Time.get_ticks_msec() + 300
	while Time.get_ticks_msec() < deadline and steps.size() < 6:
		var t := Time.get_unix_time_from_system()
		if t != last:
			steps.append(snappedf((t - last) * 1000.0, 0.001))
			last = t
	return "smallest observed step(s) in ms %s" % str(steps)


func _finish() -> void:
	if finished:
		return
	finished = true
	restore_settings()
	if EditorInterface.is_playing_scene():
		EditorInterface.stop_playing_scene()
	R("total runtime %.1f s" % (now() - t_start))
	R("")
	if results_file != null:
		results_file.flush()
		results_file = null
	await get_tree().create_timer(0.5).timeout
	get_tree().quit()


func restore_settings() -> void:
	var es := EditorInterface.get_editor_settings()
	if orig_embed_mode != null and es.get_setting("run/window_placement/game_embed_mode") != orig_embed_mode:
		es.set_setting("run/window_placement/game_embed_mode", orig_embed_mode)
		R("restored game_embed_mode to %s" % str(orig_embed_mode))
	for k in ["HANDVIEW_LINK_PORT", "HANDVIEW_LINK_TOKEN", "HANDVIEW_SPIKE_ACTION"]:
		OS.unset_environment(k)
	for item in restore_items:
		item.call()
	restore_items.clear()


func report_game_env(tag: String) -> void:
	await wait(0.3)
	var log := parse_log(log_path(tag))
	for attempt in 15:
		if not game_event(log, "info").is_empty():
			break
		await wait(0.2)
		log = parse_log(log_path(tag))
	for e in log["E"]:
		if e["name"] in ["env", "cmdline", "info"]:
			R("    game %s: %s" % [e["name"], e["detail"]])


# ---------------------------------------------------------------- experiment 1: env inheritance

func exp1() -> void:
	R("--- Experiment 1: environment inheritance ---")
	# 1a: set, play, unset in the same frame as play_main_scene returns.
	OS.set_environment("HANDVIEW_LINK_PORT", "12345")
	OS.set_environment("HANDVIEW_LINK_TOKEN", "tok_" + "ab12cd34")
	var t_call := play("1a")
	OS.unset_environment("HANDVIEW_LINK_PORT")
	OS.unset_environment("HANDVIEW_LINK_TOKEN")
	R("1a: set PORT+TOKEN, play_main_scene(), unset in the same frame. editor now sees PORT='%s' has=%s" % [
		OS.get_environment("HANDVIEW_LINK_PORT"), str(OS.has_environment("HANDVIEW_LINK_PORT"))])
	var ready := await wait_game_ready("1a")
	R("    game ready %s after play call" % (("%.0f ms" % ((ready["t"] - t_call) * 1000.0)) if not ready.is_empty() else "never"))
	await report_game_env("1a")
	await wait(0.5)
	await stop_and_wait()

	# 1b: second play without setting anything: stale values?
	t_call = play("1b")
	var r2 := await wait_game_ready("1b")
	R("1b: second play, nothing set, editor unset both (no stale value expected)")
	R("    game ready %s after play call" % (("%.0f ms" % ((r2["t"] - t_call) * 1000.0)) if not r2.is_empty() else "never"))
	await report_game_env("1b")
	await stop_and_wait()

	# 1c: set, play, unset one editor frame later (checks that spawn is synchronous or not).
	OS.set_environment("HANDVIEW_LINK_PORT", "54321")
	OS.set_environment("HANDVIEW_LINK_TOKEN", "tok_late")
	t_call = play("1c")
	await get_tree().process_frame
	OS.unset_environment("HANDVIEW_LINK_PORT")
	OS.unset_environment("HANDVIEW_LINK_TOKEN")
	var r3 := await wait_game_ready("1c")
	R("1c: set, play, unset one editor frame later")
	R("    game ready %s after play call" % (("%.0f ms" % ((r3["t"] - t_call) * 1000.0)) if not r3.is_empty() else "never"))
	await report_game_env("1c")
	await stop_and_wait()

	# 1d: control: leave the values set (stale) and play again.
	OS.set_environment("HANDVIEW_LINK_PORT", "11111")
	t_call = play("1d")
	var r4 := await wait_game_ready("1d")
	R("1d: control, value left set in the editor during the second play")
	await report_game_env("1d")
	OS.unset_environment("HANDVIEW_LINK_PORT")
	await stop_and_wait()


# ---------------------------------------------------------------- experiment 2: lifecycle

func lifecycle(tag: String, action: String, editor_stop: bool) -> void:
	var idx := frame_dts.size()
	var ev0 := events.size()
	var t_call := play(tag, action)
	var t_playing := await wait_until(func(): return EditorInterface.is_playing_scene(), 5.0)
	var t_sess := await wait_until(func(): return ev_time("session_started", t_call) > 0.0, 12.0)
	var ready := await wait_game_ready(tag, 12.0)
	var t_started_dbg := ev_time("session_started", t_call)
	var t_playing_ev := ev_time("playing_true", t_call)
	await wait(0.3)
	var t_hello := ev_time("cap_hello", t_call)
	R("%s (%s): is_playing_scene true %s after play call (first seen by polling _process: %s), debugger session started %s, game _ready %s" % [
		tag, "editor stop" if editor_stop else "game action=" + action,
		"immediately" if t_playing >= 0.0 and t_playing < 0.002 else ("%.0f ms" % (t_playing * 1000.0)),
		fmt_ms(t_playing_ev, t_call), fmt_ms(t_started_dbg, t_call),
		("%.0f ms" % ((ready["t"] - t_call) * 1000.0)) if not ready.is_empty() else "never"])
	R("    custom message game->editor 'handview_spike:hello' received %s after play call (EngineDebugger.send_message + EditorDebuggerPlugin._capture)" % fmt_ms(t_hello, t_call))
	var t_event := 0.0
	if editor_stop:
		await wait(2.0)
		t_event = now()
		EditorInterface.stop_playing_scene()
		R("    stop_playing_scene() called; is_playing_scene right after: %s" % str(EditorInterface.is_playing_scene()))
	else:
		# Wait for the game to act and the editor to notice.
		await wait_until(func(): return ev_time("playing_false", t_started_dbg) > 0.0, 12.0)
		var log := parse_log(log_path(tag))
		var a := game_event(log, "action")
		t_event = a["t"] if not a.is_empty() else now()
	await wait_until(func(): return ev_time("playing_false", t_event) > 0.0 and ev_time("session_stopped", t_event) > 0.0, 6.0)
	var t_pf := ev_time("playing_false", t_event)
	var t_ss := ev_time("session_stopped", t_event)
	R("    event time = %s; is_playing_scene false seen after %s; session_stopped signal after %s" % [
		"stop_playing_scene() call" if editor_stop else "game logged action", fmt_ms(t_pf, t_event), fmt_ms(t_ss, t_event)])
	var log2 := parse_log(log_path(tag))
	R("    game log: exit_tree %s, close_request %s, last P frame t = event %s ms" % [
		"present" if not game_event(log2, "exit_tree").is_empty() else "absent",
		"present" if not game_event(log2, "close_request").is_empty() else "absent",
		"%.0f" % ((((log2["P"][log2["P"].size() - 1][0]) if log2["P"].size() > 0 else 0.0) - t_event) * 1000.0)])
	R("    %s" % frame_stats(idx))
	R("    editor events: %s" % str(events.slice(ev0).map(func(e): return "%s@+%.0fms" % [e[1], (e[0] - t_call) * 1000.0])))
	if EditorInterface.is_playing_scene():
		R("    still playing after wait, forcing stop")
	await stop_and_wait()


func exp2() -> void:
	R("--- Experiment 2: game lifecycle detection ---")
	await lifecycle("2a_editor_stop", "", true)
	await lifecycle("2b_game_quit", "quit", false)
	await lifecycle("2c_game_kill", "kill", false)
	await lifecycle("2d_game_crash", "crash", false)


# ---------------------------------------------------------------- experiment 3: embedding

func find_by_class(root: Node, cls: String) -> Node:
	if root.get_class() == cls:
		return root
	for c in root.get_children():
		var r := find_by_class(c, cls)
		if r != null:
			return r
	return null


func find_embed_popup(root: Node) -> Dictionary:
	if root is MenuButton:
		var pm: PopupMenu = root.get_popup()
		for i in pm.item_count:
			if pm.get_item_text(i).findn("Embed Game") >= 0:
				return {"popup": pm, "idx": i}
	for c in root.get_children():
		var r := find_embed_popup(c)
		if not r.is_empty():
			return r
	return {}


func play_check_embed(tag: String, label: String) -> Dictionary:
	var t_call := play(tag)
	var ready := await wait_game_ready(tag, 10.0)
	await wait(1.0)
	var log := parse_log(log_path(tag))
	var cmd := game_event(log, "cmdline")
	var info := game_event(log, "info")
	for attempt in 20:
		if not info.is_empty():
			break
		await wait(0.2)
		log = parse_log(log_path(tag))
		info = game_event(log, "info")
	var embedded_args: bool = String(info.get("detail", "")).contains("embedded_in_editor=true")
	R("%s: game started; game reports Engine.is_embedded_in_editor() = %s" % [label, str(embedded_args)])
	R("    info: %s" % info.get("detail", "(none)"))
	if not ready.is_empty():
		var pid := int(String(ready["detail"]).replace("pid=", ""))
		for l in win_list(pid):
			R("    window: %s" % l)
	await stop_and_wait()
	return {"embedded_args": embedded_args}


func exp3() -> void:
	R("--- Experiment 3: game embedding ---")
	var es := EditorInterface.get_editor_settings()
	R("editor setting run/window_placement/game_embed_mode: exists=%s value=%s (enum: -1 Disabled, 0 Use Per-Project Configuration, 1 Embed Game, 2 Make Game Workspace Floating)" % [
		str(es.has_setting("run/window_placement/game_embed_mode")), str(orig_embed_mode)])
	R("project metadata game_view/embed_on_play (default true when absent): %s" % str(orig_embed_meta))
	R("display driver: %s; Engine.is_embedded_in_editor in editor: %s" % [DisplayServer.get_name(), str(Engine.is_embedded_in_editor())])
	var gv := find_by_class(EditorInterface.get_editor_main_screen(), "GameView")
	R("editor main window HWND = 0x%X" % DisplayServer.window_get_native_handle(DisplayServer.WINDOW_HANDLE))
	R("GameView node found in editor main screen: %s" % str(gv != null))
	var menu: Dictionary = find_embed_popup(gv) if gv != null else {}
	if not menu.is_empty():
		var pm: PopupMenu = menu["popup"]
		R("Game workspace embed menu: item '%s' id=%d checked=%s" % [pm.get_item_text(menu["idx"]), pm.get_item_id(menu["idx"]), str(pm.is_item_checked(menu["idx"]))])

	# 3a: defaults
	await play_check_embed("3a", "3a: defaults")

	# 3b: editor setting changed at runtime before play
	es.set_setting("run/window_placement/game_embed_mode", -1)
	R("3b: set_setting(game_embed_mode, -1) before play (restored at the end)")
	await play_check_embed("3b", "3b: after editor setting = -1")
	es.set_setting("run/window_placement/game_embed_mode", orig_embed_mode)

	# 3c: project metadata
	es.set_project_metadata("game_view", "embed_on_play", false)
	R("3c: set_project_metadata(game_view, embed_on_play, false) before play")
	await play_check_embed("3c", "3c: after project metadata embed_on_play = false")
	es.set_project_metadata("game_view", "embed_on_play", true if (orig_embed_meta is String) else orig_embed_meta)

	# 3d: toggle the Game workspace menu item from the plugin
	if not menu.is_empty():
		var pm: PopupMenu = menu["popup"]
		var id: int = pm.get_item_id(menu["idx"])
		var before := pm.is_item_checked(menu["idx"])
		pm.id_pressed.emit(id)
		await wait(0.2)
		R("3d: emitted id_pressed(%d) on the 'Embed Game on Next Play' popup: checked %s -> %s" % [id, str(before), str(pm.is_item_checked(menu["idx"]))])
		await play_check_embed("3d", "3d: after toggling the menu item")
		pm.id_pressed.emit(id)
		await wait(0.2)
		R("3d: toggled back, checked = %s (original %s)" % [str(pm.is_item_checked(menu["idx"])), str(before)])
		# 3e: same toggled state restored: play again to confirm embedding is back
		await play_check_embed("3e", "3e: after restoring the menu item")
		var meta_now = es.get_project_metadata("game_view", "embed_on_play", "__absent__")
		R("project metadata game_view/embed_on_play after restore: %s" % str(meta_now))


# ---------------------------------------------------------------- experiment 4: pause / step

func node_rate(path: String, dur: float) -> String:
	var s0 := last_state(path)
	await wait(dur)
	var s1 := last_state(path)
	var dt: float = s1[0] - s0[0]
	return "%.1f node frames/s (node %d->%d, engine process %d->%d, drawn %d->%d)" % [
		(s1[1] - s0[1]) / dt, s0[1], s1[1], s0[2], s1[2], s0[3], s1[3]]


func p_rows_after(path: String, t: float) -> Array:
	return parse_log(path)["P"].filter(func(r): return r[0] > t)


func exp4() -> void:
	R("--- Experiment 4: pause / step from the plugin ---")
	var tag := "4"
	var path := log_path(tag)
	var t_call4 := play(tag)
	await wait_until(func(): return ev_time("session_started", t_call4) > 0.0, 12.0)
	var ready := await wait_game_ready(tag, 12.0)
	await wait(1.5)
	var sess := active_session()
	R("debugger session active: %s, is_debuggable: %s" % [str(sess != null), str(sess.is_debuggable() if sess != null else false)])
	if sess == null:
		R("no active session, cannot continue")
		await stop_and_wait()
		return
	R("running: %s" % await node_rate(path, 0.5))
	var t_ping := now()
	sess.send_message("handview_spike:ping", [t_ping])
	await wait_until(func(): return ev_time("cap_pong", t_ping) > 0.0, 3.0)
	R("custom message round trip editor->game->editor (ping/pong via EditorDebuggerSession.send_message and _capture): %s" % fmt_ms(ev_time("cap_pong", t_ping), t_ping))

	var t_s := now()
	sess.send_message("scene:suspend_changed", [true])
	await wait(0.5)
	var a := last_state(path)
	await wait(1.0)
	var b := last_state(path)
	R("suspend: send_message(\"scene:suspend_changed\", [true])")
	R("    node frames %d -> %d over %.2f s (delta %d), engine process frames %d -> %d, frames drawn %d -> %d, get_tree().paused in game = %s" % [
		a[1], b[1], b[0] - a[0], b[1] - a[1], a[2], b[2], a[3], b[3], b[4]])
	var after := p_rows_after(path, t_s)
	R("    _process calls after the message: %d, last one %s after send (so frames stop within that time)" % [
		after.size(), fmt_ms(after[after.size() - 1][0], t_s) if after.size() > 0 else "n/a"])

	# Single steps
	for i in 3:
		await wait(0.3)
		var before := last_state(path)
		var t_send := now()
		sess.send_message("scene:next_frame", [])
		await wait(0.4)
		var st := last_state(path)
		var rows := p_rows_after(path, t_send)
		R("step %d: send_message(\"scene:next_frame\", []): node frames %d -> %d (delta %d), _process calls logged %d, first after %s, engine process frames delta %d, paused=%s" % [
			i + 1, before[1], st[1], st[1] - before[1], rows.size(),
			fmt_ms(rows[0][0], t_send) if rows.size() > 0 else "never", st[2] - before[2], st[4]])

	# Burst of two in the same editor frame
	await wait(0.3)
	var bb := last_state(path)
	sess.send_message("scene:next_frame", [])
	sess.send_message("scene:next_frame", [])
	await wait(0.4)
	var ba := last_state(path)
	R("burst: two next_frame messages in the same editor frame: node frames delta %d" % [ba[1] - bb[1]])

	# Five steps 30 ms apart
	var s5 := last_state(path)
	for i in 5:
		sess.send_message("scene:next_frame", [])
		await wait(0.03)
	await wait(0.4)
	var e5 := last_state(path)
	R("5 steps sent 30 ms apart: node frames delta %d" % [e5[1] - s5[1]])

	# Still suspended after steps?
	var c := last_state(path)
	await wait(0.5)
	var d := last_state(path)
	R("after steps still suspended: node frames delta over 0.5 s = %d" % [d[1] - c[1]])

	# Resume
	var t_r := now()
	sess.send_message("scene:suspend_changed", [false])
	await wait(0.2)
	R("resume: send_message(\"scene:suspend_changed\", [false]): %s" % await node_rate(path, 0.5))
	var rows_r := p_rows_after(path, t_r)
	R("    first _process after resume message: %s" % (fmt_ms(rows_r[0][0], t_r) if rows_r.size() > 0 else "never"))
	await wait(0.3)
	var log := parse_log(path)
	var notes: Array = []
	for e in log["E"]:
		if e["name"] == "notification":
			notes.append("%s@+%.0fms" % [e["detail"], (e["t"] - t_s) * 1000.0])
	R("node notifications seen in the game (relative to the suspend message): %s" % str(notes))
	var paused_vals := {}
	for m in log["M"]:
		paused_vals[m[4]] = true
	R("get_tree().paused values seen in the game over the whole run: %s" % str(paused_vals.keys()))
	R("is_playing_scene during all this: %s" % str(EditorInterface.is_playing_scene()))
	await stop_and_wait()
