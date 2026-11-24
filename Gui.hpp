#pragma once

namespace gui {

void setup();     // ImGui ready: load fonts
void draw();      // every frame
void toggle();    // open / close the menu
bool isOpen();

}  // namespace gui
