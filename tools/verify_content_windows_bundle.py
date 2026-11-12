#!/usr/bin/env python3
"""Package and run the Windows Content Lab outside its Chromium output."""

import argparse
import ctypes
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

from package_win import DEBUG_ONLY, package_notices, write_zip
from verify_content_assistant_helper import verify_assistant_helper
from verify_content_bundle import verify_sandbox_guards


EXECUTABLE = "zitter_content.exe"
ARCHIVE_PREFIX = "Zitter Content Lab/"
ASSISTANT_EXECUTABLE = "zitter_assistant.exe"
RUNTIME_MANIFEST = "zitter_content.runtime_deps"
ROOT_ENTRY = re.compile(r"[a-zA-Z0-9_+.-]+\.(?:exe|dll|pak|dat|bin|json)", re.IGNORECASE)
LOCALE_ENTRY = re.compile(r"locales/[a-zA-Z0-9_-]+\.pak")
RUNTIME_CRT = re.compile(r"(?:msvcp|vcruntime|concrt|vccorlib)\d.*\.dll", re.IGNORECASE)
PE_IMPORT = re.compile(r"^(?:Import|DelayImport) \{\s*Name: ([^\r\n]+)", re.MULTILINE)
RESERVED_DEVICE = re.compile(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])", re.IGNORECASE)


def select_runtime_files(manifest):
    files = {}
    debug = {name.casefold() for name in DEBUG_ONLY}
    for line in manifest.splitlines():
        name = line.strip().removeprefix("./")
        if not (ROOT_ENTRY.fullmatch(name) or LOCALE_ENTRY.fullmatch(name)):
            continue
        if RESERVED_DEVICE.fullmatch(PurePosixPath(name).name.partition(".")[0]):
            raise ValueError(f"Reserved Windows runtime path: {name}")
        key = name.casefold()
        if key in debug:
            continue
        if key in files and files[key] != name:
            raise ValueError(f"Runtime paths collide on Windows: {files[key]} and {name}")
        files[key] = name
    required = {EXECUTABLE, ASSISTANT_EXECUTABLE, "zitter_content.pak", "icudtl.dat"}
    missing = required - files.keys()
    if missing:
        raise ValueError(f"Content runtime manifest is missing {', '.join(sorted(missing))}")
    return sorted(files.values())


def validate_sources(out, files):
    for name in files:
        source = out / name
        if not source.is_file() or not source.resolve().is_relative_to(out):
            raise ValueError(f"Missing or escaped runtime file: {source}")


def extract_bundle(archive, root, files, notices):
    required = {ARCHIVE_PREFIX + name for name in files}
    allowed = required | {ARCHIVE_PREFIX + name for name in notices}
    directories = {ARCHIVE_PREFIX}
    for name in allowed:
        directories.update(parent.as_posix() + "/" for parent in PurePosixPath(name).parents
                           if parent != PurePosixPath("."))
    seen = set()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            name = entry.filename
            mode = stat.S_IFMT(entry.external_attr >> 16)
            if (name != entry.orig_filename or name in seen
                    or mode not in (0, stat.S_IFDIR if entry.is_dir() else stat.S_IFREG)
                    or name not in (directories if entry.is_dir() else allowed)):
                raise ValueError(f"Unexpected or duplicate bundle entry: {entry.orig_filename!r}")
            seen.add(name)
        if missing := required - seen:
            raise ValueError(f"Archive is missing runtime files: {', '.join(sorted(missing))}")
        package.extractall(root)


def system_directory():
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetSystemDirectoryW(buffer, len(buffer))
    if not 0 < length < len(buffer):
        raise OSError("Windows system directory is unavailable")
    return Path(buffer.value)


def validate_imports(layout, files, readobj, system32):
    executables = {name.casefold(): name for name in files
                   if name.casefold().endswith((".exe", ".dll"))}
    system = {path.name.casefold() for path in system32.iterdir() if path.is_file()}
    for name in sorted(executables.values()):
        result = subprocess.run(
            [readobj, "--file-headers", "--coff-imports", layout / name],
            capture_output=True, text=True, timeout=30, check=True,
        )
        if ("Machine: IMAGE_FILE_MACHINE_AMD64" not in result.stdout or
                "ImageOptionalHeader {" not in result.stdout):
            raise ValueError(f"Expected an AMD64 PE image: {name}")
        for imported in PE_IMPORT.findall(result.stdout):
            key = imported.strip().casefold()
            if not ROOT_ENTRY.fullmatch(key) or not key.endswith(".dll"):
                raise ValueError(f"{name} has an invalid import name: {imported}")
            if key in executables or key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if key not in system or RUNTIME_CRT.fullmatch(key):
                raise ValueError(f"{name} needs a DLL outside the package: {imported}")
    print(f"Verified import closure for {len(executables)} AMD64 PE images")


