#include "overview.hpp"
#include "Grid.hpp"
#include "OverviewPassElement.hpp"
#include "WorkspacePreview.hpp"

#include <algorithm>
#include <cmath>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/WorkspaceQuery.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/helpers/Format.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/string/VarList.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

using namespace Hyprutils::String;

static uint32_t framebufferFormatWithAlpha(uint32_t drmFormat) {
    const auto alphaFormat = NFormatUtils::alphaFormat(drmFormat);
    return alphaFormat == 0 ? DRM_FORMAT_ABGR8888 : alphaFormat;
}

static void clearWithColor(const CHyprColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

static bool ensureFramebuffer(COverview::SWorkspaceImage& image, const CBox& monbox, uint32_t drmFormat) {
    if (!image.fb)
        image.fb = g_pHyprRenderer->createFB("hyprexpo");

    if (!image.fb)
        return false;
    if (!image.fb->isAllocated() || image.fb->m_size != monbox.size() || image.fb->m_drmFormat != drmFormat) {
        image.fb->release();
        if (!image.fb->alloc(monbox.w, monbox.h, drmFormat))
            return false;
    }
    return true;
}

COverview::~COverview() {
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    if (size)
        size->resetAllCallbacks();
    if (scale)
        scale->resetAllCallbacks();
    if (pos)
        pos->resetAllCallbacks();
    images.clear();

    Pointer::Cursor::overrideController->unsetOverride(Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);
    if (pMonitor)
        pMonitor->m_blurFBDirty = true;
}

COverview::COverview(PHLWORKSPACE startedOn_, bool swipe, EOverviewMode mode_) : mode(mode_), startedOn(startedOn_) {
    const auto PMONITOR = Desktop::focusState()->monitor();
    pMonitor            = PMONITOR;

    static const CConfigValue<Config::INTEGER> PCOLUMNS("plugin:hyprexpo:columns");
    static const CConfigValue<Config::INTEGER> PGAPS("plugin:hyprexpo:gap_size");
    static const CConfigValue<Config::INTEGER> PCOL("plugin:hyprexpo:bg_col");
    static const CConfigValue<Config::INTEGER> PSKIP("plugin:hyprexpo:skip_empty");
    static const CConfigValue<Config::STRING>  PMETHOD("plugin:hyprexpo:workspace_method");

    SIDE_LENGTH       = Grid::columns(*PCOLUMNS);
    const auto MAXGAP = SIDE_LENGTH > 1 ? std::max(0.0, (std::min(pMonitor->m_size.x, pMonitor->m_size.y) - SIDE_LENGTH) / (SIDE_LENGTH - 1)) : 0.0;
    GAP_WIDTH         = static_cast<int>(std::clamp<double>(*PGAPS, 0, MAXGAP));
    BG_COLOR          = CHyprColor(*PCOL);

    bool           methodCenter  = true;
    auto           methodStartID = pMonitor->activeWorkspaceID();
    const CVarList method{*PMETHOD, 0, 's', true};
    if (method.size() != 2 || (method[0] != "center" && method[0] != "first"))
        Log::logger->log(Log::ERR, "[hyprexpo] invalid workspace_method, using center current");
    else {
        methodCenter  = method[0] == "center";
        const auto id = getWorkspaceIDNameFromString(method[1]).id;
        if (id != WORKSPACE_INVALID && !State::workspaceState()->isSpecial(id))
            methodStartID = id;
    }

    images.resize(SIDE_LENGTH * SIDE_LENGTH);
    const std::string selector    = *PSKIP ? "m" : "r";
    auto              anchor      = State::workspaceState()->query().id(methodStartID).run();
    const auto        localAnchor = Preview::localAnchor(anchor, startedOn, PMONITOR);
    if (localAnchor != anchor) {
        anchor        = localAnchor;
        methodStartID = anchor->m_id;
    }
    if (!anchor)
        anchor = CWorkspace::create(methodStartID, PMONITOR, std::to_string(methodStartID));

    // Hyprland's relative selectors use the focused monitor's active workspace.
    const auto ids = [&] {
        const auto                    savedWorkspace = PMONITOR->m_activeWorkspace;
        Hyprutils::Utils::CScopeGuard restore([&] { PMONITOR->m_activeWorkspace = savedWorkspace; });
        PMONITOR->m_activeWorkspace = anchor;
        return Grid::workspaceIDs(images.size(), methodCenter, WORKSPACE_INVALID, [&](int offset) -> int64_t {
            if (offset == 0)
                return methodStartID;
            return getWorkspaceIDNameFromString(selector + (offset > 0 ? "+" : "") + std::to_string(offset)).id;
        });
    }();

    int currentid = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        images[i].workspaceID = ids[i];
        if (ids[i] == startedOn->m_id)
            currentid = i;
    }
    openedID = currentid;
    if (mode == EOverviewMode::SWISH && SIDE_LENGTH > 1)
        totalSwipeDelta = Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH} / (SIDE_LENGTH - 1);

    redrawAll();
    const Vector2D tileSize = pMonitor->m_size / SIDE_LENGTH;

    // zoom on the current workspace.
    if (mode == EOverviewMode::EXPO)
        Animation::mgr()->createAnimation(pMonitor->m_size * pMonitor->m_size / tileSize, size, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);
    else
        Animation::mgr()->createAnimation(1.0f, scale, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);
    Animation::mgr()->createAnimation((-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) * pMonitor->m_scale) *
                                          (pMonitor->m_size / tileSize),
                                      pos, Config::animationTree()->getAnimationPropertyConfig("workspaces"), AVARDAMAGE_NONE);

    auto damageMonitor = [this](auto) { damage(); };
    pos->setUpdateCallback(damageMonitor);
    if (mode == EOverviewMode::EXPO)
        size->setUpdateCallback(damageMonitor);
    else
        scale->setUpdateCallback(damageMonitor);

    if (!swipe && mode == EOverviewMode::EXPO) {
        *size = pMonitor->m_size;
        *pos  = {0, 0};

        size->setCallbackOnEnd([this](auto) {
            fullyOpened = true;
            redrawAll();
        });
    }

    fullyOpened = !swipe;

    Pointer::Cursor::overrideController->setOverride("left_ptr", Pointer::Cursor::CURSOR_OVERRIDE_UNKNOWN);

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;

    auto onCursorMove = [this](Event::SCallbackInfo& info) {
        if (closing || m_isSwiping || !pMonitor)
            return;

        info.cancelled    = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
        damage();
    };

    auto onCursorSelect = [this](Event::SCallbackInfo& info) {
        if (closing || m_isSwiping || !pMonitor)
            return;
        info.cancelled = true;
        selectHoveredWorkspace();
        close();
    };

    mouseMoveHook   = Event::bus()->m_events.input.mouse.move.listen([onCursorMove](Vector2D, Event::SCallbackInfo& info) { onCursorMove(info); });
    mouseButtonHook = Event::bus()->m_events.input.mouse.button.listen([onCursorSelect](IPointer::SButtonEvent e, Event::SCallbackInfo& info) {
        if (e.state == WL_POINTER_BUTTON_STATE_PRESSED)
            onCursorSelect(info);
    });
    touchMoveHook   = Event::bus()->m_events.input.touch.motion.listen([this](ITouch::SMotionEvent e, Event::SCallbackInfo& info) {
        if (closing || m_isSwiping || !pMonitor)
            return;
        info.cancelled    = true;
        lastMousePosLocal = e.pos * pMonitor->m_size;
        damage();
    });
    touchDownHook   = Event::bus()->m_events.input.touch.down.listen([this, onCursorSelect](ITouch::SDownEvent e, Event::SCallbackInfo& info) {
        const auto monitor = e.device ? State::monitorState()->query().name(e.device->m_boundOutput).run() : nullptr;
        if ((monitor && monitor != pMonitor) || (!monitor && Desktop::focusState()->monitor() != pMonitor) || !pMonitor)
            return;
        lastMousePosLocal = e.pos * pMonitor->m_size;
        onCursorSelect(info);
    });
}

