{ pkgs, desktop }:
pkgs.stdenv.mkDerivation {
  pname = "opencloud-upload";
  version = "4.0.1";

  src = desktop;

  # One patch per upstream file, under nix/patches/ mirroring the source tree.
  # Each strips the GUI-only includes that file pulls in, so the UI never builds.
  patches = pkgs.lib.filesystem.listFilesRecursive ./patches;

  nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
  buildInputs = [ pkgs.qt6.qtbase pkgs.sqlite pkgs.kdePackages.qtkeychain ];
  dontWrapQtApps = true;
  strictDeps = true;

  cmakeFlags = [ "-DGIT_SHA1=${desktop.rev}" ];

  meta = {
    description = "OpenCloud Desktop's account and streaming PUT upload jobs";
    license = pkgs.lib.licenses.gpl2Plus;
    platforms = pkgs.lib.platforms.linux;
  };
}
