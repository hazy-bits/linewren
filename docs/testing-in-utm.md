# Local testing in UTM

Keep the PowerPC Mac OS 9.2.2 guest running in UTM between iterations.
The shared `classic` harness builds with the pinned Retro68 toolchain and sends both
application forks over TCP to Retro68's LaunchAPPLServer in the guest.
[Retro68 LaunchAPPL documentation](https://github.com/autc04/Retro68#launchappl-and-the-test-suite)

## One-time guest setup

1. Start the Mac OS 9.2.2 VM with UTM's **Shared Network** mode. In the guest's
   **TCP/IP** control panel, use **Ethernet** and **DHCP Server**, then note the
   IPv4 address. Keep the server on the private VM network because its launch
   protocol accepts unauthenticated connections on TCP port 1984.
2. Copy the PowerPC build of `LaunchAPPLServer` to a dedicated writable folder on the
   guest disk. If you have `LaunchAPPLServer.bin` from a Retro68 samples build,
   use the [MacBinary transfer instructions](testing-mac-os-9.md) or create a
   virtual CD on the host with the commands below. This machine already has
   the server at `../result/LaunchAPPLServer.bin` relative to the Linewren checkout.
3. Open **LaunchAPPLServer** in the guest and select **OpenTransport TCP**
   in its Connection menu. Leave it running.

To create a setup CD from the Linewren checkout on modern macOS:

```sh
tools=$(nix build .#retro68-tools --no-link --print-out-paths --max-jobs 0)
server=../result/LaunchAPPLServer.bin
stage=$(mktemp -d)
mkdir "$stage/files"
"$tools/bin/Rez" --copy "$server" --data "$server" --type APPL --creator R68L \
  -o "$stage/files/LaunchAPPLServer.APPL"
hdiutil makehybrid -hfs -iso -hfs-volume-name 'Linewren Setup' \
  -o "$stage/setup.iso" "$stage/files"
echo "$stage/setup.iso"
```

Mount that ISO using UTM's **Drives** menu. Drag the server from the CD to a
folder on the guest hard disk before opening it. The server writes the app and
its test output beside itself, replacing files called `Retro68App` and `out`.

## Iterate

Run these commands from the Linewren checkout. Replace the example IP with the
address shown in the guest.

```sh
nix run .#classic -- doctor 192.168.64.2
nix run .#classic -- test 192.168.64.2
nix run .#classic -- run 192.168.64.2
```

- `test` builds the ATSUI smoke variant. It starts the actual app, creates the editor
  window, dispatches a synthetic key event, runs Select All and Clear, then
  runs the deeper editor and workspace suite and returns TAP to the host. A
  guest crash, malformed report, timeout, or failed assertion fails the command.
- `run` opens the normal interactive build. Use the VM window to test
  drawing, mouse input and clipboard behavior. Quit Linewren in the guest before
  running the next command. LaunchAPPL waits for the app to quit.

For two-finger scrolling in a Mac OS 9 guest that uses a VirtIO tablet, turn on
**Capture Input** in the VM toolbar. In UTM 4.7.5, the bundled VirtIO tablet
driver drops wheel events. Capture switches to the emulated USB mouse, which
can deliver those events to Linewren. Press **Command–Option** to release capture
with UTM's default shortcut.
[VirtIO tablet driver source](https://github.com/elliotnunn/classicvirtio/blob/master/scrollwheel.c)

Set `CLASSIC_MAC_GUEST` to omit the IP from repeated commands.

For the editor and its smoke suite, see [Text engine](text-engines.md).

## Capture the optional font specimens

This target activates temporary copies of the bundled suitcases in the guest.
It checks the four faces and native font discovery, renders the same Markdown
at 12, 13, and 14 pt in both themes, and returns six unscaled 13 pt captures of
Linewren's content area. It removes the temporary suitcases before exiting.

```sh
nix build .#powerpc-font-specimens --out-link result-font-specimens
tools=$(nix build .#retro68-tools --no-link --print-out-paths)
"$tools/bin/LaunchAPPL" -t 70 -e tcp --tcp-address 192.168.64.5 \
  result-font-specimens/Linewren.bin > /tmp/linewren-font-specimens.ppm
python3 scripts/unpack-font-specimens.py /tmp/linewren-font-specimens.ppm /tmp/linewren-font-specimens
```
