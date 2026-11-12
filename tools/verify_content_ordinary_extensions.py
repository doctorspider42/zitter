#!/usr/bin/env python3
"""Verify approved extensions on ordinary macOS startup with disposable profiles."""

import argparse
import base64
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import plistlib
import subprocess
from tempfile import TemporaryDirectory
from threading import Event, Thread

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from package_app import BUNDLE_ID
from verify_content_fresh_startup import verify_startup
from verify_content_upgrade import run_profile
from verify_content_workspace_session import make_signing_key, seed_approved_receiver, sign_fixture


class Fixture(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/result/"):
            self.server.result = self.path
            self.server.completed.set()
            body = b""
        elif self.path.startswith("/extension-startup/"):
            role = self.path.rsplit("/", 1)[-1]
            expected = json.dumps(str(self.server.expected_count))
            body = f"""<!doctype html><title>Extension startup fixture</title>
<script>
const deadline = performance.now() + 1500;
const timer = setInterval(() => {{
  const count = document.documentElement.dataset.extensionCount || "0";
  if (count === "0" && performance.now() < deadline) return;
  clearInterval(timer);
  const passed = count === {expected};
  const error = document.documentElement.dataset.extensionError;
  fetch('/result/{role}/' + (passed ? 'passed' :
    'failed/' + encodeURIComponent(error || 'unexpected count ' + count)));
}}, 50);
</script>""".encode()
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def package(signer, directory):
    directory.mkdir()
    key = directory / "key.der"
    public = make_signing_key("openssl", key)
    digest = sha256(public).hexdigest()
    extension_id = digest[:32].translate(str.maketrans("0123456789abcdef", "abcdefghijklmnop"))
    manifest = {
        "manifest_version": 3,
        "name": "Ordinary startup fixture",
        "version": "1.0",
        "key": base64.b64encode(public).decode("ascii"),
        "permissions": ["scripting", "storage"],
        "host_permissions": ["http://127.0.0.1/*"],
        "background": {"service_worker": "worker.js"},
        "content_scripts": [{
            "matches": ["http://127.0.0.1/*"], "js": ["content.js"], "run_at": "document_start",
        }],
    }
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (directory / "worker.js").write_text("""
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.command === "page-details") {
    reply(sender.frameId === 0 && sender.tab?.id === message.tabId &&
          sender.tab?.url === message.url && sender.url === message.url);
    return;
  }
  if (message !== "startup-fixture") return;
  chrome.storage.local.get("runs").then(async ({runs = 0}) => {
    if (!Number.isInteger(sender.tab?.id) || sender.tab.id <= 0)
      throw new Error("The restored page did not have a positive tab ID");
    const tab = await chrome.tabs.get(sender.tab.id);
    if (sender.frameId !== 0 || tab.url !== sender.url)
      throw new Error("The worker could not identify the restored page");
    await chrome.scripting.executeScript({
      target: {tabId: tab.id, frameIds: [sender.frameId]},
      files: ["form.js"],
    });
    const accepted = await chrome.tabs.sendMessage(
      tab.id, {command: "collect-details", tabId: tab.id}, null);
    if (!accepted) throw new Error("The worker did not accept the form response");
    await chrome.storage.local.set({runs: runs + 1});
    reply(runs + 1);
  }).catch(error => reply({error: String(error)}));
  return true;
});
""", encoding="utf-8")
    (directory / "content.js").write_text("""
chrome.runtime.sendMessage("startup-fixture", count => {
  const report = () => {
    document.documentElement.dataset.extensionCount =
      Number.isInteger(count) ? String(count) : "error";
    if (!Number.isInteger(count))
      document.documentElement.dataset.extensionError =
        chrome.runtime.lastError?.message || count?.error || "no worker reply";
  };
  if (document.documentElement) report();
  else document.addEventListener("DOMContentLoaded", report, {once: true});
});
""", encoding="utf-8")
    (directory / "form.js").write_text("""
chrome.runtime.onMessage.addListener((message, _sender, reply) => {
  if (message?.command !== "collect-details") return;
  chrome.runtime.sendMessage({
    command: "page-details", tabId: message.tabId, url: location.href,
  }).then(reply, () => reply(false));
  return true;
});
""", encoding="utf-8")
    files = ("manifest.json", "worker.js", "content.js", "form.js")
    archive = sign_fixture(signer, directory, key, "fixture.crx", files=files)
    key.unlink()
    return extension_id, digest, archive, files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--crx-signer", required=True, type=Path)
    parser.add_argument("--scratch", type=Path, help="Directory for the temporary app copy")
    args = parser.parse_args()
    with (TemporaryDirectory(prefix="zitter-ordinary-extensions-", dir=args.scratch) as directory,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile,
          ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server):
        work, root = Path(directory).resolve(), Path(profile).resolve()
        app = work / "Zitter.app"
        # A GN bundle is sufficient for this runtime fixture. Seal the outer
        # browser so its ordinary-startup plist can be read without a switch.
        subprocess.run(["/bin/cp", "-cRp", args.app.resolve(strict=True), app],
                       check=True, timeout=90)
        plist = app / "Contents/Info.plist"
        info = plistlib.loads(plist.read_bytes())
        info.update(CFBundleIdentifier=BUNDLE_ID, ZitterUsePreparedWorkspace=True)
        plist.write_bytes(plistlib.dumps(info))
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", app],
                       check=True, timeout=90)
        library = compile_file_keychain_fixture(work)
        env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
        verify_startup(app / "Contents/MacOS/Zitter Content Lab", root, env)
        installed = package(args.crx_signer.resolve(strict=True), work / "package")
        rejected = package(args.crx_signer.resolve(strict=True), work / "damaged-package")
        owner = root / "keychain-work"
        seed_approved_receiver(owner, installed)
        seed_approved_receiver(owner, rejected)
        damaged_archive = owner / "Extensions" / rejected[0] / "1.0_0/fixture.crx"
        damaged = damaged_archive.read_bytes() + b"!"
        damaged_archive.write_bytes(damaged)
        server.completed = Event()
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        session_path = root / "ContentKeychainWorkspaceSession.json"
        try:
            for role, count in (("work", 1), ("personal", 0), ("work", 2), ("work", 0)):
                if role == "work" and count == 0:
                    (owner / "Extensions" / installed[0] / "user-enabled").write_text("disabled")
                session = json.loads(session_path.read_text())
                session["active_workspace"] = role
                session[role].update(active=0, tabs=[{
                    "url": f"http://127.0.0.1:{server.server_port}/extension-startup/{role}",
                    "title": "Extension startup fixture",
                }])
                session_path.write_text(json.dumps(session))
                server.expected_count = count
                run_profile(app, env, server, role, seed=False)
            if (root / "keychain-personal/Local Extension Settings" / installed[0]).exists():
                raise ValueError("The approved extension created storage in Personal")
            if ((owner / "Local Extension Settings" / rejected[0]).exists() or
                    damaged_archive.read_bytes() != damaged):
                raise ValueError("The damaged extension ran or its archive changed")
            print("Ordinary startup restored the approved Work extension, storage, and disabled state")
            print("The restored Work page completed a two-way extension messaging round trip")
            print("Ordinary startup skipped a damaged sibling without changing its archive")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
