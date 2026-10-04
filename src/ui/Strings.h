#pragma once
// Translation. English is the key, as in Open Doctrines: T("Play") looks the
// sentence up in lang/<code>.json (embedded) and returns it, or the English.
//
// The table also carries every achievement name and description, copied from
// the game's own translations by tools/extract_strings.py, so the launcher's
// collection reads exactly as the game's does.
#include <string>
#include <vector>

struct LangInfo {
    std::string code;     // "de"
    std::string name;     // "Deutsch" -- in its own language
    bool rtl = false;
};

namespace ustr {
const std::vector<LangInfo>& languages();
/** Load a language; "" picks the system's, falling back to English. */
void setLanguage(const std::string& code);
const std::string& current();
const LangInfo& currentInfo();
/** Every translated string of the current language, for building the font atlas. */
std::vector<const std::string*> allStrings();
std::string systemLanguage();
}  // namespace ustr

/** The translation of an English UI string. Valid until the language changes. */
const char* T(const char* english);

/** Marks an English string for the extractor without translating it here:
 *  for labels made on a worker thread and translated where they are drawn. */
#define N_(s) s
