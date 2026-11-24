#include "Stats.hpp"
#include "Util.hpp"

#include <deque>

using namespace geode::prelude;

namespace stats {

namespace {
int g_attempts = 0;
float g_best = 0.f;
int g_hitTicks = 0;
int g_totalTicks = 0;
int g_noclipDeaths = 0;
uint32_t g_lastTick = 0xFFFFFFFFu;
uint32_t g_lastHit = 0xFFFFFFFFu;
std::deque<double> g_clickTimes;
bool g_held[2] = {false, false};
bool g_usedP2 = false;
int g_sessionDeaths = 0;
double g_enterTime = 0.0;
float g_frames[120] = {};
int g_frameCount = 0, g_frameStart = 0;
int g_clicks = 0;
float g_flash = 0.f;
}  // namespace

void onLevelEnter() {
    g_attempts = 0;
    g_best = 0.f;
    g_sessionDeaths = 0;
    g_usedP2 = false;
    g_held[0] = g_held[1] = false;
    g_enterTime = util::now();
}

void onReset(PlayLayer*) {
    g_attempts++;
    g_hitTicks = 0;
    g_totalTicks = 0;
    g_noclipDeaths = 0;
    g_lastTick = 0xFFFFFFFFu;
    g_lastHit = 0xFFFFFFFFu;
    g_clicks = 0;
    g_held[0] = g_held[1] = false;
}

void onNoclipHit(PlayLayer* pl) {
    uint32_t f = pl->m_gameState.m_currentProgress;
    if (f == g_lastHit) return;
    if (g_lastHit == 0xFFFFFFFFu || f != g_lastHit + 1) {
        g_noclipDeaths++;
        g_flash = 1.f;
    }
    g_lastHit = f;
    g_hitTicks++;
}

void onButton(bool down, int button, bool p1) {
    if (button != 1) return;
    g_held[p1 ? 0 : 1] = down;
    if (!p1) g_usedP2 = true;
}

bool held(int player) { return g_held[player ? 1 : 0]; }
bool usedP2() { return g_usedP2; }
int sessionDeaths() { return g_sessionDeaths; }
double sessionTime() { return g_enterTime > 0.0 ? util::now() - g_enterTime : 0.0; }

float const* frameTimes(int& count, int& start) {
    count = g_frameCount;
    start = g_frameStart;
    return g_frames;
}

void onDeath(PlayLayer* pl) {
    g_sessionDeaths++;
    if (!pl->m_isPlatformer) g_best = util::maxv(g_best, pl->getCurrentPercent());
}

void onComplete() { g_best = 100.f; }

void onClick(bool down) {
    if (!down) return;
    g_clicks++;
    g_clickTimes.push_back(util::now());
}

void afterTick(GJBaseGameLayer* g) {
    uint32_t f = g->m_gameState.m_currentProgress;
    if (f != g_lastTick) {
        g_lastTick = f;
        g_totalTicks++;
    }
}

void update(float dt) {
    // frame time history for the FPS graph
    int idx = (g_frameStart + g_frameCount) % 120;
    g_frames[idx] = dt * 1000.f;
    if (g_frameCount < 120) g_frameCount++;
    else g_frameStart = (g_frameStart + 1) % 120;
    double t = util::now();
    while (!g_clickTimes.empty() && t - g_clickTimes.front() > 1.0) g_clickTimes.pop_front();
    g_flash = util::maxv(0.f, g_flash - dt * 2.5f);
}

int attempts() { return g_attempts; }
float best() { return g_best; }

float noclipAccuracy() {
    if (g_totalTicks <= 0) return 100.f;
    return 100.f * (1.f - static_cast<float>(g_hitTicks) / static_cast<float>(g_totalTicks));
}

int noclipDeaths() { return g_noclipDeaths; }
int cps() { return static_cast<int>(g_clickTimes.size()); }
int clicks() { return g_clicks; }
float flash() { return g_flash; }

}  // namespace stats
