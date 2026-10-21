#!/usr/bin/env python3
"""Generate own malformed RAR fixtures and decode pinned upstream BSD samples."""
import argparse
import binascii
from pathlib import Path
import struct
import subprocess
import tarfile
import zlib

ROOT = Path(__file__).resolve().parents[1]

def header(kind, flags, body):
    data = struct.pack('<BHH', kind, flags, len(body) + 7) + body
    return struct.pack('<H', zlib.crc32(data) & 65535) + data

def stored(name, payload):
    encoded = name.encode()
    body = struct.pack('<IIBIIBBHI', len(payload), len(payload), 3,
                       zlib.crc32(payload), 0, 20, 0x30, len(encoded), 0o100644) + encoded
    return b'Rar!\x1a\x07\x00' + header(0x73, 0, b'\0' * 6) + header(0x74, 0x8000, body) + payload + header(0x7b, 0, b'')

def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    valid = stored('test.txt', b'Zitter open RAR decoder\n')
    (directory / 'stored.rar').write_bytes(valid)
    (directory / 'write-error.rar').write_bytes(valid)
    (directory / 'traversal.rar').write_bytes(stored('../escape.exe', b'untrusted'))
    (directory / 'truncated.rar').write_bytes(valid[:-20])
    bad = bytearray(valid); bad[-10] ^= 0x40
    (directory / 'crc.rar').write_bytes(bad)
    cases = {'rar5': 'test_read_format_rar5_compressed', 'solid': 'test_read_format_rar5_multiple_files_solid',
             'encrypted4': 'test_read_format_rar4_encrypted', 'encrypted5': 'test_read_format_rar5_encrypted',
             'headers5': 'test_read_format_rar5_encrypted_filenames', 'symlink': 'test_read_format_rar'}
    with tarfile.open(ROOT / 'third_party/libarchive/libarchive-3.8.9.tar.xz') as archive:
        for name, upstream in cases.items():
            stream = archive.extractfile('libarchive-3.8.9/libarchive/test/' + upstream + '.rar.uu')
            lines = stream.read().splitlines()
            start = next(i for i, line in enumerate(lines) if line.startswith(b'begin ')) + 1
            data = bytearray()
            for line in lines[start:]:
                if line == b'end': break
                data.extend(binascii.a2b_uu(line))
            (directory / (name + '.rar')).write_bytes(data)
    return directory

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    args = parser.parse_args()
    directory = prepare(ROOT / 'build/rar-backend-tests')
    if args.binary: subprocess.run([str(args.binary.resolve()), str(directory)], check=True, timeout=30)
    else: print(directory)
