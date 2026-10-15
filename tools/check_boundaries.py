#!/usr/bin/env python3
"""Fail if product logic imports Chromium, Views, or platform headers."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_STANDARD = {"functional", "string", "string_view", "vector", "utility"}
errors = []
for directory in ("core", "ports", "application"):
    for path in (ROOT / "zitter" / directory).glob("*.*"):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', line)
            if match and not (match[1] in ALLOWED_STANDARD or
                              match[1].startswith(("zitter/core/", "zitter/ports/")) or
                              match[1] == "zitter/application/workspace_controller.h"):
                errors.append(f"{path.relative_to(ROOT)}:{number}: forbidden include {match[1]}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print("Product dependency boundaries passed")
