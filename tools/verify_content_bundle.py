#!/usr/bin/env python3
"""Copy and exercise a macOS Content Lab bundle outside its build directory."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

from notarize_app import verify_nested_signatures
from package_app import BUNDLE_ID
from verify_content_assistant_helper import verify_assistant_helper


PRODUCT = "Zitter Content Lab"
FRAMEWORK = f"{PRODUCT} Framework"
RPATH_LINE = re.compile(r"^\s*path (.+) \(offset \d+\)$", re.MULTILINE)
ABSOLUTE_LOAD_PATH = re.compile(r"^\s*name (/.+) \(offset \d+\)$", re.MULTILINE)
LOADED_IMAGE = re.compile(r"^dyld\[\d+\]: <[0-9A-F-]+> (/[^<>\n]+)$", re.MULTILINE)
SYSTEM_ROOTS = ("/System/Library/", "/usr/lib/", "/System/Volumes/Preboot/Cryptexes/OS/")
HELPER_SUFFIXES = ("", " (Renderer)", " (GPU)", " (Aperitif)",
                   " (Aperitif Renderer)", " (Aperitif GPU)")
RENDERER_MARKER = "Zitter Content renderer helper: "
PASSKEY_ENTITLEMENT = "com.apple.developer.web-browser.public-key-credential"
PASSKEY_PREFLIGHT = "--zitter-content-workspace-native-passkey-preflight"
PASSKEY_MARKER = "Zitter Content native passkey discovery preflight passed"


def run(*args, cwd=None, env=None):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT,
                                   cwd=cwd, env=env)


def bundle_executable(bundle):
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        name = plistlib.load(stream)["CFBundleExecutable"]
    if not isinstance(name, str) or not name or Path(name).name != name or name in (".", ".."):
        raise ValueError(f"Invalid executable name in {bundle}")
    executable = bundle / "Contents/MacOS" / name
    if not executable.is_file():
        raise ValueError(f"Missing executable {executable}")
    return executable


def assistant_executable(app):
    return app / "Contents/MacOS/zitter_assistant"


def validate_links(bundle):
    bundle = bundle.resolve(strict=True)
    for path in bundle.rglob("*"):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(bundle):
            raise ValueError(f"Bundle link escapes the copy: {path}")


def validate_payload(app):
    _, version, helpers = framework_layout(app)
    expected_helpers = {f"{PRODUCT} Helper{suffix}.app" for suffix in HELPER_SUFFIXES}
    if {helper.name for helper in helpers} != expected_helpers:
        raise ValueError("The Content framework does not contain the expected helper bundles")
    for path in (
        assistant_executable(app),
        version / FRAMEWORK,
        version / "Resources/zitter_content.pak",
        version / "Resources/gpu_shader_cache.bin",
        version / "Libraries/libvulkan.dylib",
        version / "Libraries/libvk_swiftshader.dylib",
        version / "Libraries/vk_swiftshader_icd.json",
    ):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty bundle payload: {path}")
    manifest = version / "Libraries/vk_swiftshader_icd.json"
    with manifest.open(encoding="utf-8") as stream:
        document = json.load(stream)
    icd = document.get("ICD") if isinstance(document, dict) else None
    library = icd.get("library_path") if isinstance(icd, dict) else None
    if library != "./libvk_swiftshader.dylib":
        raise ValueError(f"SwiftShader ICD must name its adjacent bundled dylib: {library!r}")


def move_runpath(tool, executable, old, new):
    # otool interprets parentheses in a filename as archive-member syntax.
    with tempfile.TemporaryDirectory(prefix="zitter-content-macho-") as temporary:
        binary = Path(temporary) / "image"
        shutil.copy2(executable, binary)
        paths = RPATH_LINE.findall(run("/usr/bin/otool", "-l", binary))
        if paths.count(old) != 1 or paths.count(new) > 1:
            raise ValueError(f"Unexpected runpaths in {executable}: {paths}")
        if new in paths:
            run(tool, "-delete_rpath", old, binary)
        else:
            run(tool, "-rpath", old, new, binary)
        shutil.copy2(binary, executable)


def framework_layout(app):
    framework = app / "Contents/Frameworks" / f"{FRAMEWORK}.framework"
    version = framework / "Versions/A"
    helpers = sorted((version / "Helpers").glob("*.app"))
    if not helpers:
        raise ValueError("The Content framework has no helpers")
    return framework, version, helpers


def package_components(app, build_directory, manifest):
    frameworks = app / "Contents/Frameworks"
    _, version, helpers = framework_layout(app)
    checkout = build_directory.parents[1]
    tool = checkout / "third_party/llvm-build/Release+Asserts/bin/llvm-install-name-tool"
    libraries = {
        Path(entry).name
        for entry in manifest.read_text(encoding="utf-8").splitlines()
        if re.fullmatch(r"\./[^/]+\.dylib", entry)
    }
    if not {"libbase.dylib", "libsandbox_mac_seatbelt.dylib"} <= libraries:
        raise ValueError("Expected the component framework's GN runtime_deps")

    runtime = frameworks / "Runtime"
    runtime.mkdir()
    for name in sorted(libraries):
        source = build_directory / name
        if not source.is_file() or source.resolve() != source:
            raise ValueError(f"Missing or indirect component library: {source}")
        shutil.copy2(source, runtime / name)

    # Keep each dylib's built-in loader paths contained without rewriting it.
    move_runpath(tool, bundle_executable(app), "@loader_path/../../..",
                 "@loader_path/../Frameworks/Runtime")
    move_runpath(tool, assistant_executable(app), "@loader_path/../../..",
                 "@loader_path/../Frameworks/Runtime")
    move_runpath(tool, version / FRAMEWORK, "@loader_path/../../../../../..",
                 "@loader_path/../../../Runtime")
    for helper in helpers:
        move_runpath(tool, bundle_executable(helper), "@executable_path/../../../../../../../../../..",
                     "@executable_path/../../../../../../../Runtime")

    print(f"Embedded {len(libraries)} component libraries")


def runpath_target(runpath, directory):
    return (directory / runpath.split("/", 1)[1]).resolve()


def validate_runpaths(app):
    _, version, helpers = framework_layout(app)
    executables = [bundle_executable(bundle) for bundle in [app, *helpers]]
    executables = {*executables, assistant_executable(app)}
    libraries = set(app.glob("Contents/Frameworks/Runtime/*.dylib"))
    libraries.update((version / "Libraries").glob("*.dylib"))
    libraries.add(version / FRAMEWORK)
    executable_directories = {path.parent for path in executables}
    architectures = set(run("/usr/bin/lipo", "-archs", bundle_executable(app)).split())

    def inspect(path):
        # Open a short alias when a helper name contains parentheses.
        with tempfile.TemporaryDirectory(prefix="zitter-content-otool-") as temporary:
            alias = Path(temporary) / "image"
            alias.symlink_to(path)
            available = set(run("/usr/bin/lipo", "-archs", alias).split())
            if not architectures or not architectures <= available:
                raise ValueError(f"Missing launcher architecture in {path}: {available}")
            commands = run("/usr/bin/otool", "-l", alias)
        for dependency in ABSOLUTE_LOAD_PATH.findall(commands):
            if not dependency.startswith(SYSTEM_ROOTS):
                raise ValueError(f"Absolute non-system dependency in {path}: {dependency}")
        for runpath in RPATH_LINE.findall(commands):
            if runpath.startswith("@loader_path/"):
                directories = {path.parent}
            elif runpath.startswith("@executable_path/"):
                directories = {path.parent} if path in executables else executable_directories
            else:
                raise ValueError(f"Unexpected runpath in {path}: {runpath}")
            if any(not runpath_target(runpath, directory).is_relative_to(app)
                   for directory in directories):
                raise ValueError(f"Runpath escapes the bundle in {path}: {runpath}")

    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(inspect, sorted(executables | libraries)))


def seal_local_copy(app):
    framework, _, helpers = framework_layout(app)
    # Only the copies are sealed. The toolchain has already signed each dylib.
    for bundle in [*helpers, framework, assistant_executable(app), app]:
        run("/usr/bin/codesign", "--force", "--sign", "-", bundle)
    verify_signature(app)


def verify_signature(app):
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)


def loader_environment():
    return {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}


def verify_sandbox_guards(executable, directory, env):
    with tempfile.TemporaryDirectory(prefix="sandbox-guards-", dir=directory) as scratch:
        isolated = dict(env, TMPDIR=scratch, TMP=scratch, TEMP=scratch)
        for flag in (
            "--no-sandbox",
            "--disable-gpu-sandbox",
            "--single-process",
            "--in-process-gpu",
            f"--browser-subprocess-path={executable}",
            "--renderer-cmd-prefix",
            "--gpu-launcher",
            "--utility-cmd-prefix",
        ):
            result = subprocess.run(
                [executable, "--zitter-content-smoke-test", "--no-proxy-server", flag],
                cwd=directory, env=isolated, capture_output=True, text=True, timeout=60,
            )
            if (result.returncode != 1 or RENDERER_MARKER in result.stdout
                    or "Zitter Content Lab smoke passed" in result.stdout
                    or any(Path(scratch).iterdir())):
                raise ValueError(f"Sandbox startup guard failed for {flag} ({result.returncode}):\n"
                                 f"{result.stdout}\n{result.stderr}")
    print("Content rejected sandbox and subprocess overrides")


def smoke(app, directory, source, flags, markers, *, trace_loader=True):
    env = loader_environment()
    probe = " ".join(flags)
    if trace_loader:
        trace = directory / (".".join(flag.removeprefix("--") for flag in flags) + ".dyld")
        env["DYLD_PRINT_LIBRARIES"] = "1"
        env["DYLD_PRINT_TO_FILE"] = str(trace)
    executable = bundle_executable(app)
    result = subprocess.run(
        [executable, *flags, "--no-proxy-server"], cwd=directory, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
    )
    if result.returncode or not set(markers).issubset(result.stdout.splitlines()):
        raise ValueError(f"{probe} failed ({result.returncode}):\n{result.stdout[-12000:]}")
    _, version, helpers = framework_layout(app)
    # dyld writes each process's output in fragments, so a shared trace can
    # splice the renderer's path into another process's line.
    expected_renderers = {bundle_executable(helper).resolve() for helper in helpers
                          if helper.name.endswith("Renderer).app")}
    renderers = [Path(line.removeprefix(RENDERER_MARKER)).resolve()
                 for line in result.stdout.splitlines() if line.startswith(RENDERER_MARKER)]
    if not renderers or any(path not in expected_renderers for path in renderers):
        raise ValueError(f"{probe} did not identify a renderer helper inside the bundle")
    if not trace_loader:
        print(f"{', '.join(markers)} from the relocated bundle with its existing signature")
        return

    trace_text = trace.read_text(encoding="utf-8", errors="replace")
    if str(source.parent) + "/" in trace_text:
        raise ValueError(f"{probe} loaded an image from the original build directory")
    images = set()
    for image in LOADED_IMAGE.findall(trace_text):
        if image.startswith(SYSTEM_ROOTS):
            images.add(image)
            continue
        path = Path(image)
        # Concurrent dyld writers can splice the paths of two images. Only
        # physical non-system images count as evidence in this trace.
        if not path.is_file():
            continue
        physical_path = path.resolve()
        if not physical_path.is_relative_to(app):
            raise ValueError(f"{probe} loaded an image outside the bundle: {image}")
        images.add(str(physical_path))
    if str(executable) not in images or str(version / FRAMEWORK) not in images:
        raise ValueError(f"{probe} did not trace the app and framework")
    print(f"{', '.join(markers)} from the relocated bundle ({len(images)} loaded images)")


def copy_dmg_app(image, mount, app, *, app_name=f"{PRODUCT}.app"):
    run("/usr/bin/hdiutil", "verify", image)
    mount.mkdir()
    run("/usr/bin/hdiutil", "attach", image, "-readonly", "-nobrowse",
        "-noautoopen", "-mountpoint", mount)
    try:
        applications = mount / "Applications"
        if not applications.is_symlink() or applications.readlink() != Path("/Applications"):
            raise ValueError("DMG is missing its Applications shortcut")
        source = mount / app_name
        if source.is_symlink() or not source.is_dir():
            raise ValueError(f"DMG is missing {app_name}")
        validate_links(source)
        app.parent.mkdir(parents=True, exist_ok=True)
        run("/usr/bin/ditto", source, app)
    finally:
        run("/usr/bin/hdiutil", "detach", mount)


def require_passkey_entitlement(app):
    signature = subprocess.run(
        ["/usr/bin/codesign", "-d", "--entitlements", "-", "--xml", app],
        capture_output=True,
        check=True,
    )
    entitlements = plistlib.loads(signature.stdout) if signature.stdout else {}
    if entitlements.get(PASSKEY_ENTITLEMENT) is not True:
        raise ValueError(f"The signed app is missing the {PASSKEY_ENTITLEMENT} entitlement")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Content app bundle or a DMG containing it")
    parser.add_argument("--scratch", type=Path,
                        help="Existing directory for the relocated bundle and image mount")
    parser.add_argument("--content-release", action="store_true",
                        help="Verify the Developer ID signed Zitter.app built on Content")
    signing = parser.add_mutually_exclusive_group()
    signing.add_argument("--component-runtime-deps", type=Path,
                         help="GN runtime_deps listing for browser_framework_shared_library")
    signing.add_argument("--preserve-signature", action="store_true",
                         help="Verify the existing seal and exercise the unmodified copy")
    parser.add_argument("--require-native-passkeys", action="store_true",
                        help="Check the existing passkey entitlement and browser discovery readiness")
    parser.add_argument("--crx-signer", type=Path,
                        help="Run the signed extension and sandboxed utility tests with this local signer")
    options = parser.parse_args()
    if options.content_release and not options.preserve_signature:
        parser.error("--content-release requires --preserve-signature")
    if options.require_native_passkeys and not options.preserve_signature:
        parser.error("--require-native-passkeys requires --preserve-signature")
    source = options.app.resolve()
    disk_image = source.is_file() and source.suffix.lower() == ".dmg"
    if disk_image and not options.preserve_signature:
        parser.error("Verifying a DMG requires --preserve-signature")
    app_name = "Zitter.app" if options.content_release else f"{PRODUCT}.app"
    if not disk_image and (not source.is_dir() or source.name != app_name):
        parser.error(f"Expected {app_name} or a DMG containing it: {source}")
    signer = options.crx_signer.resolve() if options.crx_signer else None
    if signer and not signer.is_file():
        parser.error(f"CRX signer not found: {signer}")

    try:
        if options.require_native_passkeys and not disk_image:
            require_passkey_entitlement(source)
        with tempfile.TemporaryDirectory(prefix="zitter-content-bundle-",
                                         dir=options.scratch) as temporary:
            root = Path(temporary).resolve()
            app = root / "Relocated Applications" / app_name
            if disk_image:
                copy_dmg_app(source, root / "Mounted Image", app, app_name=app_name)
                if options.require_native_passkeys:
                    require_passkey_entitlement(app)
            else:
                app.parent.mkdir()
                run("/bin/cp", "-cRp", source, app)
            validate_links(app)
            if options.content_release:
                info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
                if (info.get("CFBundleIdentifier") != BUNDLE_ID or
                        info.get("ZitterUsePreparedWorkspace") is not True):
                    raise ValueError("Expected production Content identity and ordinary startup")
                if (info.get("CFBundleIconFile") != "Zitter.icns" or
                        not (app / "Contents/Resources/Zitter.icns").is_file()):
                    raise ValueError("The signed Content app must bundle and declare the Zitter icon")
                schemes = {scheme for entry in info.get("CFBundleURLTypes", [])
                           for scheme in entry.get("CFBundleURLSchemes", [])}
                if not {"http", "https"} <= schemes:
                    raise ValueError("The signed Content app must declare HTTP and HTTPS URL handling")
                verify_nested_signatures(app)
            elif options.preserve_signature:
                verify_signature(app)
            validate_payload(app)
            if options.component_runtime_deps:
                package_components(app, source.parent, options.component_runtime_deps)
            validate_runpaths(app)
            if not options.preserve_signature:
                seal_local_copy(app)
            trace_loader = not options.preserve_signature
            verify_sandbox_guards(bundle_executable(app), root, loader_environment())
            verify_assistant_helper(assistant_executable(app), root, loader_environment())
            if not options.content_release:
                smoke(app, root, source, ("--zitter-content-smoke-test",),
                      ("Zitter Content Lab smoke passed",), trace_loader=trace_loader)
            workspace = "--zitter-content-workspace-smoke-test"
            workspace_marker = "Zitter Content workspace smoke passed"
            workspace_flags = (workspace,)
            workspace_markers = (workspace_marker,)
            if options.require_native_passkeys:
                workspace_flags += (PASSKEY_PREFLIGHT,)
                workspace_markers += (PASSKEY_MARKER,)
            smoke(app, root, source, workspace_flags, workspace_markers, trace_loader=trace_loader)
            smoke(app, root, source, (
                workspace, "--zitter-content-devtools-smoke-test",
                "--zitter-content-workspace-theme=dark", "--zitter-rust-raster=metal",
            ), (workspace_marker, "Zitter Content DevTools frontend ready"),
                  trace_loader=trace_loader)
            workspace_flags = ("--extension-registry", "--crx-signer", signer) if signer else ()
            probes = [
                ("verify_content_workspace_session.py", workspace_flags),
            ]
            if options.content_release:
                probes.append(("verify_content_workspace_session.py", ("--private-windows",)))
            else:
                probes.append(("verify_content_profiles.py", ()))
            for script, flags in probes:
                print(run(sys.executable, Path(__file__).resolve().with_name(script), app,
                          *flags, cwd=root, env=loader_environment()), end="")
            if options.preserve_signature:
                verify_signature(app)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(getattr(error, "output", None) or str(error)) from error


if __name__ == "__main__":
    main()
