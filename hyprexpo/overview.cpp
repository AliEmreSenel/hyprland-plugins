
#include <algorithm>

#include <cstddef>
#include "overview.hpp"
#include "globals.hpp"
#include "src/config/shared/Types.hpp"
#include "state/MonitorState.hpp"
#include "state/WorkspaceQuery.hpp"
#include <hyprlang.hpp>
#define private   public
#define protected public
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/string/VarList.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/helpers/Format.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/varlist/VarList.hpp>
#include <hyprland/src/render/Renderer.hpp>
#undef private
#undef protected
#include "OverviewPassElement.hpp"

static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pOverview->damage();
}

static void removeOverview(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pOverview.reset();
}

using namespace Hyprutils::String;

static uint32_t framebufferFormatWithAlpha(uint32_t drmFormat) {
    const auto alphaFormat = NFormatUtils::alphaFormat(drmFormat);
    return alphaFormat == 0 ? DRM_FORMAT_ABGR8888 : alphaFormat;
}

static void clearWithColor(const CHyprColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void ensureFramebuffer(COverview::SWorkspaceImage& image, const CBox& monbox, uint32_t drmFormat) {
    if (!image.fb)
        image.fb = g_pHyprRenderer->createFB("hyprexpo");

    if (image.fb->m_size != monbox.size()) {
        image.fb->release();
        image.fb->alloc(monbox.w, monbox.h, drmFormat);
    }
}

COverview::~COverview() {
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    images.clear(); // otherwise we get a vram leak

    Pointer::Cursor::overrideController->unsetOverride(Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);
    if (pMonitor)
        pMonitor->m_blurFBDirty = true;
}

COverview::COverview(PHLWORKSPACE startedOn_, bool swipe_, int type_) : startedOn(startedOn_), swipe(swipe_), type(type_) {
    const auto PMONITOR = Desktop::focusState()->monitor();
    pMonitor            = PMONITOR;

    static const CConfigValue<Config::INTEGER> PCOLUMNS("plugin:hyprexpo:columns");
    static const CConfigValue<Config::INTEGER> PGAPS("plugin:hyprexpo:gap_size");
    static const CConfigValue<Config::INTEGER> PCOL("plugin:hyprexpo:bg_col");
    static const CConfigValue<Config::INTEGER> PSKIP("plugin:hyprexpo:skip_empty");
    static const CConfigValue<Config::STRING>  PMETHOD("plugin:hyprexpo:workspace_method");

    SIDE_LENGTH = *PCOLUMNS;
    GAP_WIDTH   = *PGAPS;
    BG_COLOR    = CHyprColor(*PCOL);

    // process the method
    bool                        methodCenter  = true;
    int                         methodStartID = pMonitor->activeWorkspaceID();
    Hyprutils::String::CVarList method{*PMETHOD, 0, 's', true};
    if (method.size() < 2)
        Log::logger->log(Log::ERR, "[he] invalid workspace_method");
    else {
        methodCenter  = method[0] == "center";
        methodStartID = getWorkspaceIDNameFromString(method[1]).id;
        if (methodStartID == WORKSPACE_INVALID)
            methodStartID = pMonitor->activeWorkspaceID();
    }

    images.resize(SIDE_LENGTH * SIDE_LENGTH);

    // r includes empty workspaces; m skips over them
    std::string selector = *PSKIP ? "m" : "r";

    if (methodCenter) {
        int currentID = methodStartID;
        int firstID   = currentID;

        int backtracked = 0;

        // Initialize tiles to WORKSPACE_INVALID; cliking one of these results
        // in changing to "emptynm" (next empty workspace). Tiles with this id
        // will only remain if skip_empty is on.
        for (size_t i = 0; i < images.size(); i++) {
            images[i].workspaceID = WORKSPACE_INVALID;
        }

        // Scan through workspaces lower than methodStartID until we wrap; count how many
        for (size_t i = 1; i < images.size() / 2; ++i) {
            currentID = getWorkspaceIDNameFromString(selector + "-" + std::to_string(i)).id;
            if (currentID >= firstID)
                break;

            backtracked++;
            firstID = currentID;
        }

        // Scan through workspaces higher than methodStartID. If using "m"
        // (skip_empty), stop when we wrap, leaving the rest of the workspace
        // ID's set to WORKSPACE_INVALID
        for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
            auto& image = images[i];
            if ((int64_t)i - backtracked < 0) {
                currentID = getWorkspaceIDNameFromString(selector + std::to_string((int64_t)i - backtracked)).id;
            } else {
                currentID = getWorkspaceIDNameFromString(selector + "+" + std::to_string((int64_t)i - backtracked)).id;
                if (i > 0 && currentID <= firstID)
                    break;
            }
            image.workspaceID = currentID;
        }

    } else {
        int currentID         = methodStartID;
        images[0].workspaceID = currentID;

        auto PWORKSPACESTART = State::workspaceState()->query().id(currentID).run();
        if (!PWORKSPACESTART)
            PWORKSPACESTART = CWorkspace::create(currentID, pMonitor.lock(), std::to_string(currentID));

        pMonitor->m_activeWorkspace = PWORKSPACESTART;

        // Scan through workspaces higher than methodStartID. If using "m"
        // (skip_empty), stop when we wrap, leaving the rest of the workspace
        // ID's set to WORKSPACE_INVALID
        for (size_t i = 1; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
            auto& image = images[i];
            currentID   = getWorkspaceIDNameFromString(selector + "+" + std::to_string(i)).id;
            if (currentID <= methodStartID)
                break;
            image.workspaceID = currentID;
        }

        pMonitor->m_activeWorkspace = startedOn;
    }

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH * pMonitor->m_scale, GAP_WIDTH * pMonitor->m_scale} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    int          currentid = 0;

    PHLWORKSPACE openSpecial = PMONITOR->m_activeSpecialWorkspace;
    if (openSpecial)
        PMONITOR->m_activeSpecialWorkspace.reset();

    g_pHyprRenderer->m_bBlockSurfaceFeedback = true;

    startedOn->m_visible = false;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        COverview::SWorkspaceImage& image = images[i];
        ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(PMONITOR->m_output->state->state().drmFormat));

        CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
        g_pHyprRenderer->beginRender(PMONITOR, fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, image.fb);

        clearWithColor(CHyprColor{0, 0, 0, 1.0});

        const auto PWORKSPACE = State::workspaceState()->query().id(image.workspaceID).run();

        if (PWORKSPACE == startedOn) {
            currentid       = i;
            totalSwipeDelta = (Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) / (SIDE_LENGTH - 1);
        }

        if (PWORKSPACE) {
            image.pWorkspace            = PWORKSPACE;
            PMONITOR->m_activeWorkspace = PWORKSPACE;
            Animation::Workspace::startAnimation(PWORKSPACE, Animation::Workspace::ANIMATION_TYPE_IN, true, true);
            PWORKSPACE->m_visible = true;

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace = openSpecial;

            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

            PWORKSPACE->m_visible = false;
            Animation::Workspace::startAnimation(PWORKSPACE, Animation::Workspace::ANIMATION_TYPE_OUT, false, true);

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace.reset();
        } else
            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

        image.box = {(i % SIDE_LENGTH) * tileRenderSize.x + (i % SIDE_LENGTH) * GAP_WIDTH, (i / SIDE_LENGTH) * tileRenderSize.y + (i / SIDE_LENGTH) * GAP_WIDTH, tileRenderSize.x,
                     tileRenderSize.y};

        g_pHyprRenderer->m_renderData.blockScreenShader = true;
        g_pHyprRenderer->endRender();
    }

    g_pHyprRenderer->m_bBlockSurfaceFeedback = false;

    PMONITOR->m_activeSpecialWorkspace = openSpecial;
    PMONITOR->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    Animation::Workspace::startAnimation(startedOn, Animation::Workspace::ANIMATION_TYPE_IN, true, true);

    // zoom on the current workspace.
    // const auto& TILE = images[std::clamp(currentid, 0, SIDE_LENGTH * SIDE_LENGTH)];
    if (type == 0)
        Animation::mgr()->createAnimation(pMonitor->m_size * pMonitor->m_size / tileSize, size, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);
    else
        Animation::mgr()->createAnimation(1.0f, scale, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);
    Animation::mgr()->createAnimation((-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) * pMonitor->m_scale) *
                                          (pMonitor->m_size / tileSize),
                                      pos, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);

    pos->setUpdateCallback(damageMonitor);
    if (type == 0)
        size->setUpdateCallback(damageMonitor);
    else
        scale->setUpdateCallback(damageMonitor);

    if (!swipe && type == 0) {
        *size = pMonitor->m_size;
        *pos  = {0, 0};

        size->setCallbackOnEnd([this](auto) { redrawAll(true); });
    }

    openedID = currentid;

    Pointer::Cursor::overrideController->setOverride("left_ptr", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;

    auto onCursorMove = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled    = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
    };

    auto onCursorSelect = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled = true;

        // get tile x,y
        int x = lastMousePosLocal.x / pMonitor->m_size.x * SIDE_LENGTH;
        int y = lastMousePosLocal.y / pMonitor->m_size.y * SIDE_LENGTH;

        closeOnID = x + y * SIDE_LENGTH;

        close();
    };

    mouseMoveHook = Event::bus()->m_events.input.mouse.move.listen([onCursorMove](Vector2D, Event::SCallbackInfo& info) { onCursorMove(info); });
    touchMoveHook = Event::bus()->m_events.input.touch.motion.listen([onCursorMove](ITouch::SMotionEvent, Event::SCallbackInfo& info) { onCursorMove(info); });

    mouseButtonHook = Event::bus()->m_events.input.mouse.button.listen([onCursorSelect](IPointer::SButtonEvent, Event::SCallbackInfo& info) { onCursorSelect(info); });
    touchDownHook   = Event::bus()->m_events.input.touch.down.listen([onCursorSelect](ITouch::SDownEvent, Event::SCallbackInfo& info) { onCursorSelect(info); });
}

