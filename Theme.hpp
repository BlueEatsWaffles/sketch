#pragma once
#include "Config.hpp"

#include <imgui-cocos.hpp>

#include <vector>

namespace theme {

struct Preset {
    char const* name;
    ThemeColors c;
};

extern char const* const kPatternNames[PatCount];  // names of the page patterns

std::vector<Preset> const& presets();
int customIndex();                // index of the "Custom" theme
char const* nameOf(int index);
ThemeColors const& colorsOf(int index);
ThemeColors const& target();      // the theme we are fading towards
ThemeColors& live();              // the colors on screen right now (smoothly animated)

void snapToTarget();              // jump instantly (used at start)
void update(float dt);            // fade live colors towards the target
void applyStyle();                // push colors/sizes into ImGui's style

Col mix(Col const& a, Col const& b, float t);
ImU32 col(Col const& c, float alpha = 1.f);   // includes current ImGui alpha
ImVec4 vec(Col const& c, float alpha = 1.f);

}  // namespace theme
