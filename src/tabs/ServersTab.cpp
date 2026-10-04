// Servers: the ones an installation has joined, and a way back in.
#include "App.h"
#include "od/Account.h"
#include "od/Servers.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <ctime>

namespace { ui::Scroll g_scroll; std::string g_code; }

void drawServers(App& a, Rectangle r) {
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    ui::heading(T("Servers"), x, y, 30);
    Install inst;
    if (!installPicker(a, {x + w - 320, y, 320, 38}, inst)) return;
    y += 50;
    utext::drawWrapped(Account::get().state() == Account::State::SignedIn
                           ? TextFormat(T("Signed in as %s. Joining a server signs you in to it with this account."), Account::get().nickname().c_str())
                           : T("Sign in (bottom left) to join servers that ask for an account."),
                       {x, y, w, 40}, 15, theme::muted);
    y += 36;
    utext::draw(T("Join with an invite code"), x, y, 14, theme::faint, utext::Semi);
    ui::textField({x, y + 20, 260, 38}, g_code, 4000, "ABCD-EFGH");
    if (ui::button({x + 272, y + 21, 120, 36}, T("Join"), ui::Style::Primary, g_code.size() < 4)) {
        a.selectedTag = inst.tag;
        a.play("opendoctrines://join/" + g_code);
    }
    y += 76;
    Rectangle list{r.x, y, r.width, r.y + r.height - y};
    ui::beginScroll(list, g_scroll);
    float yy = y - g_scroll.y;
    auto servers = uservers::load(inst.dataDir);
    if (servers.empty()) utext::draw(T("No servers saved in this version yet. Ones you join in the game appear here."), x, yy, 15, theme::faint);
    for (auto& s : servers) {
        ui::card({x, yy, w, 72});
        ui::icon(Icon::Server, x + 30, yy + 36, 24, theme::gold);
        utext::draw(s.name, x + 60, yy + 12, 18, theme::ink, utext::Semi);
        std::string sub = s.lastHostName.empty() ? s.issuer : s.lastHostName + "  ·  " + s.issuer;
        if (s.lastJoined) {
            char d[32];
            std::time_t t = (std::time_t)s.lastJoined;
            std::strftime(d, sizeof d, "%Y-%m-%d", std::localtime(&t));
            sub += std::string("  ·  ") + T("last joined") + " " + d;
        }
        utext::draw(utext::ellipsize(sub, w - 260, 14), x + 60, yy + 40, 14, theme::muted);
        const std::string url = uservers::joinUrl(s);
        if (ui::button({x + w - 140, yy + 18, 120, 36}, T("Join"), ui::Style::Primary, url.empty())) { a.selectedTag = inst.tag; a.play(url); }
        if (url.empty()) utext::draw(T("needs a fresh invite code"), x + w - 330, yy + 28, 13, theme::faint);
        yy += 82;
    }
    ui::endScroll(list, g_scroll, yy + g_scroll.y - y + 20);
}
