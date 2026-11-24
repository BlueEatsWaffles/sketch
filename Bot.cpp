#include "Bot.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "Overlay.hpp"
#include "Render.hpp"
#include "Util.hpp"

#include <algorithm>
#include <fstream>

using namespace geode::prelude;

namespace bot {

namespace {

State g_state = State::Idle;
Macro g_macro;
bool g_injecting = false;
size_t g_idx = 0;
size_t g_fixIdx = 0;

PlayLayer* currentPlayLayer() { return GameManager::sharedState()->m_playLayer; }

PState grab(PlayerObject* p) {
    PState s;
    if (!p) return s;
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.rot = p->getRotation();
    s.yVel = p->m_yVelocity;
    return s;
}

void put(PlayerObject* p, PState const& s) {
    if (!p || p->m_isDead) return;
    p->setPosition(ccp(s.x, s.y));
    p->setRotation(s.rot);
    p->m_yVelocity = s.yVel;
}

void releaseAll(GJBaseGameLayer* g) {
    for (int b = 1; b <= 3; b++) {
        inject(g, false, b, true);
        inject(g, false, b, false);
    }
}

// --- tiny binary writer / reader ------------------------------------------
template <class T>
void wr(std::ofstream& o, T const& v) {
    o.write(reinterpret_cast<char const*>(&v), sizeof(T));
}
template <class T>
bool rd(std::ifstream& i, T& v) {
    return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

void wrState(std::ofstream& o, PState const& s) {
    wr(o, s.x);
    wr(o, s.y);
    wr(o, s.rot);
    wr(o, s.yVel);
}
bool rdState(std::ifstream& i, PState& s) {
    return rd(i, s.x) && rd(i, s.y) && rd(i, s.rot) && rd(i, s.yVel);
}

constexpr uint32_t kMagic = 0x434D4B53;  // "SKMC"
constexpr uint32_t kVersion = 1;

}  // namespace

State state() { return g_state; }
Macro const& macro() { return g_macro; }
bool injecting() { return g_injecting; }

uint32_t frame(GJBaseGameLayer* g) { return g ? g->m_gameState.m_currentProgress : 0u; }

uint32_t lastFrame() {
    uint32_t f = 0;
    if (!g_macro.inputs.empty()) f = g_macro.inputs.back().frame;
    if (!g_macro.fixes.empty()) f = util::maxv(f, g_macro.fixes.back().frame);
    return f;
}

void inject(GJBaseGameLayer* g, bool down, int button, bool p1) {
    if (!g) return;
    g_injecting = true;
    g->handleButton(down, button, p1);
    g_injecting = false;
}

void startRecord() {
    g_macro = Macro{};
    g_state = State::Record;
    if (auto pl = currentPlayLayer()) {
        if (pl->m_level) g_macro.level = pl->m_level->m_levelName.c_str();
        hacks::restartFromStart(pl);
        overlay::toast("Recording! Play the level, then save it in the Bot tab.", overlay::Good);
    } else {
        overlay::toast("Recording will start when you enter a level.", overlay::Info);
    }
}

void startPlay() {
    if (g_macro.inputs.empty() && g_macro.fixes.empty()) {
        overlay::toast("No macro loaded. Record one or load one first.", overlay::Warn);
        return;
    }
    g_state = State::Play;
    g_idx = 0;
    g_fixIdx = 0;
    if (auto pl = currentPlayLayer()) {
        hacks::restartFromStart(pl);
        if (cfg().rAutoWithBot) render::start(pl, false);
        overlay::toast("Playing macro", overlay::Good);
    } else {
        overlay::toast("Playback will start when you enter a level.", overlay::Info);
    }
}

void stop() {
    auto was = g_state;
    g_state = State::Idle;
    if (auto pl = currentPlayLayer()) {
        if (was == State::Play) releaseAll(pl);
    }
    if (was == State::Record)
        overlay::toast("Recording stopped. Don't forget to save it!", overlay::Info);
}

void record(GJBaseGameLayer* g, bool down, int button, bool p1) {
    if (g_state != State::Record) return;
    Input in;
    in.frame = frame(g);
    in.button = static_cast<uint8_t>(button);
    in.down = down;
    in.p1 = p1;
    g_macro.inputs.push_back(in);
}

void beforeTick(GJBaseGameLayer* g) {
    if (g_state != State::Play) return;
    uint32_t f = frame(g);
    auto& in = g_macro.inputs;
    while (g_idx < in.size() && in[g_idx].frame <= f) {
        Input const& i = in[g_idx++];
        inject(g, i.down, i.button, i.p1);
    }
}

void afterTick(GJBaseGameLayer* g) {
    if (cfg().botMode != 1) return;
    uint32_t f = frame(g);
    auto& fx = g_macro.fixes;

    if (g_state == State::Record) {
        uint32_t iv = static_cast<uint32_t>(util::maxv(1, cfg().fixInterval));
        if ((f % iv) == 0 && (fx.empty() || fx.back().frame < f)) {
            Fix x;
            x.frame = f;
            x.dual = g->m_gameState.m_isDualMode;
            x.a = grab(g->m_player1);
            if (x.dual) x.b = grab(g->m_player2);
            fx.push_back(x);
        }
    } else if (g_state == State::Play && !fx.empty()) {
        while (g_fixIdx < fx.size() && fx[g_fixIdx].frame < f) g_fixIdx++;
        if (g_fixIdx < fx.size() && fx[g_fixIdx].frame == f) {
            Fix const& x = fx[g_fixIdx++];
            put(g->m_player1, x.a);
            if (x.dual) put(g->m_player2, x.b);
        }
    }
}

void onReset(PlayLayer* pl, bool duringInit) {
    uint32_t f = frame(pl);
    if (g_state == State::Record) {
        if (g_macro.level.empty() && pl->m_level) g_macro.level = pl->m_level->m_levelName.c_str();
        auto& in = g_macro.inputs;
        in.erase(std::remove_if(in.begin(), in.end(), [f](Input const& i) { return i.frame >= f; }), in.end());
        auto& fx = g_macro.fixes;
        fx.erase(std::remove_if(fx.begin(), fx.end(), [f](Fix const& x) { return x.frame >= f; }), fx.end());
        // If a button was still held at the checkpoint, let go of it so the macro stays clean.
        bool held[2][4] = {};
        for (auto const& i : in) held[i.p1 ? 0 : 1][i.button & 3] = i.down;
        for (int p = 0; p < 2; p++) {
            for (int b = 1; b <= 3; b++) {
                if (!held[p][b]) continue;
                Input r;
                r.frame = f;
                r.button = static_cast<uint8_t>(b);
                r.down = false;
                r.p1 = (p == 0);
                in.push_back(r);
            }
        }
    } else if (g_state == State::Play) {
        auto& in = g_macro.inputs;
        g_idx = static_cast<size_t>(std::lower_bound(in.begin(), in.end(), f,
                    [](Input const& i, uint32_t v) { return i.frame < v; }) - in.begin());
        auto& fx = g_macro.fixes;
        g_fixIdx = static_cast<size_t>(std::lower_bound(fx.begin(), fx.end(), f,
                    [](Fix const& x, uint32_t v) { return x.frame < v; }) - fx.begin());
        if (!duringInit) releaseAll(pl);
    }
}

void onComplete() {
    if (g_state == State::Play && cfg().autoStopAtEnd) {
        g_state = State::Idle;
        overlay::toast("Macro finished the level!", overlay::Good);
    } else if (g_state == State::Record) {
        g_state = State::Idle;
        overlay::toast("Level complete! Save your macro in the Bot tab.", overlay::Good);
    }
}

void onQuit() {
    if (g_state == State::Record) overlay::toast("Recording stopped. Save it in the Bot tab!", overlay::Info);
    g_state = State::Idle;
}

bool blocksUser() { return g_state == State::Play && cfg().blockInputOnPlay; }

// ---------------------------------------------------------------------------
//  Files: <save dir>/macros/<name>.skm
// ---------------------------------------------------------------------------

std::filesystem::path folder() {
    auto p = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::vector<std::string> list() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto const& e : std::filesystem::directory_iterator(folder(), ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".skm") continue;
        out.push_back(util::pathStr(e.path().stem()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool writeFile(std::filesystem::path const& path, Macro const& m, std::string& err) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) {
        err = "Could not write the file.";
        return false;
    }
    wr(o, kMagic);
    wr(o, kVersion);
    uint32_t n = static_cast<uint32_t>(m.level.size());
    wr(o, n);
    o.write(m.level.data(), n);
    wr(o, static_cast<uint32_t>(m.inputs.size()));
    for (auto const& i : m.inputs) {
        wr(o, i.frame);
        wr(o, i.button);
        wr(o, static_cast<uint8_t>(i.down));
        wr(o, static_cast<uint8_t>(i.p1));
    }
    wr(o, static_cast<uint32_t>(m.fixes.size()));
    for (auto const& x : m.fixes) {
        wr(o, x.frame);
        wr(o, static_cast<uint8_t>(x.dual));
        wrState(o, x.a);
        wrState(o, x.b);
    }
    if (!o) err = "Could not write the file.";
    return static_cast<bool>(o);
}

bool save(std::string const& name, std::string& err) {
    if (g_macro.inputs.empty() && g_macro.fixes.empty()) {
        err = "Nothing to save yet - record something first.";
        return false;
    }
    return writeFile(folder() / util::u8path(util::safeFileName(name) + ".skm"), g_macro, err);
}

bool load(std::string const& name, std::string& err) {
    auto path = folder() / util::u8path(util::safeFileName(name) + ".skm");
    std::ifstream i(path, std::ios::binary);
    if (!i) {
        err = "Macro not found.";
        return false;
    }
    uint32_t magic = 0, version = 0, n = 0;
    if (!rd(i, magic) || magic != kMagic || !rd(i, version) || version != kVersion) {
        err = "This file is not a Sketchbook macro.";
        return false;
    }
    Macro m;
    if (!rd(i, n) || n > 4096) {
        err = "Broken file.";
        return false;
    }
    m.level.resize(n);
    if (n) i.read(m.level.data(), n);
    if (!rd(i, n) || n > 50'000'000u) {
        err = "Broken file.";
        return false;
    }
    m.inputs.resize(n);
    for (auto& in : m.inputs) {
        uint8_t d = 0, p = 0;
        if (!rd(i, in.frame) || !rd(i, in.button) || !rd(i, d) || !rd(i, p)) {
            err = "Broken file.";
            return false;
        }
        in.down = d != 0;
        in.p1 = p != 0;
    }
    if (!rd(i, n) || n > 50'000'000u) {
        err = "Broken file.";
        return false;
    }
    m.fixes.resize(n);
    for (auto& x : m.fixes) {
        uint8_t d = 0;
        if (!rd(i, x.frame) || !rd(i, d) || !rdState(i, x.a) || !rdState(i, x.b)) {
            err = "Broken file.";
            return false;
        }
        x.dual = d != 0;
    }
    g_macro = std::move(m);
    g_state = State::Idle;
    g_idx = g_fixIdx = 0;
    return true;
}

void loadImported(Macro m) {
    g_macro = std::move(m);
    g_state = State::Idle;
    g_idx = g_fixIdx = 0;
}

bool remove(std::string const& name) {
    std::error_code ec;
    return std::filesystem::remove(folder() / util::u8path(util::safeFileName(name) + ".skm"), ec);
}

}  // namespace bot
