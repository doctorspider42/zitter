#!/usr/bin/env python3
"""Verify Content Lab's restricted download probe against a loopback fixture."""

import argparse
import base64
from contextlib import closing
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import plistlib
import shutil
import ssl
import sqlite3
import subprocess
import tempfile
import threading
import time
import unicodedata
import uuid
import wave
from io import BytesIO
from urllib.parse import parse_qs, urlsplit

from profile_migration_trial import (
    IMPORTED_DOWNLOAD_MARKER, WINDOWS_LOCAL_STATE, import_workspace_profiles,
    inventory, prepare, rollback,
)
from verify_content_tls import make_tls_fixture
from notarize_app import CONTENT_BUNDLE_ID


PAYLOAD = b"Zitter Content download probe\n"
PUBLISH_RACE_PATH = "/publish-race"
PUBLISH_RACE_PAYLOAD = PAYLOAD * 256
FILE_REPUTATION_TAMPER_PATH = "/file-reputation-tamper"
RESUME_PAYLOAD = b"original download prefix and suffix\n" * 384
RESUME_OFFSET = 4096
CRASH_INITIAL_OFFSET = 1024
RESUME_ETAG = '"zitter-original"'
RESUME_REPLACEMENT = b"A different, complete representation\n" * 192
RESUME_OVERSIZED = b"x" * (64 * 1024 + 1)
RESUME_EXISTING = b"Created while the download was interrupted\n"
CASES = {
    "/text": ("text/plain", PAYLOAD),
    "/binary": ("application/octet-stream", PAYLOAD),
    "/oversized": ("text/plain", b"x" * (64 * 1024 + 1)),
}
FILE_TYPE_CASES = {
    "/attachment": ('attachment; filename="report.csv"', "report.csv"),
    "/report.csv": ("attachment", "report.csv"),
    "/utf8": ("attachment; filename*=UTF-8''r%C3%A9sum%C3%A9.txt", "résumé.txt"),
    "/executable": ('attachment; filename="report.txt.exe"', None),
    "/script": ('attachment; filename="report.command"', None),
    "/archive": ('attachment; filename="report.zip"', None),
    "/unknown": ('attachment; filename="report.zitterunknown"', None),
    "/no-extension": ('attachment; filename="report"', None),
    "/filename-precedence": (
        "attachment; filename=\"report.txt\"; filename*=UTF-8''report.exe", None
    ),
}
def wav_fixture():
    output = BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        audio.writeframes(bytes(160))
    return output.getvalue()


USER_FILE_TYPES = {
    "csv": ("text/csv", b"name,value\nfixture,1\n"),
    "pdf": ("application/pdf", b"%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF\n"),
    "png": ("image/png", base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+X8ZkAAAAASUVORK5CYII=")),
    "wav": ("audio/wav", wav_fixture()),
}


def make_media_fixtures(ffmpeg, directory):
    formats = {
        "m4a": ("audio/mp4", "aac", False),
        "mov": ("video/quicktime", "mpeg4", True),
        "mp3": ("audio/mpeg", "libmp3lame", False),
        "mp4": ("video/mp4", "mpeg4", True),
        "ogg": ("audio/ogg", "libopus", False),
        "webm": ("video/webm", "libvpx", True),
    }
    fixtures = {}
    for suffix, (mime, codec, video) in formats.items():
        output = directory / f"media.{suffix}"
        source = "color=c=black:s=32x32:r=10" if video else "sine=frequency=440:sample_rate=44100"
        codec_args = ["-c:v" if video else "-c:a", codec]
        subprocess.run(
            [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
             "-f", "lavfi", "-i", source, "-t", "0.25", *codec_args, output],
            check=True, timeout=20,
        )
        fixtures[suffix] = (mime, output.read_bytes())
    return fixtures


WORKSPACE_PAYLOADS = {
    "work": b"Work download fixture\n",
    "personal": b"Personal download fixture\n",
}
CLOSE_PROBE_PAYLOAD = WORKSPACE_PAYLOADS["work"] * 256
WORKSPACE_CASES = {
    f"/download/{profile}": ("text/plain", body)
    for profile, body in WORKSPACE_PAYLOADS.items()
}


def reputation_hash(path):
    return sha256(f"127.0.0.1{path}".encode()).digest()


def proto_bytes(field, value):
    return bytes([(field << 3) | 2, len(value)]) + value


class DownloadFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/favicon.ico":
            self.send_error(404)
            return
        self.server.requests.append(self.path)
        if urlsplit(self.path).path == "/reputation":
            self.serve_reputation()
            return
        if self.path == getattr(self.server, "reputation_target", None):
            self.server.reputation_download_cookie = self.headers.get("Cookie")
        if (self.path in ("/resume", "/download/work") and
                getattr(self.server, "resume_case", None)):
            self.serve_resumable_download()
            return
        redirect = getattr(self.server, "redirects", {}).get(self.path)
        if redirect:
            self.send_response(302)
            self.send_header("Location", redirect)
            if getattr(self.server, "reputation_case", None):
                self.send_header("Set-Cookie", "reputation_probe=private; Path=/; Secure")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        destination = getattr(self.server, "navigation_pages", {}).get(self.path)
        if destination:
            body = f"<script>location.href = {json.dumps(destination)};</script>".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/user-anchor":
            body = b'<a href="/text" download>Save the text fixture</a>'
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path in ("/user-stream", "/user-stream-oversized"):
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", 'attachment; filename="download-probe.txt"')
            self.send_header("Connection", "close")
            self.end_headers()
            if self.path == "/user-stream":
                self.wfile.write(PAYLOAD)
            else:
                try:
                    block = b"x" * (1024 * 1024)
                    for _ in range(17):
                        self.wfile.write(block)
                    # Keep the stream open until the browser stops it.
                    self.server.stream_release.wait(45)
                except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                    pass
            return
        response = CASES.get(self.path) or WORKSPACE_CASES.get(self.path)
        response = getattr(self.server, "download_overrides", {}).get(self.path, response)
        disposition = 'attachment; filename="download-probe.txt"'
        if self.path in FILE_TYPE_CASES:
            disposition, _ = FILE_TYPE_CASES[self.path]
            response = ("text/plain", PAYLOAD)
        user_type = self.path.removeprefix("/user-file/")
        user_file_types = getattr(self.server, "user_file_types", USER_FILE_TYPES)
        if self.path.startswith("/user-file/") and user_type in user_file_types:
            disposition = f'attachment; filename="download-probe.{user_type}"'
            response = user_file_types[user_type]
        if self.path == "/user-file/mismatched-pdf":
            disposition = 'attachment; filename="download-probe.pdf"'
            response = ("text/plain", PAYLOAD)
        if self.path == "/user-file/mismatched-wav":
            disposition = 'attachment; filename="download-probe.wav"'
            response = ("text/plain", PAYLOAD)
        resume = None
        if self.path == PUBLISH_RACE_PATH:
            response = ("text/plain", PUBLISH_RACE_PAYLOAD)
            resume = self.server.publish_resume
        elif self.path == "/download/work":
            resume = getattr(self.server, "close_resume", None)
            if resume:
                response = ("text/plain", CLOSE_PROBE_PAYLOAD)
        if response is None:
            self.send_error(404)
            return
        mime, body = response
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Disposition", disposition)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.path == FILE_REPUTATION_TAMPER_PATH:
            self.wfile.write(body[:1024])
            self.wfile.flush()
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                staged = next(iter(self.server.resume_directory.glob("*.crdownload")), None)
                if staged and staged.stat().st_size >= 1024:
                    with staged.open("r+b") as stream:
                        stream.write(bytes([body[0] ^ 1]))
                    self.server.staged_file_changed = True
                    break
                time.sleep(0.025)
            self.wfile.write(body[1024:])
            return
        if resume:
            self.wfile.write(body[:1024])
            self.wfile.flush()
            if resume.wait(timeout=30):
                try:
                    self.wfile.write(body[1024:])
                except (BrokenPipeError, ConnectionResetError):
                    pass
            return
        self.wfile.write(body)

    def serve_reputation(self):
        server = self.server
        case = getattr(server, "reputation_case", None)
        if case is None:
            self.send_error(404)
            return
        params = parse_qs(urlsplit(self.path).query)
        try:
            if set(params) != {"$req", "$ct"} or params["$ct"] != ["application/x-protobuf"]:
                raise ValueError("unexpected query parameters")
            if any(header in self.headers for header in ("Cookie", "Authorization", "Referer")):
                raise ValueError("lookup contained credentials or a referrer")
            if any((server.resume_directory / name).exists()
                   for name in ("download-probe.txt", "chosen-save.txt")):
                raise ValueError("download published its final file before the verdict")
            encoded, = params["$req"]
            request = base64.b64decode(encoded, altchars=b"-_", validate=True)
            chunks = [request[i:i + 6] for i in range(0, len(request), 6)]
            prefixes = {chunk[2:] for chunk in chunks}
            required = {reputation_hash(path)[:4]
                        for path in (server.reputation_source, server.reputation_target)}
            if (any(len(chunk) != 6 or not chunk.startswith(b"\x0a\x04") for chunk in chunks)
                    or len(prefixes) != len(chunks) or not required <= prefixes):
                raise ValueError("lookup did not contain deduplicated prefixes from both hops")
        except ValueError as error:
            server.reputation_error = str(error)
            self.send_error(400, str(error))
            return
        if case == "http-error":
            self.send_error(503)
            return
        if case == "redirect":
            self.send_response(302)
            self.send_header("Location", "/should-not-request")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if case == "timeout":
            time.sleep(6.5)
        source = reputation_hash(server.reputation_source)
        target = reputation_hash(server.reputation_target)
        cache_seconds = b"" if case == "resume-uncached" else b"\x08\x1e"
        duration = proto_bytes(2, cache_seconds)
        threats = {
            "source-match": (source, 7),
            "artifact-url-blocked": (source, 7),
            "target-match": (target, 1),
            "prefix-collision": (target[:4] + bytes([target[4] ^ 1]) + target[5:], 7),
            "unknown-threat": (target, 127),
            "unrequested-prefix": (b"\xff" * 32, 7),
        }
        body = duration
        if case in threats:
            full_hash, threat = threats[case]
            detail = proto_bytes(2, bytes([8, threat]))
            body += proto_bytes(1, proto_bytes(1, full_hash) + detail)
        if case == "malformed":
            body = b"\x80"
        if case == "missing-duration":
            body = b""
        if case == "invalid-duration":
            # One billion nanoseconds exceeds the protobuf Duration field limit.
            body = proto_bytes(2, b"\x10\x80\x94\xeb\xdc\x03")
        if case == "oversized":
            body = b"x" * (1024 * 1024 + 1)
        self.send_response(200)
        self.send_header("Content-Type", "application/x-protobuf")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass

    def serve_resumable_download(self):
        self.protocol_version = "HTTP/1.1"
        self.close_connection = True
        server = self.server
        request_range = self.headers.get("Range")
        server.resume_requests.append((request_range, self.headers.get("If-Range")))
        attempt = len(server.resume_requests)
        case = server.resume_case
        payload = RESUME_PAYLOAD
        disposition = 'attachment; filename="download-probe.txt"'
        if attempt == 1:
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", disposition)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("ETag", RESUME_ETAG)
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Connection", "close")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            first_offset = CRASH_INITIAL_OFFSET if case == "crash-held" else RESUME_OFFSET
            self.wfile.write(payload[:first_offset])
            self.wfile.flush()
            if case == "crash-held":
                # Advance the active checkpoint once, then keep the response open.
                if server.checkpoint_release.wait(timeout=20):
                    self.wfile.write(payload[first_offset:RESUME_OFFSET])
                    self.wfile.flush()
                    server.resume_release.wait(timeout=30)
                return
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if any(path.stat().st_size >= RESUME_OFFSET
                       for path in server.resume_directory.glob("*.crdownload")):
                    break
                time.sleep(0.025)
            else:
                server.resume_error = "The original partial file never appeared"
            self.close_connection = True
            return

        offset = getattr(server, "resume_offset", RESUME_OFFSET)
        replacement = case in ("replacement", "oversized-restart")
        can_restart = replacement or case == "corrupt-prefix"
        if attempt > (3 if can_restart or case.startswith("manual") else 2) or (
                request_range != f"bytes={offset}-"
                and not (can_restart and attempt == 3 and request_range is None)):
            server.resume_error = f"Unexpected resume request: {server.resume_requests}"
            self.log_error("%s", server.resume_error)
            self.send_error(400)
            return

        if case.startswith("manual") and attempt == 2:
            self.send_response(503)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return
        if case == "collision":
            (server.resume_directory / "chosen-save.txt").write_bytes(RESUME_EXISTING)
        if attempt == 2 and case in ("corrupt-prefix", "hardlink", "symlink"):
            partial, = server.resume_directory.glob("*.crdownload")
            if case == "corrupt-prefix":
                partial.write_bytes(b"x" * offset)
            else:
                sentinel = server.resume_directory.parent / "sentinel.txt"
                sentinel.write_bytes(payload[:offset])
                partial.unlink()
                if case == "hardlink":
                    os.link(sentinel, partial)
                else:
                    partial.symlink_to(sentinel)
        if case == "corrupt-prefix" and attempt == 3:
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", disposition)
            self.send_header("ETag", RESUME_ETAG)
            self.send_header("Content-Length", str(len(payload)))
        elif replacement:
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", disposition)
            self.send_header("ETag", '"zitter-replacement"')
            if case == "replacement":
                payload = RESUME_REPLACEMENT
                self.send_header("Content-Length", str(len(payload)))
            else:
                payload = RESUME_OVERSIZED
        else:
            self.send_response(206)
            if case == "bad-type":
                self.send_header("Content-Type", "application/octet-stream")
            if case == "bad-name":
                self.send_header("Content-Disposition", 'attachment; filename="report.exe"')
            if case == "oversized-range":
                payload = RESUME_OVERSIZED
            self.send_header("ETag", RESUME_ETAG)
            self.send_header("Content-Range",
                             f"bytes {offset}-{len(payload) - 1}/{len(payload)}")
            payload = payload[offset:]
            self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        try:
            if case == "manual-held":
                self.wfile.write(payload[:1024])
                self.wfile.flush()
                server.resume_release.wait(timeout=40)
            else:
                self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass


def probe_args(executable, port, root, endpoint):
    return [
        str(executable),
        f"--zitter-content-download-probe=http://127.0.0.1:{port}{endpoint}",
        f"--zitter-content-probe-root={root}",
        "--no-proxy-server",
    ]


def run_case(executable, port, root, endpoint, blocked, save_action=None,
             resume_blocked=False, expect_interrupted=False):
    args = probe_args(executable, port, root, endpoint)
    if blocked:
        args.append("--zitter-content-expect-download-blocked")
    if save_action:
        args.append(f"--zitter-content-download-save-probe={save_action}")
    if resume_blocked:
        args.append("--zitter-content-expect-download-resume-blocked")
    if expect_interrupted:
        args.append("--zitter-content-expect-download-interrupted")
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=45)
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"{endpoint} timed out. Output: {error.stderr!r}") from error
    if (result.returncode or "Zitter Content download probe passed" not in result.stdout or
            (save_action == "late" and
             "Zitter Content save picker detached its late reply" not in result.stdout)):
        raise SystemExit(
            f"{endpoint} failed ({result.returncode}).\n{result.stdout}\n{result.stderr}"
        )


