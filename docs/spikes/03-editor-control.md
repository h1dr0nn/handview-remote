# Spike 3: Editor control of the played game

## Question

Can an editor plugin (a) hand parameters to the game through environment variables (DECISIONS D6), (b) detect game start, exit and crash, (c) keep control over the Game workspace embedding, and (d) pause and single-step the running game for the Play control commands Pause / Step (protocol 6.4)? And which of this exists on Godot 4.4, the minimum we target?

## What was done

`tools/spikes/editor-control` is a Godot project. Its main scene shows a frame counter and, with `HANDVIEW_SPIKE=1`, logs env values, `_process` calls, engine frame counters and `get_tree().paused` to a file. The editor plugin in `addons/spike` runs the experiments unattended on editor start, writes `results.txt` and quits the editor:

```bash
HANDVIEW_SPIKE_RUN=1,2 Godot_v4.7.2-stable_win64_console.exe --path tools/spikes/editor-control --editor   # also 3 and 4 (each run stays under 60 s)
```

Machine: Windows 11, Godot 4.7.2 stable (the only version installed), Forward+, display driver `Windows`. The raw output is `tools/spikes/editor-control/results.txt`, the per-play game logs are in `tools/spikes/editor-control/logs/`. Timing uses `Time.get_unix_time_from_system()` in both processes (observed step 1 to 2 ms), so every delay below has about +-2 ms error.

Godot 4.4: **only 4.7.2 was run.** For 4.4 I compared the 4.4-stable class reference XML and the 4.4-stable C++ source (`game_view_plugin.cpp`, `scene_debugger.cpp`, `script_editor_debugger.cpp`, `editor_run.cpp`, `main.cpp`, `editor_settings.cpp`) with 4.7.2. Everything marked "4.4: source and docs" was not executed on 4.4.

## Results

### 1. Environment inheritance (works)

| Case | Game sees |
|---|---|
| 1a: `OS.set_environment` PORT and TOKEN, `play_main_scene()`, `OS.unset_environment` in the same frame | both values (`12345`, token) |
| 1b: second Play, nothing set, editor had unset both | neither (`has_environment` false) |
| 1c: unset one editor frame after `play_main_scene()` | both values |
| 1d: control, PORT left set in the editor | PORT visible (stale values do leak if not cleared) |

The child process is created synchronously inside `play_main_scene()`, so clearing right after the call is safe and a later Play never sees stale values. The engine itself does the same with `GODOT_EDITOR_CUSTOM_FEATURES` around `OS.create_instance` (`editor_run.cpp`, 4.7; the call to `unset_environment` is not in the 4.4 file, but `OS.set_environment` / `unset_environment` are documented in both). Game start: `_ready` runs about 1.9 to 2.4 s after the call (process start, Vulkan init, embedding). Game `OS.get_cmdline_args()` only shows `--scene res://main.tscn`; editor flags are consumed.

### 2. Lifecycle detection

Delays are measured from the event in the game or editor to the plugin noticing it (4 runs, one per case).

| Event | `session_stopped` signal | `is_playing_scene()` false seen by polling `_process` |
|---|---|---|
| `stop_playing_scene()` | 3 ms | 104 ms (the call itself returns false synchronously) |
| Game calls `get_tree().quit()` | 42 ms | 158 ms |
| Game `OS.kill(own pid)` | 440 ms (earlier runs: 208, 235 ms) | 553 ms (earlier: 276, 303) |
| Game `OS.crash("spike")` | 7306 ms (earlier: 6932, 7132) | 7385 ms (earlier: 7012, 7218) |

At start: `is_playing_scene()` is true immediately after `play_main_scene()`. `EditorDebuggerSession.started` fires 137 to 179 ms after the call, which is when the game process attaches to the debugger (`set_pid` message), about 1.8 s before the game's `_ready`. It does not mean the game is up. A custom message from the game (`EngineDebugger.send_message("handview_spike:hello", ...)` in `_ready`, received by `EditorDebuggerPlugin._capture` after `_has_capture("handview_spike")`) arrived 1.99 to 2.45 s after the call, about 70 ms after `_ready`. Round trip editor to game and back with `EditorDebuggerSession.send_message("handview_spike:ping")`: 31 to 44 ms.

Notes:
- The crash case is slow because the crashing process stays alive about 7 s (crash handler); frames stop at once (last `_process` 14 ms before the log line). Both the debugger and `is_playing_scene()` only react when the process is gone.
- `session_stopped` is emitted twice for quit and kill (debugger link drop, then editor stop handling about 115 ms later); once for the editor stop.
- The editor main loop is not smooth: `_process` interval mean 7 to 12 ms but max 240 to 1390 ms (spikes while the game starts and embeds). In embedded mode the editor turns off its unfocused low processor mode during play. Without embedding the game window takes focus and the editor would fall back to `interface/editor/timers/unfocused_low_processor_mode_sleep_usec` (default 100 ms; named `interface/editor/unfocused_low_processor_mode_sleep_usec` in 4.4). I did not measure the non-embedded case.
- The game process dies without `exit_tree` or `close_request` on editor stop, kill and crash. Only a game quit runs `_exit_tree`. A stop is therefore not a graceful shutdown.

