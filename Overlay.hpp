#pragma once
#include <string>

// Things drawn on top of the game: labels, toast popups, the red noclip
// flash and the "REC" badge while the video maker is running.
namespace overlay {

enum Kind { Info = 0, Good = 1, Warn = 2, Bad = 3 };

void toast(std::string msg, int kind = Info);  // safe to call from any thread
void draw(float dt);

}  // namespace overlay
