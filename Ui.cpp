#include "Ui.hpp"
#include "Theme.hpp"
#include "Util.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

int* g_capturingKey = nullptr;

namespace {

struct Spring {
    float x, v;
};

std::unordered_map<ImGuiID, float> g_anims;
std::unordered_map<ImGuiID, Spring> g_springs;
float g_dt = 1.f / 60.f;
float g_enter = 10.f;  // seconds since the tab opened
int g_row = 0;         // row counter for the slide-in
ImGuiID g_editing = 0;
int g_editFrame = 0;

struct SubFrame {
    float y0;
    float x;
};
std::vector<SubFrame> g_sub;

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float rnd(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return static_cast<float>((s >> 8) & 0xFFFF) / 65535.f * 2.f - 1.f;
}

uint32_t boilSeed() {
    if (!cfg().lineBoil) return 0;
    return static_cast<uint32_t>(ImGui::GetTime() * 7.0);  // redraw lines 7x a second, like a cartoon
}

float radius() { return cfg().rounding * sc(); }
float thick() { return cfg().outline * sc(); }

void labelText(ImDrawList* dl, ImVec2 p, float h, float maxX, char const* label) {
    float fs = ImGui::GetFontSize();
    dl->PushClipRect(p, ImVec2(maxX, p.y + h), true);
    text(dl, ImVec2(p.x + 10.f * sc(), p.y + (h - fs) * 0.5f), theme::col(theme::live().text), label, labelEnd(label));
    dl->PopClipRect();
}

void tooltipFor(char const* tip) {
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tip);
}

// Every row slides in from the right with a little bounce when a tab opens.
struct RowAnim {
    bool pushed = false;
    RowAnim() {
        auto const& c = cfg();
        int idx = g_row++;
        if (!c.cascade || c.animSpeed <= 0.01f) return;
        if (idx > 18) idx = 18;
        float local = (g_enter * c.animSpeed - static_cast<float>(idx) * 0.028f) / 0.45f;
        if (local >= 1.f) return;
        float e = util::easeOutBack(util::clamp(local, 0.f, 1.f));
        float a = util::clamp(local * 2.5f, 0.f, 1.f);
        ImVec2 cur = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(cur.x + (1.f - e) * 44.f * sc() * (0.6f + 0.4f * c.bounce), cur.y));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
        pushed = true;
    }
    ~RowAnim() {
        if (pushed) ImGui::PopStyleVar();
    }
};

ImVec2 lerp2(ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

// One pencil stroke from a to b: slightly bent, a bit wobbly, ends overshoot.
void pencil(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float amp, float over, uint32_t& s) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
    float o0 = over * (0.35f + 0.65f * (rnd(s) * 0.5f + 0.5f));
    float o1 = over * (0.15f + 0.65f * (rnd(s) * 0.5f + 0.5f));
    ImVec2 p0(a.x - ux * o0, a.y - uy * o0), p1(b.x + ux * o1, b.y + uy * o1);
    float bow = rnd(s) * amp * 1.1f;
    float shift = rnd(s) * amp * 0.5f;
    int segs = util::clamp(static_cast<int>(len / (24.f * sc())), 2, 16);
    ImVec2 pts[18];
    for (int i = 0; i <= segs; i++) {
        float t = static_cast<float>(i) / static_cast<float>(segs);
        ImVec2 q = lerp2(p0, p1, t);
        float j = (i == 0 || i == segs) ? 0.f : rnd(s) * amp * 0.45f;
        float off = shift + std::sin(t * 3.14159f) * bow + j;
        pts[i] = ImVec2(q.x + nx * off, q.y + ny * off);
    }
    dl->AddPolyline(pts, segs + 1, col, 0, th);
}

ImU32 withAlpha(ImU32 c, float mul) {
    unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    a = static_cast<unsigned>(static_cast<float>(a) * util::clamp(mul, 0.f, 1.f));
    return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

}  // namespace

// ---------------------------------------------------------------------------

float sc() { return cfg().uiScale; }
float rowH() { return ImGui::GetFontSize() + 14.f * sc(); }
float ctrlW(float w) { return util::clamp(w * 0.42f, 150.f * sc(), 300.f * sc()); }
float dt() { return g_dt; }

void beginFrame(float d) {
    g_dt = util::clamp(d, 0.f, 0.1f);
    g_row = 0;
}

void setEnterTime(float seconds) {
    g_enter = seconds;
    g_row = 0;
}

float rowEnter(float* slideX) {
    auto const& c = cfg();
    int idx = g_row++;
    if (!c.cascade || c.animSpeed <= 0.01f) {
        if (slideX) *slideX = 0.f;
        return 1.f;
    }
    if (idx > 18) idx = 18;
    float local = util::clamp((g_enter * c.animSpeed - static_cast<float>(idx) * 0.028f) / 0.45f, 0.f, 1.f);
    if (slideX) *slideX = (1.f - util::easeOutBack(local)) * 44.f * sc();
    return util::easeOutBack(local);
}