### 3. Embedding

- Setting: editor setting `run/window_placement/game_embed_mode` (enum: -1 Disabled, 0 Use Per-Project Configuration, 1 Embed Game, 2 Make Game Workspace Floating). Default 0 in 4.7.2 (confirmed in this editor's settings file) and in 4.4 (source: `default_game_embed_mode = 0`, same hints, documented in the 4.4 `EditorSettings` reference). With 0, the per-project value comes from editor project metadata `game_view/embed_on_play`, default `true`. So **embedding is on by default** on Windows; Windows support exists in 4.4 (`FEATURE_WINDOW_EMBEDDING` in `display_server_windows.cpp`, 4.4-stable).
- Reading from a plugin works: `EditorSettings.get_setting(...)`, `get_project_metadata("game_view", "embed_on_play", default)`, and in the game `Engine.is_embedded_in_editor()` (true or false, documented in 4.4 and set by the `--wid` argument in 4.4 `main.cpp`).
- Changing it from a plugin before `play_main_scene()`:

| Attempt | Effect on next Play |
|---|---|
| `set_setting("run/window_placement/game_embed_mode", -1)` at runtime | none, game still embedded |
| `set_project_metadata("game_view", "embed_on_play", false)` | none, game still embedded |
| find the `GameView` node (`get_editor_main_screen()` children, `get_class() == "GameView"`), find the `MenuButton` popup item "Embed Game on Next Play" (id 6 in 4.7.2) and `popup.id_pressed.emit(6)` | works: game not embedded; emitting again restores; third Play embedded again |

  The first two do nothing because `GameView` reads both only once in `NOTIFICATION_READY` (4.4 source line 639 and 4.7.2 source line 1162 agree). The settings are changed by the editor only through the menu handler. The menu trick also writes `game_view/embed_on_play` to `.godot/editor/project_metadata.cfg` (mode 0), which is gitignored. It relies on an internal class and the English menu text (the popup item id is an enum, text and id were stable between 4.4 source and 4.7.2).
- The window. Embedded: the game window is one top-level Win32 window, `WS_CHILD` not set, owned by another editor window (owner handle nonzero), 512x288 at the Game workspace rect (the non-embedded window is 526x326, which suggests a title bar and borders there; style bits other than WS_CHILD not inspected). Not embedded: top-level, no owner (parent 0), decorated, 526x326, visible on the desktop. So only the non-embedded case is a normal separate window. The embedded window is not in the taskbar (comment in 4.4 `display_server_windows.cpp`).
- Starting a game while embedded also switches the editor to the Game workspace; the editor switches back on stop (`screen_index_before_start`).

### 4. Pause and step (works)

Messages sent with `EditorDebuggerSession.send_message` on the active session (obtained from `EditorDebuggerPlugin.get_sessions()`), the same ones the Game workspace uses (`GameViewDebugger::set_suspend` and `next_frame`):

| Message | Data | Effect in the game |
|---|---|---|
| `scene:suspend_changed` | `[true]` / `[false]` | `SceneTree.set_suspend(bool)` |
| `scene:next_frame` | `[]` | if suspended: unsuspend, re-suspend at `RenderingServer.frame_post_draw` |

Measured (game log, `_process` call counter of a node):

- Running: about 90 node frames/s (embedded, 120 Hz display).
- Suspend: node frames 181 to 181 over 1.0 s (delta 0). The last `_process` was 5 ms after the message. The engine keeps cycling: engine process frames and frames drawn went 240 to 360 in the same second, so the **renderer keeps drawing the last image at the display rate** while the scene does not run.
- Step: 3 single steps gave node frame delta 1, 1, 1; first `_process` 12 to 22 ms after the message. Two `scene:next_frame` in the same editor frame: delta 1 (the second is ignored because the tree is not suspended any more). Five steps sent 30 ms apart: delta 5. Still suspended afterwards (delta 0 over 0.5 s).
- Resume: back to 89 node frames/s, first `_process` 18 ms after the message.
- `get_tree().paused` stayed `false` the whole time. It is a different mechanism; `SceneTree.paused` cannot even be set while suspended (`scene_tree.cpp`, "Pause state cannot be modified while suspended").
- The game sees the state change as node notifications 9003 (`NOTIFICATION_SUSPENDED`) and 9004 (`NOTIFICATION_UNSUSPENDED`), 20 ms after the message; each step shows one UNSUSPENDED then SUSPENDED pair 10 to 20 ms apart. They are not bound as GDScript constants in 4.4 or 4.7 (value 9003 and 9004 are the same in both `node.h` files). `SceneTree.is_suspended()` is not exposed to scripts.
- The debugger is on by default: `EditorRun` always passes `--remote-debug tcp://...` (4.4 and 4.7 `editor_run.cpp`), and the game logged `EngineDebugger.is_active() = true`. The scene debugger handles these messages only in a game started by the editor with a debugger; an exported release build would not.
- No `EditorInterface` method pauses or steps the game (documented methods are `play_*`, `stop_playing_scene`, `is_playing_scene`, `get_playing_scene`, same in 4.4 and 4.7).
- Not tested: the Game workspace's own pause button is not updated when the plugin sends the message (inferred from source: the button state lives in `GameView`). If the user presses it, states can disagree.

### API availability on 4.4 (class reference)

| API used | In 4.7.2 (ran) | In 4.4 |
|---|---|---|
| `EditorInterface.play_main_scene / stop_playing_scene / is_playing_scene / get_editor_settings / get_editor_main_screen / get_resource_filesystem` | yes | yes, docs |
| `OS.set_environment / unset_environment / get_environment / has_environment / kill / crash` | yes | yes, docs |
| `EditorPlugin.add_debugger_plugin`, `EditorDebuggerPlugin` (`_setup_session`, `_has_capture`, `_capture`, `get_sessions`), `EditorDebuggerSession` (`send_message`, `is_active`, signals `started`, `stopped`, `breaked`, `continued`) | yes | yes, docs |
| `EngineDebugger.is_active / register_message_capture / send_message` | yes | yes, docs |
| `Engine.is_embedded_in_editor()` | yes | yes, docs and source |
| `EditorSettings.get/set_setting`, `get/set_project_metadata`, setting `run/window_placement/game_embed_mode` | yes | yes, docs and source |
| Messages `scene:suspend_changed`, `scene:next_frame` | yes | yes, source only (`game_view_plugin.cpp` 97 to 112, `scene_debugger.cpp` 136 to 143, identical logic) |
| Notification values 9003 / 9004 | yes | yes, source only (`node.h`) |
| `GameView` class and "Embed Game on Next Play" popup item | yes | yes, source only (same text and enum id) |

Not verified on 4.4 by running: all timings, the embedding behaviour, the pause and step semantics, the crash delay. Also not tested: macOS or Linux, non-embedded lifecycle timing, game window minimized, Game workspace floating mode.

## Conclusion

- D6 holds: env vars set before `play_main_scene()` and cleared right after reach the game, and later Plays are clean.
- The editor sees start and stop reliably, but at editor-main-loop granularity (about 100 to 550 ms for exit and kill, and about 7 s for a crash). `EditorDebuggerSession.started` means "process attached", not "game running".
- The embedding mode cannot be changed through editor settings or metadata at runtime. Only the internal menu trick works. Embedded is the default and the game window is then not a normal window.
- Pause, Step and Resume can be implemented with two debugger messages. Step is exact (one `_process` per message) with 12 to 22 ms latency. Suspending does not stop the renderer.

## Recommendation for Handview

1. Keep D6 as is (set, call `play_main_scene()`, unset immediately). Do not rely on `EditorDebuggerSession.started` for "game is up"; the game's own `hello` to the editor link (D7) is the signal, with `EngineDebugger.send_message("handview:hello")` plus `_capture` as an available fallback channel.
2. Lifecycle: treat `session_stopped` and `is_playing_scene()` as secondary. The primary signals are the loopback link closing and a frame watchdog on the relay (frames stop immediately on kill or crash while the editor needs 0.5 s or 7 s). Report `EditorState{Stopped}` from whichever comes first; accept up to about 7 s on a hard crash if no watchdog is added. Do not poll from the main loop alone when timing matters (D5 already puts the relay on a native thread).
3. Pause and Step (spec 6.4 marks them "only if supported"): this host can support them. Implement `Pause` as `scene:suspend_changed [true/false]` and `Step` as `scene:next_frame` via the active `EditorDebuggerSession`; keep the paused state in the host. Because the engine keeps drawing while suspended, the game runtime must not keep capturing and encoding frames during Pause: either the editor tells the game over the link, or the game listens for notifications 9003 and 9004 (4.4 and 4.7 values), with the caveat that every Step produces a short UNSUSPENDED then SUSPENDED pair, so for a Step capture exactly one frame on the first `frame_post_draw` after the UNSUSPENDED. Ignore `Step` when not paused (matches the protocol table).
4. Embedding: do not try to change it. Detect it in the game with `Engine.is_embedded_in_editor()` and make the runtime work in both modes (it reads the root viewport either way). The "PC window hidden" assumption behind D12 (vsync off, `max_fps` set) still applies to the game process; check the readback cost in embedded mode on target machines. If a "force separate window" option is ever wanted, the popup `id_pressed` trick is best effort only: find by item text, verify with `Engine.is_embedded_in_editor()` in the launched game, and restore the previous state after launch.
5. Update the spec text: add that on host Godot the Game workspace buttons are not synchronized with Handview's Pause, and re-test on 4.4 before claiming 4.4 support for pause, step and embedding.
