#!/usr/bin/env python3
"""Verify Workspace rejects untrusted TLS and optionally accepts a trusted HTTPS page."""

import argparse
from http.client import HTTPSConnection
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading


SMOKE = "--zitter-content-workspace-smoke-test"
MARKER = "Zitter Content rejected an untrusted TLS certificate"
TRUSTED_MARKER = "Zitter Content committed a trusted HTTPS page"
TRUSTED_PROBE = "--zitter-content-workspace-trusted-tls-probe"
WEBAUTHN_PROBE = "--zitter-content-workspace-webauthn-probe"
WEBAUTHN_MARKER = "Zitter Content virtual WebAuthn probe passed"
BYPASSES = (
    "--ignore-certificate-errors",
    "--allow-insecure-localhost",
    "--ignore-certificate-errors-spki-list",
)


class TlsFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.paths.append(self.path)
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()


def launch(executable, *arguments):
    try:
        return subprocess.run(
            [str(executable), "--no-proxy-server", *arguments],
            capture_output=True,
            text=True,
            timeout=70,
        )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"Workspace TLS probe timed out: {error.stderr!r}") from error


def require_result(result, code, marker=None):
    if result.returncode != code or (
        marker is not None and marker not in result.stdout.splitlines()
    ):
        raise SystemExit(
            f"Unexpected TLS probe result ({result.returncode}).\n"
            f"{result.stdout}\n{result.stderr}"
        )


def make_tls_fixture(directory, openssl):
    cert = directory / "certificate.pem"
    key = directory / "key.pem"
    subprocess.run(
        [
            openssl, "req", "-x509", "-newkey", "rsa:2048",
            "-nodes", "-days", "1", "-subj", "/CN=127.0.0.1",
            "-addext", "subjectAltName=IP:127.0.0.1",
            "-addext", "basicConstraints=critical,CA:FALSE",
            "-keyout", str(key), "-out", str(cert),
        ],
        capture_output=True,
        check=True,
    )
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(cert, key)
    return cert, tls


def verify_virtual_webauthn(executable, appearance):
    with (
        tempfile.TemporaryDirectory(prefix="zitter-content-webauthn-") as directory,
        ThreadingHTTPServer(("127.0.0.1", 0), TlsFixture) as server,
    ):
        server.paths = []
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            probe = f"{WEBAUTHN_PROBE}=http://localhost:{server.server_port}/"
            root = Path(directory) / "unused"
            require_result(launch(executable, probe), 1)
            require_result(launch(executable, SMOKE, probe,
                                  f"--zitter-content-workspace-root={root}"), 1)
            wrong_host = probe.replace("localhost", "127.0.0.1")
            rejected = launch(executable, SMOKE, wrong_host, *appearance)
            require_result(rejected, 0)
            if "Zitter Content workspace smoke failed" not in rejected.stderr:
                raise SystemExit("The WebAuthn probe accepted a different host")
            if root.exists() or server.paths:
                raise SystemExit("Rejected WebAuthn invocation created a profile or loaded a page")
            require_result(launch(executable, SMOKE, probe, *appearance), 0,
                           WEBAUTHN_MARKER)
            if server.paths.count("/") != 1:
                raise SystemExit(f"Unexpected WebAuthn fixture requests: {server.paths}")
            print("Content virtual WebAuthn registration and assertion passed")
        finally:
            server.shutdown()
            thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    parser.add_argument("--trusted-url",
                        help="Also load this HTTPS URL with the browser's normal trust settings")
    parser.add_argument("--virtual-webauthn", action="store_true",
                        help="Also register and use a virtual key on a disposable loopback page")
    options = parser.parse_args()
    if os.name == "nt" and options.renderer == "metal":
        parser.error("Metal requires macOS")
    executable = options.app
    if os.name != "nt":
        executable /= "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")
    appearance = (f"--zitter-content-workspace-theme={options.theme}",
                  f"--zitter-rust-raster={options.renderer}")

    with (
        tempfile.TemporaryDirectory(prefix="zitter-content-tls-") as directory,
        ThreadingHTTPServer(("127.0.0.1", 0), TlsFixture) as server,
    ):
        cert, tls = make_tls_fixture(Path(directory), options.openssl)
        server.socket = tls.wrap_socket(server.socket, server_side=True)
        server.paths = []
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            client = HTTPSConnection(
                "127.0.0.1",
                server.server_port,
                context=ssl.create_default_context(cafile=str(cert)),
                timeout=5,
            )
            try:
                client.request("GET", "/control")
                if client.getresponse().status != 200:
                    raise SystemExit("TLS fixture failed with its own trust anchor")
            finally:
                client.close()

            url = f"https://127.0.0.1:{server.server_port}/"
            probe = f"--zitter-content-workspace-tls-probe={url}"
            trusted_probe = f"{TRUSTED_PROBE}={url}"
            require_result(launch(executable, SMOKE, probe, *appearance), 0, MARKER)
            rejected = launch(executable, SMOKE, trusted_probe, *appearance)
            require_result(rejected, 0)
            if (TRUSTED_MARKER in rejected.stdout.splitlines()
                    or "Zitter Content workspace smoke failed" not in rejected.stderr):
                raise SystemExit("The trusted-page probe did not refuse the untrusted certificate")
            for switch in BYPASSES:
                require_result(launch(executable, SMOKE, probe, switch), 1)
            require_result(launch(executable, probe), 1)
            require_result(launch(executable, trusted_probe), 1)
            require_result(launch(executable, SMOKE, probe, trusted_probe), 1)
            if server.paths != ["/control"]:
                raise SystemExit(f"Unexpected HTTPS requests: {server.paths}")
            print("Content TLS probe passed")
        finally:
            server.shutdown()
            thread.join()
    if options.trusted_url:
        require_result(launch(executable, SMOKE, f"{TRUSTED_PROBE}={options.trusted_url}",
                              *appearance), 0, TRUSTED_MARKER)
        print("Content trusted HTTPS navigation passed")
    if options.virtual_webauthn:
        verify_virtual_webauthn(executable, appearance)


if __name__ == "__main__":
    main()
