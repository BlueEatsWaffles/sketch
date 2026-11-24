#include "Gui.hpp"
#include "Bot.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "Import.hpp"
#include "Overlay.hpp"
#include "Predict.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "Util.hpp"
#include "Visuals.hpp"
#include "WinPaths.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace geode::prelude;

namespace gui {

namespace {

bool g_open = false;
float g_openT = 0.f;      // 0 = closed, 1 = open (bouncy, can go a bit past 1)
int g_tab = 0;
double g_tabTime = 0.0;   // when the current tab was opened
float g_markerY = -1.f;
float g_scrollCur = 0.f;
float g_scrollTarget = 0.f;
float g_saveTimer = 0.f;

std::vector<std::string> g_macros;
bool g_macrosDirty = true;
int g_selMacro = -1;
int g_resetClicks = 0;
bool g_importScanned = false;
std::string g_importSel;
bool g_autoChecked = false;

constexpr int kTabCount = 9;
char const* const kTabs[kTabCount] = {"Bot", "Predict", "Player", "Visual", "Speed", "Render", "Labels", "Extra", "Themes"};
char const* const kTabTitles[kTabCount] = {"Bot & Replays", "Predict", "Player", "Hitboxes & Visuals", "Speed",
                                           "Video Maker",   "Labels",  "Extra",  "Themes"};

char const* const kFontNames[] = {"ImGui pixel font", "Segoe UI Bold", "Comic Sans Bold (sketchy!)", "Consolas Bold",
                                  "Segoe UI"};
char const* const kFontFiles[] = {"", "segoeuib.ttf", "comicbd.ttf", "consolab.ttf", "segoeui.ttf"};
constexpr int kFontCount = 5;

std::string fontPath(int idx) {
    if (idx <= 0 || idx >= kFontCount) return {};
    char win[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryA(win, MAX_PATH);
    std::string base = (n > 0 && n < MAX_PATH) ? std::string(win) : std::string("C:\\Windows");
    return base + "\\Fonts\\" + kFontFiles[idx];
}

ImVec2 v2(float x, float y) { return ImVec2(x, y); }

float enterTime() { return static_cast<float>(ImGui::GetTime() - g_tabTime); }

// ---------------------------------------------------------------------------
//  Little hand-drawn tab icons
// ---------------------------------------------------------------------------
void drawIcon(ImDrawList* dl, int idx, ImVec2 c, float r, ImU32 col, float th) {
    switch (idx) {
        case 0: {  // bot: robot head
            dl->AddRect(v2(c.x - r, c.y - r * 0.7f), v2(c.x + r, c.y + r * 0.8f), col, r * 0.3f, 0, th);
            dl->AddCircleFilled(v2(c.x - r * 0.4f, c.y), r * 0.2f, col);
            dl->AddCircleFilled(v2(c.x + r * 0.4f, c.y), r * 0.2f, col);
            dl->AddLine(v2(c.x, c.y - r * 0.7f), v2(c.x, c.y - r * 1.1f), col, th);
            dl->AddCircleFilled(v2(c.x, c.y - r * 1.15f), r * 0.15f, col);
            break;
        }
        case 1: {  // predict: crystal ball with a sparkle
            dl->AddCircle(v2(c.x, c.y - r * 0.15f), r * 0.85f, col, 0, th);
            dl->AddLine(v2(c.x - r * 0.8f, c.y + r * 0.95f), v2(c.x + r * 0.8f, c.y + r * 0.95f), col, th * 1.3f);
            float tw = 0.6f + 0.4f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.f);
            dl->AddLine(v2(c.x + r * 0.15f, c.y - r * 0.55f * tw), v2(c.x + r * 0.15f, c.y + r * 0.15f * tw), col, th * 0.8f);
            dl->AddLine(v2(c.x - r * 0.2f, c.y - r * 0.2f), v2(c.x + r * 0.5f, c.y - r * 0.2f), col, th * 0.8f);
            break;
        }
        case 2: {  // player: GD cube
            dl->AddRect(v2(c.x - r, c.y - r), v2(c.x + r, c.y + r), col, 0.f, 0, th);
            dl->AddRectFilled(v2(c.x - r * 0.4f, c.y - r * 0.4f), v2(c.x + r * 0.4f, c.y + r * 0.4f), col);
            break;
        }
        case 3: {  // visual: eye
            for (int i = 0; i < 24; i++) {
                float a = 6.2831853f * static_cast<float>(i) / 24.f;
                dl->PathLineTo(v2(c.x + std::cos(a) * r, c.y + std::sin(a) * r * 0.55f));
            }
            dl->PathStroke(col, ImDrawFlags_Closed, th);
            dl->AddCircleFilled(c, r * 0.3f, col);
            break;
        }
        case 4: {  // speed: lightning bolt
            ImVec2 pts[6] = {
                v2(c.x + r * 0.25f, c.y - r),        v2(c.x - r * 0.55f, c.y + r * 0.1f), v2(c.x - r * 0.02f, c.y + r * 0.1f),
                v2(c.x - r * 0.25f, c.y + r),        v2(c.x + r * 0.55f, c.y - r * 0.15f), v2(c.x + r * 0.02f, c.y - r * 0.15f),
            };
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case 5: {  // render: camera
            dl->AddRect(v2(c.x - r, c.y - r * 0.6f), v2(c.x + r * 0.35f, c.y + r * 0.6f), col, r * 0.2f, 0, th);
            dl->AddTriangleFilled(v2(c.x + r * 0.35f, c.y), v2(c.x + r, c.y - r * 0.5f), v2(c.x + r, c.y + r * 0.5f), col);
            break;
        }
        case 6: {  // labels: note with lines
            dl->AddRect(v2(c.x - r * 0.8f, c.y - r), v2(c.x + r * 0.8f, c.y + r), col, r * 0.15f, 0, th);
            for (int i = -1; i <= 1; i++)
                dl->AddLine(v2(c.x - r * 0.45f, c.y + i * r * 0.45f), v2(c.x + r * 0.45f, c.y + i * r * 0.45f), col, th * 0.8f);
            break;
        }
        case 7: {  // extra: gear
            dl->AddCircle(c, r * 0.55f, col, 0, th);
            for (int i = 0; i < 8; i++) {
                float a = 6.2831853f * static_cast<float>(i) / 8.f;
                dl->AddLine(v2(c.x + std::cos(a) * r * 0.55f, c.y + std::sin(a) * r * 0.55f),
                            v2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), col, th * 1.3f);
            }
            dl->AddCircleFilled(c, r * 0.18f, col);
            break;
        }
        default: {  // themes: paint palette
            dl->AddCircle(c, r, col, 0, th);
            dl->AddCircleFilled(v2(c.x - r * 0.4f, c.y - r * 0.3f), r * 0.2f, col);
            dl->AddCircleFilled(v2(c.x + r * 0.15f, c.y - r * 0.5f), r * 0.2f, col);
            dl->AddCircleFilled(v2(c.x + r * 0.5f, c.y + r * 0.05f), r * 0.2f, col);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
//  Tab: Bot
// ---------------------------------------------------------------------------
void botStatusCard() {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = ui::sc();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x - 8.f * s, 50.f);
    float h = 58.f * s;
    ImGui::Dummy(ImVec2(w, h));

    auto st = bot::state();
    auto const& m = bot::macro();
    ImVec2 a = p, b(p.x + w, p.y + h);
    dl->AddRectFilled(a, b, theme::col(t.field), cfg().rounding * s);
    ui::sketchRect(dl, a, b, theme::col(t.outline), cfg().outline * s * 0.8f, cfg().rounding * s, 0xB07u);

    float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.f);
    Col dot = st == bot::State::Record ? rgb(255, 70, 70) : (st == bot::State::Play ? rgb(80, 220, 120) : t.textDim);
    float dotA = st == bot::State::Idle ? 0.7f : (0.55f + 0.45f * pulse);
    ImVec2 dc(a.x + 22.f * s, a.y + h * 0.5f);
    dl->AddCircleFilled(dc, 9.f * s, theme::col(dot, dotA));
    dl->AddCircle(dc, 9.f * s, theme::col(t.outline), 0, 2.f * s);

    char const* title = st == bot::State::Record ? "RECORDING" : (st == bot::State::Play ? "PLAYING" : "IDLE");
    float fs = ImGui::GetFontSize();
    ui::text(dl, v2(a.x + 42.f * s, a.y + 8.f * s), theme::col(t.text), title, nullptr, fs * 1.25f);

    char buf[200];
    auto pl = hacks::playLayer();
    uint32_t f = pl ? bot::frame(pl) : 0u;
    if (st == bot::State::Idle) {
        if (m.inputs.empty() && m.fixes.empty())
            std::snprintf(buf, sizeof(buf), "No macro loaded");
        else
            std::snprintf(buf, sizeof(buf), "Loaded: %zu clicks, %zu fixes, %.1fs%s%s", m.inputs.size(), m.fixes.size(),
                          bot::lastFrame() / 240.0, m.level.empty() ? "" : "  -  ", m.level.c_str());
    } else if (st == bot::State::Record) {
        std::snprintf(buf, sizeof(buf), "Frame %u   -   %zu clicks   -   %zu fixes", f, m.inputs.size(), m.fixes.size());
    } else {
        std::snprintf(buf, sizeof(buf), "Frame %u of %u", f, bot::lastFrame());
    }
    dl->PushClipRect(a, b, true);
    ui::text(dl, v2(a.x + 42.f * s, a.y + 10.f * s + fs * 1.3f), theme::col(t.textDim), buf);
    dl->PopClipRect();
}

std::string lowerStr(std::string s) {
    for (auto& ch : s) ch = static_cast<char>((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
    return s;
}

void importSection() {
    auto& c = cfg();
    float s = ui::sc();
    ui::section("Macros from other bots");
    ui::note("Play macros made with Eclipse (.gdr2), Mega Hack, xdBot, zBot or Prism (.gdr), "
             "yBot 2 (.ybot) and ToastyReplay (.ttr). They're found by themselves. "
             "You can also drop the files into the macros folder (the Folder button above).");
    if (!g_importScanned) {
        g_importScanned = true;
        imports::scan();
    }
    ui::inputText("Search##impsearch", c.importSearch, sizeof(c.importSearch), "level or file name");

    float gap = ImGui::GetStyle().ItemSpacing.x;
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 2.f) / 3.f;
    auto loadSel = [&](bool play) {
        if (g_importSel.empty()) {
            overlay::toast("Pick a macro in the list first.", overlay::Warn);
            return;
        }
        std::string sum, err;
        if (!imports::load(g_importSel, sum, err)) {
            overlay::toast(err, overlay::Bad);
            return;
        }
        overlay::toast("Loaded " + sum, overlay::Good);
        if (play) bot::startPlay();
    };
    if (ui::button(imports::scanning() ? "Looking...##imprescan" : "Look again##imprescan", v2(bw, 0))) imports::scan();
    ImGui::SameLine();
    if (ui::button("Load##impload", v2(bw, 0))) loadSel(false);
    ImGui::SameLine();
    if (ui::button("Load & Play##impplay", v2(bw, 0), true)) loadSel(true);

    auto all = imports::found();
    std::string q = lowerStr(c.importSearch);
    int shown = 0, matches = 0;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    for (size_t i = 0; i < all.size(); i++) {
        auto const& f = all[i];
        if (!q.empty() && lowerStr(f.name).find(q) == std::string::npos && lowerStr(f.bot).find(q) == std::string::npos)
            continue;
        matches++;
        if (shown >= 80) continue;
        shown++;
        ImGui::PushID(static_cast<int>(i) + 5000);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = util::maxv(ImGui::GetContentRegionAvail().x - 8.f * s, 50.f);
        float h = ImGui::GetFontSize() + 10.f * s;
        bool clicked = ImGui::InvisibleButton("##imp", v2(w, h));
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetTooltip("%s", f.path.c_str());
        bool sel = (g_importSel == f.path);
        if (clicked) g_importSel = f.path;
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_importSel = f.path;
            loadSel(false);
        }
        ImGuiID id = ImGui::GetID("##imp");
        float sa = ui::anim(id, sel ? 1.f : 0.f, 14.f);
        float ha = ui::anim(id ^ 0x77u, hov ? 1.f : 0.f, 14.f);
        if (sa + ha > 0.01f)
            dl->AddRectFilled(p, v2(p.x + w, p.y + h), theme::col(theme::mix(t.field, t.accent, sa * 0.35f), 0.5f * ha + sa),
                              cfg().rounding * s * 0.6f);
        ImVec2 tagSize = ui::textSize(f.bot.c_str());
        float tagX = p.x + w - 8.f * s - tagSize.x;
        dl->PushClipRect(p, v2(tagX - 8.f * s, p.y + h), true);
        ui::text(dl, v2(p.x + 10.f * s + ha * 3.f * s, p.y + 5.f * s), theme::col(t.text), f.name.c_str());
        dl->PopClipRect();
        ui::text(dl, v2(tagX, p.y + 5.f * s), theme::col(t.textDim), f.bot.c_str());
        ImGui::PopID();
    }
    if (imports::scanning() && all.empty()) {
        ui::note("Looking for macros on your PC...");
    } else if (all.empty()) {
        ui::note("No macros from other bots found yet. Drop .gdr / .gdr2 / .ybot / .ttr files into the macros "
                 "folder and press Look again.");
    } else if (matches > shown) {
        char b[96];
        std::snprintf(b, sizeof(b), "Showing %d of %d. Type in Search to find more.", shown, matches);
        ui::note(b);
    } else if (matches == 0) {
        ui::note("Nothing matches your search.");
    }
    ui::sliderInt("Frame offset", &c.importOffset, -5, 5,
                  "If a macro from another bot dies right away, try -1 or +1, then load it again.");
    ui::inputText("Also look in folder", c.importDir, sizeof(c.importDir), "C:\\my macros");
}

void tabBot() {
    auto& c = cfg();
    float s = ui::sc();
    ui::section("Bot type");
    static char const* modes[] = {"Macro bot", "Replay bot"};
    ui::segmented("##botmode", &c.botMode, modes, 2);
    ui::note(c.botMode == 0
                 ? "Macro bot: saves only your clicks. Small files. On some levels it can drift a tiny bit."
                 : "Replay bot: saves your clicks AND where the player is. During playback it puts the player back "
                   "in the right spot, so it never drifts.");

    ui::section("Controls");
    botStatusCard();
    ui::spacing(4);
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 2.f) / 3.f;
    float bh = ui::rowH() * 1.25f;
    auto st = bot::state();
    if (ui::button("Record##botrec", v2(bw, bh), st == bot::State::Record)) bot::startRecord();
    ImGui::SameLine();
    if (ui::button("Play##botplay", v2(bw, bh), st == bot::State::Play)) bot::startPlay();
    ImGui::SameLine();
    if (ui::button("Stop##botstop", v2(bw, bh))) bot::stop();

    if (c.botMode == 1)
        ui::sliderInt("Fix player every N ticks", &c.fixInterval, 1, 60,
                      "1 = most accurate. Bigger number = smaller file.");
    ui::toggle("Block my clicks while playing", &c.blockInputOnPlay, "So you can't mess up the macro by accident.");
    ui::toggle("Stop when the level is beaten", &c.autoStopAtEnd);
    ui::toggle("Make a video while playing", &c.rAutoWithBot, "Starts the Video Maker when you press Play.");

    ui::section("Macro library");
    ui::inputText("Name##macroname", c.macroName, sizeof(c.macroName), "type a name");
    float bw4 = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 3.f) / 4.f;
    std::string err;
    if (ui::button("Save##msave", v2(bw4, 0))) {
        if (bot::save(c.macroName, err)) overlay::toast(std::string("Saved \"") + c.macroName + "\"", overlay::Good);
        else overlay::toast(err, overlay::Bad);
        g_macrosDirty = true;
    }
    ImGui::SameLine();
    if (ui::button("Load##mload", v2(bw4, 0))) {
        if (bot::load(c.macroName, err)) overlay::toast(std::string("Loaded \"") + c.macroName + "\"", overlay::Good);
        else overlay::toast(err, overlay::Bad);
    }
    ImGui::SameLine();
    if (ui::button("Delete##mdel", v2(bw4, 0))) {
        if (bot::remove(c.macroName)) overlay::toast(std::string("Deleted \"") + c.macroName + "\"", overlay::Info);
        g_macrosDirty = true;
        g_selMacro = -1;
    }
    ImGui::SameLine();
    if (ui::button("Folder##mfolder", v2(bw4, 0))) render::openFolder(util::pathStr(bot::folder()));

