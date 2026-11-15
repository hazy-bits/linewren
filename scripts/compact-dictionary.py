"""Select MySpell entries by the frequency of the forms they recognize."""

import argparse
import codecs
import heapq
import math
import re
from collections import defaultdict
from pathlib import Path

CONDITION_TOKEN = re.compile(r"\[\^?[^\[\]\\]+\]|[^\[\]\\]")
SAFE_DIRECTIVES = {"SET", "TRY", "REP", "MAP", "NOSPLITSUGS"}
MYSPELL_ENCODINGS = {
    **{f"ISO8859-{number}": f"iso8859-{number}" for number in (*range(1, 11), 14)},
    "KOI8-R": "koi8-r",
    "CP-1251": "cp1251",
}


def condition_pattern(token):
    if not token.startswith("["):
        return token if token == "." else re.escape(token)
    members = token[1:-1]
    negated = "^" if members.startswith("^") else ""
    return "[" + negated + re.escape(members.lstrip("^")) + "]"


def read_affixes(lines):
    rules = {"PFX": defaultdict(list), "SFX": defaultdict(list)}
    rows = iter(lines)
    for line in rows:
        fields = line.split()
        kind = fields[0]
        if kind in SAFE_DIRECTIVES:
            continue
        if kind not in rules or len(fields) != 4:
            raise ValueError(f"Unsupported affix directive: {line}")
        _, flag, cross, count = fields
        if len(flag) != 1 or cross not in {"Y", "N"} or not count.isdecimal():
            raise ValueError(f"Invalid affix header: {line}")
        for _ in range(int(count)):
            row = next(rows, "").split()
            if len(row) != 5 or row[:2] != [kind, flag]:
                raise ValueError(f"Incomplete affix block: {line}")
            _, _, strip, append, condition = row
            tokens = CONDITION_TOKEN.findall(condition)
            if "/" in append or "".join(tokens) != condition or len(tokens) > 8:
                raise ValueError(f"Unsupported affix rule: {' '.join(row)}")
            strip_length = 0 if strip == "0" else len(strip)
            append = "" if append == "0" else append
            # MySpell treats a lone dot as no condition.
            pattern = "" if condition == "." else "".join(map(condition_pattern, tokens))
            edge = r"\A" + pattern if kind == "PFX" else pattern + r"\Z"
            rules[kind][flag].append((strip_length, append, re.compile(edge), cross == "Y"))
    return rules


def affixed(word, flags, rules, prefix, cross_only=False):
    for flag in flags:
        for strip, append, condition, cross in rules.get(flag, ()):
            if (not cross_only or cross) and len(word) > strip and condition.search(word):
                form = append + word[strip:] if prefix else word[:len(word) - strip] + append
                yield form, cross


def known_forms(root, flags, rules, weights):
    forms = {root.lower()}
    for suffix, cross in affixed(root, flags, rules["SFX"], False):
        forms.add(suffix.lower())
        if cross:
            forms.update(form.lower() for form, _ in affixed(suffix, flags, rules["PFX"], True, True))
    forms.update(form.lower() for form, _ in affixed(root, flags, rules["PFX"], True))
    return {form for form in forms if form in weights}


def select_entries(entries, rules, weights, limit):
    forms = [known_forms(*entry, rules, weights) for entry in entries]
    queue = [(-sum(weights[word] for word in group), i) for i, group in enumerate(forms)]
    heapq.heapify(queue)
    selected = []
    covered = set()
    while len(selected) < limit:
        estimate, i = heapq.heappop(queue)
        new = forms[i] - covered
        score = sum(weights[word] for word in new)
        if score != -estimate:
            heapq.heappush(queue, (-score, i))
            continue
        selected.append(i)
        covered.update(new)
    return sorted(selected), covered


def read_frequencies(args):
    if args.wordfreq:
        from wordfreq import get_frequency_dict

        values = get_frequency_dict(args.wordfreq).items()
    else:
        values = (line.split("\t") for line in args.frequencies.read_text(encoding="utf-8").splitlines()
                  if line and not line.startswith("#"))
    pattern = re.compile(args.word_pattern)
    weights = defaultdict(int)
    for word, frequency in values:
        word = word.lower()
        frequency = float(frequency)
        if not math.isfinite(frequency) or frequency < 0:
            raise ValueError(f"Invalid frequency for {word}")
        if frequency and pattern.fullmatch(word):
            weights[word] += max(1, round(frequency * 10**13))
    if not weights:
        raise ValueError("No positive frequencies matched the word pattern")
    return weights


def compact(args):
    encoding = MYSPELL_ENCODINGS.get(args.charset)
    if encoding is None or codecs.lookup(args.output_encoding).name != encoding:
        raise ValueError("Choose a matching MySpell charset and Python encoding")
    lines = [line for line in args.aff.read_text(encoding=args.source_encoding).splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if sum(line.split()[0] == "SET" for line in lines) != 1:
        raise ValueError("The affix file must contain exactly one SET directive")
    rules = read_affixes(lines)
    count, *rows = args.dic.read_text(encoding=args.source_encoding).splitlines()
    if int(count) != len(rows) or not 0 < args.limit <= len(rows):
        raise ValueError("Check the dictionary header and requested entry count")
    entries = []
    for row in rows:
        root, _, flags = row.partition("/")
        if not root or any(ch.isspace() or ch == "\\" for ch in row) or any(
            flag not in rules["SFX"] and flag not in rules["PFX"] for flag in flags
        ):
            raise ValueError(f"Unsupported dictionary entry: {row}")
        entries.append((root, flags))

    weights = read_frequencies(args)
    selected, covered = select_entries(entries, rules, weights, args.limit)
    lines = [f"SET {args.charset}" if line.split()[0] == "SET" else line for line in lines]
    aff = ("\n".join(lines) + "\n").encode(encoding)
    body = "\n".join(rows[i] for i in selected)
    dic = f"{len(selected)}\n{body}\n".encode(encoding)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / args.aff.name).write_bytes(aff)
    (args.output / args.dic.name).write_bytes(dic)
    coverage = sum(weights[word] for word in covered) / sum(weights.values())
    print(f"{len(selected):,} entries cover {len(covered):,} ranked forms ({coverage:.2%} of input frequency mass)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aff", type=Path)
    parser.add_argument("dic", type=Path)
    parser.add_argument("output", type=Path, help="write the compact pair to a separate directory")
    parser.add_argument("--limit", type=int, default=50_000)
    parser.add_argument("--source-encoding", default="utf-8")
    parser.add_argument("--output-encoding", required=True, help="Python codec for the compact pair")
    parser.add_argument("--charset", required=True, help="MySpell SET name for the same encoding")
    parser.add_argument("--word-pattern", default=r"[^\W\d_]+(?:[-'’][^\W\d_]+)*",
                        help="regular expression for ranked forms")
    frequencies = parser.add_mutually_exclusive_group(required=True)
    frequencies.add_argument("--wordfreq", metavar="LANG", help="use the installed wordfreq dataset")
    frequencies.add_argument("--frequencies", type=Path, help="UTF-8 TSV with word and frequency columns")
    args = parser.parse_args()
    if any((args.output / source.name).resolve() == source.resolve() for source in (args.aff, args.dic)):
        parser.error("choose a different output directory to preserve the original dictionary")
    try:
        compact(args)
    except (ImportError, LookupError, OSError, ValueError, re.error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
