#include "core/Process.h"
#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#  include <psapi.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/resource.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <libproc.h>
#  endif
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace uproc {

Child::~Child() {
    stop();
    if (m_out.joinable()) m_out.join();
    if (m_wait.joinable()) m_wait.join();
    if (m_watch.joinable()) m_watch.join();
#if defined(_WIN32)
    if (m_job) CloseHandle((HANDLE)m_job);
    if (m_process) CloseHandle((HANDLE)m_process);
#endif
}

void Child::push(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.push_back(line);
    if (m_lines.size() > 20000) m_lines.erase(m_lines.begin(), m_lines.begin() + 5000);
    if (!m_logPath.empty()) {
        if (std::FILE* f = std::fopen(m_logPath.c_str(), "ab")) {
            std::fprintf(f, "%s\n", line.c_str());
            std::fclose(f);
        }
    }
}

std::vector<std::string> Child::linesSince(size_t& from) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> out;
    if (from > m_lines.size()) from = 0;
    for (size_t i = from; i < m_lines.size(); ++i) out.push_back(m_lines[i]);
    from = m_lines.size();
    return out;
}

std::vector<std::string> Child::allLines() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lines;
}

bool Child::running() const { return m_running.load(); }

#if defined(_WIN32)
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    int bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++bs; continue; }
        if (c == L'"') { out.append(bs * 2 + 1, L'\\'); out += c; bs = 0; continue; }
        out.append(bs, L'\\'); bs = 0; out += c;
    }
    out.append(bs * 2, L'\\');
    return out + L"\"";
}
}  // namespace

bool Child::start(const Spec& spec, std::string* error) {
    m_logPath = spec.logPath;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { if (error) *error = "could not create a pipe"; return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = quoteArg(widen(spec.exe));
    for (auto& a : spec.args) cmd += L" " + quoteArg(widen(a));
    // Environment: ours plus the additions, as a double-NUL block.
    std::wstring envBlock;
    {
        LPWCH cur = GetEnvironmentStringsW();
        for (LPWCH p = cur; *p; p += wcslen(p) + 1) envBlock += std::wstring(p) + L'\0';
        FreeEnvironmentStringsW(cur);
        for (auto& [k, v] : spec.env) envBlock += widen(k) + L"=" + widen(v) + L'\0';
        envBlock += L'\0';
    }
    PROCESS_INFORMATION pi{};
    std::wstring cwd = widen(spec.cwd);
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                             (LPVOID)envBlock.c_str(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); if (error) *error = "could not start " + spec.exe; return false; }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (spec.memoryLimitMB > 0) {
        li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        li.ProcessMemoryLimit = (SIZE_T)spec.memoryLimitMB * 1024 * 1024;
    }
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;
    m_job = job;
    m_pipe = rd;
    m_pid = (long long)pi.dwProcessId;
    m_running = true;
    m_out = std::thread([this] { reader(0); });
    m_wait = std::thread([this] {
        WaitForSingleObject((HANDLE)m_process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess((HANDLE)m_process, &code);
        m_exit = (int)code;
        m_running = false;
    });
    return true;
}

void Child::reader(int) {
    char buf[4096];
    DWORD n = 0;
    while (ReadFile((HANDLE)m_pipe, buf, sizeof buf, &n, nullptr) && n) {
        m_partial.append(buf, n);
        size_t nl;
        while ((nl = m_partial.find('\n')) != std::string::npos) {
            std::string line = m_partial.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            push(line);
            m_partial.erase(0, nl + 1);
        }
    }
    if (!m_partial.empty()) push(m_partial);
    CloseHandle((HANDLE)m_pipe);
}

void Child::stop() {
    if (!m_running.load() || !m_process) return;
    // No console to signal, so the job is the polite and the forceful way at once.
    TerminateJobObject((HANDLE)m_job, 1);
}

void openUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void revealInFileManager(const std::string& path) {
    std::wstring arg = L"/select,\"" + widen(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}
int runQuiet(const std::string& exe, const std::vector<std::string>& args) {
    std::wstring cmd = quoteArg(widen(exe));
    for (auto& a : args) cmd += L" " + quoteArg(widen(a));
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
std::string which(const std::string& name) {
    wchar_t buf[MAX_PATH];
    std::wstring n = widen(name);
    if (SearchPathW(nullptr, n.c_str(), L".exe", MAX_PATH, buf, nullptr)) return fs::path(buf).string();
    return {};
}
bool spawnDetached(const std::string& exe, const std::vector<std::string>& args) {
    std::wstring cmd = quoteArg(widen(exe));
    for (auto& a : args) cmd += L" " + quoteArg(widen(a));
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}
bool pidAlive(long long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
long long selfPid() { return (long long)GetCurrentProcessId(); }

#else  // ---------------------------------------------------------------- POSIX

bool Child::start(const Spec& spec, std::string* error) {
    m_logPath = spec.logPath;
    int fds[2];
    if (pipe(fds) != 0) { if (error) *error = "could not create a pipe"; return false; }
    // Build argv/envp before fork: nothing that allocates may run in the child.
    std::vector<std::string> argStore{spec.exe};
    for (auto& a : spec.args) argStore.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argStore) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> envStore;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        bool overridden = false;
        for (auto& [k, v] : spec.env) if (kv.compare(0, k.size() + 1, k + "=") == 0) overridden = true;
        if (!overridden) envStore.push_back(kv);
    }
    for (auto& [k, v] : spec.env) envStore.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& e : envStore) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);
    const std::string cwd = spec.cwd;
    const int memMB = spec.memoryLimitMB;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); if (error) *error = "fork failed"; return false; }
    if (pid == 0) {
        setsid();   // its own group, so stop() can signal the game and its children together
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
        // The memory limit, where the system has one to set.
        //
        // RLIMIT_AS is not universal. macOS has the name but enforcing it
        // breaks the dynamic loader, so it was already excluded. OpenBSD does
        // not define it at all, and the launcher stopped compiling there on
        // this line -- the name is the portable-looking one, not the portable
        // one. RLIMIT_DATA is what OpenBSD bounds a process's own allocations
        // with, so it is the honest substitute; where neither exists the limit
        // is simply not applied, which is what asking for one on a system that
        // cannot impose it should do.
#if !defined(__APPLE__) && (defined(RLIMIT_AS) || defined(RLIMIT_DATA))
        if (memMB > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = (rlim_t)memMB * 1024 * 1024;
#if defined(RLIMIT_AS)
            setrlimit(RLIMIT_AS, &rl);
#else
            setrlimit(RLIMIT_DATA, &rl);
#endif
        }
#endif
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }
    close(fds[1]);
    m_fd = fds[0];
    m_pid = pid;
    m_running = true;
    m_out = std::thread([this] { reader(0); });
    m_wait = std::thread([this, pid] {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        m_exit = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        m_running = false;
    });
