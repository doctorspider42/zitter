# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Local stdio MCP server. The panel and Codex use the same job tools."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
import runner

UI = "ui://zitter-dev/panel.html"
UI_MIME = "text/html;profile=mcp-app"
SERVER = {"name": "zitter-dev", "title": "Zitter Dev", "version": "0.1.2"}
CAPABILITIES = {"tools": {}, "resources": {}}
ROOT = {"type": "string", "description": "Absolute path to the current Zitter checkout or worktree."}
JOB = {"type": "string", "description": "Job ID returned by zitter_dev_start."}


def tool(name, description, properties, required=(), read_only=False, **extra):
    return {
        "name": name, "description": description,
        "inputSchema": {"type": "object", "properties": properties, "required": list(required),
                        "additionalProperties": False},
        "annotations": {"readOnlyHint": read_only, "openWorldHint": False},
        **extra,
    }


TOOLS = [
    tool("zitter_dev_panel", "Open the live Zitter build panel. Pass the active checkout when known.",
         {"root": ROOT}, read_only=True, title="Zitter",
         _meta={"ui": {"resourceUri": UI},
                "openai/ui": {"entrypoints": [{"type": "thread"}, {"type": "global"}]}}),
    tool("zitter_dev_status", "Read the Zitter checkout, queue, latest job and live Ninja progress.",
         {"root": ROOT}, read_only=True),
    tool("zitter_dev_select_checkout", "Remember the local checkout selected in the Zitter panel.",
         {"root": ROOT}, ("root",), _meta={"ui": {"visibility": ["app"]}}),
    tool("zitter_dev_start", "Queue a detached Zitter Content build, build and run, launch, or Python test job. "
         "Return its ID immediately. Use zitter_dev_status or zitter_dev_log to check it.",
         {"root": ROOT, "kind": {"type": "string", "enum": list(runner.KINDS)}}, ("kind",)),
    tool("zitter_dev_log", "Read a Zitter job's status, errors and recent log lines. Defaults to the latest job.",
         {"root": ROOT, "id": JOB}, read_only=True),
    tool("zitter_dev_stop", "Request cancellation of a queued or running Zitter job.",
         {"root": ROOT, "id": JOB}, ("id",)),
]


def checkout_preference():
    data_dir = os.environ.get("PLUGIN_DATA")
    return Path(data_dir) / "checkout.json" if data_dir else None


def marketplace_checkout():
    """Read the local source recorded by Codex's marketplace add command."""
    codex_home = Path(os.environ.get("CODEX_HOME") or Path.home() / ".codex")
    config = (codex_home / "config.toml").read_text(encoding="utf-8")
    section = re.search(r'^\[marketplaces\.(?:zitter-tools|"zitter-tools")\][ \t]*\n(.*?)(?=^\[|\Z)',
                        config, re.M | re.S)
    if section is None:
        return None
    entries = dict(re.findall(r'^\s*(source_type|source)\s*=\s*("(?:[^"\\]|\\.)*")', section[1], re.M))
    if entries.get("source_type") == '"local"' and "source" in entries:
        return json.loads(entries["source"])
    return None


def default_checkout():
    try:
        return runner.checkout()
    except (OSError, ValueError):
        pass
    preference = checkout_preference()
    if preference:
        try:
            return runner.checkout(json.loads(preference.read_text(encoding="utf-8")))
        except (OSError, ValueError, TypeError):
            pass
    try:
        source = marketplace_checkout()
        return runner.checkout(source) if source else None
    except (OSError, ValueError):
        return None


def git_branch(root):
    result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=root,
                            capture_output=True, text=True, timeout=5)
    return result.stdout.strip() if result.returncode == 0 else ""


def status(root, directory):
    recent = runner.jobs(directory)
    active = [job for job in recent if job["state"] in runner.ACTIVE]
    current = active[0] if active else recent[-1] if recent else None
    chromium = root / ".chromium/src"
    return {
        "root": str(root), "branch": git_branch(root),
        "output": str(chromium / runner.OUTPUT),
        "buildReady": (chromium / runner.OUTPUT / "args.gn").is_file(),
        "launchReady": (root / runner.APP_BINARY).is_file(),
        "diskFree": shutil.disk_usage(chromium if chromium.is_dir() else root).free,
        "current": runner.snapshot(directory, current, include_log=True) if current else None,
        "queue": [runner.snapshot(directory, job) for job in active if job != current],
        "history": [runner.snapshot(directory, job) for job in reversed(recent)
                    if job != current and job not in active][:12],
    }


def call_tool(name, args):
    if name == "zitter_dev_panel":
        path = args.get("root")
        try:
            root = str(runner.checkout(path))
        except (OSError, ValueError):
            if path:
                raise
            root = None
        return {"root": root}
    if name == "zitter_dev_status" and not args.get("root"):
        root = default_checkout()
        if root is None:
            return {"root": None}
    else:
        root = runner.checkout(args.get("root"))
    directory = runner.cache_dir(root)
    if name == "zitter_dev_status":
        return status(root, directory)
    if name == "zitter_dev_select_checkout":
        preference = checkout_preference()
        if preference:
            preference.parent.mkdir(parents=True, exist_ok=True)
            runner.write_json(preference, str(root))
        return {"root": str(root)}
    if name == "zitter_dev_start":
        return runner.enqueue(root, args["kind"])
    if name == "zitter_dev_log":
        return runner.snapshot(directory, runner.get_job(directory, args.get("id")), include_log=True)
    if name == "zitter_dev_stop":
        return runner.stop(directory, args["id"])
    raise ValueError(f"Unknown tool {name}.")


def tool_result(params):
    try:
        data = call_tool(params["name"], params.get("arguments", {}))
    except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError) as error:
        return {"isError": True, "content": [{"type": "text", "text": str(error)}]}
    return {"content": [{"type": "text", "text": json.dumps(data)}], "structuredContent": data}


def dispatch(method, params):
    if method == "initialize":
        requested = params.get("protocolVersion")
        version = requested if requested in ("2025-03-26", "2025-06-18", "2025-11-25") else "2025-11-25"
        return {"protocolVersion": version, "capabilities": CAPABILITIES, "serverInfo": SERVER}
    if method == "server/discover":
        return {"capabilities": CAPABILITIES,
                "_meta": {"io.modelcontextprotocol/serverInfo": SERVER}}
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        return tool_result(params)
    if method == "resources/list":
        return {"resources": [{"uri": UI, "name": "Zitter build panel", "mimeType": UI_MIME}]}
    if method == "resources/templates/list":
        return {"resourceTemplates": []}
    if method == "resources/read" and params.get("uri") == UI:
        html = Path(__file__).with_name("panel.html").read_text(encoding="utf-8")
        return {"contents": [{"uri": UI, "mimeType": UI_MIME, "text": html}]}
    raise LookupError(method)


def main():
    for line in sys.stdin.buffer:
        request = {}
        try:
            request = json.loads(line)
            if "id" not in request:
                continue
            result = dispatch(request["method"], request.get("params", {}))
            reply = {"result": result}
        except LookupError:
            reply = {"error": {"code": -32601, "message": "Method not found"}}
        except (ValueError, TypeError, OSError) as error:
            reply = {"error": {"code": -32602, "message": str(error)}}
        print(json.dumps({"jsonrpc": "2.0", "id": request.get("id"), **reply}), flush=True)


if __name__ == "__main__":
    main()
