#include "tools/OtherGames.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Zip.h"
#include "od/Installs.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace uother {
namespace {
std::string java() {
    if (const char* jh = std::getenv("JAVA_HOME")) {
        fs::path p = fs::path(jh) / "bin" / "java";
        std::error_code ec;
        if (fs::exists(p, ec)) return p.string();
    }
#if defined(__APPLE__)
    // /usr/bin/java on a Mac with no JDK is a stub that opens an install
    // prompt, so ask java_home whether a real one exists first.
    if (uproc::runQuiet("/usr/libexec/java_home", {}) != 0) return {};
#endif
    return uproc::which("java");
}
std::string python() {
#if defined(_WIN32)
    std::string py = uproc::which("py");
    if (!py.empty()) return py;
    return uproc::which("python");
#else
    std::string p = uproc::which("python3");
    return p.empty() ? uproc::which("python") : p;
#endif
}
std::string venvPython() {
#if defined(_WIN32)
    return dir(Game::Gd5) + "\\.venv\\Scripts\\python.exe";
#else
    return dir(Game::Gd5) + "/.venv/bin/python";
#endif
}
std::string uncivExe() {
    const std::string d = dir(Game::Unciv);
#if defined(_WIN32)
    if (ufs::exists(d + "\\Unciv.exe")) return d + "\\Unciv.exe";
#else
    if (ufs::exists(d + "/Unciv")) return d + "/Unciv";
#endif
    return {};
}
}  // namespace

const char* name(Game g) {
    switch (g) { case Game::Unciv: return "Unciv"; case Game::Gd5: return "Greater Diplomacy 5"; default: return "Greater Diplomacy 4"; }
}
const char* licence(Game g) {
    switch (g) { case Game::Unciv: return "MPL-2.0"; case Game::Gd5: return "GPL-3.0"; default: return "by via415, free on itch.io"; }
}
const char* source(Game g) {
    switch (g) {
        case Game::Unciv: return "https://github.com/yairm210/Unciv";
        case Game::Gd5: return "https://github.com/GitGetGot415/Greater-Diplomacy-5";
        default: return "https://via415.itch.io/greater-diplomacy-4";
    }
}

std::string dir(Game g) {
    return upaths::gamesDir() + (g == Game::Unciv ? "/unciv" : g == Game::Gd5 ? "/gd5" : "/gd4");
}

bool installed(Game g) {
    if (g == Game::Gd4) return true;   // nothing to install: it is a web page
    if (g == Game::Unciv) return !uncivExe().empty() || ufs::exists(dir(g) + "/Unciv.jar");
    return ufs::exists(dir(g) + "/main.py") && ufs::exists(venvPython());
}

std::string prerequisite(Game g) {
    if (g == Game::Unciv) {
#if defined(_WIN32) || (defined(__linux__) && defined(__x86_64__))
        return {};   // the bundled-Java build
#else
        return java().empty() ? "Java 11 or newer (for example from adoptium.net)" : "";
#endif
    }
    if (g == Game::Gd5) return python().empty() ? "Python 3.10 or newer (python.org)" : "";
    return {};
}

