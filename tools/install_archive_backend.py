#!/usr/bin/env python3
"""Replace the pinned Chromium fork's UnRAR dependency with libarchive.

Preserves Safe Browsing's sandboxed reader/writer delegate pipeline. Legacy
USE_UNRAR/buildflag names mean RAR capability, not the proprietary backend.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    'chrome/utility/safe_browsing/BUILD.gn',
    'chrome/common/safe_browsing/BUILD.gn',
    'chrome/services/file_util/BUILD.gn',
    'chrome/utility/safe_browsing/rar_analyzer.h',
    'chrome/utility/safe_browsing/rar_analyzer.cc',
    'chrome/utility/safe_browsing/archive_analysis_delegate.h',
    'chrome/services/file_util/regular_archive_analysis_delegate.h',
    'chrome/services/file_util/regular_archive_analysis_delegate.cc',
    'chrome/services/file_util/obfuscated_archive_analysis_delegate.h',
    'chrome/services/file_util/obfuscated_archive_analysis_delegate.cc',
]


def patch(name, text):
    if name.endswith('.gn') and text.startswith('// Zitter: libarchive replacement'):
        text = '#' + text[2:]
    original = text
    text = text.replace('third_party/unrar/google/unrar_wrapper.h', 'third_party/zitter_libarchive/rar_reader.h')
    text = text.replace('third_party/unrar/google/unrar_delegates.h', 'third_party/zitter_libarchive/rar_delegates.h')
    text = text.replace('"//third_party/unrar"', '"//third_party/zitter_libarchive:rar_reader"')
    text = text.replace('third_party_unrar', 'third_party_libarchive')
    if name.endswith('/rar_analyzer.cc'):
        anchor = '  if (reader_.HasWriteError()) {'
        if anchor in text and 'reader_.HasReadError()' not in text:
            text = text.replace(anchor, '''  // Zitter: unsupported/encrypted/corrupt input is not a clean scan.
  if (reader_.HasReadError()) {
    results()->analysis_result = ArchiveAnalysisResult::kUnknown;
    results()->success = false;
    return true;  // Analysis finished, no nested async work is pending.
  }

''' + anchor, 1)
            text = text.replace('results()->analysis_result = ArchiveAnalysisResult::kDiskError;\n    results()->success = false;\n    return false;', 'results()->analysis_result = ArchiveAnalysisResult::kDiskError;\n    results()->success = false;\n    return true;', 1)
    if 'third_party/unrar' in text or 'third_party_unrar' in text:
        raise ValueError('UnRAR reference remains: ' + name)
    if text == original and 'Zitter: libarchive replacement' not in text:
        raise ValueError('Unexpected upstream layout: ' + name)
    if 'Zitter: libarchive replacement' not in text:
        text = ('# ' if name.endswith('.gn') else '// ') + 'Zitter: libarchive replacement for UnRAR (2026); upstream BSD license retained.\n' + text
    return text


def install(checkout):
    expected = json.loads((ROOT / 'chromium/revision.json').read_text())['revision']
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=checkout, text=True).strip()
    if actual != expected:
        raise ValueError('Unreviewed Chromium revision')
    prepared = {name: patch(name, (checkout / name).read_text()) for name in FILES}
    destination = checkout / 'third_party/zitter_libarchive'
    marker = destination / '.zitter-archive.json'
    if destination.exists():
        old = json.loads(marker.read_text())
        actual_files = {str(p.relative_to(destination)) for p in destination.rglob('*') if p.is_file() and p != marker}
        if actual_files != set(old['files']) or any(hashlib.sha256((destination / n).read_bytes()).hexdigest() != h for n, h in old['files'].items()):
            raise ValueError('Archive overlay has local edits; preserve them first')
    destination.mkdir(parents=True, exist_ok=True)
    if marker.exists():
        for name in old['files']:
            if not (ROOT / 'third_party/libarchive' / name).is_file():
                (destination / name).unlink()
    hashes = {}
    for source in sorted((ROOT / 'third_party/libarchive').iterdir()):
        if source.is_file():
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            target = destination / source.name
            if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                shutil.copyfile(source, target)
            hashes[source.name] = digest
    marker.write_text(json.dumps({'files': hashes}, indent=2) + '\n')
    for name, text in prepared.items():
        if text != (checkout / name).read_text():
            (checkout / name).write_text(text)
    print(f'Installed libarchive 3.8.9; replaced UnRAR in {len(FILES)} Chromium integration files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path)
    args = parser.parse_args()
    install(args.checkout.resolve())
