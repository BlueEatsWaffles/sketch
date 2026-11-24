// Hooks into the engine: time (speedhack), keyboard, screen fades, drawing.
#include "Bot.hpp"
#include "Config.hpp"
#include "Gui.hpp"
#include "Hacks.hpp"
#include "Overlay.hpp"
#include "Predict.hpp"
#include "Render.hpp"
#include "Ui.hpp"
#include "Util.hpp"
#include "Visuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDrawNode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTransitionFade.hpp>
#include <Geode/modify/GameManager.hpp>

#include <imgui-cocos.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
//  Time: speedhack, lock delta, video maker, per-frame jobs
// ---------------------------------------------------------------------------
class $modify(SKScheduler, CCScheduler) {
    void update(float dt) {
        auto const& c = cfg();
        float realDt = dt;
        bool inLevel = hacks::playLayer() != nullptr;

        if (render::locksTime()) {
            // FFmpeg (or built-in "not real-time"): every frame moves the game by
            // exactly one video frame, so the video is perfectly smooth.
            dt = 1.f / static_cast<float>(render::fps());
        } else {
            if (c.lockDelta && inLevel) dt = static_cast<float>(CCDirector::sharedDirector()->getAnimationInterval());
            dt *= hacks::speedFactor(inLevel);
        }

        hacks::engineTick(realDt);
        CCScheduler::update(dt);
        if (render::active()) render::capture(realDt);
    }
};

// ---------------------------------------------------------------------------
//  Keyboard: menu key + hotkeys
// ---------------------------------------------------------------------------
namespace {

bool handleKey(int key, bool down, bool repeat) {
    auto& c = cfg();

    // a keybind box is waiting for a key
    if (ui::g_capturingKey) {
        if (down && !repeat) {
            if (key == 0x1B) {
                // Esc = cancel
            } else if (key == 0x08 || key == 0x2E) {
                *ui::g_capturingKey = -1;  // Backspace / Delete = no key
            } else {
                *ui::g_capturingKey = key;
            }
            ui::g_capturingKey = nullptr;
            saveConfig();
        }
        return true;
    }

    // typing in a text box: let ImGui have the keys
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) return false;
    if (key == c.slowmoKey && !repeat) hacks::setSlowHeld(down);  // hold to slow down
    if (!down) return false;

    if (key == c.menuKey && !repeat) {
        gui::toggle();
        return true;
    }

    if (key == c.screenshotKey && !repeat) {
        std::string path;
        if (render::screenshot(path)) overlay::toast("Screenshot saved", overlay::Good);
        else overlay::toast("Couldn't take a screenshot", overlay::Bad);
        return true;
    }

    auto pl = hacks::playLayer();
    if (!pl) return false;
    if (predictor::active()) return false;  // the Predict bot is busy - no hotkeys

    if (key == c.hitBordersKey && !repeat) {
        c.hitBorders = !c.hitBorders;
        overlay::toast(c.hitBorders ? "Hit borders ON" : "Hit borders OFF");
        return true;
    }

    if (c.frameStepper && key == c.stepKey) {
        hacks::requestStep();  // holding the key keeps stepping (key repeat)
        return true;
    }
    if (repeat) return false;

    if (key == c.stepToggleKey) {
        c.frameStepper = !c.frameStepper;
        overlay::toast(c.frameStepper ? "Frame stepper ON" : "Frame stepper OFF");
        return true;
    }
    if (key == c.noclipKey) {
        c.noclip = !c.noclip;
        overlay::toast(c.noclip ? "Noclip ON" : "Noclip OFF");
        return true;
    }
    if (key == c.speedKey) {
        c.speedhack = !c.speedhack;
        overlay::toast(c.speedhack ? "Speedhack ON" : "Speedhack OFF");
        return true;
    }
    if (key == c.restartKey) {
        pl->resetLevel();
        return true;
    }
    if (key == c.practiceKey) {
        pl->togglePracticeMode(!pl->m_isPracticeMode);
        return true;
    }
    return false;
}

}  // namespace

class $modify(SKKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timestamp) {
        if (handleKey(static_cast<int>(key), isKeyDown, isKeyRepeat)) return true;
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timestamp);
    }
};

// ---------------------------------------------------------------------------
//  No screen fade
// ---------------------------------------------------------------------------
class $modify(SKFade, CCTransitionFade) {
    static CCTransitionFade* create(float duration, CCScene* scene) {
        if (cfg().noTransition) duration = 0.f;
        return CCTransitionFade::create(duration, scene);
    }
};

// ---------------------------------------------------------------------------
//  Hit borders: recolor the game's hitbox drawing
// ---------------------------------------------------------------------------
class $modify(SKDrawNode, CCDrawNode) {
    bool drawPolygon(CCPoint* verts, unsigned int count, ccColor4F const& fill, float width,
                     ccColor4F const& border, BorderAlignment alignment) {
        if (cfg().hitBorders && visuals::isDebugNode(this)) {
            auto const& c = cfg();
            ccColor4F col = visuals::recolor(border);
            ccColor4F f = col;
            f.a = c.hbFill ? c.hbFillAlpha * col.a : 0.f;
            return CCDrawNode::drawPolygon(verts, count, f, c.borderWidth, col, alignment);
        }
        return CCDrawNode::drawPolygon(verts, count, fill, width, border, alignment);
    }

    bool drawCircle(CCPoint const& pos, float radius, ccColor4F const& fill, float width, ccColor4F const& border,
                    unsigned int segments) {
        if (cfg().hitBorders && visuals::isDebugNode(this)) {
            auto const& c = cfg();
            ccColor4F col = visuals::recolor(border);
            ccColor4F f = col;
            f.a = c.hbFill ? c.hbFillAlpha * col.a : 0.f;
            return CCDrawNode::drawCircle(pos, radius, f, c.borderWidth, col, segments);
        }
        return CCDrawNode::drawCircle(pos, radius, fill, width, border, segments);
    }
};

// ---------------------------------------------------------------------------
//  Unlock all icons & colors
// ---------------------------------------------------------------------------
class $modify(SKGameManager, GameManager) {
    bool isIconUnlocked(int id, IconType type) {
        if (cfg().unlockAll) return true;
        return GameManager::isIconUnlocked(id, type);
    }

    bool isColorUnlocked(int id, UnlockType type) {
        if (cfg().unlockAll) return true;
        return GameManager::isColorUnlocked(id, type);
    }
};
