#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

// Render thumbnails through the original hook target without changing Hyprland access modifiers.
void renderWorkspacePreview(PHLMONITOR monitor, PHLWORKSPACE workspace, const CBox& geometry);
