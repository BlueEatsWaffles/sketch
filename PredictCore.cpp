#include "PredictCore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace predict {

namespace {
int64_t i64(uint32_t v) { return static_cast<int64_t>(v); }
}  // namespace

// ---------------------------------------------------------------------------
//  Start / stop
// ---------------------------------------------------------------------------

void Planner::start(Sim* sim, Settings const& s) {
    stop();
    m_sim = sim;
    m_set = s;
    if (m_set.snapEvery < 1) m_set.snapEvery = 1;
    if (m_set.maxOptions < 1) m_set.maxOptions = 1;
    if (m_set.maxSnaps < 8) m_set.maxSnaps = 8;
    m_stats = Stats{};
    m_farthest = 0;
    m_farthestAt = 0;
    m_winTick = 0;
    m_exact = false;
    m_exactBase = 0;
    m_tl.clear();
    m_choices.clear();
    m_front = Frontier{};
    m_msg = "Thinking...";
    m_sim->setRecording(false);
    m_sim->restart();
    m_cur = 0;
    m_curValid = true;
    addSnap(0);
    if (m_snaps.empty()) {
        m_phase = Phase::Failed;
        m_msg = "Couldn't save a checkpoint in this level.";
        return;
    }
    m_phase = Phase::Searching;
}

void Planner::stop() {
    dropAllSnaps();
    if (m_sim) m_sim->setRecording(false);
    if (m_phase == Phase::Searching || m_phase == Phase::Verifying) {
        m_phase = Phase::Idle;
        m_msg = "Stopped.";
    }
}

// ---------------------------------------------------------------------------
//  Checkpoints ("snaps")
// ---------------------------------------------------------------------------

void Planner::addSnap(uint32_t tick) {
    if (m_exact && tick > m_exactBase) return;  // replay from a trusted point instead
    int h = m_sim->snapshot();
    if (h < 0) return;
    // keep sorted by tick, one per tick
    auto it = std::lower_bound(m_snaps.begin(), m_snaps.end(), tick, [](Snap const& s, uint32_t t) { return s.tick < t; });
    if (it != m_snaps.end() && it->tick == tick) {
        m_sim->drop(it->handle);
        it->handle = h;
    } else {
        m_snaps.insert(it, Snap{tick, h});
    }
    trimSnaps();
}

void Planner::pruneSnaps(uint32_t afterTick) {
    while (!m_snaps.empty() && m_snaps.back().tick > afterTick) {
        m_sim->drop(m_snaps.back().handle);
        m_snaps.pop_back();
    }
}

void Planner::trimSnaps() {
    if (static_cast<int>(m_snaps.size()) <= m_set.maxSnaps) return;
    // Thin out the older two thirds (keep the first one and every second one).
    size_t old = m_snaps.size() * 2 / 3;
    std::vector<Snap> keep;
    keep.reserve(m_snaps.size());
    for (size_t i = 0; i < m_snaps.size(); i++) {
        if (i == 0 || i >= old || (i % 2) == 0) keep.push_back(m_snaps[i]);
        else m_sim->drop(m_snaps[i].handle);
    }
    m_snaps.swap(keep);
}

void Planner::dropAllSnaps() {
    if (m_sim)
        for (auto const& s : m_snaps) m_sim->drop(s.handle);
    m_snaps.clear();
}

bool Planner::heldAt(uint32_t tick) const {
    bool held = false;
    for (auto const& e : m_tl) {
        if (e.tick >= tick) break;
        held = e.down;
    }
    return held;
}

// Puts the game at the start of `tick` on the committed path.
bool Planner::gotoTick(uint32_t tick) {
    if (m_curValid && m_cur == tick) return true;
    // the closest checkpoint at or before `tick`
    auto it = std::upper_bound(m_snaps.begin(), m_snaps.end(), tick, [](uint32_t t, Snap const& s) { return t < s.tick; });
    bool fromCurrent = m_curValid && m_cur < tick && (it == m_snaps.begin() || std::prev(it)->tick <= m_cur);
    if (!fromCurrent) {
        if (it == m_snaps.begin()) return false;
        --it;
        if (!m_sim->restore(it->handle)) return false;
        m_cur = it->tick;
        m_curValid = true;
        m_sim->setButton(false);
    }
    RunResult r = runTo(m_cur, tick, {}, false);
    return r.kind == Run::Survived;
}

StepResult Planner::step() {
    m_budget--;
    m_stats.steps++;
    StepResult r = m_sim->step();
    if ((m_stats.steps & 7) == 0 || r != StepResult::Alive) {
        float p = m_sim->percent();
        if (p > m_stats.bestPercent && r != StepResult::Dead) m_stats.bestPercent = p;
    }
    return r;
}

