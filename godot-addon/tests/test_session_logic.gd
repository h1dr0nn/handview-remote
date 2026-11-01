extends RefCounted

const Logic := preload("res://addons/handview/editor/session_logic.gd")

var failures: Array = []


func _eq(actual: Variant, expected: Variant, label: String) -> void:
	if typeof(actual) != typeof(expected) or actual != expected:
		failures.append("%s: expected %s, got %s" % [label, str(expected), str(actual)])


func _decide(cmd: int, state: int, result: int, action: String, can_play: bool = true) -> void:
	var decision: Dictionary = Logic.decide_command(cmd, state, can_play)
	var label: String = "cmd %d state %d" % [cmd, state]
	_eq(decision["result"], result, label + " result")
	_eq(decision["action"], action, label + " action")


func test_play_decisions() -> void:
	_decide(Logic.CMD_PLAY, Logic.STATE_STOPPED, Logic.RESULT_OK, Logic.ACTION_PLAY)
	_decide(Logic.CMD_PLAY, Logic.STATE_PLAYING, Logic.RESULT_IGNORED, Logic.ACTION_NONE)
	_decide(Logic.CMD_PLAY, Logic.STATE_PAUSED, Logic.RESULT_IGNORED, Logic.ACTION_NONE)
	_decide(Logic.CMD_PLAY, Logic.STATE_STOPPED, Logic.RESULT_IGNORED, Logic.ACTION_NONE, false)


func test_stop_decisions() -> void:
	_decide(Logic.CMD_STOP, Logic.STATE_PLAYING, Logic.RESULT_OK, Logic.ACTION_STOP)
	_decide(Logic.CMD_STOP, Logic.STATE_PAUSED, Logic.RESULT_OK, Logic.ACTION_STOP)
	_decide(Logic.CMD_STOP, Logic.STATE_STOPPED, Logic.RESULT_IGNORED, Logic.ACTION_NONE)


func test_pause_and_step_are_ignored() -> void:
	for state: int in [Logic.STATE_STOPPED, Logic.STATE_PLAYING, Logic.STATE_PAUSED]:
		_decide(Logic.CMD_PAUSE, state, Logic.RESULT_IGNORED, Logic.ACTION_NONE)
		_decide(Logic.CMD_STEP, state, Logic.RESULT_IGNORED, Logic.ACTION_NONE)


func test_busy_while_compiling_or_reloading() -> void:
	for state: int in [Logic.STATE_COMPILING, Logic.STATE_RELOADING]:
		_decide(Logic.CMD_PLAY, state, Logic.RESULT_BUSY, Logic.ACTION_NONE)
		_decide(Logic.CMD_STOP, state, Logic.RESULT_BUSY, Logic.ACTION_NONE)


func test_unknown_command_is_ignored() -> void:
	_decide(0, Logic.STATE_STOPPED, Logic.RESULT_IGNORED, Logic.ACTION_NONE)
	_decide(99, Logic.STATE_PLAYING, Logic.RESULT_IGNORED, Logic.ACTION_NONE)


func test_state_after_action() -> void:
	_eq(Logic.state_after(Logic.ACTION_PLAY, Logic.STATE_STOPPED), Logic.STATE_PLAYING, "play")
	_eq(Logic.state_after(Logic.ACTION_STOP, Logic.STATE_PLAYING), Logic.STATE_STOPPED, "stop")
	_eq(Logic.state_after(Logic.ACTION_NONE, Logic.STATE_PLAYING), Logic.STATE_PLAYING, "none")


func test_playing_transition() -> void:
	_eq(Logic.playing_transition(false, false), {}, "idle")
	_eq(Logic.playing_transition(true, true), {}, "running")
	_eq(Logic.playing_transition(false, true), {"state": 1, "stream_paused": false}, "started")
	_eq(Logic.playing_transition(true, false), {"state": 0, "stream_paused": true}, "stopped")


func test_game_disconnect_rule() -> void:
	_eq(Logic.game_disconnect_is_stop(false), true, "not playing")
	_eq(Logic.game_disconnect_is_stop(true), false, "still playing")


func test_bitrate_default_and_clamp() -> void:
	_eq(Logic.default_bitrate_kbps(1920, 1080, 60), 12441, "1080p60")
	_eq(Logic.default_bitrate_kbps(1280, 720, 60), 5529, "720p60")
	_eq(Logic.default_bitrate_kbps(320, 240, 30), 2000, "clamped low")
	_eq(Logic.default_bitrate_kbps(3840, 2160, 60), 20000, "clamped high")


func test_announced_encoder_fallback() -> void:
	_eq(Logic.announced_encoder(-1), 1, "none")
	_eq(Logic.announced_encoder(0), 0, "nvenc")
	_eq(Logic.announced_encoder(2), 2, "software")