def verify_signatures(layout, files, signtool):
    for name in files:
        if name.casefold().endswith((".exe", ".dll")):
            result = subprocess.run(
                [signtool, "verify", "/pa", "/all", layout / name],
                capture_output=True, text=True, errors="replace", timeout=60,
            )
            if result.returncode:
                raise ValueError(f"Authenticode verification failed for {name}:\n"
                                 f"{result.stdout}\n{result.stderr}")
    print("Verified embedded Authenticode signatures for packaged executables and DLLs")


def validate_swiftshader(layout):
    manifest = layout / "vk_swiftshader_icd.json"
    library = layout / "vk_swiftshader.dll"
    if not manifest.is_file() or not library.is_file():
        raise ValueError("The SwiftShader DLL and Vulkan manifest must ship together")
    with manifest.open(encoding="utf-8") as stream:
        document = json.load(stream)
    icd = document.get("ICD") if isinstance(document, dict) else None
    path = icd.get("library_path") if isinstance(icd, dict) else None
    if path not in (library.name, f"./{library.name}", f".\\{library.name}"):
        raise ValueError(f"SwiftShader ICD resolves outside its packaged DLL: {path}")


def smoke(layout, working_directory, env):
    workspace = "--zitter-content-workspace-smoke-test"
    workspace_marker = "Zitter Content workspace smoke passed"
    for flags, markers in (
        (("--zitter-content-smoke-test",), ("Zitter Content Lab smoke passed",)),
        ((workspace,), (workspace_marker,)),
        ((workspace, "--zitter-content-devtools-smoke-test",
          "--zitter-content-workspace-theme=dark"),
         (workspace_marker, "Zitter Content DevTools frontend ready")),
    ):
        result = subprocess.run(
            [layout / EXECUTABLE, *flags, "--no-proxy-server"],
            cwd=working_directory, env=env, capture_output=True, text=True, timeout=60,
        )
        if result.returncode or not set(markers).issubset(result.stdout.splitlines()):
            raise ValueError(f"{' '.join(flags)} failed ({result.returncode}):\n"
                             f"{result.stdout}\n{result.stderr}")
        print(", ".join(markers))


def verify_runtime_probes(executable, working_directory, env, *,
                          signer=None, openssl=None, legacy=None, node=None):
    tools = Path(__file__).resolve().parent
    probes = [
        ("verify_content_workspace_session.py", (executable,)),
        ("verify_content_workspace_session.py", (executable, "--private-windows")),
        ("verify_content_fingerprint.py", (executable, "--workspace", "--cross-process-frame")),
        ("verify_content_scriptlets.py", (executable, "--private-workspace")),
        ("verify_content_downloads.py", (executable, "--require-quarantine")),
    ]
    if openssl:
        probes.extend((
            ("verify_content_tls.py", (executable, "--openssl", openssl, "--virtual-webauthn")),
            ("verify_content_downloads.py", (
                executable, "--transport", "--require-quarantine", "--openssl", openssl,
            )),
        ))
    if signer:
        probes.extend(
            ("verify_content_workspace_session.py", (
                executable, "--extension-registry", "--extension-module-worker",
                "--extension-profile", profile,
                "--crx-signer", signer, "--openssl", openssl,
            )) for profile in ("work", "personal")
        )
        probes.append(("verify_content_workspace_session.py", (
            executable, "--extension-pair", "--crx-signer", signer, "--openssl", openssl,
        )))
    if legacy:
        probes.append(("verify_content_windows_migration.py", (legacy, executable, "--node", node)))
    for script, arguments in probes:
        result = subprocess.run(
            [sys.executable, tools / script, *arguments],
            cwd=working_directory, env=env, capture_output=True, text=True,
            timeout=300, check=True,
        )
        print(result.stdout, end="")


