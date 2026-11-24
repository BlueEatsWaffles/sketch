#include "Hacks.hpp"
#include "Bot.hpp"
#include "Config.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Util.hpp"

#include <imgui-cocos.hpp>

#include <cmath>

using namespace geode::prelude;

namespace hacks {

bool g_ownRespawn = false;
bool g_inInit = false;

namespace {
bool g_deathPending = false;
float g_deathTimer = 0.f;
int g_steps = 0;
bool g_acDown = false;
bool g_fontReload = false;
bool g_fpsApplied = false;
double g_origInterval = 1.0 / 60.0;
float g_lastFps = 0.f;
float g_lastPitch = -1.f;
}  // namespace

PlayLayer* playLayer() { return GameManager::sharedState()->m_playLayer; }

PlayLayer* asPlay(GJBaseGameLayer* g) {
    auto pl = playLayer();
    if (pl && static_cast<GJBaseGameLayer*>(pl) == g) return pl;
    return nullptr;
}

bool cheating() {
    auto const& c = cfg();
    return c.noclip || (c.speedhack && std::fabs(c.speed - 1.f) > 0.001f) || c.jumpHack || c.autoclicker ||
           c.frameStepper || bot::state() == bot::State::Play;
}

bool safeActive() {
    auto const& c = cfg();
    return c.safeMode || (c.autoSafe && cheating());
}

bool noclipBlocks(PlayLayer* pl, PlayerObject* p) {
    auto const& c = cfg();
    if (!c.noclip) return false;
    bool isP1 = (p == pl->m_player1);
    return isP1 ? c.noclipP1 : c.noclipP2;
}

void onDeath(PlayLayer*) {
    if (!cfg().customRespawn) return;
    g_deathPending = true;
    g_deathTimer = cfg().respawnDelay;
}

void onReset(PlayLayer*) {
    g_deathPending = false;
    g_acDown = false;
}

void beforeTick(GJBaseGameLayer* g) {
    auto const& c = cfg();
    if (c.jumpHack) {
        if (g->m_player1) g->m_player1->m_isOnGround = true;
        if (g->m_player2) g->m_player2->m_isOnGround = true;
    }
    if (c.autoclicker && bot::state() != bot::State::Play && g->m_player1 && !g->m_player1->m_isDead) {
        uint32_t period = static_cast<uint32_t>(c.acHold + c.acRelease);
        uint32_t phase = g->m_gameState.m_currentProgress % util::maxv(1u, period);
        bool want = phase < static_cast<uint32_t>(c.acHold);
        if (want != g_acDown) {
            g_acDown = want;
            g->handleButton(want, 1, true);
            if (c.acP2) g->handleButton(want, 1, false);
        }
    } else if (g_acDown) {
        g_acDown = false;
        g->handleButton(false, 1, true);
        if (c.acP2) g->handleButton(false, 1, false);
    }
}

void requestStep() { g_steps = util::minv(g_steps + 1, 8); }

bool consumeStep() {
    if (g_steps <= 0) return false;
    g_steps--;
    return true;
}

void requestFontReload() { g_fontReload = true; }

void engineTick(float realDt) {
    auto const& c = cfg();
    stats::update(realDt);

    // custom respawn timer
    if (g_deathPending) {
        g_deathTimer -= realDt;
        if (g_deathTimer <= 0.f) {
            g_deathPending = false;
            if (auto pl = playLayer()) {
                g_ownRespawn = true;
                pl->delayedResetLevel();
                g_ownRespawn = false;
            }
        }
    }

    // FPS bypass
    auto dir = CCDirector::sharedDirector();
    if (c.fpsBypass) {
        if (!g_fpsApplied) {
            g_origInterval = dir->getAnimationInterval();
            g_fpsApplied = true;
            g_lastFps = 0.f;
        }
        if (c.fpsValue != g_lastFps) {
            dir->setAnimationInterval(1.0 / static_cast<double>(util::clamp(c.fpsValue, 15.f, 2000.f)));
            g_lastFps = c.fpsValue;
        }
    } else if (g_fpsApplied) {
        dir->setAnimationInterval(g_origInterval);
        g_fpsApplied = false;
    }

    // speedhack audio
    bool speedOn = c.speedhack && (!c.speedOnlyInLevel || playLayer()) && !render::locksTime();
    float pitch = (speedOn && c.speedAudio) ? util::clamp(c.speed, 0.05f, 10.f) : 1.f;
    if (pitch != g_lastPitch) {
        auto fe = FMODAudioEngine::sharedEngine();
        if (fe && fe->m_globalChannel) {
            fe->m_globalChannel->setPitch(pitch);
            g_lastPitch = pitch;
        }
    }

    // font change from the Themes tab
    if (g_fontReload) {
        g_fontReload = false;
        ImGuiCocos::get().reload();
    }
}

}  // namespace hacks
