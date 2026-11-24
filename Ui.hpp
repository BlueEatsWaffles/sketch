#pragma once
#include "Config.hpp"

#include <imgui-cocos.hpp>

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
//  Hand-drawn style widgets with smooth animations.
// ---------------------------------------------------------------------------
namespace ui {

extern int* g_capturingKey;  // set while a keybind box is waiting for a key

float sc();  // UI scale
float rowH();
float dt();
void beginFrame(float dt);

// Smoothly moves a stored value toward `target` and returns it.
float anim(ImGuiID id, float target, float speed = 12.f);
void animSet(ImGuiID id, float value);

// --- drawing helpers -------------------------------------------------------
char const* labelEnd(char const* s);
void text(ImDrawList* dl, ImVec2 p, ImU32 col, char const* s, char const* e = nullptr, float size = 0.f);
ImVec2 textSize(char const* s, char const* e = nullptr, float size = 0.f);
void sketchRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thick, float rounding, uint32_t seed);
void squiggle(ImDrawList* dl, ImVec2 p, float width, ImU32 col, float thick, float amp, float phase);
void checkMark(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float thick);
void crossMark(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float thick);

// --- widgets -----------------------------------------------------------------
void section(char const* title);
void spacing(float h = 6.f);
bool toggle(char const* label, bool* v, char const* tip = nullptr);
bool sliderFloat(char const* label, float* v, float mn, float mx, char const* fmt = "%.2f", char const* tip = nullptr);
bool sliderInt(char const* label, int* v, int mn, int mx, char const* tip = nullptr);
bool button(char const* label, ImVec2 size = ImVec2(0, 0), bool highlight = false);
bool keybind(char const* label, int* key);
bool combo(char const* label, int* idx, char const* const* items, int count, char const* tip = nullptr);
bool segmented(char const* id, int* v, char const* const* items, int count);
bool colorEdit(char const* label, Col* c);
bool inputText(char const* label, char* buf, size_t size, char const* hint = nullptr);
void note(char const* text);
// level: 2 = good (green tick), 1 = optional/missing (yellow), 0 = required/missing (red cross)
void status(int level, char const* label, char const* detail);

// Sub-options that fade in under a toggle. Always call subEnd() if subBegin() returned true.
bool subBegin(char const* id, bool open);
void subEnd();

}  // namespace ui
