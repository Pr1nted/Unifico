// Admin: the news board, the report queue and account lookup, for an account
// with the developer badge. The same three things the game's Admin screen does,
// against the same routes, with the same session token. See od/Admin.h.
#include "App.h"
#include "od/Admin.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <algorithm>
#include <ctime>

namespace {
int g_section = 0;                 // 0 news, 1 reports, 2 people
bool g_fetched = false, g_showClosed = false;
ui::Scroll g_scroll;

// The composer.
uadmin::NewsEntry g_draft;
std::string g_expires;
int g_action = 0;                  // none, join, community, account
const char* kActions[] = {"", "join", "community", "account"};

// Shared by the queue and the lookup: how long, and why.
std::string g_length = "7d", g_reason, g_query;

std::string date(long long t) {
    if (t <= 0) return "";
    char d[32];
    std::time_t tt = (std::time_t)t;
    std::strftime(d, sizeof d, "%Y-%m-%d %H:%M", std::localtime(&tt));
    return d;
}

const char* kindLabel(const std::string& k) {
    return k == "ban" ? T("Ban") : k == "timeout" ? T("Timeout") : T("Mod publishing restricted");
}

const char* reasonLabel(const std::string& r) {
    if (r == "harassment") return T("Harassment");
    if (r == "hate") return T("Hate speech");
    if (r == "threats") return T("Threats");
    if (r == "spam") return T("Spam");
    if (r == "cheating") return T("Cheating");
    if (r == "sexual") return T("Sexual content");
    return T("Other");
}

/** "Time out for [7d]  Reason [....]" -- the shared controls above a list. */
float sanctionControls(float x, float y, float w) {
    utext::draw(T("Timeout length"), x, y + 10, 14, theme::muted);
    ui::textField({x + 120, y, 110, 36}, g_length, 7301, "7d");
    utext::draw(T("Reason"), x + 250, y + 10, 14, theme::muted);
    ui::textField({x + 310, y, w - 310, 36}, g_reason, 7302, T("Conduct towards other players."));
    const double d = uadmin::parseDays(g_length);
    utext::draw(d > 0 ? TextFormat(T("Timeouts last %s."), uadmin::describeUntil((long long)(d * 86400), 0).c_str())
                      : T("Say how long, e.g. 3d, 12h or 2w."),
                x, y + 44, 13, d > 0 ? theme::faint : theme::danger);
    return y + 70;
}

void sanctionList(const std::vector<Sanction>& list, float x, float& y, float w) {
    const long long now = (long long)std::time(nullptr);
    for (auto& s : list) {
        const bool on = s.active(now);
        ui::card({x, y, w, 64}, false);
        DrawRectangle((int)x, (int)y, 3, 64, on ? theme::danger : theme::ruleFirm);
        utext::draw(kindLabel(s.kind), x + 18, y + 10, 16, on ? theme::ink : theme::muted, utext::Semi);
        std::string when = date(s.at);
        if (s.liftedAt) when += std::string("  ·  ") + T("lifted") + " " + date(s.liftedAt);
        else if (on) when += std::string("  ·  ") + (s.until ? TextFormat(T("ends in %s"), uadmin::describeUntil(s.until, now).c_str())
                                                             : T("permanent"));
        else when += std::string("  ·  ") + T("ended") + " " + date(s.until);
        utext::draw(when, x + 18, y + 36, 13, theme::faint);
        const float lw = utext::measure(kindLabel(s.kind), 16, utext::Semi).x;
        utext::draw(utext::ellipsize(s.reason, w - lw - 60, 14), x + lw + 34, y + 12, 14, theme::muted);
        y += 70;
    }
}

void drawNews(App& a, float x, float& y, float w) {
    // ---- composer ----
    const auto list = uadmin::news();
    ui::card({x, y, w, 430});
    utext::draw(g_draft.id.empty() || std::none_of(list.begin(), list.end(),
                                                   [](const uadmin::NewsEntry& n) { return n.id == g_draft.id; })
                    ? T("Write an announcement") : T("Edit announcement"),
                x + 24, y + 20, 19, theme::ink, utext::Semi);
    if (ui::button({x + w - 124, y + 16, 100, 32}, T("New"), ui::Style::Ghost)) { g_draft = {}; g_expires.clear(); g_action = 0; }
    float yy = y + 62;
    const float lx = x + 24, fx = x + 150, fw = w - 174;
    utext::draw(T("Id"), lx, yy + 10, 14, theme::muted);
    ui::textField({fx, yy, fw, 36}, g_draft.id, 7101, T("e.g. tournament-may"));
    yy += 46;
    utext::draw(T("Title"), lx, yy + 10, 14, theme::muted);
    ui::textField({fx, yy, fw, 36}, g_draft.title, 7102);
    yy += 46;
    utext::draw(T("Text"), lx, yy + 10, 14, theme::muted);
    ui::textArea({fx, yy, fw, 110}, g_draft.body, 7103);
    yy += 120;
    utext::draw(T("Button"), lx, yy + 10, 14, theme::muted);
    g_action = ui::dropdown({fx, yy, 200, 36}, {T("No button"), T("Join a game"), T("Community"), T("Account")}, g_action, 7104);
    if (g_action > 0) {
        ui::textField({fx + 210, yy, 180, 36}, g_draft.buttonLabel, 7105, T("Label"));
        if (g_action == 1) ui::textField({fx + 400, yy, std::max(120.0f, fw - 400), 36}, g_draft.buttonParam, 7106, T("Invite code"));
    }
    yy += 46;
    utext::draw(T("Expires in"), lx, yy + 10, 14, theme::muted);
    ui::textField({fx, yy, 200, 36}, g_expires, 7107, T("never"));
    utext::draw(T("e.g. 3d or 12h; empty keeps it up"), fx + 214, yy + 10, 13, theme::faint);
    yy += 50;
    const double days = uadmin::parseDays(g_expires);
    const bool bad = g_draft.id.empty() || (g_draft.title.empty() && g_draft.body.empty()) ||
                     (g_action > 0 && g_draft.buttonLabel.empty()) || (!g_expires.empty() && days <= 0);
    if (ui::button({x + w - 184, yy - 6, 160, 38}, T("Post"), ui::Style::Primary, bad || uadmin::busy())) {
        uadmin::NewsEntry e = g_draft;
        e.buttonAction = kActions[g_action];
        if (g_action != 1) e.buttonParam.clear();
        e.until = days > 0 ? (long long)std::time(nullptr) + (long long)(days * 86400) : 0;
        uadmin::saveNews(e);
    }
    if (bad) utext::draw(g_draft.id.empty() ? T("An announcement needs an id.")
                         : g_draft.title.empty() && g_draft.body.empty() ? T("An announcement needs a title or a body.")
                         : g_action > 0 && g_draft.buttonLabel.empty() ? T("A button needs a label.")
                         : T("Say when, e.g. 3d or 6h 30m."), lx, yy + 4, 13, theme::faint);
    y += 446;

    // ---- the board ----
    ui::heading(T("On the board"), x, y, 20);
    y += 34;
    if (list.empty()) { utext::draw(T("Nothing is posted."), x, y, 15, theme::faint); y += 30; }
    for (auto& n : list) {
        ui::card({x, y, w, 96}, false);
        utext::draw(utext::ellipsize(n.title.empty() ? n.id : n.title, w - 360, 17), x + 20, y + 14, 17,
                    n.hidden ? theme::faint : theme::ink, utext::Semi);
        std::string meta = n.id;
        if (n.postedAt) meta += "  ·  " + date(n.postedAt);
        if (n.until) meta += std::string("  ·  ") + T("until") + " " + date(n.until);
        if (n.hidden) meta += std::string("  ·  ") + T("hidden");
        utext::draw(utext::ellipsize(meta, w - 360, 13), x + 20, y + 40, 13, n.hidden ? theme::danger : theme::faint);
        utext::draw(utext::ellipsize(n.body, w - 40, 14), x + 20, y + 64, 14, theme::muted);
        float bx = x + w - 20;
        auto btn = [&](const char* label, ui::Style s) { const float bw = utext::measure(label, 15).x + 30; bx -= bw; bool r = ui::button({bx, y + 12, bw, 32}, label, s, uadmin::busy()); bx -= 8; return r; };
        const std::string id = n.id;
        if (btn(T("Delete"), ui::Style::Danger))
            a.confirm(T("Delete this announcement?"), n.title.empty() ? n.id : n.title, T("Delete"), [id] { uadmin::newsOp("purge", id); }, true);
        if (btn(n.hidden ? T("Show") : T("Hide"), ui::Style::Ghost)) uadmin::newsOp(n.hidden ? "show" : "hide", id);
        if (btn(T("Edit"), ui::Style::Secondary)) {
            g_draft = n;
            g_action = n.buttonAction == "join" ? 1 : n.buttonAction == "community" ? 2 : n.buttonAction == "account" ? 3 : 0;
            g_expires.clear();
            g_scroll.target = 0;
        }
        y += 104;
    }
}

void drawReports(App& a, float x, float& y, float w) {
    y = sanctionControls(x, y, w);
    const auto all = uadmin::reports();
    int open = 0;
    for (auto& r : all) open += r.status == "open";
    ui::toggle({x, y, 320, 32}, TextFormat(T("Show closed reports (%d open)"), open), &g_showClosed);
    y += 44;
    const double days = uadmin::parseDays(g_length);
    int shown = 0;
    for (auto& r : all) {
        if (!g_showClosed && r.status != "open") continue;
        ++shown;
        const float lines = (float)std::min<size_t>(r.context.size(), 4);
        const float h = 150 + lines * 20;
        ui::card({x, y, w, h}, r.status == "open");
        utext::draw(r.accusedNick, x + 20, y + 14, 18, theme::ink, utext::Semi);
        const float nw = utext::measure(r.accusedNick, 18, utext::Semi).x;
        utext::draw(std::string(reasonLabel(r.reason)) + "  ·  " + date(r.at) + "  ·  " + T("reported by") + " " + r.reporterNick,
                    x + 32 + nw, y + 17, 14, theme::faint);
        float yy = y + 46;
        if (!r.message.empty()) { utext::draw(utext::ellipsize("\"" + r.message + "\"", w - 40, 15), x + 20, yy, 15, theme::ink); yy += 24; }
        for (size_t i = 0; i < r.context.size() && i < 4; ++i) {
            utext::draw(utext::ellipsize(r.context[i], w - 60, 13), x + 34, yy, 13, theme::faint);
            yy += 20;
        }
        if (!r.note.empty()) { utext::draw(utext::ellipsize(std::string(T("Note:")) + " " + r.note, w - 40, 14), x + 20, yy, 14, theme::muted); yy += 22; }
        if (r.status != "open") {
            std::string s = (r.status == "dismissed" ? T("Dismissed") : T("Actioned"));
            if (!r.outcome.empty()) s += " (" + r.outcome + ")";
            if (!r.decidedBy.empty()) s += std::string(" ") + T("by") + " " + r.decidedBy;
            utext::draw(s, x + 20, y + h - 34, 14, theme::gold);
        } else {
            const std::string id = r.id, who = r.accusedNick, why = g_reason;
            float bx = x + 20;
            if (ui::button({bx, y + h - 46, 130, 34}, T("Nothing to do"), ui::Style::Secondary, uadmin::busy())) uadmin::decide(id, "dismiss", 0, why);
            bx += 140;
            if (ui::button({bx, y + h - 46, 150, 34}, TextFormat(T("Time out %s"), g_length.c_str()), ui::Style::Ghost, uadmin::busy() || days <= 0))
                uadmin::decide(id, "timeout", days, why);
            bx += 160;
            if (ui::button({bx, y + h - 46, 110, 34}, T("Ban"), ui::Style::Danger, uadmin::busy()))
                a.confirm(T("Ban this account?"), TextFormat(T("%s will not be able to join any game until pardoned."), who.c_str()),
                          T("Ban"), [id, why] { uadmin::decide(id, "ban", 0, why); }, true);
        }
        y += h + 10;
    }
    if (!shown) { utext::draw(g_showClosed ? T("No reports.") : T("No open reports. Nothing to do."), x, y, 15, theme::faint); y += 30; }
}

void drawPeople(App& a, float x, float& y, float w) {
    ui::textField({x, y, w - 150, 40}, g_query, 7201, T("Nickname or account id"));
    if (ui::button({x + w - 140, y, 140, 40}, T("Look up"), ui::Style::Primary, g_query.empty() || uadmin::busy()) ||
        (ui::focusedField() == 7201 && IsKeyPressed(KEY_ENTER) && !g_query.empty()))
        uadmin::lookup(g_query);
    y += 58;
    const uadmin::Profile p = uadmin::profile();
    if (!p.valid) return;
    const long long now = (long long)std::time(nullptr);
    ui::card({x, y, w, 120});
    ui::icon(Icon::User, x + 40, y + 44, 36, p.banned ? theme::danger : theme::gold);
    utext::draw(p.nickname, x + 80, y + 18, 22, theme::ink, utext::Serif);
    std::string meta = std::string(T("since")) + " " + date(p.created).substr(0, 10) + "  ·  " +
                       TextFormat(T("%d sign-in methods"), p.linkedCount) + "  ·  " +
                       TextFormat(T("%d reports against, %d filed"), p.against, p.filed);
    for (auto& b : p.badges) meta += "  ·  " + b;
    utext::draw(utext::ellipsize(meta, w - 100, 14), x + 80, y + 52, 14, theme::muted);
    utext::draw(p.id, x + 80, y + 80, 13, theme::faint);
    y += 136;
    if (p.banned)
        utext::draw(std::string(p.bannedUntil ? T("Timed out") : T("Banned")) + "  ·  " +
                    (p.bannedUntil ? TextFormat(T("ends in %s"), uadmin::describeUntil(p.bannedUntil, now).c_str()) : T("permanent")) +
                    (p.banReason.empty() ? "" : "  ·  " + p.banReason), x, y, 15, theme::danger);
    else utext::draw(T("Not banned."), x, y, 15, theme::ok);
    y += 34;
    y = sanctionControls(x, y, w);
    const double days = uadmin::parseDays(g_length);
    const std::string who = p.nickname, why = g_reason;
    if (ui::button({x, y, 170, 38}, TextFormat(T("Time out %s"), g_length.c_str()), ui::Style::Secondary, uadmin::busy() || days <= 0))
        uadmin::act("timeout", days, why);
    if (ui::button({x + 180, y, 120, 38}, T("Ban"), ui::Style::Danger, uadmin::busy()))
        a.confirm(T("Ban this account?"), TextFormat(T("%s will not be able to join any game until pardoned."), who.c_str()),
                  T("Ban"), [why] { uadmin::act("ban", 0, why); }, true);
    if (ui::button({x + 310, y, 120, 38}, T("Pardon"), ui::Style::Ghost, uadmin::busy() || !p.banned)) uadmin::act("dismiss", 0, why);
    y += 58;
    ui::heading(T("History"), x, y, 20);
    y += 34;
    if (p.sanctions.empty()) { utext::draw(T("No bans, timeouts or restrictions, ever."), x, y, 15, theme::faint); y += 30; }
    sanctionList(p.sanctions, x, y, w);
}
}  // namespace

