#!/usr/bin/env python3
"""Extract six unscaled guest screenshots from LaunchAPPL's specimen output."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

FAMILIES = ("atkinson-hyperlegible", "linewren-humanist", "linewren-grotesk")
THEMES = ("light", "dark")
PPM_HEADER = re.compile(rb"P6\n([1-9][0-9]*) ([1-9][0-9]*)\n255\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.capture.read_bytes()
    position = 0
    frames = {}
    for theme in THEMES:
        for family in FAMILIES:
            header = PPM_HEADER.match(data, position)
            if not header:
                raise SystemExit(f"Missing {family} {theme} frame. {data[position:position + 120]!r}")
            width, height = map(int, header.groups())
            end = header.end() + 3 * width * height
            if end > len(data):
                raise SystemExit(f"Truncated {family} {theme} frame")
            frames[f"{family}-{theme}"] = data[position:end]
            position = end
    report = data[position:].decode("ascii").strip()
    if not report.startswith("PASS"):
        raise SystemExit(report or "The guest did not report completion")

    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        ppm = Path(temporary) / "specimen.ppm"
        for name, frame in frames.items():
            ppm.write_bytes(frame)
            subprocess.run(["sips", "-s", "format", "png", ppm,
                            "--out", args.output / f"{name}.png"],
                           stdout=subprocess.DEVNULL, check=True)
    print(report)


if __name__ == "__main__":
    main()
