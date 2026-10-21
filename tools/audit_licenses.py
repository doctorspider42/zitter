#!/usr/bin/env python3
"""Inventory a macOS bundle, concrete Ninja link inputs, and GN license candidates.

Does not certify distribution compliance: metadata errors are preserved and the
GN graph includes generators as well as runtime dependencies. Dead stripping
cannot be inferred from the presence of an archive on a linker command line.
"""
import argparse
import ast
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAGIC = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def parse_metadata(text):
    rows, diagnostics = [], []
    for line in text.splitlines():
        if line.startswith("{'Name':"):
            rows.append(ast.literal_eval(line))
        elif line.strip():
            diagnostics.append(line)
    if not rows:
        raise ValueError('Chromium license scanner returned no component metadata')
    return rows, diagnostics


def ninja_tokens(value):
    return [p.replace("\0", " ").replace("$:", ":").replace("$$", "$")
            for p in value.replace("$ ", "\0").split()]


def link_records(out):
    records = []
    for path in (out / 'obj').rglob('*.ninja'):
        text = path.read_text(errors="replace")
        for match in re.finditer(r'^build (.+?): (?:link|solink|solink_module) (.+)$', text, re.M):
            outputs = ninja_tokens(match[1])
            inputs = ninja_tokens(match[2].split(' |')[0])
            inputs += re.findall(r'^  rlibs = (.*)$', text[match.end():], re.M)[0].split() if re.search(r'^  rlibs = ', text[match.end():], re.M) else []
            libraries = re.search(r'^  libs = (.*)$', text[match.end():], re.M)
            if libraries:
                inputs += [p for p in ninja_tokens(libraries[1]) if p.endswith(('.a', '.rlib', '.dylib'))]
            records.append({'ninja': str(path.relative_to(out)), 'outputs': outputs, 'inputs': inputs})
    return records


