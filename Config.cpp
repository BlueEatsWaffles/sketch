#include "Config.hpp"
#include "Util.hpp"

#include <Geode/Geode.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace geode::prelude;

MenuConfig& cfg() {
    static MenuConfig c;
    return c;
}

namespace {

template <class V>
void visitTheme(std::string const& p, ThemeColors& t, V& v) {
    v(p + "panel", t.panel);
    v(p + "blob", t.blob);
    v(p + "tab", t.tab);
    v(p + "tabActive", t.tabActive);
    v(p + "tabText", t.tabText);
    v(p + "outline", t.outline);
    v(p + "text", t.text);
    v(p + "textDim", t.textDim);
    v(p + "accent", t.accent);
    v(p + "accentText", t.accentText);
    v(p + "field", t.field);
}

template <class V>
void visitAll(MenuConfig& c, V& v) {
#define F(x) v(std::string(#x), c.x)
    // menu
    F(menuKey); F(uiScale); F(animSpeed); F(themeIndex); F(fontIndex); F(sketchy); F(lineBoil);
    F(paintBlob); F(outline); F(rounding); F(softCursor); F(toasts);
    // bot
    F(botMode); F(fixInterval); F(blockInputOnPlay); F(autoStopAtEnd); F(macroName); F(frameStepper);
    F(stepKey); F(stepToggleKey);
    // player
    F(noclip); F(noclipP1); F(noclipP2); F(deathFlash); F(noclipKey); F(safeMode); F(autoSafe);
    F(customRespawn); F(respawnDelay); F(jumpHack); F(autoclicker); F(acHold); F(acRelease); F(acP2);
    F(noDeathEffect); F(noSpawnEffect); F(hidePlayer); F(noUnfocusPause); F(noNewBest);
    // visual
    F(hitboxes); F(hitboxOnDeath); F(hitboxTrail); F(trailLen); F(hitBorders); F(borderWidth); F(hbFill);
    F(hbFillAlpha); F(hbHazard); F(hbSolid); F(hbSpecial); F(hbOther); F(hbPlayer); F(playerBox);
    F(rainbow); F(rainbowSpeed); F(rainbowTrail); F(noWaveTrail); F(solidWave); F(noTrail);
    F(hideAttempts); F(hidePercent); F(hideProgress); F(accuratePercent); F(percentDecimals);
    // speed
    F(speedhack); F(speed); F(speedAudio); F(speedOnlyInLevel); F(speedKey); F(fpsBypass); F(fpsValue);
    F(lockDelta);
    // labels
    F(lblFps); F(lblCps); F(lblNoclipAcc); F(lblNoclipDeaths); F(lblAttempts); F(lblBest); F(lblClock);
    F(lblTime); F(lblJumps); F(lblPlayer); F(lblBot); F(lblCheat); F(lblSpeed); F(lblCorner);
    F(lblScale); F(lblAlpha);
    // extra
    F(noTransition); F(unlockAll); F(restartKey); F(practiceKey);
    // render
    F(rEngine); F(rBQuality); F(rBFpsIdx); F(rBRealtime);
    F(ffmpegPath); F(renderDir); F(rResIdx); F(rW); F(rH); F(rFpsIdx); F(rEncIdx); F(rQuality);
    F(rPresetIdx); F(rExtra); F(rMusic); F(rMusicOffset); F(rEndPad); F(rRestart); F(rAutoWithBot);
#undef F
    visitTheme("custom.", c.custom, v);
}

struct Writer {
    std::string out;
    void line(std::string const& k, std::string const& val) {
        out += k;
        out += '=';
        out += val;
        out += '\n';
    }
    void operator()(std::string const& k, int& v) { line(k, std::to_string(v)); }
    void operator()(std::string const& k, float& v) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.6g", v);
        line(k, b);
    }
    void operator()(std::string const& k, bool& v) { line(k, v ? "1" : "0"); }
    void operator()(std::string const& k, Col& v) {
        char b[128];
        std::snprintf(b, sizeof(b), "%.5g,%.5g,%.5g,%.5g", v.r, v.g, v.b, v.a);
        line(k, b);
    }
    template <size_t N>
    void operator()(std::string const& k, char (&v)[N]) {
        line(k, std::string(v));
    }
};