    if (g_macrosDirty) {
        g_macros = bot::list();
        g_macrosDirty = false;
    }
    if (g_macros.empty()) {
        ui::note("No saved macros yet. Record one, give it a name and press Save.");
    } else {
        auto* dl = ImGui::GetWindowDrawList();
        auto const& t = theme::live();
        for (int i = 0; i < static_cast<int>(g_macros.size()); i++) {
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = util::maxv(ImGui::GetContentRegionAvail().x - 8.f * s, 50.f);
            float h = ImGui::GetFontSize() + 10.f * s;
            bool clicked = ImGui::InvisibleButton("##m", v2(w, h));
            bool hov = ImGui::IsItemHovered();
            if (clicked) {
                g_selMacro = i;
                std::snprintf(c.macroName, sizeof(c.macroName), "%s", g_macros[static_cast<size_t>(i)].c_str());
            }
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (bot::load(g_macros[static_cast<size_t>(i)], err)) overlay::toast("Macro loaded", overlay::Good);
                else overlay::toast(err, overlay::Bad);
            }
            ImGuiID id = ImGui::GetID("##m");
            float sel = ui::anim(id, g_selMacro == i ? 1.f : 0.f, 14.f);
            float ha = ui::anim(id ^ 0x77u, hov ? 1.f : 0.f, 14.f);
            if (sel + ha > 0.01f)
                dl->AddRectFilled(p, v2(p.x + w, p.y + h), theme::col(theme::mix(t.field, t.accent, sel * 0.35f), 0.5f * ha + sel),
                                  cfg().rounding * s * 0.6f);
            // little page icon
            ImVec2 ic(p.x + 14.f * s, p.y + h * 0.5f);
            dl->AddRect(v2(ic.x - 5.f * s, ic.y - 7.f * s), v2(ic.x + 5.f * s, ic.y + 7.f * s), theme::col(t.text), 1.5f * s, 0, 1.5f * s);
            ui::text(dl, v2(p.x + 28.f * s + ha * 3.f * s, p.y + 5.f * s), theme::col(t.text), g_macros[static_cast<size_t>(i)].c_str());
            ImGui::PopID();
        }
        ui::note("Click = pick, double-click = load.");
    }

    importSection();

    ui::section("Frame stepper");
    ui::toggle("Frame stepper (freeze the game)", &c.frameStepper,
               "Freezes the level. Each press of the step key moves it forward by 1 tick (1/240 s). "
               "Hold the key to keep going.");
    ui::keybind("Step 1 tick key", &c.stepKey);
    ui::keybind("Stepper on/off key", &c.stepToggleKey);
    if (ui::button("Step +1 tick##stepbtn", v2(-1.f, 0))) hacks::requestStep();
}

// ---------------------------------------------------------------------------
//  Tab: Predict (a bot that beats the level by itself)
// ---------------------------------------------------------------------------
std::string g_predictSaved;  // where the last file went

std::string clockStr(double secs) {
    if (secs < 0.0) secs = 0.0;
    int t = static_cast<int>(secs);
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof(b), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else std::snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
    return b;
}

