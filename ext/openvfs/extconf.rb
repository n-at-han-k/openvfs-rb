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

# mkmf puts LIBRUBYARG_SHARED on the link line whenever Ruby was built --enable-shared, and nixpkgs configures Ruby with --with-soname=ruby-<full version>, so an object compiled in `nix develop` asks the loader for libruby-4.0.9.so.4.0 - a soname only a nixpkgs Ruby of that exact patch release carries, and one no other Ruby provides under any name. A precompiled object has to load on every patch release of the ABI it was built for, so resolve Ruby's symbols from the process which dlopens it instead; that is what a rake-compiler gem gets for free by building against a --disable-shared Ruby, whose LIBRUBYARG is empty. Windows keeps the link because mingw resolves every symbol at link time, and its import library is named for the ABI rather than the patch release.
unless RbConfig::CONFIG["host_os"].match?(/mswin|mingw/)
  $LIBRUBYARG = ""
end

# A mach-o bundle which links no libruby has to leave its Ruby symbols for the loader to find in the process which opens it. Ruby's own configure probes the flag into ADDITIONAL_DLDFLAGS and mkmf appends that for a gem build, so this only covers a Ruby configured without it.
if RbConfig::CONFIG["host_os"].match?(/darwin/)
  $DLDFLAGS << " -Wl,-undefined,dynamic_lookup"
end

unless have_header("openvfs/openvfs.h")
  abort "missing OpenVFS headers"
end

unless have_library("openvfs")
  abort "missing libopenvfs"
end

create_makefile "openvfs/openvfs"
