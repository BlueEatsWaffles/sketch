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
#include <limits>
#include <vector>

using namespace geode::prelude;

namespace predictor {

namespace {

bool g_restoring = false;
constexpr uint32_t kPending = std::numeric_limits<uint32_t>::max();

// One saved spot: the game's own practice checkpoint plus a few numbers the
// checkpoint doesn't keep.
struct Slot {
    CheckpointObject* cp = nullptr;
    double extraDelta = 0.0;
    uint64_t seed = 0;
};

// ---------------------------------------------------------------------------
//  Geometry Dash as the planner's "game"
// ---------------------------------------------------------------------------
class GdSim : public predict::Sim {
public:
    PlayLayer* pl = nullptr;
    std::function<void(float)> const* update = nullptr;
    std::vector<Slot> slots;
    bool down[2] = {false, false};
    bool dead = false;
    bool won = false;
    int who = 0;
    int nPlayers = 1;
    bool recording = false;
    bot::Macro rec;
    size_t pending = 0;  // recorded inputs still waiting for their tick number
    std::string level;
    int attempts = 0;    // keeps the attempt counter from going up on every try
    uint64_t startSeed = 0;
    bool haveStartSeed = false;

    int players() override { return nPlayers; }
    int whoDied() override { return who; }

    int snapshot() override {
        if (!pl) return -1;
        CheckpointObject* cp = pl->createCheckpoint();
        if (!cp) return -1;
        cp->retain();
        Slot s{cp, pl->m_extraDelta, pl->m_randomSeed};
        for (size_t i = 0; i < slots.size(); i++) {
            if (!slots[i].cp) {
                slots[i] = s;
                return static_cast<int>(i);
            }
        }
        slots.push_back(s);
        return static_cast<int>(slots.size() - 1);
    }

    // Loads a checkpoint exactly the way practice mode does when you respawn:
    // it becomes the only checkpoint and the level resets to it. (Loading it
    // directly skips the reset, and things like portals and orbs can end up
    // in the wrong state.) No checkpoint = back to the start.
    void loadCheckpoint(CheckpointObject* cp) {
        auto arr = pl->m_checkpointArray;
        g_restoring = true;
        if (arr) {
            arr->removeAllObjects();
            if (cp) arr->addObject(cp);
            pl->resetLevel();
            arr->removeAllObjects();
        } else if (cp) {
            pl->loadFromCheckpoint(cp);
        } else {
            pl->resetLevelFromStart();
        }
        g_restoring = false;
    }

    // After a load: the game remembers held buttons across checkpoints, so let
    // go of them for real, and make sure the very next update moves 1 tick.
    void settle(double extraDelta, uint64_t seed) {
        bot::inject(pl, false, 1, true);
        bot::inject(pl, false, 1, false);
        pl->m_queuedButtons.clear();
        pl->m_resumeTimer = 0;
        pl->m_extraDelta = extraDelta;
        pl->m_randomSeed = seed;
        pl->m_attempts = attempts;
        pl->m_started = true;
        down[0] = down[1] = false;
        dead = won = false;
        who = 0;
    }

    bool restore(int h) override {
        if (!pl || h < 0 || h >= static_cast<int>(slots.size())) return false;
        Slot const& s = slots[static_cast<size_t>(h)];
        if (!s.cp) return false;
        loadCheckpoint(s.cp);
        settle(s.extraDelta, s.seed);
        releaseLater();
        return true;
    }

    // The game keeps a pointer to the checkpoint it last loaded, so that one is
    // only let go of once the game has moved on to another one.
    std::vector<CheckpointObject*> later;

    void release(CheckpointObject* cp) {
        if (!cp) return;
        if (pl && pl->m_currentCheckpoint == cp) later.push_back(cp);
        else cp->release();
    }

    void releaseLater() {
        for (size_t i = 0; i < later.size();) {
            if (!pl || pl->m_currentCheckpoint != later[i]) {
                later[i]->release();
                later[i] = later.back();
                later.pop_back();
            } else {
                i++;
            }
        }
    }

    void drop(int h) override {
        if (h < 0 || h >= static_cast<int>(slots.size())) return;
        auto& s = slots[static_cast<size_t>(h)];
        CheckpointObject* cp = s.cp;
        s = Slot{};
        release(cp);
    }

    void dropAll() {
        if (pl) pl->m_currentCheckpoint = nullptr;  // the game doesn't need ours any more
        for (auto& s : slots)
            if (s.cp) s.cp->release();
        slots.clear();
        for (auto cp : later) cp->release();
        later.clear();
    }

    void setButton(int p, bool d) override {
        if (!pl || p < 0 || p > 1 || d == down[p]) return;
        down[p] = d;
        if (recording) {
            bot::Input in;
            in.frame = kPending;  // filled in when the tick starts (same spot the bot plays it back)
            in.button = 1;
            in.down = d;
            in.p1 = p == 0;
            rec.inputs.push_back(in);
            pending++;
        }
        bot::inject(pl, d, 1, p == 0);
    }

