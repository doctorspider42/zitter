#!/usr/bin/env python3
"""Build pinned libarchive without optional external libraries or programs."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

source = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
archive = source / 'libarchive-3.8.9.tar.xz'
if hashlib.sha256(archive.read_bytes()).hexdigest() != '888c934f9d95648ecb9163dc8e23ab80a476ecb81a8f1154704a227b5b676dde':
    raise ValueError('libarchive source checksum mismatch')
work = output.parent / 'libarchive-build'
work.mkdir(exist_ok=True)
unpacked = work / 'source'
if not unpacked.exists():
    unpacked.mkdir()
    subprocess.run(['tar', '-xf', str(archive), '-C', str(unpacked), '--strip-components=1'], check=True)
# Zitter: the upstream RAR3 stored-data EOF shortcut can bypass its CRC
# check. Validate the completed stream before taking that shortcut. Our adapter
# never seeks within an entry and never marks a partial read as valid.
rar = unpacked / 'libarchive/archive_read_support_format_rar.c'
text = rar.read_text()
anchor = '  if (rar->entry_eof || rar->offset_seek >= rar->unp_size) {'
guard = """  /* Zitter: validate stored RAR CRC before the generic EOF shortcut. */
  if (rar->compression_method == COMPRESS_METHOD_STORE &&
      rar->bytes_remaining == 0 && rar->offset_seek >= rar->unp_size &&
      rar->file_crc != rar->crc_calculated) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT, "File CRC error");
    return (ARCHIVE_FAILED);
  }
"""
if 'Zitter: validate stored RAR CRC' not in text:
    if text.count(anchor) != 1:
        raise ValueError('Unreviewed RAR3 CRC implementation')
    rar.write_text(text.replace(anchor, guard + anchor))
options = ['MBEDTLS', 'NETTLE', 'OPENSSL', 'LIBB2', 'LZ4', 'LZO', 'LZMA', 'ZSTD',
           'ZLIB', 'BZip2', 'LIBXML2', 'EXPAT', 'WIN32_XMLLITE', 'PCREPOSIX',
           'PCRE2POSIX', 'LIBGCC', 'CNG', 'TAR', 'CPIO', 'CAT', 'UNZIP', 'XATTR',
           'ACL', 'TEST', 'INSTALL', 'COVERAGE']
subprocess.run(['cmake', '-S', str(unpacked), '-B', str(work / 'obj'),
                '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
                '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0',
                '-DCMAKE_C_FLAGS=-fstack-protector-strong -fvisibility=hidden',
                '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local',
                '-DENABLE_WERROR=OFF'] + ['-DENABLE_' + name + '=OFF' for name in options], check=True)
subprocess.run(['cmake', '--build', str(work / 'obj'), '--target', 'archive_static', '-j', '4'], check=True)
shutil.copyfile(work / 'obj/libarchive/libarchive.a', output)
