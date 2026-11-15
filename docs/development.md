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

## Release acceptance on PowerPC

Repeat these checks on Mac OS 9.2.2, Tiger, and Leopard. Use disposable copies
and compare the saved bytes on the host.

1. Open a UTF-8 Markdown file with a BOM and CRLF endings. Make a small edit
   and let autosave finish. Check that the BOM and CRLF endings survive.
   Repeat with a MacRoman file containing accented text and CR endings,
   using **File > Open as MacRoman**.
2. Type accented text with a dead key and enter text with an installed input
   method. Find it by typing and by selecting it before opening Find. Replace
   one match and undo. Search for an accented filename in Quick Open.
3. Move between paragraphs with Option–Up and Option–Down. Add Shift to
   extend the selection. Drag a paragraph to a new position, then undo.
4. Edit the open file from another app. Use **File > Reload from Disk** and
   confirm Linewren shows the new contents. Repeat with unsaved Linewren
   edits. Try Cancel and Save As, and compare the original and the copy.
5. Open a locked file or a file on a read-only volume. Check the status on
   open, edit the text, and save a separate copy. Verify the original bytes.
   Also check that a file over 192 KiB reports the size limit.

On all targets, put **mispelled** in a Markdown document and add typos
inside an inline code span and a link destination. On Tiger and Leopard,
choose English from the language picker next to the word count. Use
**Edit > Spelling > Check Spelling** and check that only prose is marked with dotted
underlines. Right-click or Control-click a marked word to choose a
suggestion or **Ignore in This Document**. Verify that correcting a word
keeps the other marks in place, Undo restores the text and spelling
marks, and Redo refreshes them again. A recheck skips ignored words in
that document. Ordinary edits clear the marks until the next check or
Undo or Redo.

Open **Edit > Spelling > Spelling…**. Use **Find Next** to review a second
mistake, select a suggestion, choose **Change**, then close the dialog and
undo the correction.
Open the dialog on a document with no spelling mistakes. Switch languages
inside the dialog and verify that it refreshes immediately, the status bar
reflects the choice, and reopening the document restores that choice.

Toggle **Check Spelling as You Type**, edit one word, and verify that the
other marks remain while the check waits for a pause. On Tiger and
Leopard, change the language and verify that marks and suggestions use
the new dictionary. Assign different languages to two files in one folder,
switch between them in the sidebar, and reopen both after relaunch.
On Mac OS 9, use the picker to select Russian for one document and US
English for another. Try `Ёлка, работал и работд.` in the Russian document
and verify that only `работд` is flagged and that `работа` appears in the
suggestions. The word count still toggles independently of the picker.

Mac OS 9 loads one bundled dictionary at a time and uses an 8 MiB
preferred memory partition. For Cyrillic typing and native Cyrillic
labels, install the Cyrillic Language Kit and select the Russian
keyboard layout in the guest. Tiger and Leopard use the system spelling service.

See [spelling settings and dictionary packs](spelling-dictionaries.md)
for the Classic distribution design and archive license audit.
