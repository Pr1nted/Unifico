#include "core/Jobs.h"
#include "core/Log.h"

#include <thread>

namespace ujobs {
JobPtr run(const std::string& label, std::function<bool(Job&)> fn) {
    auto job = std::make_shared<Job>();
    job->setStatus(label);
    std::thread([job, fn, label] {
        bool ok = false;
        try {
            ok = fn(*job);
        } catch (const std::exception& e) {
            job->setStatus(e.what());
            ulog::error(label + ": " + e.what());
        }
        job->ok = ok;
        if (!ok) ulog::warn(label + " failed: " + job->status());
        job->progress = 1.0f;
        job->done = true;
    }).detach();
    return job;
}
}  // namespace ujobs
