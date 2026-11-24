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
int g_clicks = 0;
float g_flash = 0.f;
}  // namespace

void onLevelEnter() {
    g_attempts = 0;
    g_best = 0.f;
}

void onReset(PlayLayer*) {
    g_attempts++;
    g_hitTicks = 0;
    g_totalTicks = 0;
    g_noclipDeaths = 0;
    g_lastTick = 0xFFFFFFFFu;
    g_lastHit = 0xFFFFFFFFu;
    g_clicks = 0;
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

void onDeath(PlayLayer* pl) {
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
