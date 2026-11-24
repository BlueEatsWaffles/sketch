#include "Visuals.hpp"
#include "HitUnion.hpp"
#include "Config.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

using namespace geode::prelude;

namespace visuals {

namespace {

bool g_weDrew = false;
CCDrawNode* g_marks = nullptr;       // death / click markers, inside the level
std::vector<CCPoint> g_deathPts;
std::vector<CCPoint> g_clickPts;
bool g_marksDirty = true;
bool g_marksShown = false;
std::deque<CCRect> g_trail;
float g_hue = 0.f;

bool g_hidAttempts = false, g_hidPercent = false, g_hidProgress = false;
bool g_hidPlayer = false, g_hidWave = false, g_hidTrail = false, g_solidApplied = false;

ccColor4F c4(Col const& c, float alphaMul = 1.f) {
    return ccColor4F{c.r, c.g, c.b, c.a * alphaMul};
}

ccColor3B hsv(float h) {
    // hue -> rainbow color (saturation 0.85, value 1)
    float s = 0.85f, v = 1.f;
    h = std::fmod(h, 1.f) * 6.f;
    int i = static_cast<int>(h);
    float f = h - static_cast<float>(i);
    float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
    float r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return ccColor3B{static_cast<GLubyte>(r * 255.f), static_cast<GLubyte>(g * 255.f), static_cast<GLubyte>(b * 255.f)};
}

void rectOutline(CCDrawNode* node, CCRect const& r, float w, ccColor4F const& col) {
    CCPoint a(r.getMinX(), r.getMinY()), b(r.getMaxX(), r.getMinY());
    CCPoint c(r.getMaxX(), r.getMaxY()), d(r.getMinX(), r.getMaxY());
    node->drawSegment(a, b, w, col);
    node->drawSegment(b, c, w, col);
    node->drawSegment(c, d, w, col);
    node->drawSegment(d, a, w, col);
}

void applyHide(CCNode* n, bool hide, bool& was) {
    if (!n) return;
    if (hide) {
        n->setVisible(false);
        was = true;
    } else if (was) {
        n->setVisible(true);
        was = false;
    }
}

void hitboxes(PlayLayer* pl) {
    auto const& c = cfg();
    auto node = pl->m_debugDrawNode;
    if (!node) return;
    bool dead = pl->m_player1 && pl->m_player1->m_isDead;
    bool want = c.hitboxes || (c.hitboxOnDeath && dead);

    // remember the player's box for the trail
    if (c.hitboxTrail && pl->m_player1 && !dead) {
        g_trail.push_back(pl->m_player1->getObjectRect());
        if (pl->m_gameState.m_isDualMode && pl->m_player2) g_trail.push_back(pl->m_player2->getObjectRect());
        size_t maxLen = static_cast<size_t>(c.trailLen) * (pl->m_gameState.m_isDualMode ? 2u : 1u);
        while (g_trail.size() > maxLen) g_trail.pop_front();
    } else if (!c.hitboxTrail) {
        g_trail.clear();
    }

    if (want) {
        node->setVisible(true);
        if (c.hbOnlyPlayer) {
            node->clear();  // only the player's own box (drawn below)
        } else {
            bool prev = pl->m_isDebugDrawEnabled;
            pl->m_isDebugDrawEnabled = true;
            pl->updateDebugDraw();
            pl->m_isDebugDrawEnabled = prev;
        }

        float w = util::maxv(0.2f, c.borderWidth);
        if (c.hitboxTrail) {
            size_t n = g_trail.size();
            for (size_t i = 0; i < n; i++) {
                float a = static_cast<float>(i + 1) / static_cast<float>(n);
                rectOutline(node, g_trail[i], w * 0.5f, c4(c.hbPlayer, a * 0.6f));
            }
        }
        if (c.playerBox || c.hbOnlyPlayer) {
            if (pl->m_player1) rectOutline(node, pl->m_player1->getObjectRect(), w * 0.75f, c4(c.hbPlayer));
            if (pl->m_gameState.m_isDualMode && pl->m_player2)
                rectOutline(node, pl->m_player2->getObjectRect(), w * 0.75f, c4(c.hbPlayer));
        }
        g_weDrew = true;
    } else if (g_weDrew) {
        if (!pl->m_isDebugDrawEnabled) {
            node->clear();
            node->setVisible(false);
        }
        g_weDrew = false;
    }
}

void drawMarks(PlayLayer* pl) {
    auto const& c = cfg();
    bool want = c.deathMarkers || c.clickMarkers;
    if (!want) {
        if (g_marks && g_marksShown) {
            g_marks->clear();
            g_marksShown = false;
        }
        return;
    }
    if (!g_marks && pl->m_objectLayer) {
        g_marks = CCDrawNode::create();
        if (!g_marks) return;
        pl->m_objectLayer->addChild(g_marks, 9999);
        g_marksDirty = true;
    }
    if (!g_marks || (!g_marksDirty && g_marksShown)) return;
    g_marks->clear();
    if (c.deathMarkers) {
        ccColor4F red{1.f, 0.25f, 0.25f, 0.9f}, dark{0.f, 0.f, 0.f, 0.6f};
        for (auto const& p : g_deathPts) {
            float r = 7.f;
            g_marks->drawSegment(ccp(p.x - r, p.y - r), ccp(p.x + r, p.y + r), 2.6f, dark);
            g_marks->drawSegment(ccp(p.x - r, p.y + r), ccp(p.x + r, p.y - r), 2.6f, dark);
            g_marks->drawSegment(ccp(p.x - r, p.y - r), ccp(p.x + r, p.y + r), 1.6f, red);
            g_marks->drawSegment(ccp(p.x - r, p.y + r), ccp(p.x + r, p.y - r), 1.6f, red);
        }
    }
    if (c.clickMarkers) {
        ccColor4F dot{0.35f, 0.95f, 1.f, 0.85f}, ring{0.f, 0.f, 0.f, 0.6f};
        for (auto const& p : g_clickPts) g_marks->drawCircle(p, 3.2f, dot, 0.8f, ring, 10);
    }
    g_marksDirty = false;
    g_marksShown = true;
}

// ---------------------------------------------------------------------------
//  Hit borders: the player is shrunk to a dot (its center) and every obstacle
//  grows by the player's size. The dot crashes exactly when it touches a red
//  border, so tight gaps are easy to read. Plus a line of where the dot went.
// ---------------------------------------------------------------------------
CCDrawNode* g_borders = nullptr;     // the joined shapes
CCDrawNode* g_bordersTop = nullptr;  // path, clicks, the dot
uint64_t g_bordersHash = 0;
bool g_bordersShown = false;
std::deque<CCPoint> g_path[2];

enum BorderKind { BHazard = 0, BSolid = 1, BSpecial = 2 };

// getObjectRect() can change a couple of flags the game uses later; put them back.
struct RectGuard {
    GameObject* o;
    bool dirty, offset;
    explicit RectGuard(GameObject* obj) : o(obj), dirty(obj->m_isObjectRectDirty), offset(obj->m_boxOffsetCalculated) {}
    ~RectGuard() {
        o->m_isObjectRectDirty = dirty;
        o->m_boxOffsetCalculated = offset;
    }
};

CCRect rectOf(GameObject* o) {
    RectGuard g(o);
    return o->getObjectRect();
}

float crossZ(CCPoint const& o, CCPoint const& a, CCPoint const& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Convex hull (counter-clockwise), in place.
void hull(std::vector<CCPoint>& pts) {
    if (pts.size() < 3) return;
    std::sort(pts.begin(), pts.end(), [](CCPoint const& a, CCPoint const& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<CCPoint> h(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        while (k >= 2 && crossZ(h[k - 2], h[k - 1], pts[i]) <= 0.f) k--;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; i--) {
        while (k >= t && crossZ(h[k - 2], h[k - 1], pts[i - 1]) <= 0.f) k--;
        h[k++] = pts[i - 1];
    }
    h.resize(k > 1 ? k - 1 : k);
    pts.swap(h);
}

// polygon (convex) grown by a box of half size hw x hh
void growPolygon(CCPoint const* v, int n, float hw, float hh, std::vector<CCPoint>& out) {
    out.clear();
    for (int i = 0; i < n; i++) {
        out.push_back(ccp(v[i].x - hw, v[i].y - hh));
        out.push_back(ccp(v[i].x + hw, v[i].y - hh));
        out.push_back(ccp(v[i].x + hw, v[i].y + hh));
        out.push_back(ccp(v[i].x - hw, v[i].y + hh));
    }
    hull(out);
}

void growRect(CCRect const& r, float hw, float hh, std::vector<CCPoint>& out) {
    out.clear();
    out.push_back(ccp(r.getMinX() - hw, r.getMinY() - hh));
    out.push_back(ccp(r.getMaxX() + hw, r.getMinY() - hh));
    out.push_back(ccp(r.getMaxX() + hw, r.getMaxY() + hh));
    out.push_back(ccp(r.getMinX() - hw, r.getMaxY() + hh));
}

// circle (saws) grown by a box = a rounded rectangle
void growCircle(CCPoint const& c, float r, float hw, float hh, std::vector<CCPoint>& out) {
    out.clear();
    float const cx[4] = {c.x + hw, c.x - hw, c.x - hw, c.x + hw};
    float const cy[4] = {c.y + hh, c.y + hh, c.y - hh, c.y - hh};
    for (int q = 0; q < 4; q++) {
        for (int s = 0; s <= 8; s++) {
            float a = 1.5707963f * (static_cast<float>(q) + static_cast<float>(s) / 8.f);
            out.push_back(ccp(cx[q] + std::cos(a) * r, cy[q] + std::sin(a) * r));
        }
    }
}

bool isSpeedPortal(GameObject* o) {
    int id = o->m_objectID;
    return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
}

// The collision shape of an object, grown by the player box. Returns false = skip it.
bool borderOf(PlayLayer* pl, GameObject* o, float hw, float hh, std::vector<CCPoint>& out, int& kind) {
    auto const& c = cfg();
    switch (o->m_objectType) {
        case GameObjectType::Decoration:
        case GameObjectType::CollisionObject:
        case GameObjectType::SecretCoin:
        case GameObjectType::UserCoin:
        case GameObjectType::Collectible:
            return false;
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard: {
            if (o == pl->m_anticheatSpike) return false;
            kind = BHazard;
            float radius = o->getObjectRadius();
            if (radius > 0.f) {
                growCircle(o->getPosition(), radius, hw, hh, out);
            } else if (o->m_orientedBox) {
                growPolygon(o->m_orientedBox->m_corners.data(), 4, hw, hh, out);
            } else {
                growRect(rectOf(o), hw, hh, out);
            }
            return true;
        }
        case GameObjectType::Solid: {
            if (!c.hbSolids) return false;
            kind = BSolid;
            if (o->m_orientedBox) growPolygon(o->m_orientedBox->m_corners.data(), 4, hw, hh, out);
            else growRect(rectOf(o), hw, hh, out);
            return true;
        }
        case GameObjectType::Slope: {
            if (!c.hbSolids) return false;
            kind = BSolid;
            CCRect r = rectOf(o);
            CCPoint tri[3] = {ccp(r.getMinX(), r.getMinY()), ccp(r.getMinX(), r.getMaxY()), ccp(r.getMaxX(), r.getMinY())};
            CCPoint topRight = ccp(r.getMaxX(), r.getMaxY());
            static int const swaps[8][3] = {{-1, 1, -1}, {0, -1, -1}, {-1, -1, -1}, {-1, -1, 2},
                                            {-1, -1, -1}, {0, -1, -1}, {-1, -1, 2}, {-1, 1, -1}};
            int dir = o->m_slopeDirection;
            if (dir >= 0 && dir < 8)
                for (int i = 0; i < 3; i++)
                    if (swaps[dir][i] >= 0) tri[swaps[dir][i]] = topRight;
            growPolygon(tri, 3, hw, hh, out);
            return true;
        }
        case GameObjectType::Modifier:
            if (!isSpeedPortal(o)) return false;
            [[fallthrough]];
        default: {
            if (!c.hbSpecials) return false;
            kind = BSpecial;
            if (o->m_orientedBox) growPolygon(o->m_orientedBox->m_corners.data(), 4, hw, hh, out);
            else growRect(rectOf(o), hw, hh, out);
            return true;
        }
    }
}

ccColor4F borderColor(int kind, float alpha) {
    auto const& c = cfg();
    if (c.hbRainbow) {
        static float const off[3] = {0.f, 0.6f, 0.33f};
        ccColor3B h = hsv(std::fmod(static_cast<float>(util::now()) * 0.25f + off[kind], 1.f));
        return ccColor4F{h.r / 255.f, h.g / 255.f, h.b / 255.f, alpha};
    }
    Col const& k = kind == BHazard ? c.hbHazard : (kind == BSolid ? c.hbSolid : c.hbSpecial);
    return ccColor4F{k.r, k.g, k.b, k.a * alpha};
}

// FNV-1a: a quick fingerprint, so the joined shapes are only rebuilt when
// something on screen actually changed (most frames nothing does).
struct Hash {
    uint64_t h = 1469598103934665603ull;
    void add(void const* p, size_t n) {
        auto const* b = static_cast<unsigned char const*>(p);
        for (size_t i = 0; i < n; i++) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void add(float f) { add(&f, sizeof(f)); }
};

void clearBorders() {
    if (g_borders) g_borders->clear();
    if (g_bordersTop) g_bordersTop->clear();
    g_bordersHash = 0;
}

void hitBorders(PlayLayer* pl) {
    auto const& c = cfg();
    if (!c.hitBorders) {
        if (g_bordersShown) {
            clearBorders();
            g_bordersShown = false;
        }
        return;
    }
    if ((!g_borders || !g_bordersTop) && pl->m_objectLayer) {
        // two layers: the shapes (rebuilt only when they change) and, on top,
        // the path, clicks and the dot (they move every frame)
        if (!g_borders) {
            g_borders = CCDrawNode::create();
            if (!g_borders) return;
            g_borders->setBlendFunc(ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            pl->m_objectLayer->addChild(g_borders, 9998);
        }
        if (!g_bordersTop) {
            g_bordersTop = CCDrawNode::create();
            if (!g_bordersTop) return;
            g_bordersTop->setBlendFunc(ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            pl->m_objectLayer->addChild(g_bordersTop, 9998);
        }
        g_bordersHash = 0;
    }
    if (!g_borders || !g_bordersTop || !pl->m_player1) return;
    g_bordersShown = true;

    // the player's box decides how much everything grows
    CCRect pr = pl->m_player1->getObjectRect();
    float hw = (pr.getMaxX() - pr.getMinX()) * 0.5f;
    float hh = (pr.getMaxY() - pr.getMinY()) * 0.5f;
    float w = util::maxv(0.35f, c.borderWidth * 1.3f);
    float fillA = util::clamp(c.hbBorderFill, 0.f, 1.f);
    ccColor4F clear{0.f, 0.f, 0.f, 0.f};
    ccColor4F shadow{0.f, 0.f, 0.f, 0.35f};

    // everything in the visible part of the level, sorted into the 3 kinds
    static std::vector<std::vector<hitunion::Pt>> groups[3];
    static size_t used[3];
    for (int k = 0; k < 3; k++) used[k] = 0;
    Hash fp;
    std::vector<CCPoint> shape;
    shape.reserve(40);
    int cols = static_cast<int>(pl->m_sections.size());
    int c0 = util::minv(pl->m_leftSectionIndex, pl->m_rightSectionIndex);
    int c1 = util::maxv(pl->m_leftSectionIndex, pl->m_rightSectionIndex);
    int r0 = util::minv(pl->m_bottomSectionIndex, pl->m_topSectionIndex);
    int r1 = util::maxv(pl->m_bottomSectionIndex, pl->m_topSectionIndex);
    for (int ci = util::maxv(0, c0); ci <= c1 && ci < cols; ci++) {
        auto* column = pl->m_sections[static_cast<size_t>(ci)];
        if (!column || static_cast<size_t>(ci) >= pl->m_sectionSizes.size() || !pl->m_sectionSizes[static_cast<size_t>(ci)]) continue;
        int rows = static_cast<int>(column->size());
        for (int ri = util::maxv(0, r0); ri <= r1 && ri < rows; ri++) {
            auto* cell = column->at(static_cast<size_t>(ri));
            if (!cell) continue;
            int count = pl->m_sectionSizes[static_cast<size_t>(ci)]->at(static_cast<size_t>(ri));
            for (int oi = 0; oi < count && oi < static_cast<int>(cell->size()); oi++) {
                GameObject* o = cell->at(static_cast<size_t>(oi));
                if (!o || !o->m_isActivated || o->m_isGroupDisabled) continue;
                if (o == pl->m_player1 || o == pl->m_player2) continue;
                int kind = BHazard;
                if (!borderOf(pl, o, hw, hh, shape, kind) || shape.size() < 3) continue;
                if (kind < 0 || kind > 2) continue;
                auto& g = groups[kind];
                if (used[kind] >= g.size()) g.emplace_back();
                auto& poly = g[used[kind]++];
                poly.clear();
                for (auto const& p : shape) poly.push_back({p.x, p.y});
                fp.add(&kind, sizeof(kind));
                fp.add(poly.data(), poly.size() * sizeof(hitunion::Pt));
            }
        }
    }

    // colors and sizes count too (rainbow changes every frame)
    for (int k = 0; k < 3; k++) {
        ccColor4F col = borderColor(k, 1.f);
        fp.add(&col, sizeof(col));
    }
    fp.add(w);
    fp.add(fillA);
    uint64_t hash = fp.h ? fp.h : 1;

    if (hash != g_bordersHash) {
        g_bordersHash = hash;
        g_borders->clear();
        // draw each kind as one joined shape: one even fill, one outline around
        // the outside. Blocks first, red last so danger is always on top.
        static std::vector<std::vector<hitunion::Pt>> polys;
        static hitunion::Fill fill;
        static std::vector<hitunion::Seg> edge;
        std::vector<CCPoint> pts;
        ccColor4F invisible{0.f, 0.f, 0.f, 0.001f};  // turns off the soft inner edge, draws nothing
        int const order[3] = {BSolid, BSpecial, BHazard};
        for (int kind : order) {
            size_t n = used[kind];
            if (!n) continue;
            auto const& g = groups[kind];
            ccColor4F fillCol = borderColor(kind, fillA);
            ccColor4F line = borderColor(kind, 1.f);
            line.a = 1.f;
            ccColor4F under{line.r * 0.3f, line.g * 0.3f, line.b * 0.3f, 1.f};
            if (n > 1500) {
                // way too many to join quickly: draw them one by one
                for (size_t i = 0; i < n; i++) {
                    pts.clear();
                    for (auto const& p : g[i]) pts.push_back(ccp(p.x, p.y));
                    auto cnt = static_cast<unsigned int>(pts.size());
                    g_borders->drawPolygon(pts.data(), cnt, clear, w * 2.6f, shadow);
                    g_borders->drawPolygon(pts.data(), cnt, fillCol, w, line);
                }
                continue;
            }
            polys.assign(g.begin(), g.begin() + static_cast<std::ptrdiff_t>(n));
            hitunion::merge(polys, fill, edge);
            if (fillA > 0.f) {
                size_t off = 0;
                for (unsigned cnt : fill.counts) {
                    pts.clear();
                    for (unsigned i = 0; i < cnt; i++) pts.push_back(ccp(fill.pts[off + i].x, fill.pts[off + i].y));
                    off += cnt;
                    g_borders->drawPolygon(pts.data(), cnt, fillCol, 0.001f, invisible);
                }
            }
            // solid colors so the round ends don't make dots where pieces meet
            for (auto const& sg : edge) g_borders->drawSegment(ccp(sg.a.x, sg.a.y), ccp(sg.b.x, sg.b.y), w * 2.2f, under);
            for (auto const& sg : edge) g_borders->drawSegment(ccp(sg.a.x, sg.a.y), ccp(sg.b.x, sg.b.y), w, line);
        }
    }

    // the dot's path (with a dark outline), where you clicked, and the dot itself
    g_bordersTop->clear();
    ccColor4F yellow = c4(c.hbPlayer);
    yellow.a = 1.f;
    ccColor4F outline{0.08f, 0.07f, 0.05f, 1.f};
    ccColor4F dark{0.f, 0.f, 0.f, 0.5f};
    ccColor4F white{1.f, 1.f, 1.f, 0.95f};
    bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
    if (c.hbPath) {
        for (int pi = 0; pi < (dual ? 2 : 1); pi++) {
            auto const& path = g_path[pi];
            size_t n = path.size();
            for (size_t i = 1; i < n; i++) {
                // skip jumps (teleports, respawns)
                if (std::fabs(path[i].x - path[i - 1].x) > 60.f || std::fabs(path[i].y - path[i - 1].y) > 60.f) continue;
                // solid colors: see-through pieces would show little beads where they overlap
                g_bordersTop->drawSegment(path[i - 1], path[i], w * 2.4f, outline);
            }
            for (size_t i = 1; i < n; i++) {
                if (std::fabs(path[i].x - path[i - 1].x) > 60.f || std::fabs(path[i].y - path[i - 1].y) > 60.f) continue;
                g_bordersTop->drawSegment(path[i - 1], path[i], w * 1.3f, yellow);
            }
        }
    }
    if (c.hbClicks) {
        for (auto const& p : g_clickPts) {
            g_bordersTop->drawCircle(p, 5.f, clear, w * 2.4f, dark, 16);
            g_bordersTop->drawCircle(p, 5.f, clear, w * 1.2f, yellow, 16);
            g_bordersTop->drawDot(p, 1.4f, yellow);
        }
    }
    PlayerObject* dots[2] = {pl->m_player1, dual ? pl->m_player2 : nullptr};
    for (auto* p : dots) {
        if (!p) continue;
        CCPoint at = p->getPosition();
        g_bordersTop->drawDot(at, 4.6f, dark);
        g_bordersTop->drawDot(at, 3.6f, yellow);
        g_bordersTop->drawDot(at, 1.5f, white);
    }
}

}  // namespace

void addDeathMarker(PlayLayer*, PlayerObject* p) {
    if (!p) return;
    CCPoint pos = p->getPosition();
    // skip markers right next to the last one (noclip hits come every tick)
    if (!g_deathPts.empty()) {
        CCPoint const& l = g_deathPts.back();
        if (std::fabs(l.x - pos.x) < 20.f && std::fabs(l.y - pos.y) < 20.f) return;
    }
    if (g_deathPts.size() > 3000) g_deathPts.erase(g_deathPts.begin());
    g_deathPts.push_back(pos);
    g_marksDirty = true;
}

void addClickMarker(PlayLayer* pl, bool p1) {
    auto const& c = cfg();
    if (!(c.clickMarkers || (c.hitBorders && c.hbClicks)) || !pl) return;
    PlayerObject* p = p1 ? pl->m_player1 : pl->m_player2;
    if (!p) return;
    if (g_clickPts.size() > 2000) g_clickPts.erase(g_clickPts.begin());
    g_clickPts.push_back(p->getPosition());
    g_marksDirty = true;
}

void clearDeathMarkers() {
    g_deathPts.clear();
    g_clickPts.clear();
    g_marksDirty = true;
}

void onLevelEnter() {
    g_marks = nullptr;  // the old level (and its marker node) is gone
    g_borders = nullptr;
    g_bordersTop = nullptr;
    g_bordersHash = 0;
    g_bordersShown = false;
    g_path[0].clear();
    g_path[1].clear();
    g_deathPts.clear();
    g_clickPts.clear();
    g_marksDirty = true;
    g_marksShown = false;
    g_weDrew = false;
    g_trail.clear();
    g_hidAttempts = g_hidPercent = g_hidProgress = false;
    g_hidPlayer = g_hidWave = g_hidTrail = g_solidApplied = false;
}

void onReset(PlayLayer*) {
    g_trail.clear();
    g_path[0].clear();
    g_path[1].clear();
    g_clickPts.clear();  // click markers show the current attempt only
    g_marksDirty = true;
}

bool isDebugNode(CCDrawNode* node) {
    auto pl = GameManager::sharedState()->m_playLayer;
    return pl && node && pl->m_debugDrawNode == node;
}

ccColor4F recolor(ccColor4F const& o) {
    auto const& c = cfg();
    if (c.hbRainbow) {
        // every kind of object gets its own spot on a moving rainbow
        float off = (o.r > 0.5f && o.g < 0.5f && o.b < 0.5f) ? 0.f : (o.b > 0.5f && o.r < 0.5f) ? 0.33f : (o.g > 0.5f) ? 0.66f : 0.15f;
        ccColor3B h = hsv(std::fmod(static_cast<float>(util::now()) * 0.25f + off, 1.f));
        return ccColor4F{h.r / 255.f, h.g / 255.f, h.b / 255.f, 1.f};
    }
    Col const* pick = &c.hbOther;
    if (o.r > 0.5f && o.g < 0.5f && o.b < 0.5f) pick = &c.hbHazard;        // red  = things that kill you
    else if (o.b > 0.5f && o.r < 0.5f) pick = &c.hbSolid;                   // blue = blocks / ground
    else if (o.g > 0.5f && o.r < 0.5f) pick = &c.hbSpecial;                 // green = orbs, pads, portals
    return ccColor4F{pick->r, pick->g, pick->b, pick->a};
}

void postUpdate(PlayLayer* pl, float dt) {
    auto const& c = cfg();
    hitboxes(pl);
    drawMarks(pl);
    hitBorders(pl);

    // Player 2 only matters in dual mode (otherwise it is hidden by the game).
    bool dual = pl->m_gameState.m_isDualMode;
    PlayerObject* players[2] = {pl->m_player1, dual ? pl->m_player2 : nullptr};

    if (c.rainbow) {
        g_hue = std::fmod(g_hue + dt * c.rainbowSpeed, 1.f);
        for (int i = 0; i < 2; i++) {
            auto p = players[i];
            if (!p) continue;
            float h = std::fmod(g_hue + (i ? 0.5f : 0.f), 1.f);
            p->setColor(hsv(h));
            p->setSecondColor(hsv(std::fmod(h + 0.5f, 1.f)));
            if (c.rainbowTrail && p->m_regularTrail) p->m_regularTrail->setColor(hsv(h));
        }
    }

    bool hidW = g_hidWave, hidT = g_hidTrail, hidP = g_hidPlayer;
    for (auto p : players) {
        if (!p) continue;
        if (p->m_waveTrail) {
            bool w = hidW;
            applyHide(p->m_waveTrail, c.noWaveTrail, w);
            g_hidWave = w;
            if (c.solidWave) {
                p->m_waveTrail->setBlendFunc(ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
                g_solidApplied = true;
            } else if (g_solidApplied) {
                p->m_waveTrail->setBlendFunc(ccBlendFunc{GL_SRC_ALPHA, GL_ONE});
            }
        }
        if (p->m_regularTrail) {
            bool t = hidT;
            applyHide(p->m_regularTrail, c.noTrail, t);
            g_hidTrail = t;
        }
        if (!p->m_isDead) {
            bool h = hidP;
            applyHide(p, c.hidePlayer, h);
            g_hidPlayer = h;
        }
    }
    if (!c.solidWave) g_solidApplied = false;

    applyHide(pl->m_attemptLabel, c.hideAttempts, g_hidAttempts);
    applyHide(pl->m_percentageLabel, c.hidePercent, g_hidPercent);
    applyHide(pl->m_progressBar, c.hideProgress, g_hidProgress);
}

void afterTick(GJBaseGameLayer* g) {
    auto const& c = cfg();
    if (!c.hitBorders || !c.hbPath || !g || !g->m_player1) return;
    size_t cap = static_cast<size_t>(util::clamp(c.hbPathLen, 30, 4000));
    bool dual = g->m_gameState.m_isDualMode && g->m_player2;
    for (int i = 0; i < (dual ? 2 : 1); i++) {
        PlayerObject* p = i ? g->m_player2 : g->m_player1;
        if (p->m_isDead) continue;
        auto& path = g_path[i];
        CCPoint pos = p->getPosition();
        if (!path.empty() && std::fabs(path.back().x - pos.x) < 0.01f && std::fabs(path.back().y - pos.y) < 0.01f) continue;
        path.push_back(pos);
        while (path.size() > cap) path.pop_front();
    }
}

void percent(PlayLayer* pl) {
    auto const& c = cfg();
    if (!c.accuratePercent || pl->m_isPlatformer || !pl->m_percentageLabel) return;
    float pct = util::clamp(pl->getCurrentPercent(), 0.f, 100.f);
    auto s = fmt::format("{:.{}f}%", pct, util::clamp(c.percentDecimals, 0, 6));
    pl->m_percentageLabel->setString(s.c_str());
}

}  // namespace visuals
