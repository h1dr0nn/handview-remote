// macOS side of HandviewRuntime::set_tool_window: a play started from the phone
// gets no Dock icon, never takes focus from the editor and is not visible
// (DECISIONS D13, D23).
#import <AppKit/AppKit.h>

#include <cstdint>

namespace handview {

bool macos_set_tool_window(int64_t native_window_handle) {
    NSWindow* window = (NSWindow*)(intptr_t)native_window_handle;
    if (window == nil || ![window isKindOfClass:[NSWindow class]]) return false;
    // An accessory app has no Dock icon and no menu bar of its own.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    // macOS keeps part of a titled window on screen whatever position it is
    // given, so the window is also made fully transparent and click-through.
    // It still renders at full rate (unlike a minimized one).
    window.ignoresMouseEvents = YES;
    window.alphaValue = 0.0;
    window.hasShadow = NO;
    // Godot stops drawing a window that macOS reports as occluded, and the
    // editor covers this one. Floating above everything, on every Space, the
    // (invisible) window never counts as occluded.
    window.level = NSStatusWindowLevel;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle |
                                NSWindowCollectionBehaviorFullScreenAuxiliary;
    return true;
}

}  // namespace handview
