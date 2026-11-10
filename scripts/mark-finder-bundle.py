"""Mark the BNDL icon as registered in Retro68's install artifacts."""

from binascii import crc_hqx
from pathlib import Path
import sys


folder = Path(sys.argv[1])
app = b"APPLLnwr"

binary = folder / "Linewren.bin"
data = bytearray(binary.read_bytes())
if data[65:73] != app or crc_hqx(data[:124], 0) != int.from_bytes(data[124:126], "big"):
    raise SystemExit("Unexpected Linewren MacBinary header")
data[73] |= 0x20  # High byte of the Finder kHasBundle flag.
data[124:126] = crc_hqx(data[:124], 0).to_bytes(2, "big")
binary.write_bytes(data)

image = folder / "Linewren.img"
if image.exists():
    data = bytearray(image.read_bytes())
    record = b"\x02\x00\x00\x00" + app
    if data[1024:1026] != b"BD" or data.count(record) != 1:
        raise SystemExit("Could not identify Linewren's HFS catalogue file record")
    data[data.index(record) + len(record)] |= 0x20
    image.write_bytes(data)
