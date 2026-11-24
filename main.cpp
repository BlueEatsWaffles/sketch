// sketch (by BlueEatsWaffles) - entry point.
#include "Config.hpp"
#include "Gui.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

using namespace geode::prelude;

$execute {
    loadConfig();
    ImGuiCocos::get()
        .setup([] { gui::setup(); })
        .draw([] { gui::draw(); });
    ImGuiCocos::get().setVisible(true);  // we hide/show our own window, ImGui stays on for labels & popups
}
