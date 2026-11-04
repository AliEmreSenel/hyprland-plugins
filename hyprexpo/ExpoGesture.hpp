#pragma once

#include "overview.hpp"
#include <hyprland/src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>

class CExpoGesture : public ITrackpadGesture {
  public:
    explicit CExpoGesture(EOverviewMode mode = EOverviewMode::EXPO) : m_mode(mode) {}
    ~CExpoGesture() override;

    void        begin(const STrackpadGestureBegin& e) override;
    void        update(const STrackpadGestureUpdate& e) override;
    void        end(const STrackpadGestureEnd& e) override;

    static bool isActive();

  private:
    EOverviewMode               m_mode;
    bool                        m_accepted = false;
    inline static CExpoGesture* s_active   = nullptr;
};
