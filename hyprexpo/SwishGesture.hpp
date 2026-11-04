#pragma once

#include "ExpoGesture.hpp"

class CSwishGesture : public CExpoGesture {
  public:
    CSwishGesture() : CExpoGesture(EOverviewMode::SWISH) {}
};