def require_quarantine(path, windows, private=True):
    if windows:
        stream = Path(str(path) + ":Zone.Identifier")
        try:
            values = stream.read_text(encoding="utf-8-sig").splitlines()
        except OSError as error:
            raise SystemExit(f"Windows quarantine did not tag the download: {error}") from error
        if "ZoneId=3" not in values:
            raise SystemExit("The Windows download was not tagged as Internet zone")
        if private and any(
            value.startswith("ReferrerUrl=")
            or (value.startswith("HostUrl=") and value != "HostUrl=about:internet")
            for value in values
        ):
            raise SystemExit("The private download recorded a Windows origin")
        return
    attrs = subprocess.check_output(
        ["/usr/bin/xattr", str(path)], text=True
    ).splitlines()
    if "com.apple.quarantine" not in attrs:
        raise SystemExit("The download was not tagged by macOS quarantine")
    if private and "com.apple.metadata:kMDItemWhereFroms" in attrs:
        raise SystemExit("The private download recorded an origin attribute")


def verify_resume(executable, server, options, windows):
    cases = (
        ("partial", None, "download-probe.txt", RESUME_PAYLOAD),
        ("partial", "accept", "chosen-save.txt", RESUME_PAYLOAD),
        ("replacement", "accept", "chosen-save.txt", RESUME_REPLACEMENT),
        ("bad-type", "accept", None, None),
        ("bad-name", "accept", None, None),
        ("oversized-range", "accept", None, None),
        ("oversized-restart", "accept", None, None),
        ("collision", "accept", "chosen-save.txt", RESUME_EXISTING),
        ("corrupt-prefix", "accept", "chosen-save.txt", RESUME_PAYLOAD),
        ("hardlink", "accept", None, None),
        ("symlink", "accept", None, None),
    )
    for case, save_action, filename, payload in cases:
        if case == "symlink" and windows:
            continue
        with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
            root = Path(temp).resolve()
            downloads = root / "downloads"
            downloads.mkdir(mode=0o700)
            server.resume_case = case
            server.resume_directory = downloads
            server.resume_requests = []
            server.resume_error = None
            rejected = filename is None or case == "collision"
            run_case(executable, server.server_port, root, "/resume",
                     blocked=False, save_action=save_action, resume_blocked=rejected)
            if (server.resume_error or server.resume_requests[:2] != [
                    (None, None), (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)]):
                raise SystemExit(f"{case} did not resume the expected range: "
                                 f"{server.resume_requests}. {server.resume_error}")
            actual = {path.name: path.read_bytes() for path in downloads.iterdir()}
            expected = {filename: payload} if filename else {}
            fixture_entries = {downloads}
            if case in ("hardlink", "symlink"):
                sentinel = root / "sentinel.txt"
                if sentinel.read_bytes() != RESUME_PAYLOAD[:RESUME_OFFSET]:
                    raise SystemExit(f"{case} modified a different file")
                fixture_entries.add(sentinel)
            if case == "corrupt-prefix" and server.resume_requests[2:] != [(None, None)]:
                raise SystemExit("The modified prefix did not force a full restart")
            if actual != expected or set(root.iterdir()) != fixture_entries:
                raise SystemExit(f"{case} produced unexpected files: {list(actual)}")
            if options.require_quarantine and not rejected:
                require_quarantine(downloads / filename, windows)
            print(f"Content download resumption passed: {case} ({save_action or 'automatic path'})")

    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
        verify_workspace_resume(executable, server, Path(temp).resolve(), options, windows)


def verify_workspace_resume(executable, server, root, options, windows):
    args = prepare_workspace(executable, server, root, options, windows)
    args.append("--zitter-content-workspace-download-probe=resume")
    downloads = root / "work-downloads"
    server.resume_case = "manual"
    server.resume_directory = downloads
    server.resume_requests = []
    server.resume_error = None
    result = subprocess.run(args, capture_output=True, text=True, timeout=45)
    marker = "Zitter Content resumed the interrupted Workspace download"
    if result.returncode or marker not in result.stdout or (
            "Zitter Content workspace session probe passed" not in result.stdout):
        raise SystemExit(f"Workspace Retry failed.\n{result.stdout}\n{result.stderr}")
    resumed = (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)
    if server.resume_error or server.resume_requests != [(None, None), resumed, resumed]:
        raise SystemExit(f"Workspace Retry did not resume: {server.resume_requests}. "
                         f"{server.resume_error}")
    target = downloads / "download-probe.txt"
    if set(downloads.iterdir()) != {target} or target.read_bytes() != RESUME_PAYLOAD:
        raise SystemExit("Workspace Retry did not publish the original representation")
    if options.require_quarantine:
        require_quarantine(target, windows, private=False)
    rows = read_workspace_download_history(root, "work")
    if len(rows) != 1 or not rows[0][0] or rows[0][1:] != (1, len(RESUME_PAYLOAD)):
        raise SystemExit(f"Workspace Retry did not persist one completed download: {rows}")
    personal_history = root / "workspace-personal" / "History"
    if personal_history.exists() and read_workspace_download_history(root, "personal"):
        raise SystemExit("Workspace Retry wrote to the wrong profile history")
    print("Content download resumption passed: Workspace Retry")


