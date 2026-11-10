# Installing Linewren on Mac OS 9

Use a PowerPC Mac running Mac OS 9.2.2. The MacBinary package contains both
application forks and can be copied through a FAT32 USB drive.

1. Copy `Linewren.bin` onto a FAT32 USB drive. On the classic Mac, copy it to a
   folder on the hard disk.
2. Drop `Linewren.bin` onto StuffIt Expander for Mac OS 9. Double-click the
   decoded **Linewren** application in Finder.
   [Apple's MacBinary instructions](https://til-2001.mirror.kb1max.com/techinfo.nsf/artnum/n24629/)
3. Open a long Markdown file and scroll over the writing area to test the
   mouse wheel. Linewren reads the wheel through the system's InputSprocket
   extension. In UTM, the host translates trackpad gestures to wheel input.
   [Apple's InputSprocket reference](https://leopard-adc.pepas.com/documentation/Carbon/Reference/Games_Sprockets_Legacy/gamesprock_legacy_ref.pdf)
   [UTM scroll settings](https://docs.getutm.app/preferences/macos/)

To rebuild the same package from the checkout on modern macOS:

```sh
nix build .#powerpc --out-link result --option fallback false
```

Copy the actual file `result/Linewren.bin`. If Nix plans to compile
Retro68, see [Nix cache setup](nix-cache.md).

## Distribution with bundled fonts

Build the ready-to-transfer archive on the host.

```sh
nix build .#distribution --out-link result-distribution
```

Extract `result-distribution/Linewren-Mac-OS-9.zip` on the host, transfer
`Linewren.bin` to the classic Mac, and decode it with StuffIt Expander. Linewren
loads the bundled fonts at startup. The separate suitcases in the ZIP are
for system-wide installation. See the [font installation notes](../fonts/README.md)
for the suitcase names and upstream licenses.
