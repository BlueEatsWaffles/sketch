#include "Overlay.hpp"
#include "Bot.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "Util.hpp"

#include <Geode/Geode.hpp>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace overlay {

namespace {

struct Toast {
    std::string msg;
    int kind = 0;
    float t = 0.f;
    float life = 3.4f;
};

std::mutex g_mx;
std::deque<Toast> g_pending;
std::deque<Toast> g_toasts;

Col kindColor(int kind) {
    switch (kind) {
        case Good: return rgb(80, 220, 120);
        case Warn: return rgb(255, 200, 60);
        case Bad: return rgb(255, 80, 80);
        default: return theme::live().accent;
    }
}

void outlinedText(ImDrawList* dl, ImVec2 p, float size, ImU32 col, ImU32 outline, char const* s) {
    float o = util::maxv(1.f, size / 14.f);
    ui::text(dl, ImVec2(p.x - o, p.y), outline, s, nullptr, size);
    ui::text(dl, ImVec2(p.x + o, p.y), outline, s, nullptr, size);
    ui::text(dl, ImVec2(p.x, p.y - o), outline, s, nullptr, size);
    ui::text(dl, ImVec2(p.x, p.y + o), outline, s, nullptr, size);
    ui::text(dl, p, col, s, nullptr, size);
}

void drawFlash(ImDrawList* dl, ImVec2 o, ImVec2 sz) {
    float f = stats::flash();
    if (f <= 0.f || !cfg().deathFlash || !cfg().noclip) return;
    float band = 70.f;
    ImU32 red = IM_COL32(255, 40, 40, static_cast<int>(170.f * f));
    ImU32 clear = IM_COL32(255, 40, 40, 0);
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + sz.x, o.y + band), red, red, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(o.x, o.y + sz.y - band), ImVec2(o.x + sz.x, o.y + sz.y), clear, clear, red, red);
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + band, o.y + sz.y), red, clear, clear, red);
    dl->AddRectFilledMultiColor(ImVec2(o.x + sz.x - band, o.y), ImVec2(o.x + sz.x, o.y + sz.y), clear, red, red, clear);
}