// Runs from the start of `fromTick` (the game must be there) to the start of
// `untilTick`, using the committed clicks plus one optional extra toggle.
Planner::RunResult Planner::runTo(uint32_t fromTick, uint32_t untilTick, std::vector<uint32_t> const& extras, bool makeSnaps) {
    bool held = heldAt(fromTick);
    m_sim->setButton(held);
    auto ev = std::lower_bound(m_tl.begin(), m_tl.end(), fromTick, [](Toggle const& e, uint32_t t) { return e.tick < t; });
    size_t xi = 0;
    uint32_t lastSnap = m_snaps.empty() ? 0 : m_snaps.back().tick;
    for (uint32_t k = fromTick; k < untilTick; k++) {
        bool onPath = extras.empty() || k <= extras.front();
        if (makeSnaps && onPath && !held && k > lastSnap && k - lastSnap >= static_cast<uint32_t>(m_set.snapEvery)) {
            m_cur = k;
            m_curValid = true;
            addSnap(k);
            lastSnap = k;
        }
        while (ev != m_tl.end() && ev->tick == k) {
            held = ev->down;
            m_sim->setButton(held);
            ++ev;
        }
        while (xi < extras.size() && extras[xi] == k) {
            held = !held;
            m_sim->setButton(held);
            xi++;
        }
        StepResult r = step();
        if (r == StepResult::Dead) {
            m_curValid = false;
            return {Run::Dead, k};
        }
        if (r == StepResult::Won) {
            m_curValid = false;
            return {Run::Won, k};
        }
    }
    m_cur = untilTick;
    m_curValid = extras.empty() || untilTick <= extras.front();
    return {Run::Survived, untilTick};
}

// ---------------------------------------------------------------------------
//  Search
// ---------------------------------------------------------------------------

// Tries extra toggles (sorted) from the committed path; survives = gets past `mustPass` + margin.
Planner::RunResult Planner::test(std::vector<uint32_t> const& toggles, uint32_t mustPass) {
    if (!gotoTick(toggles.front())) return {Run::Broken, toggles.front()};
    return runTo(toggles.front(), mustPass + static_cast<uint32_t>(m_set.margin) + 1, toggles, false);
}

bool Planner::tryToggle(uint32_t t, uint32_t death) {
    RunResult r = test({t}, death);
    return r.kind == Run::Survived || r.kind == Run::Won;
}

// No single click works: try two in a row (like "let go, then click again").
// Returns true if it found some and pushed a choice for the first one.
bool Planner::rescuePairs(uint32_t death, int64_t lo) {
    std::vector<uint32_t> firsts;
    int tried = 0;
    for (int64_t t = death; t >= lo && tried < 36 && static_cast<int>(firsts.size()) < m_set.maxOptions; t -= 4, tried++) {
        RunResult r1 = test({static_cast<uint32_t>(t)}, death);
        if (r1.kind == Run::Survived || r1.kind == Run::Won) {
            firsts.push_back(static_cast<uint32_t>(t));
            continue;
        }
        if (r1.kind != Run::Dead || r1.tick <= t) continue;
        uint32_t d1 = r1.tick;
        uint32_t pass = std::max(death, d1);
        int tried2 = 0;
        for (int64_t t2 = d1; t2 > t && tried2 < 48; t2--, tried2++) {
            RunResult r2 = test({static_cast<uint32_t>(t), static_cast<uint32_t>(t2)}, pass);
            if (r2.kind == Run::Survived || r2.kind == Run::Won) {
                firsts.push_back(static_cast<uint32_t>(t));
                break;
            }
        }
    }
    if (firsts.empty()) return false;
    std::sort(firsts.begin(), firsts.end());
    pushChoice(firsts);
    return true;
}

