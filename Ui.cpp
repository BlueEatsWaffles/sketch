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

std::unordered_map<ImGuiID, float> g_anims;
float g_dt = 1.f / 60.f;
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

}  // namespace

// ---------------------------------------------------------------------------

float sc() { return cfg().uiScale; }
float rowH() { return ImGui::GetFontSize() + 14.f * sc(); }
float dt() { return g_dt; }
void beginFrame(float d) { g_dt = util::clamp(d, 0.f, 0.1f); }

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
    if (!cfg().sketchy) {
        dl->AddRect(a, b, col, rounding, 0, th);
        return;
    }
    uint32_t s = hash32(seed ^ (boilSeed() * 0x9E3779B9u)) | 1u;
    float amp = util::clamp(th * 0.35f, 0.6f, 2.2f);
    float w = b.x - a.x, h = b.y - a.y;
    float cut = util::minv(rounding * 0.6f, util::minv(w * 0.25f, h * 0.25f));
    ImVec2 corners[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
    ImVec2 pts[96];
    int n = 0;
    for (int e = 0; e < 4; e++) {
        ImVec2 p0 = corners[e], p1 = corners[(e + 1) % 4];
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) continue;
        float ux = dx / len, uy = dy / len;
        float nx = -uy, ny = ux;
        int segs = util::clamp(static_cast<int>(len / (22.f * sc())), 1, 20);
        for (int i = 0; i < segs && n < 90; i++) {
            float t = static_cast<float>(i) / static_cast<float>(segs);
            float d = cut + t * (len - 2.f * cut);
            float j = (i == 0) ? 0.f : rnd(s) * amp;
            pts[n++] = ImVec2(p0.x + ux * d + nx * j, p0.y + uy * d + ny * j);
        }
    }
    if (n >= 3) dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, th);
}

void squiggle(ImDrawList* dl, ImVec2 p, float w, ImU32 col, float th, float amp, float phase) {
    int n = util::clamp(static_cast<int>(w / (3.f * sc())), 4, 120);
    ImVec2 pts[121];
    float freq = 6.2831853f / (34.f * sc());
    for (int i = 0; i <= n; i++) {
        float x = w * static_cast<float>(i) / static_cast<float>(n);
        pts[i] = ImVec2(p.x + x, p.y + std::sin(x * freq + phase) * amp);
    }
    dl->AddPolyline(pts, n + 1, col, 0, th);
}

void checkMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float th) {
    ImVec2 pts[3] = {
        ImVec2(c.x - size * 0.5f, c.y + size * 0.02f),
        ImVec2(c.x - size * 0.12f, c.y + size * 0.38f),
        ImVec2(c.x + size * 0.5f, c.y - size * 0.38f),
    };
    dl->AddPolyline(pts, 3, col, 0, th);
}

void crossMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float th) {
    float h = size * 0.4f;
    dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, th);
    dl->AddLine(ImVec2(c.x + h, c.y - h), ImVec2(c.x - h, c.y + h), col, th);
}

// ---------------------------------------------------------------------------
//  Widgets
// ---------------------------------------------------------------------------

void spacing(float h) { ImGui::Dummy(ImVec2(1.f, h * sc())); }

void section(char const* title) {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGui::Dummy(ImVec2(1.f, 6.f * s));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fs = ImGui::GetFontSize() * 1.2f;
    ImVec2 ts = textSize(title, nullptr, fs);
    text(dl, p, theme::col(t.text), title, nullptr, fs);
    float phase = cfg().lineBoil ? static_cast<float>(ImGui::GetTime()) * 3.f
                                 : static_cast<float>(hash32(ImGui::GetID(title)) % 100) * 0.1f;
    squiggle(dl, ImVec2(p.x, p.y + fs + 4.f * s), ts.x + 18.f * s, theme::col(t.accent, 0.9f), 2.2f * s, 2.2f * s, phase);
    ImGui::Dummy(ImVec2(util::maxv(ts.x, 1.f), fs + 12.f * s));
}