float anim(ImGuiID id, float target, float speed) {
    auto it = g_anims.find(id);
    if (it == g_anims.end()) {
        g_anims.emplace(id, target);
        return target;
    }
    float& v = it->second;
    float sp = cfg().animSpeed;
    if (sp <= 0.01f) {
        v = target;
        return v;
    }
    float k = 1.f - std::exp(-speed * sp * g_dt);
    v += (target - v) * k;
    if (std::fabs(target - v) < 0.0015f) v = target;
    return v;
}

void animSet(ImGuiID id, float value) { g_anims[id] = value; }

float spring(ImGuiID id, float target, float stiffness, float damping) {
    auto it = g_springs.find(id);
    if (it == g_springs.end()) {
        g_springs.emplace(id, Spring{target, 0.f});
        return target;
    }
    Spring& s = it->second;
    auto const& c = cfg();
    float sp = c.animSpeed;
    if (sp <= 0.01f) {
        s.x = target;
        s.v = 0.f;
        return target;
    }
    // Less damping = more bounce. Bounciness 0 is calm, 2 is very springy.
    float k = stiffness * sp * sp;
    float d = damping * sp * (1.6f - 0.55f * util::clamp(c.bounce, 0.f, 2.f));
    int n = util::clamp(static_cast<int>(std::ceil(g_dt * 240.f)), 1, 30);
    float h = g_dt / static_cast<float>(n);
    for (int i = 0; i < n; i++) {
        float a = k * (target - s.x) - d * s.v;
        s.v += a * h;
        s.x += s.v * h;
    }
    if (std::fabs(target - s.x) < 0.0005f && std::fabs(s.v) < 0.002f) {
        s.x = target;
        s.v = 0.f;
    }
    return s.x;
}

void springSet(ImGuiID id, float value) { g_springs[id] = Spring{value, 0.f}; }

// ---------------------------------------------------------------------------
//  Drawing helpers
// ---------------------------------------------------------------------------

char const* labelEnd(char const* s) {
    char const* p = std::strstr(s, "##");
    return p ? p : s + std::strlen(s);
}

void text(ImDrawList* dl, ImVec2 p, ImU32 col, char const* s, char const* e, float size) {
    if (size <= 0.f) size = ImGui::GetFontSize();
    dl->AddText(ImGui::GetFont(), size, p, col, s, e);
}

ImVec2 textSize(char const* s, char const* e, float size) {
    if (size <= 0.f) size = ImGui::GetFontSize();
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, s, e);
}

void sketchRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float rounding, uint32_t seed) {
    auto const& c = cfg();
    auto const& t = theme::live();
    if (t.glow) {
        ImU32 g = theme::col(t.accent, 0.16f);
        dl->AddRect(a, b, g, rounding, 0, th * 3.4f);
        dl->AddRect(a, b, theme::col(t.accent, 0.22f), rounding, 0, th * 1.9f);
    }
    if (!c.sketchy || c.sketchAmount <= 0.01f) {
        dl->AddRect(a, b, col, rounding, 0, th);
        return;
    }
    float s = sc();
    float amt = c.sketchAmount;
    float w = b.x - a.x, h = b.y - a.y;
    float amp = util::clamp(th * 0.32f, 0.5f, 2.f) * amt;
    float over = util::minv((2.5f + 2.5f * amt) * s, util::minv(w, h) * 0.18f);
    ImVec2 cs[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
    uint32_t st = hash32(seed ^ (boilSeed() * 0x9E3779B9u)) | 1u;
    for (int e = 0; e < 4; e++) pencil(dl, cs[e], cs[(e + 1) % 4], col, th, amp, over, st);
    if (c.doubleLines) {
        // second, lighter pass: like going over a pencil line again
        ImU32 col2 = withAlpha(col, 0.55f);
        uint32_t st2 = hash32(seed * 7u + 3u + boilSeed()) | 1u;
        for (int e = 0; e < 4; e++) pencil(dl, cs[e], cs[(e + 1) % 4], col2, th * 0.5f, amp * 1.4f, over * 0.6f, st2);
    }
}

void sketchLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, uint32_t seed) {
    auto const& c = cfg();
    if (!c.sketchy || c.sketchAmount <= 0.01f) {
        dl->AddLine(a, b, col, th);
        return;
    }
    uint32_t st = hash32(seed ^ (boilSeed() * 0x9E3779B9u)) | 1u;
    float amp = util::clamp(th * 0.32f, 0.5f, 2.f) * c.sketchAmount;
    pencil(dl, a, b, col, th, amp, 2.f * sc() * c.sketchAmount, st);
    if (c.doubleLines) pencil(dl, a, b, withAlpha(col, 0.5f), th * 0.5f, amp * 1.4f, 1.2f * sc(), st);
}

void squiggle(ImDrawList* dl, ImVec2 p, float w, ImU32 col, float th, float amp, float phase) {
    if (w < 2.f) return;
    int n = util::clamp(static_cast<int>(w / (3.f * sc())), 4, 120);
    ImVec2 pts[121];
    float freq = 6.2831853f / (34.f * sc());
    for (int i = 0; i <= n; i++) {
        float x = w * static_cast<float>(i) / static_cast<float>(n);
        pts[i] = ImVec2(p.x + x, p.y + std::sin(x * freq + phase) * amp);
    }
    dl->AddPolyline(pts, n + 1, col, 0, th);
}

void checkMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float th, float progress) {
    if (progress <= 0.f) return;
    ImVec2 p0(c.x - size * 0.5f, c.y + size * 0.02f);
    ImVec2 p1(c.x - size * 0.12f, c.y + size * 0.38f);
    ImVec2 p2(c.x + size * 0.5f, c.y - size * 0.38f);
    float l1 = std::hypot(p1.x - p0.x, p1.y - p0.y), l2 = std::hypot(p2.x - p1.x, p2.y - p1.y);
    float d = util::clamp(progress, 0.f, 1.f) * (l1 + l2);
    if (d <= l1) {
        dl->AddLine(p0, lerp2(p0, p1, d / l1), col, th);
        return;
    }
    ImVec2 pts[3] = {p0, p1, lerp2(p1, p2, (d - l1) / l2)};
    dl->AddPolyline(pts, 3, col, 0, th);
}

void crossMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float th) {
    float h = size * 0.4f;
    dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, th);
    dl->AddLine(ImVec2(c.x + h, c.y - h), ImVec2(c.x - h, c.y + h), col, th);
}

void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float spacing, float th, uint32_t seed) {
    if (spacing < 2.f) spacing = 2.f;
    uint32_t s = hash32(seed) | 1u;
    float h = b.y - a.y;
    dl->PushClipRect(a, b, true);
    for (float x = a.x - h; x < b.x; x += spacing) {
        float j0 = rnd(s) * spacing * 0.3f, j1 = rnd(s) * spacing * 0.3f;
        float al = 0.6f + 0.4f * (rnd(s) * 0.5f + 0.5f);
        dl->AddLine(ImVec2(x + j0, b.y), ImVec2(x + h + j1, a.y), withAlpha(col, al), th);
    }
    dl->PopClipRect();
}

void drawPattern(ImDrawList* dl, ImVec2 a, ImVec2 b, ThemeColors const& t, float k, uint32_t seed) {
    if (t.patternKind == PatNone || t.pattern.a <= 0.001f) return;
    ImU32 col = theme::col(t.pattern);
    float w = b.x - a.x, h = b.y - a.y;
    float time = static_cast<float>(ImGui::GetTime());
    dl->PushClipRect(a, b, true);
    switch (t.patternKind) {
        case PatRuled: {
            for (float y = a.y + 34.f * k; y < b.y; y += 24.f * k) dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col, 1.2f * k);
            dl->AddLine(ImVec2(a.x + 30.f * k, a.y), ImVec2(a.x + 30.f * k, b.y), theme::col(rgb(235, 95, 95, t.pattern.a)), 1.4f * k);
            break;
        }
        case PatGrid: {
            float g = 20.f * k;
            int i = 0;
            for (float x = a.x; x < b.x; x += g, i++) dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), withAlpha(col, i % 5 ? 0.55f : 1.f), 1.f * k);
            i = 0;
            for (float y = a.y; y < b.y; y += g, i++) dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), withAlpha(col, i % 5 ? 0.55f : 1.f), 1.f * k);
            break;
        }
        case PatDots: {
            float g = 18.f * k;
            for (float y = a.y + g * 0.5f; y < b.y; y += g)
                for (float x = a.x + g * 0.5f; x < b.x; x += g) dl->AddCircleFilled(ImVec2(x, y), 1.3f * k, col, 6);
            break;
        }
        case PatHatch: hatch(dl, a, b, col, 12.f * k, 1.1f * k, seed); break;
        case PatStars: {
            uint32_t s = hash32(seed ^ 0x5717u) | 1u;
            int n = util::clamp(static_cast<int>(w * h / (2600.f * k * k)), 6, 260);
            for (int i = 0; i < n; i++) {
                float x = a.x + (rnd(s) * 0.5f + 0.5f) * w, y = a.y + (rnd(s) * 0.5f + 0.5f) * h;
                float r = (0.8f + (rnd(s) * 0.5f + 0.5f) * 1.8f) * k;
                float tw = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(time * (1.2f + static_cast<float>(i % 5) * 0.4f) + static_cast<float>(i)));
                ImU32 c2 = withAlpha(col, tw);
                if (i % 7 == 0) {  // little sparkle
                    dl->AddLine(ImVec2(x - r * 2.2f, y), ImVec2(x + r * 2.2f, y), c2, 1.f * k);
                    dl->AddLine(ImVec2(x, y - r * 2.2f), ImVec2(x, y + r * 2.2f), c2, 1.f * k);
                }
                dl->AddCircleFilled(ImVec2(x, y), r * 0.7f, c2, 6);
            }
            break;
        }
        case PatScan: {
            for (float y = a.y; y < b.y; y += 4.f * k) dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col, 1.f * k);
            float band = std::fmod(time * 70.f * k, h + 80.f * k) - 40.f * k;
            dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + band), ImVec2(b.x, a.y + band + 40.f * k), withAlpha(col, 0.f),
                                        withAlpha(col, 0.f), withAlpha(col, 1.f), withAlpha(col, 1.f));
            break;
        }
        case PatHalftone: {
            float g = 13.f * k;
            for (float y = a.y + g * 0.5f; y < b.y; y += g)
                for (float x = a.x + g * 0.5f; x < b.x; x += g) {
                    float f = ((x - a.x) / w) * 0.45f + ((y - a.y) / h) * 0.55f;
                    dl->AddCircleFilled(ImVec2(x, y), (0.4f + 3.2f * f * f) * k, col, 8);
                }
            break;
        }
        default: break;
    }
    dl->PopClipRect();
}

