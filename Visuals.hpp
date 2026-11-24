#pragma once
#include <Geode/Geode.hpp>

namespace visuals {

void onLevelEnter();
void onReset(PlayLayer* pl);
void postUpdate(PlayLayer* pl, float dt);
void percent(PlayLayer* pl);

bool isDebugNode(cocos2d::CCDrawNode* node);
cocos2d::ccColor4F recolor(cocos2d::ccColor4F const& original);

}  // namespace visuals
