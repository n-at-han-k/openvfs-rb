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

# The static library is built with -ffunction-sections, so the link can drop every function the extension never calls. ld64 spells that -dead_strip and has no equivalent of --exclude-libs.
if RbConfig::CONFIG["host_os"].match?(/darwin/)
  $LDFLAGS << " -Wl,-dead_strip"
else
  $LDFLAGS << " -Wl,--gc-sections -Wl,--exclude-libs,ALL"
end

# opencloudsynclib.h rather than a real header: mkmf puts ruby.h first in the program it
# compiles to probe, and on Windows that defines truncate and access as macros, which Qt
# refuses to be parsed after. The generated export header pulls in nothing, so it answers
# the only question being asked - whether the development files are installed.
unless have_header("libsync/opencloudsynclib.h")
  abort "missing OpenCloud Desktop upload headers"
end

create_makefile "openvfs/occ/occ"
