#include "core/Zip.h"

#include "miniz.h"
#include "miniz_zip.h"

#include <filesystem>
#include <vector>

#if !defined(_WIN32)
#  include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace uzip {
namespace {
bool safe(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
    if (name.size() > 1 && name[1] == ':') return false;
    for (const auto& part : fs::path(name)) if (part == "..") return false;
    return true;
}
}  // namespace

bool extract(const std::string& zip, const std::string& dest, bool stripSingleRoot,
             const std::function<void(int, int)>& progress, std::string* error) {
    mz_zip_archive a{};
    if (!mz_zip_reader_init_file(&a, zip.c_str(), 0)) { if (error) *error = "not a readable zip archive"; return false; }
    const int n = (int)mz_zip_reader_get_num_files(&a);

    std::string root;
    bool single = stripSingleRoot;
    for (int i = 0; i < n && single; ++i) {
        mz_zip_archive_file_stat st{};
        mz_zip_reader_file_stat(&a, i, &st);
        std::string name = st.m_filename;
        if (name.rfind("__MACOSX/", 0) == 0) continue;
        const size_t slash = name.find('/');
        std::string top = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
        if (top.empty()) single = false;
        else if (root.empty()) root = top;
        else if (root != top) single = false;
    }
    if (!single) root.clear();

    std::error_code ec;
    fs::create_directories(dest, ec);
    int refused = 0;
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&a, i, &st)) continue;
        std::string name = st.m_filename;
        if (name.rfind("__MACOSX/", 0) == 0) continue;
        if (!root.empty()) name = name.substr(root.size());
        if (name.empty()) continue;
        if (!safe(name)) { ++refused; continue; }
        const fs::path out = fs::path(dest) / fs::u8path(name);
        if (mz_zip_reader_is_file_a_directory(&a, i)) { fs::create_directories(out, ec); continue; }
        fs::create_directories(out.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&a, i, out.string().c_str(), 0)) {
            mz_zip_reader_end(&a);
            if (error) *error = "could not write " + out.string();
            return false;
        }
#if !defined(_WIN32)
        // Zips made on Unix carry the mode in the high half of external_attr.
        const unsigned mode = (st.m_external_attr >> 16) & 0777;
        if (mode) chmod(out.string().c_str(), mode);
        // A symlink entry (macOS frameworks inside an .app) is stored as a
        // file holding its target.
        if (((st.m_external_attr >> 16) & 0170000) == 0120000) {
            std::string target;
            size_t sz = 0;
            if (void* p = mz_zip_reader_extract_to_heap(&a, i, &sz, 0)) {
                target.assign((const char*)p, sz);
                mz_free(p);
                fs::remove(out, ec);
                if (safe(target) || target.find("..") == 0) fs::create_symlink(target, out, ec);
            }
        }
#endif
        if (progress) progress(i + 1, n);
    }
    mz_zip_reader_end(&a);
    if (refused && error) *error = std::to_string(refused) + " unsafe entries were skipped";
    return true;
}

bool create(const std::string& source, const std::string& zip, std::string* error) {
    mz_zip_archive a{};
    if (!mz_zip_writer_init_file(&a, zip.c_str(), 0)) { if (error) *error = "cannot create " + zip; return false; }
    std::error_code ec;
    bool ok = true;
    const fs::path src(source);
    auto add = [&](const fs::path& file, const std::string& name) {
        if (!mz_zip_writer_add_file(&a, name.c_str(), file.string().c_str(), nullptr, 0, MZ_DEFAULT_LEVEL)) ok = false;
    };
    if (fs::is_regular_file(src, ec)) {
        add(src, src.filename().u8string());
    } else {
        for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            add(it->path(), fs::relative(it->path(), src, ec).generic_u8string());
        }
    }
    ok = mz_zip_writer_finalize_archive(&a) && ok;
    mz_zip_writer_end(&a);
    if (!ok && error) *error = "could not finish the archive";
    return ok;
}

bool readMember(const std::string& zip, const std::string& name, std::string& out) {
    mz_zip_archive a{};
    if (!mz_zip_reader_init_file(&a, zip.c_str(), 0)) return false;
    size_t sz = 0;
    void* p = mz_zip_reader_extract_file_to_heap(&a, name.c_str(), &sz, 0);
    mz_zip_reader_end(&a);
    if (!p) return false;
    out.assign((const char*)p, sz);
    mz_free(p);
    return true;
}
}  // namespace uzip
