extends Control
## Spike game scene: shows a frame counter and, when HANDVIEW_SPIKE=1, logs its own state.
##
## Log lines (tab separated, unix time in seconds):
##   E <t> <name> <detail>                         event
##   P <t> <node_frames> <process_frames> <drawn> <paused>   one per _process call
##   M <t> <node_frames> <process_frames> <drawn> <paused>   monitor thread, every 10 ms
## node_frames counts _process calls of this node (stops when the SceneTree is suspended).
## process_frames and drawn are engine counters (keep running while suspended).

const ENV_KEYS := ["HANDVIEW_SPIKE", "HANDVIEW_LINK_PORT", "HANDVIEW_LINK_TOKEN", "HANDVIEW_SPIKE_ACTION", "HANDVIEW_SPIKE_LOG"]

var spike := false
var log_path := ""
var log_ok := false
var log_mutex := Mutex.new()
var node_frames := 0
var phys_frames := 0
var monitor: Thread
var monitor_stop := false
var action := ""
var action_delay := 2.0
var action_fired := false
var started_ticks := 0

@onready var counter: Label = $Counter


func _t() -> float:
	return Time.get_unix_time_from_system()


func _log(line: String) -> void:
	# Open, append and close for every line: the editor plugin reads this file while the game
	# runs (a FileAccess held open for writing cannot be opened for reading on Windows).
	if not log_ok:
		return
	log_mutex.lock()
	for attempt in 20:
		var f := FileAccess.open(log_path, FileAccess.READ_WRITE)
		if f != null:
			f.seek_end()
			f.store_line(line)
			f.close()
			break
		OS.delay_msec(1)
	log_mutex.unlock()


func _event(name: String, detail: String = "") -> void:
	_log("E\t%.4f\t%s\t%s" % [_t(), name, detail])


func _ready() -> void:
	started_ticks = Time.get_ticks_msec()
	spike = OS.get_environment("HANDVIEW_SPIKE") == "1"
	if not spike:
		return
	log_path = OS.get_environment("HANDVIEW_SPIKE_LOG")
	if log_path.is_empty():
		log_path = ProjectSettings.globalize_path("res://logs/game_log.txt")
	var f := FileAccess.open(log_path, FileAccess.WRITE)
	if f == null:
		push_error("spike: cannot open log %s" % log_path)
		return
	f.close()
	log_ok = true
	_event("ready", "pid=%d" % OS.get_process_id())
	for k in ENV_KEYS:
		_event("env", "%s=%s (has=%s)" % [k, OS.get_environment(k), str(OS.has_environment(k))])
	_event("cmdline", " ".join(OS.get_cmdline_args()))
	_event("info", "debug_build=%s embedded_in_editor=%s display=%s window_mode=%d version=%s" % [
		str(OS.is_debug_build()), str(Engine.is_embedded_in_editor()),
		DisplayServer.get_name(), DisplayServer.window_get_mode(), Engine.get_version_info()["string"]])
	action = OS.get_environment("HANDVIEW_SPIKE_ACTION")
	_event("debugger", "EngineDebugger.is_active=%s" % str(EngineDebugger.is_active()))
	if EngineDebugger.is_active():
		EngineDebugger.register_message_capture("handview_spike", _on_debugger_message)
		EngineDebugger.send_message("handview_spike:hello", [OS.get_process_id()])
	monitor = Thread.new()
	monitor.start(_monitor_loop)


func _on_debugger_message(message: String, data: Array) -> bool:
	# Editor -> game custom message. The capture callable gets the part after "handview_spike:".
	_event("debugger_rx", "%s %s" % [message, str(data)])
	if message == "ping":
		EngineDebugger.send_message("handview_spike:pong", data)
	return true


func _monitor_loop() -> void:
	while not monitor_stop:
		_log("M\t%.4f\t%d\t%d\t%d\t%s" % [_t(), node_frames, Engine.get_process_frames(), Engine.get_frames_drawn(), str(get_tree().paused)])
		OS.delay_msec(10)


func _process(_delta: float) -> void:
	node_frames += 1
	counter.text = "Frame: %d\nspike=%s action=%s\npaused=%s" % [node_frames, str(spike), action, str(get_tree().paused)]
	if not spike or not log_ok:
		return
	_log("P\t%.4f\t%d\t%d\t%d\t%s" % [_t(), node_frames, Engine.get_process_frames(), Engine.get_frames_drawn(), str(get_tree().paused)])
	if action != "" and not action_fired and (Time.get_ticks_msec() - started_ticks) >= int(action_delay * 1000.0):
		action_fired = true
		_event("action", action)
		match action:
			"quit":
				get_tree().quit()
			"crash":
				OS.crash("spike")
			"kill":
				OS.kill(OS.get_process_id())


func _physics_process(_delta: float) -> void:
	phys_frames += 1


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		_event("close_request")
	elif what == 9003:
		# Node::NOTIFICATION_SUSPENDED (not bound as a GDScript constant in 4.4 nor 4.7).
		_event("notification", "SUSPENDED (9003)")
	elif what == 9004:
		_event("notification", "UNSUSPENDED (9004)")


func _exit_tree() -> void:
	_event("exit_tree", "node_frames=%d phys=%d" % [node_frames, phys_frames])
	monitor_stop = true
	if monitor != null and monitor.is_started():
		monitor.wait_to_finish()
