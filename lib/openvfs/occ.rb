# frozen_string_literal: true

# Deliberately not "../openvfs": that pulls in the openvfs extension, which wraps the
# placeholder attributes libopenvfs keeps in extended attributes. Nothing here needs
# them, and upstream ships no Windows build of that library.
require_relative "version"
require "openvfs/occ/occ"

require "fileutils"
require "securerandom"

module OpenVFS
  # Writes the INI file AccountManager::restore and FolderMan::loadFolders read on start.
  #
  # The credential is not written here: credentials live in the keychain, so store it with Account#credential_manager.set("http/oauthtoken", credential) before starting.
  #
  # @parameter path [String] The local directory to sync.
  # @parameter url [String] The server's base URL.
  # @parameter remote_path [String] The WebDAV path to sync against.
  # @parameter uuid [String] The account's UUID; also names the keychain entry.
  # @parameter display_name [String] How the folder is named in logs.
  # @parameter space_id [String] The space to sync, if any.
  # @returns [String] The UUID the account was written under.
  def self.configure(path, url:, remote_path:, uuid: SecureRandom.uuid, display_name: path, space_id: "")
    config_path = File.join(Dir.home, ".config", "openvfs-rb", "openvfs-rb.cfg")
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
