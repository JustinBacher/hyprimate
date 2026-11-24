{
  description = "Hyprimate animation plugin for Hyprland";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland/v0.57.2";
    nixpkgs.follows = "hyprland/nixpkgs";
  };

  outputs = { hyprland, nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      host = hyprland.packages.${system}.hyprland;
      hyprlandElf = "${host}/bin/.Hyprland-wrapped";
      hyprimate = host.stdenv.mkDerivation {
        pname = "hyprimate";
        version = "0.1.0";
        src = ./.;
        strictDeps = true;

        nativeBuildInputs = [
          pkgs.cmake
          pkgs.ninja
          pkgs.pkg-config
          pkgs.binutils
          (pkgs.lib.getBin pkgs.glibc)
        ];
        buildInputs = [
          host
          pkgs.libpng
          pkgs.nlohmann_json
          pkgs.libGL
          pkgs.lua5_5
        ] ++ host.buildInputs;

        cmakeBuildType = "RelWithDebInfo";
        cmakeFlags = [
          "-DBUILD_TESTING=OFF"
          # The checker needs the compositor ELF, not its launcher.
          "-DHYPRLAND_EXECUTABLE=${hyprlandElf}"
          "-DHYPRIMATE_SPRITE_DIR=${builtins.placeholder "out"}/share/hyprimate/sprites"
        ];
        ninjaFlags = [ "hyprimate" ];

        installPhase = ''
          runHook preInstall
          install -Dm755 hyprimate.so "$out/lib/hyprimate.so"
          mkdir -p "$out/share/hyprimate/sprites"
          install -Dm644 -t "$out/share/hyprimate/sprites" \
            ../src/window/swarm/sprites/{bird,cat,spider}.png
          runHook postInstall
        '';
      };
    in
    {
      packages.${system} = {
        inherit hyprimate;
        default = hyprimate;
      };

      devShells.${system}.default = (pkgs.mkShell.override { stdenv = host.stdenv; }) {
        inputsFrom = [ hyprimate ];
        packages = [ host pkgs.jq pkgs.python3 ];
        HYPRIMATE_HYPRLAND_ELF = hyprlandElf;
        HYPRIMATE_HYPRLAND_BIN = "${host}/bin";
      };
    };
}