std::string bigNum(long long n) {
    std::string s = std::to_string(n < 0 ? 0 : n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}

void predictCard() {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = ui::sc();
    float slide = 0.f;
    float en = ui::rowEnter(&slide);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x - 8.f * s, 50.f);
    float fs = ImGui::GetFontSize();
    float h = fs * 2.6f + 46.f * s;
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * util::clamp(en * 1.5f, 0.f, 1.f));

    auto ph = predictor::phase();
    bool act = predictor::active();
    bool done = !act && predictor::hasResult();
    bool failed = !act && !done && ph == predict::Phase::Failed;
    auto st = predictor::stats();
    float time = static_cast<float>(ImGui::GetTime());
    ImVec2 a(p.x + slide, p.y), b(p.x + slide + w, p.y + h);
    float R = cfg().rounding * s;

    // little shadow, box, pencil outline
    dl->AddRectFilled(ImVec2(a.x + 4.f * s, a.y + 5.f * s), ImVec2(b.x + 4.f * s, b.y + 5.f * s), theme::col(t.outline, 0.3f), R);
    dl->AddRectFilled(a, b, theme::col(t.field), R);
    ui::sketchRect(dl, a, b, theme::col(t.outline), cfg().outline * s * 0.85f, R, 0x9E1Du);

    // crystal ball that wobbles while the bot thinks
    float bob = act ? std::sin(time * 3.2f) * 3.f * s : 0.f;
    ImVec2 bc(a.x + 32.f * s, a.y + 30.f * s + bob);
    float br = 16.f * s;
    Col ballCol = done ? rgb(90, 220, 130) : (failed ? rgb(240, 90, 90) : t.accent);
    float glow = act ? 0.35f + 0.25f * std::sin(time * 5.f) : 0.2f;
    dl->AddCircleFilled(bc, br * 1.35f, theme::col(ballCol, glow * 0.5f));
    dl->AddCircleFilled(bc, br, theme::col(ballCol, 0.85f));
    dl->AddCircle(bc, br, theme::col(t.outline), 0, 2.4f * s);
    if (act) {  // swirl inside the ball
        for (int i = 0; i < 3; i++) {
            float ang = time * 2.4f + static_cast<float>(i) * 2.094f;
            ImVec2 q(bc.x + std::cos(ang) * br * 0.5f, bc.y + std::sin(ang) * br * 0.5f);
            dl->AddCircleFilled(q, 2.6f * s, theme::col(t.accentText, 0.8f));
        }
    } else if (done) {
        ui::checkMark(dl, bc, br * 0.9f, theme::col(t.accentText), 2.6f * s);
    } else if (failed) {
        ui::crossMark(dl, bc, br * 0.8f, theme::col(t.accentText), 2.6f * s);
    } else {
        dl->AddCircleFilled(ImVec2(bc.x - br * 0.35f, bc.y - br * 0.35f), br * 0.22f, theme::col(t.accentText, 0.7f));
    }
    dl->AddLine(ImVec2(bc.x - br * 0.9f, bc.y + br + 4.f * s), ImVec2(bc.x + br * 0.9f, bc.y + br + 4.f * s), theme::col(t.outline), 3.f * s);

    // title + one line of details
    char const* title = "Ready";
    std::string detail;
    char buf[200];
    if (act && ph == predict::Phase::Verifying) {
        title = "Double-checking...";
        detail = "Playing the run from 0% to make sure it really works.";
    } else if (act) {
        int dots = static_cast<int>(time * 3.f) % 4;
        static char const* const thinking[] = {"Thinking", "Thinking.", "Thinking..", "Thinking..."};
        title = thinking[dots];
        std::snprintf(buf, sizeof(buf), "%sBest %.1f%%   -   %s tries   -   %s backtracks",
                      predictor::twoPlayer() ? "2 players   -   " : "", st.bestPercent, bigNum(st.decisions).c_str(),
                      bigNum(st.backtracks).c_str());
        detail = buf;
    } else if (done) {
        title = "Beat it!";
        std::snprintf(buf, sizeof(buf), "%zu clicks, found in %s. Save it below.", predictor::result().inputs.size(),
                      clockStr(predictor::elapsed()).c_str());
        detail = buf;
    } else if (failed) {
        title = "Gave up";
        detail = predictor::message();
    } else if (!hacks::playLayer()) {
        title = "Open a level";
        detail = "Go into any level, then press the big button.";
    } else {
        detail = "Press the big button and the bot plays this level by itself.";
    }
    float tx = a.x + 62.f * s;
    dl->PushClipRect(a, b, true);
    ui::text(dl, ImVec2(tx, a.y + 9.f * s), theme::col(t.text), title, nullptr, fs * 1.25f);
    if (act) {
        std::string tm = clockStr(predictor::elapsed());
        ImVec2 ts = ui::textSize(tm.c_str());
        ui::text(dl, ImVec2(b.x - 12.f * s - ts.x, a.y + 12.f * s), theme::col(t.textDim), tm.c_str());
    }
    ui::text(dl, ImVec2(tx, a.y + 13.f * s + fs * 1.3f), theme::col(t.textDim), detail.c_str());
    dl->PopClipRect();

    // progress bar (hatched like pencil shading)
    float pct = done ? 100.f : predictor::percentDone();
    float shown = ui::anim(ImGui::GetID("##predbar"), pct / 100.f, 6.f);
    ImVec2 g0(tx, b.y - 20.f * s), g1(b.x - 74.f * s, b.y - 9.f * s);
    dl->AddRectFilled(g0, g1, theme::col(t.panel), 4.f * s);
    if (shown > 0.003f) {
        ImVec2 f1(g0.x + (g1.x - g0.x) * util::clamp(shown, 0.f, 1.f), g1.y);
        dl->AddRectFilled(g0, f1, theme::col(t.accent, 0.9f), 4.f * s);
        ui::hatch(dl, g0, f1, theme::col(t.accentText, 0.25f), 6.f * s, 1.2f * s, 0x4A7Cu);
    }
    ui::sketchRect(dl, g0, g1, theme::col(t.outline), 2.f * s, 4.f * s, 0x4A7Du);
    std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
    ImVec2 ps = ui::textSize(buf);
    ui::text(dl, ImVec2(b.x - 12.f * s - ps.x, (g0.y + g1.y) * 0.5f - fs * 0.5f), theme::col(t.text), buf);
    ImGui::PopStyleVar();
}

void predictSaved(bool ok, std::string const& path, std::string const& err) {
    if (ok) {
        g_predictSaved = path;
        overlay::toast("Saved to " + path, overlay::Good);
    } else {
        overlay::toast(err, overlay::Bad);
    }
}

void tabPredict() {
    auto& c = cfg();
    float s = ui::sc();
    float gap = ImGui::GetStyle().ItemSpacing.x;
    bool act = predictor::active();
    auto pl = hacks::playLayer();

    ui::note("Press the button and a bot plays the level all by itself. It tries, dies, goes back a bit "
             "(backtracking) and tries again until it wins. It uses practice mode while it thinks and "
             "never uses speedhack.");
    predictCard();
    ui::spacing(4);

    float bh = ui::rowH() * 1.6f;
    if (!act) {
        if (ui::button("Predict this level!##pgo", v2(-1.f, bh), true)) {
            if (!pl) {
                overlay::toast("Open a level first, then press Predict.", overlay::Warn);
            } else if (predictor::start(pl)) {
                g_predictSaved.clear();
                if (g_open) toggle();  // close the menu so you can watch
            }
        }
    } else {
        if (ui::button("Stop thinking##pstop", v2(-1.f, bh), true)) predictor::stop(true);
        ui::note("You can close the menu and watch. Your clicks are blocked until it's done.");
    }

    ui::section("How should it click?");
    static char const* const timings[] = {"Early", "Normal", "Late"};
    if (act) {
        ui::note("Stop the bot first to change this.");
    } else {
        ui::segmented("##ptiming", &c.predictTiming, timings, 3);
    }
    static char const* const timingNotes[] = {
        "Early: clicks as soon as it safely can. Looks quick and snappy.",
        "Normal: clicks in the middle of the safe window. The safest choice.",
        "Late: waits until the very last safe moment. Looks risky and cool.",
    };
    ui::note(timingNotes[util::clamp(c.predictTiming, 0, 2)]);

    ui::section("Smarts");
    ui::sliderInt("Look-ahead after a click", &c.predictMargin, 8, 90,
                  "How far (in ticks) the bot checks that a click is really safe.\n"
                  "Bigger = smarter on hard parts, but slower.");
    ui::toggle("Double-check from the start", &c.predictVerify,
               "When it wins, it plays the whole run again from 0% to make sure it works.\nKeep this on!");
    ui::toggle("Show the bot thinking", &c.predictShow, "Shows how far the bot got while it thinks.");
    ui::sliderInt("Thinking speed", &c.predictBudget, 4, 40,
                  "Milliseconds of thinking per frame.\nBigger = finds the run faster, but the game gets choppy.");

    ui::section("Save the run");
    if (!predictor::hasResult()) {
        ui::note("When the bot beats the level, you can save it here.");
    } else {
        char info[200];
        std::snprintf(info, sizeof(info), "Run for \"%s\": %zu clicks.", predictor::levelName().c_str(),
                      predictor::result().inputs.size());
        ui::note(info);
        if (!c.predictName[0]) std::snprintf(c.predictName, sizeof(c.predictName), "%s", predictor::defaultName().c_str());
        ui::inputText("File name##pname", c.predictName, sizeof(c.predictName), predictor::defaultName().c_str());
        static char const* const formats[] = {"GDR (.gdr) - works in other bots", "sketch (.skm) - this menu"};
        ui::combo("File type", &c.predictFormat, formats, 2,
                  ".gdr works in Mega Hack, xdBot, zBot, Prism, ToastyReplay and this menu.");

        std::string name = c.predictName;
        std::string err, path;
        float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 2.f) / 3.f;
        if (ui::button("Save as macro##psavelib", v2(bw, 0), true)) {
            bool ok = predictor::saveToLibrary(name, err);
            if (ok) {
                g_macrosDirty = true;
                std::snprintf(c.macroName, sizeof(c.macroName), "%s", util::safeFileName(name).c_str());
                g_predictSaved = "your macro library (Bot tab)";
                overlay::toast("Saved! It's in the Bot tab's macro list.", overlay::Good);
            } else {
                overlay::toast(err, overlay::Bad);
            }
        }
        ImGui::SameLine();
        if (ui::button("Save to Downloads##psavedl", v2(bw, 0))) {
            bool ok = predictor::saveToFolder(winpaths::downloads(), name, c.predictFormat, path, err);
            predictSaved(ok, path, err);
        }
        ImGui::SameLine();
        if (ui::button("Watch it##pwatch", v2(bw, 0))) {
            if (predictor::watch() && g_open) toggle();
        }

        ui::inputText("Your folder##pdir", c.predictDir, sizeof(c.predictDir), "C:\\my bot runs");
        float bw2 = (ImGui::GetContentRegionAvail().x - 8.f * s - gap) / 2.f;
        if (ui::button("Save to my folder##psavedir", v2(bw2, 0))) {
            bool ok = predictor::saveToFolder(c.predictDir, name, c.predictFormat, path, err);
            predictSaved(ok, path, err);
        }
        ImGui::SameLine();
        if (ui::button("Open my folder##popendir", v2(bw2, 0))) {
            if (c.predictDir[0]) render::openFolder(c.predictDir);
            else overlay::toast("Type a folder first.", overlay::Warn);
        }
        if (ui::button("Make a video of it##pvideo", v2(-1.f, 0))) {
            bool old = c.rAutoWithBot;
            c.rAutoWithBot = true;  // the Video Maker starts with the playback
            bool ok = predictor::watch();
            c.rAutoWithBot = old;
            if (ok && g_open) toggle();
        }
        if (!g_predictSaved.empty()) {
            std::string m = "Last saved to: " + g_predictSaved;
            ui::note(m.c_str());
        }
    }

    ui::section("Good to know");
    ui::note("- Works on normal and 2-player levels (each player gets its own clicks).\n"
             "- Platformer levels aren't supported yet.\n"
             "- Hard levels can take a few minutes. Short levels take seconds.\n"
             "- It's in practice mode, so nothing gets saved to your stats.\n"
             "- The file is named after the level, with \" - bot\" at the end.");
}

