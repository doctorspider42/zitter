# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Guard common shell commands that affect Zitter's Chromium checkout."""
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runner import OUTPUT, WINDOWS, checkout, host_helper

HEREDOC = re.compile(r"<<(-)?\s*['\"]?([A-Za-z_]\w*)")
OUT = re.compile(r"(?:^|/)out/([^/\s\"']+)/?$")
COMMAND = re.compile(
    r"(?:^|[;&|(]\s*|\$\(\s*)"
    r"(?:(?:[A-Za-z_]\w*=\S+|exec|time|(?:[^\s;&|()]+/)?(?:env|nice|nohup|"
    r"caffeinate(?:\s+-\w+)*|zsh|bash|sh|cmd(?:\.exe)?\s+/c|"
    r"(?:powershell|pwsh)(?:\.exe)?(?:\s+-\w+)*))\s+)*"
    r'''("[^"\n]*"|'[^'\n]*'|[^\s;&|)"']+)((?:"[^"\n]*"|'[^'\n]*'|[^;&|)\n"'])*)''',
    re.M | re.I,
)


def strip_heredocs(command):
    lines = []
    delimiter = None
    for line in command.splitlines():
        if delimiter:
            if (line.lstrip("\t") if delimiter[1] else line) == delimiter[2]:
                delimiter = None
            continue
        lines.append(line)
        delimiter = HEREDOC.search(line)
    return "\n".join(lines)


def check(command, root):
    normalized = strip_heredocs(command).replace("\\", "/")
    for raw_command, args in COMMAND.findall(normalized):
        executable = raw_command.strip("\"'").lower()
        name = executable.rsplit("/", 1)[-1]
        words = re.findall(r'''"[^"]*"|'[^']*'|\S+''', args)
        words = [word.strip("\"'") for word in words]
        if name == "robocopy" and any(word.lower() == "/mir" for word in words):
            return "Use cmd /c rmdir /s /q for deletion. Robocopy /MIR can follow Chromium junctions."
        if not WINDOWS and executable in ("ninja", "/opt/homebrew/bin/ninja", "/usr/local/bin/ninja"):
            return "Use the Chromium checkout's Ninja or zitter_dev_start. Homebrew Ninja can wipe the build log."
        if name in ("mkdir", "md", "new-item") or (name in ("gn", "gn.exe") and "gen" in words):
            for word in words:
                match = OUT.search(word)
                if match and not (root / ".chromium/src/out" / match[1]).is_dir():
                    return f"Creating a Chromium output takes 10–40 GB and hours. Ask the user first. Use existing {OUTPUT}."
        recursive = (
            name == "rm" and any(re.match(r"-[A-Za-z]*[rR]", word) or word == "--recursive" for word in words)
            or name in ("rd", "rmdir", "del", "erase") and any(word.lower() == "/s" for word in words)
            or name in ("remove-item", "ri", "rm", "del", "erase") and any(word.lower() in ("-r", "-recurse") for word in words)
        )
        if recursive and any(OUT.search(word) or re.search(r"(?:^|/)\.chromium(?:/src)?/?$", word) for word in words):
            return "Deleting Chromium or an output directory requires the user's explicit approval."
        if name in ("build.sh", "build_app.sh", "build_app_win.ps1"):
            if "--help" in words:
                continue
            return "Use zitter_dev_start for a queued build with progress and logs."
        staging = name == "stage_content_embedder.py" or any(
            word.rsplit("/", 1)[-1] == "stage_content_embedder.py" for word in words
        )
        if name in ("ninja", "ninja.exe", "autoninja", "autoninja.bat", "gn", "gn.exe") or staging:
            helper = host_helper(root)
            with helper.file_lock(helper.chromium_lock(root)) as acquired:
                if not acquired:
                    return "A Zitter Dev job holds the Chromium checkout. Wait or queue through zitter_dev_start."
    return None


def main(event):
    tool_input = event.get("tool_input") or {}
    try:
        root = checkout(tool_input.get("workdir") or event.get("cwd"))
    except (OSError, ValueError):
        return
    reason = check(tool_input.get("command", ""), root)
    if reason:
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse",
                          "permissionDecision": "deny", "permissionDecisionReason": reason}}))


if __name__ == "__main__":
    main(json.load(sys.stdin))
