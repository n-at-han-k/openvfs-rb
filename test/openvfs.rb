# frozen_string_literal: true

# Released under the MIT License.
# Copyright, 2026, by Nathan Kidd.

require "openvfs"

describe OpenVFS do
  it "has a version" do
    expect(OpenVFS::VERSION).to be =~ /\A\d+\.\d+\.\d+\z/
  end
end
