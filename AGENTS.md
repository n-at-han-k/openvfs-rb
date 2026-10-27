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
