#include "Visuals.hpp"
#include "Config.hpp"
#include "Util.hpp"

#include <cmath>
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
    if (!cfg().clickMarkers || !pl) return;
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

void percent(PlayLayer* pl) {
    auto const& c = cfg();
    if (!c.accuratePercent || pl->m_isPlatformer || !pl->m_percentageLabel) return;
    float pct = util::clamp(pl->getCurrentPercent(), 0.f, 100.f);
    auto s = fmt::format("{:.{}f}%", pct, util::clamp(c.percentDecimals, 0, 6));
    pl->m_percentageLabel->setString(s.c_str());
}

}  // namespace visuals
