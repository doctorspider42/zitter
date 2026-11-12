#!/usr/bin/env python3
"""Reproduce the pinned Dark Reader assets used by Night Shift."""

import base64
import hashlib
import io
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
EXTENSION = ROOT / "chromium/night_shift"
VERSION = "4.9.135"
REVISION = "dbf2a1029131280d9dab21ed29f1dfdeb51a6cc7"
PACKAGE_SHA512 = (
    "h/gmS/KzY8HgmsgOSFh7ssJnsC5ymuhIwZqxksPKCS1K400OUJyjDPZN1L2YIrzX+oezhVUNnCDZPfmPUvdJNA=="
)
SOURCE_SHA256 = "45045c84999be054f4b35dd6bb430231fb9ace5adcdfb6f394665eedf4c7e7a2"
ESBUILD = "0.27.0"

INLINE_PROXY = '''        {
            const proxyScript = createOrUpdateScript("darkreader--proxy");
            proxyScript.append(
                `(${injectProxy})(${enableStyleSheetsProxy}, ${enableCustomElementRegistryProxy})`
            );
            document.head.insertBefore(
                proxyScript,
                document.head.querySelector(".darkreader")?.nextSibling ??
                    document.head.firstElementChild
            );
            proxyScript.remove();
        }'''
MAIN_WORLD_PROXY = '''        document.dispatchEvent(new CustomEvent("zitter-night-shift-install-proxy", {
            detail: Number(enableStyleSheetsProxy) | (Number(enableCustomElementRegistryProxy) << 1)
        }));'''
FORWARD_MESSAGE = '''            sendMessage(...args);
            nativeSendMessage.apply(chrome.runtime, args);'''
ROUTE_MESSAGE = '''            if (args[0]?.type === MessageTypeCStoBG.FETCH) {
                return sendMessage(...args);
            }
            return nativeSendMessage.apply(chrome.runtime, args);'''
POLL_STYLE = '''        function watchForSheetChanges() {
            watchForSheetChangesUsingProxy();
            if (!(canUseSheetProxy && element.sheet)) {'''
POLL_IF_ALLOWED = '''        function watchForSheetChanges(allowPolling) {
            watchForSheetChangesUsingProxy();
            if (allowPolling && !(canUseSheetProxy && element.sheet)) {'''
WATCH_STYLE = '''            if (element instanceof HTMLStyleElement) {
                sheetChangeWatcher.start();
            }'''
WATCH_LINK_AND_STYLE = '''            sheetChangeWatcher.start(element instanceof HTMLStyleElement);'''


def download(url):
    with urlopen(url, timeout=45) as response:
        return response.read()


def bundle(npm, source, entry, output, global_name=None, module=False):
    flags = {
        "__CHROMIUM_MV2__": "false",
        "__CHROMIUM_MV3__": "true",
        "__DEBUG__": "false",
        "__FIREFOX_MV2__": "false",
        "__LOG__": "'warn'",
        "__PLUS__": "false",
        "__TEST__": "false",
        "__THUNDERBIRD__": "false",
        "__WATCH__": "false",
    }
    command = [
        npm, "exec", "--yes", f"--package=esbuild@{ESBUILD}", "--", "esbuild",
        entry, "--bundle", "--platform=browser", "--format=esm" if module else "--format=iife",
        "--target=chrome120", f"--outfile={output}",
        "--banner:js=/* Derived from Dark Reader 4.9.135. See LICENSE. */",
        *(f"--define:{name}={value}" for name, value in flags.items()),
    ]
    if global_name:
        command.append(f"--global-name={global_name}")
    subprocess.run(command, cwd=source, check=True)


def main():
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    if not npm:
        raise SystemExit("npm is required to refresh the vendor assets")
    package = download(f"https://registry.npmjs.org/darkreader/-/darkreader-{VERSION}.tgz")
    if base64.b64encode(hashlib.sha512(package).digest()).decode() != PACKAGE_SHA512:
        raise ValueError("Dark Reader package integrity mismatch")
    source_archive = download(f"https://codeload.github.com/darkreader/darkreader/tar.gz/{REVISION}")
    if hashlib.sha256(source_archive).hexdigest() != SOURCE_SHA256:
        raise ValueError("Dark Reader source integrity mismatch")

    with tempfile.TemporaryDirectory(prefix="zitter-darkreader-") as temporary:
        work = Path(temporary)
        output = work / "vendor"
        output.mkdir()
        with tarfile.open(fileobj=io.BytesIO(package), mode="r:gz") as archive:
            script = archive.extractfile("package/darkreader.js").read().decode("utf-8").replace("\r\n", "\n")
            for before, after in (
                (INLINE_PROXY, MAIN_WORLD_PROXY),
                (FORWARD_MESSAGE, ROUTE_MESSAGE),
                (POLL_STYLE, POLL_IF_ALLOWED),
                (WATCH_STYLE, WATCH_LINK_AND_STYLE),
            ):
                if script.count(before) != 1:
                    raise ValueError("Review the upstream extension adapter before refreshing")
                script = script.replace(before, after)
            (output / "darkreader.js").write_bytes(script.encode("utf-8"))
            (output / "LICENSE").write_bytes(archive.extractfile("package/LICENSE").read())
        with tarfile.open(fileobj=io.BytesIO(source_archive), mode="r:gz") as archive:
            archive.extractall(work, filter="data")
        source = work / f"darkreader-{REVISION}"
        for name in ("dark-sites.config", "dynamic-theme-fixes.config"):
            shutil.copyfile(source / "src/config" / name, output / name)
        for name in ("vendor_bridge.ts", "vendor_proxy.ts"):
            shutil.copyfile(EXTENSION / name, source / name)
        bundle(npm, source, "vendor_bridge.ts", output / "compatibility.js", module=True)
        bundle(npm, source, "vendor_proxy.ts", output / "proxy.js")
        bundle(npm, source, "src/inject/detector.ts", output / "detector.js", "NightShiftDetector")
        shutil.copytree(output, EXTENSION / "vendor", dirs_exist_ok=True)


if __name__ == "__main__":
    main()
