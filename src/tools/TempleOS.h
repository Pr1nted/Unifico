#pragma once
// Open Doctrines' TempleOS edition, run in an emulator from the launcher.
//
// Nothing about TempleOS is changed or shipped here. The launcher fetches the
// TempleOS live CD from templeos.org (public domain), the game's TempleOS
// release from GitHub, puts the game on a small FAT32 hard disk (TempleOS
// cannot read an ISO9660 CD), and boots QEMU with both. Then it answers the
// boot questions, mounts the disk and starts the game by typing through QEMU's
// monitor, watching the screen to know when each prompt is ready -- the same
// way the project's own templeos/vm.sh does. Every step also has a button, as
// the fallback.
#include "core/Jobs.h"
#include "core/Process.h"
#include <memory>
#include <string>
#include <vector>

namespace utemple {
std::string qemu();                 // path, or empty
std::string qemuHelp();             // how to install it on this system
/** Whether QEMU is there and actually starts; `problem` says what to do if not. */
struct QemuStatus { bool ok = false; std::string path, problem; };
QemuStatus qemuStatus();
bool ready();                       // the live CD and the game's files are present
JobPtr prepare();                   // download everything and build the payload CD
std::unique_ptr<uproc::Child> boot(std::string* error);
/** Typed into the guest through QEMU's monitor. */
bool sendKeys(const std::vector<std::string>& qemuKeys);
bool typeText(const std::string& text);
/** The documented steps, each with what the launcher types for it. */
struct Step { const char* what; std::vector<std::string> keys; std::string text; };
std::vector<Step> steps();
/** Boot to the game unattended: answers, mounts, compiles, starts. Cancel stops it. */
JobPtr autoStart();
/** Save what the emulator shows now, as a PPM. For checking a run from outside. */
bool screenshot(const std::string& ppmPath);
/** Write an ISO9660 image of one directory. Used when no system tool exists. */
/** Write a FAT32 hard-disk image (MBR + one partition) of one directory: the one format TempleOS reads besides its own. */
bool writeFat32(const std::string& srcDir, const std::string& imgPath, const std::string& volume, std::string* error);
bool writeIso(const std::string& srcDir, const std::string& isoPath, const std::string& volume, std::string* error);
}
