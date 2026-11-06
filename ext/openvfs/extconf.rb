# frozen_string_literal: true

require "mkmf-rice"

prefix = ENV.fetch("OPENVFS_PREFIX") do
  abort "OPENVFS_PREFIX must point to the OpenVFS installation prefix"
end

openvfs_include_dir = File.join(prefix, "include")
openvfs_library_dir = File.join(prefix, "lib")

dir_config("openvfs", openvfs_include_dir, openvfs_library_dir)
$CXXFLAGS << " -std=c++23 -Wno-array-bounds -ftemplate-backtrace-limit=0"
# __FILE__ expands to the source path at the point a macro like Q_ASSERT or Q_D is reached, and in a nix build that path is under /nix/store, so the shipped object would otherwise name nix store paths through every assertion message. -ffile-prefix-map rewrites it.
$CXXFLAGS << " -ffile-prefix-map=/nix/store=/__nix"
$LDFLAGS << " -Wl,-rpath,#{openvfs_library_dir}"

unless have_header("openvfs/openvfs.h")
  abort "missing OpenVFS headers"
end

unless have_library("openvfs")
  abort "missing libopenvfs"
end

create_makefile "openvfs/openvfs"
