#include "core/Log.h"

#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>

namespace ulog {
namespace {
std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::string g_path;
std::deque<std::string> g_lines;

void write(const char* level, const std::string& line) {
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
    std::string full = std::string(ts) + " " + level + " " + line;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "%s\n", full.c_str());
    if (g_file) { std::fprintf(g_file, "%s\n", full.c_str()); std::fflush(g_file); }
    g_lines.push_back(full);
    while (g_lines.size() > 600) g_lines.pop_front();
}
}  // namespace

void init(const std::string& p) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_path = p;
    g_file = std::fopen(p.c_str(), "w");
}
void info(const std::string& l) { write("INFO ", l); }
void warn(const std::string& l) { write("WARN ", l); }
void error(const std::string& l) { write("ERROR", l); }
std::vector<std::string> tail(size_t n) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    size_t start = g_lines.size() > n ? g_lines.size() - n : 0;
    for (size_t i = start; i < g_lines.size(); ++i) out.push_back(g_lines[i]);
    return out;
}
std::string path() { std::lock_guard<std::mutex> lock(g_mutex); return g_path; }
}  // namespace ulog