def verify_resume_after_crash(executable, server, root, options, windows):
    args = prepare_workspace(executable, server, root, options, windows)
    work = root / "work-downloads"
    database = root / "workspace-work" / "History"
    partial = work / "download-probe.txt.crdownload"
    target = work / "download-probe.txt"
    prefix = RESUME_PAYLOAD[:RESUME_OFFSET]
    server.resume_case = "crash-held"
    server.resume_release = threading.Event()
    server.checkpoint_release = threading.Event()
    server.resume_directory = work
    server.resume_requests = []
    server.resume_error = None

    snapshot = root / "checkpoint-snapshot"

    def checkpoint(live=False):
        try:
            if live:
                # History opens SQLite exclusively. Use a disposable snapshot
                # to time the kill, then verify the real DB after process exit.
                shutil.copyfile(database, snapshot)
            path = snapshot if live else database
            with closing(sqlite3.connect(path, timeout=0.25)) as db:
                return db.execute(
                    "SELECT guid, state, received_bytes, current_path, target_path, hash, etag "
                    "FROM downloads"
                ).fetchall()
        except (FileNotFoundError, sqlite3.DatabaseError):
            if not live:
                raise
            return []

    log = root / "crash-probe.log"
    with (log.open("w") as output,
          subprocess.Popen(
              [*args, "--zitter-content-workspace-download-probe=resume-save"],
              stdout=output, stderr=subprocess.STDOUT
          ) as process):
        try:
            deadline = time.monotonic() + 12

            def wait_for_checkpoint(offset):
                while process.poll() is None and time.monotonic() < deadline:
                    rows = checkpoint(live=True)
                    if rows and rows[0][2] == offset:
                        expected = (0, offset, str(partial), str(target),
                                    sha256(prefix[:offset]).digest(), RESUME_ETAG)
                        if (len(rows) != 1 or not rows[0][0] or rows[0][1:] != expected or
                                set(work.iterdir()) != {partial} or
                                partial.read_bytes() != prefix[:offset] or
                                server.resume_requests != [(None, None)]):
                            raise SystemExit(f"Unexpected active checkpoint: {rows}")
                        return rows
                    time.sleep(0.05)
                raise SystemExit(f"No durable checkpoint at offset {offset}.\n{log.read_text()}")

            first = wait_for_checkpoint(CRASH_INITIAL_OFFSET)
            server.checkpoint_release.set()
            rows = wait_for_checkpoint(RESUME_OFFSET)
            guid = rows[0][0]
            if guid != first[0][0]:
                raise SystemExit("Advancing the active checkpoint changed its GUID")
            process.kill()
            process.wait(timeout=10)
        finally:
            server.checkpoint_release.set()
            server.resume_release.set()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)

    def run(phase, marker):
        result = subprocess.run(
            [*args, f"--zitter-content-workspace-download-probe=resume-{phase}"],
            capture_output=True, text=True, timeout=45,
        )
        if (result.returncode or marker not in result.stdout or
                "Zitter Content workspace session probe passed" not in result.stdout):
            raise SystemExit(f"Crash recovery {phase} failed.\n{result.stdout}\n{result.stderr}")

    if checkpoint() != rows or not partial.is_file():
        raise SystemExit("Killing the browser lost the durable active checkpoint")
    bad_prefix = bytes([prefix[0] ^ 1]) + prefix[1:]
    partial.write_bytes(bad_prefix)
    run("reject", "Zitter Content rejected an interrupted download record")
    if partial.read_bytes() != bad_prefix or server.resume_requests != [(None, None)]:
        raise SystemExit("Startup touched the corrupt prefix or issued a request")

    # Bytes past the recorded offset must be discarded before appending the range.
    uncheckpointed = prefix + b"Uncheckpointed trailing bytes"
    partial.write_bytes(uncheckpointed)
    run("inspect", "Zitter Content restored an interrupted download without requesting it")
    if server.resume_requests != [(None, None)] or partial.read_bytes() != uncheckpointed:
        raise SystemExit("Startup issued a request or altered the uncheckpointed tail")

    run("restore", "Zitter Content resumed the interrupted Workspace download")
    if (server.resume_error or
            server.resume_requests != [(None, None), (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)] or
            set(work.iterdir()) != {target} or target.read_bytes() != RESUME_PAYLOAD or
            read_workspace_download_history(root, "work") != [(guid, 1, len(RESUME_PAYLOAD))] or
            list((root / "personal-downloads").iterdir())):
        raise SystemExit("Crash recovery changed the wrong file, offset, or history record")
    if options.require_quarantine:
        require_quarantine(target, windows, private=False)
    print("Content active download recovered after a forced process exit")


def verify_resume_after_restart(executable, server, root, options, windows, abandon=False):
    args = prepare_workspace(executable, server, root, options, windows)
    work = root / "work-downloads"
    server.resume_case = "manual-held" if abandon else "manual"
    server.resume_release = threading.Event()
    server.resume_directory = work
    server.resume_requests = []
    server.resume_error = None
    database = root / "workspace-work" / "History"

    def run(phase, marker):
        result = subprocess.run(
            [*args, f"--zitter-content-workspace-download-probe=resume-{phase}"],
            capture_output=True, text=True, timeout=45,
        )
        if result.returncode or marker not in result.stdout or (
                "Zitter Content workspace session probe passed" not in result.stdout):
            raise SystemExit(f"Durable download {phase} failed.\n"
                             f"{result.stdout}\n{result.stderr}")

    run("save", "Zitter Content checkpointed an interrupted download")
    resumed = (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)
    before_retry = [(None, None), resumed]
    prefix = RESUME_PAYLOAD[:RESUME_OFFSET]
    partial = work / "download-probe.txt.crdownload"
    target = work / "download-probe.txt"
    if set(work.iterdir()) != {partial} or partial.read_bytes() != prefix:
        raise SystemExit("The interrupted transfer did not leave the expected partial file")
    connection = sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)
    try:
        rows = connection.execute(
            "SELECT guid, state, received_bytes, current_path, target_path, hash, etag "
            "FROM downloads"
        ).fetchall()
    finally:
        connection.close()
    if len(rows) != 1 or not rows[0][0] or rows[0][1:] != (
            4, RESUME_OFFSET, str(partial), str(target), sha256(prefix).digest(), RESUME_ETAG):
        raise SystemExit(f"The interrupted transfer did not persist its checkpoint: {rows}")
    guid = rows[0][0]
    if abandon:
        try:
            run("abandon", "Zitter Content confirmed close during a download")
        finally:
            server.resume_release.set()
        if server.resume_requests != [*before_retry, resumed] or server.resume_error:
            raise SystemExit("The abandoned download did not restart its request")
        if list(work.iterdir()) or read_workspace_download_history(root, "work"):
            raise SystemExit("Closing a resumed transfer left an obsolete checkpoint")
        print("Content removed the abandoned restart checkpoint")
        return

    run("inspect", "Zitter Content restored an interrupted download without requesting it")
    if server.resume_requests != before_retry:
        raise SystemExit("Reopening an interrupted download issued an unsolicited request")

    # A profile copy can contain foreign paths. The Work manager must leave
    # Personal's file and its own original partial untouched.
    sentinel = root / "personal-downloads" / partial.name
    sentinel.write_bytes(prefix)

    def set_current_path(new_path):
        connection = sqlite3.connect(database)
        try:
            connection.execute("UPDATE downloads SET current_path=? WHERE guid=?",
                               (str(new_path), guid))
            connection.commit()
        finally:
            connection.close()

    set_current_path(sentinel)
    run("reject", "Zitter Content rejected an interrupted download record")
    if sentinel.read_bytes() != prefix or partial.read_bytes() != prefix:
        raise SystemExit("Rejecting a foreign checkpoint modified a partial file")
    set_current_path(partial)
    corrupt = bytes([prefix[0] ^ 1]) + prefix[1:]
    partial.write_bytes(corrupt)
    run("reject", "Zitter Content rejected an interrupted download record")
    if partial.read_bytes() != corrupt:
        raise SystemExit("Rejecting a corrupt checkpoint modified the partial file")
    partial.write_bytes(prefix)
    original = work / "original-partial"
    partial.rename(original)
    try:
        os.link(sentinel, partial)
        run("reject", "Zitter Content rejected an interrupted download record")
        if not partial.samefile(sentinel) or sentinel.read_bytes() != prefix:
            raise SystemExit("Rejecting a hard-linked checkpoint modified a file")
    finally:
        if partial.exists():
            partial.unlink()
        original.rename(partial)
    if not windows:
        partial.rename(original)
        partial.symlink_to(sentinel)
        try:
            run("reject", "Zitter Content rejected an interrupted download record")
            if not partial.is_symlink() or sentinel.read_bytes() != prefix:
                raise SystemExit("Rejecting a linked checkpoint modified a file")
        finally:
            partial.unlink()
            original.rename(partial)
    if server.resume_requests != before_retry:
        raise SystemExit("Rejecting an interrupted record issued a request")

    run("restore", "Zitter Content resumed the interrupted Workspace download")
    if server.resume_error or server.resume_requests != [*before_retry, resumed]:
        raise SystemExit(f"Retry after reopening used unexpected requests: "
                         f"{server.resume_requests}. {server.resume_error}")
    if set(work.iterdir()) != {target} or target.read_bytes() != RESUME_PAYLOAD:
        raise SystemExit("Retry after reopening did not publish the original representation")
    if sentinel.read_bytes() != prefix:
        raise SystemExit("Retry modified another profile's file")
    if read_workspace_download_history(root, "work") != [(guid, 1, len(RESUME_PAYLOAD))]:
        raise SystemExit("Retry did not update the original history row")
    if options.require_quarantine:
        require_quarantine(target, windows, private=False)
    print("Content interrupted download recovered after restart")