def keep_archive(archive, destination):
    with tempfile.NamedTemporaryFile(
        prefix=".zitter-content-", dir=destination.parent, delete=False
    ) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(archive, temporary)
        # On Windows rename fails if the destination already exists.
        temporary.rename(destination)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path, help="Windows Content Lab build directory")
    parser.add_argument("--runtime-deps", type=Path,
                        help=f"Override the GN manifest (default: {RUNTIME_MANIFEST} in out)")
    parser.add_argument("--llvm-readobj", type=Path, help="LLVM PE inspector")
    parser.add_argument("--signtool", type=Path,
                        help="Require trusted embedded signatures using this Windows SDK SignTool")
    parser.add_argument("--crx-signer", type=Path,
                        help="Also verify signed extensions with this local crx3_build_action.exe")
    parser.add_argument("--openssl", type=Path,
                        help="OpenSSL executable for TLS, WebAuthn, download transport, and signing probes")
    parser.add_argument("--legacy", type=Path,
                        help="Also verify DPAPI migration from this portable stable zitter.exe")
    parser.add_argument("--node", type=Path, help="Node.js executable for the legacy migration probe")
    archive = parser.add_mutually_exclusive_group()
    archive.add_argument("--archive", type=Path, help="Keep the generated ZIP at this new path")
    archive.add_argument("--input-archive", type=Path,
                         help="Verify an existing ZIP against the GN manifest without repackaging")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("The packaged executable must be verified in a Windows GUI session")
    out = args.out.resolve()
    manifest = args.runtime_deps or out / RUNTIME_MANIFEST
    if not manifest.is_file():
        parser.error(f"GN runtime manifest not found: {manifest}. Generate the Content output first.")
    readobj = (args.llvm_readobj or out.parents[1] /
               "third_party/llvm-build/Release+Asserts/bin/llvm-readobj.exe").resolve()
    if not readobj.is_file():
        parser.error(f"LLVM PE inspector not found: {readobj}")
    signtool = args.signtool.resolve() if args.signtool else None
    if signtool and not signtool.is_file():
        parser.error(f"Windows SDK SignTool not found: {signtool}")
    signer = args.crx_signer.resolve() if args.crx_signer else None
    openssl = args.openssl or (shutil.which("openssl") if signer else None)
    if openssl:
        openssl = Path(openssl).resolve()
        if not openssl.is_file():
            parser.error(f"OpenSSL executable not found: {openssl}")
    if signer and (not signer.is_file() or not openssl):
        parser.error("The extension probe requires Chromium's crx3_build_action.exe and OpenSSL")
    legacy = args.legacy.resolve() if args.legacy else None
    node = None
    if args.node and not legacy:
        parser.error("--node requires --legacy")
    if legacy:
        node_tool = args.node or shutil.which("node")
        if not legacy.is_file() or not node_tool or not Path(node_tool).is_file():
            parser.error("The migration probe requires a portable stable zitter.exe and Node.js")
        node = Path(node_tool).resolve()

    try:
        files = select_runtime_files(manifest.read_text(encoding="utf-8-sig"))
        notices = package_notices()
        if not args.input_archive:
            validate_sources(out, files)
        system32 = system_directory()
        env = dict(os.environ, PATH=os.pathsep.join((str(system32), str(system32.parent))))
        with tempfile.TemporaryDirectory(prefix="zitter-content-win-bundle-") as temporary:
            root = Path(temporary).resolve()
            archive = args.input_archive or root / "Zitter-Content-Lab-win-x64.zip"
            if not args.input_archive:
                write_zip(archive, out, files, prefix=ARCHIVE_PREFIX, extras=notices)
            extract_bundle(archive, root, files, {*notices, "THIRD_PARTY_LICENSES.txt"})
            layout = root / ARCHIVE_PREFIX
            validate_swiftshader(layout)
            validate_imports(layout, files, readobj, system32)
            if signtool:
                verify_signatures(layout, files, signtool)
            verify_sandbox_guards(layout / EXECUTABLE, root, env)
            smoke(layout, root, env)
            verify_assistant_helper(layout / ASSISTANT_EXECUTABLE, root, env)
            verify_runtime_probes(layout / EXECUTABLE, root, env, signer=signer, openssl=openssl,
                                  legacy=legacy, node=node)
            if args.archive:
                keep_archive(archive, args.archive.absolute())
                print(f"Verified archive: {args.archive}")
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        raise SystemExit(getattr(error, "stderr", None) or str(error)) from error


if __name__ == "__main__":
    main()
