{
  description = "ExtremeTuxRacer - High speed arctic racing game based on Tux Racer";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "extremetuxracer";
          version = "0.8.4";
          src = ./.;
          nativeBuildInputs = [
            pkgs.pkg-config
            pkgs.intltool
          ];
          buildInputs = [
            pkgs.libGLU
            pkgs.libGL
            pkgs.xorg.libX11
            pkgs.xorg.xorgproto
            pkgs.tcl
            pkgs.libglut
            pkgs.freetype
            pkgs.sfml_2
            pkgs.xorg.libXi
            pkgs.xorg.libXmu
            pkgs.xorg.libXext
            pkgs.xorg.libXt
            pkgs.xorg.libSM
            pkgs.xorg.libICE
            pkgs.libpng
            pkgs.gettext
          ];
          configureFlags = [ "--with-tcl=${pkgs.tcl}/lib" ];
          preConfigure = ''
            export NIX_CFLAGS_COMPILE="$NIX_CFLAGS_COMPILE"
          '';
          meta = with pkgs.lib; {
            description = "High speed arctic racing game based on Tux Racer";
            longDescription = "ExtremeTuxRacer - Tux lies on his belly and accelerates down ice slopes.";
            license = licenses.gpl2Plus;
            homepage = "https://sourceforge.net/projects/extremetuxracer/";
            platforms = platforms.linux;
            mainProgram = "etr";
          };
        };
      }
    );
}
