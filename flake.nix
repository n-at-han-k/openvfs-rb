{
  description = "openvfs-rb — Ruby gem";

  inputs = {
    nixpkgs.url = "nixpkgs";
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
  };
  outputs = { self, mine, nixpkgs, utils, openvfs, desktop }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        lib = mine.lib.${system};

        desktopUpload = import ./nix/desktop-upload.nix { inherit pkgs desktop; };

        openvfsLibrary = pkgs.stdenv.mkDerivation {
          pname = "openvfs";
          version = "0.1.0";
          src = openvfs;
          buildInputs = [ pkgs.nlohmann_json ];

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
              -o "$out/lib/libopenvfs.so"
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            cp "$src/src/openvfs/openvfs.h" "$out/include/openvfs/"
            cp "$src/src/openvfs/openvfconstants.h" "$out/include/openvfs/"
            cp generated/openvfs/version.h generated/openvfs/openvfs_export.h "$out/include/openvfs/"
            runHook postInstall
          '';
        };

        # bundlerEnv evaluates the gemspec, so it needs the gemspec and the version it reads.
        # The gemspec parses lib/openvfs/version.rb rather than requiring it, so only the
        # gemspec itself is copied and no second copy of the library reaches the load path.
        gemspecFiles = pkgs.runCommand "openvfs-rb-gemspec" { } ''
          mkdir -p $out/lib/openvfs
          cp ${./openvfs-rb.gemspec}      $out/openvfs-rb.gemspec
          cp ${./lib/openvfs/version.rb}  $out/lib/openvfs/version.rb
        '';

        gems = pkgs.bundlerEnv {
          name = "openvfs-rb-gems";
          ruby = pkgs.ruby_3_4;
          gemfile = ./Gemfile;
          lockfile = ./Gemfile.lock;
          gemset = ./gemset.nix;
          extraConfigPaths = [ "${gemspecFiles}/." ];
        };

      in
      {
        packages.desktop-upload = desktopUpload;

        devShells.default = lib.mkRubyShell {
          buildInputs = with pkgs; [
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
      }
    );
}
