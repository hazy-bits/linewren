# Development approach

Run the portable unit suite before a cross-build:

```sh
./scripts/test-local.sh
```

The same suite is exposed as the flake check. The normal build and Mac OS 9
smoke build are:

```sh
nix build .#checks.aarch64-darwin.local --no-link
nix build .#powerpc .#powerpc-smoke --no-link
nix run .#classic -- test 192.168.64.5
```

Portable policies belong in small ISO C modules, with host tests in `tests/`.
Toolbox, ATSUI, file-system, and rendering behavior belongs in the guest smoke
suite. A capability is complete only when its lowest practical regression test
runs in the fastest appropriate layer.

## Where changes belong

Application code and Toolbox resources live in `src/`.

- `main.c` coordinates the event loop, menus, windows, and temporary UI.
  Find works on the current document. Quick Open navigates the workspace.
- `editor-atsui.c` owns the buffer, selection, edit history, and layout.
  Text mutations go through the same splice path so Undo, reflow, and save
  tracking see the edit.
- `workspace.c` owns the active file, its on-disk baseline, autosave and
  recovery policy, and folder browsing. The sidebar and Quick Open share
  the catalog visibility rules while walking folders differently.
- `document.c` and `markdown.c` hold portable text policies. `text-files.c`
  is the boundary for encoding and safe File Manager writes.

The host suite covers matching, counts, file classification, and Markdown
actions. The guest suite covers the paths that depend on ATSUI or Toolbox,
including hit testing, commands, file conflicts, and recovery into a fresh
editor. A physical-machine pass still matters for disk stalls and input
latency. Keep new text rules host-testable, and keep guest fixtures close to
the platform code they exercise.
