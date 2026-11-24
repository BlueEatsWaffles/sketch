#include "Theme.hpp"
#include "Util.hpp"

#include <cmath>

namespace theme {

namespace {

ThemeColors make(Col panel, Col blob, Col tab, Col tabActive, Col tabText, Col outline, Col text,
                 Col textDim, Col accent, Col accentText, Col field) {
    ThemeColors t;
    t.panel = panel;
    t.blob = blob;
    t.tab = tab;
    t.tabActive = tabActive;
    t.tabText = tabText;
    t.outline = outline;
    t.text = text;
    t.textDim = textDim;
    t.accent = accent;
    t.accentText = accentText;
    t.field = field;
    return t;
}

ThemeColors& liveStore() {
    static ThemeColors c = sketchColors();
    return c;
}

}  // namespace

std::vector<Preset> const& presets() {
    static std::vector<Preset> const list = {
        {"Sketch", sketchColors()},
        {"Paper", make(rgb(246, 244, 236), rgb(226, 221, 204), rgb(255, 214, 102), rgb(255, 234, 158),
                       rgb(30, 30, 30), rgb(22, 22, 22), rgb(28, 28, 28), rgb(105, 105, 100),
                       rgb(36, 36, 36), rgb(250, 250, 245), rgb(233, 229, 216))},
        {"Midnight", make(rgb(32, 36, 58), rgb(22, 25, 42), rgb(122, 138, 214), rgb(170, 184, 255),
                          rgb(12, 14, 28), rgb(8, 9, 16), rgb(236, 239, 255), rgb(150, 158, 200),
                          rgb(170, 184, 255), rgb(12, 14, 28), rgb(45, 51, 82))},
        {"Toast", make(rgb(122, 74, 42), rgb(96, 56, 30), rgb(238, 196, 128), rgb(255, 224, 164),
                       rgb(62, 33, 12), rgb(40, 20, 8), rgb(255, 242, 222), rgb(228, 192, 152),
                       rgb(255, 202, 92), rgb(62, 33, 12), rgb(100, 60, 33))},
        {"Mint", make(rgb(58, 112, 99), rgb(44, 90, 80), rgb(190, 240, 220), rgb(226, 255, 243),
                      rgb(18, 50, 42), rgb(10, 30, 26), rgb(242, 255, 250), rgb(172, 216, 202),
                      rgb(150, 255, 210), rgb(14, 44, 37), rgb(46, 92, 81))},
        {"Bubblegum", make(rgb(212, 112, 162), rgb(186, 90, 140), rgb(255, 214, 234), rgb(255, 238, 247),
                           rgb(92, 20, 60), rgb(70, 10, 45), rgb(255, 255, 255), rgb(255, 216, 236),
                           rgb(255, 255, 255), rgb(140, 40, 95), rgb(188, 94, 142))},
        {"Neon", make(rgb(16, 16, 21), rgb(8, 8, 12), rgb(0, 255, 200), rgb(130, 255, 232),
                      rgb(4, 22, 18), rgb(0, 222, 184), rgb(232, 255, 250), rgb(112, 182, 172),
                      rgb(0, 255, 200), rgb(4, 22, 18), rgb(27, 31, 36))},
        {"Lava", make(rgb(72, 22, 18), rgb(52, 14, 12), rgb(255, 122, 42), rgb(255, 172, 92),
                      rgb(42, 8, 4), rgb(15, 4, 2), rgb(255, 236, 222), rgb(232, 152, 122),
                      rgb(255, 142, 52), rgb(42, 8, 4), rgb(92, 33, 25))},
        {"Ocean", make(rgb(24, 78, 119), rgb(17, 60, 95), rgb(132, 206, 236), rgb(192, 236, 251),
                       rgb(8, 35, 55), rgb(5, 20, 35), rgb(240, 250, 255), rgb(162, 206, 230),
                       rgb(132, 232, 255), rgb(8, 35, 55), rgb(31, 96, 141))},
    };
    return list;
}

int customIndex() { return static_cast<int>(presets().size()); }

char const* nameOf(int index) {
    if (index >= 0 && index < customIndex()) return presets()[index].name;
    return "Custom";
}

ThemeColors const& colorsOf(int index) {
    if (index >= 0 && index < customIndex()) return presets()[index].c;
    return cfg().custom;
}

ThemeColors const& target() { return colorsOf(cfg().themeIndex); }

ThemeColors& live() { return liveStore(); }

void snapToTarget() { liveStore() = target(); }

Col mix(Col const& a, Col const& b, float t) {
    return Col{util::lerp(a.r, b.r, t), util::lerp(a.g, b.g, t), util::lerp(a.b, b.b, t),
               util::lerp(a.a, b.a, t)};
}

void update(float dt) {
    float sp = cfg().animSpeed;
    auto& L = liveStore();
    auto const& T = target();
    if (sp <= 0.01f) {
        L = T;
        return;
    }
    float k = 1.f - std::exp(-7.f * sp * dt);
    L.panel = mix(L.panel, T.panel, k);
    L.blob = mix(L.blob, T.blob, k);
    L.tab = mix(L.tab, T.tab, k);
    L.tabActive = mix(L.tabActive, T.tabActive, k);
    L.tabText = mix(L.tabText, T.tabText, k);
    L.outline = mix(L.outline, T.outline, k);
    L.text = mix(L.text, T.text, k);
    L.textDim = mix(L.textDim, T.textDim, k);
    L.accent = mix(L.accent, T.accent, k);
    L.accentText = mix(L.accentText, T.accentText, k);
    L.field = mix(L.field, T.field, k);
}

ImVec4 vec(Col const& c, float alpha) {
    return ImVec4(c.r, c.g, c.b, c.a * alpha);
}

ImU32 col(Col const& c, float alpha) {
    float a = util::clamp(c.a * alpha * ImGui::GetStyle().Alpha, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, a));
}

