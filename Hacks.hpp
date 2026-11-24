#pragma once
#include <Geode/Geode.hpp>

namespace hacks {

extern bool g_ownRespawn;   // true while WE call delayedResetLevel
extern bool g_inInit;       // true while PlayLayer::init is running

bool cheating();            // is any "unfair" hack on right now?
bool safeActive();          // should progress saving be blocked?
bool noclipBlocks(PlayLayer* pl, PlayerObject* p);

void onDeath(PlayLayer* pl);
void onReset(PlayLayer* pl);
void beforeTick(GJBaseGameLayer* g);

void requestStep();
bool consumeStep();

void engineTick(float realDt);   // every frame from CCScheduler::update
void requestFontReload();
void setSlowHeld(bool held);
float speedFactor(bool inLevel);  // speedhack x slow-motion

// Restart from the very start, closing the pause menu if it's open.
void restartFromStart(PlayLayer* pl);

PlayLayer* playLayer();
PlayLayer* asPlay(GJBaseGameLayer* g);

}  // namespace hacks
