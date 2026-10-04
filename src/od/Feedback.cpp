#include "od/Feedback.h"
#include "BuildInfo.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "od/Account.h"
#include "ui/Strings.h"

#include <random>

namespace ufeedback {
const char* const kCategories[8] = {"ui", "security", "ai", "data", "multiplayer", "scripting", "mods", "other"};

const char* categoryLabel(int i) {
    static const char* labels[8] = {N_("Interface"), N_("Security"), N_("AI"), N_("Game data"), N_("Multiplayer"),
                                    N_("Scripting"), N_("Mods"), N_("Other")};
    return labels[i & 7];
}

// The service caps reports per install per day. A random id kept for that
// purpose alone -- not the analytics id, which exists only with consent.
static std::string installId() {
    Settings& s = Settings::get();
    if (s.feedbackInstall.empty()) {
        std::random_device rd;
        const char* hex = "0123456789abcdef";
        for (int i = 0; i < 32; ++i) s.feedbackInstall += hex[rd() & 15];
        s.save();
    }
    return s.feedbackInstall;
}

JobPtr send(bool bug, int category, const std::string& title, const std::string& body, const std::string& diagnostics) {
    const std::string install = installId();
    return ujobs::run("feedback", [=](Job& job) {
        json r = {{"kind", bug ? "bug" : "suggestion"}, {"category", kCategories[category & 7]},
                  {"title", title.substr(0, 140)}, {"body", body.substr(0, 4000)},
                  {"version", std::string("unifico-") + UNIFICO_VERSION}, {"platform", upaths::platformTag()},
                  {"install", install}};
        if (!diagnostics.empty()) r["diagnostics"] = diagnostics.size() > 24000 ? diagnostics.substr(diagnostics.size() - 24000) : diagnostics;
        uhttp::Response res = uhttp::postJson(Account::get().issuer() + "/feedback", r.dump(), Account::get().token());
        json j = ujson::parse(res.body);
        if (!res.ok()) {
            job.fail(ujson::str(j, "message", res.error.empty() ? "The report could not be sent." : res.error));
            return false;
        }
        // Security reports go to a private channel, never a public issue.
        job.setStatus(ujson::flag(j, "private") ? "Sent privately to the maintainers. Thank you."
                    : ujson::flag(j, "duplicate") ? "Already received. Thank you."
                    : "Sent. Thank you.");
        return true;
    });
}
}  // namespace ufeedback
