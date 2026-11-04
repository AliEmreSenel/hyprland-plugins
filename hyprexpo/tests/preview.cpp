#include "../WorkspacePreview.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

static void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

// Exercise the production preview guard without starting a compositor or GPU.
// The animation values deliberately differ from their goals during a transition.
template <typename T>
struct SAnimation {
    T    current{}, goal{}, begun{};
    bool animating = false;
    T&   value() {
        return current;
    }
};

struct SMonitor;
struct SWorkspace {
    SMonitor*                                          m_monitor;
    bool                                               m_visible            = false;
    bool                                               m_isSpecialWorkspace = false;
    std::unique_ptr<SAnimation<float>>                 m_alpha              = std::make_unique<SAnimation<float>>();
    std::unique_ptr<SAnimation<std::array<double, 2>>> m_renderOffset       = std::make_unique<SAnimation<std::array<double, 2>>>();
};

struct SMonitor {
    std::shared_ptr<SWorkspace> m_activeWorkspace, m_activeSpecialWorkspace;
};

static auto workspace(SMonitor& monitor, bool visible) {
    auto result              = std::make_shared<SWorkspace>();
    result->m_monitor        = &monitor;
    result->m_visible        = visible;
    result->m_alpha->current = visible ? 1.F : 0.F;
    return result;
}