#if defined(__APPLE__)
    if (memMB > 0) {
        m_watch = std::thread([this, pid, memMB] {
            while (m_running.load()) {
                proc_taskinfo ti{};
                if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof ti) == (int)sizeof ti) {
                    if (ti.pti_resident_size > (uint64_t)memMB * 1024 * 1024) {
                        m_memKill = true;
                        push("[unifico] The game passed its memory limit of " + std::to_string(memMB) +
                             " MB and was stopped. Raise the limit in the installation's launch options.");
                        kill(-pid, SIGTERM);
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                        if (m_running.load()) kill(-pid, SIGKILL);
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }
#endif
    return true;
}

void Child::reader(int) {
    char buf[4096];
    ssize_t n;
    while ((n = read(m_fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        if (n <= 0) continue;
        m_partial.append(buf, (size_t)n);
        size_t nl;
        while ((nl = m_partial.find('\n')) != std::string::npos) {
            push(m_partial.substr(0, nl));
            m_partial.erase(0, nl + 1);
        }
    }
    if (!m_partial.empty()) push(m_partial);
    close(m_fd);
}

void Child::stop() {
    if (!m_running.load() || m_pid <= 0) return;
    const pid_t pid = (pid_t)m_pid;
    kill(-pid, SIGTERM);
    for (int i = 0; i < 30 && m_running.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (m_running.load()) kill(-pid, SIGKILL);
}

void openUrl(const std::string& url) {
#if defined(__APPLE__)
    spawnDetached("/usr/bin/open", {url});
#else
    spawnDetached(which("xdg-open").empty() ? "/usr/bin/xdg-open" : which("xdg-open"), {url});
#endif
}

void revealInFileManager(const std::string& path) {
#if defined(__APPLE__)
    spawnDetached("/usr/bin/open", {"-R", path});
#else
    std::error_code ec;
    const std::string dir = fs::is_directory(path, ec) ? path : fs::path(path).parent_path().string();
    openUrl(dir);
#endif
}

int runQuiet(const std::string& exe, const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); }
        std::vector<char*> argv{const_cast<char*>(exe.c_str())};
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string which(const std::string& name) {
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/usr/bin:/bin:/usr/local/bin";
    p += ":/opt/homebrew/bin:/usr/local/bin";
    size_t start = 0;
    while (start <= p.size()) {
        size_t c = p.find(':', start);
        std::string dir = p.substr(start, c == std::string::npos ? std::string::npos : c - start);
        if (!dir.empty()) {
            std::string full = dir + "/" + name;
            if (access(full.c_str(), X_OK) == 0) return full;
        }
        if (c == std::string::npos) break;
        start = c + 1;
    }
    return {};
}

bool spawnDetached(const std::string& exe, const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Double fork: the grandchild is adopted by init, so neither the
        // launcher nor a launcher that is about to exit leaves a zombie.
        if (fork() != 0) _exit(0);
        setsid();
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
        std::vector<char*> argv{const_cast<char*>(exe.c_str())};
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return true;
}

bool pidAlive(long long pid) { return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM); }
long long selfPid() { return (long long)getpid(); }
#endif
}  // namespace uproc
