// Account: sign in once here, and every version the launcher starts is signed in.
#include "App.h"
#include "od/Account.h"
#include "od/Achievements.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

namespace { std::string g_nick; }

void drawAccount(App& a, Rectangle r) {
    Account& acc = Account::get();
    const float x = r.x + 40, w = std::min(720.0f, r.width - 80);
    float y = r.y + 28;
    ui::heading(T("Account"), x, y, 30);
    y += 52;
    switch (acc.state()) {
        case Account::State::SignedIn: {
            ui::card({x, y, w, 150});
            ui::icon(Icon::User, x + 46, y + 50, 40, theme::gold);
            utext::draw(acc.nickname(), x + 90, y + 26, 24, theme::ink, utext::Serif);
            std::string badges;
            for (auto& b : acc.badges()) badges += (badges.empty() ? "" : ", ") + b;
            utext::draw(badges.empty() ? T("Open Doctrines account") : badges, x + 90, y + 60, 15, theme::muted);
            utext::draw(TextFormat(T("%d achievements confirmed"), uach::grantedCount()), x + 90, y + 84, 15, theme::gold);
            if (ui::button({x + 24, y + 104, 160, 34}, T("Manage in the game"), ui::Style::Ghost)) a.play();
            if (ui::button({x + w - 144, y + 104, 120, 34}, T("Sign out"), ui::Style::Danger)) {
                a.confirm(T("Sign out?"), T("The launcher forgets this session. Versions you start afterwards keep whatever session they already had."),
                          T("Sign out"), [] { Account::get().signOut(); });
            }
            y += 170;
            utext::drawWrapped(T("Every version you start from here opens already signed in to this account, and achievements you earn are confirmed for it."),
                               {x, y, w, 60}, 15, theme::muted);
            break;
        }
        case Account::State::WaitingForBrowser:
        case Account::State::Starting: {
            ui::card({x, y, w, 170});
            ui::spinner(x + 40, y + 44, 14);
            utext::draw(T("Finish signing in in your browser."), x + 70, y + 32, 18, theme::ink, utext::Semi);
            utext::drawWrapped(T("A page opened in your browser. If it did not, open this address:"), {x + 24, y + 72, w - 48, 30}, 15, theme::muted);
            utext::draw(utext::ellipsize(acc.verifyUrl(), w - 48, 13), x + 24, y + 100, 13, theme::gold);
            if (ui::button({x + 24, y + 124, 140, 34}, T("Open again"), ui::Style::Secondary, acc.verifyUrl().empty())) uproc::openUrl(acc.verifyUrl());
            if (ui::button({x + 176, y + 124, 120, 34}, T("Copy link"), ui::Style::Ghost)) SetClipboardText(acc.verifyUrl().c_str());
            if (ui::button({x + w - 124, y + 124, 100, 34}, T("Cancel"), ui::Style::Ghost)) acc.cancel();
            break;
        }
        case Account::State::NeedsNickname: {
            if (g_nick.empty()) g_nick = acc.nickname();
            ui::card({x, y, w, 170});
            utext::draw(T("Choose a nickname"), x + 24, y + 22, 20, theme::ink, utext::Semi);
            utext::draw(T("Other players see this. You can change it later."), x + 24, y + 52, 15, theme::muted);
            ui::textField({x + 24, y + 84, w - 200, 40}, g_nick, 5000);
            if (ui::button({x + w - 160, y + 86, 136, 36}, T("Create account"), ui::Style::Primary, g_nick.size() < 3)) acc.createAccount(g_nick);
            if (!acc.message().empty()) utext::draw(acc.message(), x + 24, y + 136, 14, theme::danger);
            break;
        }
        default: {
            utext::drawWrapped(T("Sign in to have achievements confirmed, to play on servers that ask for an account, and so every version you start opens signed in."),
                               {x, y, w, 60}, 16, theme::muted);
            y += 60;
            auto ps = acc.providers();
            if (ps.empty()) {
                utext::draw(acc.reachable() ? T("This account service offers no way to sign in.") : T("Reaching the account service..."), x, y, 15, theme::faint);
                if (!acc.reachable() && ui::button({x, y + 30, 120, 34}, T("Retry"), ui::Style::Ghost)) acc.bootstrap();
            }
            for (auto& p : ps) {
                if (!p.canCreate) continue;
                if (ui::button({x, y, 320, 44}, TextFormat(T("Continue with %s"), p.label.c_str()), ui::Style::Secondary)) acc.beginSignIn(p.id);
                y += 54;
            }
            if (!acc.message().empty()) utext::draw(acc.message(), x, y + 6, 15, theme::danger);
            y += 40;
            if (ui::button({x, y, 180, 34}, T("Privacy policy"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/privacy");
            if (ui::button({x + 190, y, 160, 34}, T("Terms of use"), ui::Style::Ghost)) uproc::openUrl(acc.issuer() + "/terms");
            break;
        }
    }
}