bool toggle(char const* label, bool* v, char const* tip) {
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
    tooltipFor(tip);

    float ha = anim(id ^ 0x1111u, hov ? 1.f : 0.f, 14.f);
    float on = anim(id, *v ? 1.f : 0.f, 16.f);
    float r = radius();

    if (ha > 0.01f) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(t.field, 0.6f * ha), r);

    float fs = ImGui::GetFontSize();
    float bs = fs + 4.f * s;
    ImVec2 b0(p.x + w - bs - 8.f * s, p.y + (h - bs) * 0.5f);
    ImVec2 b1(b0.x + bs, b0.y + bs);

    dl->PushClipRect(p, ImVec2(b0.x - 6.f * s, p.y + h), true);
    text(dl, ImVec2(p.x + 10.f * s + ha * 4.f * s, p.y + (h - fs) * 0.5f), theme::col(t.text), label, labelEnd(label));
    dl->PopClipRect();

    dl->AddRectFilled(b0, b1, theme::col(t.field), r * 0.6f);
    if (on > 0.01f) {
        float e = util::easeOutBack(on);
        float inset = (1.f - e) * bs * 0.5f + 3.f * s;
        inset = util::clamp(inset, 0.f, bs * 0.5f);
        dl->AddRectFilled(ImVec2(b0.x + inset, b0.y + inset), ImVec2(b1.x - inset, b1.y - inset),
                          theme::col(t.accent, util::clamp(on, 0.f, 1.f)), r * 0.4f);
    }
    sketchRect(dl, b0, b1, theme::col(t.outline), thick() * 0.85f, r * 0.6f, id);
    if (on > 0.5f) {
        ImVec2 c((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
        checkMark(dl, c, bs * 0.5f, theme::col(t.accentText, (on - 0.5f) * 2.f), 2.4f * s);
    }
    return pressed;
}

static bool sliderCore(char const* label, float* v, float mn, float mx, char const* fmt, bool isInt, char const* tip) {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float trackW = util::maxv(w * 0.46f, 80.f * s);
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
        float af = anim(id, frac, 18.f);
        float ha = anim(id ^ 0x5151u, (hovered || active) ? 1.f : 0.f, 14.f);
        float r = radius() * 0.7f;
        dl->AddRectFilled(t0, t1, theme::col(t.field), r);
        float fx = t0.x + (t1.x - t0.x) * af;
        if (fx > t0.x + 1.f) dl->AddRectFilled(t0, ImVec2(fx, t1.y), theme::col(t.accent, 0.75f + 0.25f * ha), r);
        dl->AddLine(ImVec2(fx, t0.y + 2.f * s), ImVec2(fx, t1.y - 2.f * s), theme::col(t.outline), 2.5f * s);
        sketchRect(dl, t0, t1, theme::col(t.outline), thick() * 0.8f, r, id);

        char buf[64];
        if (isInt) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(*v)));
        else std::snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 vs = textSize(buf);
        ImVec2 tp(t0.x + ((t1.x - t0.x) - vs.x) * 0.5f, t0.y + ((t1.y - t0.y) - vs.y) * 0.5f);
        // two-tone value text: dark on the filled part, light on the empty part
        dl->PushClipRect(t0, ImVec2(fx, t1.y), true);
        text(dl, tp, theme::col(t.accentText), buf);
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(fx, t0.y), t1, true);
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

    float ha = anim(id ^ 0xB0B0u, hov ? 1.f : 0.f, 14.f);
    float pa = anim(id ^ 0xB1B1u, act ? 1.f : 0.f, 30.f);
    float hl = anim(id ^ 0xB2B2u, highlight ? 1.f : 0.f, 10.f);
    float r = radius() * 0.8f;
    float lift = ha * 1.5f * s * (1.f - pa);
    float push = pa * 1.5f * s;
    float sh = (3.f - 2.f * pa) * s;

    ImVec2 a(p.x + 1.f * s, p.y + 1.f * s - lift + push);
    ImVec2 b(p.x + size.x - 3.f * s, p.y + size.y - 3.f * s - lift + push);
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
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = util::maxv(120.f * s, w * 0.3f);
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
    float ca = anim(id, capturing ? 1.f : 0.f, 12.f);
    float ha = anim(id ^ 0x4B4Bu, hov ? 1.f : 0.f, 14.f);
    float pulse = capturing ? (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.f)) : 0.f;
    float r = radius() * 0.7f;
    Col fill = theme::mix(t.field, t.accent, util::clamp(0.14f * ha + ca * (0.35f + 0.35f * pulse), 0.f, 1.f));
    dl->AddRectFilled(b0, b1, theme::col(fill), r);
    sketchRect(dl, b0, b1, theme::col(t.outline), thick() * 0.8f, r, id);
    std::string txt = capturing ? std::string("press a key...") : util::keyName(*key);
    ImVec2 ts = textSize(txt.c_str());
    Col tc = theme::mix(t.text, t.accentText, ca * 0.8f);
    text(dl, ImVec2((b0.x + b1.x - ts.x) * 0.5f, (b0.y + b1.y - ts.y) * 0.5f), theme::col(tc), txt.c_str());

    labelText(dl, p, h, b0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

bool combo(char const* label, int* idx, char const* const* items, int count, char const* tip) {
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = util::maxv(w * 0.46f, 90.f * s);
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
        animSet(animId, 0.f);
    }
    bool open = ImGui::IsPopupOpen("##pop");
    ImGui::SetNextWindowPos(ImVec2(b0.x, b1.y + 4.f * s));
    ImGui::SetNextWindowSizeConstraints(ImVec2(b1.x - b0.x, 0.f), ImVec2(b1.x - b0.x + 80.f * s, 320.f * s));
    if (ImGui::BeginPopup("##pop")) {
        float a = anim(animId, 1.f, 16.f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
        ImGui::Dummy(ImVec2(1.f, (1.f - a) * 8.f * s));  // tiny slide-down
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

    float ha = anim(id ^ 0xC0C0u, (hov || open) ? 1.f : 0.f, 14.f);
    float oa = anim(id ^ 0xC1C1u, open ? 1.f : 0.f, 14.f);
    float r = radius() * 0.7f;
    dl->AddRectFilled(b0, b1, theme::col(theme::mix(t.field, t.accent, 0.14f * ha)), r);
    sketchRect(dl, b0, b1, theme::col(t.outline), thick() * 0.8f, r, id);
    float fs = ImGui::GetFontSize();
    dl->PushClipRect(b0, ImVec2(b1.x - 22.f * s, b1.y), true);
    text(dl, ImVec2(b0.x + 8.f * s, (b0.y + b1.y - fs) * 0.5f), theme::col(t.text), items[*idx]);
    dl->PopClipRect();
    // chevron that flips when open
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

    float pos = anim(id, static_cast<float>(*v), 14.f);
    float r = radius() * 0.8f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(t.field), r);
    ImVec2 h0(p.x + segW * pos + 3.f * s, p.y + 3.f * s);
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
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float bw = 64.f * s;
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

    float ha = anim(id, hov ? 1.f : 0.f, 14.f);
    float lift = ha * 2.f * s;
    ImVec2 a(b0.x, b0.y - lift), b(b1.x, b1.y - lift);
    float r = radius() * 0.6f;
    // checker so you can see transparency
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
    auto* dl = ImGui::GetWindowDrawList();
    auto const& t = theme::live();
    float s = sc();
    ImGuiID id = ImGui::GetID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = util::maxv(ImGui::GetContentRegionAvail().x, 60.f);
    float h = rowH();
    float iw = w * 0.6f;
    float frameH = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f;
    ImVec2 i0(p.x + w - iw - 8.f * s, p.y + (h - frameH) * 0.5f);

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(i0);
    ImGui::SetNextItemWidth(iw);
    bool changed = ImGui::InputTextWithHint("##in", hint ? hint : "", buf, size);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    bool active = ImGui::IsItemActive();
    ImGui::PopID();

    float aa = anim(id, active ? 1.f : 0.f, 14.f);
    sketchRect(dl, a, b, theme::col(theme::mix(t.outline, t.accent, aa * 0.7f)), thick() * 0.75f, radius() * 0.7f, id);
    labelText(dl, p, h, i0.x - 6.f * s, label);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

void note(char const* txt) {
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
    float a = anim(ImGui::GetID(id), open ? 1.f : 0.f, 14.f);
    if (a < 0.02f) return false;
    float s = sc();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
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
        dl->AddLine(ImVec2(f.x, f.y0 + 2.f * s), ImVec2(f.x, p.y - 6.f * s), theme::col(theme::live().accent, 0.6f), 2.f * s);
    ImGui::Unindent(24.f * s);
    ImGui::PopStyleVar();
}

}  // namespace ui
