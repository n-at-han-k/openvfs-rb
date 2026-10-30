# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.2.0] - 2026-10-30

### Changed

- `OpenVFS.configure` takes `user:`, and writes the config where that account's engine will read it. One call writes one file for one account, so several accounts are several calls from several processes rather than one file holding them all - the previous path was fixed, so a second call overwrote the first and only the last account synced.
- `ApplicationScope` restores whatever application name is set rather than putting back `openvfs-rb`. The name decides which config the engine reads, so a credential write would otherwise repoint it at another account.

### Added

- `OpenVFS.application_name` and `OpenVFS.application_name=`. `ConfigFile::configPath` is `QStandardPaths::AppConfigLocation`, which is `$XDG_CONFIG_HOME/<application name>`, and `ConfigFile::configFile` is that plus the lowercased name and `.cfg` - so the name is what picks an account's config out, and `configure` sets it.
- `OpenVFS.application_name_for(user, url)`, the `<user>@<host>` an account is named by.
- `OpenVFS.config_path`, the file the current application name resolves to. Derived the way `ConfigFile` derives it, so the two agree; the old path ignored `XDG_CONFIG_HOME` where the engine honours it.

## [1.1.3] - 2026-10-30

### Added

- `OpenVFS::ProgressInfo#current_items`, the files a run has in flight, as a hash of the path the engine names a file by to an `OpenVFS::ProgressItem`. This is what a per-file progress bar reads; `ProgressInfo`'s own totals are the whole run, and `#file_progress` gives a file's bandwidth and eta but not how much of it has gone.
- `OpenVFS::ProgressItem`, with `#item` and `#progress`. Both hand back a copy, so a block may keep them after the signal returns - the engine drops its own entry when the file finishes.
- `OpenVFS::Progress`, with `#completed`, `#remaining` and `#estimates`.

## [1.1.2] - 2026-10-30

### Fixed

- The precompiled platform gems are selectable. Each one now carries an object per Ruby ABI - 3.3, 3.4 and 4.0, under `lib/openvfs/<abi>/` - so `required_ruby_version` is the range `>= 3.3, < 4.1.dev` and no `required_rubygems_version` floor is set. A gem built for one ABI has to pin that ABI, and then its name, version and platform no longer identify it, which puts it in rubygems.org's content-addressable scheme; pushing there is refused unless the floor is `">= 4.1.0.a"`, a prerelease almost nothing meets. That is why 1.1.0's platform gems were published and then never chosen by bundler, and why 1.1.1 published only a source gem. This is the layout vodozemac-rb gets from rb-sys.

### Changed

- `lib/openvfs.rb` and `lib/openvfs/occ.rb` require the object for the running Ruby, falling back to the plain require the load path answers, which is what a source gem and the working tree need.
- The Cross Compile workflow builds in two stages: `cross-objects` compiles every platform and ABI pair, and `cross-gem` collects one platform's ABIs into one gem. Packaging compiles nothing, so it runs without the native dependencies whatever platform the gem is for.
- `flake.nix` exposes a shell per Ruby ABI - `nix develop .#ruby-33` - since the objects for each have to be built somewhere, and the default shell is unchanged.

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
