# frozen_string_literal: true

# Released under the MIT License.
# Copyright, 2026, by Nathan Kidd.

require_relative "openvfs/version"

# A precompiled platform gem carries one object per Ruby ABI, because an object only loads on the Ruby it was compiled against. The source gem has no ABI directory - extconf builds one object, under ext/ - so the fallback is the plain require the load path answers.
begin
  require_relative "openvfs/#{RUBY_VERSION[/\d+\.\d+/]}/openvfs"
rescue LoadError
  require "openvfs/openvfs"
end

# A virtual filesystem.
module OpenVFS
end