/** The Account page's list of punishments, shared with the lookup above. */
void drawSanctionHistory(const std::vector<Sanction>& list, float x, float& y, float w) { sanctionList(list, x, y, w); }

void drawAdmin(App& a, Rectangle r) {
    if (!uadmin::isAdmin()) { a.tab = Tab::Account; return; }
    if (!g_fetched) { g_fetched = true; uadmin::refresh(); }
    ui::beginScroll(r, g_scroll);
    const float x = r.x + 40, w = std::min(900.0f, r.width - 80);
    float y = r.y + 28 - g_scroll.y;
    ui::heading(T("Admin"), x, y, 30);
    if (ui::button({x + w - 120, y + 2, 120, 34}, T("Refresh"), ui::Style::Ghost, uadmin::busy())) uadmin::refresh();
    y += 46;
    const long long n = uadmin::accounts();
    utext::draw(n >= 0 ? TextFormat(uadmin::accountsExact() ? T("%lld accounts") : T("at least %lld accounts"), n)
                       : T("Developer tools. Every action here is checked by the account service."),
                x, y, 15, theme::muted);
    y += 34;

    const char* sections[] = {T("News"), T("Reports"), T("People")};
    float cx = x;
    for (int i = 0; i < 3; ++i) {
        const float cw = utext::measure(sections[i], 16).x + 32;
        Rectangle chip{cx, y, cw, 34};
        const bool on = g_section == i;
        DrawRectangleRounded(chip, 0.5f, 8, on ? theme::gold : (ui::hovered(chip) ? theme::ruleFirm : theme::raise));
        utext::draw(sections[i], cx + 16, y + 7, 16, on ? theme::ground : theme::ink, on ? utext::Semi : utext::Sans);
        if (ui::clicked(chip)) { g_section = i; g_scroll.target = 0; }
        cx += cw + 8;
    }
    if (uadmin::busy()) ui::spinner(cx + 18, y + 17, 9);
    const std::string st = uadmin::status();
    if (!st.empty()) utext::draw(utext::ellipsize(T(st.c_str()), x + w - cx - 40, 14), cx + 36, y + 9, 14,
                                 st == "Saved." || st == "Done." ? theme::ok : theme::muted);
    y += 56;

    if (g_section == 0) drawNews(a, x, y, w);
    else if (g_section == 1) drawReports(a, x, y, w);
    else drawPeople(a, x, y, w);
    ui::endScroll(r, g_scroll, y + g_scroll.y - r.y + 20);
}