double COverview::gapSize() const {
    if (mode == EOverviewMode::SWISH)
        return 0;
    const auto zoom = SIDE_LENGTH > 1 ? (size->value().x / pMonitor->m_size.x - 1.0) / (SIDE_LENGTH - 1) : 0.0;
    return GAP_WIDTH * (1.0 - std::clamp(zoom, 0.0, 1.0));
}

int COverview::hoveredTile() const {
    if (!pMonitor)
        return -1;
    // Position is in physical pixels; sizes and input positions are logical.
    const auto point = mode == EOverviewMode::EXPO ? lastMousePosLocal : pMonitor->m_size / 2;
    const auto gap   = gapSize();
    const auto tile  = mode == EOverviewMode::EXPO ? (size->value() - Vector2D{gap, gap} * (SIDE_LENGTH - 1)) / SIDE_LENGTH : pMonitor->m_size * scale->value();
    return Grid::tileAtPosition(point.x, point.y, pos->value().x, pos->value().y, pMonitor->m_scale, tile.x, tile.y, gap, SIDE_LENGTH);
}

void COverview::selectHoveredWorkspace() {
    if (!closing)
        closeOnID = hoveredTile();
}

void COverview::redrawID(int id) {
    if (!pMonitor || id < 0 || id >= static_cast<int>(images.size()))
        return;
    const bool previousDamageBlock = blockDamageReporting;
    blockOverviewRendering         = true;
    blockDamageReporting           = true;
    Hyprutils::Utils::CScopeGuard unblock([this, previousDamageBlock] {
        blockOverviewRendering = false;
        blockDamageReporting   = previousDamageBlock;
    });
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    const CBox monbox{{0, 0}, pMonitor->m_pixelSize};

    auto&      image = images[id];

    image.pWorkspace = image.workspaceID == WORKSPACE_INVALID ? nullptr : State::workspaceState()->query().id(image.workspaceID).run();
    if (!Preview::canRender(image.pWorkspace, pMonitor.lock())) {
        // The workspace may have moved to another monitor since the grid was built.
        image.pWorkspace.reset();
        image.workspaceID = WORKSPACE_INVALID;
    }

    if (!ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(pMonitor->m_output->state->state().drmFormat))) {
        finished = true;
        damage();
        return;
    }

    Preview::CWorkspacePreviewState previewState(image.pWorkspace, pMonitor.lock(), g_pHyprRenderer->m_bBlockSurfaceFeedback);
    if (!previewState) {
        finished = true;
        damage();
        return;
    }

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    if (!g_pHyprRenderer->beginFullFakeRender(pMonitor.lock(), fakeDamage, image.fb)) {
        finished = true;
        damage();
        return;
    }

    clearWithColor(CHyprColor{0, 0, 0, 1.0});

    renderWorkspacePreview(pMonitor.lock(), image.pWorkspace, monbox);

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();
}

