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

PlayLayer* playLayer();
PlayLayer* asPlay(GJBaseGameLayer* g);

}  // namespace hacks
