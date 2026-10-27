{ pkgs, desktop }:
let
  files = [
    "COPYING"
    "VERSION.cmake"
    "src/libsync/abstractnetworkjob.cpp"
    "src/libsync/abstractnetworkjob.h"
    "src/libsync/accessmanager.cpp"
    "src/libsync/accessmanager.h"
    "src/libsync/account.cpp"
    "src/libsync/account.h"
    "src/libsync/accountfwd.h"
    "src/libsync/appprovider.cpp"
    "src/libsync/appprovider.h"
    "src/libsync/capabilities.cpp"
    "src/libsync/capabilities.h"
    "src/libsync/cookiejar.cpp"
    "src/libsync/cookiejar.h"
    "src/libsync/csync.h"
    "src/libsync/filesystem.h"
    "src/libsync/httplogger.cpp"
    "src/libsync/httplogger.h"
    "src/libsync/jobqueue.cpp"
    "src/libsync/jobqueue.h"
    "src/libsync/propagateupload.cpp"
    "src/libsync/propagateupload.h"
    "src/libsync/creds/abstractcredentials.cpp"
    "src/libsync/creds/abstractcredentials.h"
    "src/libsync/common/asserts.h"
    "src/libsync/common/checksumalgorithms.cpp"
    "src/libsync/common/checksumalgorithms.h"
    "src/libsync/common/chronoelapsedtimer.cpp"
    "src/libsync/common/chronoelapsedtimer.h"
    "src/libsync/common/constexpr_list.h"
    "src/libsync/common/filesystembase.h"
    "src/libsync/common/result.h"
    "src/libsync/common/utility.cpp"
    "src/libsync/common/utility.h"
    "src/libsync/common/version.cpp.in"
    "src/libsync/common/version.h"
  ];
in
pkgs.stdenv.mkDerivation {
  pname = "opencloud-upload";
  version = "4.0.1";

  src = pkgs.runCommand "opencloud-upload-source" { } ''
    mkdir -p "$out"
    cd ${desktop}
    for file in ${pkgs.lib.escapeShellArgs files}; do
      mkdir -p "$out/$(dirname "$file")"
      cp "$file" "$out/$file"
    done
  '';

  patches = [ ./desktop-upload.patch ];

  nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
  buildInputs = [ pkgs.qt6.qtbase ];
  dontWrapQtApps = true;
  strictDeps = true;

  postPatch = ''
    cp ${./desktop-upload.CMakeLists.txt} CMakeLists.txt
    cp ${./opencloud-upload.pc.in} opencloud-upload.pc.in
  '';

  cmakeFlags = [ "-DGIT_SHA1=${desktop.rev}" ];

  meta = {
    description = "OpenCloud Desktop's account and streaming PUT upload jobs";
    license = pkgs.lib.licenses.gpl2Plus;
    platforms = pkgs.lib.platforms.linux;
  };
}
