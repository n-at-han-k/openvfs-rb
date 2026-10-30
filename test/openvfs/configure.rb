# frozen_string_literal: true

require "openvfs/occ"

require "fileutils"
require "tmpdir"

# The INI the engine reads on start. An account is a server and a user, so being signed in to one server twice, or to two servers as one user, is two accounts and two syncs - and the file is written whole, so it has to carry all of them at once.
describe OpenVFS do
  def with_home
    Dir.mktmpdir("openvfs-configure") do |root|
      home = File.join(root, "home")
      FileUtils.mkdir_p(home)
      was = ENV.fetch("HOME", nil)
      ENV["HOME"] = home

      begin
        yield(root)
      ensure
        ENV["HOME"] = was
      end
    end
  end

  def account(path, url, user) = { path: path, url: url, user: user, remote_path: "#{url}/dav/#{user}" }

  it "derives an account's id from the server and the user" do
    first = OpenVFS.account_uuid("https://one.example", "alice")

    expect(first).to be == OpenVFS.account_uuid("https://one.example", "alice")
    expect(first).not.to be == OpenVFS.account_uuid("https://one.example", "bob")
    expect(first).not.to be == OpenVFS.account_uuid("https://two.example", "alice")
    expect(first).to be(:match?, /\A\h{8}-\h{4}-5\h{3}-[89ab]\h{3}-\h{12}\z/)
  end

  it "writes one account and one folder per entry" do
    with_home do |root|
      uuids = OpenVFS.configure([
        account(File.join(root, "a"), "https://one.example", "alice"),
        account(File.join(root, "b"), "https://one.example", "bob"),
        account(File.join(root, "c"), "https://two.example", "alice"),
      ])

      expect(uuids.size).to be == 3
      expect(uuids.uniq.size).to be == 3

      config = File.read(OpenVFS.config_path)

      expect(config).to be(:include?, "size=3")
      expect(config).to be(:include?, "1\\display-name=alice")
      expect(config).to be(:include?, "2\\display-name=bob")
      expect(config).to be(:include?, "3\\url=https://two.example")
      expect(config).to be(:include?, "1\\accountUUID=#{uuids[0]}")
      expect(config).to be(:include?, "3\\accountUUID=#{uuids[2]}")
    end
  end

  it "makes the local directory of each folder" do
    with_home do |root|
      OpenVFS.configure([account(File.join(root, "made"), "https://one.example", "alice")])

      expect(File.directory?(File.join(root, "made"))).to be == true
    end
  end

  # The engine reads the path from Dir.home, so a caller cannot choose it.
  it "writes where the engine looks" do
    with_home do
      expect(OpenVFS.config_path).to be == File.join(ENV.fetch("HOME"), ".config", "openvfs-rb", "openvfs-rb.cfg")
    end
  end
end