void drawLabels(ImDrawList* dl, PlayLayer* pl, ImVec2 o, ImVec2 sz) {
    auto const& c = cfg();
    std::vector<std::string> lines;
    char b[160];

    if (c.lblFps) {
        std::snprintf(b, sizeof(b), "FPS: %d", static_cast<int>(ImGui::GetIO().Framerate + 0.5f));
        lines.emplace_back(b);
    }
    if (c.lblCps) {
        std::snprintf(b, sizeof(b), "CPS: %d  (%d clicks)", stats::cps(), stats::clicks());
        lines.emplace_back(b);
    }
    if (c.lblNoclipAcc && c.noclip) {
        std::snprintf(b, sizeof(b), "Noclip: %.2f%%", stats::noclipAccuracy());
        lines.emplace_back(b);
    }
    if (c.lblNoclipDeaths && c.noclip) {
        std::snprintf(b, sizeof(b), "Noclip deaths: %d", stats::noclipDeaths());
        lines.emplace_back(b);
    }
    if (c.lblAttempts) {
        std::snprintf(b, sizeof(b), "Attempt %d", stats::attempts());
        lines.emplace_back(b);
    }
    if (c.lblBest) {
        std::snprintf(b, sizeof(b), "Best run: %.1f%%", stats::best());
        lines.emplace_back(b);
    }
    if (c.lblTime) {
        double t = pl->m_attemptTime;
        int m = static_cast<int>(t / 60.0);
        double sec = t - m * 60.0;
        std::snprintf(b, sizeof(b), "Time: %d:%05.2f", m, sec);
        lines.emplace_back(b);
    }
    if (c.lblJumps) {
        std::snprintf(b, sizeof(b), "Jumps: %d", pl->m_jumps);
        lines.emplace_back(b);
    }
    if (c.lblClock) {
        std::time_t tt = std::time(nullptr);
        std::tm tmv = util::localTime(tt);
        std::strftime(b, sizeof(b), "%I:%M:%S %p", &tmv);
        lines.emplace_back(b);
    }
    if (c.lblSpeed && c.speedhack && std::fabs(c.speed - 1.f) > 0.001f) {
        std::snprintf(b, sizeof(b), "Speed: %.2fx", c.speed);
        lines.emplace_back(b);
    }
    if (c.frameStepper) {
        std::snprintf(b, sizeof(b), "FROZEN - press %s to step", util::keyName(c.stepKey).c_str());
        lines.emplace_back(b);
    }
    if (c.lblBot && bot::state() != bot::State::Idle) {
        uint32_t f = bot::frame(pl);
        if (bot::state() == bot::State::Record)
            std::snprintf(b, sizeof(b), "REC  frame %u  (%zu clicks)", f, bot::macro().inputs.size());
        else
            std::snprintf(b, sizeof(b), "PLAY  frame %u / %u", f, bot::lastFrame());
        lines.emplace_back(b);
    }
    if (c.lblPlayer && pl->m_player1) {
        auto p = pl->m_player1;
        std::snprintf(b, sizeof(b), "X %.1f  Y %.1f  vY %.2f  F %u", p->getPositionX(), p->getPositionY(),
                      p->m_yVelocity, pl->m_gameState.m_currentProgress);
        lines.emplace_back(b);
    }

    float size = 17.f * c.lblScale;
    float gap = 3.f * c.lblScale;
    float margin = 10.f;
    bool right = (c.lblCorner == 1 || c.lblCorner == 3);
    bool bottom = (c.lblCorner >= 2);
    ImU32 white = IM_COL32(255, 255, 255, static_cast<int>(255.f * c.lblAlpha));
    ImU32 black = IM_COL32(0, 0, 0, static_cast<int>(200.f * c.lblAlpha));

    // cheat indicator dot goes first
    int count = static_cast<int>(lines.size()) + (c.lblCheat ? 1 : 0);
    float total = count * (size + gap);
    float y = bottom ? (o.y + sz.y - margin - total) : (o.y + margin);

    if (c.lblCheat) {
        bool cheat = hacks::cheating();
        bool safe = hacks::safeActive();
        char const* label = !cheat ? "Legit" : (safe ? "Cheats on (safe mode)" : "Cheats ON");
        ImU32 dot = !cheat ? IM_COL32(80, 220, 120, 255) : (safe ? IM_COL32(255, 200, 60, 255) : IM_COL32(255, 70, 70, 255));
        ImVec2 ts = ui::textSize(label, nullptr, size);
        float r = size * 0.3f;
        float w = r * 2.f + 6.f + ts.x;
        float x = right ? (o.x + sz.x - margin - w) : (o.x + margin);
        ImVec2 cc(x + r, y + size * 0.5f);
        dl->AddCircleFilled(cc, r + 1.5f, black);
        dl->AddCircleFilled(cc, r, dot);
        outlinedText(dl, ImVec2(x + r * 2.f + 6.f, y), size, white, black, label);
        y += size + gap;
    }
    for (auto const& l : lines) {
        ImVec2 ts = ui::textSize(l.c_str(), nullptr, size);
        float x = right ? (o.x + sz.x - margin - ts.x) : (o.x + margin);
        outlinedText(dl, ImVec2(x, y), size, white, black, l.c_str());
        y += size + gap;
    }
}

void drawRenderBadge(ImDrawList* dl, ImVec2 o, ImVec2 sz) {
    if (!render::active()) return;
    auto const& t = theme::live();
    char b[128];
    double vs = render::videoSeconds();
    int m = static_cast<int>(vs / 60.0);
    std::snprintf(b, sizeof(b), "REC  %d:%05.2f   %d frames", m, vs - m * 60.0, render::frames());
    float size = 17.f;
    ImVec2 ts = ui::textSize(b, nullptr, size);
    float w = ts.x + 44.f, h = size + 16.f;
    ImVec2 a(o.x + (sz.x - w) * 0.5f, o.y + 12.f), z(a.x + w, a.y + h);
    dl->AddRectFilled(ImVec2(a.x + 3, a.y + 4), ImVec2(z.x + 3, z.y + 4), theme::col(t.outline, 0.5f), h * 0.5f);
    dl->AddRectFilled(a, z, theme::col(t.panel), h * 0.5f);
    dl->AddRect(a, z, theme::col(t.outline), h * 0.5f, 0, 2.5f);
    float blink = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.f);
    dl->AddCircleFilled(ImVec2(a.x + 18.f, (a.y + z.y) * 0.5f), 6.f, IM_COL32(255, 60, 60, static_cast<int>(120 + 135 * blink)));
    ui::text(dl, ImVec2(a.x + 32.f, a.y + 8.f), theme::col(t.text), b, nullptr, size);
}

