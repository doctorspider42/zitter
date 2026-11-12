#!/usr/bin/env python3
"""Verify Content GitHub live folders with disposable profiles and a loopback endpoint."""

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import secrets
import tempfile
from threading import Thread
from urllib.parse import parse_qs, urlsplit

from verify_content_workspace_session import (
    KEY_FILE,
    SESSION_FILE,
    WorkspaceFixture,
    WorkspaceServer,
    launch,
    seed_session,
)

PULL_URL = "https://github.com/example/project/pull/17"
MARKER = "Zitter Content GitHub folder probe passed"


class GitHubFixture(WorkspaceFixture):
    def do_GET(self):
        url = urlsplit(self.path)
        if url.path != "/search/issues":
            super().do_GET()
            return
        auth = self.headers.get("Authorization")
        request = (self.headers.get("Host"), url.path, parse_qs(url.query),
                   self.headers.get("Cookie"), auth)
        self.server.github_requests.append(request)
        title = self.server.phase if auth == self.server.expected_auth else "stale token"
        body = json.dumps({
            "incomplete_results": False,
            "items": [{"html_url": PULL_URL, "title": title}],
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


@contextmanager
def serve(server):
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        thread.join()


def verify_requests(requests, expected_host, expected_auth, preceding_auth):
    if not requests or requests[-1][-1] != expected_auth:
        raise SystemExit("The expected GitHub authorization was not used")
    expected_query = {
        "sort": ["updated"],
        "per_page": ["100"],
        "q": ["is:open is:pr repo:example/project"],
    }
    saw_current_auth = False
    for host, path, query, cookie, auth in requests:
        if (host != expected_host or path != "/search/issues" or query != expected_query
                or cookie is not None or auth not in (expected_auth, preceding_auth)
                or (saw_current_auth and auth != expected_auth)):
            raise SystemExit("Unexpected request to the GitHub fixture")
        saw_current_auth |= auth == expected_auth


def verify_folders(executable, root, workspace):
    host = f"127.0.0.1:{workspace.server_port}"
    origin = f"http://{host}"
    key = root / KEY_FILE
    key.write_bytes(secrets.token_bytes(32))
    key.chmod(0o600)
    session = seed_session(origin)
    session_file = root / SESSION_FILE
    expected_folder_id = expected_pull_id = previous_ciphertext = previous_auth = None
    for phase, token in (
        ("create", "zitter_fixture_first"),
        ("restart", "zitter_fixture_first"),
        ("replace", "zitter_fixture_second"),
        ("wrong-profile", None),
        ("remove", None),
    ):
        session["active_workspace"] = "personal"
        if phase == "wrong-profile":
            session["personal"]["pins"] = [session["work"]["pins"][0]]
        else:
            session["personal"].pop("pins", None)
        session_file.write_text(json.dumps(session), encoding="utf-8")
        workspace.requests = []
        workspace.reopening = phase != "create"
        workspace.github_requests = []
        workspace.phase = phase
        workspace.expected_auth = f"Bearer {token}" if token else None
        result = launch(executable, root, origin, [
            "--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1",
            f"--zitter-content-github-folder-probe={phase}",
        ])
        if result.returncode or MARKER not in result.stdout.splitlines():
            raise SystemExit(f"GitHub folder {phase} failed.\n{result.stdout}\n{result.stderr}")
        raw_session = session_file.read_text(encoding="utf-8")
        if "zitter_fixture_first" in raw_session or "zitter_fixture_second" in raw_session:
            raise SystemExit("The GitHub token was written in plaintext")
        session = json.loads(raw_session)
        folder, pull = session["work"]["pins"]
        ciphertext = folder["github"]["encrypted_token"]
        if phase == "wrong-profile":
            if workspace.github_requests or len(session["personal"]["pins"]) != 1:
                raise SystemExit("A copied credential triggered a request from Personal")
            continue
        verify_requests(workspace.github_requests, host, workspace.expected_auth, previous_auth)
        if (not folder["folder"] or pull["parent_id"] != folder["id"]
                or pull["url"] != PULL_URL or pull["title"] != phase
                or session["personal"].get("pins")):
            raise SystemExit(f"Unexpected persisted folder state after {phase}")
        if phase == "create":
            expected_folder_id, expected_pull_id = folder["id"], pull["id"]
        elif (folder["id"], pull["id"]) != (expected_folder_id, expected_pull_id):
            raise SystemExit("The folder or pull request changed pin identity")
        if bool(ciphertext) != bool(token):
            raise SystemExit("Unexpected persisted token state")
        if ((phase == "restart" and ciphertext != previous_ciphertext)
                or (phase == "replace" and ciphertext == previous_ciphertext)):
            raise SystemExit("The encrypted token did not survive restart or replacement")
        previous_ciphertext, previous_auth = ciphertext, workspace.expected_auth


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    executable = (args.app / "Contents/MacOS/Zitter Content Lab").resolve(strict=True)
    with (tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory,
          WorkspaceServer(("127.0.0.1", 0), GitHubFixture) as workspace,
          serve(workspace)):
        verify_folders(executable, Path(directory).resolve(), workspace)
    print("Content GitHub folder restart, credential scope, replacement, and removal passed")


if __name__ == "__main__":
    main()