void tape(ImDrawList* dl, ImVec2 c, float angle, float w, float h) {
    float ca = std::cos(angle), sa = std::sin(angle);
    auto pt = [&](float x, float y) { return ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca); };
    ImU32 fill = theme::col(rgb(250, 240, 205, 0.62f));
    ImU32 edge = theme::col(rgb(140, 120, 80, 0.35f));
    ImVec2 p0 = pt(-w / 2, -h / 2), p1 = pt(w / 2, -h / 2), p2 = pt(w / 2, h / 2), p3 = pt(-w / 2, h / 2);
    dl->AddQuadFilled(p0, p1, p2, p3, fill);
    // torn ends
    for (int side = -1; side <= 1; side += 2) {
        ImVec2 z[6];
        for (int i = 0; i < 6; i++) {
            float y = -h / 2 + h * static_cast<float>(i) / 5.f;
            float x = side * (w / 2 + ((i % 2) ? -2.5f : 0.5f) * sc());
            z[i] = pt(x, y);
        }
        dl->AddPolyline(z, 6, edge, 0, 1.f * sc());
    }
    dl->AddLine(p0, p1, edge, 1.f * sc());
    dl->AddLine(p3, p2, edge, 1.f * sc());
}

void doodleStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    ImVec2 pts[11];
    for (int i = 0; i < 10; i++) {
        float a = -1.5708f + 3.14159f * static_cast<float>(i) / 5.f;
        float rr = (i % 2) ? r * 0.45f : r;
        pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    pts[10] = pts[0];
    dl->AddPolyline(pts, 11, col, 0, th);
}

void doodleSpiral(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    ImVec2 pts[48];
    for (int i = 0; i < 48; i++) {
        float t = static_cast<float>(i) / 47.f;
        float a = t * 6.2831853f * 2.4f;
        pts[i] = ImVec2(c.x + std::cos(a) * r * t, c.y + std::sin(a) * r * t);
    }
    dl->AddPolyline(pts, 48, col, 0, th);
}

// ---------------------------------------------------------------------------
//  Widgets
// ---------------------------------------------------------------------------

void spacing(float h) { ImGui::Dummy(ImVec2(1.f, h * sc())); }

void section(char const* title) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGui::Dummy(ImVec2(1.f, 8.f * s));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fs = ImGui::GetFontSize() * 1.18f;
    ImVec2 ts = textSize(title, nullptr, fs);
    text(dl, p, theme::col(t.text), title, nullptr, fs);
    // the underline "writes itself" when the tab opens
    float write = util::clamp((g_enter * util::maxv(cfg().animSpeed, 0.01f) - 0.08f) / 0.5f, 0.f, 1.f);
    if (cfg().animSpeed <= 0.01f) write = 1.f;
    float phase = cfg().lineBoil ? static_cast<float>(ImGui::GetTime()) * 3.f
                                 : static_cast<float>(hash32(ImGui::GetID(title)) % 100) * 0.1f;
    squiggle(dl, ImVec2(p.x, p.y + fs + 4.f * s), (ts.x + 18.f * s) * util::easeOutCubic(write), theme::col(t.accent, 0.9f),
             2.2f * s, 2.2f * s, phase);
    ImGui::Dummy(ImVec2(util::maxv(ts.x, 1.f), fs + 10.f * s));
}

bool toggle(char const* label, bool* v, char const* tip) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 40.f);
    float h = rowH();

    bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    if (pressed) *v = !*v;
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    tooltipFor(tip);

    float ha = spring(id ^ 0x1111u, hov ? 1.f : 0.f, 220.f, 16.f);
    float on = spring(id, *v ? 1.f : 0.f, 260.f, 13.f);        // overshoots: the box "pops"
    float sq = spring(id ^ 0x2222u, act ? 1.f : 0.f, 420.f, 18.f);  // squish while pressed
    float r = radius();

    if (ha > 0.01f) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(t.field, 0.55f * util::clamp(ha, 0.f, 1.f)), r);

    float fs = ImGui::GetFontSize();
    float bs = (fs + 4.f * s) * (1.f + 0.07f * ha - 0.12f * sq);
    ImVec2 c(p.x + w - (fs + 4.f * s) * 0.5f - 8.f * s, p.y + h * 0.5f);
    ImVec2 b0(c.x - bs * 0.5f, c.y - bs * 0.5f), b1(c.x + bs * 0.5f, c.y + bs * 0.5f);

    dl->PushClipRect(p, ImVec2(b0.x - 6.f * s, p.y + h), true);
    text(dl, ImVec2(p.x + 10.f * s + ha * 4.f * s, p.y + (h - fs) * 0.5f), theme::col(t.text), label, labelEnd(label));
    dl->PopClipRect();

    dl->AddRectFilled(b0, b1, theme::col(t.field), r * 0.6f);
    if (on > 0.02f) {
        float inset = util::clamp((1.f - on) * bs * 0.5f + 3.f * s, -2.f * s, bs * 0.5f);
        dl->AddRectFilled(ImVec2(b0.x + inset, b0.y + inset), ImVec2(b1.x - inset, b1.y - inset),
                          theme::col(t.accent, util::clamp(on, 0.f, 1.f)), r * 0.4f);
    }
    sketchRect(dl, b0, b1, theme::col(t.outline), thick() * 0.85f, r * 0.6f, id);
    if (*v || on > 0.3f) {
        float prog = util::clamp((on - 0.25f) / 0.6f, 0.f, 1.f);
        checkMark(dl, c, bs * 0.52f, theme::col(t.accentText), 2.4f * s, prog);
    }
    return pressed;
}