void COverview::selectHoveredWorkspace() {
    if (closing)
        return;

    // get tile x,y
    int x     = lastMousePosLocal.x / pMonitor->m_size.x * SIDE_LENGTH;
    int y     = lastMousePosLocal.y / pMonitor->m_size.y * SIDE_LENGTH;
    closeOnID = x + y * SIDE_LENGTH;
}

void COverview::redrawID(int id, bool forcelowres) {
    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    blockOverviewRendering = true;

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    id = std::clamp(id, 0, SIDE_LENGTH * SIDE_LENGTH);

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH, GAP_WIDTH} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!forcelowres || !ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    auto& image = images[id];

    ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(pMonitor->m_output->state->state().drmFormat));

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, image.fb);

    clearWithColor(CHyprColor{0, 0, 0, 1.0});

    const auto   PWORKSPACE = image.pWorkspace;

    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    startedOn->m_visible = false;

    if (PWORKSPACE) {
        pMonitor->m_activeWorkspace = PWORKSPACE;
        Animation::Workspace::startAnimation(PWORKSPACE, Animation::Workspace::ANIMATION_TYPE_IN, true, true);
        PWORKSPACE->m_visible = true;

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        PWORKSPACE->m_visible = false;
        Animation::Workspace::startAnimation(PWORKSPACE, Animation::Workspace::ANIMATION_TYPE_OUT, false, true);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace.reset();
    } else
        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    Animation::Workspace::startAnimation(startedOn, Animation::Workspace::ANIMATION_TYPE_IN, true, true);

    blockOverviewRendering = false;
}

