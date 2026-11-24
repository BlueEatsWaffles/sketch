#pragma once
#include <cstdint>
#include <vector>

// Tiny PNG writer (no compression library needed). Used by the screenshot key.
namespace png {
// rgb: width*height*3 bytes. bottomUp = true if the first row is the bottom one (OpenGL).
std::vector<uint8_t> encodeRgb(uint8_t const* rgb, int width, int height, bool bottomUp);
}
