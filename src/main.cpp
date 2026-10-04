// Unifico -- the Open Doctrines launcher.
#include "App.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "tools/TempleOS.h"
#include "update/SelfUpdate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    ulog::init(upaths::logsDir() + "/unifico.log");

    // The second half of a self-update runs before any window exists: it only
    // waits, deletes, renames and restarts. See update/SelfUpdate.h.
    for (int i = 1; i + 2 < argc + 1; ++i) {
        if (std::strcmp(argv[i], "--finish-update") == 0 && i + 2 < argc)
            return uupdate::finish(argv[i + 1], std::atoll(argv[i + 2]));
    }
    // A maintainer's tool: the TempleOS game disk, built from a folder, so the
    // image the launcher boots can be looked at with ordinary disk tools.
    if (argc == 4 && std::strcmp(argv[1], "--templeos-disk") == 0) {
        std::string err;
        const bool ok = utemple::writeFat32(argv[2], argv[3], "ODGAME", &err);
        if (!ok) std::fprintf(stderr, "%s\n", err.c_str());
        return ok ? 0 : 1;
    }
    // ...and the whole unattended start, end to end: boot, answer, mount,
    // compile, then a picture of whatever the emulator shows.
    if (argc == 3 && std::strcmp(argv[1], "--templeos-selftest") == 0) {
        std::string err;
        auto vm = utemple::boot(&err);
        if (!vm) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        JobPtr j = utemple::autoStart();
        std::string last;
        while (!j->done) {
            if (j->status() != last) { last = j->status(); std::printf("%s\n", last.c_str()); std::fflush(stdout); }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::this_thread::sleep_for(std::chrono::seconds(90));   // the compile
        const bool shot = utemple::screenshot(argv[2]);
        vm->stop();
        std::printf("%s; screenshot %s\n", j->ok ? "started" : j->status().c_str(), shot ? "saved" : "failed");
        return j->ok && shot ? 0 : 1;
    }
    uupdate::tidy();

    App app;
    app.init(argc, argv);
    while (!app.wantsQuit && !WindowShouldClose()) app.frame();
    app.shutdown();
    return 0;
}