def inventory(app):
    files, hashes, binaries = [], {}, []
    for p in sorted(app.rglob('*')):
        rel = str(p.relative_to(app))
        if p.is_symlink():
            files.append({'path': rel, 'symlink': str(p.readlink())})
        elif p.is_file():
            st = p.stat()
            key = (st.st_dev, st.st_ino)
            if key not in hashes:
                hashes[key] = sha(p)
            row = {'path': rel, 'bytes': st.st_size, 'sha256': hashes[key]}
            files.append(row)
            with p.open('rb') as stream:
                macho = stream.read(4) in MAGIC
            if macho:
                uuid = subprocess.check_output(['dwarfdump', '--uuid', str(p)], text=True).splitlines()
                # otool-classic interprets parentheses in helper filenames as archive syntax.
                with tempfile.TemporaryDirectory(prefix='zitter-macho-') as temporary:
                    alias = Path(temporary) / 'binary'
                    alias.symlink_to(p)
                    deps = subprocess.check_output(['otool', '-L', str(alias)], text=True).splitlines()[1:]
                binaries.append({**row, 'uuid': [l.split()[1] for l in uuid], 'load_commands': [l.strip() for l in deps]})
    return files, binaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--target', default='//zitter_optimized:zitter_optimized')
    parser.add_argument('--destination', type=Path, default=ROOT / 'build/license-audit/current')
    args = parser.parse_args()
    out, app, dest = args.out.resolve(), args.app.resolve(), args.destination.resolve()
    src = ROOT / '.chromium/src'
    dest.mkdir(parents=True, exist_ok=True)
    links = link_records(out)
    files, binaries = inventory(app)
    # Main, framework, helpers and all bundled dylibs (including dlopen-only
    # libraries). Match LC_UUID, which survives packaging/codesign.
    by_name = {}
    for link in links:
        for output in link['outputs']:
            by_name.setdefault(Path(output).name, []).append(link)
    actual_links, unmatched = [], []
    seen = set()
    for binary in binaries:
        signature = tuple(binary['uuid'])
        if signature in seen:
            continue
        seen.add(signature)
        candidates = by_name.get(Path(binary['path']).name, [])
        if binary['path'] == 'Contents/MacOS/Chromium':
            candidates = by_name.get(args.target.split(':')[-1], [])
        matched = []
        for link in candidates:
            for output in link['outputs']:
                p = out / output
                if not p.is_file() or p.suffix in ('.TOC', '.dSYM'):
                    continue
                result = subprocess.run(['dwarfdump', '--uuid', str(p)], text=True, capture_output=True)
                uuids = [l.split()[1] for l in result.stdout.splitlines() if l.startswith('UUID:')]
                if uuids == binary['uuid']:
                    matched.append(link)
                    break
        if matched:
            actual_links.extend(matched)
        else:
            unmatched.append(binary['path'])
    targets = {args.target, '//chrome:chrome_framework'}
    for link in actual_links:
        p = Path(link['ninja'])
        targets.add('//' + str(p.parent.relative_to('obj')) + ':' + p.stem)
    def scan(target):
        result = subprocess.run(['python3', str(src / 'tools/licenses/licenses.py'), 'list',
                                 '--gn-out-dir', str(out), '--gn-target', target, '--target-os', 'mac',
                                 '--shipped-only', '--verbose', '--enable-warnings'], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(target + ': ' + result.stdout + result.stderr)
        return target, parse_metadata(result.stdout + result.stderr)
    components, diagnostics = {}, {}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for target, (rows, errors) in pool.map(scan, sorted(targets)):
            diagnostics[target] = errors
            for row in rows:
                key = (row['dir'], row['Name'])
                entry = components.setdefault(key, {**row, 'reachable_from': [], 'link_evidence': []})
                entry['reachable_from'].append(target)
    supplements = json.loads((ROOT / 'docs/licenses/supplements.json').read_text())
    for row in supplements['components']:
        components[(row['dir'], row['Name'])] = {**row, 'reachable_from': ['manual-metadata-review'], 'link_evidence': []}
    def license_path(value):
        return ROOT / value[6:] if value.startswith('@root/') else src / value
    for entry in components.values():
        prefix = 'obj/' + entry['dir'] + '/'
        if entry['dir']:
            entry['link_evidence'] = sorted({i for link in actual_links for i in link['inputs'] if i.startswith(prefix)})
        notices = sorted({str(p.relative_to(src)) for file in entry['License File'] if not file.startswith('@root/') for name in ('NOTICE', 'NOTICE.txt', 'NOTICE.md') for p in [(src / file).parent / name] if p.is_file()})
        entry['notice_files'] = [{'path': p, 'sha256': sha(src / p)} for p in notices]
        entry['license_files'] = [{'path': p, 'sha256': sha(license_path(p))} for p in entry['License File'] if p != 'NOT_SHIPPED']
    manifest = {'reviewed_supplements': supplements, 'chromium_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=src, text=True).strip(),
                'target': args.target, 'gn_args': (out / 'args.gn').read_text(),
                'scope': 'Packaged files and UUID-matched linker inputs; conservative GN shipped metadata includes build tools. Link inputs do not establish which sections survive ThinLTO/dead stripping. Diagnostics and unmatched binaries prevent treating this as a complete release compliance certificate.',
                'components': sorted(components.values(), key=lambda x: (x['Name'].lower(), x['dir'])),
                'metadata_diagnostics': diagnostics, 'unmatched_binaries': unmatched,
                'binaries': binaries, 'link_records': actual_links, 'files': files}
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    with (dest / 'THIRD_PARTY_LICENSES.txt').open('w') as stream:
        stream.write('GN-reachable third-party license collection; see manifest.json for scope and unresolved metadata.\n')
        for row in manifest['components']:
            stream.write('\n' + '=' * 80 + '\n' + row['Name'] + '\n' + row['dir'] + '\n' + row['License'] + '\n')
            for file in row['License File'] + [n['path'] for n in row['notice_files']]:
                if file != 'NOT_SHIPPED':
                    stream.write('\n' + file + '\n' + license_path(file).read_text(errors='replace') + '\n')
    print(json.dumps({'destination': str(dest), 'components': len(components), 'macho_files': len(binaries),
                      'unique_binaries': len(seen), 'matched_link_rules': len(actual_links),
                      'unmatched_binaries': unmatched, 'targets': sorted(targets)}, indent=2))


if __name__ == '__main__':
    main()
