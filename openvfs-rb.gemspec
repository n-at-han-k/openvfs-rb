# frozen_string_literal: true

# Released under the MIT License.
# Copyright, 2026, by Nathan Kidd.

version = File.read(File.expand_path("lib/openvfs/rb/version.rb", __dir__))[/VERSION = "([^"]+)"/, 1]

Gem::Specification.new do |spec|
  spec.name = "openvfs-rb"
  spec.version = version
  spec.authors = ["Nathan Kidd"]
  spec.email = ["nathankidd@hey.com"]
  spec.license = "MIT"

  spec.summary = "A virtual filesystem for Ruby."
  spec.homepage = "https://github.com/n-at-han-k/openvfs-rb"

  spec.required_ruby_version = ">= 3.3"

  spec.metadata["source_code_uri"] = spec.homepage

  spec.files = Dir.glob(
    ["ext/**/*.{rb,cpp,h,hpp}", "lib/**/*", "license.md", "README.md"],
    File::FNM_DOTMATCH,
    base: __dir__,
  )

  spec.require_paths = ["lib"]
  spec.extensions = ["ext/openvfs/extconf.rb", "ext/openvfs/occ/extconf.rb"]

  spec.add_dependency "gem_kit", "~> 0.2"
  spec.add_dependency "rice", "~> 4.0"
end
