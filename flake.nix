{
  description = "HyprExpo overview and workspace swipe plugin";

  inputs.hyprland.url = "github:hyprwm/Hyprland/v0.56.2";

  outputs = { self, hyprland }: let
    systems = [ "x86_64-linux" "aarch64-linux" ];
    eachSystem = hyprland.inputs.nixpkgs.lib.genAttrs systems;
    pkgsFor = system: import hyprland.inputs.nixpkgs { inherit system; };
  in {
    packages = eachSystem (system: let
      pkgs = pkgsFor system;
      plugin = pkgs.callPackage ./hyprexpo {
        hyprland = hyprland.packages.${system}.hyprland;
      };
    in {
      hyprexpo = plugin;
      default = plugin;
    });

    checks = eachSystem (system: {
      hyprexpo = self.packages.${system}.hyprexpo;
    });
  };
}
