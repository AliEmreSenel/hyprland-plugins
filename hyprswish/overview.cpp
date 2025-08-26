#include "overview.hpp"
#include "src/plugins/PluginAPI.hpp"
#include "src/render/OpenGL.hpp"
#include <algorithm>
#include <any>
#include <hyprlang.hpp>
#define private public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/managers/AnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#undef private
#include "OverviewPassElement.hpp"

static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pOverview->damage();
}

static void removeOverview(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pOverview.reset();
}

COverview::~COverview() {
    g_pHyprRenderer->makeEGLCurrent();
    images.clear(); // otherwise we get a vram leak
    g_pInputManager->unsetCursorImage();
    g_pHyprOpenGL->markBlurDirtyForMonitor(pMonitor.lock());
}

COverview::COverview(PHLWORKSPACE startedOn_) : startedOn(startedOn_) {
    const auto PMONITOR = g_pCompositor->m_lastMonitor.lock();
    pMonitor            = PMONITOR;

    static auto* const* PCOLUMNS = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprswish:columns")->getDataStaticPtr();
    static auto* const* PSKIP    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprswish:skip_empty")->getDataStaticPtr();
    static auto const*  PMETHOD  = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprswish:workspace_method")->getDataStaticPtr();

    SIDE_LENGTH = **PCOLUMNS;

    // process the method
    bool     methodCenter  = true;
    int      methodStartID = pMonitor->activeWorkspaceID();
    CVarList method{*PMETHOD, 0, 's', true};
    if (method.size() < 2)
        Debug::log(ERR, "[he] invalid workspace_method");
    else {
        methodCenter  = method[0] == "center";
        methodStartID = getWorkspaceIDNameFromString(method[1]).id;
        if (methodStartID == WORKSPACE_INVALID)
            methodStartID = pMonitor->activeWorkspaceID();
    }

    images.resize(SIDE_LENGTH * SIDE_LENGTH);

    // r includes empty workspaces; m skips over them
    std::string selector = **PSKIP ? "m" : "r";

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

        auto PWORKSPACESTART = g_pCompositor->getWorkspaceByID(currentID);
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

    g_pHyprRenderer->makeEGLCurrent();

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
        image.fb.alloc(monbox.w, monbox.h, PMONITOR->m_output->state->state().drmFormat);

        CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
        g_pHyprRenderer->beginRender(PMONITOR, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, &image.fb);

        g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0});

        const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(image.workspaceID);

        if (PWORKSPACE == startedOn) {
            currentid       = i;
            totalSwipeDelta = (Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) / (SIDE_LENGTH - 1);
        }

        if (PWORKSPACE) {
            image.pWorkspace            = PWORKSPACE;
            PMONITOR->m_activeWorkspace = PWORKSPACE;
            PWORKSPACE->startAnim(true, true, true);
            PWORKSPACE->m_visible = true;

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace = openSpecial;

            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

            PWORKSPACE->m_visible = false;
            PWORKSPACE->startAnim(false, false, true);

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace.reset();
        } else
            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

        image.box = {(i % SIDE_LENGTH) * tileRenderSize.x + (i % SIDE_LENGTH) * GAP_WIDTH, (i / SIDE_LENGTH) * tileRenderSize.y + (i / SIDE_LENGTH) * GAP_WIDTH, tileRenderSize.x,
                     tileRenderSize.y};

        g_pHyprOpenGL->m_renderData.blockScreenShader = true;
        g_pHyprRenderer->endRender();
    }

    g_pHyprRenderer->m_bBlockSurfaceFeedback = false;

    PMONITOR->m_activeSpecialWorkspace = openSpecial;
    PMONITOR->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    startedOn->startAnim(true, true, true);

    // zoom on the current workspace.
    // const auto& TILE = images[std::clamp(currentid, 0, SIDE_LENGTH * SIDE_LENGTH)];
    g_pAnimationManager->createAnimation(1.0f, scale, g_pConfigManager->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);
    g_pAnimationManager->createAnimation((-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) * pMonitor->m_scale) *
                                             (pMonitor->m_size / tileSize),
                                         pos, g_pConfigManager->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    scale->setUpdateCallback(damageMonitor);
    pos->setUpdateCallback(damageMonitor);

    openedID = currentid;

    g_pInputManager->setCursorImageUntilUnset("left_ptr");

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;

    auto onCursorMove = [this](void* self, SCallbackInfo& info, std::any param) {
        if (closing)
            return;

        info.cancelled    = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
    };

    auto onCursorSelect = [this](void* self, SCallbackInfo& info, std::any param) {
        if (closing)
            return;

        info.cancelled = true;

        // get tile x,y
        int x = lastMousePosLocal.x / pMonitor->m_size.x * SIDE_LENGTH;
        int y = lastMousePosLocal.y / pMonitor->m_size.y * SIDE_LENGTH;

        closeOnID = x + y * SIDE_LENGTH;

        close();
    };

    mouseMoveHook = g_pHookSystem->hookDynamic("mouseMove", onCursorMove);
    touchMoveHook = g_pHookSystem->hookDynamic("touchMove", onCursorMove);

    mouseButtonHook = g_pHookSystem->hookDynamic("mouseButton", onCursorSelect);
    touchDownHook   = g_pHookSystem->hookDynamic("touchDown", onCursorSelect);
}

