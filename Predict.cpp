#include "Predict.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "ImportCore.hpp"
#include "Overlay.hpp"
#include "Render.hpp"
#include "Util.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace geode::prelude;

namespace predictor {

namespace {

// ---------------------------------------------------------------------------
//  Geometry Dash as the planner's "game"
// ---------------------------------------------------------------------------
class GdSim : public predict::Sim {
public:
    PlayLayer* pl = nullptr;
    std::function<void()> const* stepFn = nullptr;
    std::vector<CheckpointObject*> slots;
    bool down = false;
    bool dead = false;
    bool won = false;
    bool recording = false;
    bot::Macro rec;
    std::string level;

    int snapshot() override {
        if (!pl) return -1;
        CheckpointObject* cp = pl->createCheckpoint();
        if (!cp) return -1;
        cp->retain();
        for (size_t i = 0; i < slots.size(); i++) {
            if (!slots[i]) {
                slots[i] = cp;
                return static_cast<int>(i);
            }
        }
        slots.push_back(cp);
        return static_cast<int>(slots.size() - 1);
    }

    bool restore(int h) override {
        if (!pl || h < 0 || h >= static_cast<int>(slots.size()) || !slots[static_cast<size_t>(h)]) return false;
        pl->loadFromCheckpoint(slots[static_cast<size_t>(h)]);
        down = false;  // checkpoints are only taken with the button up
        dead = won = false;
        return true;
    }

    void drop(int h) override {
        if (h < 0 || h >= static_cast<int>(slots.size())) return;
        auto& s = slots[static_cast<size_t>(h)];
        if (s) s->release();
        s = nullptr;
    }

    void dropAll() {
        for (auto& s : slots) {
            if (s) s->release();
            s = nullptr;
        }
        slots.clear();
    }

    void setButton(bool d) override {
        if (!pl || d == down) return;
        down = d;
        if (recording) {
            bot::Input in;
            in.frame = bot::frame(pl);
            in.button = 1;
            in.down = d;
            in.p1 = true;
            rec.inputs.push_back(in);
        }
        bot::inject(pl, d, 1, true);
    }

    static bot::PState grab(PlayerObject* p) {
        bot::PState s;
        if (!p) return s;
        s.x = p->getPositionX();
        s.y = p->getPositionY();
        s.rot = p->getRotation();
        s.yVel = p->m_yVelocity;
        return s;
    }

    predict::StepResult step() override {
        if (!pl || !stepFn) return predict::StepResult::Dead;
        dead = won = false;
        (*stepFn)();
        if (recording) {
            uint32_t f = bot::frame(pl);
            uint32_t every = static_cast<uint32_t>(util::maxv(1, cfg().fixInterval));
            if ((f % every) == 0 && (rec.fixes.empty() || rec.fixes.back().frame < f)) {
                bot::Fix x;
                x.frame = f;
                x.dual = pl->m_gameState.m_isDualMode;
                x.a = grab(pl->m_player1);
                if (x.dual) x.b = grab(pl->m_player2);
                rec.fixes.push_back(x);
            }
        }
        if (dead) return predict::StepResult::Dead;
        if (won || pl->getCurrentPercent() >= 100.f) return predict::StepResult::Won;
        return predict::StepResult::Alive;
    }

    void restart() override {
        if (!pl) return;
        pl->resetLevelFromStart();
        down = true;  // force a real release below
        setButton(false);
        dead = won = false;
    }

    float percent() override { return pl ? pl->getCurrentPercent() : 0.f; }

    void setRecording(bool on) override {
        recording = on;
        if (on) {
            rec = bot::Macro{};
            rec.level = level;
        }
    }
};

struct State {
    bool active = false;
    GdSim sim;
    predict::Planner planner;
    PlayLayer* pl = nullptr;
    std::chrono::steady_clock::time_point started;
    double finishedAfter = 0.0;
    bool hasResult = false;
    bot::Macro result;
    std::string resultLevel;
    predict::Phase lastPhase = predict::Phase::Idle;
};
State g;

std::string levelOf(PlayLayer* pl) {
    if (!pl || !pl->m_level) return "level";
    std::string n = pl->m_level->m_levelName.c_str();
    return n.empty() ? std::string("level") : n;
}

void finish(bool restartLevel) {
    bool wasActive = g.active;
    g.active = false;
    g.finishedAfter = std::chrono::duration<double>(std::chrono::steady_clock::now() - g.started).count();
    g.sim.setRecording(false);
    g.sim.stepFn = nullptr;
    g.sim.dropAll();
    if (wasActive && restartLevel && g.pl) {
        // leave practice mode and go back to the start, ready to watch
        if (g.pl->m_isPracticeMode) g.pl->togglePracticeMode(false);
        g.pl->resetLevelFromStart();
    }
}

}  // namespace

bool active() { return g.active; }

bool start(PlayLayer* pl) {
    if (g.active) return true;
    if (!pl) {
        overlay::toast("Open a level first, then press Predict.", overlay::Warn);
        return false;
    }
    if (pl->m_isPlatformer) {
        overlay::toast("Platformer levels aren't supported by Predict yet.", overlay::Warn);
        return false;
    }
    if (pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode) {
        overlay::toast("2-player levels aren't supported by Predict yet.", overlay::Warn);
        return false;
    }
    if (bot::state() != bot::State::Idle) bot::stop();
    if (render::active()) render::stop(false);

    hacks::restartFromStart(pl);  // closes the pause menu if it's open
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);