void COverview::redrawAll() {
    for (size_t i = 0; i < images.size(); ++i) {
        redrawID(i);
    }
}

void COverview::damage() {
    if (!pMonitor)
        return;
    const bool previousBlock = blockDamageReporting;
    blockDamageReporting     = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = previousBlock;
}

void COverview::onDamageReported() {
    damageDirty = true;

    damage();
    if (pMonitor)
        pMonitor->scheduleFrame();
}

void COverview::close(bool keepCurrentWorkspace) {
    if (closing || !pMonitor)
        return;
    closing     = true;
    m_isSwiping = false;
    size ? size->setCallbackOnEnd(nullptr) : scale->setCallbackOnEnd(nullptr);

    const int   ID   = Grid::validTile(closeOnID, openedID, images.size());
    const auto& TILE = images[ID];

    Vector2D    tileSize = (pMonitor->m_size / SIDE_LENGTH);
    *pos                 = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{ID % SIDE_LENGTH, ID / SIDE_LENGTH}) * pMonitor->m_scale) * (pMonitor->m_size / tileSize);
    if (mode == EOverviewMode::EXPO) {
        *size = pMonitor->m_size * pMonitor->m_size / tileSize;
        size->setCallbackOnEnd([this](auto) { damage(); });
    } else {
        *scale = 1.0f;
        scale->setCallbackOnEnd([this](auto) { damage(); });
    }

    redrawAll();
    damage();

    if (!keepCurrentWorkspace && closeOnID >= 0 && TILE.workspaceID != pMonitor->activeWorkspaceID()) {
        const auto focusResult = Config::Actions::focusMonitor(pMonitor.lock());
        if (!focusResult)
            return;

        // If this tile's workspace was WORKSPACE_INVALID, move to the next
        // empty workspace. This should only happen if skip_empty is on, in
        // which case some tiles will be left with this ID intentionally.
        const auto NEWID = TILE.workspaceID == WORKSPACE_INVALID ? getWorkspaceIDNameFromString("emptynm").id : TILE.workspaceID;

        const auto NEWIDWS = State::workspaceState()->query().id(NEWID).run();

        const auto OLDWS = pMonitor->m_activeWorkspace;

        if (NEWID == WORKSPACE_INVALID || !Preview::canRender(NEWIDWS, pMonitor.lock()))
            return;
        pMonitor->setSpecialWorkspace(0);
        const auto result = NEWIDWS ? Config::Actions::changeWorkspace(NEWIDWS) : Config::Actions::changeWorkspace(std::to_string(NEWID));
        if (!result) {
            Log::logger->log(Log::ERR, "[hyprexpo] workspace change failed: {}", result.error().message);
            return;
        }

        Animation::Workspace::startAnimation(pMonitor->m_activeWorkspace, Animation::Workspace::ANIMATION_TYPE_IN, true, true);
        Animation::Workspace::startAnimation(OLDWS, Animation::Workspace::ANIMATION_TYPE_OUT, false, true);

        startedOn = pMonitor->m_activeWorkspace;
    }
}

