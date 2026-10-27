{ pkgs, desktop }:
let
  files = [
    "COPYING"
    "VERSION.cmake"
    "src/gui/accountstate.h"
    "src/gui/connectionvalidator.h"
    "src/gui/folder.cpp"
    "src/gui/folder.h"
    "src/gui/folderdefinition.h"
    "src/gui/folderman.h"
    "src/gui/folderwatcher.cpp"
    "src/gui/folderwatcher.h"
    "src/gui/folderwatcher_linux.cpp"
    "src/gui/folderwatcher_linux.h"
    "src/gui/scheduling/etagwatcher.h"
    "src/gui/scheduling/syncscheduler.h"
    "src/libsync/abstractcorejob.h"
    "src/libsync/abstractnetworkjob.cpp"
    "src/libsync/abstractnetworkjob.h"
    "src/libsync/accessmanager.cpp"
    "src/libsync/accessmanager.h"
    "src/libsync/account.cpp"
    "src/libsync/account.h"
    "src/libsync/accountfwd.h"
    "src/libsync/appprovider.cpp"
    "src/libsync/appprovider.h"
    "src/libsync/bandwidthmanager.h"
    "src/libsync/capabilities.cpp"
    "src/libsync/capabilities.h"
    "src/libsync/common/asserts.h"
    "src/libsync/common/checksumalgorithms.cpp"
    "src/libsync/common/checksumalgorithms.h"
    "src/libsync/common/checksums.h"
    "src/libsync/common/chronoelapsedtimer.cpp"
    "src/libsync/common/chronoelapsedtimer.h"
    "src/libsync/common/constexpr_list.h"
    "src/libsync/common/filesystembase.h"
    "src/libsync/common/ownsql.h"
    "src/libsync/common/pinstate.h"
    "src/libsync/common/preparedsqlquerymanager.h"
    "src/libsync/common/remotepermissions.h"
    "src/libsync/common/result.h"
    "src/libsync/common/syncfilestatus.h"
    "src/libsync/common/syncjournaldb.h"
    "src/libsync/common/syncjournalfilerecord.h"
    "src/libsync/common/utility.cpp"
    "src/libsync/common/utility.h"
    "src/libsync/common/version.cpp.in"
    "src/libsync/common/version.h"
    "src/libsync/configfile.h"
    "src/libsync/cookiejar.cpp"
    "src/libsync/cookiejar.h"
    "src/libsync/creds/abstractcredentials.cpp"
    "src/libsync/creds/abstractcredentials.h"
    "src/libsync/csync.h"
    "src/libsync/csync_exclude.h"
    "src/libsync/discovery.cpp"
    "src/libsync/discovery.h"
    "src/libsync/discoveryphase.h"
    "src/libsync/discoveryremoteinfo.h"
    "src/libsync/filesystem.h"
    "src/libsync/graphapi/space.h"
    "src/libsync/graphapi/spacesmanager.h"
    "src/libsync/httplogger.cpp"
    "src/libsync/httplogger.h"
    "src/libsync/jobqueue.cpp"
    "src/libsync/jobqueue.h"
    "src/libsync/localdiscoverytracker.cpp"
    "src/libsync/localdiscoverytracker.h"
    "src/libsync/localinfo.h"
    "src/libsync/networkjobs.h"
    "src/libsync/networkjobs/simplenetworkjob.h"
    "src/libsync/owncloudpropagator.cpp"
    "src/libsync/owncloudpropagator.h"
    "src/libsync/owncloudpropagator_p.h"
    "src/libsync/path.h"
    "src/libsync/progressdispatcher.h"
    "src/libsync/propagatedownload.h"
    "src/libsync/propagateremotedelete.h"
    "src/libsync/propagateremotemkdir.h"
    "src/libsync/propagateremotemove.h"
    "src/libsync/propagateupload.cpp"
    "src/libsync/propagateupload.h"
    "src/libsync/propagateuploadtus.cpp"
    "src/libsync/propagateuploadtus.h"
    "src/libsync/propagateuploadv1.cpp"
    "src/libsync/propagatorjobs.h"
    "src/libsync/syncengine.h"
    "src/libsync/syncfileitem.h"
    "src/libsync/syncfilestatustracker.h"
    "src/libsync/syncoptions.h"
    "src/libsync/syncresult.h"
    "src/libsync/vfs/vfs.h"
  ];
in
pkgs.stdenv.mkDerivation {
  passthru.filteredFiles = files;

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

  # One patch per upstream file, under nix/patches/ mirroring the source tree.
  # Each strips the GUI-only includes that file pulls in, so the UI never builds.
  patches = pkgs.lib.filesystem.listFilesRecursive ./patches;

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
