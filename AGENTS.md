# openvfs-rb ruby gem

## Important

* This project uses nix flakes... every command should be prefixed with `nix develop --command`.

## Testing

```bash
nix develop --command bundle exec sus
```

## Rubocop

```bash
nix develop --command bundle exec rubocop
nix develop --command bundle exec rubocop -A
```

The cops under `cops/` are house style, shared with ratalada, and are documented where they are
enabled in `.rubocop.yml`:

* `Local/BeginBlockAssignment` - no assigning a `begin...end`; the assignment goes inside.
* `Local/NoConditionalAssignment` - no assigning an `if` / `case`; assign inside each branch.
* `Local/NoModifierIf` - no modifier `if` / `unless`, so a guard cannot hide off the right edge.
* `Local/NoReturn` - no `return`; the last expression is the return value.
* `Local/UseTap` - `tap` where a method assigns a local and returns it.
* `Local/SingleLineMethodsForEvents` - a one-expression body uses the endless form.
* `Local/CommentStructure` - how a comment is built: unwrapped paragraphs, a summary no longer than
  the code earns, and annotations on a documented interface.

## Patching OpenCloud Desktop

The native `OpenVFS::OCC` extension links a trimmed static library built from OpenCloud Desktop's
sources. Desktop is vendored as the `opencloud-desktop` submodule, and two files decide what gets
built from it:

* `nix/desktop-upload.nix` - the `files` list, which is the filter. Only the paths named there are
  copied out of the submodule, so the UI is never even present at build time.
* `nix/desktop-upload.CMakeLists.txt` - the replacement `CMakeLists.txt`, which names the `.cpp`
  files that become `libopencloud-upload.a`.

Desktop's sources still `#include` the UI headers the filter leaves out, so each affected file gets
a patch that deletes those includes along with the code behind them. The patch is the removal -
there is no feature flag, and no `#ifdef` should be added to carry one.

### Never edit the patch files directly

`nix/patches/` is generated output. It mirrors the submodule's layout, one patch per source file
(`nix/patches/src/gui/folder.cpp.patch` patches `src/gui/folder.cpp`), and it is rewritten wholesale
every time the patches are extracted. Hand-written hunks carry hand-counted `@@` line numbers, which
go stale the moment anything above them shifts and fail the build with `malformed patch`.

Work on the submodule checkout instead and let git do the counting:

```bash
git submodule update --init          # once
bin/apply-patches                    # patches -> submodule working tree
$EDITOR opencloud-desktop/src/...    # make the change against real, compiling sources
bin/extract-patches                  # submodule working tree -> patches
nix build .#desktop-upload           # confirm it still builds
```

`bin/apply-patches` refuses to run unless the submodule's working tree is clean, so the patches are
always the only source of truth for what is changed. To start over, `git -C opencloud-desktop
checkout .` and apply again.

### Adding a source file

1. Add the `.cpp` and every header it needs to the `files` list in `nix/desktop-upload.nix`.
2. Add the `.cpp` to `add_library(opencloud-upload STATIC ...)` in
   `nix/desktop-upload.CMakeLists.txt`.
3. `bin/apply-patches`, build, and delete whatever UI the compiler complains about.
4. `bin/extract-patches`.

Patches must be staged in git before building. The flake only sees tracked files, so an untracked
patch is silently skipped and nix builds the previous source.
