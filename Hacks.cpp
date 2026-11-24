#include "Hacks.hpp"
#include "Bot.hpp"
#include "Predict.hpp"
#include "Config.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Util.hpp"

#include <imgui-cocos.hpp>

#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

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
bool g_slowHeld = false;
bool g_muted = false;
float g_focusTimer = 0.f;
bool g_focused = true;
bool g_fpsApplied = false;
double g_origInterval = 1.0 / 60.0;
float g_lastFps = 0.f;
float g_lastPitch = -1.f;
}  // namespace

// Keys you can pick on Geode's settings page (must match "one-of" in mod.json).
bool isSettingKey(std::string const& name) {
    static char const* const keys[] = {"Tab", "`", "Insert", "Home", "End", "Delete", "PgUp", "PgDn", "F1", "F2",
                                       "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                                       "B", "M", "Q", "Y"};
    for (auto k : keys)
        if (name == k) return true;
    return false;
}

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

void restartFromStart(PlayLayer* pl) {
    if (!pl) return;
    // If the game is paused, use the pause menu's own "restart from start"
    // button, so the pause menu closes and the level starts right away.
    auto findPause = [](CCNode* parent) -> PauseLayer* {
        if (!parent || !parent->getChildren()) return nullptr;
        auto kids = parent->getChildren();
        for (unsigned i = 0; i < kids->count(); i++) {
            if (auto pause = geode::cast::typeinfo_cast<PauseLayer*>(kids->objectAtIndex(i))) return pause;
        }
        return nullptr;
    };
    PauseLayer* pause = findPause(CCDirector::sharedDirector()->getRunningScene());
    if (!pause) pause = findPause(pl);
    if (pause) {
        pause->onRestartFull(nullptr);
        return;
    }
    pl->resetLevelFromStart();
}

void requestStep() { g_steps = util::minv(g_steps + 1, 8); }

bool consumeStep() {
    if (g_steps <= 0) return false;
    g_steps--;
    return true;
}

void requestFontReload() { g_fontReload = true; }

void setSlowHeld(bool held) { g_slowHeld = held; }

float speedFactor(bool inLevel) {
    auto const& c = cfg();
    if (predictor::active()) return 1.f;  // Predict never uses speedhack
    if (render::audioPassActive()) return 1.f;  // recording the game's sound: real speed only
    float f = 1.f;
    if (c.speedhack && (!c.speedOnlyInLevel || inLevel)) f *= util::clamp(c.speed, 0.01f, 100.f);
    if (c.slowmo && g_slowHeld && inLevel) f *= util::clamp(c.slowmoSpeed, 0.05f, 1.f);
    return f;
}

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

    // speedhack / slow-motion audio
    bool inLevel = playLayer() != nullptr;
    float factor = render::locksTime() ? 1.f : speedFactor(inLevel);
    float pitch = c.speedAudio ? util::clamp(factor, 0.05f, 10.f) : 1.f;
    if (pitch != g_lastPitch) {
        auto fe = FMODAudioEngine::sharedEngine();
        if (fe && fe->m_globalChannel) {
            fe->m_globalChannel->setPitch(pitch);
            g_lastPitch = pitch;
        }
    }

    // "Menu key" in Geode's settings page for this mod <-> the key in our menu
    static float settingTimer = 1.f;
    static std::string seenSetting;
    static int seenKey = -999;
    settingTimer += realDt;
    if (settingTimer > 0.5f) {
        settingTimer = 0.f;
        std::string v = Mod::get()->getSettingValue<std::string>("menu-key");
        if (seenKey == -999) {
            seenSetting = v;  // first look: the key saved in our own settings wins
            seenKey = c.menuKey;
        } else if (v != seenSetting) {  // changed on Geode's settings page
            seenSetting = v;
            int k = util::keyFromName(v);
            if (k >= 0) {
                cfg().menuKey = k;
                saveConfig();
            }
            seenKey = cfg().menuKey;
        } else if (c.menuKey != seenKey) {  // changed inside our menu
            seenKey = c.menuKey;
            std::string name = util::keyName(c.menuKey);
            if (name != seenSetting && util::keyFromName(name) >= 0 && isSettingKey(name)) {
                Mod::get()->setSettingValue<std::string>("menu-key", name);
                seenSetting = name;
            }
        }
    }

    // mute while Predict thinks, or (if you want) while the game isn't focused
    g_focusTimer += realDt;
    if (g_focusTimer > 0.25f) {
        g_focusTimer = 0.f;
        DWORD pid = 0;
        HWND fg = GetForegroundWindow();
        if (fg) GetWindowThreadProcessId(fg, &pid);
        g_focused = fg && pid == GetCurrentProcessId();
    }
    bool mute = !render::audioPassActive() && (predictor::active() || (c.muteUnfocused && !g_focused));
    if (mute != g_muted) {
        auto fe = FMODAudioEngine::sharedEngine();
        if (fe && fe->m_globalChannel) {
            fe->m_globalChannel->setMute(mute);
            g_muted = mute;
        }
    }

    // font change from the Themes tab
    if (g_fontReload) {
        g_fontReload = false;
        ImGuiCocos::get().reload();
    }
}

}  // namespace hacks
