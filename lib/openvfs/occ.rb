# frozen_string_literal: true

require_relative "../openvfs"
require "openvfs/occ/occ"

module OpenVFS
  # OpenCloud Desktop's sync client.
  module OCC
    # Starts syncing a local directory against a server, and keeps syncing it as files
    # change on disk.
    #
    # @parameter path [String] The local directory to sync.
    # @parameter url [String] The server's base URL.
    # @parameter username [String] The account's user name.
    # @parameter password [String] The account's password or token.
    # @parameter remote_path [String] The WebDAV path to sync against.
    # @parameter display_name [String] How the folder is named in logs.
    # @returns [Folder] The folder now being synced.
    def self.mount(path, url:, username:, password:, remote_path:, display_name: path)
      uuid = Utility.uuid
      account = AccountPtr.create(uuid)
      account.url = url
      account.credentials = TokenCredentials.new(username, password)

      state = AccountManager.instance.add_account(account)

      # The account has to report its capabilities before AccountState will connect.
      FetchServerSettingsJob.new(account).start
      state.check_connectivity(false)

      definition = FolderDefinition.new(uuid, remote_path, "", display_name)
      definition.local_path = path

      FolderMan.instance.add_folder(state, definition).tap do
        FolderMan.instance.scheduler.start
      end
    end

    # Stops syncing a local directory.
    #
    # @parameter path [String] The local directory to stop syncing.
    def self.unmount(path)
      folder = FolderMan.instance.folder_for_path(path)
      if folder.nil?
        raise ArgumentError, "#{path} is not mounted"
      end

      FolderMan.instance.remove_folder(folder)
    end
  end
end
