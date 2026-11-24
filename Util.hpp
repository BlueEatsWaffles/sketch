#pragma once
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>

// Small helpers. (We avoid std::min/std::max on purpose: Windows.h likes to
// #define min/max as macros and break them.)
namespace util {

template <class T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline T minv(T a, T b) { return a < b ? a : b; }
template <class T> inline T maxv(T a, T b) { return a > b ? a : b; }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float easeOutCubic(float t) {
    t = clamp(t, 0.f, 1.f);
    float u = 1.f - t;
    return 1.f - u * u * u;
}

inline float easeOutBack(float t) {
    t = clamp(t, 0.f, 1.f);
    const float c1 = 1.70158f, c3 = c1 + 1.f;
    float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

inline float easeInOut(float t) {
    t = clamp(t, 0.f, 1.f);
    return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
}

// Name for a key code (GD key codes match Windows virtual-key codes).
std::string keyName(int key);

// Seconds since program start (steady clock).
double now();

// Local time (works with every compiler).
std::tm localTime(std::time_t t);

// UTF-8 <-> filesystem path helpers (safe for non-English folder names).
std::filesystem::path u8path(std::string const& s);
std::string pathStr(std::filesystem::path const& p);

// Keep only safe characters for a file name.
std::string safeFileName(std::string const& s);

}  // namespace util