func test_encoder_line() -> void:
	_eq(Logic.encoder_line(0), "Encoder: NVENC", "nvenc")
	_eq(Logic.encoder_line(-1), "No H.264 encoder found", "none")


func test_embed_label_matching() -> void:
	var candidates: PackedStringArray = Logic.embed_candidates(PackedStringArray(["Spiel beim naechsten Start einbetten", "", "Embed Game on Next Play"]))
	_eq(candidates.size(), 2, "deduplicated")
	_eq(Logic.embed_text_matches("Embed Game on Next Play", candidates), true, "english")
	_eq(Logic.embed_text_matches("  Embed Game on Next Play ", candidates), true, "trimmed")
	_eq(Logic.embed_text_matches("Spiel beim naechsten Start einbetten", candidates), true, "translated")
	_eq(Logic.embed_text_matches("Embed Game", candidates), false, "partial")
	var texts: PackedStringArray = PackedStringArray(["Keep Aspect", "Embed Game on Next Play", "Make Game Workspace Floating"])
	_eq(Logic.find_embed_index(texts, candidates), 1, "index")
	_eq(Logic.find_embed_index(PackedStringArray(["Other"]), candidates), -1, "missing")


func test_device_hints() -> void:
	_eq(Logic.device_hint("unauthorized", true), "Accept the USB debugging prompt on the phone.", "unauthorized")
	_eq(Logic.device_hint("offline", true), "Reconnect the USB cable.", "offline")
	_eq(Logic.device_hint("device", true), "", "ready")
	_eq(Logic.device_hint("device", false), Logic.SDK_HINT, "no adb")
	_eq(Logic.can_start("device"), true, "can start")
	_eq(Logic.can_start("offline"), false, "cannot start")


func test_device_list_helpers() -> void:
	var real: Array = [{"serial": "ABC", "state": "device", "model": "SM_A528B"}]
	_eq(Logic.merge_devices(real, false).size(), 1, "no loopback")
	var merged: Array = Logic.merge_devices(real, true)
	_eq(merged.size(), 2, "with loopback")
	_eq(Logic.device_label(merged[1]), "Loopback (Test)", "loopback label")
	_eq(Logic.device_label(merged[0]), "SM A528B (ABC)", "model label")
	_eq(Logic.device_label({"serial": "XYZ", "state": "offline", "model": ""}), "XYZ, offline", "state label")
	_eq(Logic.preselect_index(merged, "ABC"), 0, "remembered")
	_eq(Logic.preselect_index(merged, "gone"), -1, "unknown with two")
	_eq(Logic.preselect_index(real, "gone"), 0, "single device")
	_eq(real.size(), 1, "input untouched")


func test_messages() -> void:
	_eq(Logic.session_ended_message(false), "Session ended.", "ended")
	_eq(Logic.session_ended_message(true), "Session ended: the phone stopped responding.", "timeout")
	var parsed: Dictionary = JSON.parse_string(Logic.loopback_json(38500, "ab"))
	_eq(int(parsed["port"]), 38500, "loopback port")
	_eq(parsed["token"], "ab", "loopback token")
	_eq(Logic.phone_line({"model": "Pixel", "screen_w": 1080, "screen_h": 2400}), "Phone: Pixel, 1080x2400", "phone line")
	_eq(Logic.phone_line({}), "", "no phone")


func test_launch_timeout() -> void:
	_eq(Logic.LAUNCH_TIMEOUT_SEC, 20.0, "timeout constant")
	_eq(Logic.launch_wait_expired(true, 19.9), false, "not yet")
	_eq(Logic.launch_wait_expired(true, 20.0), true, "at limit")
	_eq(Logic.launch_wait_expired(true, 45.0), true, "past limit")
	_eq(Logic.launch_wait_expired(false, 45.0), false, "phone already connected")
	_eq(Logic.should_stop_listener_on_timeout(false), true, "stop listener without phone")
	_eq(Logic.should_stop_listener_on_timeout(true), false, "keep listener with phone")
	_eq(
		Logic.LAUNCH_TIMEOUT_MESSAGE,
		"The phone did not connect. Check the phone screen and press Start Session again.",
		"timeout message"
	)


func test_launch_failed() -> void:
	_eq(Logic.launch_failed(0, "Starting: Intent { cmp=io.github.h1dr0nn.handview/.MainActivity }"), false, "ok")
	_eq(Logic.launch_failed(0, "Starting: Intent { }\nWarning: Activity not started, its current task has been brought to the front"), false, "warning only")
	_eq(Logic.launch_failed(1, ""), true, "exit code")
	_eq(Logic.launch_failed(0, "Error: Activity class {io.github/.MainActivity} does not exist."), true, "Error prefix")
	_eq(Logic.launch_failed(0, "error: device not found"), true, "lowercase error")
	_eq(Logic.launch_failed(0, "ERROR: closed"), true, "uppercase error")
