#!/usr/bin/env python3
"""Verify signed Content Lab navigation against a disposable local reputation list."""

import argparse
import base64
import hashlib
import os
import plistlib
import secrets
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


def varint(value):
    encoded = bytearray()
    while value >= 128:
        encoded.append((value & 127) | 128)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def field(number, value):
    if isinstance(value, int):
        return varint(number << 3) + varint(value)
    return varint((number << 3) | 2) + varint(len(value)) + value


def read_varint(payload, offset):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(payload):
            break
        byte = payload[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("invalid protobuf varint")


def byte_fields(payload):
    offset = 0
    while offset < len(payload):
        tag, offset = read_varint(payload, offset)
        if tag < 8 or tag & 7 != 2:
            raise ValueError("unexpected protobuf tag")
        length, offset = read_varint(payload, offset)
        end = offset + length
        if end > len(payload):
            raise ValueError("truncated protobuf field")
        yield tag >> 3, payload[offset:end]
        offset = end


# Wire fields from the pinned Chromium Safe Browsing protobuf definitions.
BLOCKED_HASH = hashlib.sha256(b"127.0.0.1/reputation/blocked").digest()
BLOCKED_PREFIX = BLOCKED_HASH[:4]


def v4_list_update():
    raw_hashes = field(1, len(BLOCKED_PREFIX)) + field(2, BLOCKED_PREFIX)
    additions = field(1, 1) + field(2, raw_hashes)  # RAW
    checksum = field(1, hashlib.sha256(BLOCKED_PREFIX).digest())
    update = b"".join((
        field(1, 5),  # SOCIAL_ENGINEERING
        field(2, 1),  # URL
        field(3, 4),  # OSX_PLATFORM
        field(4, 2),  # FULL_UPDATE
        field(5, additions), field(7, b"zitter-test-state"), field(8, checksum),
    ))
    return field(1, update) + field(2, field(1, 600))


def v4_full_hash_response():
    match = b"".join((
        field(1, 5), field(2, 4), field(3, field(1, BLOCKED_HASH)),
        field(5, field(1, 600)), field(6, 1),
    ))
    return field(1, match) + field(3, field(1, 600))


def v5_list_update(payload):
    names = [value for number, value in byte_fields(payload) if number == 1]
    if b"se-4b" not in names or len(names) != len(set(names)):
        raise ValueError("invalid V5 list names")
    lists = []
    for name in names:
        update = field(1, name) + field(2, b"zitter-test-state") + field(6, field(1, 600))
        prefixes = BLOCKED_PREFIX if name == b"se-4b" else b""
        if prefixes:
            # One Rice value is represented by its big-endian integer alone.
            additions = field(1, int.from_bytes(prefixes, "big"))
            update += field(4, additions)
        update += field(7, hashlib.sha256(prefixes).digest())
        lists.append(field(1, update))
    return b"".join(lists)


def v5_full_hash_response(payload):
    if list(byte_fields(payload)) != [(1, BLOCKED_PREFIX)]:
        raise ValueError("unexpected V5 full-hash request payload")
    full_hash = field(1, BLOCKED_HASH) + field(2, field(1, 2))  # SOCIAL_ENGINEERING
    return field(1, full_hash) + field(2, field(1, 600))


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        route = urlsplit(self.path)
        if route.path.startswith(("/v4/", "/v5/")):
            self.send_provider_response(route)
            return
        if self.path != "/favicon.ico":
            self.server.requests.append(self.path)
        if self.path == "/reputation/redirect":
            self.send_response(302)
            self.send_header("Location", "/reputation/blocked")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path != "/reputation/safe":
            self.send_error(404)
            return
        body = (
            b"<!doctype html><title>Safe loopback page</title>"
            b'<iframe src="/reputation/blocked"></iframe>'
        )
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_provider_response(self, route):
        self.server.provider_requests.append(route.path)
        query = parse_qs(route.query)
        payloads = query.get("$req", [])
        if (query.get("key") != ["zitter-loopback-test-key"] or len(payloads) != 1
                or self.headers.get("Cookie") or self.headers.get("Referer")
                or self.headers.get("Authorization")):
            self.server.provider_errors.append("invalid provider credentials, headers or payload")
            self.send_error(400)
            return
        try:
            payload = base64.b64decode(payloads[0], altchars=b"-_", validate=True)
            if route.path == "/v4/threatListUpdates:fetch":
                response = v4_list_update()
            elif route.path == "/v4/fullHashes:find":
                if (field(1, BLOCKED_PREFIX) not in payload or BLOCKED_HASH in payload
                        or b"/reputation/" in payload):
                    raise ValueError("unexpected V4 full-hash request payload")
                response = v4_full_hash_response()
            elif route.path == "/v5/hashLists:batchGet":
                response = v5_list_update(payload)
            elif route.path == "/v5/hashes:search":
                response = v5_full_hash_response(payload)
            else:
                raise ValueError(f"unexpected provider route {route.path}")
        except ValueError as error:
            self.server.provider_errors.append(str(error))
            self.send_error(400)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/x-protobuf")
        self.send_header("Content-Length", str(len(response)))
        self.send_header("Set-Cookie", "provider-test=must-not-return; Path=/")
        self.end_headers()
        self.wfile.write(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != "dev.zitter.content-lab":
        parser.error("the reputation probe requires the Content Lab bundle")
    executable = app / "Contents/MacOS" / info["CFBundleExecutable"]

    with ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server:
        server.requests = []
        server.provider_requests = []
        server.provider_errors = []
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            v5_feature = "SafeBrowsingLocalListsUseSBv5"
            for protocol, v5, profile, provider in (
                ("V4", False, "work", False),
                ("V4", False, "private", False),
                ("V5", True, "work", False),
                ("V5", True, "private", False),
                ("V4 provider", False, "work", True),
                ("V5 provider", True, "work", True),
                ("V5 provider", True, "private", True),
            ):
                server.requests.clear()
                server.provider_requests.clear()
                server.provider_errors.clear()
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory:
                    root = Path(directory).resolve()
                    if profile == "work":
                        (root / "work").mkdir()
                        key = root / "probe-cookie-key"
                        key.write_bytes(secrets.token_bytes(32))
                        key.chmod(0o600)
                    command = [
                        executable, "--zitter-content-navigation-reputation-probe",
                        f"--zitter-content-profile-probe=http://127.0.0.1:{server.server_port}/reputation/safe",
                        f"--zitter-content-probe-profile={profile}",
                        f"--zitter-content-probe-root={root}",
                        "--no-proxy-server",
                        *(["--zitter-content-navigation-reputation-provider-probe"]
                          if provider else []),
                        *([f"--enable-features={v5_feature}"] if v5 else
                          [f"--disable-features={v5_feature}"]),
                    ]
                    try:
                        result = subprocess.run(
                            command, timeout=40, capture_output=True, text=True,
                            env={**os.environ, "ZITTER_SAFE_BROWSING_API_KEY_FILE": str(root / "absent")},
                        )
                    except subprocess.TimeoutExpired as error:
                        raise SystemExit(
                            f"{protocol} {profile} reputation probe timed out.\n"
                            f"Navigation requests: {server.requests!r}\n"
                            f"Provider requests: {server.provider_requests!r}"
                        ) from error
                if (result.returncode or
                    "Zitter Content blocked unsafe direct, redirected and subframe navigations"
                    not in result.stdout.splitlines()):
                    raise SystemExit(
                        f"{protocol} {profile} reputation probe failed.\n"
                        f"Provider requests {server.provider_requests}, errors {server.provider_errors}\n"
                        f"{result.stdout}\n{result.stderr}"
                    )
                if server.requests != ["/reputation/safe", "/reputation/redirect"]:
                    raise SystemExit(f"Unexpected {protocol} {profile} navigation requests: {server.requests}")
                expected_provider_requests = []
                if provider:
                    expected_provider_requests = (
                        ["/v5/hashLists:batchGet", "/v5/hashes:search"] if v5 else
                        ["/v4/threatListUpdates:fetch", "/v4/fullHashes:find"]
                    )
                if (server.provider_requests != expected_provider_requests
                        or server.provider_errors):
                    raise SystemExit(
                        f"Unexpected provider traffic: {server.provider_requests}, "
                        f"{server.provider_errors}"
                    )
                print(f"{protocol} {profile} blocked the direct, redirected and subframe requests")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
