#include "core/FileDialog.h"
#include "core/Process.h"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <commdlg.h>
#  include <shobjidl.h>
#endif

namespace udialog {
namespace {
#if !defined(_WIN32)
std::string capture(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        while (fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}
std::string shq(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}
std::string asq(const std::string& s) {   // AppleScript string
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
#endif
}  // namespace

std::string openFile(const std::string& title, const std::string& ext) {
#if defined(__APPLE__)
    std::string script = "POSIX path of (choose file with prompt " + asq(title) + ")";
    return capture("osascript -e " + shq(script) + " 2>/dev/null");
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH * 4] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    std::wstring filter;
    if (!ext.empty()) {
        std::wstring we(ext.begin(), ext.end());
        filter = L"*." + we + std::wstring(1, L'\0') + L"*." + we + std::wstring(1, L'\0');
    }
    filter += std::wstring(L"All files") + L'\0' + L"*.*" + L'\0' + L'\0';
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    std::wstring wt(title.begin(), title.end());
    ofn.lpstrTitle = wt.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &out[0], n, nullptr, nullptr);
    return out;
#else
    (void)ext;
    if (!uproc::which("zenity").empty()) return capture("zenity --file-selection --title=" + shq(title) + " 2>/dev/null");
    if (!uproc::which("kdialog").empty()) return capture("kdialog --getopenfilename ~ 2>/dev/null");
    return {};
#endif
}

std::string openFolder(const std::string& title) {
#if defined(__APPLE__)
    std::string script = "POSIX path of (choose folder with prompt " + asq(title) + ")";
    return capture("osascript -e " + shq(script) + " 2>/dev/null");
#elif defined(_WIN32)
    std::string result;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* d = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&d))) {
        DWORD opts = 0;
        d->GetOptions(&opts);
        d->SetOptions(opts | FOS_PICKFOLDERS);
        if (SUCCEEDED(d->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(d->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
                    result.assign(n > 0 ? n - 1 : 0, 0);
                    WideCharToMultiByte(CP_UTF8, 0, p, -1, &result[0], n, nullptr, nullptr);
                    CoTaskMemFree(p);
                }
                item->Release();
            }
        }
        d->Release();
    }
    return result;
#else
    if (!uproc::which("zenity").empty()) return capture("zenity --file-selection --directory --title=" + shq(title) + " 2>/dev/null");
    if (!uproc::which("kdialog").empty()) return capture("kdialog --getexistingdirectory ~ 2>/dev/null");
    return {};
#endif
}

std::string saveFile(const std::string& title, const std::string& defaultName) {
#if defined(__APPLE__)
    std::string script = "POSIX path of (choose file name with prompt " + asq(title) + " default name " + asq(defaultName) + ")";
    return capture("osascript -e " + shq(script) + " 2>/dev/null");
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH * 4] = {0};
    std::wstring dn(defaultName.begin(), defaultName.end());
    wcsncpy(buf, dn.c_str(), MAX_PATH * 4 - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    std::wstring wt(title.begin(), title.end());
    ofn.lpstrTitle = wt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, 0);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &out[0], n, nullptr, nullptr);
    return out;
#else
    if (!uproc::which("zenity").empty())
        return capture("zenity --file-selection --save --confirm-overwrite --filename=" + shq(defaultName) + " --title=" + shq(title) + " 2>/dev/null");
    if (!uproc::which("kdialog").empty()) return capture("kdialog --getsavefilename ~/" + shq(defaultName) + " 2>/dev/null");
    return {};
#endif
}
}  // namespace udialog