void drawToasts(ImDrawList* dl, float dt, ImVec2 o, ImVec2 sz) {
    auto const& t = theme::live();
    float size = 16.f;
    float y = o.y + sz.y - 18.f;
    for (int i = static_cast<int>(g_toasts.size()) - 1; i >= 0; --i) {
        Toast& ts = g_toasts[static_cast<size_t>(i)];
        ts.t += dt;
        float in = util::easeOutBack(ts.t / 0.4f);
        float out = ts.t > ts.life ? util::clamp((ts.t - ts.life) / 0.35f, 0.f, 1.f) : 0.f;
        float a = (1.f - out) * util::clamp(ts.t / 0.15f, 0.f, 1.f);
        if (a <= 0.f) continue;

        ImVec2 txt = ui::textSize(ts.msg.c_str(), nullptr, size);
        float w = util::minv(txt.x + 58.f, sz.x * 0.6f);
        float h = size + 22.f;
        float x = o.x + sz.x - 18.f - w + (1.f - in) * (w + 40.f) + out * 30.f;
        y -= (h + 10.f) * (1.f - out * 0.6f);

        ImVec2 p0(x, y), p1(x + w, y + h);
        Col kc = kindColor(ts.kind);
        dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 5), ImVec2(p1.x + 4, p1.y + 5), theme::col(t.outline, 0.45f * a), 8.f);
        dl->AddRectFilled(p0, p1, theme::col(t.panel, a), 8.f);
        dl->AddRectFilled(p0, ImVec2(p0.x + 8.f, p1.y), theme::col(kc, a), 8.f, ImDrawFlags_RoundCornersLeft);
        ui::sketchRect(dl, p0, p1, theme::col(t.outline, a), 2.5f, 8.f, static_cast<uint32_t>(i * 977 + 13));
        ImVec2 cc(p0.x + 26.f, (p0.y + p1.y) * 0.5f);
        dl->AddCircleFilled(cc, 9.f, theme::col(kc, a));
        ImU32 ink = IM_COL32(20, 20, 20, static_cast<int>(255 * a));
        if (ts.kind == Good) ui::checkMark(dl, cc, 9.f, ink, 2.f);
        else if (ts.kind == Bad) ui::crossMark(dl, cc, 10.f, ink, 2.f);
        else {
            dl->AddLine(ImVec2(cc.x, cc.y - 4.5f), ImVec2(cc.x, cc.y + 1.5f), ink, 2.2f);
            dl->AddCircleFilled(ImVec2(cc.x, cc.y + 4.5f), 1.4f, ink);
        }
        dl->PushClipRect(p0, ImVec2(p1.x - 8.f, p1.y), true);
        ui::text(dl, ImVec2(p0.x + 44.f, p0.y + 11.f), theme::col(t.text, a), ts.msg.c_str(), nullptr, size);
        dl->PopClipRect();
    }
    while (!g_toasts.empty() && g_toasts.front().t > g_toasts.front().life + 0.4f) g_toasts.pop_front();
}

}  // namespace

void toast(std::string msg, int kind) {
    std::lock_guard<std::mutex> lock(g_mx);
    Toast t;
    t.msg = std::move(msg);
    t.kind = kind;
    g_pending.push_back(std::move(t));
}

void draw(float dt) {
    {
        std::lock_guard<std::mutex> lock(g_mx);
        while (!g_pending.empty()) {
            if (cfg().toasts || g_pending.front().kind == Bad) g_toasts.push_back(std::move(g_pending.front()));
            g_pending.pop_front();
        }
    }
    while (g_toasts.size() > 5) g_toasts.pop_front();

    auto* dl = ImGui::GetForegroundDrawList();
    auto* vp = ImGui::GetMainViewport();
    ImVec2 o = vp->Pos, sz = vp->Size;

    if (auto pl = hacks::playLayer()) {
        drawFlash(dl, o, sz);
        drawLabels(dl, pl, o, sz);
    }
    drawRenderBadge(dl, o, sz);
    drawToasts(dl, dt, o, sz);
}

}  // namespace overlay