// ---------------------------------------------------------------------------
//  Tab: Player
// ---------------------------------------------------------------------------
void tabPlayer() {
    auto& c = cfg();
    ui::section("Not dying");
    ui::toggle("Noclip", &c.noclip, "You can't die. Spikes go right through you.");
    if (ui::subBegin("##noclipsub", c.noclip)) {
        ui::toggle("Player 1", &c.noclipP1);
        ui::toggle("Player 2", &c.noclipP2);
        ui::toggle("Red flash when you get hit", &c.deathFlash);
        ui::keybind("Noclip on/off key", &c.noclipKey);
        ui::subEnd();
    }
    ui::toggle("Safe mode", &c.safeMode, "Your progress and stars will NOT be saved.");
    ui::toggle("Auto safe mode", &c.autoSafe, "Turns safe mode on by itself whenever a cheat is on.");

    ui::section("Respawning");
    ui::toggle("Custom respawn time", &c.customRespawn);
    if (ui::subBegin("##respawnsub", c.customRespawn)) {
        ui::sliderFloat("Respawn delay (seconds)", &c.respawnDelay, 0.f, 2.f, "%.2f s", "0 = instant respawn");
        ui::subEnd();
    }
    ui::toggle("No death effect", &c.noDeathEffect, "No explosion when you die.");
    ui::toggle("No respawn effect", &c.noSpawnEffect, "No circle effect when you come back.");

    ui::section("Clicking");
    ui::toggle("Jump hack (jump in the air)", &c.jumpHack, "Lets you jump again while flying through the air.");
    ui::toggle("Auto clicker", &c.autoclicker);
    if (ui::subBegin("##acsub", c.autoclicker)) {
        ui::sliderInt("Hold for (ticks)", &c.acHold, 1, 60);
        ui::sliderInt("Let go for (ticks)", &c.acRelease, 1, 60);
        ui::toggle("Also click for player 2", &c.acP2);
        ui::subEnd();
    }

    ui::section("Other");
    ui::toggle("Don't pause when I click away", &c.noUnfocusPause, "The game keeps going when the window loses focus.");
    ui::toggle("No \"New best\" popup", &c.noNewBest);
}

// ---------------------------------------------------------------------------
//  Tab: Visual
// ---------------------------------------------------------------------------
void tabVisual() {
    auto& c = cfg();
    float s = ui::sc();
    ui::section("Hitboxes");
    ui::toggle("Show hitboxes", &c.hitboxes, "Shows the invisible boxes the game uses to check if you hit something.");
    ui::toggle("Show hitboxes when I die", &c.hitboxOnDeath);
    ui::toggle("Only my hitbox", &c.hbOnlyPlayer, "Hides all the other boxes. Just you.");
    ui::toggle("Highlight my own hitbox", &c.playerBox);
    ui::toggle("Hitbox trail", &c.hitboxTrail, "Leaves a trail of your hitbox behind you.");
    if (ui::subBegin("##trailsub", c.hitboxTrail)) {
        ui::sliderInt("Trail length", &c.trailLen, 5, 400);
        ui::subEnd();
    }
    ui::toggle("Custom hitbox colors", &c.hbCustom, "Pick your own colors and line thickness for the hitboxes.");
    if (ui::subBegin("##customhb", c.hbCustom)) {
        ui::sliderFloat("Line thickness##hbw", &c.borderWidth, 0.1f, 3.f, "%.2f");
        ui::toggle("Rainbow lines##hbr", &c.hbRainbow, "The lines slowly change color.");
        ui::toggle("Fill the boxes", &c.hbFill);
        if (ui::subBegin("##fillsub", c.hbFill)) {
            ui::sliderFloat("Fill see-through", &c.hbFillAlpha, 0.f, 1.f, "%.2f");
            ui::subEnd();
        }
        ui::colorEdit("Spikes & saws (kill you)##hb1", &c.hbHazard);
        ui::colorEdit("Blocks (solid)##hb2", &c.hbSolid);
        ui::colorEdit("Orbs, pads, portals##hb3", &c.hbSpecial);
        ui::colorEdit("Everything else##hb4", &c.hbOther);
        ui::colorEdit("Player##hb5", &c.hbPlayer);
        ui::subEnd();
    }

    ui::section("Hit borders");
    if (ui::toggle("Hit borders", &c.hitBorders,
                   "Your player becomes a small dot, and every spike, saw and block grows by your size.\n"
                   "The dot crashes exactly when it touches a red border, so tight gaps are easy to see.\n"
                   "Turning this on hides the normal hitboxes (you can turn them back on).") &&
        c.hitBorders)
        c.hitboxes = false;
    if (ui::subBegin("##bordersub", c.hitBorders)) {
        ui::toggle("Path line", &c.hbPath, "A line that shows where the dot went.");
        if (ui::subBegin("##pathsub", c.hbPath)) {
            ui::sliderInt("Path length", &c.hbPathLen, 60, 4000, "How long the line is (240 = 1 second).");
            ui::subEnd();
        }
        ui::toggle("Show where I clicked", &c.hbClicks, "Little rings on the path where you clicked.");
        ui::toggle("Blocks too (blue)", &c.hbSolids);
        ui::toggle("Orbs, pads & portals too (green)", &c.hbSpecials);
        ui::sliderFloat("Line thickness", &c.borderWidth, 0.1f, 3.f, "%.2f");
        ui::sliderFloat("Fill strength", &c.hbBorderFill, 0.f, 1.f, "%.2f", "0 = only outlines, 1 = solid colors.");
        ui::toggle("Rainbow borders", &c.hbRainbow, "The borders slowly change color.");
        if (!c.hbRainbow) {
            ui::colorEdit("Spikes & saws", &c.hbHazard);
            ui::colorEdit("Blocks", &c.hbSolid);
            ui::colorEdit("Orbs, pads, portals", &c.hbSpecial);
        }
        ui::colorEdit("Player dot & path", &c.hbPlayer);
        ui::subEnd();
    }
    ui::keybind("Hit borders on/off key", &c.hitBordersKey);

    ui::section("Markers");
    ui::toggle("Death markers", &c.deathMarkers, "Draws an X everywhere you died (or got hit with noclip).");
    ui::toggle("Click markers", &c.clickMarkers, "Draws a dot everywhere you clicked.");
    if (ui::subBegin("##marksub", c.deathMarkers || c.clickMarkers)) {
        if (ui::button("Clear all markers##clrmarks", v2(-1.f, 0))) {
            visuals::clearDeathMarkers();
            overlay::toast("Markers cleared", overlay::Info);
        }
        ui::note("Markers also go away when you leave the level.");
        ui::subEnd();
    }

    ui::section("Player look");
    ui::toggle("Rainbow icon", &c.rainbow, "Turn off = colors come back next time you open a level.");
    if (ui::subBegin("##rainbowsub", c.rainbow)) {
        ui::sliderFloat("Rainbow speed", &c.rainbowSpeed, 0.05f, 3.f, "%.2f");
        ui::toggle("Rainbow trail too", &c.rainbowTrail);
        ui::subEnd();
    }
    ui::toggle("No wave trail", &c.noWaveTrail);
    ui::toggle("Solid wave trail", &c.solidWave, "Wave trail is not see-through.");
    ui::toggle("No player trail", &c.noTrail);
    ui::toggle("Hide player", &c.hidePlayer);

    ui::section("Level screen");
    ui::toggle("Hide attempt counter", &c.hideAttempts);
    ui::toggle("Hide percentage", &c.hidePercent);
    ui::toggle("Hide progress bar", &c.hideProgress);
    ui::toggle("Exact percentage", &c.accuratePercent, "Shows 54.37% instead of 54%.");
    if (ui::subBegin("##pctsub", c.accuratePercent)) {
        ui::sliderInt("Decimals", &c.percentDecimals, 0, 6);
        ui::subEnd();
    }
    (void)s;
}

// ---------------------------------------------------------------------------
//  Tab: Speed
// ---------------------------------------------------------------------------
void tabSpeed() {
    auto& c = cfg();
    float s = ui::sc();
    if (predictor::active()) ui::note("Speedhack and slow motion are paused while Predict is thinking.");
    ui::section("Speedhack");
    ui::toggle("Speedhack", &c.speedhack, "Makes the whole game faster or slower.");
    if (ui::subBegin("##speedsub", c.speedhack)) {
        ui::sliderFloat("Speed", &c.speed, 0.1f, 4.f, "%.2fx");
        static float const presets[] = {0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f};
        static char const* const names[] = {"0.25x", "0.5x", "0.75x", "1x", "1.5x", "2x", "3x"};
        float gap = ImGui::GetStyle().ItemSpacing.x;
        float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 6.f) / 7.f;
        for (int i = 0; i < 7; i++) {
            if (i) ImGui::SameLine();
            ImGui::PushID(i);
            if (ui::button(names[i], v2(bw, 0), std::fabs(c.speed - presets[i]) < 0.001f)) c.speed = presets[i];
            ImGui::PopID();
        }
        ui::toggle("Change music speed too", &c.speedAudio);
        ui::toggle("Only inside levels", &c.speedOnlyInLevel);
        ui::keybind("Speedhack on/off key", &c.speedKey);
        ui::subEnd();
    }

    ui::section("Slow motion");
    ui::toggle("Hold a key for slow motion", &c.slowmo, "While you hold the key the game slows down. Let go = normal.");
    if (ui::subBegin("##slowsub", c.slowmo)) {
        ui::keybind("Slow motion key", &c.slowmoKey);
        ui::sliderFloat("Slow speed", &c.slowmoSpeed, 0.05f, 0.95f, "%.2fx");
        ui::subEnd();
    }

    ui::section("Frames");
    ui::toggle("FPS unlock", &c.fpsBypass, "Lets the game run at more FPS. Turn off V-Sync in GD settings for this.");
    if (ui::subBegin("##fpssub", c.fpsBypass)) {
        ui::sliderFloat("Target FPS", &c.fpsValue, 30.f, 1000.f, "%.0f");
        ui::subEnd();
    }
    ui::toggle("Lock delta (smooth physics)", &c.lockDelta,
               "Every frame moves the game by exactly the same time. Helps bots and videos stay smooth.");
}

// ---------------------------------------------------------------------------
//  Tab: Render (video maker)
// ---------------------------------------------------------------------------
void ffmpegReport(render::Report const& r) {
    ui::status(r.ffmpegFound ? 2 : 0, "FFmpeg (needed for this renderer!)",
               r.ffmpegFound ? r.ffmpegPath.c_str() : "Not found. You need to install it - steps below.");
    if (r.ffmpegFound && !r.version.empty()) ui::status(2, "FFmpeg version", r.version.c_str());
    if (r.ffmpegFound) {
        for (int i = 0; i < render::kEncCount; i++) {
            bool cpu = (i == 0 || i == 4);
            ui::status(r.enc[i] ? 2 : (cpu ? 0 : 1), render::kEncLabels[i],
                       r.enc[i] ? "works on this PC" : (cpu ? "missing - get a full FFmpeg build" : "not available (optional)"));
        }
    }
}

void builtinReport(render::Report const& r) {
    ui::status(r.builtinOk ? 2 : 0, "Windows video encoder (built in)",
               r.builtinOk ? "ready - nothing to install" : "not found. Use the FFmpeg renderer instead.");
    ui::status(r.builtinGpu ? 2 : 1, "Graphics card encoding",
               r.builtinGpu ? "yes - extra fast" : "no - your CPU will do it (still works)");
}

