#pragma once
// Discord rich presence for the launcher.
//
// The protocol is the game's (Open Doctrines src/stream/DiscordRpc.cpp): a
// handshake with the application id, then SET_ACTIVITY frames, over Discord's
// local IPC -- a Unix socket on macOS, Linux and the BSDs, and a named pipe
// (\\.\pipe\discord-ipc-N) on Windows, which the game's copy does not speak.
//
// While Open Doctrines itself runs, the launcher steps aside: the game shows
// its own presence (country, turn) and two clients publishing at once would
// flicker between them. For the other games, which publish nothing, the
// launcher shows "Playing Unciv" with the session's start time.
#include <string>

namespace udiscord {
void init(const std::string& appId);
/** Publish; cheap to call every frame. Empty `details` clears. */
void set(const std::string& details, const std::string& state, long long startedAt);
void clear();
void tick(double now);
bool connected();
}
