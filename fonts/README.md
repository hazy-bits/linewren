# Bundled fonts for Linewren on Mac OS 9

The ATSUI build makes these families available in the **Font** menu while
Linewren runs. Install the standalone suitcases if you want to use the fonts
in other classic Mac applications.

Expand each MacBinary `.bin` with StuffIt Expander on Mac OS 9. Drag the
resulting suitcase onto the System Folder icon, then relaunch Linewren. The
installed family appears in the Font menu. Each suitcase contains Regular,
Italic, Bold, and Bold Italic.

| Font menu name | Upstream static TrueType release |
| --- | --- |
| Atkinson Hyperlegible | [Braille Institute's original family, Google Fonts revision 95f4904](https://github.com/google/fonts/tree/95f4904fc8bcf26d3420fe315560c96417c6dec7/ofl/atkinsonhyperlegible) |
| Linewren Humanist | [Adobe Source Sans 3, 3.052R](https://github.com/adobe-fonts/source-sans/releases/tag/3.052R) |
| Linewren Grotesk | [IBM Plex Sans, package 1.1.0](https://github.com/IBM/plex/releases/tag/%40ibm%2Fplex-sans%401.1.0) |

The accompanying OFL files contain the upstream copyright and license notices.
Linewren Humanist and Linewren Grotesk are named separately to respect the Reserved
Font Names in their upstream licenses. The suitcases add classic Mac family
associations and MacRoman naming and encoding where needed. The source glyph
outlines, TrueType hints, and layout tables are retained. The Linewren Humanist
character map uses the upstream Basic Multilingual Plane mappings for
compatibility with classic ATSUI.

To regenerate the suitcases on modern macOS, extract the upstream static
TrueType downloads and use the script below. It uses FontForge for the
classic family association and Retro68 Rez to write each MacBinary package.
The script takes the source folders and the upstream Adobe license as inputs.

```sh
python3 -m venv /tmp/linewren-font-tools
/tmp/linewren-font-tools/bin/pip install fonttools
/tmp/linewren-font-tools/bin/python scripts/build-font-suitcases.py --help
```
