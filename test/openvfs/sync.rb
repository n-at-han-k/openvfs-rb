# frozen_string_literal: true

require "openvfs/occ"
require "json"
require "net/http"
require "securerandom"
require "time"
require "tmpdir"
require "uri"

# Watches a local directory and checks that a file dropped into it reaches the server.
#
# This is the whole point of the OCC bindings, so it is also the specification of what
# nix/desktop-upload.nix has to build: whatever this test needs is in scope, and whatever
# it does not need gets patched out.
describe OpenVFS::OCC do
  SERVER = ENV.fetch("OPENCLOUD_URL", "http://127.0.0.1:18280")
  USER = ENV.fetch("OPENCLOUD_USER", "admin")
  PASSWORD = ENV.fetch("OPENCLOUD_PASSWORD", "openvfs-rb-test-admin")

  def server_up?
    uri = URI.parse("#{SERVER}/status.php")
    Net::HTTP.start(uri.host, uri.port, open_timeout: 2, read_timeout: 2) do |http|
      http.get(uri.request_uri).is_a?(Net::HTTPSuccess)
    end
  rescue StandardError
    false
  end

  # The personal space's WebDAV root for the authenticated user.
  def dav_root = "#{SERVER}/remote.php/dav/files/#{USER}"

  def remote_get(path)
    uri = URI.parse("#{dav_root}/#{path}")
    Net::HTTP.start(uri.host, uri.port) do |http|
      request = Net::HTTP::Get.new(uri.request_uri)
      request.basic_auth(USER, PASSWORD)
      http.request(request)
    end
  end

  def basic_auth = "Basic " + ["#{USER}:#{PASSWORD}"].pack("m0")

  def server_capabilities
    uri = URI.parse("#{SERVER}/ocs/v2.php/cloud/capabilities?format=json")
    request = Net::HTTP::Get.new(uri.request_uri)
    request["Authorization"] = basic_auth
    body = Net::HTTP.start(uri.host, uri.port) { |http| http.request(request) }.body
    JSON.parse(body).dig("ocs", "data", "capabilities")
  end

  # Run the Qt event loop until the block is true, or give up. processEvents is not
  # enough: network replies only complete under a real loop.
  def wait_until(timeout: 60)
    deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
    until yield
      if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
        return false
      end

      OpenVFS::OCC.process_events(100)
    end
    true
  end

  it "uploads a file that appears in a watched directory" do
    skip "no OpenCloud server at #{SERVER}; run docker compose up -d" unless server_up?

    OpenVFS::OCC::Logger.instance.install

    Dir.mktmpdir("openvfs-sync") do |local|
      uuid = SecureRandom.uuid
      account = OpenVFS::OCC::AccountManager.create_account(uuid)
      account.url = SERVER
      account.credentials = OpenVFS::OCC::TokenCredentials.new(USER, PASSWORD)
      account_state = OpenVFS::OCC::AccountManager.instance.add_account(account)

      # AccountState will not report Connected until the account has capabilities, and
      # with no cached config to restore from, this is the job that fetches them.
      OpenVFS::OCC::FetchServerSettingsJob.new(account).start
      expect(wait_until { account.has_capabilities? }).to be == true

      account_state.check_connectivity(false)
      expect(wait_until { account_state.connected? }).to be == true

      definition = OpenVFS::OCC::FolderDefinition.new(uuid, dav_root, "", "openvfs-rb test")
      definition.local_path = local

      folder = OpenVFS::OCC::FolderMan.instance.add_folder(account_state, definition)
      expect(folder).not.to be_nil

      # Nothing syncs until the scheduler is running; it owns the sync queue.
      OpenVFS::OCC::FolderMan.instance.scheduler.start
      expect(wait_until { folder.ready? && folder.can_sync? }).to be == true

      # The watcher is what makes this interesting: nothing calls start_sync by hand.
      name = "watched-#{SecureRandom.hex(4)}.txt"
      body = "openvfs-rb #{Time.now.utc.iso8601}"
      staged = File.join(Dir.tmpdir, name)
      File.write(staged, body)
      File.rename(staged, File.join(local, name))

      uploaded = wait_until { remote_get(name).is_a?(Net::HTTPSuccess) }
      expect(uploaded).to be == true
      expect(remote_get(name).body).to be == body
    end
  end
end