// Video quality: either a simple quality setting or an exact bitrate.
void bitrateRows(bool ffmpeg) {
    auto& c = cfg();
    static char const* const modes[] = {"Quality", "Bitrate"};
    ui::segmented("##ratemode", &c.rRateMode, modes, 2);
    if (c.rRateMode == 0) {
        if (ffmpeg) {
            ui::sliderInt("Quality (lower = better)", &c.rQuality, 0, 51, "18 looks great. 0 = perfect but huge files.");
        } else {
            static char const* const q[] = {"Low", "Medium", "High"};
            ui::combo("Quality##bq", &c.rBQuality, q, 3, "Higher = nicer video, bigger file.");
        }
        return;
    }
    ui::sliderFloat("Bitrate", &c.rBitrate, 1.f, 1000.f, "%.1f Mbps",
                    "How much data each second of video gets. More = sharper video and a bigger file.\n"
                    "1080p 60 FPS: 8-12 is good, 20-40 is great. YouTube likes 12+.\n"
                    "4K: 50-100. 8K or 240+ FPS: 200-500. 1000 = almost perfect (giant files).");
    float s = ui::sc();
    float gap = ImGui::GetStyle().ItemSpacing.x;
    static float const presets[] = {6.f, 12.f, 25.f, 50.f, 100.f, 200.f, 350.f, 500.f, 1000.f};
    static char const* const names[] = {"6 Mbps",   "12 Mbps",  "25 Mbps",  "50 Mbps",  "100 Mbps",
                                        "200 Mbps", "350 Mbps", "500 Mbps", "1000 Mbps"};
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 2.f) / 3.f;
    for (int i = 0; i < 9; i++) {
        if (i % 3) ImGui::SameLine();
        ImGui::PushID(i + 900);
        if (ui::button(names[i], v2(bw, 0), std::fabs(c.rBitrate - presets[i]) < 0.05f)) c.rBitrate = presets[i];
        ImGui::PopID();
    }
    char b[160];
    std::snprintf(b, sizeof(b), "About %.0f MB for each minute of video.%s", static_cast<double>(c.rBitrate) * 7.5,
                  (!ffmpeg && c.rBitrate > 300.f) ? "\nThe built-in renderer stops at 300 Mbps." : "");
    ui::note(b);
}

void tabRender() {
    auto& c = cfg();
    float s = ui::sc();
    auto r = render::report();
    if (!g_autoChecked) {
        g_autoChecked = true;
        render::runCheck();
    }
    bool ffm = (c.rEngine == render::FFmpeg);

    ui::section("Pick a renderer");
    static char const* const engines[] = {"FFmpeg  (best)", "Built-in  (fast)"};
    if (render::active()) {
        ui::note("Stop the video first to switch renderer.");
    } else {
        ui::segmented("##engine", &c.rEngine, engines, 2);
    }
    ui::note(ffm ? "FFmpeg: best quality, any size up to 8K, up to 1000 FPS, and it adds the level's music. "
                   "You must install FFmpeg (free). The game looks slow while it renders - that's normal, "
                   "the video comes out perfectly smooth."
                 : "Built-in: uses Windows' own video maker, so there is nothing to install, and it records "
                   "while you play at normal speed. But it's worse: lower quality, no music, and the video is "
                   "the size of your game window.");

    ui::section("Setup report (what you need)");
    if (r.checking || !r.checked) {
        ui::note("Checking your PC...");
    } else {
        if (ffm) ffmpegReport(r);
        else builtinReport(r);
        ui::status(r.outputOk ? 2 : 0, "Output folder", r.outputDir.c_str());
        if (ffm) {
            if (auto pl = hacks::playLayer()) {
                std::string song = render::songPath(pl);
                ui::status(song.empty() ? 1 : 2, "This level's song",
                           song.empty() ? "not found - the video will be silent (optional)" : song.c_str());
            } else {
                ui::status(1, "Level song", "open a level to check it");
            }
        }
    }
    float gap = ImGui::GetStyle().ItemSpacing.x;
    if (ffm) {
        float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * 2.f) / 3.f;
        if (ui::button("Check again##rcheck", v2(bw, 0))) render::runCheck();
        ImGui::SameLine();
        if (ui::button("Open mod folder##rmod", v2(bw, 0))) render::openFolder(render::saveDirStr());
        ImGui::SameLine();
        if (ui::button("Get FFmpeg##rget", v2(bw, 0))) render::openUrl("https://ffmpeg.org/download.html#build-windows");

        if (r.checked && !r.ffmpegFound) {
            ui::note("How to install FFmpeg (it's free):\n"
                     "1. Press \"Get FFmpeg\". Pick a Windows build (gyan.dev or BtbN).\n"
                     "2. Download the \"essentials\" .zip and unzip it.\n"
                     "3. Press \"Open mod folder\" and copy bin\\ffmpeg.exe into it.\n"
                     "   (Or paste the full path to ffmpeg.exe in the box below.)\n"
                     "4. Press \"Check again\". The red X should turn into a green tick.\n"
                     "No time for that? Pick the Built-in renderer above.");
        }
        ui::inputText("FFmpeg path (optional)", c.ffmpegPath, sizeof(c.ffmpegPath), "auto-detect");
    } else {
        if (ui::button("Check again##rcheck2", v2(-1.f, 0))) render::runCheck();
    }

    ui::section("Video");
    if (ffm) {
        // the list is in size order; the saved number keeps "Custom" at 4 so old settings still work
        static char const* const res[] = {"1280 x 720 (720p)",  "1920 x 1080 (1080p)", "2560 x 1440 (1440p)",
                                          "3840 x 2160 (4K)",   "5120 x 2880 (5K)",    "7680 x 4320 (8K)",
                                          "Custom"};
        static int const resId[] = {0, 1, 2, 3, 5, 6, 4};
        int shown = 1;
        for (int i = 0; i < 7; i++)
            if (resId[i] == c.rResIdx) shown = i;
        if (ui::combo("Size", &shown, res, 7, "Bigger = sharper, but slower to make and a bigger file."))
            c.rResIdx = resId[util::clamp(shown, 0, 6)];
        if (ui::subBegin("##ressub", c.rResIdx == 4)) {
            ui::sliderInt("Width", &c.rW, 256, 8192);
            ui::sliderInt("Height", &c.rH, 256, 8192);
            ui::subEnd();
        }
        if (c.rResIdx == 5 || c.rResIdx == 6 || (c.rResIdx == 4 && (c.rW > 3840 || c.rH > 2160)))
            ui::note("Huge video! It takes a lot longer to make. Graphics-card H.264 can't go this big, "
                     "so pick H.265 or a CPU encoder (it switches for you if you forget).");
        static char const* const fps[] = {"30 FPS",  "60 FPS",  "120 FPS", "144 FPS",
                                          "240 FPS", "360 FPS", "480 FPS", "Custom"};
        ui::combo("Frame rate", &c.rFpsIdx, fps, 8);
        if (ui::subBegin("##fpssub", c.rFpsIdx == 7)) {
            ui::sliderInt("FPS", &c.rFpsCustom, 10, 1000);
            ui::subEnd();
        }
        int vf = render::fpsFor(c.rFpsIdx);
        if (vf > 240)
            ui::note("GD moves 240 times a second, so above 240 FPS some frames are copies of the last one. "
                     "It works, but it won't look smoother than 240.");

        std::vector<std::string> encLabels;
        std::vector<char const*> encPtrs;
        for (int i = 0; i < render::kEncCount; i++) {
            std::string l = render::kEncLabels[i];
            if (r.checked && !r.enc[i]) l += "  (not on this PC)";
            encLabels.push_back(l);
        }
        for (auto const& l : encLabels) encPtrs.push_back(l.c_str());
        ui::combo("Encoder", &c.rEncIdx, encPtrs.data(), render::kEncCount, "GPU encoders are much faster if you have one.");
        bitrateRows(true);
        if (c.rEncIdx == 0 || c.rEncIdx == 4) {
            static char const* const pre[] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow"};
            ui::combo("CPU speed", &c.rPresetIdx, pre, 7, "Faster = quicker render, slightly bigger file.");
        }
        ui::inputText("Extra FFmpeg options", c.rExtra, sizeof(c.rExtra), "for experts");
    } else {
        auto px = CCDirector::sharedDirector()->getWinSizeInPixels();
        char sz[96];
        std::snprintf(sz, sizeof(sz), "Size: same as your game window (%d x %d)",
                      static_cast<int>(px.width) & ~1, static_cast<int>(px.height) & ~1);
        ui::note(sz);
        bitrateRows(false);
        static char const* const bf[] = {"30 FPS", "60 FPS", "120 FPS", "144 FPS", "240 FPS"};
        ui::combo("Frame rate##bfps", &c.rBFpsIdx, bf, 5,
                  "120+ FPS needs a newer PC. If the video fails, pick a lower one.");
        ui::toggle("Real-time (no slow-down)", &c.rBRealtime,
                   "ON: records while you play at normal speed (fastest).\n"
                   "OFF: slows the game down so no frame is missed (smoother).");
    }

    ui::section("Sound & timing");
    if (ffm) {
        ui::toggle("Add sound", &c.rMusic);
        if (ui::subBegin("##musicsub", c.rMusic)) {
            static char const* const modes[] = {"Song file", "Record the game"};
            ui::segmented("##audiomode", &c.rAudioMode, modes, 2);
            if (c.rAudioMode == 0) {
                ui::note("Adds the level's song file. NONG songs (from the Jukebox mod) work too. Fast.");
                ui::sliderFloat("Music start (seconds)", &c.rMusicOffset, -5.f, 60.f, "%.2f s",
                                "Skip into the song. Negative = song starts later.");
            } else {
                ui::note("After the video is made, the macro plays the level once more at normal speed while "
                         "sketch records everything you'd hear: music, NONG songs, sound effects and clicks. "
                         "Then it puts that sound into the video.\n"
                         "Needs a macro playing (Bot tab). Don't touch anything while it records, and keep "
                         "your game volume up.");
                ui::sliderFloat("Sound timing", &c.rRecDelay, -0.5f, 0.5f, "%.2f s",
                                "If the sound is a little early or late in the video, nudge it here.\n"
                                "Bigger = sound comes earlier.");
            }
            ui::subEnd();
        }
    } else {
        ui::note("The built-in renderer makes silent videos. Want music? Use FFmpeg.");
    }
    ui::sliderFloat("Keep recording after win", &c.rEndPad, 0.f, 10.f, "%.1f s");
    ui::toggle("Restart the level when I press Start", &c.rRestart, "Closes the pause menu too, if it's open.");
    ui::toggle("Close this menu when the video starts", &c.rCloseMenu);
    ui::inputText("Save videos to", c.renderDir, sizeof(c.renderDir), "mod folder / renders");

    ui::spacing(4);
    bool on = render::active();
    float bh = ui::rowH() * 1.4f;
    if (render::audioPassActive()) {
        char pb[128];
        std::snprintf(pb, sizeof(pb), "Recording the sound... %.0f s. The level is playing once more - don't touch anything!",
                      render::audioPassSeconds());
        ui::note(pb);
        if (ui::button("Stop recording sound (use the song file)##rpass", v2(-1.f, bh))) render::cancelAudioPass();
    } else if (!on) {
        char const* lbl = ffm ? "Start video (FFmpeg)##rstart" : "Start video (built-in)##rstart";
        if (ui::button(lbl, v2(-1.f, bh), true)) render::start(hacks::playLayer(), c.rRestart);
    } else {
        if (ui::button("Stop & save video##rstop", v2(-1.f, bh), true)) render::stop(true);
        char b[160];
        double vs = render::videoSeconds();
        std::snprintf(b, sizeof(b), "%d frames  -  %.1f s of video  -  running %.0f s", render::frames(), vs, render::elapsed());
        ui::note(b);
    }
    if (ui::button("Open videos folder##rvids", v2(-1.f, 0))) render::openFolder(render::outputDir());
    ui::note("Tip: record or load a macro in the Bot tab and turn on \"Make a video while playing\" to get a perfect showcase.");
    ui::note("Labels and this menu are NOT in the video - only the game is.");
}

