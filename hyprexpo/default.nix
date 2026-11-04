{
  lib,
  hyprland,
  hyprlandPlugins,
}:
(hyprlandPlugins.override { inherit hyprland; }).mkHyprlandPlugin {
  pluginName = "hyprexpo";
  version = "0.1";
  src = ./.;

  inherit (hyprland) nativeBuildInputs;
  doCheck = true;

  meta = with lib; {
    homepage = "https://github.com/hyprwm/hyprland-plugins/tree/main/hyprexpo";
    description = "Hyprland workspaces overview plugin";
    license = licenses.bsd3;
    platforms = platforms.linux;
  };
}
