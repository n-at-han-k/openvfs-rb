# frozen_string_literal: true

# Deliberately not "../openvfs": that pulls in the openvfs extension, which wraps the placeholder attributes libopenvfs keeps in extended attributes. Nothing here needs them, and upstream ships no Windows build of that library.
require_relative "version"

# One object per Ruby ABI in a platform gem, and in the source gem the single one extconf built under ext/. See lib/openvfs.rb.
begin
  require_relative "occ/#{RUBY_VERSION[/\d+\.\d+/]}/occ"
rescue LoadError
  require "openvfs/occ/occ"
end

require "fileutils"
require "securerandom"
require "uri"

module OpenVFS
  # What the engine calls itself, which is also where it reads its config.
  # @parameter user [String] the user on the server.
  # @parameter url [String] the server's base URL, whose host names the account with the user.
  # @returns [String] the application name for that account.
  def self.application_name_for(user, url) = "#{user}@#{URI(url.to_s).host}"

  # The INI that application name resolves to, derived the way ConfigFile derives it.
  # @parameter name [String] the application name; the current one by default.
  # @returns [String] the config file for that name.
  def self.config_path(name = application_name)
    File.join(ENV.fetch("XDG_CONFIG_HOME", File.join(Dir.home, ".config")), name, "#{name.downcase}.cfg")
  end

  # Writes the INI file AccountManager::restore and FolderMan::loadFolders read on start.
  #
  # The credential is not written here: credentials live in the keychain, so store it with Account#credential_manager.set("http/oauthtoken", credential) before starting.
  #
  # @parameter path [String] The local directory to sync.
  # @parameter url [String] The server's base URL.
  # @parameter remote_path [String] The WebDAV path to sync against.
  # @parameter user [String] The user on that server. With the server's host it names the account, which is the application name and so the config file.
  # @parameter uuid [String] The account's UUID; also names the keychain entry.
  # @parameter display_name [String] How the folder is named in logs.
  # @parameter space_id [String] The space to sync, if any.
  # @returns [String] The UUID the account was written under.
  def self.configure(path, url:, remote_path:, user:, uuid: SecureRandom.uuid, display_name: path, space_id: "")
    # One account per process, so the application name is the account's: it is what decides which config the engine reads, and OpenVFS.start reads it rather than being told.
    self.application_name = application_name_for(user, url)

    config_path = self.config_path
    FileUtils.mkdir_p(File.dirname(config_path))
    FileUtils.mkdir_p(path)

    File.write(config_path, <<~CONFIG)
      [Accounts]
      1\\url=#{url}
      1\\uuid=#{uuid}
      size=1

      [Folders]
      1\\accountUUID=#{uuid}
      1\\localPath=#{File.join(path, "")}
      1\\journalPath=.sync_journal.db
      1\\davUrl=#{remote_path}
      1\\spaceId=#{space_id}
      1\\displayString=#{display_name}
      1\\paused=false
      1\\ignoreHiddenFiles=true
      1\\deployed=false
      1\\priority=0
      1\\virtualFilesMode=off
      size=1
    CONFIG

    uuid
  end
end
