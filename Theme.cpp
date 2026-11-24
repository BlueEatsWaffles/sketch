#include "Theme.hpp"
#include "Util.hpp"

#include <cmath>

namespace theme {

namespace {

Col hex(unsigned v, float a = 1.f) {
    return rgb(static_cast<int>((v >> 16) & 0xFF), static_cast<int>((v >> 8) & 0xFF), static_cast<int>(v & 0xFF), a);
}

// One line per theme:
//   page top, page bottom, paint blob, tabs, picked tab, tab text, marker lines,
//   text, hint text, checkmarks, text on checkmarks, boxes, pattern color, pattern strength, pattern, glow
struct Row {
    char const* name;
    unsigned panel, panel2, blob, tab, tabActive, tabText, outline, text, textDim, accent, accentText, field, pattern;
    float patternAlpha;
    int kind;
    bool glow;
};

Row const kRows[] = {
    // the original look from your drawing
    {"Sketch", 0x636363, 0x5C5C5C, 0x636363, 0xBEBFBA, 0xE5E6E0, 0x161616, 0x000000, 0xFFFFFF, 0xD2D2CD, 0xFFFFFF, 0x1C1C1C, 0x525252, 0x000000, 0.10f, PatNone, false},
    {"Paper", 0xF6F4EC, 0xEFEBDD, 0xE2DDCC, 0xFFD666, 0xFFEA9E, 0x1E1E1E, 0x161616, 0x1C1C1C, 0x69695F, 0x242424, 0xFAFAF5, 0xE9E5D8, 0x7FA7D9, 0.55f, PatRuled, false},
    {"Midnight", 0x20243A, 0x151829, 0x16192A, 0x7A8AD6, 0xAAB8FF, 0x0C0E1C, 0x080910, 0xECEFFF, 0x969EC8, 0xAAB8FF, 0x0C0E1C, 0x2D3352, 0xFFFFFF, 0.60f, PatStars, false},
    {"Toast", 0x7A4A2A, 0x6A3E22, 0x60381E, 0xEEC480, 0xFFE0A4, 0x3E210C, 0x281408, 0xFFF2DE, 0xE4C098, 0xFFCA5C, 0x3E210C, 0x643C21, 0x000000, 0.10f, PatDots, false},
    {"Mint", 0x3A7063, 0x2F5F54, 0x2C5A50, 0xBEF0DC, 0xE2FFF3, 0x12322A, 0x0A1E1A, 0xF2FFFA, 0xACD8CA, 0x96FFD2, 0x0E2C25, 0x2E5C51, 0xFFFFFF, 0.07f, PatGrid, false},
    {"Bubblegum", 0xD470A2, 0xC05E90, 0xBA5A8C, 0xFFD6EA, 0xFFEEF7, 0x5C143C, 0x460A2D, 0xFFFFFF, 0xFFD8EC, 0xFFFFFF, 0x8C285F, 0xBC5E8E, 0xFFFFFF, 0.16f, PatDots, false},
    {"Neon", 0x101015, 0x0B0B10, 0x08080C, 0x00FFC8, 0x82FFE8, 0x04160F, 0x00DEB8, 0xE8FFFA, 0x70B6AC, 0x00FFC8, 0x041612, 0x1B1F24, 0x00FFC8, 0.10f, PatGrid, true},
    {"Lava", 0x481612, 0x2E0C0A, 0x340E0C, 0xFF7A2A, 0xFFAC5C, 0x2A0804, 0x0F0402, 0xFFECDE, 0xE8987A, 0xFF8E34, 0x2A0804, 0x5C2119, 0xFF5A1E, 0.12f, PatHatch, false},
    {"Ocean", 0x184E77, 0x103A5A, 0x113C5F, 0x84CEEC, 0xC0ECFB, 0x082337, 0x051423, 0xF0FAFF, 0xA2CEE6, 0x84E8FF, 0x082337, 0x1F608D, 0xFFFFFF, 0.07f, PatHalftone, false},
    // 20 new ones
    {"Blueprint", 0x1B4F9C, 0x154387, 0x123C7A, 0xD8E6FF, 0xFFFFFF, 0x0E2C5E, 0xF0F6FF, 0xFFFFFF, 0xB6CCF0, 0xFFFFFF, 0x1B4F9C, 0x1A4A92, 0xFFFFFF, 0.16f, PatGrid, false},
    {"Chalkboard", 0x2E4A3A, 0x263F31, 0x4A3422, 0xD9D2B4, 0xF2ECD2, 0x2A1E12, 0x1E140C, 0xF4F1E6, 0xC9CCBE, 0xFFFFFF, 0x2E4A3A, 0x3A5847, 0xFFFFFF, 0.06f, PatHatch, false},
    {"Graph Paper", 0xFBFDF8, 0xF2F7EE, 0xDDE8D6, 0x9BD3AE, 0xCFF0D8, 0x123122, 0x1B2A22, 0x1E2B24, 0x5E7366, 0x2E8B57, 0xFFFFFF, 0xEAF2E6, 0x2E8B57, 0.20f, PatGrid, false},
    {"Sticky Note", 0xFFE873, 0xFFDD4A, 0xF5C842, 0xFF9F43, 0xFFC27A, 0x4A2A00, 0x3B2A0A, 0x3B2A0A, 0x7A5E2A, 0xE8590C, 0xFFF7D6, 0xFFE15C, 0xC9A227, 0.40f, PatRuled, false},
    {"Cardboard", 0xB98B5E, 0xA97A4E, 0x9C6E45, 0xE9D3B0, 0xF7E8CF, 0x3B2612, 0x1A1208, 0x1F160C, 0x5A4128, 0x1F160C, 0xF7E8CF, 0xA47750, 0x5A4128, 0.14f, PatHatch, false},
    {"Sakura", 0xFCE4EC, 0xF8D0DE, 0xF4C2D3, 0xF48FB1, 0xFFD3E2, 0x5A1631, 0x6D1B3B, 0x5A1631, 0xA0627A, 0xE91E63, 0xFFFFFF, 0xF9D5E1, 0xF48FB1, 0.35f, PatDots, false},
    {"Matcha", 0xDCE8C8, 0xCBDDB2, 0xBFD3A2, 0x7FA35A, 0xB6D38C, 0x1F2E12, 0x2C3D1C, 0x22301A, 0x5F7550, 0x5A7F3A, 0xF3F8EA, 0xCDDDB5, 0x7FA35A, 0.18f, PatGrid, false},
    {"Grape Soda", 0x3E2163, 0x2E1849, 0x2A1645, 0xB8F35B, 0xDCFF9E, 0x1E1033, 0x120A1F, 0xF6EEFF, 0xBCA6DA, 0xB8F35B, 0x1E1033, 0x4E2C7A, 0xB8F35B, 0.10f, PatDots, true},
    {"Sunset", 0xFF8A5B, 0x7B3FA0, 0x5E2E82, 0xFFD27A, 0xFFE9B8, 0x4A1838, 0x2A0C24, 0xFFFFFF, 0xFFE0D2, 0xFFD27A, 0x4A1838, 0xB9587A, 0xFFFFFF, 0.09f, PatHalftone, false},
    {"Arctic", 0xEEF6FC, 0xDCEBF7, 0xCFE3F3, 0x9FD0F0, 0xD6EEFF, 0x0E2A44, 0x12324F, 0x0E2A44, 0x5D7B95, 0x2F8FD8, 0xFFFFFF, 0xDDEAF5, 0x5FA8E0, 0.45f, PatStars, false},
    {"Forest", 0x2F4A2C, 0x233A21, 0x4A3424, 0xA7C957, 0xD4E8A0, 0x1B2A12, 0x0F1A0B, 0xEEF5E2, 0xA9BF9A, 0xF2C94C, 0x2A2005, 0x3B5937, 0xA7C957, 0.08f, PatHatch, false},
    {"Terminal", 0x0A0F0A, 0x050805, 0x030503, 0x1AFF66, 0x9CFFC0, 0x001A08, 0x1AFF66, 0x7CFFA6, 0x2E9E57, 0x1AFF66, 0x001A08, 0x0F1A12, 0x1AFF66, 0.07f, PatScan, true},
    {"Vaporwave", 0x2B1055, 0x7597DE, 0x1C0B3C, 0xFF71CE, 0xFFB8EA, 0x2B1055, 0x120526, 0xFFFFFF, 0xE2D5FF, 0x01CDFE, 0x120526, 0x3D1A6E, 0xFF71CE, 0.20f, PatGrid, true},
    {"Coffee", 0x3B2A20, 0x2E2019, 0x2A1D15, 0xD9B99B, 0xF0DCC5, 0x2A1D15, 0x140D09, 0xF6EBDD, 0xC8AE95, 0xE3A869, 0x2A1D15, 0x4E382B, 0xD9B99B, 0.07f, PatDots, false},
    {"Bumblebee", 0x1C1C1C, 0x141414, 0xFFC800, 0xFFC800, 0xFFE066, 0x1C1C1C, 0x000000, 0xFFFFFF, 0xD9D2B0, 0xFFC800, 0x1C1C1C, 0x2E2E2E, 0xFFC800, 0.10f, PatHatch, false},
    {"Comic", 0xFFFFFF, 0xF4F4F4, 0x2D9CDB, 0xFFDA44, 0xFFF0A8, 0x111111, 0x000000, 0x111111, 0x5A5A5A, 0xE63946, 0xFFFFFF, 0xF1F1F1, 0x2D9CDB, 0.24f, PatHalftone, false},
    {"Galaxy", 0x1B0F3A, 0x0A0620, 0x120A2B, 0x9D7BFF, 0xC9B8FF, 0x120A2B, 0x05030F, 0xF3EEFF, 0xA99BD6, 0xFF9EF3, 0x120A2B, 0x2B1B57, 0xFFFFFF, 0.85f, PatStars, true},
    {"Ink", 0xFFFFFF, 0xF7F7F7, 0xE6E6E6, 0xFFFFFF, 0xF0F0F0, 0x000000, 0x000000, 0x000000, 0x5C5C5C, 0x000000, 0xFFFFFF, 0xF2F2F2, 0x000000, 0.07f, PatHatch, false},
    {"Lavender", 0xB9A7E0, 0xA893D4, 0x9F8BCC, 0xF3EBFF, 0xFFFFFF, 0x3A2A63, 0x2A1E4A, 0xFFFFFF, 0xEDE4FF, 0xFFFFFF, 0x4B3880, 0xA391CF, 0xFFFFFF, 0.14f, PatDots, false},
    {"Rose Gold", 0xE8B4A6, 0xD99A8A, 0xC98B7B, 0xFFE3D6, 0xFFF2EA, 0x5C2E24, 0x4A221A, 0x4A221A, 0x8A5A4E, 0xFFFFFF, 0x7A3E30, 0xF0C8BC, 0xFFFFFF, 0.18f, PatDots, false},
};

std::vector<Preset> build() {
    std::vector<Preset> out;
    for (auto const& r : kRows) {
        ThemeColors t;
        t.panel = hex(r.panel);
        t.panel2 = hex(r.panel2);
        t.blob = hex(r.blob);
        t.tab = hex(r.tab);
        t.tabActive = hex(r.tabActive);
        t.tabText = hex(r.tabText);
        t.outline = hex(r.outline);
        t.text = hex(r.text);
        t.textDim = hex(r.textDim);
        t.accent = hex(r.accent);
        t.accentText = hex(r.accentText);
        t.field = hex(r.field);
        t.pattern = hex(r.pattern, r.patternAlpha);
        t.patternKind = r.kind;
        t.glow = r.glow;
        out.push_back({r.name, t});
    }
    return out;
}

ThemeColors& liveStore() {
    static ThemeColors c = sketchColors();
    return c;
}

}  // namespace

std::vector<Preset> const& presets() {
    static std::vector<Preset> const list = build();
    return list;
}

char const* const kPatternNames[PatCount] = {"None", "Lined paper", "Grid", "Dots", "Pencil hatching", "Stars", "Scanlines", "Comic dots"};

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
    L.patternKind = T.patternKind;
    L.glow = T.glow;
    if (sp <= 0.01f) {
        L = T;
        return;
    }
    float k = 1.f - std::exp(-7.f * sp * dt);
    L.panel = mix(L.panel, T.panel, k);
    L.panel2 = mix(L.panel2, T.panel2, k);
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
    L.pattern = mix(L.pattern, T.pattern, k);
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
    C[ImGuiCol_PopupBg] = vec(mix(t.panel, t.panel2, 0.5f));
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
