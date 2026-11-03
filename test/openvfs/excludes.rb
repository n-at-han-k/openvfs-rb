# frozen_string_literal: true

require "openvfs/occ"

# The default exclude list, which the client will not sync a folder without.
#
# SyncEngine::loadDefaultExcludes adds ConfigFile::excludeFile(SystemScope) whether or not it is there, and ExcludedFiles::reloadExcludeFiles returns false if any list it was given cannot be opened. Folder's constructor then puts the folder in SyncResult::SetupError and returns, which means no watcher and nothing scheduled, ever. The account still connects and authenticates first, so what it looks like from outside is a sync that logs nothing wrong and never happens.
#
# The compiled-in path is the datadir of the opencloud-upload build the extension was compiled against. In this repository's shell that path exists, because the shell has that derivation in it; in a released gem on another machine it does not. So the gem carries the file and names it in the environment, and these are the checks that it still does.
describe OpenVFS do
  SHIPPED = File.expand_path("../../lib/openvfs/occ/sync-exclude.lst", __dir__)

  it "ships the exclude list in the gem" do
    expect(File).to be(:exist?, SHIPPED)
  end

  it "ships a list with patterns in it rather than an empty file" do
    lines = File.readlines(SHIPPED).map(&:strip).reject {|line| line.empty? || line.start_with?("#")}

    expect(lines).not.to be(:empty?)
  end

  it "names the shipped list in the environment the client reads" do
    expect(ENV.fetch("OPENCLOUD_SYNC_EXCLUDE_FILE", nil)).to be == SHIPPED
  end

  it "names a list that can actually be opened" do
    expect(File).to be(:readable?, ENV.fetch("OPENCLOUD_SYNC_EXCLUDE_FILE"))
  end
end