void COverview::redrawAll(bool forcelowres) {
    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        redrawID(i, forcelowres);
    }
}

void COverview::redrawAllValid(bool forcelowres) {
    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        if (images[i].pWorkspace)
            redrawID(i, forcelowres);
    }
}

void COverview::damage() {
    blockDamageReporting = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = false;
}

void COverview::onDamageReported() {
    damageDirty = true;

    Vector2D SIZE = type == 0 ? size->value() : pMonitor->m_size * pMonitor->m_scale;

    Vector2D tileSize       = (SIZE / SIDE_LENGTH);
    Vector2D tileRenderSize = (SIZE - Vector2D{GAP_WIDTH, GAP_WIDTH} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    // const auto& TILE           = images[std::clamp(openedID, 0, SIDE_LENGTH * SIDE_LENGTH)];
    CBox texbox = CBox{(openedID % SIDE_LENGTH) * tileRenderSize.x + (openedID % SIDE_LENGTH) * GAP_WIDTH,
                       (openedID / SIDE_LENGTH) * tileRenderSize.y + (openedID / SIDE_LENGTH) * GAP_WIDTH, tileRenderSize.x, tileRenderSize.y}
                      .translate(pMonitor->m_position);

    damage();

    blockDamageReporting = true;
    g_pHyprRenderer->damageBox(texbox);
    blockDamageReporting = false;

    pMonitor.lock()->scheduleFrame();
}

void COverview::close() {
    if (closing)
        return;
    closing = true;

    const int   ID = closeOnID == -1 ? hoveredID : closeOnID;

    const auto& TILE = images[std::clamp(ID, 0, SIDE_LENGTH * SIDE_LENGTH)];

    Vector2D    tileSize = (pMonitor->m_size / SIDE_LENGTH);
    *pos                 = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{ID % SIDE_LENGTH, ID / SIDE_LENGTH}) * pMonitor->m_scale) * (pMonitor->m_size / tileSize);
    if (type == 0) {
        *size = pMonitor->m_size * pMonitor->m_size / tileSize;
        size->setCallbackOnEnd(removeOverview);
    } else {
        *scale = 1.0f;
        scale->setCallbackOnEnd(removeOverview);
    }

    redrawAll();

    if (TILE.workspaceID != pMonitor->activeWorkspaceID()) {
        pMonitor->setSpecialWorkspace(0);

        // If this tile's workspace was WORKSPACE_INVALID, move to the next
        // empty workspace. This should only happen if skip_empty is on, in
        // which case some tiles will be left with this ID intentionally.
        const int  NEWID = TILE.workspaceID == WORKSPACE_INVALID ? getWorkspaceIDNameFromString("emptynm").id : TILE.workspaceID;

        const auto NEWIDWS = State::workspaceState()->query().id(NEWID).run();

        const auto OLDWS = pMonitor->m_activeWorkspace;

        if (!NEWIDWS)
            auto _ = Config::Actions::changeWorkspace(std::to_string(NEWID));
        else
            auto _ = Config::Actions::changeWorkspace(NEWIDWS->getConfigName());

        Animation::Workspace::startAnimation(pMonitor->m_activeWorkspace, Animation::Workspace::ANIMATION_TYPE_IN, true, true);
        Animation::Workspace::startAnimation(OLDWS, Animation::Workspace::ANIMATION_TYPE_OUT, false, true);

        startedOn = pMonitor->m_activeWorkspace;
    }
}

