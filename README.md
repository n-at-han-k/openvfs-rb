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

## Contributing

See [RELEASE.md](RELEASE.md) for the release process and
[DEPRECATIONS.md](DEPRECATIONS.md) for the deprecation policy.

## License

Released under the [MIT License](license.md).
