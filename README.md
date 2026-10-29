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

## OpenCloud Desktop sync client

`OpenVFS` wraps the desktop client's account and keychain. Configure a folder, store the account's credential, then run the client:

```ruby
require "openvfs/occ"

uuid = OpenVFS.configure(
  "/local/folder",
  url: "https://cloud.example",
  remote_path: "/remote.php/dav/files/user",
)

account = OpenVFS::Account.create(uuid)
account.url = "https://cloud.example"
account.credential_manager.set("http/oauthtoken", ENV.fetch("OPENCLOUD_ACCESS_TOKEN"))

OpenVFS.start(["--logfile", "-"])
```

`OpenVFS.configure` writes `~/.config/openvfs-rb/openvfs-rb.cfg`; it does not touch the keychain. `OpenVFS::Account.create` returns the wrapped account, `account.url=` sets the server, and `account.credential_manager` exposes `set`, `get`, `remove` and `contains?`. The keychain methods block until QtKeychain's job finishes, so no event loop needs to be running. For a server that enables HTTP Basic authentication, store the complete `Basic ...` header instead of a bearer token. `OpenVFS.start` calls the desktop's `OCC::init`, which runs the sync client's event loop and does not return until the client exits; run it in its own process.

### Building the desktop subset

The locked `desktop` flake input is built into a small static library with Qt's generated meta-object code, using the patches under `patches/`. The patches keep the sync engine and keychain, and omit the desktop UI, QML, translations and unrelated plugins.

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
The installed `opencloud-upload.pc`, headers and static library are what the extension builds against.

### Local OpenCloud server

The integration test in `test/openvfs/sync.rb` configures a temporary local folder, starts the client, and checks that a file dropped into the folder reaches a real OpenCloud server.

The test skips while no server answers `OPENCLOUD_URL`, so the default run stays hermetic.

```sh
docker compose up -d opencloud
nix develop --command bundle exec sus test/openvfs/sync.rb
```

The compose file starts `opencloudeu/opencloud-rolling:9.0.0` with WebDAV basic
authentication enabled and listens on `127.0.0.1:${OPENCLOUD_PORT:-18280}`.
Override the credentials with `OPENCLOUD_USER` and `OPENCLOUD_PASSWORD` when the default admin user is not the account you want to sync.

## Contributing

See [RELEASE.md](RELEASE.md) for the release process and
[DEPRECATIONS.md](DEPRECATIONS.md) for the deprecation policy.

## License

Released under the [MIT License](license.md).

The linked OpenCloud Desktop sources retain their upstream GPL/LGPL licenses;
the subset derivation installs the upstream `COPYING` file with the library.
