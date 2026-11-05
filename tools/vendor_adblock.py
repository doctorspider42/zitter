#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Vendor Brave's adblock-rust and the crates Chromium lacks into third_party/rust.

Downloads the pinned .crate archives from crates.io, checks their SHA-256 against
the pins below (the checksums of Cargo.lock), unpacks the sources unmodified and
writes third_party/rust/BUILD.gn with `cargo_crate` targets. The overlay installs
the directory as //third_party/rust/zitter_vendor, where Chromium's own crates
(regex-automata, memchr, serde, ICU…) are visible to it.

Usage: python tools/vendor_adblock.py [--check]
  --check  verify the vendored tree and BUILD.gn match the pins, download nothing.
"""
import argparse
import hashlib
import io
from pathlib import Path
import shutil
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'third_party/rust'
LABEL = '//third_party/rust/zitter_vendor'
CHROMIUM = '//third_party/rust'
KEEP = ('src/', 'Cargo.toml', 'LICENSE', 'LICENSE-APACHE', 'LICENSE-MIT', 'COPYRIGHT', 'README.md')

# name, version, sha256, edition, epoch, features, deps (vendored names or Chromium
# labels), crate_type, allow_unsafe. Versions of Chromium's crates must satisfy
# these crates' requirements; see docs/ad-blocking.md.
CRATES = [
    ('adblock', '0.13.5', '7a4c8558f27608dec74695c866f9a8ef897ec7a77cb826f8a5baf0b8ba461d2a', '2024', '0.13',
     ['full-regex-handling'],
     ['arrayvec', f'{CHROMIUM}/base64/v0_22:lib', 'bitflags', 'flatbuffers', 'idna', f'{CHROMIUM}/itertools/v0_14:lib',
      f'{CHROMIUM}/memchr/v2:lib', 'percent-encoding', 'regex', 'rustc-hash', 'seahash', f'{CHROMIUM}/serde/v1:lib',
      f'{CHROMIUM}/serde_json/v1:lib', 'thiserror'], 'rlib', True),
    ('arrayvec', '0.7.8', 'd3fb67a6e08acf24fdeccbac2cb6ac4305825bd1f117462e0e6f2f193345ad56', '2018', '0.7',
     ['default', 'std'], [], 'rlib', True),
    # Chromium's bitflags has no `serde` feature, which adblock needs.
    ('bitflags', '2.13.2', '3ded4057c258ba199e2d26386d3af3780957ecaee6c4ef4041c6b4b8b97c0b06', '2021', '2',
     ['serde', 'serde_core'], [f'{CHROMIUM}/serde_core/v1:lib'], 'rlib', False),
    ('flatbuffers', '25.12.19', '35f6839d7b3b98adde531effaf34f0c2badc6f4735d26fe74709d8e513a96ef3', '2018', '25',
     ['default', 'std'], ['bitflags'], 'rlib', True),
    ('idna', '1.1.0', '3b0875f23caa03898994f6ddc501886a45c7d3d62d04d2d90788d47be1b1e4de', '2018', '1',
     ['alloc', 'compiled_data', 'default', 'std'],
     ['idna_adapter', f'{CHROMIUM}/smallvec/v1:lib', f'{CHROMIUM}/utf8_iter/v1:lib'], 'rlib', True),
    ('idna_adapter', '1.2.2', 'cb68373c0d6620ef8105e855e7745e18b0d00d3bdb07fb532e434244cdb9a714', '2024', '1',
     ['compiled_data'], [f'{CHROMIUM}/icu_normalizer/v2:lib', f'{CHROMIUM}/icu_properties/v2:lib'], 'rlib', False),
    ('percent-encoding', '2.3.2', '9b4f627cb1b25917193a259e49bdad08f671f8d9708acfd5fe0a8c1455d87220', '2018', '2',
     ['alloc', 'default', 'std'], [], 'rlib', False),
    # Chromium keeps its `regex` façade test-only; its engine crates are shared.
    ('regex', '1.13.1', 'f020237b6c8eed93db2e2cb53c00c60a8e1bc73da7d073199a1180401450218d', '2021', '1',
     ['default', 'perf', 'perf-backtrack', 'perf-cache', 'perf-dfa', 'perf-inline', 'perf-literal', 'perf-onepass',
      'std', 'unicode', 'unicode-age', 'unicode-bool', 'unicode-case', 'unicode-gencat', 'unicode-perl',
      'unicode-script', 'unicode-segment'],
     [f'{CHROMIUM}/aho_corasick/v1:lib', f'{CHROMIUM}/memchr/v2:lib', f'{CHROMIUM}/regex_automata/v0_4:lib',
      f'{CHROMIUM}/regex_syntax/v0_8:lib'], 'rlib', True),
    ('rustc-hash', '2.1.3', '6b1e7f9a428571be2dc5bc0505c13fb6bf936822b894ec87abf8a08a4e51742d', '2021', '2',
     [], [], 'rlib', False),
    ('seahash', '4.1.0', '1c107b6f4780854c8b126e228ea8869f4d7b71260f962fefb57b996b8959ba6b', '2015', '4',
     ['default'], [], 'rlib', True),
    ('thiserror', '1.0.69', 'b6aaf5339b578ea85b50e080feb250a3e8ae8cfcdff9a461c9ec2904bc923f52', '2021', '1',
     [], ['thiserror-impl'], 'rlib', False),
    ('thiserror-impl', '1.0.69', '4fee6c4efc90059e10f81e6d42c60a18f76588c3d74cb83a0b242a2b6c7504c1', '2021', '1',
     [], [f'{CHROMIUM}/proc_macro2/v1:lib', f'{CHROMIUM}/quote/v1:lib', f'{CHROMIUM}/syn/v2:lib'], 'proc-macro', False),
]


def target(name):
    return name.replace('-', '_')


def label(dep):
    return dep if dep.startswith('//') else f'{LABEL}:{target(dep)}'


def unpack(name, version, sha256, data):
    if hashlib.sha256(data).hexdigest() != sha256:
        raise ValueError(f'{name} {version}: checksum mismatch')
    destination = VENDOR / name
    if destination.exists():
        shutil.rmtree(destination)
    prefix = f'{name}-{version}/'
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.startswith(prefix):
                continue
            relative = member.name[len(prefix):]
            if '..' in Path(relative).parts or not any(relative == keep or relative.startswith(keep) for keep in KEEP):
                continue
            path = destination / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            # Bytes as published: no newline translation.
            path.write_bytes(archive.extractfile(member).read())


def build_gn():
    lines = ['# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0',
             '# Generated by tools/vendor_adblock.py. Do not edit.',
             '# Installed at //third_party/rust/zitter_vendor by tools/install_chromium_overlay.py.',
             '', 'import("//build/rust/cargo_crate.gni")', '']
    for name, version, _, edition, epoch, features, deps, crate_type, unsafe in CRATES:
        base = f'{LABEL}/{name}'
        sources = sorted(p.relative_to(VENDOR / name).as_posix() for p in (VENDOR / name / 'src').rglob('*.rs'))
        lines.append(f'cargo_crate("{target(name)}") {{')
        lines.append(f'  crate_name = "{target(name)}"')
        lines.append(f'  epoch = "{epoch}"')
        lines.append(f'  crate_type = "{crate_type}"')
        lines.append(f'  crate_root = "{base}/src/lib.rs"')
        lines.append('  sources = [')
        lines.extend(f'    "{base}/{source}",' for source in sources)
        lines.append('  ]')
        lines.append('  inputs = []')
        lines.append('  build_native_rust_unit_tests = false')
        lines.append(f'  edition = "{edition}"')
        lines.append(f'  cargo_pkg_name = "{name}"')
        lines.append(f'  cargo_pkg_version = "{version}"')
        if unsafe:
            lines.append('  allow_unsafe = true')
        if deps:
            lines.append('  deps = [')
            lines.extend(f'    "{label(dep)}",' for dep in deps)
            lines.append('  ]')
        if features:
            lines.append('  features = [')
            lines.extend(f'    "{feature}",' for feature in features)
            lines.append('  ]')
        lines.append(f'  visibility = [ "//third_party/rust/*", "//zitter:*" ]')
        lines.append('  _configs_to_remove = [')
        lines.append('    "//build/config/coverage:default_coverage",')
        lines.append('    "//build/config/compiler:chromium_code",')
        lines.append('  ]')
        lines.append('  library_configs -= _configs_to_remove')
        lines.append('  executable_configs -= _configs_to_remove')
        lines.append('  proc_macro_configs -= _configs_to_remove')
        lines.append('}')
        lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not args.check:
        for name, version, sha256, *_ in CRATES:
            url = f'https://static.crates.io/crates/{name}/{name}-{version}.crate'
            with urllib.request.urlopen(url, timeout=60) as response:
                unpack(name, version, sha256, response.read())
            print(f'vendored {name} {version}')
        (VENDOR / 'BUILD.gn').write_bytes(build_gn().encode('utf-8'))
        return
    for name, version, *_ in CRATES:
        manifest = VENDOR / name / 'Cargo.toml'
        if not manifest.exists() or f'version = "{version}"' not in manifest.read_text(encoding='utf-8'):
            sys.exit(f'{name} {version} is not vendored; run tools/vendor_adblock.py')
    if (VENDOR / 'BUILD.gn').read_bytes() != build_gn().encode('utf-8'):
        sys.exit('third_party/rust/BUILD.gn is stale; run tools/vendor_adblock.py')
    print('vendored crates match their pins')


if __name__ == '__main__':
    main()
