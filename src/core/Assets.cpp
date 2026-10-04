#include "core/Assets.h"

#include "miniz.h"

#include <cstring>
#include <map>
#include <mutex>

extern "C" {
struct unifico_asset { const char* path; const unsigned char* data; size_t size; size_t raw; int deflated; };
extern const struct unifico_asset unifico_assets[];
}

namespace uassets {
namespace {
std::mutex g_mutex;
std::map<std::string, std::string> g_cache;
}

const std::string* get(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return &it->second;
    for (const unifico_asset* a = unifico_assets; a->path; ++a) {
        if (path != a->path) continue;
        std::string out;
        if (a->deflated) {
            out.resize(a->raw);
            mz_ulong len = (mz_ulong)a->raw;
            if (mz_uncompress((unsigned char*)&out[0], &len, a->data, (mz_ulong)a->size) != MZ_OK) return nullptr;
            out.resize(len);
        } else {
            out.assign((const char*)a->data, a->size);
        }
        return &(g_cache[path] = std::move(out));
    }
    return nullptr;
}

std::vector<std::string> list(const std::string& prefix) {
    std::vector<std::string> out;
    for (const unifico_asset* a = unifico_assets; a->path; ++a)
        if (std::strncmp(a->path, prefix.c_str(), prefix.size()) == 0) out.push_back(a->path);
    return out;
}
}  // namespace uassets
