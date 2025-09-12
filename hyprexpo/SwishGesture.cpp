#include "SwishGesture.hpp"

#include "overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

void CSwishGesture::begin(const ITrackpadGesture::STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);

    if (!g_pOverview)
        g_pOverview = std::make_unique<COverview>(g_pCompositor->m_lastMonitor->m_activeWorkspace, true, 1);
}

void CSwishGesture::update(const ITrackpadGesture::STrackpadGestureUpdate& e) {

    g_pOverview->onSwipeUpdate(e.swipe->delta);
}

void CSwishGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    g_pOverview->onSwipeEnd();
}
