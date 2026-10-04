#pragma once
// Work that must not run on the frame: downloads, unpacking, sign-in polls.
//
// A Job is shared between the worker and the screen. The worker writes
// progress and status; the screen reads them each frame and, when the job is
// done, takes its result. Nothing here touches raylib.
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct Job {
    std::atomic<float> progress{0};       // 0..1, or <0 for "busy, unknown"
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::atomic<bool> cancel{false};
    void setStatus(const std::string& s) { std::lock_guard<std::mutex> l(m); st = s; }
    std::string status() const { std::lock_guard<std::mutex> l(m); return st; }
    void fail(const std::string& why) { setStatus(why); ok = false; }
private:
    mutable std::mutex m;
    std::string st;
};

using JobPtr = std::shared_ptr<Job>;

namespace ujobs {
/** Run `fn` on a detached worker. It returns success; the job is marked done after. */
JobPtr run(const std::string& label, std::function<bool(Job&)> fn);
inline bool busy(const JobPtr& j) { return j && !j->done.load(); }
}  // namespace ujobs