static bool sliderCore(char const* label, float* v, float mn, float mx, char const* fmt, bool isInt, char const* tip) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float trackW = ctrlW(w);
    ImVec2 t0(p.x + w - trackW - 8.f * s, p.y + 4.f * s);
    ImVec2 t1(p.x + w - 8.f * s, p.y + h - 4.f * s);
    bool changed = false;

    ImGui::PushID(label);
    if (g_editing == id) {
        // typing an exact number
        ImGui::SetCursorScreenPos(t0);
        ImGui::SetNextItemWidth(t1.x - t0.x);
        if (ImGui::GetFrameCount() <= g_editFrame) ImGui::SetKeyboardFocusHere();
        bool ch = false;
        if (isInt) {
            int iv = static_cast<int>(std::lround(*v));
            ch = ImGui::InputInt("##edit", &iv, 0, 0);
            if (ch) *v = static_cast<float>(iv);
        } else {
            ch = ImGui::InputFloat("##edit", v, 0.f, 0.f, fmt);
        }
        if (ch) changed = true;
        bool done = ImGui::IsItemDeactivated() ||
                    (!ImGui::IsItemActive() && ImGui::GetFrameCount() > g_editFrame + 2) ||
                    ImGui::IsKeyPressed(ImGuiKey_Enter);
        if (done) {
            g_editing = 0;
            *v = util::clamp(*v, mn, mx);
            changed = true;
        }
        sketchRect(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::col(t.accent), thick() * 0.8f, radius() * 0.7f, id);
    } else {
        ImGui::SetCursorScreenPos(t0);
        ImGui::InvisibleButton("##track", ImVec2(t1.x - t0.x, t1.y - t0.y));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (active) {
            float f = util::clamp((ImGui::GetIO().MousePos.x - t0.x) / (t1.x - t0.x), 0.f, 1.f);
            float nv = mn + f * (mx - mn);
            if (isInt) nv = std::round(nv);
            if (nv != *v) {
                *v = nv;
                changed = true;
            }
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_editing = id;
            g_editFrame = ImGui::GetFrameCount() + 1;
        }
        if (!active && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            if (tip) ImGui::SetTooltip("%s\n(drag, or double-click to type a number)", tip);
            else ImGui::SetTooltip("Drag, or double-click to type a number");
        }

        float frac = (mx > mn) ? util::clamp((*v - mn) / (mx - mn), 0.f, 1.f) : 0.f;
        float af = util::clamp(spring(id, frac, 240.f, 15.f), 0.f, 1.f);
        float ha = spring(id ^ 0x5151u, (hovered || active) ? 1.f : 0.f, 220.f, 16.f);
        float grow = 1.5f * s * util::clamp(ha, 0.f, 1.2f);
        ImVec2 a0(t0.x, t0.y - grow * 0.5f), a1(t1.x, t1.y + grow * 0.5f);
        float r = radius() * 0.7f;
        dl->AddRectFilled(a0, a1, theme::col(t.field), r);
        float fx = a0.x + (a1.x - a0.x) * af;
        if (fx > a0.x + 1.f) dl->AddRectFilled(a0, ImVec2(fx, a1.y), theme::col(t.accent, 0.75f + 0.25f * util::clamp(ha, 0.f, 1.f)), r);
        dl->AddLine(ImVec2(fx, a0.y + 2.f * s), ImVec2(fx, a1.y - 2.f * s), theme::col(t.outline), 2.5f * s);
        sketchRect(dl, a0, a1, theme::col(t.outline), thick() * 0.8f, r, id);

        char buf[64];
        if (isInt) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(*v)));
        else std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = textSize(buf);
        ImVec2 tp(a0.x + ((a1.x - a0.x) - vs.x) * 0.5f, a0.y + ((a1.y - a0.y) - vs.y) * 0.5f);
        // two-tone value text: dark on the filled part, light on the empty part
        dl->PushClipRect(a0, ImVec2(fx, a1.y), true);
        text(dl, tp, theme::col(t.accentText), buf);
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(fx, a0.y), a1, true);
        text(dl, tp, theme::col(t.text), buf);
        dl->PopClipRect();
    }
    ImGui::PopID();

    labelText(dl, p, h, t0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

bool sliderFloat(char const* label, float* v, float mn, float mx, char const* fmt, char const* tip) {
    return sliderCore(label, v, mn, mx, fmt, false, tip);
}

bool sliderInt(char const* label, int* v, int mn, int mx, char const* tip) {
    float f = static_cast<float>(*v);
    bool ch = sliderCore(label, &f, static_cast<float>(mn), static_cast<float>(mx), "%d", true, tip);
    if (ch) *v = util::clamp(static_cast<int>(std::lround(f)), mn, mx);
    return ch;
}

bool button(char const* label, ImVec2 size, bool highlight) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    char const* le = labelEnd(label);
    ImVec2 ts = textSize(label, le);
    float avail = ImGui::GetContentRegionAvail().x;
    if (size.x == 0.f) size.x = ts.x + 30.f * s;
    else if (size.x < 0.f) size.x = util::maxv(avail, 10.f);
    if (size.y <= 0.f) size.y = rowH();

    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    if (pressed) springSet(id ^ 0xB1B1u, 1.4f);  // a little "boing" on click

    float ha = spring(id ^ 0xB0B0u, hov ? 1.f : 0.f, 220.f, 15.f);
    float pa = spring(id ^ 0xB1B1u, act ? 1.f : 0.f, 380.f, 14.f);
    float hl = anim(id ^ 0xB2B2u, highlight ? 1.f : 0.f, 10.f);
    float r = radius() * 0.8f;
    float lift = ha * 1.8f * s;
    float push = pa * 2.2f * s;
    float sh = util::clamp(3.f - 2.f * pa, 0.5f, 4.f) * s;
    // squash and stretch
    float cx = p.x + size.x * 0.5f - 1.f * s;
    float hw = (size.x * 0.5f - 2.f * s) * (1.f + 0.03f * pa);
    float hh = (size.y * 0.5f - 2.f * s) * (1.f - 0.06f * pa);
    float cy = p.y + size.y * 0.5f - 1.f * s - lift + push;
    ImVec2 a(cx - hw, cy - hh), b(cx + hw, cy + hh);
    dl->AddRectFilled(ImVec2(a.x + sh, a.y + sh), ImVec2(b.x + sh, b.y + sh), theme::col(t.outline, 0.55f), r);
    Col fill = theme::mix(t.field, t.accent, util::clamp(0.16f * ha + 0.84f * hl, 0.f, 1.f));
    dl->AddRectFilled(a, b, theme::col(fill), r);
    sketchRect(dl, a, b, theme::col(t.outline), thick() * 0.8f, r, id);
    Col tc = theme::mix(t.text, t.accentText, hl);
    dl->PushClipRect(a, b, true);
    text(dl, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), theme::col(tc), label, le);
    dl->PopClipRect();
    return pressed;
}

