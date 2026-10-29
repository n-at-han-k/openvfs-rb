# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.1] - 2026-10-29

### Fixed

- The precompiled platform gems are published the way vodozemac-rb publishes its own, and are selectable again. 1.1.0's carried `required_rubygems_version = ">= 4.1.0.a"`, a prerelease floor almost no installation meets, so RubyGems and bundler skipped every platform gem and resolved the source gem instead - which compiles the extensions, and so wants Qt 6, the OpenVFS library and a C++ toolchain on a machine that should have needed none of them. No floor is set now: a platform gem needs none to be resolved.

### Changed

- The platform gems are built by the Cross Compile workflow, one per matrix job, by the new `bin/package-platform-gem`. This is what vodozemac-rb's `oxidize-rb/actions/cross-gem` step does for a Rust extension, and that action builds Rust only, hence the script. `bin/release-gem` downloads and pushes them and no longer opens or repacks an artifact.
- `bin/release-gem` identifies a release's artifacts by gem filename rather than by the commit its workflow run was for. A gem's name carries its version, so the ancestry check and the workflow-paths staleness gate 1.1.0 needed are both gone.

## [1.1.0] - 2026-10-29

### Added

- `OpenVFS::ProgressDispatcher.instance`, the singleton every sync progress signal comes through. `#on_progress_info`, `#on_item_completed`, `#on_sync_error` and `#on_folder_conflicts` each subscribe a block (or a proc passed as the argument), and nothing disconnects it. The blocks are called from inside a running sync, so a dispatcher is only useful alongside `OpenVFS.start`.
- `OpenVFS::Folder`, the sync folder a progress signal is about, with `#display_name`, `#path`, `#clean_path`, `#short_gui_local_path` and `#sync_running?`. Folders come from the desktop client, so the class has no constructor.
- `OpenVFS::SyncFileItem`, one file in a sync run, with `#local_name`, `#destination`, `#rename_target`, `#original_file`, `#status`, `#direction`, `#error_string`, `#message_string`, `#http_error_code`, `#affected_items`, `#size`, `#modtime`, `#etag`, `#empty?`, `#directory?` and `#error_status?`, plus `#action_string`, `#result_string`, `#warning_kind?` and `#ignored_kind?` for the lines the desktop's own UI would show. `OpenVFS::SyncFileItem::Status` and `OpenVFS::SyncFileItem::Direction` name the values `#status` and `#direction` return.
- `OpenVFS::ProgressInfo`, the payload of `#on_progress_info`, with `#status`, `#updating_estimates?`, `#total_files`, `#completed_files`, `#total_size`, `#completed_size`, `#current_file`, `#total_progress`, `#optimistic_eta`, `#trust_eta?`, `#file_progress(item)`, `#current_discovered_remote_folder`, `#current_discovered_local_folder` and `#last_completed_item`. `OpenVFS::ProgressInfo::Status` names the phases a sync run passes through.
- `OpenVFS::Estimates`, returned by `#total_progress` and `#file_progress`, with read-only `#estimated_bandwidth` and `#estimated_eta`.
- `OpenVFS::ErrorCategory`, the third argument handed to an `#on_sync_error` block, with `Normal` and `InsufficientRemoteStorage`.

## [1.0.0] - 2026-10-29

### Added

- `require "openvfs/occ"`, a second native extension wrapping the OpenCloud Desktop sync client.
- `OpenVFS.configure(path, url:, remote_path:, uuid:, display_name:, space_id:)` writes the account and folder entries to `~/.config/openvfs-rb/openvfs-rb.cfg` and returns the account's UUID. It does not touch the keychain; store the credential separately.
- `OpenVFS.start(arguments = [])` runs the desktop client's event loop and does not return until the client exits, so run it in its own process.
- `OpenVFS::Account.create(uuid)`, with `#uuid`, `#url`, `#url=` and `#credential_manager`.
- `OpenVFS::CredentialManager`, with `#set`, `#get`, `#remove` and `#contains?`. Each call runs the keychain job to completion, so no event loop needs to be running.

### Changed

- The version constant moved to `OpenVFS::Rb::VERSION` in `lib/openvfs/rb/version.rb`. `OpenVFS::VERSION` is unchanged and now delegates to it.
- The gemspec declares a second extension, `ext/openvfs/occ/extconf.rb`. Installing the gem now also needs Qt 6 and the trimmed OpenCloud Desktop upload library; `extconf.rb` aborts unless `pkg-config` can find `opencloud-upload`, which `OPENCLOUD_UPLOAD_PREFIX` or the Nix development shell provides.
