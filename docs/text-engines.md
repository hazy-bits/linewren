# ATSUI text engine

Linewren uses one PowerPC-native editor implemented directly on ATSUI. ATSUI screen
metrics drive drawing, wrapping, selection, hit testing, and caret placement, so
Unicode text keeps classic-Mac screen spacing without a second editor backend.

The editor keeps UTF-16 source in a relocatable Handle, a parallel Markdown style
map, bounded Undo deltas, and lazy soft-wrap checkpoints. It paints through an
offscreen buffer when memory allows. Long-line wrapping completes incrementally
during idle time; a viewport is laid out before offscreen work continues.

Markdown files use MD4C with CommonMark, tables, task lists, strikethrough, and
bare autolinks. Headings, emphasis, links, quotes, lists, code, and table headers
are styled in place while the saved file remains UTF-8 source. Supported non-
Markdown text files use the same editor without semantic styling.

Linewren reads UTF-8 files up to 192 KiB and writes UTF-8 with LF line endings.
Existing files are autosaved with File Manager exchange and an expected-byte
snapshot prevents overwriting external changes.

The editor supports Unicode clipboard data, MacRoman keyboard input, mouse and
keyboard selection, word movement, list continuation and nesting, Markdown bold
and italic commands, Find and Replace, font selection, adjustable point size,
themes, wheel scrolling, and resizable windows.

Build and exercise it with:

```sh
nix build .#powerpc
nix run .#classic -- test 192.168.64.5
nix run .#classic -- run 192.168.64.5
```

The guest suite covers editing and Undo, Unicode paste, file round trips,
autosave and conflict protection, Markdown and plain-text presentation, search,
selection and hit testing, typography changes, large-document wrapping,
incremental reflow, and memory/timing diagnostics.
