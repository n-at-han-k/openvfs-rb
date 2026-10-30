# frozen_string_literal: true

# Deliberately not "../openvfs": that pulls in the openvfs extension, which wraps the placeholder attributes libopenvfs keeps in extended attributes. Nothing here needs them, and upstream ships no Windows build of that library.
require_relative "version"

# One object per Ruby ABI in a platform gem, and in the source gem the single one extconf built under ext/. See lib/openvfs.rb.
begin
  require_relative "occ/#{RUBY_VERSION[/\d+\.\d+/]}/occ"
rescue LoadError
  require "openvfs/occ/occ"
end

require "digest"
require "fileutils"

module OpenVFS
  # Where the engine looks for its accounts and folders. ConfigFile::makeQSettings builds this path, so it is not ours to choose.
  # @returns [String] the INI file the engine reads on start.
  def self.config_path = File.join(Dir.home, ".config", "openvfs-rb", "openvfs-rb.cfg")

  # An account is a server and a user, so its id is derived from the two rather than drawn at random. A random one would be a new account on every start: the keychain entry is keyed by it, so the credential written last time would be orphaned and the journal would start again.
  # @parameter url [String] the server's base URL.
  # @parameter user [String] the user on that server.
  # @returns [String] a UUIDv5 of the two, stable for as long as they are.
  def self.account_uuid(url, user)
    bytes = Digest::SHA1.digest("#{url}\n#{user}")[0, 16].bytes
    # Version 5 and the RFC 4122 variant, which is what makes it a name-based UUID rather than 16 random bytes.
    bytes[6] = (bytes[6] & 0x0f) | 0x50
    bytes[8] = (bytes[8] & 0x3f) | 0x80
    hex = bytes.pack("C*").unpack1("H*")

    [hex[0, 8], hex[8, 4], hex[12, 4], hex[16, 4], hex[20, 12]].join("-")
  end

  # Writes the INI file AccountManager::restore and FolderMan::loadFolders read on start.
  #
  # One account and one folder per entry, so being signed in to several servers - or to one server as several users - is several syncs rather than whichever was configured last. The file is written whole, so what is passed here is the whole of what the engine will sync.
  #
  # The credential is not written here: credentials live in the keychain, so store each one with Account#credential_manager.set("http/oauthtoken", credential) before starting, against the uuid this hands back.
  #
  # @parameter accounts [Array(Hash)] one per sync, each with:
  #   path [String] the local directory to sync.
  #   url [String] the server's base URL.
  #   user [String] the user on that server, which the engine shows as the account's name.
  #   remote_path [String] the WebDAV path to sync against.
  #   uuid [String] the account's id; derived from the url and the user when absent.
  #   space_id [String] the space to sync, if any.
  # @returns [Array(String)] the uuid of each account, in the order they were given.
  def self.configure(accounts)
    entries = accounts.map.with_index(1) do |account, index|
      uuid = account[:uuid] || account_uuid(account[:url], account[:user])
      FileUtils.mkdir_p(account[:path])

      { index: index, uuid: uuid, account: account }
    end

    FileUtils.mkdir_p(File.dirname(config_path))
    File.write(config_path, <<~CONFIG)
      [Accounts]
      #{entries.map { |entry| account_section(entry) }.join("\n")}
      size=#{entries.size}

      [Folders]
      #{entries.map { |entry| folder_section(entry) }.join("\n")}
      size=#{entries.size}
    CONFIG

    entries.map { |entry| entry[:uuid] }
  end

  # display-name is what AccountManager::restore reads the user from, and what the engine then calls the account.
  def self.account_section(entry)
    <<~SECTION.chomp
      #{entry[:index]}\\url=#{entry[:account][:url]}
      #{entry[:index]}\\uuid=#{entry[:uuid]}
      #{entry[:index]}\\display-name=#{entry[:account][:user]}
    SECTION
  end

  def self.folder_section(entry)
    index = entry[:index]
    account = entry[:account]

    <<~SECTION.chomp
      #{index}\\accountUUID=#{entry[:uuid]}
      #{index}\\localPath=#{File.join(account[:path], "")}
      #{index}\\journalPath=.sync_journal.db
      #{index}\\davUrl=#{account[:remote_path]}
      #{index}\\spaceId=#{account[:space_id] || ""}
      #{index}\\displayString=#{account[:display_name] || account[:user]}
      #{index}\\paused=false
      #{index}\\ignoreHiddenFiles=true
      #{index}\\deployed=false
      #{index}\\priority=0
      #{index}\\virtualFilesMode=off
    SECTION
  end
end