void COverview::onPreRender() {
    int hoveredX =
        type == 0 ? ((lastMousePosLocal.x - pos->value().x) / size->value().x) * SIDE_LENGTH : (((pMonitor->m_size.x / 2) - pos->value().x / scale->value()) / pMonitor->m_size.x);
    int hoveredY =
        type == 0 ? ((lastMousePosLocal.y - pos->value().y) / size->value().y) * SIDE_LENGTH : (((pMonitor->m_size.y / 2) - pos->value().y / scale->value()) / pMonitor->m_size.y);
    hoveredID = hoveredX + hoveredY * SIDE_LENGTH;

    redrawID(hoveredID, true);
    redrawAllValid(true);
}

void COverview::onWorkspaceChange() {
    if (valid(startedOn))
        Animation::Workspace::startAnimation(startedOn, Animation::Workspace::ANIMATION_TYPE_OUT, false, true);
    else
        startedOn = pMonitor->m_activeWorkspace;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        if (images[i].workspaceID != pMonitor->activeWorkspaceID())
            continue;

        openedID = i;
        break;
    }

    closeOnID = openedID;
    close();
}

void COverview::render() {
    g_pHyprRenderer->m_renderPass.add(makeUnique<COverviewPassElement>());
}

void COverview::fullRender() {
    const auto GAPSIZE = type == 0 ? ((closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH) : 0.0f;

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    Vector2D tileRenderSize = type == 0 ? ((size->value() - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1)) / SIDE_LENGTH) : (pMonitor->m_size * scale->value());

    clearWithColor(BG_COLOR.stripA());

    for (size_t y = 0; y < (size_t)SIDE_LENGTH; ++y) {
        for (size_t x = 0; x < (size_t)SIDE_LENGTH; ++x) {
            CBox texbox = {x * tileRenderSize.x + x * GAPSIZE, y * tileRenderSize.y + y * GAPSIZE, tileRenderSize.x, tileRenderSize.y};
            texbox.scale(pMonitor->m_scale).translate(pos->value());
            texbox.round();
            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            Render::GL::g_pHyprOpenGL->renderTexture(images[x + y * SIDE_LENGTH].fb->getTexture(), texbox, {.damage = &damage, .a = 1.0f});
            if (type == 0 && x + y * SIDE_LENGTH == hoveredID) {
                auto zoomFactor = (pMonitor->m_size.x / (size->value().x / SIDE_LENGTH)) - 2.0;
                Render::GL::g_pHyprOpenGL->renderRect(texbox, CHyprColor{1.0, 1.0, 1.0, lerp(0.0, 0.3, std::clamp(zoomFactor, 0.0, 1.0))}, {});
            }
        }
    }
}

