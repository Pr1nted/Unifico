#include "tools/Translate.h"

#include <mutex>

#if defined(UNIFICO_HAS_DRAGOMAN)
#  include "dragoman/dragoman.h"
#endif

namespace utranslate {
namespace {
std::mutex g_mutex;
std::vector<std::string> g_notes;
}

bool available() {
#if defined(UNIFICO_HAS_DRAGOMAN)
    return true;
#else
    return false;
#endif
}

std::string version() {
#if defined(UNIFICO_HAS_DRAGOMAN)
    return dg_version_string();
#else
    return "unavailable";
#endif
}

std::string detect(const std::string& path) {
#if defined(UNIFICO_HAS_DRAGOMAN)
    switch (dg_detect(path.c_str())) {
        case DG_FORMAT_ODMAP: return "Open Doctrines map";
        case DG_FORMAT_GD5: return "Greater Diplomacy 5 map";
        case DG_FORMAT_UNCIV: return "Unciv map";
        default: return {};
    }
#else
    (void)path;
    return {};
#endif
}

std::vector<std::string> lastNotes() { std::lock_guard<std::mutex> l(g_mutex); return g_notes; }

JobPtr convert(const std::string& in, const std::string& out, Target to, int cols, int rows) {
    return ujobs::run("Translating map", [=](Job& job) {
#if defined(UNIFICO_HAS_DRAGOMAN)
        job.progress = -1;
        dg_options opts;
        dg_options_defaults(&opts);
        dg_report* rep = nullptr;
        int rc;
        if (to == Target::Unciv) rc = dg_convert_unciv(in.c_str(), out.c_str(), cols, rows, &opts, &rep);
        else rc = dg_convert(in.c_str(), out.c_str(), to == Target::Gd5 ? DG_FORMAT_GD5 : DG_FORMAT_ODMAP, &opts, &rep);
        std::vector<std::string> notes;
        if (rep) {
            for (int i = 0; i < dg_report_count(rep); ++i) notes.push_back(dg_report_message(rep, i));
            dg_report_free(rep);
        }
        {
            std::lock_guard<std::mutex> l(g_mutex);
            g_notes = notes;
        }
        if (rc != 0) {
            const char* e = dg_last_error();
            job.fail(e && *e ? e : "The map could not be translated.");
            return false;
        }
        job.setStatus("Written to " + out);
        return true;
#else
        (void)in; (void)out; (void)to; (void)cols; (void)rows;
        job.fail("This build has no map translator.");
        return false;
#endif
    });
}
}  // namespace utranslate
