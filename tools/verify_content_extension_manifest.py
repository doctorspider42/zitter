#!/usr/bin/env python3
"""Verify Chromium's unpacked extension validation in the standalone host."""

import argparse
import json
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


MARKER = "Zitter Content extension manifest and host permissions passed"


def run(executable, root, marker, expected_code):
    result = subprocess.run(
        [str(executable), f"--zitter-content-extension-manifest-probe={root}"],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode != expected_code or marker not in result.stdout + result.stderr:
        raise SystemExit(
            f"Unexpected extension probe result ({result.returncode}).\n"
            f"{result.stdout}\n{result.stderr}"
        )


def write_fixture(root, public_key=None, *, expect_legacy_storage=False, initial_counter=0):
    manifest = {
        "manifest_version": 3,
        "incognito": "not_allowed",
        "name": "Zitter extension fixture",
        "version": "1.0",
        "permissions": ["storage"],
        "background": {"service_worker": "worker.js"},
        "content_scripts": [{
            "matches": [
                "http://127.0.0.1/extension-content/allowed",
                "https://chromewebstore.google.com/*",
            ],
            "js": ["content.js"],
            "run_at": "document_start",
        }],
    }
    if public_key is not None:
        manifest["key"] = public_key
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    legacy_check = (
        "    if (legacyMarker !== 'chrome-profile') throw Error('legacy value missing');\n"
        if expect_legacy_storage else ""
    )
    (root / "worker.js").write_text(
        "chrome.runtime.onStartup.addListener(async () => {\n"
        "  let reply;\n"
        "  try {\n"
        f"    const {{counter, legacyMarker}} = await chrome.storage.local.get("
        f"{{counter: {initial_counter}, legacyMarker: null}});\n"
        + legacy_check +
        "    if (!Number.isSafeInteger(counter) || counter < 0) throw Error('invalid counter');\n"
        "    await chrome.storage.local.set({counter: counter + 1});\n"
        "    const updated = await chrome.storage.local.get('counter');\n"
        "    if (updated.counter !== counter + 1) throw Error('write did not persist');\n"
        "    reply = 'startup:' + chrome.runtime.getManifest().version + ':' + updated.counter;\n"
        "  } catch (error) { reply = String(error); }\n"
        "  for (const client of await clients.matchAll({type: 'window',"
        " includeUncontrolled: true})) client.postMessage(reply);\n"
        "});\n"
        "self.addEventListener('message', event => {"
        "  if (event.data === 'ping' && event.ports.length === 1)"
        "    event.ports[0].postMessage('pong');"
        "});\n",
        encoding="utf-8",
    )
    (root / "content.js").write_text(
        "globalThis.zitterContentScript = true;\n"
        "document.documentElement.dataset.zitterContentScript = 'injected';\n",
        encoding="utf-8",
    )
    nested = root / "assets" / "scripts"
    nested.mkdir(parents=True)
    (nested / "café.js").write_text(
        "globalThis.zitterNestedArchiveReady = true;\n", encoding="utf-8"
    )
    (root / "assets" / "empty.txt").touch()
    (root / "assets" / "data.bin").write_bytes(bytes(range(256)))
    import struct
    import zlib

    def png_chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))

    (root / "assets" / "action.png").write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
        png_chunk(b"IDAT", zlib.compress(b"\x00\x2a\x71\xc0")) +
        png_chunk(b"IEND", b"")
    )
    (root / "probe.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Zitter extension page</title>'
        "<script>document.documentElement.dataset.inlineRan = 'true'</script>"
        '<script src="assets/scripts/café.js"></script>'
        '<script src="probe.js"></script>',
        encoding="utf-8",
    )
    (root / "probe.js").write_text(
        "document.title = document.documentElement.dataset.inlineRan"
        " ? 'Inline script escaped CSP' : globalThis.zitterNestedArchiveReady"
        " ? 'Zitter extension script executed' : 'Nested extension script missing';\n"
        "const root = document.documentElement;\n"
        "if (!navigator.serviceWorker) root.dataset.workerReply = 'unavailable';\n"
        "else {\n"
        "  navigator.serviceWorker.addEventListener('message', event => {\n"
        "    root.dataset.workerStartup = event.data;\n"
        "  });\n"
        "  navigator.serviceWorker.ready.then(registration => {\n"
        "    const channel = new MessageChannel();\n"
        "    channel.port1.onmessage = event => {\n"
        "      root.dataset.workerReply = event.data;\n"
        "      channel.port1.close();\n"
        "    };\n"
        "    registration.active.postMessage('ping', [channel.port2]);\n"
        "  }).catch(error => { root.dataset.workerReply = String(error); });\n"
        "}\n",
        encoding="utf-8",
    )
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    app = parser.parse_args().app
    executable = app if os.name == "nt" else app / "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with TemporaryDirectory(prefix="zitter-content-extension-") as directory:
        root = Path(directory).resolve()
        manifest_path = root / "manifest.json"
        content_script = root / "content.js"
        manifest = write_fixture(root)
        run(executable, root, MARKER, 0)

        content_script.unlink()
        run(executable, root, "Extension manifest rejected:", 1)
        content_script.write_text("void 0;\n", encoding="utf-8")
        script = manifest["content_scripts"][0]
        script["matches"].append("https://example.invalid/*")
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        run(executable, root, "did not match the permission fixture", 1)
        script["matches"].pop()
        script["world"] = "MAIN"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        run(executable, root, "uses capabilities unsupported by the Content host", 1)

        manifest_path.write_text("{", encoding="utf-8")
        run(executable, root, "Extension manifest rejected:", 1)
        run(executable, root.parent, "requires a temporary fixture", 1)
    print("Content extension manifest probe passed")


if __name__ == "__main__":
    main()