JobPtr install(Game g) {
    return ujobs::run(std::string("Installing ") + name(g), [g](Job& job) {
        const std::string d = dir(g);
        std::string err;
        if (g == Game::Unciv) {
            uhttp::Response r = uhttp::get("https://api.github.com/repos/yairm210/Unciv/releases/latest");
            json rel = ujson::parse(r.body);
            if (!r.ok() || !rel.is_object()) { job.fail("Could not read Unciv's releases."); return false; }
#if defined(_WIN32)
            const std::string want = "Unciv-Windows64.zip";
#elif defined(__linux__) && defined(__x86_64__)
            const std::string want = "Unciv-Linux64.zip";
#else
            const std::string want = "Unciv.jar";
#endif
            std::string url;
            uint64_t size = 0;
            for (auto& a : rel["assets"]) if (ujson::str(a, "name") == want) { url = ujson::str(a, "browser_download_url"); size = (uint64_t)ujson::num(a, "size"); }
            if (url.empty()) { job.fail("Unciv has no " + want + " in its latest release."); return false; }
            if (ufs::freeSpace(upaths::gamesDir()) < size * 3) { job.fail("Not enough disk space."); return false; }
            const std::string file = upaths::cacheDir() + "/" + want;
            job.setStatus("Downloading Unciv " + ujson::str(rel, "tag_name") + "...");
            if (!uhttp::download(url, file, [&](uint64_t a, uint64_t t) { job.progress = t ? 0.9f * a / t : -1.0f; }, &job.cancel, &err)) {
                job.fail(err); return false;
            }
            ufs::removeAll(d);
            fs::create_directories(d);
            if (want == "Unciv.jar") {
                std::error_code ec;
                fs::rename(file, d + "/Unciv.jar", ec);
                if (ec) fs::copy_file(file, d + "/Unciv.jar", ec);
            } else if (!uzip::extract(file, d, true, nullptr, &err)) {
                job.fail(err); return false;
            }
            ufs::removeAll(file);
            uinstalls::clearQuarantine(d);
            return true;
        }
        if (g == Game::Gd5) {
            const std::string py = python();
            if (py.empty()) { job.fail("Greater Diplomacy 5 needs Python 3.10 or newer."); return false; }
            const std::string zip = upaths::cacheDir() + "/gd5-main.zip";
            job.setStatus("Downloading Greater Diplomacy 5's source...");
            if (!uhttp::download("https://codeload.github.com/GitGetGot415/Greater-Diplomacy-5/zip/refs/heads/main", zip,
                                 [&](uint64_t a, uint64_t) { job.progress = std::min(0.4f, (float)a / 4.0e8f); }, &job.cancel, &err)) {
                job.fail(err); return false;
            }
            ufs::removeAll(d);
            if (!uzip::extract(zip, d, true, nullptr, &err)) { job.fail(err); return false; }
            ufs::removeAll(zip);
            job.setStatus("Making its Python environment...");
            job.progress = -1;
            std::vector<std::string> venvArgs = {"-m", "venv", d + "/.venv"};
#if defined(_WIN32)
            if (py.find("py.exe") != std::string::npos) venvArgs.insert(venvArgs.begin(), "-3");
#endif
            if (uproc::runQuiet(py, venvArgs) != 0) { job.fail("Python could not make a virtual environment."); return false; }
            job.setStatus("Installing its Python packages (a few minutes)...");
            if (uproc::runQuiet(venvPython(), {"-m", "pip", "install", "--disable-pip-version-check", "-r", d + "/requirements.txt"}) != 0) {
                job.fail("pip could not install Greater Diplomacy 5's requirements. See its README for help.");
                return false;
            }
            return true;
        }
        return true;
    });
}

bool uninstall(Game g, std::string* error) { return ufs::removeAll(dir(g), error); }

std::unique_ptr<uproc::Child> launch(Game g, std::string* error) {
    if (g == Game::Gd4) { uproc::openUrl(source(g)); return nullptr; }
    uproc::Spec spec;
    spec.cwd = dir(g);
    spec.logPath = upaths::logsDir() + "/" + (g == Game::Unciv ? "unciv" : "gd5") + ".log";
    if (g == Game::Unciv) {
        std::string exe = uncivExe();
        if (!exe.empty()) spec.exe = exe;
        else { spec.exe = java(); spec.args = {"-jar", dir(g) + "/Unciv.jar"}; }
        if (spec.exe.empty()) { if (error) *error = "Unciv needs Java 11 or newer."; return nullptr; }
    } else {
        spec.exe = venvPython();
        spec.args = {dir(g) + "/main.py"};
    }
    auto c = std::make_unique<uproc::Child>();
    if (!c->start(spec, error)) return nullptr;
    return c;
}

std::string mapsDir(Game g) {
    if (g == Game::Unciv) { const std::string d = dir(g) + "/maps"; std::error_code ec; fs::create_directories(d, ec); return d; }
    if (g == Game::Gd5) return dir(g) + "/base_maps";
    return {};
}
}  // namespace uother
