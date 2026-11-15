# Spelling settings and dictionary packs

Linewren uses **Edit > Spelling** on Mac OS 9, Tiger, and Leopard.

- **Spelling…** opens the review dialog with suggestions, Change, Ignore,
  and Find Next.
- **Check Spelling** marks the current document on demand.
- **Check Spelling as You Type** is off by default. It rechecks after a
  typing pause and retains marks outside the edit while waiting.

Open the language picker in the spelling dialog or beside the word count.
Changing the language in the dialog rechecks the document immediately.
Right-click or Control-click an underlined word for corrections.
The Classic app uses MySpell with bundled US English and Russian. Leopard lists the
languages reported by the system spelling service. On Tiger, Linewren
reads the language declarations in installed spelling service bundles
and checks whether the system accepts each language. If no services
advertise a usable list, the picker shows the current system language.

Each document has its own spelling language. Linewren remembers the
choice locally using the file's catalog identity on Mac OS 9 and its
path on Tiger and Leopard. An untitled document keeps its choice when
first saved.

## Classic dictionary installation

Publish versioned dictionary ZIPs as separate GitHub release assets and
link them from the project's downloads page. Users can download on a
modern machine, unzip, and copy a language folder to **Dictionaries**
beside Linewren. The files are plain data and can be transferred without
MacBinary. The status bar picker can list installed languages when pack
loading is implemented. Load one dictionary at a time.

A pack should contain a matched MySpell `.aff` and `.dic` pair, the
original license and attribution, and a short manifest with its locale,
display name, encoding, upstream version, and any adaptations. Pin and
hash the source archive in the pack build. Validate affix features,
encoding and dictionary size before loading. If a pack fails to load,
keep the previous language selected and explain the failure. Check input,
capitalization, inflections, and suggestions in the guest. Measure
memory for each pack, especially Russian. The earlier 146,269-entry
Russian probe used 13.1 MiB after loading, beyond the current 8 MiB
preferred editor partition.

## OpenOffice archive audit

These entries describe the notices in the exact archived ZIPs. The
OpenOffice index hosts dictionaries under several licenses. A candidate
needs its own notices, a pinned source, and a Classic test before release.

| Archive | Declared encoding | License in the archive | Packaging decision |
| --- | --- | --- | --- |
| [en_GB.zip](https://ftp5.gwdg.de/pub/openoffice/contrib/dictionaries/en_GB.zip) | ISO-8859-1 | LGPL for the word list and affix file. The README gives no LGPL version. | Prefer a prepared ESDB British English pack with its upstream notices. |
| [es_ES.zip](https://ftp5.gwdg.de/pub/openoffice/contrib/dictionaries/es_ES.zip) | ISO-8859-1 | LGPL for both dictionary files. The README links to the generic GNU license page. | Confirm the version and source before republishing. |
| [de_DE.zip](https://ftp5.gwdg.de/pub/openoffice/contrib/dictionaries/de_DE.zip) | ISO-8859-1 | GPL version 2, with the license text in the README. | Treat as a separate GPL pack. Check the corresponding source and distribution terms first. |
| [ru_RU.zip](https://ftp5.gwdg.de/pub/openoffice/contrib/dictionaries/ru_RU.zip) | KOI8-R | The notice allows copying and redistribution, and specifies patches for distributing source modifications. | Use the unchanged pair if it passes, or use a pinned LibreOffice revision whose notice explicitly permits modified redistribution with attribution and marking. |

[OpenOffice dictionary publishing guidance](https://www.openoffice.org/lingucomponent/dictionary.html) ·
[ESDB downloads](https://wordlist.aspell.net/dicts/) ·
[LibreOffice Russian dictionary notice](https://github.com/LibreOffice/dictionaries/blob/master/ru_RU/README_ru_RU.txt)

## Build a compact MySpell dictionary

Run [the generator](../scripts/compact-dictionary.py) on a modern host with
Python 3. It selects dictionary entries by the total frequency of the
forms they recognize. Each round gives credit only for forms that earlier
entries have yet to cover. The output keeps the original flags, source
order, and matching affix rules.

For another language, start with a matched MySpell pair and a frequency
list licensed for your intended use. Use `--frequencies` with a UTF-8 TSV
containing one word and a positive numeric frequency per row, without a
header. Alternatively, install [wordfreq](https://github.com/rspeer/wordfreq)
and use `--wordfreq` with its language code. Use `--word-pattern` if you
need to limit the ranking to a particular script.

```sh
python3 scripts/compact-dictionary.py source/fr.aff source/fr.dic compact-fr \
  --frequencies fr-frequency.tsv --limit 50000 \
  --output-encoding iso8859-1 --charset ISO8859-1
```

Set `--source-encoding` for a source pair that uses an encoding other
than UTF-8. Choose an output encoding and its corresponding MySpell
`SET` name. The example uses Latin-1 and requires that the source rules
and selected entries can be encoded in Latin-1.

The script accepts single-character flags, basic prefix and suffix rules,
and prefix–suffix combinations. Modern Hunspell features such as
continuation flags, flag aliases, and compound directives need separate
adaptation and testing. The generator rejects unsupported directives and
entries so they can be reviewed before conversion.

### Reproduce the Russian pilot

From the repository root, fetch the pinned LibreOffice sources and check
their SHA-256 hashes. The command below uses `uv` to run with the same
wordfreq version as the pilot.

```sh
source_dir=$(mktemp -d)
base=https://raw.githubusercontent.com/LibreOffice/dictionaries
curl -fL "$base/12e591abdbc5565e5018ed9b8fbe6d635ade87d7/ru_RU/ru_RU.aff" \
  -o "$source_dir/ru_RU.aff"
curl -fL "$base/30dd499a102c930b6386d9b0b44ed13e9d4cf8d2/ru_RU/ru_RU.dic" \
  -o "$source_dir/ru_RU.dic"
(cd "$source_dir" && shasum -a 256 -c <<'EOF'
38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7  ru_RU.aff
f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc  ru_RU.dic
EOF
) && uv run --with wordfreq==3.1.1 python scripts/compact-dictionary.py \
  "$source_dir/ru_RU.aff" "$source_dir/ru_RU.dic" compact-ru \
  --limit 50000 --wordfreq ru --word-pattern '[а-яё]+(?:-[а-яё]+)*' \
  --output-encoding koi8-r --charset KOI8-R
```

The pilot covers 96.30% of the frequency mass in its ranking data. The
standalone benchmark passed on Mac OS 9.2.2 in UTM and used 4.89 MiB of
live heap after loading, with a load time of 1.69 seconds. The benchmark
requested an 8 MiB partition. Peak heap use and memory alongside the
editor still need measurement.

Before distributing a new pack, retain its upstream notices, record the
pinned source and ranking data versions, and review their redistribution
terms. Test coverage on writing outside the ranking corpus and measure
heap use, capitalization, inflections, and corrections in the guest.
Run the generator's offline regression test with
`python3 tests/compact-dictionary-test.py`.

## Initial release candidates

US English and the 50,000-entry Russian dictionary are bundled.
ESDB 2026.02.25 provides British English
(both -ise and -ize), Canadian English, and Australian English under its
MIT-like terms. Keep the full upstream notices and validate each adapted
pack in the guest before distributing it. A LibreOffice Russian revision
with the Lebedev notice is a candidate for a separate pack. Its notice
permits modification and redistribution with attribution and marking.

Spanish and German are later candidates. The audited Spanish archive needs
a confirmed LGPL version and source. The audited German pack needs its
GPLv2 notices and corresponding source reviewed for standalone release.
