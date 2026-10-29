#!/usr/bin/env python3
"""Checks the UI language catalogue: every Polish string wrapped in t()/tf()
(Rust) or T()/TF() (C++) has an English entry, and no catalogue holds an entry
twice. With --missing it lists the untranslated strings per source file."""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PROTOTYPE = ROOT / "chromium/prototype"
LITERAL = r'"((?:[^"\\\n]|\\.)*)"'
# t("…"), tf("…", …) in Rust; T(u"…"), TF(u"…", …) in C++.
RUST_USE = re.compile(r'(?<![A-Za-z0-9_])tf?\(\s*' + LITERAL)
CXX_USE = re.compile(r'(?<![A-Za-z0-9_])TF?\(\s*u' + LITERAL)
ENTRY = re.compile(r'^\s*\(\s*' + LITERAL + r'\s*,\s*' + LITERAL + r'\s*\),?\s*$')
CATALOGUES = {"scene": "i18n_en_scene.rs", "shell": "i18n_en_shell.rs", "native": "i18n_en_native.rs"}


def catalogue(name):
    entries = {}
    duplicates = []
    for line in (PROTOTYPE / CATALOGUES[name]).read_text(encoding="utf-8").splitlines():
        if match := ENTRY.match(line):
            polish, english = match.groups()
            if polish in entries:
                duplicates.append(polish)
            entries[polish] = english
    return entries, duplicates


# The macOS menu lives in the app target.
EXTRA = [ROOT / "chromium/app/app_shell_mac.mm"]


def sources():
    for path in EXTRA:
        yield path, CXX_USE
    for path in sorted(PROTOTYPE.iterdir()):
        if path.name.startswith("i18n") or "test" in path.name:
            continue
        if path.suffix == ".rs":
            yield path, RUST_USE
        elif path.suffix in (".cc", ".mm", ".h"):
            yield path, CXX_USE


def uses():
    found = {}
    for path, pattern in sources():
        text = path.read_text(encoding="utf-8")
        # Rust unit tests may quote UI strings without translating them.
        if path.suffix == ".rs":
            text = re.split(r'\n#\[cfg\(test\)\]\s*\nmod ', text)[0]
        for match in pattern.finditer(text):
            found.setdefault(match.group(1), set()).add(path.name)
    return found


def missing():
    known = {}
    for name in CATALOGUES:
        known.update(catalogue(name)[0])
    return {text: files for text, files in uses().items() if text not in known}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--missing", action="store_true", help="list untranslated strings")
    args = parser.parse_args()
    gaps = missing()
    duplicates = [d for name in CATALOGUES for d in catalogue(name)[1]]
    if args.missing:
        for text, files in sorted(gaps.items(), key=lambda item: (sorted(item[1]), item[0])):
            print(f'{",".join(sorted(files))}\t"{text}"')
    for d in duplicates:
        print(f'duplicate entry: "{d}"', file=sys.stderr)
    print(f"{len(uses())} wrapped strings, {len(gaps)} without English", file=sys.stderr)
    return 1 if gaps or duplicates else 0


if __name__ == "__main__":
    raise SystemExit(main())
