#pragma once
// The "brain" of the Predict bot. Plain C++ (no game code) so it can be tested
// with a pretend game. Predict.cpp connects it to Geometry Dash.
//
// How it thinks:
//  1. From where it is, it lets the level run without clicking until the
//     player would die (or the level ends).
//  2. It then tries a click (or a release) at each tick before that death and
//     keeps the ones that get the player safely past it. That gives a timing
//     window: earliest .. latest.
//  3. It picks early, late or the middle of that window (your setting) and
//     moves on. The other timings are kept as backups.
//  4. If it ever gets stuck (nothing works), it backtracks: it goes back to
//     the last click and tries a backup timing, and further back if needed.
//  5. When it reaches the end, it replays everything from the very start to
//     double-check. If that run dies, it repairs the run from just before
//     that spot and tries again.
#include <cstdint>
#include <string>
#include <vector>

namespace predict {

enum class StepResult { Alive, Dead, Won };

// What the planner needs from the game.
class Sim {
public:
    virtual ~Sim() = default;
    virtual int snapshot() = 0;           // save the current state, return a handle (-1 = failed)
    virtual bool restore(int handle) = 0;  // go back to a saved state
    virtual void drop(int handle) = 0;     // forget a saved state
    virtual void setButton(bool down) = 0; // press / release (takes effect on the next step)
    virtual StepResult step() = 0;         // advance one physics tick
    virtual void restart() = 0;            // back to the very start of the level, button released
    virtual float percent() = 0;           // how far into the level (0..100)
    virtual void setRecording(bool on) = 0;  // record the final, double-checked run
};

enum class Timing { Early = 0, Normal = 1, Late = 2 };

struct Settings {
    Timing timing = Timing::Normal;
    int margin = 24;        // a click must keep the player alive this many ticks past the old death
    int maxWindow = 180;    // how far before a death to look for a click (ticks)
    int horizon = 720;      // run at most this far without a click before moving on
    int snapEvery = 16;     // save a checkpoint every N ticks
    int maxSnaps = 320;     // keep at most this many checkpoints
    int maxOptions = 8;     // backup timings kept per click
    int maxRepairs = 8;     // how many times the double-check may repair the run
    long long maxSteps = 60'000'000;  // give up after this many simulated ticks
    long long stuckSteps = 4'000'000; // give up if it makes no progress for this many ticks
    bool verify = true;     // double-check from the start
};

enum class Phase { Idle, Searching, Verifying, Done, Failed };

struct Toggle {
    uint32_t tick;
    bool down;
};

struct Stats {
    long long steps = 0;
    long long backtracks = 0;
    long long decisions = 0;
    int repairs = 0;
    float bestPercent = 0.f;
    uint32_t frontier = 0;
    size_t depth = 0;
};

class Planner {
public:
    void start(Sim* sim, Settings const& s);
    void stop();
    // Do up to `maxSteps` ticks of thinking. Returns when out of budget or finished.
    void run(long long maxSteps);
    Phase phase() const { return m_phase; }
    Stats const& stats() const { return m_stats; }
    std::string const& message() const { return m_msg; }
    std::vector<Toggle> const& timeline() const { return m_tl; }
    // Puts the game at the current "best guess" spot (for showing progress).
    void showFrontier();

private:
    struct Snap {
        uint32_t tick;
        int handle;
    };
    struct Choice {
        uint32_t baseTick;
        bool baseHeld;
        size_t baseTl;
        std::vector<uint32_t> options;
        size_t idx = 0;
    };
    struct Frontier {
        uint32_t tick = 0;
        bool held = false;
        bool fromToggle = false;
    };
    enum class Run { Dead, Won, Survived, Broken };
    struct RunResult {
        Run kind;
        uint32_t tick;  // tick of the step that died / won / where it stopped
    };

    // state helpers
    bool gotoTick(uint32_t tick);
    bool heldAt(uint32_t tick) const;
    void addSnap(uint32_t tick);
    void pruneSnaps(uint32_t afterTick);
    void trimSnaps();
    void dropAllSnaps();
    RunResult runTo(uint32_t fromTick, uint32_t untilTick, std::vector<uint32_t> const& extras, bool makeSnaps);
    StepResult step();

    // search
    void expand();
    RunResult test(std::vector<uint32_t> const& toggles, uint32_t mustPass);
    bool tryToggle(uint32_t t, uint32_t death);
    void pushChoice(std::vector<uint32_t> const& window);
    bool rescuePairs(uint32_t death, int64_t lo);
    bool backtrack();
    void beginVerify();
    void verifyChunk(long long budget);

    Sim* m_sim = nullptr;
    Settings m_set;
    Phase m_phase = Phase::Idle;
    Stats m_stats;
    std::string m_msg;
    std::vector<Toggle> m_tl;
    std::vector<Snap> m_snaps;
    std::vector<Choice> m_choices;
    Frontier m_front;
    long long m_budget = 0;
    uint32_t m_cur = 0;     // tick the game is at right now
    bool m_curValid = false;
    // verify state
    uint32_t m_winTick = 0;
    bool m_exact = false;           // don't trust checkpoints after this tick (they were wrong before)
    uint32_t m_exactBase = 0;
    uint32_t m_farthest = 0;        // farthest frontier reached
    long long m_farthestAt = 0;     // step count when it was reached
    uint32_t m_vTick = 0;
    size_t m_vEvent = 0;
    std::vector<Snap> m_vSnaps;
};

}  // namespace predict
