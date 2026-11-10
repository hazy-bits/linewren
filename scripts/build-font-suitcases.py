#!/usr/bin/env python3
"""Build the optional Mac OS 9 font files from the pinned sources in fonts/README.md."""

import argparse
from io import BytesIO
from pathlib import Path
import subprocess
import tempfile

from fontTools.misc.macRes import ResourceReader
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable

STYLES = ("Regular", "Bold", "Italic", "BoldItalic")
FONTFORGE_SCRIPT = """faces = Array(4)
i = 0
while (i < 4)
  Open($argv[i+2], 1)
  faces[i] = $filename
  i++
endloop
GenerateFamily($argv[1], "", 0x8090, faces)
"""


def prepare_face(source, destination, family, style):
    subfamily = "Bold Italic" if style == "BoldItalic" else style
    postscript = f"{family.replace(' ', '')}-{style}"
    full = family if style == "Regular" else f"{family} {subfamily}"
    names = {
        1: family, 2: subfamily, 3: postscript, 4: full, 6: postscript,
        16: family, 17: subfamily, 18: full, 21: family, 22: subfamily,
    }
    with TTFont(source, recalcTimestamp=False) as font:
        table = font["name"]
        for record in table.names[:]:
            if record.nameID in names:
                table.setName(names[record.nameID], record.nameID, record.platformID,
                              record.platEncID, record.langID)
        for name_id in (1, 2, 3, 4, 6):
            table.setName(names[name_id], name_id, 1, 0, 0)
        cmap = font["cmap"]
        # Classic ATSUI cannot load the UCS-4 cmap in this Source Sans release.
        if family == "Linewren Humanist":
            cmap.tables = [table for table in cmap.tables if table.format != 12]
        if cmap.getcmap(1, 0) is None:
            unicode_map = cmap.getBestCmap()
            roman = CmapSubtable.newSubtable(6)
            roman.platformID, roman.platEncID, roman.language = 1, 0, 0
            roman.cmap = {
                byte: unicode_map[ord(bytes([byte]).decode("mac_roman"))]
                for byte in range(256)
                if ord(bytes([byte]).decode("mac_roman")) in unicode_map
            }
            cmap.tables.append(roman)
        # The upstream digital signature no longer describes the modified file.
        if "DSIG" in font:
            del font["DSIG"]
        font.save(destination)


def rez_data(header, data):
    lines = [f"data {header} {{"]
    lines.extend(f'    $"{data[i:i + 64].hex()}"' for i in range(0, len(data), 64))
    return "\n".join([*lines, "};"])


def build_family(args, stage, family, source_faces, license_file):
    faces = []
    for source, style in zip(source_faces, STYLES, strict=True):
        destination = stage / f"{family}-{style}.ttf"
        prepare_face(source, destination, family, style)
        faces.append(destination)
    suitcase = stage / f"{family}.suit"
    subprocess.run([args.fontforge, "-lang=ff", "-script", stage / "family.pe",
                    suitcase, *faces], check=True)
    # Use FontForge's family association, but keep the upstream outlines,
    # hints and layout tables instead of the regenerated FontForge sfnts.
    with ResourceReader.openResourceFork(suitcase) as fork:
        resources = ResourceReader(fork)
        fond, = resources["FOND"]
        statements = [rez_data(f"""'FOND' ({fond.id}, "{family}")""", fond.data)]
        styles = set()
        for resource in resources["sfnt"]:
            with TTFont(BytesIO(resource.data)) as font:
                style = font["head"].macStyle & 3
            styles.add(style)
            face = faces[style]
            statements.append(rez_data(f"'sfnt' ({resource.id})", face.read_bytes()))
        if len(resources["sfnt"]) != 4 or styles != {0, 1, 2, 3}:
            raise ValueError(f"Unexpected face associations for {family}")
    rez_source = stage / "family.r"
    rez_source.write_text("\n".join(statements) + "\n")
    subprocess.run([args.rez, "--type", "FFIL", "--creator", "DMOV",
                    "-o", args.output / f"{family}.bin", rez_source], check=True)
    license_lines = license_file.read_text(encoding="utf-8").splitlines()
    (args.output / f"{family}-OFL.txt").write_text(
        "\n".join(line.rstrip(" \t") for line in license_lines) + "\n", encoding="utf-8"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fontforge", required=True)
    parser.add_argument("--rez", required=True)
    parser.add_argument("--atkinson", type=Path, required=True, help="directory with static TTFs and OFL.txt")
    parser.add_argument("--source", type=Path, required=True, help="Adobe release's TTF directory")
    parser.add_argument("--source-license", type=Path, required=True)
    parser.add_argument("--plex", type=Path, required=True, help="IBM release's ibm-plex-sans directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary)
        (stage / "family.pe").write_text(FONTFORGE_SCRIPT)
        build_family(args, stage, "Atkinson Hyperlegible",
                     [args.atkinson / f"AtkinsonHyperlegible-{style}.ttf" for style in STYLES],
                     args.atkinson / "OFL.txt")
        build_family(args, stage, "Linewren Humanist",
                     [args.source / f"SourceSans3-{style}.ttf"
                      for style in ("Regular", "Bold", "It", "BoldIt")],
                     args.source_license)
        build_family(args, stage, "Linewren Grotesk",
                     [args.plex / "fonts/complete/ttf" / f"IBMPlexSans-{style}.ttf"
                      for style in STYLES],
                     args.plex / "LICENSE.txt")


if __name__ == "__main__":
    main()
