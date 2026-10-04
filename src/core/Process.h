#pragma once
// Starting the game (and the other games), watching it, and stopping it.
//
// The child's stdout and stderr are captured into one line buffer the console
// panel reads, and into the per-launch log file. A memory ceiling is enforced
// the way each platform allows:
//
//   Windows  a Job Object with JOB_OBJECT_LIMIT_PROCESS_MEMORY -- the kernel
//            refuses the allocation, and the job dies with the launcher.
//   Linux/BSD RLIMIT_AS in the child before exec -- the allocation fails.
//   macOS    the kernel ignores RLIMIT_AS, so a watchdog samples the resident
//            size twice a second and stops the game when it passes the ceiling,
//            saying so in the console. Softer, and said to be softer.
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uproc {

struct Spec {
    std::string exe;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;   // added to ours
    std::string cwd;
    std::string logPath;          // every line is also appended here
    int memoryLimitMB = 0;        // 0 = none
};

class Child {
public:
    ~Child();
    bool start(const Spec& spec, std::string* error);
    bool running() const;
    int exitCode() const { return m_exit.load(); }
    void stop();                  // polite, then forceful after a few seconds
    /** Lines since `from`; `from` is advanced. */
    std::vector<std::string> linesSince(size_t& from) const;
    std::vector<std::string> allLines() const;
    bool killedForMemory() const { return m_memKill.load(); }
    long long pid() const { return m_pid; }

private:
    void reader(int which);
    void push(const std::string& line);
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
    std::string m_partial;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_exit{0};
    std::atomic<bool> m_memKill{false};
    std::thread m_out, m_wait, m_watch;
    std::string m_logPath;
    long long m_pid = 0;
#if defined(_WIN32)
    void* m_process = nullptr;
    void* m_job = nullptr;
    void* m_pipe = nullptr;
#else
    int m_fd = -1;
#endif
};

/** Fire and forget: a browser, a file manager. */
void openUrl(const std::string& url);
void revealInFileManager(const std::string& path);
/** Run a command to completion, returning its exit code; output discarded. */
int runQuiet(const std::string& exe, const std::vector<std::string>& args);
/** Find an executable on PATH; empty if absent. */
std::string which(const std::string& name);
/** Start a process detached from the launcher (the updater hands over this way). */
bool spawnDetached(const std::string& exe, const std::vector<std::string>& args);
/** Whether a pid is alive. */
bool pidAlive(long long pid);
long long selfPid();
}  // namespace uproc
