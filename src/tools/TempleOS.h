#pragma once
// Open Doctrines' TempleOS edition, run in an emulator from the launcher.
//
// Nothing about TempleOS is changed or shipped here. The launcher fetches the
// TempleOS live CD from templeos.org (public domain), the game's TempleOS
// release from GitHub, puts the game on a second CD image, and boots QEMU with
// both. TempleOS asks two questions at boot and then wants a few commands
// typed; the launcher can type them through QEMU's monitor, exactly as the
// project's own templeos/vm.sh does.
#include "core/Jobs.h"
#include "core/Process.h"
#include <memory>
#include <string>
#include <vector>

namespace utemple {
std::string qemu();                 // path, or empty
std::string qemuHelp();             // how to install it on this system
bool ready();                       // ISO, payload and payload.iso all present
JobPtr prepare();                   // download everything and build the payload CD
std::unique_ptr<uproc::Child> boot(std::string* error);
/** Typed into the guest through QEMU's monitor. */
bool sendKeys(const std::vector<std::string>& qemuKeys);
bool typeText(const std::string& text);
/** The documented steps, each with what the launcher types for it. */
struct Step { const char* what; std::vector<std::string> keys; std::string text; };
std::vector<Step> steps(const std::string& payloadDrive);
/** Write an ISO9660 image of one directory. Used when no system tool exists. */
bool writeIso(const std::string& srcDir, const std::string& isoPath, const std::string& volume, std::string* error);
}