    auto const& c = cfg();
    predict::Settings s;
    s.timing = static_cast<predict::Timing>(util::clamp(c.predictTiming, 0, 2));
    s.margin = c.predictMargin;
    s.verify = c.predictVerify;
    s.snapEvery = 16;
    s.maxSnaps = 300;
    s.stuckSteps = 3'000'000;

    g = State{};
    g.pl = pl;
    g.sim.pl = pl;
    g.sim.level = levelOf(pl);
    g.resultLevel = g.sim.level;
    g.started = std::chrono::steady_clock::now();
    g.active = true;

    // the planner restarts the level and saves a checkpoint at the start
    std::function<void()> none = [] {};
    g.sim.stepFn = &none;
    g.planner.start(&g.sim, s);
    g.sim.stepFn = nullptr;
    if (g.planner.phase() == predict::Phase::Failed) {
        overlay::toast(g.planner.message(), overlay::Bad);
        finish(true);
        return false;
    }
    overlay::toast("Predicting... the bot is figuring out the level.", overlay::Info);
    return true;
}

void stop(bool restartLevel) {
    if (!g.active) return;
    g.planner.stop();
    finish(restartLevel);
    overlay::toast("Predict stopped.", overlay::Info);
}

void onQuit() {
    if (!g.active) return;
    g.planner.stop();
    g.pl = nullptr;
    finish(false);
}

void frame(PlayLayer* pl, std::function<void()> const& stepOneTick) {
    if (!g.active) return;
    if (pl != g.pl) {
        stop(false);
        return;
    }
    g.sim.stepFn = &stepOneTick;
    auto t0 = std::chrono::steady_clock::now();
    double budget = util::clamp(cfg().predictBudget, 4, 60) / 1000.0;
    while (true) {
        auto ph = g.planner.phase();
        if (ph != predict::Phase::Searching && ph != predict::Phase::Verifying) break;
        g.planner.run(240);
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > budget) break;
    }
    auto ph = g.planner.phase();
    if (ph == predict::Phase::Searching && cfg().predictShow) g.planner.showFrontier();
    g.sim.stepFn = nullptr;

    if (ph == predict::Phase::Done) {
        g.result = g.sim.rec;
        g.result.level = g.resultLevel;
        g.hasResult = !g.result.inputs.empty() || !g.result.fixes.empty();
        finish(true);
        if (g.hasResult) {
            std::snprintf(cfg().predictName, sizeof(cfg().predictName), "%s", defaultName().c_str());
            overlay::toast("Predict beat the level! Save it or press Watch it.", overlay::Good);
        } else {
            overlay::toast("The level needs no clicks at all!", overlay::Good);
        }
    } else if (ph == predict::Phase::Failed) {
        std::string msg = g.planner.message();
        finish(true);
        overlay::toast(msg, overlay::Bad);
    }
    g.lastPhase = ph;
}

void onDeath() { g.sim.dead = true; }
void onWin() { g.sim.won = true; }

predict::Phase phase() { return g.active ? g.planner.phase() : (g.hasResult ? predict::Phase::Done : g.planner.phase()); }
predict::Stats stats() { return g.planner.stats(); }
std::string message() { return g.planner.message(); }

double elapsed() {
    if (g.active) return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.started).count();
    return g.finishedAfter;
}

float percentDone() {
    auto const& st = g.planner.stats();
    if (g.planner.phase() == predict::Phase::Done) return 100.f;
    return util::clamp(st.bestPercent, 0.f, 100.f);
}

bool hasResult() { return g.hasResult; }
bot::Macro const& result() { return g.result; }
std::string levelName() { return g.resultLevel; }
std::string defaultName() { return g.resultLevel + " - bot"; }

bool watch() {
    if (!g.hasResult) return false;
    bot::loadImported(g.result);
    bot::startPlay();
    return true;
}

bool saveToLibrary(std::string const& name, std::string& err) {
    if (!g.hasResult) {
        err = "Nothing to save yet - press Predict first.";
        return false;
    }
    std::string n = name.empty() ? defaultName() : name;
    return bot::writeFile(bot::folder() / util::u8path(util::safeFileName(n) + ".skm"), g.result, err);
}

bool saveToFolder(std::string const& folder, std::string const& name, int format, std::string& savedPath, std::string& err) {
    if (!g.hasResult) {
        err = "Nothing to save yet - press Predict first.";
        return false;
    }
    if (folder.empty()) {
        err = "Pick a folder first.";
        return false;
    }
    std::string n = util::safeFileName(name.empty() ? defaultName() : name);
    auto dir = util::u8path(folder);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (format == 1) {
        auto path = dir / util::u8path(n + ".skm");
        if (!bot::writeFile(path, g.result, err)) return false;
        savedPath = util::pathStr(path);
        return true;
    }
    // .gdr: works in Mega Hack, xdBot, zBot, Prism, ToastyReplay and more
    std::vector<importer::Click> clicks;
    clicks.reserve(g.result.inputs.size());
    for (auto const& i : g.result.inputs) clicks.push_back({i.frame, i.button, i.p1, i.down});
    double duration = g.result.inputs.empty() ? 0.0 : g.result.inputs.back().frame / 240.0;
    auto bytes = importer::writeGdr(clicks, g.resultLevel, "Sketchbook Predict", duration);
    auto path = dir / util::u8path(n + ".gdr");
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) {
        err = "Couldn't write to that folder.";
        return false;
    }
    o.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!o) {
        err = "Couldn't write the file.";
        return false;
    }
    savedPath = util::pathStr(path);
    return true;
}

}  // namespace predictor