// ---------------------------------------------------------------------------
//  Tab: Labels
// ---------------------------------------------------------------------------
void tabLabels() {
    auto& c = cfg();
    ui::section("Show on screen");
    ui::toggle("Cheat indicator (dot)", &c.lblCheat, "Green = legit, yellow = cheats but safe mode, red = cheats.");
    ui::toggle("FPS", &c.lblFps);
    ui::toggle("Clicks per second", &c.lblCps);
    ui::toggle("Noclip accuracy", &c.lblNoclipAcc);
    ui::toggle("Noclip deaths", &c.lblNoclipDeaths);
    ui::toggle("Level name", &c.lblLevel);
    ui::toggle("Attempts (this session)", &c.lblAttempts);
    ui::toggle("Deaths (this session)", &c.lblDeaths);
    ui::toggle("Best run (this session)", &c.lblBest);
    ui::toggle("Attempt time", &c.lblTime);
    ui::toggle("Time played (this session)", &c.lblSession);
    ui::toggle("Jumps", &c.lblJumps);
    ui::toggle("Clock", &c.lblClock);
    ui::toggle("Speedhack speed", &c.lblSpeed);
    ui::toggle("Bot status", &c.lblBot);
    ui::toggle("Player position (debug)", &c.lblPlayer);

    ui::section("Fun extras");
    ui::toggle("FPS graph", &c.fpsGraph, "A little line that shows if the game stutters.");
    ui::toggle("Input display", &c.inputDisplay, "Shows when the jump button is held (great for videos).");
    if (ui::subBegin("##inputsub", c.inputDisplay)) {
        ui::sliderFloat("Input display size", &c.inputScale, 0.5f, 2.5f, "%.2fx");
        ui::subEnd();
    }

    ui::section("Where & how");
    static char const* const corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    ui::segmented("##corner", &c.lblCorner, corners, 4);
    ui::sliderFloat("Label size", &c.lblScale, 0.5f, 2.5f, "%.2fx");
    ui::sliderFloat("Label see-through", &c.lblAlpha, 0.1f, 1.f, "%.2f");
}

// ---------------------------------------------------------------------------
//  Tab: Extra
// ---------------------------------------------------------------------------
void tabExtra() {
    auto& c = cfg();
    float s = ui::sc();
    float gap = ImGui::GetStyle().ItemSpacing.x;
    ui::section("Game");
    ui::toggle("No screen fade", &c.noTransition, "Menus switch instantly.");
    ui::toggle("Unlock all icons & colors", &c.unlockAll, "Lets you pick any icon or color in the garage.");
    ui::toggle("Mute when I click away", &c.muteUnfocused, "The game goes quiet when it's not the window you're using.");

    ui::section("Screenshots");
    ui::keybind("Screenshot key", &c.screenshotKey);
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap) / 2.f;
    if (ui::button("Take one now##shotnow", v2(bw, 0))) {
        std::string path;
        if (render::screenshot(path)) overlay::toast("Screenshot saved", overlay::Good);
        else overlay::toast("Couldn't take a screenshot here.", overlay::Bad);
    }
    ImGui::SameLine();
    if (ui::button("Open screenshots##shotdir", v2(bw, 0))) {
        std::string dir = render::saveDirStr() + "\\screenshots";
        std::error_code ec;
        std::filesystem::create_directories(util::u8path(dir), ec);
        render::openFolder(dir);
    }
    ui::note("Screenshots only show the game - not this menu.");

    ui::section("Keys (in a level)");
    ui::keybind("Restart level", &c.restartKey);
    ui::keybind("Practice mode on/off", &c.practiceKey);

    ui::section("Menu");
    ui::keybind("Open / close menu", &c.menuKey);
    ui::note("You can also change it in Geode: Mods > sketch > Settings (the gear).");
    ui::toggle("Popup messages", &c.toasts);
    ui::toggle("Draw mouse cursor in levels", &c.softCursor, "GD hides the mouse in levels. This shows it while the menu is open.");

    ui::section("Settings file");
    if (ui::button("Save now##savenow", v2(bw, 0))) {
        saveConfig();
        overlay::toast("Settings saved", overlay::Good);
    }
    ImGui::SameLine();
    char const* resetLabel = g_resetClicks > 0 ? "Click again to reset!##reset" : "Reset everything##reset";
    if (ui::button(resetLabel, v2(bw, 0), g_resetClicks > 0)) {
        if (++g_resetClicks >= 2) {
            resetConfig();
            hacks::requestFontReload();
            g_resetClicks = 0;
            overlay::toast("Everything is back to default", overlay::Info);
        }
    }
    if (!ImGui::IsItemHovered() && g_resetClicks > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_resetClicks = 0;
    ui::note("Settings save by themselves every few seconds and when you close the menu.");
}

// ---------------------------------------------------------------------------
//  Tab: Themes
// ---------------------------------------------------------------------------
bool themeCard(char const* idStr, char const* name, ThemeColors const& tc, bool selected, float w, float h, int index) {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& L = theme::live();
    float s = ui::sc();
    float slide = 0.f;
    float en = ui::rowEnter(&slide);  // cards pop in one after another
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(idStr, v2(w, h));
    bool hov = ImGui::IsItemHovered();
    ImGuiID id = ImGui::GetID(idStr);
    float ha = ui::spring(id ^ 0xC0FFEEu, hov ? 1.f : 0.f, 240.f, 14.f);
    float sel = ui::spring(id ^ 0xBEEFu, selected ? 1.f : 0.f, 200.f, 12.f);
    float pop = util::clamp(en, 0.f, 1.3f);
    float k = 0.82f + 0.18f * pop;  // grows from 82% to 100% (bouncy)
    float lift = ha * 4.f * s;
    float r = cfg().rounding * s;
    ImVec2 ctr(p.x + w * 0.5f, p.y + h * 0.5f - lift);
    float hw = (w - 8.f * s) * 0.5f * k, hh = (h - 10.f * s) * 0.5f * k;
    ImVec2 a(ctr.x - hw, ctr.y - hh), b(ctr.x + hw, ctr.y + hh);
    float al = util::clamp(en * 1.6f, 0.f, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * al);

    dl->AddRectFilled(v2(a.x + 4.f * s, a.y + 5.f * s + lift * 0.6f), v2(b.x + 4.f * s, b.y + 5.f * s + lift * 0.6f),
                      theme::col(L.outline, 0.35f + 0.2f * util::clamp(ha, 0.f, 1.f)), r);
    dl->AddRectFilled(a, b, theme::col(L.field), r);

    // mini notebook preview in that theme's colors
    float px0 = a.x + 8.f * s, py0 = a.y + 8.f * s, px1 = b.x - 8.f * s, py1 = b.y - 24.f * s;
    float pw = px1 - px0, ph = py1 - py0;
    if (tc.glow) {
        for (int g = 3; g >= 1; g--)
            dl->AddRectFilled(v2(px0 + pw * 0.3f - g * 3.f * s, py0 - g * 3.f * s), v2(px1 + g * 3.f * s, py1 + g * 3.f * s),
                              theme::col(tc.accent, 0.07f), 6.f * s);
    }
    dl->AddRectFilled(v2(px0 + pw * 0.28f, py0 - 3.f * s), v2(px1 + 3.f * s, py1 + 4.f * s), theme::col(tc.blob), 8.f * s);
    for (int i = 0; i < 3; i++) {
        float ty = py0 + ph * 0.1f + static_cast<float>(i) * ph * 0.3f;
        ImVec2 t0(px0 + (i == 0 ? 0.f : 4.f * s), ty), t1(px0 + pw * 0.36f, ty + ph * 0.21f);
        dl->AddRectFilled(t0, t1, theme::col(i == 0 ? tc.tabActive : tc.tab), 2.f * s);
        dl->AddRect(t0, t1, theme::col(tc.outline), 2.f * s, 0, 1.4f * s);
    }
    ImVec2 q0(px0 + pw * 0.32f, py0), q1(px1, py1);
    dl->AddRectFilledMultiColor(q0, q1, theme::col(tc.panel), theme::col(tc.panel), theme::col(tc.panel2), theme::col(tc.panel2));
    ui::drawPattern(dl, q0, q1, tc, 0.45f * s, 0x7000u + static_cast<uint32_t>(index));
    dl->AddRect(q0, q1, theme::col(tc.glow ? tc.accent : tc.outline), 2.f * s, 0, 1.8f * s);
    for (int i = 0; i < 3; i++) {
        float ly = q0.y + (q1.y - q0.y) * (0.25f + static_cast<float>(i) * 0.25f);
        ui::squiggle(dl, v2(q0.x + 6.f * s, ly), (q1.x - q0.x) * 0.48f, theme::col(tc.text), 1.4f * s, 1.5f * s, static_cast<float>(i) * 1.3f);
        ImVec2 b0(q1.x - 15.f * s, ly - 4.5f * s), b1(q1.x - 6.f * s, ly + 4.5f * s);
        if (i != 1) dl->AddRectFilled(b0, b1, theme::col(tc.accent), 1.5f * s);
        dl->AddRect(b0, b1, theme::col(tc.outline), 1.5f * s, 0, 1.3f * s);
    }

    dl->PushClipRect(a, b, true);
    ui::text(dl, v2(a.x + 9.f * s, b.y - 21.f * s), theme::col(L.text), name);
    dl->PopClipRect();
    ui::sketchRect(dl, a, b, theme::col(theme::mix(L.outline, L.accent, util::clamp(sel, 0.f, 1.f))),
                   (cfg().outline + sel * 1.5f) * s * 0.85f, r, id);
    if (sel > 0.01f) {
        float e = util::clamp(sel, 0.f, 1.3f);
        ImVec2 cc(b.x - 11.f * s, a.y + 11.f * s);
        dl->AddCircleFilled(cc, 9.f * s * e, theme::col(L.accent));
        dl->AddCircle(cc, 9.f * s * e, theme::col(L.outline), 0, 2.f * s);
        if (e > 0.5f) ui::checkMark(dl, cc, 9.f * s, theme::col(L.accentText), 2.f * s, util::clamp((e - 0.5f) * 2.f, 0.f, 1.f));
    }
    ImGui::PopStyleVar();
    return pressed;
}

