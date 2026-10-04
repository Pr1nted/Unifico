#include "od/News.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Paths.h"

#include <mutex>

namespace unews {
namespace {
std::mutex g_mutex;
std::string path() { return upaths::cacheDir() + "/news.json"; }

std::vector<NewsItem> parse(const std::string& text) {
    std::vector<NewsItem> out;
    json j = ujson::parse(text);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array()) return out;
    for (auto& it : j["items"]) {
        NewsItem n;
        n.id = ujson::str(it, "id");
        n.title = ujson::str(it, "title").substr(0, 120);
        n.body = plain(ujson::str(it, "body")).substr(0, 2000);
        n.buttonLabel = ujson::str(it, "buttonLabel").substr(0, 40);
        n.buttonAction = ujson::str(it, "buttonAction");
        n.buttonParam = ujson::str(it, "buttonParam");
        n.postedAt = (long long)ujson::num(it, "postedAt");
        if (n.buttonAction != "join" && n.buttonAction != "community") n.buttonAction.clear();
        if (!n.title.empty()) out.push_back(n);
        if (out.size() >= 12) break;
    }
    return out;
}
}  // namespace

std::string plain(const std::string& s) {
    // The dialogue markup (src/dialog/DialogScript.h): effects in {braces},
    // emphasis as **bold**, __underline__ and ~~strike~~. All of it goes.
    std::string out;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{') { ++depth; continue; }
        if (c == '}' && depth) { --depth; continue; }
        if (depth) continue;
        if (i + 1 < s.size() && (c == '*' || c == '_' || c == '~') && s[i + 1] == c) { ++i; continue; }
        out += c;
    }
    return out;
}

std::vector<NewsItem> cached() {
    std::lock_guard<std::mutex> l(g_mutex);
    std::string t;
    return ufs::readFile(path(), t) ? parse(t) : std::vector<NewsItem>{};
}

bool refresh(const std::string& issuer) {
    uhttp::Response r = uhttp::get(issuer + "/announcements");
    if (!r.ok()) return false;
    std::lock_guard<std::mutex> l(g_mutex);
    return ufs::writeFileAtomic(path(), r.body);
}
}  // namespace unews