void COverview::redrawID(int id, bool forcelowres) {
    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    blockOverviewRendering = true;

    g_pHyprRenderer->makeEGLCurrent();

    id = std::clamp(id, 0, SIDE_LENGTH * SIDE_LENGTH);

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH, GAP_WIDTH} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!forcelowres || !ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    auto& image = images[id];

    if (image.fb.m_size != monbox.size()) {
        image.fb.release();
        image.fb.alloc(monbox.w, monbox.h, pMonitor->m_output->state->state().drmFormat);
    }

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, &image.fb);

    g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0});

    const auto   PWORKSPACE = image.pWorkspace;

    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    startedOn->m_visible = false;

    if (PWORKSPACE) {
        pMonitor->m_activeWorkspace = PWORKSPACE;
        PWORKSPACE->startAnim(true, true, true);
        PWORKSPACE->m_visible = true;

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        PWORKSPACE->m_visible = false;
        PWORKSPACE->startAnim(false, false, true);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace.reset();
    } else
        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

    g_pHyprOpenGL->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    startedOn->startAnim(true, true, true);

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

    Vector2D SIZE = pMonitor->m_size * pMonitor->m_scale;

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
    g_pCompositor->scheduleFrameForMonitor(pMonitor.lock());
}

void COverview::close() {
    if (closing)
        return;

    const int   ID = closeOnID == -1 ? hoveredID : closeOnID;

    const auto& TILE = images[std::clamp(ID, 0, SIDE_LENGTH * SIDE_LENGTH)];

    Vector2D    tileSize = (pMonitor->m_size / SIDE_LENGTH);
    *pos                 = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{ID % SIDE_LENGTH, ID / SIDE_LENGTH}) * pMonitor->m_scale) * (pMonitor->m_size / tileSize);
    *scale               = 1.0f;
    closing              = true;

    pos->setCallbackOnEnd(removeOverview);
    redrawAll();

    if (TILE.workspaceID != pMonitor->activeWorkspaceID()) {
        pMonitor->setSpecialWorkspace(0);

        // If this tile's workspace was WORKSPACE_INVALID, move to the next
        // empty workspace. This should only happen if skip_empty is on, in
        // which case some tiles will be left with this ID intentionally.
        const int  NEWID = TILE.workspaceID == WORKSPACE_INVALID ? getWorkspaceIDNameFromString("emptynm").id : TILE.workspaceID;

        const auto NEWIDWS = g_pCompositor->getWorkspaceByID(NEWID);

        const auto OLDWS = pMonitor->m_activeWorkspace;

        if (!NEWIDWS)
            g_pKeybindManager->changeworkspace(std::to_string(NEWID));
        else
            g_pKeybindManager->changeworkspace(NEWIDWS->getConfigName());

        pMonitor->m_activeWorkspace->startAnim(true, true, true);
        OLDWS->startAnim(false, false, true);

        startedOn = pMonitor->m_activeWorkspace;
    }
}

void COverview::onPreRender() {
    int hoveredX = (((pMonitor->m_size.x / 2) - pos->value().x / scale->value()) / pMonitor->m_size.x);
    int hoveredY = (((pMonitor->m_size.y / 2) - pos->value().y / scale->value()) / pMonitor->m_size.y);
    hoveredID    = hoveredX + hoveredY * SIDE_LENGTH;

    redrawID(hoveredID, true);
    redrawAllValid(true);
}

void COverview::onWorkspaceChange() {
    if (valid(startedOn))
        startedOn->startAnim(false, false, true);
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

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    Vector2D tileRenderSize = pMonitor->m_size * scale->value();

    g_pHyprOpenGL->clear(BG_COLOR.stripA());

    for (size_t y = 0; y < (size_t)SIDE_LENGTH; ++y) {
        for (size_t x = 0; x < (size_t)SIDE_LENGTH; ++x) {
            CBox texbox = {x * tileRenderSize.x, y * tileRenderSize.y, tileRenderSize.x, tileRenderSize.y};
            texbox.scale(pMonitor->m_scale).translate(pos->value());
            texbox.round();
            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            g_pHyprOpenGL->renderTextureWithDamage(images[x + y * SIDE_LENGTH].fb.getTexture(), texbox, damage, 1.0);
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
    static auto* const* PDISTANCE = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprswish:gesture_distance")->getDataStaticPtr();
    static auto* const* PSCALE    = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprswish:zoom_scale")->getDataStaticPtr();

    totalSwipeDelta -= delta / **PDISTANCE;
    totalSwipeDelta = clamp(totalSwipeDelta, 0.0001, 0.9999);

    const auto POSMAX = (Vector2D{SIDE_LENGTH, SIDE_LENGTH} * pMonitor->m_scale * pMonitor->m_size * scale->value()) - (pMonitor->m_size * pMonitor->m_scale);
    const auto POSMIN = Vector2D{0, 0};

    pos->setValueAndWarp(lerp(POSMIN, -POSMAX, totalSwipeDelta));
    *scale = **PSCALE;
}

void COverview::onSwipeEnd() {
    close();
}