void tabThemes() {
    auto& c = cfg();
    float s = ui::sc();
    float gap = ImGui::GetStyle().ItemSpacing.x;
    int total = theme::customIndex() + 1;

    ui::section("Pick a theme");
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap) / 2.f;
    if (ui::button("Surprise me!##randtheme", v2(bw, 0), true)) {
        static std::mt19937 rng(std::random_device{}());
        int n = theme::customIndex();  // not "Custom"
        if (n > 1) {
            int pick = c.themeIndex;
            while (pick == c.themeIndex) pick = static_cast<int>(rng() % static_cast<unsigned>(n));
            c.themeIndex = pick;
            overlay::toast(std::string("Theme: ") + theme::nameOf(pick), overlay::Info);
        }
    }
    ImGui::SameLine();
    if (ui::button("Back to Sketch##deftheme", v2(bw, 0))) c.themeIndex = 0;

    int perRow = 4;
    float cw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    float ch = cw * 0.8f;
    for (int i = 0; i < total; i++) {
        if (i % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        if (themeCard("##theme", theme::nameOf(i), theme::colorsOf(i), c.themeIndex == i, cw, ch, i)) c.themeIndex = i;
        ImGui::PopID();
    }
    ui::note("The colors fade smoothly into the new theme.");

    ui::section("Make your own");
    if (ui::button("Copy the current theme into Custom##copytheme", v2(-1.f, 0))) {
        c.custom = theme::target();
        c.themeIndex = theme::customIndex();
    }
    if (ui::subBegin("##customsub", c.themeIndex == theme::customIndex())) {
        auto& t = c.custom;
        ui::colorEdit("Page (top)", &t.panel);
        ui::colorEdit("Page (bottom)", &t.panel2);
        ui::colorEdit("Paint behind the page", &t.blob);
        ui::colorEdit("Side tabs", &t.tab);
        ui::colorEdit("Picked tab", &t.tabActive);
        ui::colorEdit("Tab text", &t.tabText);
        ui::colorEdit("Marker lines", &t.outline);
        ui::colorEdit("Text", &t.text);
        ui::colorEdit("Hint text", &t.textDim);
        ui::colorEdit("Checkmarks & sliders", &t.accent);
        ui::colorEdit("Text on checkmarks", &t.accentText);
        ui::colorEdit("Boxes", &t.field);
        int pk = util::clamp(t.patternKind, 0, PatCount - 1);
        if (ui::combo("Page pattern", &pk, theme::kPatternNames, PatCount)) t.patternKind = pk;
        if (t.patternKind != PatNone) ui::colorEdit("Pattern color", &t.pattern);
        ui::toggle("Neon glow", &t.glow, "Lines glow a little, like neon signs.");
        ui::subEnd();
    } else {
        ui::note("Pick the Custom card above to edit its colors.");
    }

    ui::section("Look");
    ui::toggle("Hand-drawn lines", &c.sketchy, "Lines wobble a little, like your drawing.");
    if (ui::subBegin("##sketchsub", c.sketchy)) {
        ui::sliderFloat("Sketchiness", &c.sketchAmount, 0.2f, 2.5f, "%.2fx", "How wobbly and messy the pencil lines are.");
        ui::toggle("Double pencil lines", &c.doubleLines, "Every line is drawn twice, like a real sketch.");
        ui::toggle("Wiggly lines (cartoon boil)", &c.lineBoil, "Lines wiggle all the time, like an old cartoon.");
        ui::subEnd();
    }
    ui::toggle("Paint blob behind the page", &c.paintBlob);
    if (ui::subBegin("##blobsub", c.paintBlob)) {
        ui::toggle("Pencil shading on the paint", &c.pencilShading);
        ui::toggle("Little doodles", &c.doodles, "A star and a spiral drawn on the edges.");
        ui::subEnd();
    }
    ui::toggle("Tape on the corners", &c.tape);
    ui::sliderFloat("Marker thickness", &c.outline, 1.f, 6.f, "%.1f");
    ui::sliderFloat("Round corners", &c.rounding, 0.f, 16.f, "%.0f");
    int font = c.fontIndex;
    if (ui::combo("Font", &font, kFontNames, kFontCount)) {
        c.fontIndex = font;
        hacks::requestFontReload();
    }

    ui::section("Size & motion");
    ui::sliderFloat("Menu size", &c.uiScale, 0.6f, 2.f, "%.2fx");
    ui::sliderFloat("Animation speed", &c.animSpeed, 0.f, 3.f, "%.2fx", "0 = no animations");
    ui::sliderFloat("Bounciness", &c.bounce, 0.f, 2.f, "%.2f", "0 = smooth, 1 = bouncy, 2 = super bouncy!");
    ui::toggle("Rows slide in", &c.cascade, "When you open a tab, the rows slide in one after another.");
}

// ---------------------------------------------------------------------------
//  The notebook window
// ---------------------------------------------------------------------------
constexpr ImGuiID kOpenSpring = 0x5B0011u;
double g_openedAt = 0.0;

void smoothScroll() {
    auto& io = ImGui::GetIO();
    float s = ui::sc();
    float maxY = ImGui::GetScrollMaxY();
    float cur = ImGui::GetScrollY();
    if (std::fabs(cur - g_scrollCur) > 0.5f) {  // moved by the scrollbar
        g_scrollCur = cur;
        g_scrollTarget = cur;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.f) g_scrollTarget -= io.MouseWheel * 80.f * s;
    g_scrollTarget = util::clamp(g_scrollTarget, 0.f, maxY);
    float k = cfg().animSpeed <= 0.01f ? 1.f : 1.f - std::exp(-16.f * cfg().animSpeed * io.DeltaTime);
    g_scrollCur += (g_scrollTarget - g_scrollCur) * k;
    if (std::fabs(g_scrollTarget - g_scrollCur) < 0.5f) g_scrollCur = g_scrollTarget;
    ImGui::SetScrollY(g_scrollCur);
}

uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// -1..1, stable per seed
float wob(uint32_t seed) { return static_cast<float>(hashU(seed) % 2001u) / 1000.f - 1.f; }

