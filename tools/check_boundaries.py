#!/usr/bin/env python3
"""Fail if product logic imports Chromium, Views, or platform headers."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_STANDARD = {
    "algorithm", "cmath", "cstddef", "functional", "map", "optional", "span",
    "string", "string_view", "unordered_map", "utility", "vector",
}
PRODUCT_DIRECTORIES = ("zitter/core/", "zitter/ports/", "zitter/application/")


def violations(root):
    errors = []
    for directory in PRODUCT_DIRECTORIES:
        for path in sorted((root / directory).rglob("*")):
            if not path.is_file():
                continue
            for number, line in enumerate(path.read_text().splitlines(), 1):
                match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', line)
                if not match:
                    continue
                header = match[1]
                product = header.startswith(PRODUCT_DIRECTORIES) and ".." not in header.split("/")
                if header not in ALLOWED_STANDARD and not product:
                    errors.append(f"{path.relative_to(root)}:{number}: forbidden include {header}")
    return errors


if __name__ == "__main__":
    errors = violations(ROOT)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)
    print("Product dependency boundaries passed")