static float lerp(const float& from, const float& to, const float perc) {
    return (to - from) * perc + from;
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const float perc) {
    return Vector2D{lerp(from.x, to.x, perc), lerp(from.y, to.y, perc)};
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const Vector2D perc) {
    return Vector2D{lerp(from.x, to.x, perc.x), lerp(from.y, to.y, perc.y)};
}

static Vector2D clamp(const Vector2D& val, const Vector2D& min, const Vector2D& max) {
    return Vector2D{std::clamp(val.x, min.x, max.x), std::clamp(val.y, min.y, max.y)};
}

static Vector2D clamp(const Vector2D& val, const double min, const double max) {
    return Vector2D{std::clamp(val.x, min, max), std::clamp(val.y, min, max)};
}

void COverview::onSwipeUpdate(Vector2D delta) {
    m_isSwiping = true;
    if (type == 0) {
        static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprexpo:gesture_distance");
        totalSwipeDelta.y -= delta.y / (double)*PDISTANCE;
        totalSwipeDelta.y = std::clamp(totalSwipeDelta.y, 0.0001, 0.9999);

        const float PERC = 1.0 - totalSwipeDelta.y;

        const auto  focusedID = fullyOpened ? hoveredID : openedID;
        const auto  SIZEMAX   = pMonitor->m_size * SIDE_LENGTH;
        const auto  POSMAX    = Vector2D{focusedID % SIDE_LENGTH, focusedID / SIDE_LENGTH} * pMonitor->m_size * pMonitor->m_scale;

        const auto  SIZEMIN = pMonitor->m_size;
        const auto  POSMIN  = Vector2D{0, 0};

        size->setValueAndWarp(lerp(SIZEMIN, SIZEMAX, PERC));
        pos->setValueAndWarp(lerp(POSMIN, -POSMAX, PERC));
    } else {

        static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprswish:gesture_distance");
        static const CConfigValue<Config::FLOAT>   PSCALE("plugin:hyprswish:zoom_scale");

        totalSwipeDelta -= delta / *PDISTANCE;
        totalSwipeDelta = clamp(totalSwipeDelta, 0.0001, 0.9999);

        const auto POSMAX = (Vector2D{SIDE_LENGTH, SIDE_LENGTH} * pMonitor->m_scale * pMonitor->m_size * scale->value()) - (pMonitor->m_size * pMonitor->m_scale);
        const auto POSMIN = Vector2D{0, 0};

        pos->setValueAndWarp(lerp(POSMIN, -POSMAX, totalSwipeDelta));
        *scale = *PSCALE;
    }
}

void COverview::onSwipeEnd() {
    if (type == 1) {
        close();
        return;
    }

    const auto SIZEMIN = pMonitor->m_size;
    const auto SIZEMAX = pMonitor->m_size * pMonitor->m_size / (pMonitor->m_size / SIDE_LENGTH);
    const auto PERC    = (size->value() - SIZEMIN).x / (SIZEMAX - SIZEMIN).x;
    if (PERC > 0.5) {
        close();
        return;
    }
    *size = pMonitor->m_size;
    *pos  = {0, 0};

    size->setCallbackOnEnd([this](WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) { redrawAll(true); });

    m_isSwiping = false;
    fullyOpened = true;
}
