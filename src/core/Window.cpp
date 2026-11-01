#include "core/Window.h"
#include "raylib.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

namespace uwindow {
void fitToScreen() {
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    int w = GetScreenWidth(), h = GetScreenHeight();
    if (mw <= 0 || mh <= 0 || (w <= mw && h <= mh)) return;
    const float k = std::min(0.9f * mw / w, 0.9f * mh / h);
    w = std::max(640, (int)(w * k));
    h = std::max(400, (int)(h * k));
    SetWindowMinSize(std::min(w, 960), std::min(h, 600));
    SetWindowSize(w, h);
    SetWindowPosition((mw - w) / 2, (mh - h) / 2);
}

bool underGamescope() {
    if (std::getenv("GAMESCOPE_WAYLAND_DISPLAY")) return true;
    const char* d = std::getenv("XDG_CURRENT_DESKTOP");
    return d && std::strcmp(d, "gamescope") == 0;
}

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
