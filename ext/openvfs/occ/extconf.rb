# frozen_string_literal: true

require "mkmf-rice"

if ENV.key?("OPENCLOUD_UPLOAD_PREFIX")
  pkg_config_path = File.join(ENV.fetch("OPENCLOUD_UPLOAD_PREFIX"), "lib", "pkgconfig")
  ENV["PKG_CONFIG_PATH"] = [pkg_config_path, ENV["PKG_CONFIG_PATH"]].compact.join(File::PATH_SEPARATOR)
end

unless pkg_config("opencloud-upload")
  abort "missing opencloud-upload development files; enter nix develop or set OPENCLOUD_UPLOAD_PREFIX"
end

$CXXFLAGS << " -std=c++23 -fPIC -Wno-array-bounds"
# __FILE__ expands to the source path at the point a macro like Q_ASSERT or Q_D is reached, and in a nix build that path is under /nix/store, so the shipped object would otherwise name nix store paths through every assertion message. -ffile-prefix-map rewrites it.
$CXXFLAGS << " -ffile-prefix-map=/nix/store=/__nix"

# The static library is built with -ffunction-sections, so the link can drop every function the extension never calls. ld64 spells that -dead_strip and has no equivalent of --exclude-libs.
if RbConfig::CONFIG["host_os"].match?(/darwin/)
  $LDFLAGS << " -Wl,-dead_strip"
else
  $LDFLAGS << " -Wl,--gc-sections -Wl,--exclude-libs,ALL"
end

# mkmf puts LIBRUBYARG_SHARED on the link line whenever Ruby was built --enable-shared, and nixpkgs configures Ruby with --with-soname=ruby-<full version>, so an object compiled in `nix develop` asks the loader for libruby-4.0.9.so.4.0 - a soname only a nixpkgs Ruby of that exact patch release carries, and one no other Ruby provides under any name. A precompiled object has to load on every patch release of the ABI it was built for, so resolve Ruby's symbols from the process which dlopens it instead; that is what a rake-compiler gem gets for free by building against a --disable-shared Ruby, whose LIBRUBYARG is empty. Windows keeps the link because mingw resolves every symbol at link time, and its import library is named for the ABI rather than the patch release.
unless RbConfig::CONFIG["host_os"].match?(/mswin|mingw/)
  $LIBRUBYARG = ""
end

# A mach-o bundle which links no libruby has to leave its Ruby symbols for the loader to find in the process which opens it. Ruby's own configure probes the flag into ADDITIONAL_DLDFLAGS and mkmf appends that for a gem build, so this only covers a Ruby configured without it.
if RbConfig::CONFIG["host_os"].match?(/darwin/)
  $DLDFLAGS << " -Wl,-undefined,dynamic_lookup"
end

# opencloudsynclib.h rather than a real header: mkmf puts ruby.h first in the program it compiles to probe, and on Windows that defines truncate and access as macros, which Qt refuses to be parsed after. The generated export header pulls in nothing, so it answers the only question being asked - whether the development files are installed.
unless have_header("libsync/opencloudsynclib.h")
  abort "missing OpenCloud Desktop upload headers"
end

create_makefile "openvfs/occ/occ"
