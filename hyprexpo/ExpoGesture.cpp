#include "ExpoGesture.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/output/Monitor.hpp>

CExpoGesture::~CExpoGesture() {
    if (s_active == this)
        s_active = nullptr;
}

bool CExpoGesture::isActive() {
    return s_active != nullptr;
}

void CExpoGesture::begin(const STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);
    m_accepted = false;
    if (!e.swipe)
        return;
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor || !monitor->m_activeWorkspace || (g_pOverview && g_pOverview->pMonitor != monitor))
        return;
    if (!g_pOverview) {
        g_pOverview = std::make_unique<COverview>(monitor->m_activeWorkspace, true, m_mode);
    }
    m_accepted = g_pOverview->beginSwipe(m_mode);
    if (m_accepted)
        s_active = this;
}

void CExpoGesture::update(const STrackpadGestureUpdate& e) {
    if (m_accepted && g_pOverview && e.swipe)
        g_pOverview->onSwipeUpdate(e.swipe->delta * m_scale);
}

void CExpoGesture::end(const STrackpadGestureEnd& e) {
    if (m_accepted && g_pOverview)
        g_pOverview->onSwipeEnd(e.swipe && e.swipe->cancelled);
    m_accepted = false;
    if (s_active == this)
        s_active = nullptr;
}
