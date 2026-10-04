#include "ui/Text.h"
#include "ui/Arabic.h"
#include "ui/Strings.h"
#include "core/Assets.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

namespace utext {
namespace {

// Atlases are rasterised at these pixel sizes (times the display scale) and
// the nearest larger one is used for any requested size.
// Rasterised at about twice the drawn size and mipmapped: advances are integers
// in raylib's glyph table, and at 14 px a space rounds to 2 px, which ran words
// together. Twice the size halves every rounding error.
const int kBuckets[] = {20, 28, 36, 48, 64, 84, 110, 144, 190};

struct Atlas {
    Font font{};
    bool ok = false;
};

struct FaceData {
    const std::string* ttf = nullptr;   // embedded bytes
    std::unordered_set<int> has;        // codepoints this face can draw
    std::map<int, Atlas> atlases;       // bucket px -> atlas
};

FaceData g_faces[3];
std::string g_fallbackTtf;              // bytes of the fallback face, if any
std::unordered_set<int> g_fallbackHas;
std::map<int, Atlas> g_fallbackAtlases;
std::vector<int> g_fallbackCps;         // what the fallback atlas holds
std::set<int> g_missing;                // codepoints seen but not in any atlas yet
bool g_dirty = false;
float g_dpi = 1.0f;
float g_uiScale = 1.0f;   // the interface zoom (ui::scale), so atlases match pixels on screen

std::vector<int> decode(const std::string& s) {
    std::vector<int> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        int bytes = 0;
        int cp = GetCodepointNext(s.c_str() + i, &bytes);
        if (bytes <= 0) bytes = 1;
        out.push_back(cp);
        i += (size_t)bytes;
    }
    return out;
}

std::vector<int> baseSet() {
    std::vector<int> cps;
    for (int c = 0x20; c <= 0x24F; ++c) cps.push_back(c);
    for (int c = 0x370; c <= 0x3FF; ++c) cps.push_back(c);
    for (int c = 0x400; c <= 0x52F; ++c) cps.push_back(c);
    for (int c = 0x1E00; c <= 0x1EFF; ++c) cps.push_back(c);
    for (int c = 0x2000; c <= 0x206F; ++c) cps.push_back(c);
    for (int c : {0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2713, 0x2715, 0x25B6, 0x25BC, 0x25B2, 0x2022, 0x00B7})
        cps.push_back(c);
    return cps;
}

// Which codepoints a TrueType file actually maps, via raylib's stb_truetype.
std::unordered_set<int> coverage(const std::string& ttf, const std::vector<int>& cps) {
    std::unordered_set<int> out;
    const int count = (int)cps.size();
    GlyphInfo* g = LoadFontData((const unsigned char*)ttf.data(), (int)ttf.size(), 8,
                                const_cast<int*>(cps.data()), count, FONT_DEFAULT);
    // LoadFontData gives every requested codepoint an entry; a missing glyph
    // has an empty image and no advance. That is the test for "this face has it".
    for (int i = 0; g && i < count; ++i)
        if (g[i].advanceX > 0 || g[i].image.width > 0 || cps[i] == 0x20) out.insert(g[i].value);
    if (g) UnloadFontData(g, count);
    return out;
}

Atlas buildAtlas(const std::string& ttf, const std::vector<int>& cps, int px) {
    Atlas a;
    if (cps.empty()) return a;
    a.font = LoadFontFromMemory(".ttf", (const unsigned char*)ttf.data(), (int)ttf.size(), px,
                                const_cast<int*>(cps.data()), (int)cps.size());
    a.ok = a.font.texture.id != 0;
    if (a.ok) {
        GenTextureMipmaps(&a.font.texture);
        SetTextureFilter(a.font.texture, TEXTURE_FILTER_TRILINEAR);
    }
    return a;
}

int bucketFor(float size) {
    // The pixels this text will actually cover: logical size, times the
    // interface zoom, times the display's density -- and a little over, since
    // advances in raylib's glyph table are whole pixels at the atlas size.
    const float want = size * g_uiScale * g_dpi * 1.5f;
    for (int b : kBuckets) if (b >= want - 0.5f) return b;
    return kBuckets[sizeof kBuckets / sizeof kBuckets[0] - 1];
}

Atlas& primaryAtlas(Face f, int px) {
    FaceData& fd = g_faces[f];
    auto it = fd.atlases.find(px);
    if (it != fd.atlases.end()) return it->second;
    std::vector<int> cps(fd.has.begin(), fd.has.end());
    std::sort(cps.begin(), cps.end());
    return fd.atlases[px] = buildAtlas(*fd.ttf, cps, px);
}

Atlas& fallbackAtlas(int px) {
    auto it = g_fallbackAtlases.find(px);
    if (it != g_fallbackAtlases.end()) return it->second;
    return g_fallbackAtlases[px] = buildAtlas(g_fallbackTtf, g_fallbackCps, px);
}

// Candidate fallback fonts, broadest first. Collections (.ttc) are fine:
// stb_truetype reads the first face.
std::vector<std::string> systemFallbacks() {
#if defined(__APPLE__)
    return {"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/Library/Fonts/Arial Unicode.ttf",
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/System/Library/Fonts/AppleSDGothicNeo.ttc"};
#elif defined(_WIN32)
    const std::string w = std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:\\Windows";
    const std::string lang = ustr::current();
    std::vector<std::string> v;
    if (lang == "ja") v.push_back(w + "\\Fonts\\YuGothM.ttc"), v.push_back(w + "\\Fonts\\meiryo.ttc");
    if (lang == "ko") v.push_back(w + "\\Fonts\\malgun.ttf");
    if (lang == "zh") v.push_back(w + "\\Fonts\\msyh.ttc"), v.push_back(w + "\\Fonts\\simsun.ttc");
    if (lang == "hi") v.push_back(w + "\\Fonts\\Nirmala.ttf");
    for (const char* f : {"\\Fonts\\seguisym.ttf", "\\Fonts\\segoeui.ttf", "\\Fonts\\arialuni.ttf", "\\Fonts\\arial.ttf"})
        v.push_back(w + f);
    return v;
#else
    return {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/local/share/fonts/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/local/share/fonts/dejavu/DejaVuSans.ttf"};
#endif
}

// Downloaded once, into the cache, when nothing on the system covers the
// language. Google Fonts' repository; every one is under the SIL OFL.
std::string downloadableFallback(const std::string& lang) {
    const char* file = nullptr;
    if (lang == "zh") file = "ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf";
    else if (lang == "ja") file = "ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf";
    else if (lang == "ko") file = "ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf";
    else if (lang == "ar" || lang == "ur") file = "ofl/notonaskharabic/NotoNaskhArabic%5Bwght%5D.ttf";
    else if (lang == "hi") file = "ofl/notosansdevanagari/NotoSansDevanagari%5Bwdth,wght%5D.ttf";
    else if (lang == "hy") file = "ofl/notosansarmenian/NotoSansArmenian%5Bwdth,wght%5D.ttf";
    else if (lang == "ka") file = "ofl/notosansgeorgian/NotoSansGeorgian%5Bwdth,wght%5D.ttf";
    if (!file) return {};
    const std::string dest = upaths::cacheDir() + "/font-" + lang + ".ttf";
    if (ufs::exists(dest)) return dest;
    std::string err;
    ulog::info("downloading a font for " + lang);
    if (uhttp::download(std::string("https://github.com/google/fonts/raw/main/") + file, dest, nullptr, nullptr, &err))
        return dest;
    ulog::warn("font download failed: " + err);
    return {};
}

void loadFallbackFor(const std::vector<int>& need) {
    g_fallbackTtf.clear();
    g_fallbackHas.clear();
    if (need.empty()) return;
    auto covers = [&](const std::string& ttf) {
        auto has = coverage(ttf, need);
        return std::make_pair(has.size(), has);
    };
    size_t best = 0;
    for (const auto& path : systemFallbacks()) {
        std::string ttf;
        if (!ufs::readFile(path, ttf)) continue;
        auto [n, has] = covers(ttf);
        if (n > best) { best = n; g_fallbackTtf = std::move(ttf); g_fallbackHas = std::move(has); }
        if (best == need.size()) break;
    }
    if (best < need.size() / 2) {
        const std::string dl = downloadableFallback(ustr::current());
        std::string ttf;
        if (!dl.empty() && ufs::readFile(dl, ttf)) {
            auto [n, has] = covers(ttf);
            if (n > best) { g_fallbackTtf = std::move(ttf); g_fallbackHas = std::move(has); }
        }
    }
}

void clearAtlases() {
    for (auto& fd : g_faces) {
        for (auto& [px, a] : fd.atlases) if (a.ok) UnloadFont(a.font);
        fd.atlases.clear();
    }
    for (auto& [px, a] : g_fallbackAtlases) if (a.ok) UnloadFont(a.font);
    g_fallbackAtlases.clear();
}

bool needsShaping(const std::vector<int>& cps) {
    for (int c : cps) if (odText::isArabic((unsigned)c)) return true;
    return false;
}

std::vector<int> shaped(const std::string& s) {
    std::vector<int> cps = decode(s);
    if (!needsShaping(cps)) return cps;
    std::vector<unsigned> u(cps.begin(), cps.end());
    u = odText::reorderForDisplay(odText::shapeArabic(u));
    return std::vector<int>(u.begin(), u.end());
}

struct Pick { Font* font; float scale; };

Pick pick(int cp, Face f, float size) {
    const int px = bucketFor(size);
    if (g_faces[f].has.count(cp)) {
        Atlas& a = primaryAtlas(f, px);
        if (a.ok) return {&a.font, size / (float)a.font.baseSize};
    }
    if (g_fallbackHas.count(cp)) {
        if (std::find(g_fallbackCps.begin(), g_fallbackCps.end(), cp) != g_fallbackCps.end()) {
            Atlas& a = fallbackAtlas(px);
            if (a.ok) return {&a.font, size / (float)a.font.baseSize};
        } else {
            g_missing.insert(cp);
        }
    } else if (cp > 0x24F && !g_faces[f].has.count(cp)) {
        g_missing.insert(cp);
    }
    Atlas& a = primaryAtlas(f, px);
    return {a.ok ? &a.font : nullptr, a.ok ? size / (float)a.font.baseSize : 1.0f};
}
}  // namespace

void init() {
    const char* files[3] = {"fonts/ss3-400.ttf", "fonts/ss3-600.ttf", "fonts/spectral-600.ttf"};
    const std::vector<int> base = baseSet();
    for (int i = 0; i < 3; ++i) {
        g_faces[i].ttf = uassets::get(files[i]);
        if (g_faces[i].ttf) g_faces[i].has = coverage(*g_faces[i].ttf, base);
    }
    g_dpi = std::max(1.0f, GetWindowScaleDPI().x);
    rebuild();
}

void rebuild() {
    clearAtlases();
    // Every codepoint the language uses that the embedded faces cannot draw.
    std::set<int> need;
    for (const std::string* s : ustr::allStrings())
        for (int cp : shaped(*s))
            if (!g_faces[Sans].has.count(cp) && cp >= 0x20) need.insert(cp);
    for (int cp : g_missing) need.insert(cp);
    g_missing.clear();
    std::vector<int> v(need.begin(), need.end());
    loadFallbackFor(v);
    g_fallbackCps.clear();
    for (int cp : v) if (g_fallbackHas.count(cp)) g_fallbackCps.push_back(cp);
    g_dirty = false;
}

void frame() {
    // A world name in Japanese, typed into an English launcher: its glyphs
    // were not in any atlas. They were noted when first drawn; rebuild once.
    if (!g_missing.empty()) {
        bool anyNew = false;
        for (int cp : g_missing) if (std::find(g_fallbackCps.begin(), g_fallbackCps.end(), cp) == g_fallbackCps.end()) anyNew = true;
        if (anyNew) rebuild();
        else g_missing.clear();
    }
}

void shutdown() { clearAtlases(); }

void setScale(float s) {
    // A new zoom needs atlases at new sizes; the old ones are dropped so a
    // window dragged through a dozen sizes does not keep a dozen of each.
    if (std::fabs(s - g_uiScale) < 0.01f) return;
    g_uiScale = s;
    clearAtlases();
}

// Captions below 15 units are nudged up towards it: at the default window size
// 13 px slate-on-black was the most-reported thing about the launcher. One
// place, so measuring and drawing can never disagree about it.
static float readable(float size) { return size < 15.0f ? size + (15.0f - size) * 0.6f : size; }

Vector2 measure(const std::string& s, float size, Face f) {
    size = readable(size);
    float w = 0;
    for (int cp : shaped(s)) {
        Pick p = pick(cp, f, size);
        if (!p.font) continue;
        int gi = GetGlyphIndex(*p.font, cp);
        const GlyphInfo& g = p.font->glyphs[gi];
        float adv = g.advanceX ? (float)g.advanceX : p.font->recs[gi].width;
        w += adv * p.scale;
    }
    return {w, size};
}

float draw(const std::string& s, float x, float y, float size, Color c, Face f) {
    size = readable(size);
    float pen = x;
    for (int cp : shaped(s)) {
        Pick p = pick(cp, f, size);
        if (!p.font) continue;
        int gi = GetGlyphIndex(*p.font, cp);
        const GlyphInfo& g = p.font->glyphs[gi];
        if (cp != ' ' && cp != '\t') DrawTextCodepoint(*p.font, cp, {std::round(pen), std::round(y)}, size, c);
        float adv = g.advanceX ? (float)g.advanceX : p.font->recs[gi].width;
        pen += adv * p.scale;
    }
    return pen - x;
}

float drawWrapped(const std::string& s, Rectangle box, float size, Color c, Face f, bool doDraw, float lineGap) {
    float y = box.y;
    const float lh = size * lineGap;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // Words, or characters for scripts without spaces (CJK).
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i + 1);
            std::string trial = line + word;
            if (!line.empty() && measure(trial, size, f).x > box.width) {
                if (doDraw) draw(line, box.x, y, size, c, f);
                y += lh;
                line = word;
            } else if (line.empty() && measure(word, size, f).x > box.width) {
                // One unbreakable run: split by codepoint.
                std::string chunk;
                for (size_t k = 0; k < word.size();) {
                    int b = 0;
                    GetCodepointNext(word.c_str() + k, &b);
                    if (b <= 0) b = 1;
                    std::string next = chunk + word.substr(k, (size_t)b);
                    if (!chunk.empty() && measure(next, size, f).x > box.width) {
                        if (doDraw) draw(chunk, box.x, y, size, c, f);
                        y += lh;
                        chunk = word.substr(k, (size_t)b);
                    } else {
                        chunk = next;
                    }
                    k += (size_t)b;
                }
                line = chunk;
            } else {
                line = trial;
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        if (doDraw && !line.empty()) draw(line, box.x, y, size, c, f);
        y += lh;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return y - box.y;
}

std::string ellipsize(const std::string& s, float width, float size, Face f) {
    if (measure(s, size, f).x <= width) return s;
    std::string out = s;
    while (!out.empty()) {
        // Remove one whole codepoint from the end.
        size_t k = out.size() - 1;
        while (k > 0 && ((unsigned char)out[k] & 0xC0) == 0x80) --k;
        out.erase(k);
        if (measure(out + "…", size, f).x <= width) return out + "…";
    }
    return "…";
}
}  // namespace utext
