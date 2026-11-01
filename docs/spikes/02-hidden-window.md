# Spike 2: Hidden Play Window (Godot)

## Question

Can the game launched from the phone run without a visible window on the PC, and keep rendering at full rate so frames can be captured?

## What was done

1. `tools/spikes/capture` (spike 1) measured rendering with the window normal, off-screen (`DisplayServer.window_set_position(Vector2i(-20000, -20000))`), off-screen plus `WINDOW_FLAG_NO_FOCUS`, and minimized.
2. The Handview runtime autoload was run with `HANDVIEW_HIDE_WINDOW=1` while streaming to `tools/mock-client`: no focus, position `(-32000, -32000)`, then `HandviewRuntime.set_tool_window()` adds `WS_EX_TOOLWINDOW` and removes `WS_EX_APPWINDOW` (hide, restyle, show without activating). The top-level windows of the game process were then listed with `EnumWindows` / `GetWindowLong(GWL_EXSTYLE)`.
3. Spike 3 (`03-editor-control.md`) covered embedding in the editor's Game workspace.

Machine: Windows 11, RTX 3060, 120 Hz display, Godot 4.7.2.

## Results

| Window | Renders | FPS (vsync on) | Notes |
|---|---|---|---|
| Normal | yes | 119.5 | |
| Off-screen | yes | 119.1 | |
| Off-screen, no focus | yes | 119.0 | |
| Minimized | no | 0 | `frame_post_draw` never fires |
| Runtime hidden mode | yes | streams at the target 60 fps | visible, `WS_EX_TOOLWINDOW` set, `WS_EX_APPWINDOW` cleared, rect at about (-25600, -25600) after DPI scaling |

- The hidden game window has no taskbar button and never takes focus, so the editor keeps keyboard focus.
- Embedding into the Game workspace (default in Godot 4.4+) would show the game inside the editor; the addon turns it off for phone-initiated plays (DECISIONS D13).

## Conclusion

Full hiding works: the game runs at full rate with no visible window, no taskbar button and no focus change. The fallback "window behind other windows" is not needed on this machine.

## Recommendation (adopted)

- Hide only plays started from the phone (`HANDVIEW_HIDE_WINDOW=1`); plays started from the editor keep their normal window.
- Never minimize.
- If the Game workspace menu item cannot be found (future Godot versions), the game runs embedded; the README documents this.
