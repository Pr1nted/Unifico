#include "core/Window.h"
#include "raylib.h"

#if defined(__APPLE__)
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

namespace uwindow {
#if defined(__APPLE__)
namespace {
// raylib's GetWindowHandle() is the NSWindow* on macOS (glfwGetCocoaWindow).
void* nsWindow() { return GetWindowHandle(); }
}  // namespace

void toggleFullscreen() {
    if (void* w = nsWindow())
        ((void (*)(void*, SEL, void*))objc_msgSend)(w, sel_registerName("toggleFullScreen:"), nullptr);
}

bool isFullscreen() {
    void* w = nsWindow();
    if (!w) return false;
    const unsigned long mask = ((unsigned long (*)(void*, SEL))objc_msgSend)(w, sel_registerName("styleMask"));
    return (mask & (1ul << 14)) != 0;   // NSWindowStyleMaskFullScreen
}

bool needsOwnControls() { return false; }
#else
void toggleFullscreen() { ToggleBorderlessWindowed(); }
bool isFullscreen() { return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); }
bool needsOwnControls() { return isFullscreen(); }
#endif
}  // namespace uwindow
