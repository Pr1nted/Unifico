// The console: the running game's output, live, and a way to keep it.
#include "App.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <algorithm>

void drawConsole(App& a, Rectangle r) {
    DrawRectangleRec(r, Color{7, 9, 13, 250});
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, 1, theme::ruleFirm);
    utext::draw(T("Console"), r.x + 20, r.y + 10, 15, theme::gold, utext::Semi);
    float bx = r.x + r.width - 16;
    auto b = [&](float w, const char* l) { bx -= w; bool c = ui::button({bx, r.y + 6, w, 28}, l, ui::Style::Ghost); bx -= 6; return c; };
    if (b(70, T("Close"))) a.consoleOpen = false;
    if (b(120, T("Download log"))) a.saveLog();
    if (b(70, T("Clear"))) a.console.clear();
    if (b(70, T("Copy"))) {
        std::string all;
        for (auto& l : a.console) all += l + "\n";
        SetClipboardText(all.c_str());
    }
    const bool runningNow = a.running && a.running->child && a.running->child->running();
    if (runningNow && b(80, T("Stop game"))) a.stop();
    Rectangle body{r.x + 12, r.y + 40, r.width - 24, r.height - 48};
    const float lh = 17;
    const int visible = (int)(body.height / lh);
    if (CheckCollisionPointRec(ui::mouse(), body)) {
        float wh = GetMouseWheelMove();
        if (wh != 0) { a.consoleScroll += wh * 3; a.consoleFollow = a.consoleScroll <= 0; }
    }
    a.consoleScroll = std::clamp(a.consoleScroll, 0.0f, std::max(0.0f, (float)a.console.size() - visible));
    if (a.consoleFollow) a.consoleScroll = 0;
    ui::scissor((int)body.x, (int)body.y, (int)body.width, (int)body.height);
    const int end = (int)a.console.size() - (int)a.consoleScroll;
    const int start = std::max(0, end - visible);
    if (a.console.empty()) utext::draw(T("Nothing yet. Output from the game appears here while it runs."), body.x + 8, body.y + 4, 14, theme::faint);
    for (int i = start; i < end; ++i) {
        const std::string& l = a.console[(size_t)i];
        Color c = theme::muted;
        if (l.find("ERROR") != std::string::npos || l.find("error") != std::string::npos) c = Color{232, 140, 130, 255};
        else if (l.find("WARN") != std::string::npos) c = Color{220, 190, 110, 255};
        utext::draw(utext::ellipsize(l, body.width - 16, 14), body.x + 8, body.y + (i - start) * lh, 14, c);
    }
    EndScissorMode();
}
