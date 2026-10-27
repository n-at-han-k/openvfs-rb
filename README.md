# openvfs-rb

A virtual filesystem for Ruby.

```ruby
gem "openvfs-rb"
```

```ruby
require "openvfs"

OpenVFS::VERSION
```

## OpenVFS API

The native extension wraps OpenVFS's registration and placeholder metadata
types:

```ruby
registration = OpenVFS::Registration.register_filesystem(path, owner)
raise registration.error unless registration.ok?

attributes = OpenVFS::PlaceHolderAttributes.create(path, etag, file_id, size)
attributes.state = OpenVFS::Constants::States::DeHydrated
serialized_attributes = attributes.to_data
```

OpenVFS development headers and `libopenvfs` are required to build the
extension. In this repository's Nix development shell they are provided by the
locked OpenVFS source input.

## OpenCloud Desktop uploads

`OpenVFS::OCC` wraps the desktop client's `OCC::Account` and `OCC::PUTFileJob`
upload path. Give the account a WebDAV **space root URL** and an access token:

```ruby
require "openvfs/occ"

account = OpenVFS::OCC::Account.new(
  "https://cloud.example/remote.php/dav/spaces/SPACE_ID",
  ENV.fetch("OPENCLOUD_ACCESS_TOKEN"),
)

created = account.upload_new("/local/report.txt", "documents/report.txt")
raise created.error unless created.ok?

# After editing the local file, replace the revision previously uploaded.
updated = account.upload_changed(
  "/local/report.txt",
  "documents/report.txt",
  etag: created.etag,
)
raise updated.error unless updated.ok?

updated.etag
updated.file_id
```

`upload_new` sends `If-None-Match: *`. `upload_changed` requires the previous
ETag and sends `If-Match`; a stale revision returns HTTP 412 and `ok? == false`.
Paths are relative to the account URL, and the remote parent directory must
already exist. Uploads stream the file through the desktop client's native PUT
job. The returned ETag is unquoted, matching OpenVFS placeholder metadata.

Both methods return an `OpenVFS::OCC::UploadResult` with `ok?`, `http_status`,
`network_error`, `etag`, `file_id`, `request_id`, `response_body`, `error`,
`bytes_sent`, `total_bytes`, `elapsed_ms`, `aborted?`, and `timed_out?`.
They accept `timeout_ms:` (default: 300,000 milliseconds).

For explicit job control:

```ruby
job = OpenVFS::OCC::PUTFileJob.new(account, "/local/report.txt", "documents/report.txt", updated.etag)
job.start
# job.abort cancels an active upload.
result = job.wait
job.finished?
job.result
```

`wait` drives Qt's event loop and releases Ruby's GVL while waiting. Use these
objects on the thread that loaded `openvfs/occ`. The wrapper retains the account
for each job and copies the response before Qt deletes its completed job.

Credentials are supplied by the Ruby caller and held in memory. Set
`account.access_token = token` between uploads to replace an expired token.
The API performs explicit PUT uploads; filesystem monitoring, sync-journal
updates, OAuth login/refresh, and the desktop's resumable TUS orchestration are
outside this API.

### Building the desktop subset

The existing locked `desktop` flake input supplies an explicit allowlist of files
in `nix/desktop-upload.nix`. The derivation copies those files, applies
`nix/desktop-upload.patch`, and builds a small static library with Qt's generated
meta-object code. The patch isolates the PUT job and omits the account's desktop
UI, space discovery, keychain, and disk-cache initialization for this build.

```sh
nix develop --command ruby -C ext/openvfs extconf.rb
nix develop --command make -C ext/openvfs
nix develop --command ruby -C ext/openvfs/occ extconf.rb
nix develop --command make -C ext/openvfs/occ
nix develop --command bundle exec sus
```

The development shell provides `OPENCLOUD_UPLOAD_PREFIX` and `pkg-config`
metadata. Outside the shell, install that subset and its Qt 6 development
dependencies, then make `opencloud-upload.pc` visible to `pkg-config`.
`OpenVFS::OCC::DESKTOP_VERSION` and `DESKTOP_REVISION` identify the compiled
desktop source.

## Contributing

See [RELEASE.md](RELEASE.md) for the release process and
[DEPRECATIONS.md](DEPRECATIONS.md) for the deprecation policy.

## License

Released under the [MIT License](license.md).

The linked OpenCloud Desktop sources retain their upstream GPL/LGPL licenses;
the subset derivation installs the upstream `COPYING` file with the library.