void applyStyle() {
    auto& s = ImGui::GetStyle();
    auto const& t = liveStore();
    auto const& c = cfg();
    float sc = c.uiScale;

    s.FontScaleMain = sc;
    s.WindowRounding = c.rounding * sc;
    s.ChildRounding = c.rounding * sc;
    s.FrameRounding = c.rounding * 0.7f * sc;
    s.PopupRounding = c.rounding * sc;
    s.GrabRounding = c.rounding * 0.7f * sc;
    s.ScrollbarRounding = 12.f * sc;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.PopupBorderSize = util::maxv(1.f, c.outline * 0.7f) * sc;
    s.FrameBorderSize = 0.f;
    s.ScrollbarSize = 9.f * sc;
    s.ItemSpacing = ImVec2(8.f * sc, 6.f * sc);
    s.FramePadding = ImVec2(8.f * sc, 5.f * sc);
    s.WindowPadding = ImVec2(10.f * sc, 10.f * sc);

    auto* C = s.Colors;
    C[ImGuiCol_Text] = vec(t.text);
    C[ImGuiCol_TextDisabled] = vec(t.textDim);
    C[ImGuiCol_WindowBg] = vec(t.panel);
    C[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_PopupBg] = vec(t.panel);
    C[ImGuiCol_Border] = vec(t.outline);
    C[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_FrameBg] = vec(t.field);
    C[ImGuiCol_FrameBgHovered] = vec(mix(t.field, t.accent, 0.12f));
    C[ImGuiCol_FrameBgActive] = vec(mix(t.field, t.accent, 0.22f));
    C[ImGuiCol_TitleBg] = vec(t.tab);
    C[ImGuiCol_TitleBgActive] = vec(t.tabActive);
    C[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_ScrollbarGrab] = vec(t.tab, 0.8f);
    C[ImGuiCol_ScrollbarGrabHovered] = vec(t.tabActive);
    C[ImGuiCol_ScrollbarGrabActive] = vec(t.accent);
    C[ImGuiCol_CheckMark] = vec(t.accent);
    C[ImGuiCol_SliderGrab] = vec(t.accent);
    C[ImGuiCol_SliderGrabActive] = vec(t.accent);
    C[ImGuiCol_Button] = vec(t.field);
    C[ImGuiCol_ButtonHovered] = vec(mix(t.field, t.accent, 0.2f));
    C[ImGuiCol_ButtonActive] = vec(mix(t.field, t.accent, 0.35f));
    C[ImGuiCol_Header] = vec(mix(t.field, t.accent, 0.25f));
    C[ImGuiCol_HeaderHovered] = vec(mix(t.field, t.accent, 0.35f));
    C[ImGuiCol_HeaderActive] = vec(mix(t.field, t.accent, 0.45f));
    C[ImGuiCol_Separator] = vec(t.outline);
    C[ImGuiCol_TextSelectedBg] = vec(t.accent, 0.35f);
}

}  // namespace theme
