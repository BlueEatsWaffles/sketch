#pragma once
#include <Geode/Geode.hpp>

namespace visuals {

void onLevelEnter();
void onReset(PlayLayer* pl);
void postUpdate(PlayLayer* pl, float dt);
void afterTick(GJBaseGameLayer* g);   // every physics tick (hit border path)
void percent(PlayLayer* pl);

bool isDebugNode(cocos2d::CCDrawNode* node);
void addDeathMarker(PlayLayer* pl, PlayerObject* p);
void addClickMarker(PlayLayer* pl, bool p1);
void clearDeathMarkers();
cocos2d::ccColor4F recolor(cocos2d::ccColor4F const& original);

}  // namespace visuals
