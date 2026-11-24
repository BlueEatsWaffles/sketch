// Hooks into the level itself: PlayLayer, the shared game layer and the player.
#include "Bot.hpp"
#include "Config.hpp"
#include "Hacks.hpp"
#include "Predict.hpp"
#include "Render.hpp"
#include "Stats.hpp"
#include "Visuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

class $modify(SKPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        stats::onLevelEnter();
        visuals::onLevelEnter();
        hacks::g_inInit = true;
        bool ok = PlayLayer::init(level, useReplay, dontCreateObjects);
        hacks::g_inInit = false;
        return ok;
    }

    void resetLevel() {
        if (predictor::restoring()) {  // Predict loading one of its checkpoints
            PlayLayer::resetLevel();
            return;
        }
        PlayLayer::resetLevel();
        stats::onReset(this);
        hacks::onReset(this);
        visuals::onReset(this);
        bot::onReset(this, hacks::g_inInit);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool realHazard = object != m_anticheatSpike;
        if (predictor::active()) {
            // The Predict bot is testing a run: just note the death.
            if (realHazard) {
                predictor::onDeath(player);
                return;
            }
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        if (realHazard && player && hacks::noclipBlocks(this, player)) {
            stats::onNoclipHit(this);
            visuals::addDeathMarker(this, player);
            return;
        }
        bool prevTest = m_isTestMode;
        if (hacks::safeActive()) m_isTestMode = true;  // the game doesn't save progress in test mode
        PlayLayer::destroyPlayer(player, object);
        m_isTestMode = prevTest;
        if (realHazard && player && player->m_isDead) {
            visuals::addDeathMarker(this, player);
            stats::onDeath(this);
            hacks::onDeath(this);
            render::onDeath();
        }
    }

    void levelComplete() {
        if (predictor::active()) {
            predictor::onWin();
            return;
        }
        bool prevTest = m_isTestMode;
        if (hacks::safeActive()) m_isTestMode = true;
        PlayLayer::levelComplete();
        m_isTestMode = prevTest;
        stats::onComplete();
        bot::onComplete();
        render::onLevelComplete();
    }

    void onQuit() {
        predictor::onQuit();
        bot::onQuit();
        render::onQuit();
        saveConfigIfChanged();
        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(CCPoint position) {
        if (predictor::active()) {
            predictor::onWin();
            return;
        }
        PlayLayer::playEndAnimationToPos(position);
    }

    // Practice mode can place checkpoints by itself; not while Predict is using its own.
    void storeCheckpoint(CheckpointObject* checkpoint) {
        if (predictor::active()) return;
        PlayLayer::storeCheckpoint(checkpoint);
    }

    void delayedResetLevel() {
        // With a custom respawn time, we decide when to respawn (see hacks::engineTick).
        if (cfg().customRespawn && !hacks::g_ownRespawn) return;
        PlayLayer::delayedResetLevel();
    }

    void pauseGame(bool unfocused) {
        if (unfocused && cfg().noUnfocusPause) return;
        PlayLayer::pauseGame(unfocused);
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (cfg().noNewBest) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        visuals::percent(this);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!predictor::active()) visuals::postUpdate(this, dt);
    }
};

class $modify(SKGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (predictor::active() && !bot::injecting()) return;  // the Predict bot is driving
        auto pl = hacks::asPlay(this);
        if (!bot::injecting() && pl) {
            if (bot::blocksUser()) return;  // the macro is driving
            stats::onClick(down);
            bot::record(this, down, button, isPlayer1);
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (pl && !predictor::active()) {
            stats::onButton(down, button, isPlayer1);
            if (down) visuals::addClickMarker(pl, isPlayer1);
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (predictor::active()) {  // no cheats or other bots while Predict thinks
            predictor::tickBegin(this);
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            predictor::tickEnd(this);
            return;
        }
        bool inLevel = hacks::asPlay(this) != nullptr;
        if (inLevel) {
            hacks::beforeTick(this);
            bot::beforeTick(this);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (inLevel) {
            bot::afterTick(this);
            stats::afterTick(this);
            visuals::afterTick(this);
        }
    }

    void update(float dt) {
        // Predict: the bot runs the level itself, one tick at a time.
        if (predictor::active()) {
            if (auto pl = hacks::asPlay(this)) {
                predictor::frame(pl, dt, [this](float d) { GJBaseGameLayer::update(d); });
                visuals::postUpdate(pl, dt);  // hitboxes etc. once per frame, not every tick
                return;
            }
        }
        // Frame stepper: the level is frozen until you press the step key.
        if (cfg().frameStepper && !render::active() && hacks::asPlay(this)) {
            if (!hacks::consumeStep()) return;
            GJBaseGameLayer::update(1.f / 240.f);
            return;
        }
        GJBaseGameLayer::update(dt);
    }
};

class $modify(SKPlayer, PlayerObject) {
    void playDeathEffect() {
        if (cfg().noDeathEffect || predictor::active()) return;
        PlayerObject::playDeathEffect();
    }

    void playSpawnEffect() {
        if (cfg().noSpawnEffect || predictor::active()) return;
        PlayerObject::playSpawnEffect();
    }
};

// The Predict bot tries thousands of times: none of that counts toward your stats.
class $modify(SKStats, GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (predictor::active()) return;
        GameStatsManager::incrementStat(key, amount);
    }
};