def verify_user_resume_after_crash(executable, server, root, options):
    with os.fdopen(os.open(root / "probe-cookie-key",
                          os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as key:
        key.write(os.urandom(32))
    cert, tls = make_tls_fixture(root, options.openssl)
    (root / "download-probe-leaf.der").write_bytes(
        ssl.PEM_cert_to_DER_cert(cert.read_text(encoding="ascii")))
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    url = f"https://127.0.0.1:{server.server_port}/resume"
    downloads = root / "downloads"
    downloads.mkdir(mode=0o700)
    target = downloads / "chosen-save.txt"
    database = root / "download-profile" / "History"
    snapshot = root / "checkpoint-snapshot"
    log = root / "checkpoint.log"
    server.resume_case = "crash-held"
    server.resume_directory = downloads
    server.resume_release = threading.Event()
    server.checkpoint_release = threading.Event()
    server.resume_requests = []
    server.resume_error = None
    args = [
        str(executable), "--no-proxy-server",
        "--zitter-content-download-transport-probe",
        "--zitter-content-download-save-probe=user-accept",
        f"--zitter-content-download-probe={url}",
        f"--zitter-content-probe-root={root}",
    ]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with log.open("w") as output, subprocess.Popen(
            [*args, "--zitter-content-download-checkpoint-probe=create"],
            stdout=output, stderr=subprocess.STDOUT,
        ) as process:
            try:
                deadline = time.monotonic() + 20
                while process.poll() is None and time.monotonic() < deadline:
                    try:
                        shutil.copyfile(database, snapshot)
                        with closing(sqlite3.connect(snapshot, timeout=0.25)) as db:
                            rows = db.execute(
                                "SELECT current_path, target_path, received_bytes, "
                                "hash, etag, embedder_download_data FROM downloads"
                            ).fetchall()
                    except (FileNotFoundError, sqlite3.DatabaseError):
                        rows = []
                    if rows:
                        (partial, selected, size, digest, etag, approval), = rows
                        prefix = RESUME_PAYLOAD[:size]
                        if (selected != str(target) or size != CRASH_INITIAL_OFFSET
                                or digest != sha256(prefix).digest() or etag != RESUME_ETAG
                                or approval != "zitter-user-download-checkpoint-v1"
                                or set(downloads.iterdir()) != {Path(partial)}
                                or Path(partial).read_bytes() != prefix):
                            raise SystemExit(f"Invalid user checkpoint: {rows}")
                        break
                    time.sleep(0.05)
                else:
                    raise SystemExit(f"No user checkpoint before crash.\n{log.read_text()}")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
                server.checkpoint_release.set()
                server.resume_release.set()

        partial = Path(partial)
        partial.write_bytes(bytes([prefix[0] ^ 1]) + prefix[1:])
        rejected = subprocess.run(
            [*args, "--zitter-content-download-checkpoint-probe=reject"],
            capture_output=True, text=True, timeout=25,
        )
        if (rejected.returncode or "Zitter Content download probe passed" not in rejected.stdout
                or server.resume_requests != [(None, None)] or target.exists()):
            raise SystemExit(f"Changed checkpoint was not rejected.\n"
                             f"{rejected.stdout}\n{rejected.stderr}")
        # A crash can leave the file ahead of the last committed History row.
        # Use an unverified tail beyond the advertised end of the transfer to
        # catch both reuse of those bytes and a missing truncation.
        partial.write_bytes(prefix + b"x" * len(RESUME_PAYLOAD))

        server.resume_case = "partial"
        server.resume_offset = CRASH_INITIAL_OFFSET
        result = subprocess.run(
            [*args, "--zitter-content-download-checkpoint-probe=restore"],
            capture_output=True, text=True, timeout=25,
        )
        if (result.returncode or "Zitter Content download probe passed" not in result.stdout
                or server.resume_error or server.resume_requests != [
                    (None, None), (f"bytes={CRASH_INITIAL_OFFSET}-", RESUME_ETAG)]
                or set(downloads.iterdir()) != {target}
                or target.read_bytes() != RESUME_PAYLOAD):
            raise SystemExit(f"User checkpoint restore failed {server.resume_error!r}.\n"
                             f"{result.stdout}\n{result.stderr}")
        if options.require_quarantine:
            require_quarantine(target, False, private=False)
        print("Content restored the user-approved save target after a crash")
    finally:
        server.shutdown()
        thread.join()


def verify_private_interruption(executable, server, root):
    downloads = root / "downloads"
    downloads.mkdir(mode=0o700)
    server.resume_case = "manual"
    server.resume_directory = downloads
    server.resume_requests = []
    server.resume_error = None
    run_case(executable, server.server_port, root, "/resume", blocked=False,
             expect_interrupted=True)
    if server.resume_error or server.resume_requests != [
            (None, None), (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)]:
        raise SystemExit("The private transfer did not reach its interruption")
    if set(root.iterdir()) != {downloads} or list(downloads.iterdir()):
        raise SystemExit("The private profile left an unfinished download")
    print("Content discarded the private partial file")


def verify_file_types(executable, port, root, options, windows):
    downloads = root / "downloads"
    downloads.mkdir(mode=0o700)
    for endpoint, (_, name) in FILE_TYPE_CASES.items():
        run_case(executable, port, root, endpoint, blocked=name is None)
        entries = list(downloads.iterdir())
        if name is None:
            if entries:
                raise SystemExit(f"{endpoint} saved a file: {entries}")
            continue
        # APFS can expose names in a canonically equivalent Unicode form.
        if (len(entries) != 1 or unicodedata.normalize("NFC", entries[0].name) != name
                or entries[0].read_bytes() != PAYLOAD):
            raise SystemExit(f"{endpoint} did not save {name!r}: {entries}")
        if options.require_quarantine:
            require_quarantine(entries[0], windows)
        entries[0].unlink()
    if set(root.iterdir()) != {downloads}:
        raise SystemExit("The file type probe left a temporary browser profile")
    print("Content download file type policy passed")


def verify_transport(executable, http_server, root, options, windows):
    user_file_types = dict(USER_FILE_TYPES)
    if options.ffmpeg:
        user_file_types.update(make_media_fixtures(options.ffmpeg, root))
    cert, tls = make_tls_fixture(root, options.openssl)
    trust = ssl.PEM_cert_to_DER_cert(cert.read_text(encoding="ascii"))
    with ThreadingHTTPServer(("127.0.0.1", 0), DownloadFixture) as https_server:
        https_server.socket = tls.wrap_socket(https_server.socket, server_side=True)
        https_server.requests = []
        https_server.user_file_types = user_file_types
        http = f"http://127.0.0.1:{http_server.server_port}"
        https = f"https://127.0.0.1:{https_server.server_port}"
        http_server.redirects = {"/upgrade": https + "/text"}
        https_server.redirects = {
            "/redirect": https + "/text",
            "/downgrade": http + "/text",
        }
        http_server.navigation_pages = https_server.navigation_pages = {
            "/navigate": https + "/text",
        }
        thread = threading.Thread(target=https_server.serve_forever, daemon=True)
        thread.start()
        try:
            cases = (
                ("direct-https", https + "/text", https + "/text", None, [], ["/text"]),
                ("redirect-https", https + "/redirect", https + "/text", None,
                 [], ["/redirect", "/text"]),
                ("direct-http", http + "/text", http + "/text", "request", ["/text"], []),
                ("redirect-upgrade", http + "/upgrade", https + "/text",
                 "target", ["/upgrade"], ["/text"]),
                ("redirect-downgrade", https + "/downgrade", http + "/text",
                 "request", ["/text"], ["/downgrade"]),
                ("https-initiator", https + "/navigate", https + "/text", None,
                 [], ["/navigate", "/text"]),
                ("http-initiator", http + "/navigate", https + "/text",
                 "request", ["/navigate"], ["/text"]),
            )
            if options.user_save:
                cases = (
                    ("user-accept", https + "/text", https + "/text", None, [], ["/text"]),
                    ("user-anchor", https + "/user-anchor", https + "/text", None,
                     [], ["/user-anchor", "/text"]),
                    ("user-save-link", https + "/user-anchor", https + "/text", None,
                     [], ["/user-anchor", "/text"]),
                    ("user-anchor-passive", https + "/user-anchor", https + "/text", "request",
                     [], ["/user-anchor"]),
                    ("user-accept", https + "/user-stream", https + "/user-stream", None,
                     [], ["/user-stream"]),
                    ("user-size-limit", https + "/user-stream-oversized", https + "/user-stream-oversized",
                     None, [], ["/user-stream-oversized"]),
                    ("user-outside", https + "/text", https + "/text", None, [], ["/text"]),
                    ("user-delete", https + "/text", https + "/text", None, [], ["/text"]),
                    ("user-delete-changed", https + "/text", https + "/text", None, [], ["/text"]),
                    ("user-cancel", https + "/text", https + "/text", "target", [], ["/text"]),
                    ("user-wrong-type", https + "/text", https + "/text", "target", [], ["/text"]),
                    ("user-blocked", https + "/executable", https + "/executable",
                     "target", [], ["/executable"]),
                    ("user-blocked", https + "/binary", https + "/binary",
                     "target", [], ["/binary"]),
                    ("user-resume", https + "/resume", https + "/resume", None, [], ["/resume"] * 2),
                    ("user-resume", https + "/resume", https + "/resume", "resume", [], ["/resume"] * 2),
                    ("user-blocked", https + "/user-file/mismatched-pdf",
                     https + "/user-file/mismatched-pdf", "target", [], ["/user-file/mismatched-pdf"]),
                    ("user-blocked", https + "/user-file/mismatched-wav",
                     https + "/user-file/mismatched-wav", "target", [], ["/user-file/mismatched-wav"]),
                    *((f"user-{kind}", f"{https}/user-file/{kind}", f"{https}/user-file/{kind}",
                       None, [], [f"/user-file/{kind}"])
                      for kind in user_file_types),
                )
            if options.native_picker:
                endpoint = https + "/text"
                cases = (
                    ("user-native-accept", endpoint, endpoint, None, [], ["/text"]),
                    ("user-native-cancel", endpoint, endpoint, "target", [], ["/text"]),
                )
            for name, url, target, denial, http_paths, https_paths in cases:
                http_server.requests.clear()
                https_server.requests.clear()
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
                    fixture = Path(temp).resolve()
                    leaf = fixture / "download-probe-leaf.der"
                    leaf.write_bytes(trust)
                    downloads = fixture / "downloads"
                    downloads.mkdir(mode=0o700)
                    if name == "user-resume":
                        https_server.resume_case = "replacement" if denial else "partial"
                        https_server.resume_directory = downloads
                        https_server.resume_requests = []
                        https_server.resume_error = None
                    args = [
                        str(executable), "--no-proxy-server",
                        "--zitter-content-download-transport-probe",
                        f"--zitter-content-download-probe={url}",
                        f"--zitter-content-download-probe-target={target}",
                        f"--zitter-content-probe-root={fixture}",
                    ]
                    if denial == "resume":
                        args.append("--zitter-content-expect-download-resume-blocked")
                    elif denial:
                        args.append("--zitter-content-expect-download-blocked")
                    if options.user_save:
                        args.append(f"--zitter-content-download-save-probe={name}")
                    if name == "user-size-limit":
                        https_server.stream_release = threading.Event()
                    try:
                        interactive_save = name == "user-native-accept"
                        if interactive_save:
                            print("Enter native-save.txt in the macOS save panel and choose Save.",
                                  flush=True)
                        result = subprocess.run(args, capture_output=True, text=True,
                                                timeout=120 if interactive_save else 45)
                    except subprocess.TimeoutExpired as error:
                        raise SystemExit(f"{name} timed out: {error.stderr!r}") from error
                    finally:
                        if name == "user-size-limit":
                            https_server.stream_release.set()
                    marker = f"Zitter Content rejected the download {denial}"
                    if (result.returncode or
                            "Zitter Content download probe passed" not in result.stdout or
                            (denial and denial != "resume" and marker not in result.stdout)):
                        raise SystemExit(
                            f"{name} failed (exit {result.returncode}).\n"
                            f"HTTP paths: {http_server.requests!r}\n"
                            f"HTTPS paths: {https_server.requests!r}\n"
                            f"{result.stdout}\n{result.stderr}"
                        )
                    if name.startswith("user-native-") and (
                            "Zitter Content exercised the native download save panel"
                            not in result.stdout):
                        raise SystemExit(f"{name} did not exercise AppKit.\n{result.stderr}")
                    actual = {path.name: path.read_bytes() for path in downloads.iterdir()}
                    saved = "chosen-save.txt" if options.user_save else "download-probe.txt"
                    if name == "user-native-accept":
                        saved = "native-save.txt"
                    expected = {} if denial else {saved: RESUME_PAYLOAD if name == "user-resume" else PAYLOAD}
                    kind = name.removeprefix("user-")
                    if kind in user_file_types:
                        saved = f"chosen-save.{kind}"
                        expected = {saved: user_file_types[kind][1]}
                    if name == "user-resume" and (
                            https_server.resume_error or https_server.resume_requests != [
                                (None, None), (f"bytes={RESUME_OFFSET}-", RESUME_ETAG)]):
                        raise SystemExit(f"Unexpected HTTPS resume requests: {https_server.resume_requests}")
                    if name in ("user-delete", "user-size-limit"):
                        expected = {}
                    elif name == "user-delete-changed":
                        expected = {saved: b"Changed after download\n"}
                    entries = {downloads, leaf}
                    saved_path = downloads / saved
                    if name == "user-outside":
                        saved_path = fixture / "outside.txt"
                        entries.add(saved_path)
                        expected = {}
                        if not saved_path.is_file() or saved_path.read_bytes() != PAYLOAD:
                            raise SystemExit("The picker did not save to the selected alternate directory")
                    if (actual != expected or set(fixture.iterdir()) != entries or
                            http_server.requests != http_paths or
                            https_server.requests != https_paths):
                        raise SystemExit(f"{name} produced unexpected requests or files")
                    if options.require_quarantine and not denial and name not in ("user-delete", "user-size-limit"):
                        require_quarantine(saved_path, windows)
                    print(f"Content download transport passed: {name}")
        finally:
            https_server.shutdown()
            thread.join()


def file_reputation_entry(decision, payload):
    return f"{decision} {sha256(payload).hexdigest()}\n"


def verify_file_reputation(executable, server, options, windows):
    allowed = file_reputation_entry("allow", PAYLOAD)
    threats = file_reputation_entry("block", PAYLOAD)
    unavailable = "unavailable"
    modified = bytes([PAYLOAD[0] ^ 1]) + PAYLOAD[1:]
    cases = (
        ("allowed", "/text", PAYLOAD, allowed, None),
        ("threat", "/text", PAYLOAD, threats, "threat"),
        ("same-url-replacement", "/text", modified, allowed, unavailable),
        ("missing-catalog", "/text", PAYLOAD, None, unavailable),
        ("malformed-catalog", "/text", PAYLOAD, allowed + "invalid\n", unavailable),
        ("ambiguous-catalog", "/text", PAYLOAD, allowed + threats, unavailable),
        ("oversized-catalog", "/text", PAYLOAD, allowed + "\n" * 65536, unavailable),
        ("resume", "/resume", RESUME_PAYLOAD,
         file_reputation_entry("allow", RESUME_PAYLOAD), None),
        ("resume-threat", "/resume", RESUME_PAYLOAD,
         file_reputation_entry("block", RESUME_PAYLOAD), "threat"),
        ("tampered-staging", FILE_REPUTATION_TAMPER_PATH, PUBLISH_RACE_PAYLOAD,
         file_reputation_entry("allow", PUBLISH_RACE_PAYLOAD), unavailable),
    )
    for case, endpoint, payload, catalog_contents, denial in cases:
        if windows and case == "tampered-staging":
            continue
        with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
            root = Path(temp).resolve()
            downloads = root / "downloads"
            downloads.mkdir(mode=0o700)
            catalog = root / "download-file-reputation.txt"
            if catalog_contents is not None:
                catalog.write_bytes(catalog_contents.encode("ascii"))
            server.download_overrides = {endpoint: ("text/plain", payload)}
            server.resume_case = "partial" if endpoint == "/resume" else None
            server.resume_directory = downloads
            server.resume_requests = []
            server.resume_error = None
            server.staged_file_changed = False
            server.requests.clear()
            args = probe_args(executable, server.server_port, root, endpoint)
            args.append("--zitter-content-download-file-reputation-probe")
            if denial:
                args.append(f"--zitter-content-expect-download-file-{denial}")
            try:
                result = subprocess.run(args, capture_output=True, text=True, timeout=45)
            except subprocess.TimeoutExpired as error:
                raise SystemExit(f"{case} timed out: {error.stderr!r}") from error
            denial_marker = "Zitter Content rejected the completed download artifact"
            if (result.returncode or
                    "Zitter Content download probe passed" not in result.stdout or
                    (denial_marker in result.stdout) != bool(denial)):
                raise SystemExit(f"{case} failed.\n{result.stdout}\n{result.stderr}")
            actual = {path.name: path.read_bytes() for path in downloads.iterdir()}
            expected = {} if denial else {"download-probe.txt": payload}
            expected_paths = {downloads, catalog} if catalog_contents is not None else {downloads}
            requests = [endpoint, endpoint] if endpoint == "/resume" else [endpoint]
            if (actual != expected or set(root.iterdir()) != expected_paths or
                    server.requests != requests or server.resume_error or
                    (case == "tampered-staging" and not server.staged_file_changed)):
                raise SystemExit(
                    f"{case} produced unexpected files {list(actual)}, "
                    f"requests {server.requests}, or error {server.resume_error!r}"
                )
            if options.require_quarantine and not denial:
                require_quarantine(downloads / "download-probe.txt", windows)
            print(f"Content download file reputation passed: {case}")


def verify_reputation(executable, server, root, options, windows):
    cert, tls = make_tls_fixture(root, options.openssl)
    trust = ssl.PEM_cert_to_DER_cert(cert.read_text(encoding="ascii"))
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    origin = f"https://127.0.0.1:{server.server_port}"
    cases = {
        "clean": PAYLOAD,
        "source-match": None,
        "target-match": None,
        "prefix-collision": PAYLOAD,
        "unknown-threat": None,
        "unrequested-prefix": None,
        "malformed": None,
        "missing-duration": None,
        "invalid-duration": None,
        "oversized": None,
        "http-error": None,
        "redirect": None,
        "timeout": None,
        "resume": RESUME_PAYLOAD,
        "resume-uncached": RESUME_PAYLOAD,
        "artifact-allowed": PAYLOAD,
        "artifact-blocked": None,
        "artifact-url-blocked": None,
    }
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        scenarios = [(case, payload, False) for case, payload in cases.items()]
        scenarios.extend((case, cases[case], True)
                         for case in ("clean", "source-match", "target-match", "http-error",
                                      "resume-uncached"))
        for case, payload, user_save in scenarios:
            with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
                fixture = Path(temp).resolve()
                leaf = fixture / "download-probe-leaf.der"
                leaf.write_bytes(trust)
                downloads = fixture / "downloads"
                downloads.mkdir(mode=0o700)
                resumable = case.startswith("resume")
                target = "/resume" if resumable else "/text"
                source = target if user_save and resumable else "/redirect"
                server.redirects = {"/redirect": origin + target}
                server.reputation_case = case
                server.reputation_source = source
                server.reputation_target = target
                server.reputation_error = None
                server.reputation_download_cookie = None
                server.resume_case = "partial" if resumable else None
                server.resume_directory = downloads
                server.resume_requests = []
                server.resume_error = None
                server.requests.clear()
                args = [
                    str(executable), "--no-proxy-server",
                    "--zitter-content-download-transport-probe",
                    f"--zitter-content-download-reputation-probe={origin}/reputation",
                    f"--zitter-content-download-probe={origin}{source}",
                    f"--zitter-content-download-probe-target={origin}{target}",
                    f"--zitter-content-probe-root={fixture}",
                ]
                expected_paths = {downloads, leaf}
                expected_denial = "Zitter Content rejected the download target"
                filename = "chosen-save.txt" if user_save else "download-probe.txt"
                if user_save:
                    action = "user-resume" if resumable else (
                        "user-accept" if payload is not None else "user-blocked")
                    args.append(f"--zitter-content-download-save-probe={action}")
                if case.startswith("artifact-"):
                    catalog = fixture / "download-file-reputation.txt"
                    decision = "block" if case == "artifact-blocked" else "allow"
                    catalog.write_bytes(file_reputation_entry(decision, PAYLOAD).encode("ascii"))
                    expected_paths.add(catalog)
                    args.append("--zitter-content-download-file-reputation-probe")
                    if case == "artifact-url-blocked":
                        args.append("--zitter-content-expect-download-blocked")
                    else:
                        expected_denial = "Zitter Content rejected the completed download artifact"
                        if payload is None:
                            args.append("--zitter-content-expect-download-file-threat")
                elif payload is None:
                    args.append("--zitter-content-expect-download-blocked")
                try:
                    result = subprocess.run(args, capture_output=True, text=True, timeout=45)
                except subprocess.TimeoutExpired as error:
                    raise SystemExit(f"{case} timed out: {error.stderr!r}") from error
                if (result.returncode or
                        "Zitter Content download probe passed" not in result.stdout or
                        (expected_denial in result.stdout) != (payload is None)):
                    raise SystemExit(
                        f"{case} failed ({server.reputation_error!r}).\n"
                        f"{result.stdout}\n{result.stderr}"
                    )
                actual = {path.name: path.read_bytes() for path in downloads.iterdir()}
                expected = {} if payload is None else {filename: payload}
                requests = [urlsplit(path).path for path in server.requests]
                expected_requests = ([target] if source == target else [source, target])
                expected_requests.append("/reputation")
                if resumable:
                    expected_requests.append(target)
                if case == "resume-uncached":
                    expected_requests.append("/reputation")
                if (actual != expected or set(fixture.iterdir()) != expected_paths or
                        requests != expected_requests or server.reputation_error or
                        server.resume_error):
                    raise SystemExit(
                        f"{case} produced unexpected requests {requests}, files {list(actual)}, "
                        f"or fixture errors {server.reputation_error!r}, {server.resume_error!r}"
                    )
                if case == "clean" and server.reputation_download_cookie != "reputation_probe=private":
                    raise SystemExit("The fixture did not set a cookie on the download origin")
                if options.require_quarantine and payload is not None:
                    require_quarantine(downloads / filename, windows)
                flow = "user save" if user_save else "fixture target"
                print(f"Content download URL reputation passed: {case} ({flow})")
    finally:
        server.shutdown()
        thread.join()


def verify_save_picker(executable, port, root, options, windows):
    folder = root / "downloads"
    folder.mkdir(mode=0o700)
    original = folder / "download-probe.txt"
    original.write_bytes(b"Existing file\n")
    chosen = folder / "chosen-save.txt"
    for action in ("cancel", "collision", "outside", "wrong-type", "late"):
        run_case(executable, port, root, "/text", True, action)
        if (set(root.iterdir()) != {folder} or set(folder.iterdir()) != {original}
                or original.read_bytes() != b"Existing file\n"):
            raise SystemExit(f"The {action} reply created or changed a file")

    if not windows:
        target = root / "must-not-create.txt"
        chosen.symlink_to(target)
        run_case(executable, port, root, "/text", True, "accept")
        if not chosen.is_symlink() or target.exists():
            raise SystemExit("The save picker followed a dangling link")
        chosen.unlink()

    run_case(executable, port, root, "/text", False, "accept")
    if (set(root.iterdir()) != {folder} or set(folder.iterdir()) != {original, chosen}
            or original.read_bytes() != b"Existing file\n" or chosen.read_bytes() != PAYLOAD):
        raise SystemExit("The selected destination or existing file did not match")
    if options.require_quarantine:
        require_quarantine(chosen, windows)

    if not windows:
        alias = root / "parent-alias"
        alias.symlink_to(root, target_is_directory=True)
        through_alias = folder / "alias-save.txt"
        run_case(executable, port, root, "/text", False, "alias")
        alias.unlink()
        if (set(root.iterdir()) != {folder} or
                set(folder.iterdir()) != {original, chosen, through_alias} or
                through_alias.read_bytes() != PAYLOAD):
            raise SystemExit("The picker did not resolve the selected folder alias")
        if options.require_quarantine:
            require_quarantine(through_alias, windows)
    print("Content save picker selection, cancellation, and path checks passed")


def verify_publication_binding(executable, server, windows):
    changed_payload = bytes([PAYLOAD[0] ^ 1]) + PAYLOAD[1:]
    cases = {"rewrite": "rewrite", "replacement": "replace"}
    if not windows:
        cases["symlink"] = "replace"
    for case, change in cases.items():
        with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
            root = Path(temp).resolve()
            downloads = root / "downloads"
            downloads.mkdir(mode=0o700)
            catalog = root / "download-file-reputation.txt"
            catalog.write_bytes(file_reputation_entry("allow", PAYLOAD).encode("ascii"))
            replacement = root / "download-publication-replacement"
            sentinel = root / "publication-sentinel"
            sentinel.write_bytes(changed_payload)
            if case == "symlink":
                replacement.symlink_to(sentinel)
            else:
                replacement.write_bytes(changed_payload)
            server.requests.clear()
            args = probe_args(executable, server.server_port, root, "/text") + [
                "--zitter-content-download-file-reputation-probe",
                f"--zitter-content-download-publication-change={change}",
            ]
            try:
                result = subprocess.run(args, capture_output=True, text=True, timeout=45)
            except subprocess.TimeoutExpired as error:
                raise SystemExit(f"{case} timed out: {error.stderr!r}") from error
            markers = (
                "Zitter Content changed the staged file after the allow verdict",
                "Zitter Content blocked publication of a changed file",
                "Zitter Content download probe passed",
            )
            if result.returncode or any(marker not in result.stdout for marker in markers):
                raise SystemExit(f"{case} failed.\n{result.stdout}\n{result.stderr}")
            expected_paths = {downloads, catalog, sentinel}
            if case == "rewrite":
                expected_paths.add(replacement)
            if (list(downloads.iterdir()) or set(root.iterdir()) != expected_paths or
                    sentinel.read_bytes() != changed_payload or server.requests != ["/text"]):
                raise SystemExit(f"{case} published a file or changed the unrelated sentinel")
            print(f"Content bound final download publication to the verdict: {case}")


def verify_publish_collision(executable, server, root, symlink):
    folder = root / "downloads"
    folder.mkdir(mode=0o700)
    target = folder / "chosen-save.txt"
    existing = b"Created by another process during the transfer\n"
    sentinel = root / "external-sentinel.txt"
    if symlink:
        sentinel.write_bytes(existing)
    server.publish_resume = threading.Event()
    args = probe_args(executable, server.server_port, root, PUBLISH_RACE_PATH) + [
        "--zitter-content-download-save-probe=accept",
        "--zitter-content-expect-download-publish-collision",
    ]
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True) as process:
        try:
            deadline = time.monotonic() + 15
            # A .crdownload appears after the picker has validated and reserved
            # the chosen destination. The fixture holds the remaining bytes.
            while not list(folder.glob("*.crdownload")):
                if process.poll() is not None:
                    stdout, stderr = process.communicate()
                    raise SystemExit(f"Download exited before the race\n{stdout}\n{stderr}")
                if time.monotonic() >= deadline:
                    raise SystemExit("No intermediate download file appeared")
                time.sleep(0.025)
            if symlink:
                target.symlink_to(sentinel)
            else:
                with target.open("xb") as stream:
                    stream.write(existing)
            server.publish_resume.set()
            stdout, stderr = process.communicate(timeout=30)
            if process.returncode or "Zitter Content download probe passed" not in stdout:
                raise SystemExit(f"Final publication collision failed\n{stdout}\n{stderr}")
        finally:
            server.publish_resume.set()
            if process.poll() is None:
                process.kill()
                process.communicate()

    if set(folder.iterdir()) != {target} or target.read_bytes() != existing:
        raise SystemExit("Final publication changed the target or left a partial file")
    if symlink and (not target.is_symlink() or target.readlink() != sentinel):
        raise SystemExit("Final publication replaced the destination symlink")
    print(f"Content preserved the late {'symlink' if symlink else 'file'} collision")


def prepare_workspace(executable, server, root, options, windows):
    for profile in WORKSPACE_PAYLOADS:
        (root / f"{profile}-downloads").mkdir(mode=0o700)
    if not windows:
        key = root / "probe-cookie-key"
        with os.fdopen(os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as output:
            output.write(os.urandom(32))

    return [
        str(executable),
        "--zitter-content-workspace",
        f"--zitter-content-workspace-root={root}",
        f"--zitter-content-workspace-probe=http://127.0.0.1:{server.server_port}/",
        f"--zitter-content-workspace-theme={options.theme}",
        f"--zitter-rust-raster={options.renderer}",
        "--no-proxy-server",
    ]


def read_workspace_download_history(root, profile):
    database = root / f"workspace-{profile}" / "History"
    connection = sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)
    try:
        return connection.execute(
            "SELECT guid, state, received_bytes FROM downloads"
        ).fetchall()
    finally:
        connection.close()


def verify_workspace_history_clear(run, server, root, snapshot, options, windows):
    work = root / "work-downloads" / "download-probe.txt"
    old = work.with_name("old-download.txt")
    shutil.copy2(work, old)
    old_guid = str(uuid.uuid4())
    with closing(sqlite3.connect(root / "workspace-work" / "History")) as db, db:
        db.row_factory = sqlite3.Row
        row = dict(db.execute("SELECT * FROM downloads").fetchone())
        original_id = row["id"]
        row.update(id=original_id + 1, guid=old_guid, current_path=str(old),
                   target_path=str(old), start_time=row["start_time"] - 7_200_000_000)
        columns = ", ".join('"' + name.replace('"', '""') + '"' for name in row)
        placeholders = ", ".join("?" for _ in row)
        db.execute(f"INSERT INTO downloads ({columns}) VALUES ({placeholders})",
                   tuple(row.values()))
        db.execute("INSERT INTO downloads_url_chains "
                   "SELECT ?, chain_index, url FROM downloads_url_chains WHERE id = ?",
                   (row["id"], original_id))

    marker = "Zitter Content cleared Work download history"
    for phase, expected in (
        ("=clear-recent", [(old_guid, *snapshot["work"][0][1:])]),
        ("=clear-all", []),
    ):
        run(phase, marker)
        if (read_workspace_download_history(root, "work") != expected
                or read_workspace_download_history(root, "personal") != snapshot["personal"]):
            raise SystemExit("Clearing download history changed the wrong profile or period")

    run("=after-removal", "Zitter Content workspace download history passed")
    if server.requests != list(WORKSPACE_CASES):
        raise SystemExit(f"Clearing download history made unexpected requests: {server.requests}")
    for profile, payload in WORKSPACE_PAYLOADS.items():
        folder = root / f"{profile}-downloads"
        path = folder / "download-probe.txt"
        files = {path, old} if profile == "work" else {path}
        if set(folder.iterdir()) != files or any(file.read_bytes() != payload for file in files):
            raise SystemExit("Clearing download history changed a downloaded file")
        if options.require_quarantine:
            require_quarantine(path, windows, private=False)
    print(f"Content cleared download history by period and profile in {options.theme} with {options.renderer}")


def verify_workspace_history_import(args, server, root, snapshot, options, windows):
    source = root / "legacy-snapshot"
    source.mkdir()
    profiles, workspaces = [], []
    for role in WORKSPACE_PAYLOADS:
        directory = f"Zitter{role.title()}"
        profiles.append({"id": role, "name": role.title(), "directory": directory})
        workspaces.append({"id": role, "name": role.title(), "profile_id": role})
        (source / directory).mkdir()
        shutil.copy2(root / f"workspace-{role}" / "History", source / directory / "History")
    (source / "ZitterWorkspaces.json").write_text(
        json.dumps({"profiles": profiles, "workspaces": workspaces}), encoding="utf-8",
    )
    if windows:
        shutil.copy2(root / WINDOWS_LOCAL_STATE, source / "Local State")
    else:
        (source / "Local State").write_text("{}", encoding="utf-8")
    # Use the pinned History schema and real completed records written by the fixture.
    partial = root / "legacy-transfer.crdownload"
    partial.write_bytes(b"legacy partial file")
    expected = {role: list(rows) for role, rows in snapshot.items()}
    with closing(sqlite3.connect(source / "ZitterWork" / "History")) as db, db:
        db.row_factory = sqlite3.Row
        row = dict(db.execute("SELECT * FROM downloads").fetchone())
        original_id = row["id"]
        for offset, state, reason, danger in (
            (1, 4, 22, 0), (2, 2, 40, 0), (3, 0, 0, 0), (4, 4, 7, 3),
        ):
            candidate = dict(
                row, id=original_id + offset, guid=str(uuid.uuid4()), state=state,
                interrupt_reason=reason, danger_type=danger, current_path=str(partial),
                target_path=str(root / f"legacy-terminal-{offset}.txt"),
                received_bytes=3, total_bytes=9, etag='"legacy-etag"', http_method="GET",
            )
            columns = ", ".join('"' + name.replace('"', '""') + '"' for name in candidate)
            db.execute(f"INSERT INTO downloads ({columns}) VALUES "
                       f"({', '.join('?' for _ in candidate)})", tuple(candidate.values()))
            db.execute("INSERT INTO downloads_url_chains "
                       "SELECT ?, chain_index, url FROM downloads_url_chains WHERE id = ?",
                       (candidate["id"], original_id))
            if offset <= 2:
                expected["work"].append((candidate["guid"], state, 3))
    original = inventory(source)
    trial = root / "trial"
    prepare(source, trial)
    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory:
        target = Path(directory).resolve()
        target.rmdir()
        import_workspace_profiles(trial, target, "windows-dpapi" if windows else "workspace")
        if not windows:
            shutil.copy2(root / "probe-cookie-key", target / "probe-cookie-key")
        command = [f"--zitter-content-workspace-root={target}"
                   if arg.startswith("--zitter-content-workspace-root=") else arg for arg in args]
        command.append("--zitter-content-workspace-download-probe=import-history")
        for _ in range(2):
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode or "Zitter Content imported terminal download history" not in result.stdout:
                raise SystemExit(f"Download history import failed.\n{result.stdout}\n{result.stderr}")
            for role, rows in expected.items():
                if sorted(read_workspace_download_history(target, role)) != sorted(rows):
                    raise SystemExit("Imported download history did not preserve the terminal records")
                database = target / f"workspace-{role}" / "History"
                with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
                    metadata = db.execute(
                        "SELECT embedder_download_data, hash, http_method, etag, "
                        "last_modified FROM downloads"
                    ).fetchall()
                    failures = db.execute(
                        "SELECT state, interrupt_reason, current_path FROM downloads "
                        "WHERE state != 1 ORDER BY state"
                    ).fetchall()
                    if (metadata != [(IMPORTED_DOWNLOAD_MARKER, b"", "", "", "")] * len(rows)
                            or failures != ([(2, 40, ""), (4, 22, "")] if role == "work" else [])
                            or db.execute("SELECT * FROM downloads_slices").fetchone() is not None):
                        raise SystemExit("Imported download history retained unsafe transfer metadata")
    if partial.read_bytes() != b"legacy partial file":
        raise SystemExit("Import changed a legacy partial file")
    recovered = root / "rolled-back"
    rollback(trial, recovered)
    if any(inventory(path) != original for path in (source, trial / "candidate", recovered)):
        raise SystemExit("Download history import changed the offline source or rollback")
    if server.requests != list(WORKSPACE_CASES):
        raise SystemExit(f"Import replayed a download request: {server.requests}")
    for role, payload in WORKSPACE_PAYLOADS.items():
        file = root / f"{role}-downloads" / "download-probe.txt"
        if file.read_bytes() != payload:
            raise SystemExit("Import changed a source download")
        if options.require_quarantine:
            require_quarantine(file, windows, private=False)
    print(f"Content imported terminal download history in {options.theme} with {options.renderer}")


def verify_workspace(executable, server, root, options, windows):
    args = prepare_workspace(executable, server, root, options, windows)

    def run(phase, marker, rejected=False):
        try:
            result = subprocess.run(
                [*args, f"--zitter-content-workspace-download-probe{phase}"],
                capture_output=True, text=True, timeout=60,
            )
        except subprocess.TimeoutExpired as error:
            raise SystemExit(f"Workspace downloads timed out. Output: {error.stderr!r}") from error
        if rejected:
            if result.returncode and marker not in result.stdout:
                return
        elif not result.returncode and marker in result.stdout:
            return
        raise SystemExit(
            f"Workspace download test {phase} failed ({result.returncode}).\n"
            f"{result.stdout}\n{result.stderr}"
        )

    run("", "Zitter Content workspace download panel isolation passed")
    snapshot = {
        profile: read_workspace_download_history(root, profile) for profile in WORKSPACE_PAYLOADS
    }
    for profile, payload in WORKSPACE_PAYLOADS.items():
        rows = snapshot[profile]
        if len(rows) != 1 or not rows[0][0] or rows[0][1:] != (1, len(payload)):
            raise SystemExit(f"{profile} did not persist its completed download")
    if snapshot["work"][0][0] == snapshot["personal"][0][0]:
        raise SystemExit("The profiles share a download GUID")
    if options.delete:
        verify_workspace_deletion(run, server, root, snapshot, options, windows)
        return
    if options.clear_history:
        verify_workspace_history_clear(run, server, root, snapshot, options, windows)
        return
    if options.import_history:
        verify_workspace_history_import(args, server, root, snapshot, options, windows)
        return

    work_history = root / "workspace-work" / "History"
    source_copy = root / "source-history"
    shutil.copyfile(work_history, source_copy)
    original = source_copy.read_bytes()
    # Restore the candidate from an independent copy of the closed database.
    work_history.unlink()
    shutil.copyfile(source_copy, work_history)
    action_marker = "Zitter Content workspace download file action passed"
    run("=open", action_marker)
    run("=reveal", action_marker)
    work_folder = root / "work-downloads"
    work_file = work_folder / "download-probe.txt"
    original_file = work_folder / "original.txt"
    work_file.rename(original_file)
    try:
        work_file.mkdir()
        try:
            run("=open-invalid", action_marker)
            run("=reveal-invalid", action_marker)
        finally:
            work_file.rmdir()
        original_bytes = original_file.read_bytes()
        work_file.write_bytes(bytes([original_bytes[0] ^ 1]) + original_bytes[1:])
        try:
            run("=open-invalid", action_marker)
            run("=reveal", action_marker)
        finally:
            work_file.unlink()
        if not windows:
            work_file.symlink_to(original_file)
            try:
                run("=open-invalid", action_marker)
            finally:
                work_file.unlink()
    finally:
        original_file.rename(work_file)
    if not windows:
        moved_folder = work_folder.with_name("moved-work-downloads")
        work_folder.rename(moved_folder)
        work_folder.symlink_to(moved_folder, target_is_directory=True)
        try:
            run("=reveal-invalid", action_marker)
        finally:
            work_folder.unlink()
            moved_folder.rename(work_folder)
    run("=open-removed", action_marker)
    if read_workspace_download_history(root, "work"):
        raise SystemExit("The pending file action prevented history removal")
    shutil.copyfile(source_copy, work_history)
    marker = "Zitter Content workspace download history passed"
    run("=restore", marker)
    run("=after-removal", marker)
    if (read_workspace_download_history(root, "work") or
            read_workspace_download_history(root, "personal") != snapshot["personal"]):
        raise SystemExit("Removing a Work entry changed the wrong download history")
    if source_copy.read_bytes() != original:
        raise SystemExit("Removing a Work entry changed the source database copy")
    if server.requests != list(WORKSPACE_CASES):
        raise SystemExit(f"Restoring downloads made unexpected requests: {server.requests}")
    for profile, payload in WORKSPACE_PAYLOADS.items():
        folder = root / f"{profile}-downloads"
        path = folder / "download-probe.txt"
        if set(folder.iterdir()) != {path} or path.read_bytes() != payload:
            raise SystemExit(f"{profile} did not save exactly its own download")
        if options.require_quarantine:
            require_quarantine(path, windows, private=False)
    if not windows:
        for name in ("History", "History-journal", "Favicons"):
            path = work_history.with_name(name)
            backup = path.with_name(name + ".fixture-backup")
            if path.exists():
                path.rename(backup)
            path.symlink_to(source_copy)
            run("=after-removal", marker, rejected=True)
            if not path.is_symlink() or source_copy.read_bytes() != original:
                raise SystemExit(f"A linked {name} database path was modified")
            path.unlink()
            if backup.exists():
                backup.rename(path)
    print(f"Content download panel and history passed in {options.theme} with {options.renderer}")


def verify_workspace_deletion(run, server, root, snapshot, options, windows):
    marker = "Zitter Content workspace download deletion passed"
    work = root / "work-downloads" / "download-probe.txt"
    personal = root / "personal-downloads" / work.name
    payload = WORKSPACE_PAYLOADS["work"]
    personal_payload = WORKSPACE_PAYLOADS["personal"]

    def require_unchanged(work_bytes):
        if work.read_bytes() != work_bytes or personal.read_bytes() != personal_payload:
            raise SystemExit("The deletion changed the wrong file")
        if any(read_workspace_download_history(root, profile) != rows
               for profile, rows in snapshot.items()):
            raise SystemExit("The deletion changed the wrong history entry")

    run("=delete-cancel", marker)
    require_unchanged(payload)
    replacement = bytes([payload[0] ^ 1]) + payload[1:]
    work.write_bytes(replacement)
    run("=delete-reject", marker)
    require_unchanged(replacement)
    work.write_bytes(payload)

    alias = personal.with_name("work-alias.txt")
    os.link(work, alias)
    try:
        run("=delete-reject", marker)
        require_unchanged(payload)
        if alias.read_bytes() != payload:
            raise SystemExit("The deletion modified a hard-linked file")
    finally:
        alias.unlink()
    if not windows:
        saved = work.with_name("original.txt")
        work.rename(saved)
        alias.write_bytes(payload)
        work.symlink_to(alias)
        try:
            run("=delete-reject", marker)
            require_unchanged(payload)
            if not work.is_symlink() or alias.read_bytes() != payload:
                raise SystemExit("The deletion touched a symlink or its target")
        finally:
            if work.is_symlink():
                work.unlink()
            saved.rename(work)
            alias.unlink()

    if options.require_quarantine:
        require_quarantine(work, windows, private=False)
        require_quarantine(personal, windows, private=False)
    run("=delete-confirm", marker)
    if list(work.parent.iterdir()) or read_workspace_download_history(root, "work"):
        raise SystemExit("Confirmed deletion did not remove Work's file and history entry")
    if (set(personal.parent.iterdir()) != {personal} or personal.read_bytes() != personal_payload
            or read_workspace_download_history(root, "personal") != snapshot["personal"]):
        raise SystemExit("Confirmed deletion affected the other profile")
    if server.requests != list(WORKSPACE_CASES):
        raise SystemExit(f"Deletion issued unexpected network requests: {server.requests}")
    print(f"Content confirmed download deletion passed in {options.theme} with {options.renderer}")


def verify_close(executable, server, root, options, windows, wait):
    args = prepare_workspace(executable, server, root, options, windows)
    phase = "close-wait" if wait else "close-quit"
    args.append(f"--zitter-content-workspace-download-probe={phase}")
    server.requests.clear()
    server.close_resume = threading.Event()
    output = root / "close-probe.stdout"
    errors = root / "close-probe.stderr"
    marker = "Zitter Content waited for an active download"
    with (output.open("w") as stdout, errors.open("w") as stderr,
          subprocess.Popen(args, stdout=stdout, stderr=stderr) as process):
        try:
            if wait:
                deadline = time.monotonic() + 20
                while marker not in output.read_text():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise SystemExit(f"Close prompt did not let the download continue.\n"
                                         f"{output.read_text()}\n{errors.read_text()}")
                    time.sleep(0.05)
                server.close_resume.set()
                marker = "Zitter Content finished the download and closed without prompting"
            else:
                marker = "Zitter Content confirmed close during a download"
            try:
                status = process.wait(timeout=30)
            except subprocess.TimeoutExpired as error:
                raise SystemExit(f"{phase} timed out.\n{errors.read_text()}") from error
            if status or marker not in output.read_text() or (
                    "Zitter Content workspace session probe passed" not in output.read_text()):
                raise SystemExit(f"{phase} failed ({status}).\n"
                                 f"{output.read_text()}\n{errors.read_text()}")
        finally:
            server.close_resume.set()
            if process.poll() is None:
                process.kill()
                process.wait()
            server.close_resume = None

    work = root / "work-downloads"
    expected = {"download-probe.txt": CLOSE_PROBE_PAYLOAD} if wait else {}
    actual = {item.name: item.read_bytes() for item in work.iterdir()}
    if actual != expected or list((root / "personal-downloads").iterdir()):
        raise SystemExit(f"{phase} produced unexpected download files")
    if server.requests != ["/download/work"]:
        raise SystemExit(f"{phase} issued unexpected requests: {server.requests}")
    if wait and options.require_quarantine:
        require_quarantine(work / "download-probe.txt", windows, private=False)
    session = json.loads((root / "ContentWorkspaceSession.json").read_text())
    if session["active_workspace"] != ("work" if wait else "personal"):
        raise SystemExit(f"{phase} did not save the expected workspace")
    rows = [row[1:] for row in read_workspace_download_history(root, "work")]
    if rows != ([(1, len(CLOSE_PROBE_PAYLOAD))] if wait else []):
        raise SystemExit(f"{phase} saved unexpected download history: {rows}")
    print(f"Content download shutdown passed: {phase}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--workspace", action="store_true")
    mode.add_argument("--delete", action="store_true")
    mode.add_argument("--clear-history", action="store_true")
    mode.add_argument("--import-history", action="store_true")
    mode.add_argument("--close-with-downloads", action="store_true")
    mode.add_argument("--save-picker", action="store_true")
    mode.add_argument("--publish-collision", action="store_true")
    mode.add_argument("--publication-binding", action="store_true")
    mode.add_argument("--transport", action="store_true")
    mode.add_argument("--user-save", action="store_true")
    mode.add_argument("--reputation", action="store_true")
    mode.add_argument("--file-reputation", action="store_true")
    mode.add_argument("--resume", action="store_true")
    mode.add_argument("--resume-after-restart", action="store_true")
    mode.add_argument("--workspace-restart", action="store_true",
                      help="Run restart and discard checks through the Workspace entry point")
    mode.add_argument("--resume-after-crash", action="store_true")
    mode.add_argument("--user-resume-after-crash", action="store_true")
    mode.add_argument("--file-types", action="store_true")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--ffmpeg", help="Generate additional audio and video fixtures for --user-save")
    parser.add_argument("--native-picker", action="store_true",
                        help="Interactively accept, then automatically cancel, the macOS save panel")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    parser.add_argument(
        "--require-quarantine",
        action="store_true",
        help="Require platform quarantine without recording the private download origin",
    )
    options = parser.parse_args()
    if options.ffmpeg and not options.user_save:
        parser.error("--ffmpeg requires --user-save")
    windows = os.name == "nt"
    if options.native_picker and (windows or not options.user_save):
        parser.error("--native-picker requires macOS and --user-save")
    if options.native_picker:
        try:
            info = plistlib.loads((options.app / "Contents/Info.plist").read_bytes())
        except (OSError, plistlib.InvalidFileException) as error:
            parser.error(f"Could not read the test bundle identity: {error}")
        if (not isinstance(info, dict) or info.get("CFBundleIdentifier") != CONTENT_BUNDLE_ID
                or info.get("ZitterUsePreparedWorkspace") is True):
            parser.error("--native-picker requires the isolated Content Lab bundle. "
                         "A release bundle could open the account profile when relaunched.")
    if windows and options.renderer == "metal":
        parser.error("Metal is only available on macOS")
    if windows and options.user_resume_after_crash:
        parser.error("The persistent HTTPS download fixture is available on macOS")
    executable = options.app if windows else options.app / "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with (
        ThreadingHTTPServer(("127.0.0.1", 0), DownloadFixture) as server,
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp,
    ):
        root = Path(temp).resolve()
        server.requests = []
        if options.reputation:
            verify_reputation(executable, server, root, options, windows)
            return
        if options.user_resume_after_crash:
            verify_user_resume_after_crash(executable, server, root, options)
            return
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            if options.resume_after_crash:
                verify_resume_after_crash(executable, server, root, options, windows)
                return
            if options.resume_after_restart or options.workspace_restart:
                verify_resume_after_restart(executable, server, root, options, windows)
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as discard:
                    verify_resume_after_restart(executable, server, Path(discard).resolve(),
                                                options, windows, abandon=True)
                if options.resume_after_restart:
                    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as private:
                        verify_private_interruption(executable, server, Path(private).resolve())
                return
            if options.resume:
                verify_resume(executable, server, options, windows)
                return
            if options.close_with_downloads:
                verify_close(executable, server, root, options, windows, wait=True)
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as quit_temp:
                    verify_close(executable, server, Path(quit_temp).resolve(),
                                 options, windows, wait=False)
                return
            if options.file_reputation:
                verify_file_reputation(executable, server, options, windows)
                return
            if options.file_types:
                verify_file_types(executable, server.server_port, root, options, windows)
                return
            if options.transport or options.user_save:
                verify_transport(executable, server, root, options, windows)
                return
            if options.workspace or options.delete or options.clear_history or options.import_history:
                verify_workspace(executable, server, root, options, windows)
                return
            if options.save_picker:
                verify_save_picker(executable, server.server_port, root, options, windows)
                return
            if options.publication_binding:
                verify_publication_binding(executable, server, windows)
                return
            if options.publish_collision:
                verify_publish_collision(executable, server, root, symlink=False)
                if not windows:
                    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as link_temp:
                        verify_publish_collision(
                            executable, server, Path(link_temp).resolve(), symlink=True
                        )
                return
            downloads = root / "downloads"
            downloads.mkdir(mode=0o700)
            expected = {"download-probe.txt": b"Existing file\n"}
            (downloads / "download-probe.txt").write_bytes(expected["download-probe.txt"])
            for endpoint in CASES:
                blocked = endpoint != "/text"
                run_case(executable, server.server_port, root, endpoint, blocked)
                if not blocked:
                    downloaded = "download-probe (1).txt"
                    expected[downloaded] = PAYLOAD
                    if options.require_quarantine:
                        require_quarantine(downloads / downloaded, windows)
                actual = {path.name: path.read_bytes() for path in downloads.iterdir()}
                if actual != expected:
                    raise SystemExit(f"{endpoint} produced unexpected files: {list(actual)}")
                if set(root.iterdir()) != {downloads}:
                    raise SystemExit(f"{endpoint} left a temporary browser profile")
            print("Content download probe passed")
        finally:
            server.shutdown()
            server_thread.join()


if __name__ == "__main__":
    main()
