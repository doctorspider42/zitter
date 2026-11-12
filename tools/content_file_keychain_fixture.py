"""Build and configure the disposable macOS file Keychain fixture."""

from pathlib import Path
import secrets
import subprocess

from verify_content_bundle import loader_environment


def compile_file_keychain_fixture(scratch):
    source = Path(__file__).resolve().parents[1] / "tests/fixtures/content_file_keychain_mac.mm"
    library = scratch / "libcontent_file_keychain_fixture.dylib"
    subprocess.run([
        "xcrun", "--sdk", "macosx", "clang++", "-fobjc-arc", "-dynamiclib",
        "-framework", "Foundation", "-framework", "Security", source, "-o", library,
    ], check=True)
    return library


def file_keychain_environment(library, *, seed_legacy_key=False):
    env = dict(loader_environment(), DYLD_INSERT_LIBRARIES=str(library),
               ZITTER_CONTENT_TEST_KEYCHAIN_UNLOCK=secrets.token_hex(32))
    for name in ("ZITTER_CONTENT_TEST_SEED_LEGACY_KEY",
                 "ZITTER_CONTENT_TEST_SHARED_KEYCHAIN_ROOT",
                 "ZITTER_CONTENT_TEST_OPEN_LEGACY_KEYCHAIN",
                 "ZITTER_CONTENT_TEST_KEYCHAIN_READER",
                 "ZITTER_CONTENT_TEST_KEYCHAIN_VERIFIER",
                 "ZITTER_CONTENT_TEST_KEYCHAIN_UPGRADE_READER",
                 "ZITTER_CONTENT_TEST_KEYCHAIN_LIBRARY",
                 "ZITTER_CONTENT_TEST_KEYCHAIN_LOOKUP_FAILURE"):
        env.pop(name, None)
    if seed_legacy_key:
        env["ZITTER_CONTENT_TEST_SEED_LEGACY_KEY"] = "1"
    return env


def seal_legacy_copy(app, scratch):
    copy = scratch / "Zitter.app"
    subprocess.run(["/usr/bin/ditto", app, copy], check=True, timeout=90)
    subprocess.run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", copy],
                   check=True, timeout=90)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", copy],
                   check=True, timeout=90)
    return copy / "Contents/MacOS/Chromium"


def shared_keychain_environments(library, scratch, reader, *, verifier=None):
    content = file_keychain_environment(library)
    content["ZITTER_CONTENT_TEST_SHARED_KEYCHAIN_ROOT"] = str(scratch)
    legacy = dict(content, ZITTER_CONTENT_TEST_OPEN_LEGACY_KEYCHAIN="1",
                  ZITTER_CONTENT_TEST_KEYCHAIN_READER=str(reader))
    if verifier:
        legacy["ZITTER_CONTENT_TEST_KEYCHAIN_VERIFIER"] = str(verifier)
    legacy["ZITTER_CONTENT_TEST_KEYCHAIN_LIBRARY"] = legacy.pop("DYLD_INSERT_LIBRARIES")
    return content, legacy