bool COverview::isFinished() const {
    return !pMonitor || finished || (closing && !pos->isBeingAnimated() && !(size ? size->isBeingAnimated() : scale->isBeingAnimated()));
}

void COverview::onPreRender() {
    if (!pMonitor)
        return;
    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        onWorkspaceChange();
        return;
    }
    const auto previous = hoveredID;
    hoveredID           = hoveredTile();
    if (previous != hoveredID)
        damage();
    if (damageDirty) {
        damageDirty = false;
        redrawAll();
    }
}

void COverview::onWorkspaceChange() {
    startedOn = pMonitor->m_activeWorkspace;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID == pMonitor->activeWorkspaceID()) {
            openedID = i;
            break;
        }
    }
    closeOnID = openedID;
    close(true);
}

void COverview::render() {
    g_pHyprRenderer->m_renderPass.add(makeUnique<COverviewPassElement>());
}

void COverview::fullRender() {
    if (!pMonitor)
        return;
    const auto GAPSIZE = gapSize();

    Vector2D tileRenderSize = mode == EOverviewMode::EXPO ? ((size->value() - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1)) / SIDE_LENGTH) : (pMonitor->m_size * scale->value());

    clearWithColor(BG_COLOR.stripA());

    for (int y = 0; y < SIDE_LENGTH; ++y) {
        for (int x = 0; x < SIDE_LENGTH; ++x) {
            CBox texbox = {x * tileRenderSize.x + x * GAPSIZE, y * tileRenderSize.y + y * GAPSIZE, tileRenderSize.x, tileRenderSize.y};
            texbox.scale(pMonitor->m_scale).translate(pos->value());
            texbox.round();
            CRegion     damage{0, 0, INT16_MAX, INT16_MAX};
            const auto& fb = images[x + y * SIDE_LENGTH].fb;
            if (fb && fb->isAllocated())
                Render::GL::g_pHyprOpenGL->renderTexture(fb->getTexture(), texbox, {.damage = &damage, .a = 1.0f});
            if (mode == EOverviewMode::EXPO && x + y * SIDE_LENGTH == hoveredID) {
                auto zoomFactor = (pMonitor->m_size.x / (size->value().x / SIDE_LENGTH)) - 2.0;
                Render::GL::g_pHyprOpenGL->renderRect(texbox, CHyprColor{1.0, 1.0, 1.0, static_cast<float>(0.3 * std::clamp(zoomFactor, 0.0, 1.0))}, {});
            }
        }
    }
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const float perc) {
    return Vector2D{std::lerp(from.x, to.x, perc), std::lerp(from.y, to.y, perc)};
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const Vector2D perc) {
    return Vector2D{std::lerp(from.x, to.x, perc.x), std::lerp(from.y, to.y, perc.y)};
}