bool keybind(char const* label, int* key) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = ctrlW(w);
    ImVec2 b0(p.x + w - bw - 8.f * s, p.y + 4.f * s);
    ImVec2 b1(p.x + w - 8.f * s, p.y + h - 4.f * s);
    bool changed = false;

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(b0);
    bool pressed = ImGui::InvisibleButton("##kb", ImVec2(b1.x - b0.x, b1.y - b0.y));
    bool hov = ImGui::IsItemHovered();
    if (pressed) g_capturingKey = (g_capturingKey == key) ? nullptr : key;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        *key = -1;
        if (g_capturingKey == key) g_capturingKey = nullptr;
        changed = true;
    }
    if (hov) ImGui::SetTooltip("Click, then press a key.\nRight-click = no key.");
    ImGui::PopID();

    bool capturing = g_capturingKey == key;
    float ca = spring(id, capturing ? 1.f : 0.f, 200.f, 13.f);
    float ha = spring(id ^ 0x4B4Bu, hov ? 1.f : 0.f, 220.f, 16.f);
    float pulse = capturing ? (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.f)) : 0.f;
    float r = radius() * 0.7f;
    float grow = 1.5f * s * util::clamp(ha + ca, 0.f, 1.3f);
    ImVec2 a0(b0.x - grow, b0.y - grow * 0.5f), a1(b1.x + grow, b1.y + grow * 0.5f);
    Col fill = theme::mix(t.field, t.accent, util::clamp(0.14f * ha + ca * (0.35f + 0.35f * pulse), 0.f, 1.f));
    dl->AddRectFilled(a0, a1, theme::col(fill), r);
    sketchRect(dl, a0, a1, theme::col(t.outline), thick() * 0.8f, r, id);
    std::string txt = capturing ? std::string("press a key...") : util::keyName(*key);
    ImVec2 ts = textSize(txt.c_str());
    Col tc = theme::mix(t.text, t.accentText, util::clamp(ca, 0.f, 1.f) * 0.8f);
    text(dl, ImVec2((a0.x + a1.x - ts.x) * 0.5f, (a0.y + a1.y - ts.y) * 0.5f), theme::col(tc), txt.c_str());

    labelText(dl, p, h, b0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

bool combo(char const* label, int* idx, char const* const* items, int count, char const* tip) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = ctrlW(w);
    ImVec2 b0(p.x + w - bw - 8.f * s, p.y + 4.f * s);
    ImVec2 b1(p.x + w - 8.f * s, p.y + h - 4.f * s);
    bool changed = false;
    if (*idx < 0 || *idx >= count) *idx = 0;

    ImGui::PushID(label);
    ImGuiID animId = ImGui::GetID("##popanim");
    ImGui::SetCursorScreenPos(b0);
    bool pressed = ImGui::InvisibleButton("##cb", ImVec2(b1.x - b0.x, b1.y - b0.y));
    bool hov = ImGui::IsItemHovered();
    if (pressed) {
        ImGui::OpenPopup("##pop");
        springSet(animId, 0.f);
    }
    bool open = ImGui::IsPopupOpen("##pop");
    ImGui::SetNextWindowPos(ImVec2(b0.x, b1.y + 4.f * s));
    ImGui::SetNextWindowSizeConstraints(ImVec2(b1.x - b0.x, 0.f), ImVec2(b1.x - b0.x + 80.f * s, 320.f * s));
    if (ImGui::BeginPopup("##pop")) {
        float a = spring(animId, 1.f, 260.f, 14.f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * util::clamp(a, 0.f, 1.f));
        ImGui::Dummy(ImVec2(1.f, util::maxv(0.f, (1.f - a) * 10.f * s)));  // drops down with a bounce
        for (int i = 0; i < count; i++) {
            bool sel = (i == *idx);
            if (ImGui::Selectable(items[i], sel)) {
                *idx = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopID();

    float ha = spring(id ^ 0xC0C0u, (hov || open) ? 1.f : 0.f, 220.f, 16.f);
    float oa = spring(id ^ 0xC1C1u, open ? 1.f : 0.f, 240.f, 14.f);
    float r = radius() * 0.7f;
    dl->AddRectFilled(b0, b1, theme::col(theme::mix(t.field, t.accent, 0.14f * util::clamp(ha, 0.f, 1.f))), r);
    sketchRect(dl, b0, b1, theme::col(t.outline), thick() * 0.8f, r, id);
    float fs = ImGui::GetFontSize();
    dl->PushClipRect(b0, ImVec2(b1.x - 22.f * s, b1.y), true);
    text(dl, ImVec2(b0.x + 8.f * s, (b0.y + b1.y - fs) * 0.5f), theme::col(t.text), items[*idx]);
    dl->PopClipRect();
    // chevron that flips (and wobbles) when open
    ImVec2 c(b1.x - 12.f * s, (b0.y + b1.y) * 0.5f);
    float d = 4.f * s * (1.f - 2.f * oa);
    dl->AddTriangleFilled(ImVec2(c.x - 5.f * s, c.y - d * 0.6f), ImVec2(c.x + 5.f * s, c.y - d * 0.6f),
                          ImVec2(c.x, c.y + d * 0.9f), theme::col(t.text));

    labelText(dl, p, h, b0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    tooltipFor(tip);
    return changed;
}

bool segmented(char const* idl, int* v, char const* const* items, int count) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(idl);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x - 8.f * s, 60.f);
    float h = rowH();
    float segW = w / static_cast<float>(count);
    bool changed = false;

    ImGui::PushID(idl);
    for (int i = 0; i < count; i++) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + segW * i, p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(segW, h)) && *v != i) {
            *v = i;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    float pos = spring(id, static_cast<float>(*v), 230.f, 14.f);  // the highlight slides and wobbles
    float r = radius() * 0.8f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(t.field), r);
    float cp = util::clamp(pos, -0.15f, static_cast<float>(count) - 0.85f);
    ImVec2 h0(p.x + segW * cp + 3.f * s, p.y + 3.f * s);
    ImVec2 h1(h0.x + segW - 6.f * s, p.y + h - 3.f * s);
    dl->AddRectFilled(h0, h1, theme::col(t.accent), r * 0.8f);
    sketchRect(dl, h0, h1, theme::col(t.outline), thick() * 0.6f, r * 0.8f, id ^ 7u);
    for (int i = 0; i < count; i++) {
        float closeness = util::clamp(1.f - std::fabs(pos - static_cast<float>(i)), 0.f, 1.f);
        ImVec2 ts = textSize(items[i]);
        ImVec2 c0(p.x + segW * i, p.y), c1(p.x + segW * (i + 1), p.y + h);
        dl->PushClipRect(c0, c1, true);
        text(dl, ImVec2((c0.x + c1.x - ts.x) * 0.5f, (c0.y + c1.y - ts.y) * 0.5f),
             theme::col(theme::mix(t.text, t.accentText, closeness)), items[i]);
        dl->PopClipRect();
    }
    sketchRect(dl, p, ImVec2(p.x + w, p.y + h), theme::col(t.outline), thick() * 0.8f, r, id);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

bool colorEdit(char const* label, Col* c) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = util::minv(64.f * s, ctrlW(w));
    ImVec2 b0(p.x + w - bw - 8.f * s, p.y + 5.f * s);
    ImVec2 b1(p.x + w - 8.f * s, p.y + h - 5.f * s);
    bool changed = false;

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(b0);
    bool pressed = ImGui::InvisibleButton("##sw", ImVec2(b1.x - b0.x, b1.y - b0.y));
    bool hov = ImGui::IsItemHovered();
    if (pressed) ImGui::OpenPopup("##picker");
    if (ImGui::BeginPopup("##picker")) {
        ImGui::SetNextItemWidth(220.f * s);
        changed = ImGui::ColorPicker4("##pick", &c->r,
                                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview |
                                          ImGuiColorEditFlags_PickerHueWheel);
        ImGui::EndPopup();
    }
    ImGui::PopID();

    float ha = spring(id, hov ? 1.f : 0.f, 240.f, 13.f);
    float lift = ha * 2.5f * s;
    ImVec2 a(b0.x, b0.y - lift), b(b1.x, b1.y - lift);
    float r = radius() * 0.6f;
    float mid = (a.x + b.x) * 0.5f;
    dl->AddRectFilled(a, ImVec2(mid, b.y), theme::col(rgb(235, 235, 235)), r, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(mid, a.y), b, theme::col(rgb(150, 150, 150)), r, ImDrawFlags_RoundCornersRight);
    dl->AddRectFilled(a, b, theme::col(*c), r);
    sketchRect(dl, a, b, theme::col(t.outline), thick() * 0.8f, r, id);

    labelText(dl, p, h, b0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

bool inputText(char const* label, char* buf, size_t size, char const* hint) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float iw = ctrlW(w);
    float frameH = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f;
    ImVec2 i0(p.x + w - iw - 8.f * s, p.y + (h - frameH) * 0.5f);

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(i0);
    ImGui::SetNextItemWidth(iw);
    bool changed = ImGui::InputTextWithHint("##in", hint ? hint : "", buf, size);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    bool active = ImGui::IsItemActive();
    ImGui::PopID();

    float aa = spring(id, active ? 1.f : 0.f, 220.f, 15.f);
    sketchRect(dl, a, b, theme::col(theme::mix(t.outline, t.accent, util::clamp(aa, 0.f, 1.f) * 0.7f)), thick() * 0.75f,
               radius() * 0.7f, id);
    labelText(dl, p, h, i0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

void note(char const* txt) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Indent(16.f * s);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(t.textDim));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(txt);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Unindent(16.f * s);
    ImVec2 mx = ImGui::GetItemRectMax();
    dl->AddRectFilled(ImVec2(p.x + 5.f * s, p.y + 1.f * s), ImVec2(p.x + 8.f * s, mx.y - 1.f * s),
                      theme::col(t.accent, 0.7f), 2.f * s);
    ImGui::Dummy(ImVec2(1.f, 2.f * s));
}

void status(int level, char const* label, char const* detail) {
    RowAnim ra;
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fs = ImGui::GetFontSize();
    float ic = fs + 2.f * s;
    ImVec2 c(p.x + 8.f * s + ic * 0.5f, p.y + ic * 0.5f);
    Col cc = level >= 2 ? rgb(80, 220, 120) : (level == 1 ? rgb(255, 200, 60) : rgb(255, 80, 80));
    dl->AddCircleFilled(c, ic * 0.5f, theme::col(cc));
    dl->AddCircle(c, ic * 0.5f, theme::col(t.outline), 0, 2.f * s);
    ImU32 ink = theme::col(rgb(20, 20, 20));
    if (level >= 2) {
        checkMark(dl, c, ic * 0.55f, ink, 2.2f * s);
    } else if (level == 1) {
        dl->AddLine(ImVec2(c.x, c.y - ic * 0.25f), ImVec2(c.x, c.y + ic * 0.08f), ink, 2.4f * s);
        dl->AddCircleFilled(ImVec2(c.x, c.y + ic * 0.26f), 1.6f * s, ink);
    } else {
        crossMark(dl, c, ic * 0.6f, ink, 2.2f * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + ic + 18.f * s, p.y));
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    if (detail && *detail) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(t.textDim));
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(detail);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1.f, 4.f * s));
}

bool subBegin(char const* id, bool open) {
    float a = spring(ImGui::GetID(id), open ? 1.f : 0.f, 240.f, 16.f);
    if (a < 0.02f && !open) return false;
    float s = sc();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * util::clamp(a, 0.f, 1.f));
    ImGui::Indent(24.f * s);
    ImVec2 p = ImGui::GetCursorScreenPos();
    g_sub.push_back({p.y, p.x - 12.f * s});
    return true;
}

void subEnd() {
    if (g_sub.empty()) return;
    SubFrame f = g_sub.back();
    g_sub.pop_back();
    float s = sc();
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    if (p.y - 6.f * s > f.y0 + 2.f * s)
        sketchLine(dl, ImVec2(f.x, f.y0 + 2.f * s), ImVec2(f.x, p.y - 6.f * s), theme::col(theme::live().accent, 0.6f), 2.f * s,
                   static_cast<uint32_t>(f.y0));
    ImGui::Unindent(24.f * s);
    ImGui::PopStyleVar();
}

}  // namespace ui