    void stampPending(uint32_t f) {
        for (size_t i = rec.inputs.size() - pending; i < rec.inputs.size(); i++) rec.inputs[i].frame = f;
        pending = 0;
    }

    void tickBegin(GJBaseGameLayer* g) {
        if (recording && pending) stampPending(bot::frame(g));
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

    void tickEnd(GJBaseGameLayer* g) {
        if (!recording) return;
        uint32_t f = bot::frame(g);
        uint32_t every = static_cast<uint32_t>(util::maxv(1, cfg().fixInterval));
        if ((f % every) == 0 && (rec.fixes.empty() || rec.fixes.back().frame < f)) {
            bot::Fix x;
            x.frame = f;
            x.dual = g->m_gameState.m_isDualMode || nPlayers > 1;
            x.a = grab(g->m_player1);
            if (x.dual) x.b = grab(g->m_player2);
            rec.fixes.push_back(x);
        }
    }

    predict::StepResult step() override {
        if (!pl || !update) return predict::StepResult::Dead;
        dead = won = false;
        uint32_t before = pl->m_gameState.m_currentProgress;
        pl->m_resumeTimer = 0;
        (*update)(1.f / 240.f);
        if (pl->m_gameState.m_currentProgress == before && !dead && !won) {
            // the game didn't move (it was still waiting to start) - nudge it once
            pl->m_started = true;
            pl->m_resumeTimer = 0;
            (*update)(1.f / 240.f);
        }
        if (recording && pending) stampPending(before);
        if (dead) return predict::StepResult::Dead;
        if (won || pl->getCurrentPercent() >= 100.f) return predict::StepResult::Won;
        return predict::StepResult::Alive;
    }

    void restart() override {
        if (!pl) return;
        loadCheckpoint(nullptr);
        releaseLater();
        if (!haveStartSeed) {
            startSeed = pl->m_randomSeed;
            haveStartSeed = true;
        }
        settle(0.0, startSeed);
    }

    float percent() override { return pl ? pl->getCurrentPercent() : 0.f; }

    void setRecording(bool on) override {
        recording = on;
        pending = 0;
        if (on) {
            rec = bot::Macro{};
            rec.level = level;
        }
    }
};

// What the level looked like before Predict started (put back afterwards so
// your attempts and jumps don't count the bot's thousands of tries).
struct Saved {
    int levelAttempts = 0;
    int levelJumps = 0;
    int practicePercent = 0;
    int jumps = 0;
    bool wasPractice = false;
};

struct State {
    bool active = false;
    bool waiting = false;  // waiting for the level to start moving
    double waited = 0.0;
    GdSim sim;
    predict::Planner planner;
    predict::Settings settings;
    PlayLayer* pl = nullptr;
    Saved saved;
    std::chrono::steady_clock::time_point started;
    double finishedAfter = 0.0;
    bool hasResult = false;
    bool resultTwoPlayer = false;
    bot::Macro result;
    std::string resultLevel;
};
State g;

std::string levelOf(PlayLayer* pl) {
    if (!pl || !pl->m_level) return "level";
    std::string n = pl->m_level->m_levelName.c_str();
    return n.empty() ? std::string("level") : n;
}

void putBackStats() {
    auto pl = g.pl;
    if (!pl) return;
    if (pl->m_level) {
        pl->m_level->m_attempts = g.saved.levelAttempts;
        pl->m_level->m_jumps = g.saved.levelJumps;
        pl->m_level->m_practicePercent = g.saved.practicePercent;
    }
    pl->m_jumps = g.saved.jumps;
}

void finish(bool restartLevel) {
    bool wasActive = g.active;
    g.active = false;
    g.waiting = false;
    g.finishedAfter = std::chrono::duration<double>(std::chrono::steady_clock::now() - g.started).count();
    g.sim.setRecording(false);
    g.sim.update = nullptr;
    g.sim.dropAll();
    if (wasActive && g.pl) {
        if (g.pl->m_checkpointArray) g.pl->m_checkpointArray->removeAllObjects();
        putBackStats();
        bot::inject(g.pl, false, 1, true);
        bot::inject(g.pl, false, 1, false);
        if (restartLevel) {
            // back to how you had it, at the start, ready to watch
            if (g.pl->m_isPracticeMode != g.saved.wasPractice) g.pl->togglePracticeMode(g.saved.wasPractice);
            g.pl->resetLevelFromStart();
        }
    }
}

}  // namespace

bool active() { return g.active; }
bool restoring() { return g_restoring; }
bool twoPlayer() { return g.active ? g.sim.nPlayers > 1 : g.resultTwoPlayer; }

bool start(PlayLayer* pl) {
    if (g.active) return true;
    if (!pl) {
        overlay::toast("Open a level first, then press Predict.", overlay::Warn);
        return false;
    }
    if (pl->m_isPlatformer || (pl->m_levelSettings && pl->m_levelSettings->m_platformerMode)) {
        overlay::toast("Platformer levels aren't supported by Predict yet.", overlay::Warn);
        return false;
    }
    if (bot::state() != bot::State::Idle) bot::stop();
    if (render::active()) render::stop(false);

    hacks::restartFromStart(pl);  // closes the pause menu if it's open

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
    g.settings = s;
    g.saved.wasPractice = pl->m_isPracticeMode;
    if (pl->m_level) {
        g.saved.levelAttempts = pl->m_level->m_attempts.value();
        g.saved.levelJumps = pl->m_level->m_jumps.value();
        g.saved.practicePercent = pl->m_level->m_practicePercent;
    }
    g.saved.jumps = pl->m_jumps;
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    if (pl->m_checkpointArray) pl->m_checkpointArray->removeAllObjects();  // your own practice checkpoints

    g.sim.pl = pl;
    g.sim.level = levelOf(pl);
    g.sim.attempts = pl->m_attempts;
    g.sim.nPlayers = (pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode) ? 2 : 1;
    g.resultLevel = g.sim.level;
    g.started = std::chrono::steady_clock::now();
    g.active = true;
    g.waiting = true;  // the planner starts on the next game update
    overlay::toast(g.sim.nPlayers > 1 ? "Predicting a 2-player level... the bot is figuring it out."
                                      : "Predicting... the bot is figuring out the level.",
                   overlay::Info);
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
    finish(false);  // puts your attempts back and lets go of every checkpoint
    g.pl = nullptr;
    g.sim.pl = nullptr;
}

void frame(PlayLayer* pl, float realDt, std::function<void(float)> const& update) {
    if (!g.active) return;
    if (pl != g.pl) {
        stop(false);
        return;
    }
    if (g.waiting) {
        // Let the level start normally first (it waits a moment after a restart).
        if (!pl->m_started && g.waited < 3.0) {
            g.waited += realDt;
            update(realDt);
            return;
        }
        g.waiting = false;
        g.sim.update = &update;
        g.planner.start(&g.sim, g.settings);
        g.sim.update = nullptr;
        if (g.planner.phase() == predict::Phase::Failed) {
            overlay::toast(g.planner.message(), overlay::Bad);
            finish(true);
            return;
        }
    }

    g.sim.update = &update;
    auto t0 = std::chrono::steady_clock::now();
    double budget = util::clamp(cfg().predictBudget, 4, 60) / 1000.0;
    while (true) {
        auto ph = g.planner.phase();
        if (ph != predict::Phase::Searching && ph != predict::Phase::Verifying) break;
        g.planner.run(120);
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > budget) break;
    }
    auto ph = g.planner.phase();
    // show where the bot has got to (instead of whatever it was just trying)
    if (ph == predict::Phase::Searching && cfg().predictShow) g.planner.showFrontier();
    g.sim.update = nullptr;

