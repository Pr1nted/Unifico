#pragma once
// Can this computer run it? Decided by LOOKING, not by trusting a file name.
//
// A game binary's own header says what it is: Mach-O, ELF or PE, and for which
// CPU. That is compared with this machine, allowing only the emulation the
// operating system really provides:
//   macOS on Apple Silicon  runs x86_64 under Rosetta, if Rosetta is installed
//   Windows on Arm          runs x64 and x86
//   Windows x64             runs x86
// Everything else must match exactly. An installation copied from another
// system, or a build for the wrong CPU, is listed but never started.
#include <string>

struct Verdict {
    bool ok = true;
    // When !ok: an English TEMPLATE (marked N_ for the extractor) and up to two
    // values for its %s, so the sentence is translated where it is drawn
    // rather than assembled in English here.
    std::string why;
    std::string a1, a2;
};

/** The reason, translated and filled in. Defined in ui/Strings.cpp. */
std::string verdictText(const Verdict& v);

namespace usupport {
/** The format and CPU of an executable, e.g. "macOS arm64", "Windows x64". */
std::string describe(const std::string& exe);
Verdict binary(const std::string& exe);
/** This machine, in the same words. */
std::string host();
}
