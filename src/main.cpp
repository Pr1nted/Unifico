// Unifico -- the Open Doctrines launcher.
#include "App.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "update/SelfUpdate.h"

#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    ulog::init(upaths::logsDir() + "/unifico.log");

    // The second half of a self-update runs before any window exists: it only
    // waits, deletes, renames and restarts. See update/SelfUpdate.h.
    for (int i = 1; i + 2 < argc + 1; ++i) {
        if (std::strcmp(argv[i], "--finish-update") == 0 && i + 2 < argc)
            return uupdate::finish(argv[i + 1], std::atoll(argv[i + 2]));
    }
    uupdate::tidy();

    App app;
    app.init(argc, argv);
    while (!app.wantsQuit && !WindowShouldClose()) app.frame();
    app.shutdown();
    return 0;
}
