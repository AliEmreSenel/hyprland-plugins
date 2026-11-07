#pragma once

#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <vector>

namespace Render {
    class CRenderContext;
}

enum class EOverviewMode {
    EXPO,
    SWISH
};

class COverview {
  public:
    COverview(PHLWORKSPACE startedOn_, bool swipe = false, EOverviewMode mode = EOverviewMode::EXPO);
    ~COverview();

    void render(Render::CRenderContext& ctx);
    void damage();
    void onDamageReported();
    void onPreRender();

    void onSwipeUpdate(Vector2D delta);
    void onSwipeEnd(bool cancelled = false);
    bool beginSwipe(EOverviewMode mode);
    bool isFinished() const;

    // close without a selection
    void          close(bool keepCurrentWorkspace = false);
    void          selectHoveredWorkspace();

    bool          blockOverviewRendering = false;
    bool          blockDamageReporting   = false;
    bool          fullyOpened            = false;
    bool          m_isSwiping            = false;

    PHLMONITORREF pMonitor;
    struct SWorkspaceImage {
        SP<Render::IFramebuffer> fb;
        std::string              workspaceAddress;
        PHLWORKSPACE             pWorkspace;
    };

  private:
    void                         redrawID(int id);
    void                         redrawAll();
    void                         onWorkspaceChange();
    void                         fullRender(Render::CRenderContext& ctx);
    int                          hoveredTile() const;
    double                       gapSize() const;

    int                          SIDE_LENGTH = 3;
    int                          GAP_WIDTH   = 5;
    CHyprColor                   BG_COLOR    = CHyprColor{0.1, 0.1, 0.1, 1.0};

    bool                         damageDirty = false;

    Vector2D                     lastMousePosLocal = Vector2D{};

    Vector2D                     totalSwipeDelta = Vector2D{0, 0};

    EOverviewMode                mode = EOverviewMode::EXPO;

    int                          openedID  = -1;
    int                          closeOnID = -1;

    std::vector<SWorkspaceImage> images;

    PHLWORKSPACE                 startedOn;

    PHLANIMVAR<Vector2D>         size;
    PHLANIMVAR<Vector2D>         pos;
    PHLANIMVAR<float>            scale;
    int                          hoveredID = -1;

    bool                         closing  = false;
    bool                         finished = false;

    CHyprSignalListener          mouseMoveHook;
    CHyprSignalListener          mouseButtonHook;
    CHyprSignalListener          touchMoveHook;
    CHyprSignalListener          touchDownHook;

    bool                         swipeStartedOpen = false;

    friend class COverviewPassElement;
};

inline std::unique_ptr<COverview> g_pOverview;