void Planner::pushChoice(std::vector<uint32_t> const& window) {
    // window is sorted from earliest to latest
    uint32_t E = window.front(), L = window.back();
    uint32_t pref = m_set.timing == Timing::Early ? E : (m_set.timing == Timing::Late ? L : static_cast<uint32_t>((i64(E) + i64(L)) / 2));
    std::vector<uint32_t> pick;
    size_t n = window.size(), k = static_cast<size_t>(m_set.maxOptions);
    if (n <= k) {
        pick = window;
    } else {
        pick.push_back(pref);
        for (size_t j = 0; j < k - 1; j++) pick.push_back(window[(j * (n - 1)) / (k - 2 > 0 ? k - 2 : 1)]);
        std::sort(pick.begin(), pick.end());
        pick.erase(std::unique(pick.begin(), pick.end()), pick.end());
    }
    std::stable_sort(pick.begin(), pick.end(), [pref](uint32_t a, uint32_t b) {
        return std::llabs(i64(a) - i64(pref)) < std::llabs(i64(b) - i64(pref));
    });
    Choice c;
    c.baseTick = m_front.tick;
    c.baseHeld = m_front.held;
    c.baseTl = m_tl.size();
    c.options = std::move(pick);
    c.idx = 0;
    m_choices.push_back(c);

    uint32_t t = m_choices.back().options[0];
    pruneSnaps(t);
    m_tl.push_back({t, !c.baseHeld});
    m_front = {t, !c.baseHeld, true};
    m_curValid = false;
    m_stats.decisions++;
}

bool Planner::backtrack() {
    while (!m_choices.empty()) {
        Choice& c = m_choices.back();
        c.idx++;
        if (c.idx < c.options.size()) {
            uint32_t t = c.options[c.idx];
            m_tl.resize(c.baseTl);
            pruneSnaps(t);
            m_tl.push_back({t, !c.baseHeld});
            m_front = {t, !c.baseHeld, true};
            m_curValid = false;
            m_stats.backtracks++;
            return true;
        }
        m_choices.pop_back();
    }
    return false;
}

void Planner::expand() {
    Frontier f = m_front;
    m_stats.frontier = f.tick;
    if (f.tick > m_farthest || m_farthestAt == 0) {
        m_farthest = std::max(m_farthest, f.tick);
        m_farthestAt = m_stats.steps + 1;
    } else if (m_stats.steps - m_farthestAt > m_set.stuckSteps) {
        m_phase = Phase::Failed;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Stuck at %.1f%% for too long. Try a different click timing or a bigger safety margin.",
                      static_cast<double>(m_stats.bestPercent));
        m_msg = buf;
        return;
    }
    m_stats.depth = m_choices.size();
    if (!gotoTick(f.tick)) {
        // The saved run doesn't hold up any more (can happen if a checkpoint
        // wasn't perfect). Treat it like a dead end.
        if (!backtrack()) {
            m_phase = Phase::Failed;
            m_msg = "Got stuck right at the start.";
        }
        return;
    }
    RunResult r = runTo(f.tick, f.tick + static_cast<uint32_t>(m_set.horizon), {}, true);
    if (r.kind == Run::Won) {
        m_winTick = r.tick;
        if (m_set.verify) beginVerify();
        else {
            m_phase = Phase::Done;
            m_msg = "Beat the level!";
        }
        return;
    }
    if (r.kind == Run::Survived) {
        // Nothing to do for a while: just move on.
        m_front = {f.tick + static_cast<uint32_t>(m_set.horizon / 2), f.held, false};
        return;
    }
    if (r.kind == Run::Broken) return;

    uint32_t death = r.tick;
    int64_t lo = i64(f.tick) + (f.fromToggle ? 1 : 0);
    lo = std::max<int64_t>(lo, i64(death) - m_set.maxWindow);
    int64_t latest = -1;
    for (int64_t t = death; t >= lo; t--) {
        if (tryToggle(static_cast<uint32_t>(t), death)) {
            latest = t;
            break;
        }
    }
    if (latest < 0 && rescuePairs(death, lo)) return;
    if (latest < 0) {
        if (!backtrack()) {
            m_phase = Phase::Failed;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "Couldn't find a way past %.1f%%. The level may need a 2nd player or be too tight.",
                          static_cast<double>(m_stats.bestPercent));
            m_msg = buf;
        }
        return;
    }
    // Find the earliest timing that still works, without testing every tick:
    // jump back 1, 2, 4, 8... ticks while it works, then narrow down.
    int64_t good = latest, bad = lo - 1;
    for (int64_t stepBack = 1; good - stepBack >= lo; stepBack *= 2) {
        int64_t t = good - stepBack;
        if (tryToggle(static_cast<uint32_t>(t), death)) {
            good = t;
        } else {
            bad = t;
            break;
        }
        if (latest - good > 160) break;
    }
    while (bad + 1 < good) {
        int64_t mid = (bad + good) / 2;
        if (tryToggle(static_cast<uint32_t>(mid), death)) good = mid;
        else bad = mid;
    }
    std::vector<uint32_t> window;
    for (int64_t t = good; t <= latest; t++) window.push_back(static_cast<uint32_t>(t));
    pushChoice(window);
}

