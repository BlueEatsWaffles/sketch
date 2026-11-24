#pragma once
#include <Geode/Geode.hpp>

// Counters used by the labels (noclip accuracy, CPS, attempts...).
namespace stats {

void onLevelEnter();
void onReset(PlayLayer* pl);
void onNoclipHit(PlayLayer* pl);
void onDeath(PlayLayer* pl);
void onComplete();
void onClick(bool down);
void afterTick(GJBaseGameLayer* g);
void update(float dt);

int attempts();
float best();
float noclipAccuracy();
int noclipDeaths();
int cps();
int clicks();
float flash();

}  // namespace stats
