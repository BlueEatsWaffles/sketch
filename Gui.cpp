#include "Gui.hpp"
#include "Bot.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "Overlay.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "Util.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

#include <cmath>
#include <cstdio>
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
float g_openT = 0.f;
int g_tab = 0;
float g_tabT = 1.f;
float g_markerY = -1.f;
float g_scrollCur = 0.f;
float g_scrollTarget = 0.f;
float g_saveTimer = 0.f;

std::vector<std::string> g_macros;
bool g_macrosDirty = true;
int g_selMacro = -1;
int g_resetClicks = 0;
bool g_autoChecked = false;

constexpr int kTabCount = 8;
char const* const kTabs[kTabCount] = {"Bot", "Player", "Visual", "Speed", "Render", "Labels", "Extra", "Themes"};
char const* const kTabTitles[kTabCount] = {"Bot & Replays",  "Player",        "Hitboxes & Visuals", "Speed",
                                           "Video Maker",    "Labels",        "Extra",              "Themes"};

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
        case 1: {  // player: GD cube
            dl->AddRect(v2(c.x - r, c.y - r), v2(c.x + r, c.y + r), col, 0.f, 0, th);
            dl->AddRectFilled(v2(c.x - r * 0.4f, c.y - r * 0.4f), v2(c.x + r * 0.4f, c.y + r * 0.4f), col);
            break;
        }
        case 2: {  // visual: eye
            for (int i = 0; i < 24; i++) {
                float a = 6.2831853f * static_cast<float>(i) / 24.f;
                dl->PathLineTo(v2(c.x + std::cos(a) * r, c.y + std::sin(a) * r * 0.55f));
            }
            dl->PathStroke(col, ImDrawFlags_Closed, th);
            dl->AddCircleFilled(c, r * 0.3f, col);
            break;
        }
        case 3: {  // speed: lightning bolt
            ImVec2 pts[6] = {
                v2(c.x + r * 0.25f, c.y - r),        v2(c.x - r * 0.55f, c.y + r * 0.1f), v2(c.x - r * 0.02f, c.y + r * 0.1f),
                v2(c.x - r * 0.25f, c.y + r),        v2(c.x + r * 0.55f, c.y - r * 0.15f), v2(c.x + r * 0.02f, c.y - r * 0.15f),
            };
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case 4: {  // render: camera
            dl->AddRect(v2(c.x - r, c.y - r * 0.6f), v2(c.x + r * 0.35f, c.y + r * 0.6f), col, r * 0.2f, 0, th);
            dl->AddTriangleFilled(v2(c.x + r * 0.35f, c.y), v2(c.x + r, c.y - r * 0.5f), v2(c.x + r, c.y + r * 0.5f), col);
            break;
        }
        case 5: {  // labels: note with lines
            dl->AddRect(v2(c.x - r * 0.8f, c.y - r), v2(c.x + r * 0.8f, c.y + r), col, r * 0.15f, 0, th);
            for (int i = -1; i <= 1; i++)
                dl->AddLine(v2(c.x - r * 0.45f, c.y + i * r * 0.45f), v2(c.x + r * 0.45f, c.y + i * r * 0.45f), col, th * 0.8f);
            break;
        }
        case 6: {  // extra: gear
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

    ui::section("Frame stepper");
    ui::toggle("Frame stepper (freeze the game)", &c.frameStepper,
               "Freezes the level. Each press of the step key moves it forward by 1 tick (1/240 s). "
               "Hold the key to keep going.");
    ui::keybind("Step 1 tick key", &c.stepKey);
    ui::keybind("Stepper on/off key", &c.stepToggleKey);
    if (ui::button("Step +1 tick##stepbtn", v2(-1.f, 0))) hacks::requestStep();
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
    ui::toggle("Hide player", &c.hidePlayer);
    ui::toggle("Don't pause when I click away", &c.noUnfocusPause, "The game keeps going when the window loses focus.");
    ui::toggle("No \"New best\" popup", &c.noNewBest);
}

// ---------------------------------------------------------------------------
//  Tab: Visual
// ---------------------------------------------------------------------------
void tabVisual() {
    auto& c = cfg();
    ui::section("Hitboxes");
    ui::toggle("Show hitboxes", &c.hitboxes, "Shows the invisible boxes the game uses to check if you hit something.");
    ui::toggle("Show hitboxes when I die", &c.hitboxOnDeath);
    ui::toggle("Hitbox trail", &c.hitboxTrail, "Leaves a trail of your hitbox behind you.");
    if (ui::subBegin("##trailsub", c.hitboxTrail)) {
        ui::sliderInt("Trail length", &c.trailLen, 5, 400);
        ui::subEnd();
    }
    ui::toggle("Highlight my own hitbox", &c.playerBox);

    ui::section("Hit borders");
    ui::toggle("Custom hit borders", &c.hitBorders, "Change the color and thickness of the hitbox lines.");
    if (ui::subBegin("##bordersub", c.hitBorders)) {
        ui::sliderFloat("Border thickness", &c.borderWidth, 0.1f, 3.f, "%.2f");
        ui::toggle("Fill the boxes", &c.hbFill);
        if (c.hbFill) ui::sliderFloat("Fill see-through", &c.hbFillAlpha, 0.f, 1.f, "%.2f");
        ui::colorEdit("Spikes & saws (kill you)", &c.hbHazard);
        ui::colorEdit("Blocks (solid)", &c.hbSolid);
        ui::colorEdit("Orbs, pads, portals", &c.hbSpecial);
        ui::colorEdit("Everything else", &c.hbOther);
        ui::colorEdit("Player", &c.hbPlayer);
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

    ui::section("Level screen");
    ui::toggle("Hide attempt counter", &c.hideAttempts);
    ui::toggle("Hide percentage", &c.hidePercent);
    ui::toggle("Hide progress bar", &c.hideProgress);
    ui::toggle("Exact percentage", &c.accuratePercent, "Shows 54.37% instead of 54%.");
    if (ui::subBegin("##pctsub", c.accuratePercent)) {
        ui::sliderInt("Decimals", &c.percentDecimals, 0, 6);
        ui::subEnd();
    }
}

// ---------------------------------------------------------------------------
//  Tab: Speed
// ---------------------------------------------------------------------------
void tabSpeed() {
    auto& c = cfg();
    float s = ui::sc();
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
    ui::note(ffm ? "FFmpeg: best quality, any size up to 4K, and it adds the level's music. "
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
        static char const* const res[] = {"1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160 (4K)", "Custom"};
        ui::combo("Size", &c.rResIdx, res, 5);
        if (ui::subBegin("##ressub", c.rResIdx == 4)) {
            ui::sliderInt("Width", &c.rW, 256, 7680);
            ui::sliderInt("Height", &c.rH, 256, 4320);
            ui::subEnd();
        }
        static char const* const fps[] = {"30 FPS", "60 FPS", "120 FPS", "144 FPS", "240 FPS"};
        ui::combo("Frame rate", &c.rFpsIdx, fps, 5);

        std::vector<std::string> encLabels;
        std::vector<char const*> encPtrs;
        for (int i = 0; i < render::kEncCount; i++) {
            std::string l = render::kEncLabels[i];
            if (r.checked && !r.enc[i]) l += "  (not on this PC)";
            encLabels.push_back(l);
        }
        for (auto const& l : encLabels) encPtrs.push_back(l.c_str());
        ui::combo("Encoder", &c.rEncIdx, encPtrs.data(), render::kEncCount, "GPU encoders are much faster if you have one.");
        ui::sliderInt("Quality (lower = better)", &c.rQuality, 0, 51, "18 looks great. 0 = perfect but huge files.");
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
        static char const* const q[] = {"Low", "Medium", "High"};
        ui::combo("Quality##bq", &c.rBQuality, q, 3, "Higher = nicer video, bigger file.");
        static char const* const bf[] = {"30 FPS", "60 FPS"};
        ui::combo("Frame rate##bfps", &c.rBFpsIdx, bf, 2);
        ui::toggle("Real-time (no slow-down)", &c.rBRealtime,
                   "ON: records while you play at normal speed (fastest).\n"
                   "OFF: slows the game down so no frame is missed (smoother).");
    }

    ui::section("Sound & timing");
    if (ffm) {
        ui::toggle("Add the level's music", &c.rMusic);
        if (ui::subBegin("##musicsub", c.rMusic)) {
            ui::sliderFloat("Music start (seconds)", &c.rMusicOffset, -5.f, 60.f, "%.2f s",
                            "Skip into the song. Negative = song starts later.");
            ui::subEnd();
        }
    } else {
        ui::note("The built-in renderer makes silent videos. Want music? Use FFmpeg.");
    }
    ui::sliderFloat("Keep recording after win", &c.rEndPad, 0.f, 10.f, "%.1f s");
    ui::toggle("Restart the level when I press Start", &c.rRestart);
    ui::inputText("Save videos to", c.renderDir, sizeof(c.renderDir), "mod folder / renders");

    ui::spacing(4);
    bool on = render::active();
    float bh = ui::rowH() * 1.4f;
    if (!on) {
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
    ui::toggle("Attempts (this session)", &c.lblAttempts);
    ui::toggle("Best run (this session)", &c.lblBest);
    ui::toggle("Attempt time", &c.lblTime);
    ui::toggle("Jumps", &c.lblJumps);
    ui::toggle("Clock", &c.lblClock);
    ui::toggle("Speedhack speed", &c.lblSpeed);
    ui::toggle("Bot status", &c.lblBot);
    ui::toggle("Player position (debug)", &c.lblPlayer);

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
    ui::section("Game");
    ui::toggle("No screen fade", &c.noTransition, "Menus switch instantly.");
    ui::toggle("Unlock all icons & colors", &c.unlockAll, "Lets you pick any icon or color in the garage.");

    ui::section("Keys (in a level)");
    ui::keybind("Restart level", &c.restartKey);
    ui::keybind("Practice mode on/off", &c.practiceKey);

    ui::section("Menu");
    ui::keybind("Open / close menu", &c.menuKey);
    ui::toggle("Popup messages", &c.toasts);
    ui::toggle("Draw mouse cursor in levels", &c.softCursor, "GD hides the mouse in levels. This shows it while the menu is open.");

    ui::section("Settings file");
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float bw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap) / 2.f;
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
bool themeCard(char const* idStr, char const* name, ThemeColors const& tc, bool selected, float w, float h) {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& L = theme::live();
    float s = ui::sc();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(idStr, v2(w, h));
    ImGuiID id = ImGui::GetID(idStr);
    float ha = ui::anim(id ^ 0xC0FFEEu, ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
    float sel = ui::anim(id ^ 0xBEEFu, selected ? 1.f : 0.f, 12.f);
    float lift = ha * 4.f * s;
    float r = cfg().rounding * s;
    ImVec2 a(p.x + 2.f * s, p.y + 4.f * s - lift), b(p.x + w - 6.f * s, p.y + h - 6.f * s - lift);

    dl->AddRectFilled(v2(a.x + 4.f * s, a.y + 5.f * s + lift * 0.6f), v2(b.x + 4.f * s, b.y + 5.f * s + lift * 0.6f),
                      theme::col(L.outline, 0.35f + 0.2f * ha), r);
    dl->AddRectFilled(a, b, theme::col(L.field), r);

    // mini notebook preview in that theme's colors
    float px0 = a.x + 10.f * s, py0 = a.y + 10.f * s, px1 = b.x - 10.f * s, py1 = b.y - 26.f * s;
    float pw = px1 - px0, ph = py1 - py0;
    dl->AddRectFilled(v2(px0 + pw * 0.28f, py0 - 3.f * s), v2(px1 + 3.f * s, py1 + 4.f * s), theme::col(tc.blob), 9.f * s);
    for (int i = 0; i < 3; i++) {
        float ty = py0 + ph * 0.12f + static_cast<float>(i) * ph * 0.28f;
        ImVec2 t0(px0 + (i == 1 ? 5.f * s : 0.f), ty), t1(px0 + pw * 0.36f, ty + ph * 0.19f);
        dl->AddRectFilled(t0, t1, theme::col(i == 0 ? tc.tabActive : tc.tab), 2.f * s);
        dl->AddRect(t0, t1, theme::col(tc.outline), 2.f * s, 0, 1.5f * s);
    }
    ImVec2 q0(px0 + pw * 0.32f, py0), q1(px1, py1);
    dl->AddRectFilled(q0, q1, theme::col(tc.panel), 3.f * s);
    dl->AddRect(q0, q1, theme::col(tc.outline), 3.f * s, 0, 2.f * s);
    for (int i = 0; i < 3; i++) {
        float ly = q0.y + (q1.y - q0.y) * (0.25f + static_cast<float>(i) * 0.25f);
        ui::squiggle(dl, v2(q0.x + 7.f * s, ly), (q1.x - q0.x) * 0.5f, theme::col(tc.text), 1.5f * s, 1.6f * s, static_cast<float>(i) * 1.3f);
        ImVec2 b0(q1.x - 17.f * s, ly - 5.f * s), b1(q1.x - 7.f * s, ly + 5.f * s);
        if (i != 1) dl->AddRectFilled(b0, b1, theme::col(tc.accent), 1.5f * s);
        dl->AddRect(b0, b1, theme::col(tc.outline), 1.5f * s, 0, 1.5f * s);
    }

    ui::text(dl, v2(a.x + 10.f * s, b.y - 22.f * s), theme::col(L.text), name);
    ui::sketchRect(dl, a, b, theme::col(theme::mix(L.outline, L.accent, sel)), (cfg().outline + sel * 1.5f) * s * 0.9f, r, id);
    if (sel > 0.01f) {
        float e = util::easeOutBack(sel);
        ImVec2 cc(b.x - 12.f * s, a.y + 12.f * s);
        dl->AddCircleFilled(cc, 10.f * s * e, theme::col(L.accent));
        dl->AddCircle(cc, 10.f * s * e, theme::col(L.outline), 0, 2.f * s);
        if (e > 0.5f) ui::checkMark(dl, cc, 10.f * s, theme::col(L.accentText), 2.f * s);
    }
    return pressed;
}

void tabThemes() {
    auto& c = cfg();
    float s = ui::sc();
    ui::section("Pick a theme");
    int total = theme::customIndex() + 1;
    int perRow = 3;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float cw = (ImGui::GetContentRegionAvail().x - 8.f * s - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    float ch = cw * 0.68f;
    for (int i = 0; i < total; i++) {
        if (i % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        if (themeCard("##theme", theme::nameOf(i), theme::colorsOf(i), c.themeIndex == i, cw, ch)) c.themeIndex = i;
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
        ui::colorEdit("Page", &t.panel);
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
        ui::subEnd();
    } else {
        ui::note("Pick the Custom card above to edit its colors.");
    }

    ui::section("Look");
    ui::toggle("Hand-drawn lines", &c.sketchy, "Lines wobble a little, like your drawing.");
    ui::toggle("Wiggly lines (cartoon boil)", &c.lineBoil, "Lines wiggle all the time, like an old cartoon.");
    ui::toggle("Paint blob behind the page", &c.paintBlob);
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
}

// ---------------------------------------------------------------------------
//  The notebook window
// ---------------------------------------------------------------------------
void smoothScroll() {
    auto& io = ImGui::GetIO();
    float s = ui::sc();
    float maxY = ImGui::GetScrollMaxY();
    float cur = ImGui::GetScrollY();
    if (std::fabs(cur - g_scrollCur) > 0.5f) {  // moved by the scrollbar
        g_scrollCur = cur;
        g_scrollTarget = cur;
    }
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.f) g_scrollTarget -= io.MouseWheel * 80.f * s;
    g_scrollTarget = util::clamp(g_scrollTarget, 0.f, maxY);
    float k = cfg().animSpeed <= 0.01f ? 1.f : 1.f - std::exp(-16.f * cfg().animSpeed * io.DeltaTime);
    g_scrollCur += (g_scrollTarget - g_scrollCur) * k;
    if (std::fabs(g_scrollTarget - g_scrollCur) < 0.5f) g_scrollCur = g_scrollTarget;
    ImGui::SetScrollY(g_scrollCur);
}

void drawMenu(float t) {
    auto const& c = cfg();
    auto const& T = theme::live();
    float s = ui::sc();
    float e = g_open ? util::easeOutBack(t) : util::easeOutCubic(t);
    float alpha = util::clamp(t * 1.5f, 0.f, 1.f);

    ImVec2 size(740.f * s, 500.f * s);
    auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(v2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, v2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, v2(0.f, 0.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##sketchbook", nullptr, flags);

    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float slide = (1.f - e) * 34.f * s;
    float grow = (1.f - e) * 0.05f;  // page "pops" in
    float tabArea = 150.f * s;

    ImVec2 P0(wp.x + tabArea, wp.y + 14.f * s + slide);
    ImVec2 P1(wp.x + size.x - 16.f * s, wp.y + size.y - 16.f * s + slide);
    float gw = (P1.x - P0.x) * grow * 0.5f, gh = (P1.y - P0.y) * grow * 0.5f;
    P0 = v2(P0.x + gw, P0.y + gh);
    P1 = v2(P1.x - gw, P1.y - gh);
    float R = c.rounding * s;
    float OT = c.outline * s;

    // 1) messy paint blob behind the page (like the grey scribble in the drawing)
    if (c.paintBlob) {
        dl->AddRectFilled(v2(P0.x - 6.f * s, P0.y - 11.f * s), v2(P1.x + 12.f * s, P1.y + 10.f * s), theme::col(T.blob), 30.f * s);
        dl->AddCircleFilled(v2(P1.x - 30.f * s, P0.y - 4.f * s), 26.f * s, theme::col(T.blob));
        dl->AddCircleFilled(v2(P0.x + 26.f * s, P1.y + 2.f * s), 22.f * s, theme::col(T.blob));
    }

    // 2) side tabs (behind the page)
    float tabH = 42.f * s, tabGap = 9.f * s;
    float ty0 = P0.y + 46.f * s;
    static float const wobble[kTabCount] = {0.f, 7.f, -6.f, 3.f, -4.f, 8.f, -2.f, 5.f};
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
            g_tabT = 0.f;
            g_scrollTarget = g_scrollCur = 0.f;
            ui::g_capturingKey = nullptr;
        }
        bool active = (g_tab == i);
        float out = ui::anim(id, active ? 1.f : (hov ? 0.5f : 0.f), 13.f);
        float wob = c.sketchy ? wobble[i] * s : 0.f;
        float x0 = wp.x + 22.f * s - out * 18.f * s + wob;
        ImVec2 a(x0, y), b(P0.x + 16.f * s, y + tabH);
        Col fill = theme::mix(T.tab, T.tabActive, util::clamp(out * 1.2f, 0.f, 1.f));
        dl->AddRectFilled(a, b, theme::col(fill), R * 0.6f);
        ui::sketchRect(dl, a, b, theme::col(T.outline), OT * 0.9f, R * 0.6f, 0x7AB0u + static_cast<uint32_t>(i));
        float fs = ImGui::GetFontSize();
        ImVec2 ic(a.x + 20.f * s, y + tabH * 0.5f);
        drawIcon(dl, i, ic, 7.f * s, theme::col(T.tabText), 1.8f * s);
        ui::text(dl, v2(a.x + 36.f * s + out * 3.f * s, y + (tabH - fs) * 0.5f), theme::col(T.tabText), kTabs[i]);
    }

    // 3) the page itself
    dl->AddRectFilled(P0, P1, theme::col(T.panel), R);
    ui::sketchRect(dl, P0, P1, theme::col(T.outline), OT * 1.15f, R, 0xB00Cu);

    // sliding marker on the page edge next to the picked tab
    float targetY = ty0 + static_cast<float>(g_tab) * (tabH + tabGap) + tabH * 0.5f;
    if (g_markerY < 0.f) g_markerY = targetY;
    float mk = cfg().animSpeed <= 0.01f ? 1.f : 1.f - std::exp(-14.f * cfg().animSpeed * ImGui::GetIO().DeltaTime);
    g_markerY += (targetY - g_markerY) * mk;
    dl->AddCircleFilled(v2(P0.x, g_markerY), 6.f * s, theme::col(T.accent));
    dl->AddCircle(v2(P0.x, g_markerY), 6.f * s, theme::col(T.outline), 0, 2.f * s);

    // 4) header
    float fs = ImGui::GetFontSize();
    float titleSize = fs * 1.55f;
    ImVec2 tp(P0.x + 24.f * s, P0.y + 14.f * s);
    char const* title = kTabTitles[g_tab];
    ImVec2 tsz = ui::textSize(title, nullptr, titleSize);
    ui::text(dl, tp, theme::col(T.text), title, nullptr, titleSize);
    float phase = c.lineBoil ? static_cast<float>(ImGui::GetTime()) * 3.f : 0.7f;
    ui::squiggle(dl, v2(tp.x, tp.y + titleSize + 5.f * s), tsz.x + 30.f * s, theme::col(T.text), 2.6f * s, 2.6f * s, phase);

    std::string hint = util::keyName(c.menuKey) + " to close";
    ImVec2 hs = ui::textSize(hint.c_str());
    float closeSz = 26.f * s;
    ImVec2 cb0(P1.x - 20.f * s - closeSz, P0.y + 16.f * s), cb1(cb0.x + closeSz, cb0.y + closeSz);
    float hintX = cb0.x - 12.f * s - hs.x;
    if (hintX > tp.x + tsz.x + 40.f * s)  // only if it doesn't bump into the title
        ui::text(dl, v2(hintX, cb0.y + (closeSz - fs) * 0.5f), theme::col(T.textDim), hint.c_str());
    ImGui::SetCursorScreenPos(cb0);
    bool closeHit = ImGui::InvisibleButton("##close", v2(closeSz, closeSz));
    float ch = ui::anim(ImGui::GetID("##close"), ImGui::IsItemHovered() ? 1.f : 0.f, 14.f);
    dl->AddRectFilled(cb0, cb1, theme::col(theme::mix(T.field, rgb(230, 70, 70), ch)), R * 0.5f);
    ui::sketchRect(dl, cb0, cb1, theme::col(T.outline), OT * 0.8f, R * 0.5f, 0xC105Eu);
    ui::crossMark(dl, v2((cb0.x + cb1.x) * 0.5f, (cb0.y + cb1.y) * 0.5f), closeSz * 0.55f, theme::col(T.text), 2.4f * s);
    if (closeHit) toggle();

    // 5) content, sliding in when you switch tabs
    g_tabT = util::minv(1.f, g_tabT + ImGui::GetIO().DeltaTime * 5.f * util::maxv(c.animSpeed, 0.01f));
    if (c.animSpeed <= 0.01f) g_tabT = 1.f;
    float te = util::easeOutCubic(g_tabT);
    ImVec2 cpos(P0.x + 18.f * s + (1.f - te) * 28.f * s, P0.y + 64.f * s);
    ImVec2 csize(P1.x - P0.x - 26.f * s, P1.y - P0.y - 74.f * s);
    ImGui::SetCursorScreenPos(cpos);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * te);
    ImGui::BeginChild("##content", csize, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    smoothScroll();
    switch (g_tab) {
        case 0: tabBot(); break;
        case 1: tabPlayer(); break;
        case 2: tabVisual(); break;
        case 3: tabSpeed(); break;
        case 4: tabRender(); break;
        case 5: tabLabels(); break;
        case 6: tabExtra(); break;
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
    if (!g_open) saveConfigIfChanged();
    else g_macrosDirty = true;
}

bool isOpen() { return g_open; }

void draw() {
    auto& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    ui::beginFrame(dt);
    theme::update(dt);
    theme::applyStyle();

    overlay::draw(dt);

    float target = g_open ? 1.f : 0.f;
    float sp = cfg().animSpeed;
    if (sp <= 0.01f) g_openT = target;
    else if (g_openT < target) g_openT = util::minv(target, g_openT + dt * 4.5f * sp);
    else if (g_openT > target) g_openT = util::maxv(target, g_openT - dt * 6.f * sp);

    bool inLevel = hacks::playLayer() != nullptr;
    io.MouseDrawCursor = g_openT > 0.f && inLevel && cfg().softCursor;
    if (g_openT <= 0.f) return;

    drawMenu(g_openT);

    g_saveTimer += dt;
    if (g_saveTimer > 4.f) {
        g_saveTimer = 0.f;
        saveConfigIfChanged();
    }
}

}  // namespace gui
