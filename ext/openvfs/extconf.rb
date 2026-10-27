# frozen_string_literal: true

require "mkmf-rice"

prefix = ENV.fetch("OPENVFS_PREFIX") do
  abort "OPENVFS_PREFIX must point to the OpenVFS installation prefix"
end

openvfs_include_dir = File.join(prefix, "include")
openvfs_library_dir = File.join(prefix, "lib")

dir_config("openvfs", openvfs_include_dir, openvfs_library_dir)
$CXXFLAGS << " -std=c++23 -Wno-array-bounds -ftemplate-backtrace-limit=0"
$LDFLAGS << " -Wl,-rpath,#{openvfs_library_dir}"

unless have_header("openvfs/openvfs.h")
  abort "missing OpenVFS headers"
end

unless have_library("openvfs")
  abort "missing libopenvfs"
end

create_makefile "openvfs/openvfs"
