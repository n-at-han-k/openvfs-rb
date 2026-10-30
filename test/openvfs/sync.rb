# frozen_string_literal: true

require "openvfs/occ"

require "net/http"
require "securerandom"
require "tempfile"
require "time"
require "timeout"
require "tmpdir"
require "uri"

# Configure a sync folder, start the client, and check that a file dropped into the folder reaches the server.
#
# This is what the bindings exist for, so it is also the specification of what flake.nix has to build: whatever this needs is in scope, whatever it does not need gets patched out.
describe OpenVFS do
  SERVER = ENV.fetch("OPENCLOUD_URL", "http://127.0.0.1:18280")
  USER = ENV.fetch("OPENCLOUD_USER", "admin")
  PASSWORD = ENV.fetch("OPENCLOUD_PASSWORD", "openvfs-rb-test-admin")

  UPLOAD_TIMEOUT = 60

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
    Net::HTTP.start(uri.host, uri.port) {|http| http.request(request)}
  end

  it "uploads a file that appears in a configured folder" do
    unless server_up?
      skip "no OpenCloud server at #{SERVER}; run docker compose up -d"
    end

    Dir.mktmpdir("openvfs-sync") do |local|
      uuid = OpenVFS.configure(local, url: SERVER, remote_path: dav_root, user: USER)

      # Credentials live in the keychain, keyed by the account. Store them in a separate process so Qt (used by the keychain) is never initialised in this process before we fork the sync client — Qt is not fork-safe.
      setup = fork do
        account = OpenVFS::Account.create(uuid)
        account.url = SERVER
        unless account.credential_manager.set("http/oauthtoken", basic_auth)
          abort "credential set failed"
        end
        unless account.credential_manager.contains?("http/oauthtoken")
          abort "credential contains? failed"
        end
        exit 0
      end
      Process.wait(setup)
      unless $?.success?
        raise "credential setup failed (exit #{$?.exitstatus})"
      end

      # start runs the client's own event loop, so it has to be its own process.
      # Capture its output to a temp file so a hang or crash leaves evidence.
      log = Tempfile.new("openvfs-client")
      client = fork do
        log.close
        $stdout.reopen(log.path, "w")
        $stderr.reopen($stdout)
        OpenVFS.start(["--logfile", "-", "--logdebug"])
      end

      name = "watched-#{SecureRandom.hex(4)}.txt"
      body = "openvfs-rb #{Time.now.utc.iso8601}"
      staged = File.join(Dir.tmpdir, name)
      File.write(staged, body)
      File.rename(staged, File.join(local, name))

      uploaded = false
      begin
        deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + UPLOAD_TIMEOUT
        until uploaded || Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
          begin
            uploaded = remote_get(name).is_a?(Net::HTTPSuccess)
          rescue StandardError
            uploaded = false
          end
          # Fail fast if the client died.
          begin
            exited = Process.waitpid(client, Process::WNOHANG)
            if exited
              raise "sync client exited early (pid #{client})"
            end
          rescue Errno::ECHILD
            raise "sync client exited early (pid #{client})"
          end
          unless uploaded
            sleep 1
          end
        end

        unless uploaded
          warn "=== sync client log (#{log.path}) ==="
          warn File.read(log.path) rescue "(unreadable)"
          warn "=== end of log ==="
        end

        expect(uploaded).to be == true
        expect(remote_get(name).body).to be == body
      rescue StandardError => e
        warn "=== sync client log (#{log.path}) ==="
        warn File.read(log.path) rescue "(unreadable)"
        warn "=== end of log (exception: #{e.class}: #{e.message}) ==="
        raise
      ensure
        begin
          Process.kill("TERM", client)
        rescue Errno::ESRCH
        end
        begin
          Timeout.timeout(5) {Process.wait(client)}
        rescue StandardError
          Process.kill("KILL", client) rescue nil
          Process.wait(client) rescue nil
        end
        log.close
        log.unlink
      end
    end
  end
end
