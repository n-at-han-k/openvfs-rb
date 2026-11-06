{
  description = "openvfs-rb — Ruby gem";

  inputs = {
    # 26.05 rather than unstable: 26.11 dropped x86_64-darwin, and evaluating it for
    # that system throws outright. This is the last branch that still builds every
    # platform the gem ships, so all of them use it and there is one tree to reason
    # about. It is security-fix-only from the end of 2026.
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-26.05-darwin";
    utils.url = "github:numtide/flake-utils";
    mine.url = "github:n-at-han-k/flake.nix";
    mine.inputs.nixpkgs.follows = "nixpkgs";
    openvfs = {
      url = "github:opencloud-eu/openvfs";
      flake = false;
    };
    desktop = {
      url = "github:opencloud-eu/desktop";
      flake = false;
    };
    libregraph = {
      url = "github:opencloud-eu/libre-graph-api-cpp-qt-client/v1.0.8";
      flake = false;
    };
  };
  outputs = { self, mine, nixpkgs, utils, openvfs, desktop, libregraph }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        lib = mine.lib.${system};

        # Desktop 4.0.1 requires LibreGraphAPI 1.0.8; nixpkgs only packages 1.0.4,
        # so overlay the nixpkgs package with the pinned source. sourceRoot has to be
        # overridden too because the original derives it from the fetchFromGitHub name.
        libreGraphApi = pkgs.libre-graph-api-cpp-qt-client.overrideAttrs (old: {
          version = "1.0.8";
          src = libregraph;
          sourceRoot = "source/client";
          postPatch = ''
            substituteInPlace CMakeLists.txt \
              --replace-fail "cmake_minimum_required(VERSION 3.5)" "cmake_minimum_required(VERSION 3.10)"
          '';
        });

        desktopUpload = pkgs.stdenv.mkDerivation {
          pname = "opencloud-upload";
          version = "4.0.1";

          src = desktop;

          # One patch per upstream file, under patches/ mirroring the source tree.
          # Each strips the GUI-only includes that file pulls in, so the UI never builds.
          patches = pkgs.lib.filesystem.listFilesRecursive ./patches;

          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          # ECM is a buildInput, not a nativeBuildInput: cmake's setup hook only adds
          # targetOffset inputs to CMAKE_PREFIX_PATH, so a native one is never found
          # by find_package(ECM).
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.sqlite
            pkgs.zlib
            pkgs.kdePackages.qtkeychain
            pkgs.kdePackages.extra-cmake-modules
            libreGraphApi
            # The vfs/openvfs plugin is built into libsync, so it links the library.
            openvfsLibrary
          ];
          dontWrapQtApps = true;
          strictDeps = true;

          # OPENVFS_EXE is baked as a string literal into the sync plugin, so
          # an absolute /nix/store path there would end up in every shipped
          # gem. "openvfs" resolves through the loader's PATH search instead,
          # which is what the plugin already falls back to when the baked
          # path does not exist. -ffile-prefix-map rewrites __FILE__ in
          # macros like Q_ASSERT so Qt's own header paths do not reach the
          # output either.
          cmakeFlags = [
            "-DGIT_SHA1=${desktop.rev}"
            "-DOPENVFS_EXECUTABLE=openvfs"
            "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=/nix/store=/__nix"
            "-DCMAKE_C_FLAGS=-ffile-prefix-map=/nix/store=/__nix"
          ];

          meta = {
            description = "OpenCloud Desktop's account and streaming PUT upload jobs";
            license = pkgs.lib.licenses.gpl2Plus;
            platforms = pkgs.lib.platforms.linux ++ pkgs.lib.platforms.darwin;
          };
        };

        openvfsLibrary = pkgs.stdenv.mkDerivation {
          pname = "openvfs";
          version = "0.1.0";
          src = openvfs;
          # fuse3 and pthreads are the fuse binary's, not the library's. On
          # darwin fuse3 is macFUSE's stubs, which carry the headers and
          # something to link against; the kernel extension is the user's to
          # install.
          nativeBuildInputs = [ pkgs.pkg-config ];
          buildInputs = [ pkgs.nlohmann_json pkgs.fuse3 ];

          meta.platforms = pkgs.lib.platforms.linux ++ pkgs.lib.platforms.darwin;

          buildPhase = ''
            runHook preBuild
            mkdir -p generated/openvfs "$out/include/openvfs" "$out/lib"
            cat > generated/openvfs/version.h <<'EOF'
            #pragma once
            #define OPENVFS_VERSION_MAJOR 0
            EOF
            cat > generated/openvfs/openvfs_export.h <<'EOF'
            #pragma once
            #define OPENVFS_EXPORT
            EOF
            $CXX -std=c++23 -fPIC -shared -D_FILE_OFFSET_BITS=64 \
              -I"$src/src" -Igenerated -I"${pkgs.nlohmann_json}/include" \
              "$src/src/openvfs/openvfs.cpp" "$src/src/openvfs/xattr.cpp" \
              -o "$out/lib/libopenvfs${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}"

            # The fuse binary, which is what actually mounts the layer. The vfs
            # plugin spawns it by the path baked into OPENVFS_EXE, so the
            # library on its own leaves the plugin with nothing to run. Named
            # openvfs, which is this target's OUTPUT_NAME upstream.
            mkdir -p "$out/bin" "$out/etc/openvfs"
            # Both defines are openvfsfuse's own CMakeLists': with macFUSE's
            # darwin extensions on, its operations take fuse_darwin_attr and a
            # statfs where this code passes stat and statvfs.
            $CXX -std=c++23 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=30 \
              -DFUSE_DARWIN_ENABLE_EXTENSIONS=0 \
              -I"$src/src" -Igenerated -I"${pkgs.nlohmann_json}/include" \
              $(pkg-config --cflags fuse3) \
              "$src/src/openvfsfuse/main.cpp" "$src/src/openvfsfuse/openvfsfuse.cpp" \
              "$src/src/openvfsfuse/socketthread.cpp" "$src/src/openvfsfuse/sharedmap.cpp" \
              "$src/src/openvfsfuse/strtools.cpp" \
              -L"$out/lib" -lopenvfs -Wl,-rpath,"$out/lib" \
              $(pkg-config --libs fuse3) -lpthread \
              -o "$out/bin/openvfs"
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            cp "$src/src/openvfs/openvfs.h" "$out/include/openvfs/"
            cp "$src/src/openvfs/openvfconstants.h" "$out/include/openvfs/"
            cp generated/openvfs/version.h generated/openvfs/openvfs_export.h "$out/include/openvfs/"
            # The ignore list the fuse layer reads, so an indexer or thumbnailer does not hydrate the whole sync root.
            cp "$src/src/openvfsfuse/config.json" "$out/etc/openvfs/"
            runHook postInstall
          '';
        };

        # bundlerEnv evaluates the gemspec, so it needs the gemspec and the version it reads.
        # The gemspec parses lib/openvfs/version.rb rather than requiring it, so only the
        # gemspec itself is copied and no second copy of the library reaches the load path.
        gemspecFiles = pkgs.runCommand "openvfs-rb-gemspec" { } ''
          mkdir -p $out/lib/openvfs/rb
          cp ${./openvfs-rb.gemspec}            $out/openvfs-rb.gemspec
          cp ${./lib/openvfs/rb/version.rb}     $out/lib/openvfs/rb/version.rb
          cp ${./lib/openvfs/version.rb}        $out/lib/openvfs/version.rb
        '';

        gemsFor = ruby: pkgs.bundlerEnv {
          name = "openvfs-rb-gems";
          inherit ruby;
          gemfile = ./Gemfile;
          lockfile = ./Gemfile.lock;
          gemset = ./gemset.nix;
          extraConfigPaths = [ "${gemspecFiles}/." ];
        };

        # A compiled object only loads on the Ruby ABI it was built against, so a precompiled
        # platform gem carries one per ABI and lib/openvfs.rb picks by RUBY_VERSION. Cross
        # Compile builds each of these, which is why there is a shell per Ruby. The set has to
        # match RUBY_ABIS in bin/package-platform-gem. The shell drops the dot from its name -
        # `nix develop .#ruby-33` - because nix splits an installable's attribute path on dots.
        rubies = {
          "33" = pkgs.ruby_3_3;
          "34" = pkgs.ruby_3_4;
          "40" = pkgs.ruby_4_0;
        };

        shellFor = ruby:
          let
            gems = gemsFor ruby;
          in
          lib.mkRubyShell {
            buildInputs = [
              gems
              gems.wrappedRuby
              openvfsLibrary
              desktopUpload
              pkgs.qt6.qtbase
              pkgs.pkg-config
              pkgs.gcc
              pkgs.nlohmann_json
            ];

            shellHook = /* bash */ ''
              export BUNDLE_FORCE_RUBY_PLATFORM=true
              export BUNDLE_GEMFILE="$PWD/Gemfile"
              export BUNDLE_FROZEN=false

              export LANG="''${LANG:-C.UTF-8}"
              export LC_ALL="''${LC_ALL:-$LANG}"
              export OPENVFS_PREFIX="${openvfsLibrary}"
              export OPENCLOUD_UPLOAD_PREFIX="${desktopUpload}"

              # This gem is not in the bundle — it is the repository — so put this lib/ and
              # fixtures/ on the load path and let `require "openvfs"` find the working tree.
              export RUBYLIB="$PWD/lib:$PWD/ext:$PWD/fixtures''${RUBYLIB:+:$RUBYLIB}"
            '';
          };

        gems = gemsFor pkgs.ruby_3_4;

      in
      {
        # Everything here is built from source on every runner: none of these are in
        # cache.nixos.org, so CI's binary cache step copies exactly these closures.
        packages.desktop-upload = desktopUpload;
        packages.openvfs = openvfsLibrary;
        packages.libre-graph-api = libreGraphApi;
        packages.gems = gems;
        packages.wrapped-ruby = gems.wrappedRuby;

        devShells = pkgs.lib.mapAttrs' (abi: ruby:
          pkgs.lib.nameValuePair "ruby-${abi}" (shellFor ruby)
        ) rubies // {
          default = shellFor pkgs.ruby_3_4;
        };
      }
    );
}
