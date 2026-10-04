#include "ui/Strings.h"
#include "core/Assets.h"
#include "core/Json.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#endif

namespace ustr {
namespace {
std::unordered_map<std::string, std::string> g_table;
std::string g_code = "en";

// The languages Open Doctrines ships, named in themselves. Kept in step with
// the game's src/i18n/Locale.cpp; a language the game does not have would be a
// launcher that speaks it and a game that does not.
const std::vector<LangInfo> kLangs = {
    {"en", "English"}, {"af", "Afrikaans"}, {"ar", "العربية", true}, {"az", "Azərbaycanca"},
    {"be", "Беларуская"}, {"bg", "Български"}, {"bs", "Bosanski"}, {"cs", "Čeština"},
    {"da", "Dansk"}, {"de", "Deutsch"}, {"el", "Ελληνικά"}, {"eo", "Esperanto"},
    {"es", "Español"}, {"et", "Eesti"}, {"fi", "Suomi"}, {"fr", "Français"},
    {"hi", "हिन्दी"}, {"hr", "Hrvatski"}, {"hu", "Magyar"}, {"hy", "Հայերեն"},
    {"it", "Italiano"}, {"ja", "日本語"}, {"ka", "ქართული"}, {"kk", "Қазақша"},
    {"ko", "한국어"}, {"ky", "Кыргызча"}, {"la", "Latina"}, {"lt", "Lietuvių"},
    {"lv", "Latviešu"}, {"mn", "Монгол"}, {"nb", "Norsk bokmål"}, {"nl", "Nederlands"},
    {"pl", "Polski"}, {"pt", "Português"}, {"ro", "Română"}, {"sk", "Slovenčina"},
    {"sl", "Slovenščina"}, {"sq", "Shqip"}, {"sr", "Српски"}, {"sv", "Svenska"},
    {"tr", "Türkçe"}, {"uk", "Українська"}, {"ur", "اردو", true}, {"vi", "Tiếng Việt"},
    {"zh", "中文"},
};
}  // namespace

const std::vector<LangInfo>& languages() { return kLangs; }
const std::string& current() { return g_code; }
const LangInfo& currentInfo() {
    for (auto& l : kLangs) if (l.code == g_code) return l;
    return kLangs[0];
}

std::string systemLanguage() {
    std::string code;
#if defined(_WIN32)
    wchar_t buf[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH))
        for (int i = 0; buf[i] && buf[i] != L'-'; ++i) code += (char)buf[i];
#elif defined(__APPLE__)
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    if (langs && CFArrayGetCount(langs) > 0) {
        char buf[64] = {0};
        CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, 0), buf, sizeof buf, kCFStringEncodingUTF8);
        code = buf;
    }
    if (langs) CFRelease(langs);
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) { code = v; break; }
#endif
    code = code.substr(0, code.find_first_of("-_.@"));
    for (char& c : code) c = (char)std::tolower((unsigned char)c);
    if (code == "no" || code == "nn") code = "nb";
    for (auto& l : kLangs) if (l.code == code) return code;
    return "en";
}

void setLanguage(const std::string& requested) {
    std::string code = requested.empty() ? systemLanguage() : requested;
    g_table.clear();
    g_code = "en";
    if (code == "en") return;
    const std::string* text = uassets::get("lang/" + code + ".json");
    if (!text) return;
    json j = ujson::parse(*text);
    if (!j.is_object()) return;
    for (auto& [k, v] : j.items()) if (v.is_string() && !v.get<std::string>().empty()) g_table[k] = v.get<std::string>();
    g_code = code;
}

std::vector<const std::string*> allStrings() {
    std::vector<const std::string*> out;
    for (auto& [k, v] : g_table) out.push_back(&v);
    for (auto& l : kLangs) out.push_back(&l.name);
    return out;
}
}  // namespace ustr

const char* T(const char* english) {
    if (!english) return "";
    auto it = ustr::g_table.find(english);
    return it == ustr::g_table.end() ? english : it->second.c_str();
}

#include "od/Support.h"
#include <cstdio>

std::string verdictText(const Verdict& v) {
    if (v.ok) return {};
    char buf[512];
    std::snprintf(buf, sizeof buf, T(v.why.c_str()), v.a1.c_str(), v.a2.c_str());
    return buf;
}
