# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Keep Chromium's Keychain implementation under a distinct app identity."""


def patch_keychain(text, product="Zitter"):
    for old, new in (("Chromium Safe Storage", f"{product} Safe Storage"),
                     ('"Chromium";', f'"{product}";')):
        if text.count(new) == 1 and old not in text:
            continue
        if text.count(old) != 1 or new in text:
            raise ValueError("Unexpected upstream Keychain identity; review the patch")
        text = text.replace(old, new)
    return text