static Vector2D clamp(const Vector2D& val, const double min, const double max) {
    return Vector2D{std::clamp(val.x, min, max), std::clamp(val.y, min, max)};
}

void COverview::onSwipeUpdate(Vector2D delta) {
    if (!m_isSwiping || closing || !pMonitor)
        return;
    if (mode == EOverviewMode::EXPO) {
        static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprexpo:gesture_distance");
        totalSwipeDelta.y -= delta.y / std::max<double>(1, *PDISTANCE);
        totalSwipeDelta.y = std::clamp(totalSwipeDelta.y, 0.0001, 0.9999);

        const float PERC = 1.0 - totalSwipeDelta.y;

        const auto  focusedID = Grid::validTile(closeOnID, openedID, images.size());
        const auto  SIZEMAX   = pMonitor->m_size * SIDE_LENGTH;
        const auto  POSMAX    = Vector2D{focusedID % SIDE_LENGTH, focusedID / SIDE_LENGTH} * pMonitor->m_size * pMonitor->m_scale;

        const auto  SIZEMIN = pMonitor->m_size;
        const auto  POSMIN  = Vector2D{0, 0};

        size->setValueAndWarp(lerp(SIZEMIN, SIZEMAX, PERC));
        pos->setValueAndWarp(lerp(POSMIN, -POSMAX, PERC));
    } else {

        static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprswish:gesture_distance");
        static const CConfigValue<Config::FLOAT>   PSCALE("plugin:hyprswish:zoom_scale");

        totalSwipeDelta -= delta / std::max<double>(1, *PDISTANCE);
        totalSwipeDelta = clamp(totalSwipeDelta, 0.0001, 0.9999);

        const float zoom   = std::clamp<float>(*PSCALE, 0.1f, 1.0f);
        const auto  POSMAX = pMonitor->m_size * pMonitor->m_scale * (SIDE_LENGTH - 1) * zoom;
        const auto  center = pMonitor->m_size * pMonitor->m_scale * ((1.0 - zoom) / 2);
        const auto  POSMIN = Vector2D{0, 0};

        pos->setValueAndWarp(center + lerp(POSMIN, -POSMAX, totalSwipeDelta));
        scale->setValueAndWarp(zoom);
    }
}

void COverview::onSwipeEnd(bool cancelled) {
    if (!m_isSwiping || closing || !pMonitor)
        return;
    m_isSwiping = false;
    if (cancelled || SIDE_LENGTH == 1) {
        closeOnID = openedID;
        if (!swipeStartedOpen || mode == EOverviewMode::SWISH || SIDE_LENGTH == 1) {
            close(true);
            return;
        }
    } else if (mode == EOverviewMode::SWISH) {
        closeOnID = hoveredTile();
        close();
        return;
    }

    const auto SIZEMIN = pMonitor->m_size;
    const auto SIZEMAX = pMonitor->m_size * pMonitor->m_size / (pMonitor->m_size / SIDE_LENGTH);
    const auto PERC    = (size->value() - SIZEMIN).x / (SIZEMAX - SIZEMIN).x;
    if (!cancelled && PERC > 0.5) {
        close(!swipeStartedOpen);
        return;
    }
    *size = pMonitor->m_size;
    *pos  = {0, 0};

    size->setCallbackOnEnd([this](auto) { redrawAll(); });

    m_isSwiping = false;
    fullyOpened = true;
    closeOnID   = -1;
}

bool COverview::beginSwipe(EOverviewMode gestureMode) {
    if (closing || m_isSwiping || mode != gestureMode || !pMonitor)
        return false;
    m_isSwiping      = true;
    swipeStartedOpen = fullyOpened;
    closeOnID        = fullyOpened ? Grid::validTile(hoveredTile(), openedID, images.size()) : openedID;
    if (mode == EOverviewMode::EXPO) {
        size->setCallbackOnEnd(nullptr);
        totalSwipeDelta.y = SIDE_LENGTH > 1 ? 1.0 - std::clamp((size->value().x / pMonitor->m_size.x - 1.0) / (SIDE_LENGTH - 1), 0.0, 1.0) : 0;
    }
    return true;
}
