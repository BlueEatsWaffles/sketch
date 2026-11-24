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
void onButton(bool down, int button, bool p1);  // every click (yours or a bot's)
void afterTick(GJBaseGameLayer* g);
void update(float dt);

int attempts();
float best();
float noclipAccuracy();
int noclipDeaths();
int cps();
int clicks();
float flash();
bool held(int player);            // 0 = player 1, 1 = player 2
bool usedP2();
int sessionDeaths();
double sessionTime();
float const* frameTimes(int& count, int& start);  // ring buffer of frame times (ms)

}  // namespace stats