    if (ph == predict::Phase::Done) {
        g.result = g.sim.rec;
        g.result.level = g.resultLevel;
        // inputs that never reached a tick (shouldn't happen) go at the end
        for (auto& in : g.result.inputs)
            if (in.frame == kPending) in.frame = g.result.fixes.empty() ? 0u : g.result.fixes.back().frame;
        g.hasResult = !g.result.inputs.empty() || !g.result.fixes.empty();
        g.resultTwoPlayer = g.sim.nPlayers > 1;
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
}

void tickBegin(GJBaseGameLayer* gl) {
    if (g.active) g.sim.tickBegin(gl);
}

void tickEnd(GJBaseGameLayer* gl) {
    if (g.active) g.sim.tickEnd(gl);
}

void onDeath(PlayerObject* who) {
    g.sim.dead = true;
    g.sim.who = (g.pl && who && who == g.pl->m_player2) ? 1 : 0;
}

void onWin() { g.sim.won = true; }

predict::Phase phase() {
    if (g.active && g.waiting) return predict::Phase::Searching;
    return g.active ? g.planner.phase() : (g.hasResult ? predict::Phase::Done : g.planner.phase());
}
predict::Stats stats() { return g.planner.stats(); }
std::string message() { return g.waiting ? std::string("Getting ready...") : g.planner.message(); }

double elapsed() {
    if (g.active) return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.started).count();
    return g.finishedAfter;
}

float percentDone() {
    auto const& st = g.planner.stats();
    if (!g.active && g.hasResult) return 100.f;
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
    auto bytes = importer::writeGdr(clicks, g.resultLevel, "sketch Predict", duration);
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
