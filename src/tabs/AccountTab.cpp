// Account: sign in with any method the game offers, and manage the account
// here exactly as the game's Account screen does -- nickname, sign-in methods,
// the account id, a data export, and deleting it.
#include "App.h"
#include "core/FileDialog.h"
#include "od/Account.h"
#include "od/Admin.h"
#include "od/Achievements.h"
#include "od/Playtime.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <algorithm>
#include <ctime>

namespace {
std::string g_nick, g_newNick;
bool g_agree = false, g_showId = false, g_renaming = false;
ui::Scroll g_scroll;

std::string labelFor(const std::vector<ProviderInfo>& ps, const std::string& id) {
    for (auto& p : ps) if (p.id == id) return p.label;
    return id;
}
}  // namespace

void drawAccount(App& a, Rectangle r) {
    Account& acc = Account::get();
    ui::beginScroll(r, g_scroll);
    const float x = r.x + 40, w = std::min(760.0f, r.width - 80);
    float y = r.y + 28 - g_scroll.y;
    ui::heading(T("Account"), x, y, 30);
    y += 52;
    const auto ps = acc.providers();

    switch (acc.state()) {
        case Account::State::SignedIn: {
            // ---- who ----
            ui::card({x, y, w, 132});
            ui::icon(Icon::User, x + 46, y + 48, 40, theme::gold);
            if (g_renaming) {
                ui::textField({x + 90, y + 20, w - 330, 40}, g_newNick, 5100);
                if (ui::button({x + w - 228, y + 22, 100, 36}, T("Save"), ui::Style::Primary, g_newNick.size() < 3 || acc.busy())) {
                    acc.changeNickname(g_newNick);
                    g_renaming = false;
                }
                if (ui::button({x + w - 120, y + 22, 96, 36}, T("Cancel"), ui::Style::Ghost)) g_renaming = false;
            } else {
                utext::draw(acc.nickname(), x + 90, y + 22, 24, theme::ink, utext::Serif);
                if (ui::button({x + w - 200, y + 22, 176, 34}, T("Change nickname"), ui::Style::Ghost)) {
                    g_renaming = true;
                    g_newNick = acc.nickname();
                }
            }
            std::string badges;
            for (auto& b : acc.badges()) badges += (badges.empty() ? "" : ", ") + b;
            std::string line = TextFormat(T("%d achievements confirmed"), uach::grantedCount());
            if (const double pt = uplay::openDoctrines(a.allDataDirs()); pt >= 60) line += std::string("  ·  ") + uplay::human(pt) + " " + T("in Open Doctrines");
            if (!badges.empty()) line += "  ·  " + badges;
            if (acc.created()) {
                char d[32];
                std::time_t t = (std::time_t)acc.created();
                std::strftime(d, sizeof d, "%Y-%m-%d", std::localtime(&t));
                line += std::string("  ·  ") + T("since") + " " + d;
            }
            utext::draw(line, x + 90, y + 62, 15, theme::muted);
            const std::string id = acc.accountId();
            utext::draw(g_showId ? id : std::string(T("Account ID hidden")), x + 90, y + 92, 14, theme::faint);
            if (ui::button({x + w - 260, y + 86, 110, 30}, g_showId ? T("Hide") : T("Show account ID"), ui::Style::Ghost)) g_showId = !g_showId;
            if (ui::button({x + w - 140, y + 86, 116, 30}, T("Copy"), ui::Style::Ghost)) { SetClipboardText(id.c_str()); a.toast(T("Account ID copied.")); }
            y += 148;
            if (!acc.message().empty()) { utext::draw(acc.message(), x, y, 15, theme::danger); y += 28; }

            // ---- standing ----
            // Every ban, timeout and restriction the account has had, lifted
            // ones included, so a player is never left guessing why a server
            // turned them away -- or whether an old timeout is still on file.
            ui::heading(T("Standing"), x, y, 20);
            y += 34;
            {
                const auto list = acc.sanctions();
                const long long now = (long long)std::time(nullptr);
                const bool any = std::any_of(list.begin(), list.end(), [now](const Sanction& s) { return s.active(now); });
                if (list.empty()) {
                    ui::icon(Icon::Check, x + 10, y + 10, 16, theme::ok);
                    utext::draw(T("In good standing. No bans, timeouts or restrictions."), x + 28, y, 15, theme::ok);
                    y += 34;
                } else {
                    utext::drawWrapped(any ? T("Something is in force on this account. It ends on its own when the time is up; if you think it is wrong, report a problem.")
                                           : T("Nothing is in force now. Past entries stay on record."),
                                       {x, y, w, 40}, 14, any ? theme::danger : theme::muted);
                    y += 30;
                    drawSanctionHistory(list, x, y, w);
                    y += 8;
                }
            }

            if (uadmin::isAdmin()) {
                if (ui::button({x, y, 220, 38}, T("Open admin tools"), ui::Style::Secondary)) a.tab = Tab::Admin;
                y += 56;
            }

            // ---- ways to sign in ----
            ui::heading(T("Ways to sign in"), x, y, 20);
            y += 34;
            const auto linked = acc.linked();
            for (auto& p : linked) {
                ui::card({x, y, w, 52}, false);
                ui::icon(Icon::Check, x + 24, y + 26, 16, theme::ok);
                utext::draw(labelFor(ps, p), x + 46, y + 15, 17, theme::ink, utext::Semi);
                const bool last = linked.size() <= 1;
                if (last) utext::draw(T("That is your only way to sign in. Link another first."), x + 220, y + 17, 13, theme::faint);
                else if (ui::button({x + w - 124, y + 9, 104, 34}, T("Remove"), ui::Style::Danger, acc.busy())) {
                    std::string prov = p;
                    a.confirm(T("Remove this sign-in method?"), labelFor(ps, p), T("Remove"), [prov] { Account::get().unlink(prov); }, true);
                }
                y += 58;
            }
            float bx = x;
            for (auto& p : ps) {
                if (std::find(linked.begin(), linked.end(), p.id) != linked.end()) continue;
                const float bw = utext::measure(TextFormat(T("Link %s"), p.label.c_str()), 16).x + 40;
                if (bx + bw > x + w) { bx = x; y += 46; }
                if (ui::button({bx, y, bw, 38}, TextFormat(T("Link %s"), p.label.c_str()), ui::Style::Secondary, acc.busy())) acc.beginLink(p.id);
                bx += bw + 10;
            }
            y += 56;

            // ---- your data ----
            ui::heading(T("Your data"), x, y, 20);
            y += 34;
            if (ui::button({x, y, 220, 38}, T("Download my data..."), ui::Style::Secondary)) {
                std::string dest = udialog::saveFile(T("Save your account data"), "opendoctrines-account.json");
                if (!dest.empty()) { acc.exportTo(dest); a.toast(T("Saving your account data...")); }
            }
            if (ui::button({x + 230, y, 160, 38}, T("Privacy policy"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/privacy");
            if (ui::button({x + 400, y, 150, 38}, T("Terms of use"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/terms");
            y += 58;

            // ---- leaving ----
            ui::heading(T("Sign out or delete"), x, y, 20);
            y += 34;
            if (ui::button({x, y, 140, 38}, T("Sign out"), ui::Style::Secondary)) {
                a.confirm(T("Sign out?"), T("The launcher forgets this session. Versions you start afterwards keep whatever session they already had."),
                          T("Sign out"), [] { Account::get().signOut(); });
            }
            if (ui::button({x + 150, y, 180, 38}, T("Delete account"), ui::Style::Danger, acc.busy())) acc.beginDelete();
            y += 56;

            // Step two of deletion: the service's own account of what goes.
            Account::DeletePlan plan;
            if (acc.deletePlan(plan) && !a.modal) {
                a.modal = [&a]() -> bool {
                    Account::DeletePlan p;
                    if (!Account::get().deletePlan(p)) return false;
                    const float mw = 600, mh = 460;
                    Rectangle m{(ui::W() - mw) / 2, (ui::H() - mh) / 2, mw, mh};
                    a.modalRect = m;
                    ui::card(m);
                    ui::heading(T("Delete your account?"), m.x + 26, m.y + 22, 24);
                    float yy = m.y + 70;
                    auto list = [&](const char* head, const std::vector<std::string>& items, Color c) {
                        if (items.empty()) return;
                        utext::draw(head, m.x + 26, yy, 15, c, utext::Semi);
                        yy += 24;
                        for (auto& it : items) yy += utext::drawWrapped("• " + it, {m.x + 36, yy, mw - 62, 0}, 14, theme::muted) + 4;
                        yy += 8;
                    };
                    list(T("Deleted"), p.willDelete, theme::danger);
                    list(T("Kept"), p.willKeep, theme::gold);
                    list(T("Out of our reach"), p.cannotReach, theme::faint);
                    bool keep = true;
                    if (ui::button({m.x + mw - 26 - 170, m.y + mh - 62, 170, 40}, T("Yes, delete it"), ui::Style::Danger)) { Account::get().confirmDelete(); keep = false; }
                    if (ui::button({m.x + mw - 26 - 170 - 12 - 170, m.y + mh - 62, 170, 40}, T("Keep my account"), ui::Style::Ghost) || ui::escapePressed()) {
                        Account::get().cancelDelete();
                        keep = false;
                    }
                    return keep;
                };
            }
            break;
        }
        case Account::State::WaitingForBrowser:
        case Account::State::Starting: {
            ui::card({x, y, w, 170});
            ui::spinner(x + 40, y + 44, 14);
            utext::draw(T("Finish signing in in your browser."), x + 70, y + 32, 18, theme::ink, utext::Semi);
            utext::drawWrapped(T("A page opened in your browser. If it did not, open this address:"), {x + 24, y + 72, w - 48, 30}, 15, theme::muted);
            utext::draw(utext::ellipsize(acc.verifyUrl(), w - 48, 13), x + 24, y + 100, 13, theme::gold);
            if (ui::button({x + 24, y + 124, 160, 34}, T("Open the page again"), ui::Style::Secondary, acc.verifyUrl().empty())) uproc::openUrl(acc.verifyUrl());
            if (ui::button({x + 194, y + 124, 120, 34}, T("Copy"), ui::Style::Ghost)) SetClipboardText(acc.verifyUrl().c_str());
            if (ui::button({x + w - 124, y + 124, 100, 34}, T("Cancel"), ui::Style::Ghost)) acc.cancel();
            y += 190;
            break;
        }
        case Account::State::NeedsNickname: {
            if (g_nick.empty()) g_nick = acc.nickname();
            ui::card({x, y, w, 220});
            utext::draw(T("Create account"), x + 24, y + 22, 20, theme::ink, utext::Semi);
            utext::draw(T("Other players see this. You can change it later."), x + 24, y + 52, 15, theme::muted);
            ui::textField({x + 24, y + 84, w - 48, 40}, g_nick, 5000);
            ui::toggle({x + 24, y + 134, w - 48, 36}, T("I agree to the terms of use and privacy policy"), &g_agree);
            if (ui::button({x + 24, y + 176, 140, 32}, T("Terms of use"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/terms");
            if (ui::button({x + 174, y + 176, 140, 32}, T("Privacy policy"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/privacy");
            if (ui::button({x + w - 184, y + 174, 160, 36}, T("Create account"), ui::Style::Primary, g_nick.size() < 3 || !g_agree)) acc.createAccount(g_nick);
            y += 236;
            if (!acc.message().empty()) { utext::draw(acc.message(), x, y, 14, theme::danger); y += 24; }
            break;
        }
        default: {
            utext::drawWrapped(T("Sign in to have achievements confirmed, to play on servers that ask for an account, and so every version you start opens signed in."),
                               {x, y, w, 60}, 16, theme::muted);
            y += 60;
            if (ps.empty()) {
                utext::draw(acc.reachable() ? T("The account service has no sign-in providers set up.") : T("Could not reach the account service."), x, y, 15, theme::faint);
                if (ui::button({x, y + 30, 140, 34}, T("Try again"), ui::Style::Ghost)) acc.bootstrap();
                y += 70;
            }
            // Every way the game signs in. Providers that can only be ADDED to
            // an existing account (the service says which) are shown, labelled,
            // and disabled until there is an account to add them to -- the
            // same rule as the game's own screen.
            for (auto& p : ps) {
                if (p.canCreate) {
                    if (ui::button({x, y, 340, 44}, TextFormat(T("Continue with %s"), p.label.c_str()), ui::Style::Secondary)) acc.beginSignIn(p.id);
                } else {
                    ui::button({x, y, 340, 44}, TextFormat(T("Continue with %s"), p.label.c_str()), ui::Style::Secondary, true);
                    utext::draw(T("Link it once you are signed in another way."), x + 356, y + 14, 14, theme::faint);
                }
                y += 54;
            }
            if (!acc.message().empty()) { utext::draw(acc.message(), x, y + 6, 15, theme::danger); y += 30; }
            y += 20;
            if (ui::button({x, y, 180, 34}, T("Privacy policy"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/privacy");
            if (ui::button({x + 190, y, 160, 34}, T("Terms of use"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/terms");
            y += 50;
            break;
        }
    }
    ui::endScroll(r, g_scroll, y + g_scroll.y - r.y + 20);
}