int main() {
    SMonitor   firstMonitor, secondMonitor;
    const auto first                       = workspace(firstMonitor, true);
    const auto second                      = workspace(secondMonitor, true);
    const auto hidden                      = workspace(secondMonitor, false);
    const auto special                     = workspace(secondMonitor, true);
    special->m_isSpecialWorkspace          = true;
    firstMonitor.m_activeWorkspace         = first;
    secondMonitor.m_activeWorkspace        = second;
    secondMonitor.m_activeSpecialWorkspace = special;
    bool blockFeedback                     = false;

    hidden->m_alpha->current          = 0.25F;
    hidden->m_alpha->goal             = 0.F;
    hidden->m_alpha->begun            = 1.F;
    hidden->m_alpha->animating        = true;
    hidden->m_renderOffset->current   = {120, -30};
    hidden->m_renderOffset->goal      = {1920, 0};
    hidden->m_renderOffset->begun     = {0, 0};
    hidden->m_renderOffset->animating = true;
    second->m_alpha->current          = 0.7F;
    second->m_alpha->goal             = 1.F;
    second->m_alpha->begun            = 0.1F;
    second->m_alpha->animating        = true;
    second->m_renderOffset->current   = {40, -10};
    second->m_renderOffset->goal      = {0, 0};
    second->m_renderOffset->begun     = {80, -20};
    second->m_renderOffset->animating = true;

    auto checkFirstMonitor = [&] {
        check(firstMonitor.m_activeWorkspace == first, "second-monitor previews must not change the first monitor's workspace");
        check(first->m_visible && first->m_alpha->current == 1.F && first->m_renderOffset->current == std::array<double, 2>{},
              "second-monitor previews must not hide or move windows on the first monitor");
    };
    auto checkRestored = [&] {
        checkFirstMonitor();
        check(secondMonitor.m_activeWorkspace == second && secondMonitor.m_activeSpecialWorkspace == special, "preview must restore active and special workspaces");
        check(second->m_visible && !hidden->m_visible, "preview must restore both workspace visibility flags");
        check(second->m_alpha->current == 0.7F && second->m_renderOffset->current == std::array<double, 2>{40, -10}, "preview must restore the active workspace's animated values");
        check(second->m_alpha->goal == 1.F && second->m_alpha->begun == 0.1F && second->m_alpha->animating && second->m_renderOffset->goal == std::array<double, 2>{0, 0} &&
                  second->m_renderOffset->begun == std::array<double, 2>{80, -20} && second->m_renderOffset->animating,
              "preview must preserve the active workspace's animation progress");
        check(hidden->m_alpha->current == 0.25F && hidden->m_renderOffset->current == std::array<double, 2>{120, -30}, "preview must restore exact animated values");
        check(hidden->m_alpha->goal == 0.F && hidden->m_alpha->begun == 1.F && hidden->m_alpha->animating, "preview must preserve alpha animation progress");
        check(hidden->m_renderOffset->goal == std::array<double, 2>{1920, 0} && hidden->m_renderOffset->begun == std::array<double, 2>{0, 0} && hidden->m_renderOffset->animating,
              "preview must preserve workspace slide animation progress");
        check(!blockFeedback, "preview must restore surface feedback reporting");
    };

    // Reproduce first 1 on monitor two while workspace one belongs to monitor one.
    check(Preview::localAnchor(first, second, &secondMonitor) == second, "a foreign first/center anchor must fall back to the local active workspace");
    check(Preview::localAnchor(hidden, second, &secondMonitor) == hidden, "a local explicit anchor must be preserved");
    check(!Preview::localAnchor(std::shared_ptr<SWorkspace>{}, second, &secondMonitor), "unallocated workspace IDs remain available for local creation");
    {
        Preview::CWorkspacePreviewState preview(first, &secondMonitor, blockFeedback);
        check(!preview, "a foreign workspace must not be rendered on the second monitor");
        checkRestored();
    }
    checkRestored();

    secondMonitor.m_activeWorkspace = first;
    {
        Preview::CWorkspacePreviewState preview(hidden, &secondMonitor, blockFeedback);
        check(!preview, "an already shared active workspace must not be hidden by a local preview");
        checkFirstMonitor();
        check(!blockFeedback && !hidden->m_visible && secondMonitor.m_activeWorkspace == first, "rejecting a shared active workspace must leave state untouched");
    }
    secondMonitor.m_activeWorkspace = second;
    checkRestored();

    for (int redraw = 0; redraw < 3; ++redraw) {
        Preview::CWorkspacePreviewState preview(hidden, &secondMonitor, blockFeedback);
        check(static_cast<bool>(preview), "local workspace preview must succeed");
        check(secondMonitor.m_activeWorkspace == hidden && !second->m_visible && hidden->m_visible, "local preview must make only the thumbnail workspace visible");
        check(hidden->m_alpha->current == 1.F && hidden->m_renderOffset->current == std::array<double, 2>{}, "thumbnail must render opaque and without a slide offset");
        check(!secondMonitor.m_activeSpecialWorkspace && blockFeedback, "other tiles must hide the special workspace and suppress feedback");
        checkFirstMonitor();
    }
    checkRestored();

    {
        Preview::CWorkspacePreviewState preview(second, &secondMonitor, blockFeedback);
        check(secondMonitor.m_activeSpecialWorkspace == special, "the current workspace thumbnail must include its special workspace");
        {
            Preview::CWorkspacePreviewState nested(hidden, &secondMonitor, blockFeedback);
            check(secondMonitor.m_activeWorkspace == hidden, "nested preview must select its local workspace");
        }
        check(secondMonitor.m_activeWorkspace == second && second->m_visible && blockFeedback, "nested preview must restore its parent's state");
    }
    checkRestored();

    try {
        Preview::CWorkspacePreviewState preview(hidden, &secondMonitor, blockFeedback);
        throw std::runtime_error("render failure");
    } catch (const std::runtime_error&) {}
    checkRestored();

    {
        Preview::CWorkspacePreviewState preview(std::shared_ptr<SWorkspace>{}, &secondMonitor, blockFeedback);
        check(static_cast<bool>(preview) && !secondMonitor.m_activeSpecialWorkspace, "empty tiles must render without the current special workspace");
    }
    checkRestored();

    hidden->m_monitor = &firstMonitor;
    {
        Preview::CWorkspacePreviewState preview(hidden, &secondMonitor, blockFeedback);
        check(!preview, "a workspace moved to another monitor must be rejected before rendering");
    }
    hidden->m_monitor = &secondMonitor;
    checkRestored();
    {
        Preview::CWorkspacePreviewState preview(special, &secondMonitor, blockFeedback);
        check(!preview, "special workspaces must not become grid tiles");
    }
    checkRestored();
    std::cout << "Workspace preview regression checks passed\n";
}
