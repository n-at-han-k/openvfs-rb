# frozen_string_literal: true

require "openvfs/occ"

require "json"
require "net/http"
require "securerandom"
require "tmpdir"
require "uri"

# Configure a sync folder, start the client, and check that a file dropped into the
# folder reaches the server.
#
# This is what the bindings exist for, so it is also the specification of what
# flake.nix has to build: whatever this needs is in scope, whatever it does
# not need gets patched out.
describe OpenVFS::OCC do
  SERVER = ENV.fetch("OPENCLOUD_URL", "http://127.0.0.1:18280")
  USER = ENV.fetch("OPENCLOUD_USER", "admin")
  PASSWORD = ENV.fetch("OPENCLOUD_PASSWORD", "openvfs-rb-test-admin")

  def basic_auth = "Basic #{["#{USER}:#{PASSWORD}"].pack("m0")}"

  def dav_root = "#{SERVER}/remote.php/dav/files/#{USER}"

  def server_up?
    uri = URI.parse("#{SERVER}/status.php")
    Net::HTTP.start(uri.host, uri.port, open_timeout: 2, read_timeout: 2) do |http|
      http.get(uri.request_uri).is_a?(Net::HTTPSuccess)
    end
  rescue StandardError
    false
  end

  def remote_get(name)
    uri = URI.parse("#{dav_root}/#{name}")
    request = Net::HTTP::Get.new(uri.request_uri)
    request["Authorization"] = basic_auth
    Net::HTTP.start(uri.host, uri.port) { |http| http.request(request) }
  end

  it "uploads a file that appears in a configured folder" do
    skip "no OpenCloud server at #{SERVER}; run docker compose up -d" unless server_up?

    Dir.mktmpdir("openvfs-sync") do |local|
      uuid = OpenVFS::OCC.configure(local, url: SERVER, remote_path: dav_root)

      # Credentials live in the keychain, keyed by the account.
      account = OpenVFS::OCC::AccountPtr.create(uuid)
      account.get.url = SERVER
      job = account.get.credential_manager.set("http/oauthtoken", basic_auth)
      job.start
      expect(account.get.credential_manager.contains?("http/oauthtoken")).to be == true

      # start runs the client's own event loop, so it has to be its own process.
      client = fork do
        $stdout.reopen(File::NULL)
        OpenVFS::OCC.start(["--logfile", "-"])
      end

      name = "watched-#{SecureRandom.hex(4)}.txt"
      body = "openvfs-rb #{Time.now.utc.iso8601}"
      staged = File.join(Dir.tmpdir, name)
      File.write(staged, body)
      File.rename(staged, File.join(local, name))

      begin
        deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 60
        uploaded = false
        until uploaded || Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
          uploaded = remote_get(name).is_a?(Net::HTTPSuccess)
          sleep 1 unless uploaded
        end

        expect(uploaded).to be == true
        expect(remote_get(name).body).to be == body
      ensure
        Process.kill("TERM", client)
        Process.wait(client)
      end
    end
  end
end