struct Reader {
    std::unordered_map<std::string, std::string> kv;
    std::string const* get(std::string const& k) const {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    }
    void operator()(std::string const& k, int& v) {
        if (auto s = get(k)) v = std::atoi(s->c_str());
    }
    void operator()(std::string const& k, float& v) {
        if (auto s = get(k)) v = std::strtof(s->c_str(), nullptr);
    }
    void operator()(std::string const& k, bool& v) {
        if (auto s = get(k)) v = (*s == "1");
    }
    void operator()(std::string const& k, Col& v) {
        auto s = get(k);
        if (!s) return;
        float f[4] = {v.r, v.g, v.b, v.a};
        char const* p = s->c_str();
        for (int i = 0; i < 4; i++) {
            char* e = nullptr;
            float x = std::strtof(p, &e);
            if (e == p) break;
            f[i] = x;
            p = e;
            if (*p == ',') p++;
        }
        v = Col{f[0], f[1], f[2], f[3]};
    }
    template <size_t N>
    void operator()(std::string const& k, char (&v)[N]) {
        auto s = get(k);
        if (!s) return;
        size_t n = s->size() < N - 1 ? s->size() : N - 1;
        for (size_t i = 0; i < n; i++) v[i] = (*s)[i];
        v[n] = 0;
    }
};

std::filesystem::path configPath() {
    return Mod::get()->getSaveDir() / "config.txt";
}

std::string serialize() {
    Writer w;
    visitAll(cfg(), w);
    return w.out;
}

std::string& lastSaved() {
    static std::string s;
    return s;
}

void sanitize(MenuConfig& c) {
    using util::clamp;
    c.uiScale = clamp(c.uiScale, 0.6f, 2.0f);
    c.animSpeed = clamp(c.animSpeed, 0.f, 3.f);
    c.outline = clamp(c.outline, 1.f, 6.f);
    c.rounding = clamp(c.rounding, 0.f, 16.f);
    c.botMode = clamp(c.botMode, 0, 1);
    c.fixInterval = clamp(c.fixInterval, 1, 240);
    c.speed = clamp(c.speed, 0.01f, 100.f);
    c.fpsValue = clamp(c.fpsValue, 15.f, 2000.f);
    c.trailLen = clamp(c.trailLen, 2, 600);
    c.percentDecimals = clamp(c.percentDecimals, 0, 6);
    c.lblCorner = clamp(c.lblCorner, 0, 3);
    c.lblScale = clamp(c.lblScale, 0.5f, 2.5f);
    c.lblAlpha = clamp(c.lblAlpha, 0.1f, 1.f);
    c.acHold = clamp(c.acHold, 1, 240);
    c.acRelease = clamp(c.acRelease, 1, 240);
    c.rW = clamp(c.rW, 128, 7680);
    c.rH = clamp(c.rH, 128, 4320);
    c.rQuality = clamp(c.rQuality, 0, 51);
    c.respawnDelay = clamp(c.respawnDelay, 0.f, 5.f);
    c.rEngine = clamp(c.rEngine, 0, 1);
    c.rBQuality = clamp(c.rBQuality, 0, 2);
    c.rBFpsIdx = clamp(c.rBFpsIdx, 0, 1);
    c.rResIdx = clamp(c.rResIdx, 0, 4);
    c.rFpsIdx = clamp(c.rFpsIdx, 0, 4);
    c.rEncIdx = clamp(c.rEncIdx, 0, 5);
    c.rPresetIdx = clamp(c.rPresetIdx, 0, 6);
}

}  // namespace

void loadConfig() {
    std::ifstream in(configPath());
    if (!in) {
        lastSaved() = serialize();
        return;
    }
    Reader r;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        r.kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    visitAll(cfg(), r);
    sanitize(cfg());
    lastSaved() = serialize();
}

void saveConfig() {
    auto data = serialize();
    std::error_code ec;
    std::filesystem::create_directories(configPath().parent_path(), ec);
    std::ofstream out(configPath(), std::ios::binary | std::ios::trunc);
    if (!out) {
        log::warn("Sketchbook: could not write config file");
        return;
    }
    out << data;
    lastSaved() = data;
}

void saveConfigIfChanged() {
    if (serialize() != lastSaved()) saveConfig();
}

void resetConfig() {
    cfg() = MenuConfig{};
    saveConfig();
}
