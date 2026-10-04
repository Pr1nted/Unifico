#pragma once
// Fullscreen that behaves like the platform's own.
//
// macOS: native fullscreen -- the window moves to its own Space and keeps its
// title bar, traffic lights and Cmd+Q, exactly like the green button. raylib's
// borderless mode is an undecorated window instead: no title bar to drag, no
// close button, which on a Mac reads as a broken window.
// Windows/Linux/BSD: borderless windowed, with Esc and F11 always leaving it
// and an exit button drawn in the top bar while it is on.
namespace uwindow {
void toggleFullscreen();
bool isFullscreen();
/** True where fullscreen hides the system's own window controls. */
bool needsOwnControls();
}
