#include "OverviewPassElement.hpp"
#include <hyprland/src/render/OpenGL.hpp>
#include "overview.hpp"

std::vector<UP<IPassElement>> COverviewPassElement::draw(Render::CRenderContext& ctx) {
    if (g_pOverview && g_pOverview->pMonitor)
        g_pOverview->fullRender(ctx);
    return {};
}

bool COverviewPassElement::needsLiveBlur(Render::CRenderContext& ctx) {
    return false;
}

bool COverviewPassElement::needsPrecomputeBlur(Render::CRenderContext& ctx) {
    return false;
}

std::optional<CBox> COverviewPassElement::boundingBox(Render::CRenderContext& ctx) {
    if (!g_pOverview || !g_pOverview->pMonitor)
        return std::nullopt;

    return CBox{{}, g_pOverview->pMonitor->m_size};
}

CRegion COverviewPassElement::opaqueRegion(Render::CRenderContext& ctx) {
    if (!g_pOverview || !g_pOverview->pMonitor)
        return CRegion{};

    return CBox{{}, g_pOverview->pMonitor->m_size};
}