// Rounded page with a top -> bottom color fade.
void pageFill(ImDrawList* dl, ImVec2 a, ImVec2 b, Col const& top, Col const& bot, float r) {
    float h = b.y - a.y;
    r = util::clamp(r, 0.f, h * 0.45f);
    if (r < 1.f) {
        dl->AddRectFilledMultiColor(a, b, theme::col(top), theme::col(top), theme::col(bot), theme::col(bot));
        return;
    }
    float f0 = r / h, f1 = 1.f - r / h;
    Col c0 = theme::mix(top, bot, f0), c1 = theme::mix(top, bot, f1);
    dl->AddRectFilled(a, v2(b.x, a.y + r + 0.5f), theme::col(top), r, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor(v2(a.x, a.y + r), v2(b.x, b.y - r), theme::col(c0), theme::col(c0), theme::col(c1), theme::col(c1));
    dl->AddRectFilled(v2(a.x, b.y - r - 0.5f), b, theme::col(bot), r, ImDrawFlags_RoundCornersBottom);
}

void drawMenu(float fade, float pop) {
    auto const& c = cfg();
    auto const& T = theme::live();
    float s = ui::sc();
    float sk = c.sketchy ? c.sketchAmount : 0.f;
    float now = static_cast<float>(ImGui::GetTime());
    float openAge = static_cast<float>(ImGui::GetTime() - g_openedAt) * util::maxv(c.animSpeed, 0.01f);
    if (c.animSpeed <= 0.01f) openAge = 10.f;

    ImVec2 size(780.f * s, 540.f * s);
    auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(v2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, v2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, util::clamp(fade, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, v2(0.f, 0.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##sketchbook", nullptr, flags);

    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float tabArea = 140.f * s;

    // the page bounces in: drops down a bit and grows from 90%
    float drop = (1.f - pop) * 40.f * s;
    float grow = (1.f - pop) * 0.10f;
    ImVec2 P0(wp.x + tabArea, wp.y + 14.f * s + drop);
    ImVec2 P1(wp.x + size.x - 16.f * s, wp.y + size.y - 14.f * s + drop);
    float gw = (P1.x - P0.x) * grow * 0.5f, gh = (P1.y - P0.y) * grow * 0.5f;
    P0 = v2(P0.x + gw, P0.y + gh);
    P1 = v2(P1.x - gw, P1.y - gh);
    float R = c.rounding * s;
    float OT = c.outline * s;
    float boil = c.lineBoil ? std::floor(now * 8.f) : 0.f;

    // 1) messy paint blob behind the page (like the grey scribble in the drawing)
    if (c.paintBlob) {
        ImVec2 b0(P0.x - 7.f * s, P0.y - 11.f * s), b1(P1.x + 12.f * s, P1.y + 10.f * s);
        dl->AddRectFilled(b0, b1, theme::col(T.blob), 30.f * s);
        dl->AddCircleFilled(v2(P1.x - 30.f * s, P0.y - 4.f * s), 26.f * s, theme::col(T.blob));
        dl->AddCircleFilled(v2(P0.x + 26.f * s, P1.y + 2.f * s), 22.f * s, theme::col(T.blob));
        dl->AddCircleFilled(v2(P1.x + 4.f * s, (P0.y + P1.y) * 0.55f), 20.f * s, theme::col(T.blob));
        if (c.pencilShading)
            ui::hatch(dl, v2(b0.x - 4.f * s, b0.y - 16.f * s), v2(b1.x + 14.f * s, b1.y + 14.f * s),
                      theme::col(T.outline, 0.16f), 7.f * s, 1.1f * s, 0x5AADu + static_cast<uint32_t>(boil));
    }

    // 2) side tabs: same size, spread evenly down the page
    float pageH = P1.y - P0.y;
    float tabGap = 7.f * s;
    float tabH = util::clamp((pageH - 30.f * s - tabGap * static_cast<float>(kTabCount - 1)) / static_cast<float>(kTabCount),
                             26.f * s, 46.f * s);
    float stackH = tabH * kTabCount + tabGap * static_cast<float>(kTabCount - 1);
    float ty0 = P0.y + (pageH - stackH) * 0.5f;
    float fs = ImGui::GetFontSize();
    for (int i = 0; i < kTabCount; i++) {
        float y = ty0 + static_cast<float>(i) * (tabH + tabGap);
        ImGui::SetCursorScreenPos(v2(wp.x + 4.f * s, y));
        ImGui::PushID(i);
        bool clicked = ImGui::InvisibleButton("##tab", v2(P0.x - wp.x - 4.f * s, tabH));
        bool hov = ImGui::IsItemHovered();
        ImGuiID id = ImGui::GetID("##tab");
        ImGui::PopID();
        if (clicked && g_tab != i) {
            g_tab = i;
            g_tabTime = ImGui::GetTime();
            g_scrollTarget = g_scrollCur = 0.f;
            ui::g_capturingKey = nullptr;
        }
        bool active = (g_tab == i);
        float out = ui::spring(id, active ? 1.f : (hov ? 0.45f : 0.f), 260.f, 13.f);  // picked tab pops out
        // tabs slide in one by one when the menu opens
        float enter = util::easeOutBack(util::clamp((openAge - 0.04f * static_cast<float>(i)) / 0.38f, 0.f, 1.f));
        float in = (1.f - enter) * 60.f * s;
        float wx = sk * 2.5f * s * wob(0x7AB0u + static_cast<uint32_t>(i));
        float x0 = wp.x + 20.f * s - out * 16.f * s + wx - in;
        ImVec2 a(x0, y), b(P0.x + 16.f * s, y + tabH);
        Col fill = theme::mix(T.tab, T.tabActive, util::clamp(out * 1.2f, 0.f, 1.f));
        dl->AddRectFilled(v2(a.x + 3.f * s, a.y + 4.f * s), v2(b.x, b.y + 4.f * s), theme::col(T.outline, 0.25f), R * 0.6f);
        dl->AddRectFilled(a, b, theme::col(fill), R * 0.6f);
        ui::sketchRect(dl, a, b, theme::col(T.outline), OT * 0.85f, R * 0.6f, 0x7AB0u + static_cast<uint32_t>(i));
        float ir = util::minv(7.f * s, tabH * 0.22f);
        ImVec2 ic(a.x + 19.f * s, y + tabH * 0.5f);
        drawIcon(dl, i, ic, ir * (1.f + 0.12f * util::clamp(out, 0.f, 1.2f)), theme::col(T.tabText), 1.8f * s);
        dl->PushClipRect(a, v2(P0.x, b.y), true);
        ui::text(dl, v2(a.x + 34.f * s + out * 3.f * s, y + (tabH - fs) * 0.5f), theme::col(T.tabText), kTabs[i]);
        dl->PopClipRect();
    }

    // 3) the page itself
    dl->AddRectFilled(v2(P0.x + 5.f * s, P0.y + 7.f * s), v2(P1.x + 5.f * s, P1.y + 7.f * s), theme::col(T.outline, 0.3f), R);
    pageFill(dl, P0, P1, T.panel, T.panel2, R);
    float inset = util::maxv(R * 0.4f, 2.f * s);
    ui::drawPattern(dl, v2(P0.x + inset, P0.y + inset), v2(P1.x - inset, P1.y - inset), T, s, 0xB00Bu);
    ui::sketchRect(dl, P0, P1, theme::col(T.outline), OT * 1.15f, R, 0xB00Cu);

    // bouncy marker on the page edge next to the picked tab
    // (springs in "tab slots" so it doesn't lag behind when the window moves)
    if (g_markerY < 0.f) {
        g_markerY = static_cast<float>(g_tab);
        ui::springSet(0x3A3Au, g_markerY);
    }
    g_markerY = util::maxv(0.f, ui::spring(0x3A3Au, static_cast<float>(g_tab), 180.f, 13.f));
    float my = ty0 + g_markerY * (tabH + tabGap) + tabH * 0.5f;
    dl->AddCircleFilled(v2(P0.x, my), 6.f * s, theme::col(T.accent));
    dl->AddCircle(v2(P0.x, my), 6.f * s, theme::col(T.outline), 0, 2.f * s);

    // 4) header: title that "writes itself" + close button
    float titleSize = fs * 1.55f;
    ImVec2 tp(P0.x + 24.f * s, P0.y + 14.f * s);
    char const* title = kTabTitles[g_tab];
    ImVec2 tsz = ui::textSize(title, nullptr, titleSize);
    float tEnter = util::clamp(enterTime() * util::maxv(c.animSpeed, 0.01f) / 0.35f, 0.f, 1.f);
    if (c.animSpeed <= 0.01f) tEnter = 1.f;
    float tb = util::easeOutBack(tEnter);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * util::clamp(tEnter * 2.f, 0.f, 1.f));
    ui::text(dl, v2(tp.x - (1.f - tb) * 16.f * s, tp.y), theme::col(T.text), title, nullptr, titleSize);
    ImGui::PopStyleVar();
    float phase = c.lineBoil ? now * 3.f : 0.7f;
    float write = util::easeOutCubic(util::clamp((enterTime() * util::maxv(c.animSpeed, 0.01f) - 0.1f) / 0.45f, 0.f, 1.f));
    if (c.animSpeed <= 0.01f) write = 1.f;
    ui::squiggle(dl, v2(tp.x, tp.y + titleSize + 5.f * s), (tsz.x + 30.f * s) * write, theme::col(T.text), 2.6f * s,
                 2.6f * s * (0.5f + 0.5f * util::maxv(sk, 0.4f)), phase);

    std::string hint = util::keyName(c.menuKey) + " to close";
    ImVec2 hs = ui::textSize(hint.c_str());
    float closeSz = 26.f * s;
    ImVec2 cb0(P1.x - 20.f * s - closeSz, P0.y + 16.f * s), cb1(cb0.x + closeSz, cb0.y + closeSz);
    float hintX = cb0.x - 12.f * s - hs.x;
    if (hintX > tp.x + tsz.x + 40.f * s)  // only if it doesn't bump into the title
        ui::text(dl, v2(hintX, cb0.y + (closeSz - fs) * 0.5f), theme::col(T.textDim), hint.c_str());
    ImGui::SetCursorScreenPos(cb0);
    bool closeHit = ImGui::InvisibleButton("##close", v2(closeSz, closeSz));
    ImGuiID cid = ImGui::GetID("##close");
    float ch = ui::spring(cid, ImGui::IsItemHovered() ? 1.f : 0.f, 260.f, 12.f);
    float cg = closeSz * 0.08f * ch;
    ImVec2 cc0(cb0.x - cg, cb0.y - cg), cc1(cb1.x + cg, cb1.y + cg);
    dl->AddRectFilled(cc0, cc1, theme::col(theme::mix(T.field, rgb(230, 70, 70), util::clamp(ch, 0.f, 1.f))), R * 0.5f);
    ui::sketchRect(dl, cc0, cc1, theme::col(T.outline), OT * 0.8f, R * 0.5f, 0xC105Eu);
    ui::crossMark(dl, v2((cb0.x + cb1.x) * 0.5f, (cb0.y + cb1.y) * 0.5f), closeSz * (0.55f + 0.1f * ch), theme::col(T.text), 2.4f * s);
    if (closeHit) toggle();

    // 5) little extras: tape and doodles
    if (c.tape) {
        ui::tape(dl, v2(P0.x + 2.f * s, P0.y + 3.f * s), -0.75f + 0.08f * wob(11), 52.f * s, 17.f * s);
        ui::tape(dl, v2(P1.x - 1.f * s, P1.y - 2.f * s), -0.72f + 0.08f * wob(12), 52.f * s, 17.f * s);
    }
    if (c.doodles && c.paintBlob) {
        float spin = c.lineBoil ? std::sin(now * 2.f) * 0.15f : 0.f;
        float tw = 1.f + 0.06f * std::sin(now * 2.6f);
        ui::doodleStar(dl, v2(P1.x - 64.f * s, P0.y - 4.f * s + spin * 10.f * s), 8.f * s * tw, theme::col(T.text, 0.85f), 1.8f * s);
        ui::doodleSpiral(dl, v2(P0.x + 26.f * s, P1.y + 6.f * s), 8.f * s, theme::col(T.text, 0.8f), 1.6f * s);
        dl->AddCircleFilled(v2(P1.x + 7.f * s, P0.y + pageH * 0.55f), 2.2f * s, theme::col(T.text, 0.7f));
        dl->AddCircleFilled(v2(P1.x + 3.f * s, P0.y + pageH * 0.55f + 9.f * s), 1.6f * s, theme::col(T.text, 0.7f));
    }

    // 6) content, sliding in when you switch tabs
    float te = util::clamp(enterTime() * 5.f * util::maxv(c.animSpeed, 0.01f), 0.f, 1.f);
    if (c.animSpeed <= 0.01f) te = 1.f;
    float tc = util::easeOutCubic(te);
    ImVec2 cpos(P0.x + 18.f * s + (1.f - tc) * 24.f * s, P0.y + 64.f * s);
    ImVec2 csize(P1.x - P0.x - 26.f * s, P1.y - P0.y - 74.f * s);
    ImGui::SetCursorScreenPos(cpos);
    ui::setEnterTime(enterTime());
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tc);
    ImGui::BeginChild("##content", csize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    smoothScroll();
    switch (g_tab) {
        case 0: tabBot(); break;
        case 1: tabPredict(); break;
        case 2: tabPlayer(); break;
        case 3: tabVisual(); break;
        case 4: tabSpeed(); break;
        case 5: tabRender(); break;
        case 6: tabLabels(); break;
        case 7: tabExtra(); break;
        default: tabThemes(); break;
    }
    ui::spacing(14);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace

// ---------------------------------------------------------------------------

void setup() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    std::string path = fontPath(cfg().fontIndex);
    ImFont* font = nullptr;
    std::error_code ec;
    if (!path.empty() && std::filesystem::exists(path, ec)) font = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.f);
    if (!font) font = io.Fonts->AddFontDefault();
    io.FontDefault = font;
    theme::snapToTarget();
}

void toggle() {
    g_open = !g_open;
    ui::g_capturingKey = nullptr;
    if (!g_open) {
        saveConfigIfChanged();
    } else {
        g_macrosDirty = true;
        if (g_openT <= 0.01f) {  // opening from fully closed: replay the bounce
            ui::springSet(kOpenSpring, 0.f);
            g_openedAt = ImGui::GetTime();
            g_tabTime = ImGui::GetTime();
        }
    }
}

bool isOpen() { return g_open; }

void draw() {
    auto& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    ui::beginFrame(dt);
    theme::update(dt);
    theme::applyStyle();

    overlay::draw(dt);

    // fade: quick and simple. pop: a bouncy spring that overshoots a little.
    float target = g_open ? 1.f : 0.f;
    float sp = cfg().animSpeed;
    if (sp <= 0.01f) g_openT = target;
    else if (g_openT < target) g_openT = util::minv(target, g_openT + dt * 6.f * sp);
    else if (g_openT > target) g_openT = util::maxv(target, g_openT - dt * 7.f * sp);
    float pop = ui::spring(kOpenSpring, target, 170.f, 12.f);

    bool inLevel = hacks::playLayer() != nullptr;
    io.MouseDrawCursor = g_openT > 0.f && inLevel && cfg().softCursor;
    if (g_openT <= 0.f) return;

    drawMenu(g_openT, g_open ? pop : util::maxv(pop, 0.6f));

    g_saveTimer += dt;
    if (g_saveTimer > 4.f) {
        g_saveTimer = 0.f;
        saveConfigIfChanged();
    }
}

}  // namespace gui
