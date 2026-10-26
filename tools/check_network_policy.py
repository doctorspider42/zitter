#!/usr/bin/env python3
"""Validate policy feature names against the pinned Chromium definitions."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(checkout):
    expected = json.loads((ROOT / 'chromium/revision.json').read_text())['revision']
    actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
    if expected != actual:
        raise ValueError('Chromium revision differs from the lock')
    manifest = (ROOT / 'chromium/app/network_policy_features.inc').read_text()
    entries = re.findall(r'ZITTER_NETWORK_FEATURE\("([^"]+)", "([^"]+)", (\w+)\)', manifest)
    if not entries or len({name for name, _, _ in entries}) != len(entries):
        raise ValueError('Empty or duplicate network policy features')
    for name, filename, symbol in entries:
        source = (checkout / filename).read_text()
        definition = re.search(r'BASE_FEATURE\(\s*' + re.escape(symbol) + r'\s*,\s*(?:"([^"]+)"\s*,)?', source)
        if not definition or (definition[1] or symbol.removeprefix('k')) != name:
            raise ValueError(f'Feature definition changed: {filename}: {symbol} / {name}')
    signin_source = (checkout / 'components/signin/public/base/signin_switches.cc').read_text()
    policy_source = (ROOT / 'chromium/app/network_policy.cc').read_text()
    for name in ['SkipListAccountsWithoutGaiaCookies', 'AvoidAutoTriggerListAccountsOnStale']:
        if f'"{name}"' not in policy_source or not re.search(r'BASE_FEATURE\(\s*k' + name + r'\s*,', signin_source):
            raise ValueError(f'Account-request feature definition changed: {name}')
    components = (ROOT / 'chromium/app/network_policy_components.inc').read_text()
    ids = re.findall(r'ZITTER_NETWORK_COMPONENT\("([a-p]{32})",', components)
    protected = {'hfnkpimlhhgieaddgfemjhofmfblmnib', 'efniojlnjndmcbiieegkicadnoecjjef',
                 'khaoiebndkojlmppeemjhbpbandiljpe', 'jflookgnkcckhobaglndicnbbgbonegd',
                 'gcmjkmgdlgnkkcocmoeiminaijmmjnii', 'ggkkehgbnfjpeggfpleeakpidbkibbmn'}
    if not ids or protected.intersection(ids) or len(ids) != len(set(ids)):
        raise ValueError('Invalid or security-sensitive component exclusion')
    print(f'Validated {len(entries)} feature definitions and {len(ids)} non-security component exclusions')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkout', type=Path, default=ROOT / '.chromium/src')
    args = parser.parse_args()
    try:
        check(args.checkout)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Network policy validation failed: {error}\n')