// ---------------------------------------------------------------------------
//  Double-check from the start
// ---------------------------------------------------------------------------

void Planner::beginVerify() {
    m_phase = Phase::Verifying;
    m_msg = "Double-checking from the start...";
    for (auto const& s : m_vSnaps) m_sim->drop(s.handle);
    m_vSnaps.clear();
    m_sim->restart();
    m_sim->setButton(false);
    m_sim->setRecording(true);
    m_vTick = 0;
    m_vEvent = 0;
    m_curValid = false;
}

void Planner::verifyChunk(long long budget) {
    bool held = m_vEvent > 0 ? m_tl[m_vEvent - 1].down : false;
    uint32_t lastEvent = m_tl.empty() ? 0 : m_tl.back().tick;
    while (budget-- > 0) {
        if (!held && (m_vTick % static_cast<uint32_t>(m_set.snapEvery * 2)) == 0) {
            int h = m_sim->snapshot();
            if (h >= 0) m_vSnaps.push_back({m_vTick, h});
            if (static_cast<int>(m_vSnaps.size()) > m_set.maxSnaps) {
                // thin out the older half
                std::vector<Snap> keep;
                for (size_t i = 0; i < m_vSnaps.size(); i++) {
                    if (i == 0 || i >= m_vSnaps.size() / 2 || (i % 2) == 0) keep.push_back(m_vSnaps[i]);
                    else m_sim->drop(m_vSnaps[i].handle);
                }
                m_vSnaps.swap(keep);
            }
        }
        while (m_vEvent < m_tl.size() && m_tl[m_vEvent].tick == m_vTick) {
            held = m_tl[m_vEvent].down;
            m_sim->setButton(held);
            m_vEvent++;
        }
        StepResult r = step();
        if (r == StepResult::Won) {
            m_sim->setRecording(false);
            for (auto const& s : m_vSnaps) m_sim->drop(s.handle);
            m_vSnaps.clear();
            dropAllSnaps();
            m_phase = Phase::Done;
            m_msg = "Beat the level! Double-checked from the start.";
            return;
        }
        bool tooLong = m_vTick > std::max(lastEvent, m_winTick) + static_cast<uint32_t>(m_set.horizon);
        if (r == StepResult::Dead || tooLong) {
            // The real run died here: repair from a little before this spot.
            m_sim->setRecording(false);
            m_stats.repairs++;
            if (m_stats.repairs > m_set.maxRepairs) {
                for (auto const& s : m_vSnaps) m_sim->drop(s.handle);
                m_vSnaps.clear();
                dropAllSnaps();
                m_phase = Phase::Failed;
                m_msg = "The double-check kept failing. This level may not play the same way twice.";
                return;
            }
            uint32_t back = static_cast<uint32_t>(60 + 60 * m_stats.repairs);
            uint32_t target = m_vTick > back ? m_vTick - back : 0;
            // switch to the clean checkpoints from this run
            dropAllSnaps();
            m_snaps.clear();
            for (auto const& s : m_vSnaps) {
                if (s.tick <= target) m_snaps.push_back(s);
                else m_sim->drop(s.handle);
            }
            m_vSnaps.clear();
            if (m_snaps.empty()) {
                m_sim->restart();
                m_cur = 0;
                m_curValid = true;
                addSnap(0);
            }
            uint32_t base = m_snaps.back().tick;
            while (!m_tl.empty() && m_tl.back().tick >= base) m_tl.pop_back();
            if (m_stats.repairs >= 2) {
                // Checkpoints after this point were wrong twice: from now on,
                // test everything by replaying from this trusted point.
                m_exact = true;
                m_exactBase = base;
            }
            m_choices.clear();
            m_front = {base, false, false};
            m_curValid = false;
            m_phase = Phase::Searching;
            m_msg = "Fixing a spot the double-check found...";
            return;
        }
        m_vTick++;
    }
}

// ---------------------------------------------------------------------------

void Planner::run(long long maxSteps) {
    m_budget = maxSteps;
    while (m_budget > 0) {
        if (m_stats.steps > m_set.maxSteps) {
            stop();
            m_phase = Phase::Failed;
            m_msg = "Thought for too long and gave up. Try a different click timing.";
            return;
        }
        if (m_phase == Phase::Searching) {
            expand();
        } else if (m_phase == Phase::Verifying) {
            verifyChunk(m_budget);
            m_budget = 0;
        } else {
            return;
        }
    }
}

void Planner::showFrontier() {
    if (m_phase != Phase::Searching) return;
    gotoTick(m_front.tick);
}

}  // namespace predict
