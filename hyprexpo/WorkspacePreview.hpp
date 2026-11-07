#pragma once

#include <type_traits>
#include <utility>

namespace Preview {
    template <typename Workspace, typename Monitor>
    bool canRender(const Workspace& workspace, const Monitor& monitor) {
        return monitor && (!workspace || (workspace->m_monitor == monitor && workspace->type() == decltype(workspace->type())::NORMAL));
    }

    template <typename Workspace, typename Monitor>
    Workspace localAnchor(const Workspace& requested, const Workspace& current, const Monitor& monitor) {
        return canRender(requested, monitor) ? requested : current;
    }

    // Override only the values sampled by rendering. Animation goals, timing, and
    // callbacks belong to the live compositor and must survive a thumbnail render.
    template <typename Workspace, typename Monitor>
    class CWorkspacePreviewState {
      public:
        CWorkspacePreviewState(Workspace workspace, Monitor monitor, bool& blockFeedback) :
            m_workspace(std::move(workspace)), m_monitor(std::move(monitor)), m_blockFeedback(blockFeedback) {
            if (!canRender(m_workspace, m_monitor))
                return;

            m_active = m_monitor->m_activeWorkspace;
            if (!canRender(m_active, m_monitor))
                return;
            m_special          = m_monitor->m_activeSpecialWorkspace;
            m_wasBlocking      = m_blockFeedback;
            m_activeWasVisible = m_active && m_active->visible();
            if (m_workspace) {
                m_wasVisible = m_workspace->visible();
                m_alpha      = m_workspace->m_alpha->value();
                m_offset     = m_workspace->m_renderOffset->value();
            }

            m_valid         = true;
            m_blockFeedback = true;
            if (m_active)
                m_active->setVisible(false);
            if (m_workspace != m_active)
                m_monitor->m_activeSpecialWorkspace.reset();
            if (m_workspace) {
                m_monitor->m_activeWorkspace = m_workspace;
                m_workspace->setVisible(true);
                m_workspace->m_alpha->value()        = 1.F;
                m_workspace->m_renderOffset->value() = {};
            }
        }

        ~CWorkspacePreviewState() {
            if (!m_valid)
                return;
            if (m_workspace) {
                m_workspace->m_alpha->value()        = m_alpha;
                m_workspace->m_renderOffset->value() = m_offset;
                m_workspace->setVisible(m_wasVisible);
            }
            if (m_active)
                m_active->setVisible(m_activeWasVisible);
            m_monitor->m_activeWorkspace        = m_active;
            m_monitor->m_activeSpecialWorkspace = m_special;
            m_blockFeedback                     = m_wasBlocking;
        }

        CWorkspacePreviewState(const CWorkspacePreviewState&)            = delete;
        CWorkspacePreviewState& operator=(const CWorkspacePreviewState&) = delete;

        explicit                operator bool() const {
            return m_valid;
        }

      private:
        Workspace                                                                         m_workspace;
        Monitor                                                                           m_monitor;
        Workspace                                                                         m_active, m_special;
        bool&                                                                             m_blockFeedback;
        bool                                                                              m_valid            = false;
        bool                                                                              m_wasBlocking      = false;
        bool                                                                              m_activeWasVisible = false;
        bool                                                                              m_wasVisible       = false;
        std::remove_cvref_t<decltype(std::declval<Workspace>()->m_alpha->value())>        m_alpha{};
        std::remove_cvref_t<decltype(std::declval<Workspace>()->m_renderOffset->value())> m_offset{};
    };
}
