# frozen_string_literal: true

require "openvfs/occ"

# The dispatcher's signals only fire from inside a running sync, which is sync.rb's job. What is checked here is the binding itself: that the singleton is reachable, that a block can be subscribed to each of the four signals, and that the payload classes and enums the blocks are handed exist.
describe OpenVFS::ProgressDispatcher do
  it "hands back the dispatcher singleton" do
    expect(OpenVFS::ProgressDispatcher.instance).to be_a(OpenVFS::ProgressDispatcher)
  end

  it "subscribes a block to every signal" do
    dispatcher = OpenVFS::ProgressDispatcher.instance

    dispatcher.on_progress_info {|folder, progress|}
    dispatcher.on_item_completed {|folder, item|}
    dispatcher.on_sync_error {|folder, message, category|}
    dispatcher.on_folder_conflicts {|folder, paths|}
  end

  it "subscribes a proc as well as a block" do
    OpenVFS::ProgressDispatcher.instance.on_item_completed(proc {|folder, item|})
  end

  it "names the sync statuses an item can report" do
    expect(OpenVFS::SyncFileItem::Status::Success.to_i).to be == 4
    expect(OpenVFS::SyncFileItem::Direction::Up.to_i).to be == 1
  end

  it "names the phases a sync run passes through" do
    expect(OpenVFS::ProgressInfo::Status::None.to_i).to be == 0
    expect(OpenVFS::ProgressInfo::Status::Done.to_i).to be == 4
  end

  it "names the error categories a sync error carries" do
    expect(OpenVFS::ErrorCategory::Normal.to_i).to be == 0
    expect(OpenVFS::ErrorCategory::InsufficientRemoteStorage.to_i).to be == 1
  end
end
