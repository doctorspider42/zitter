---
name: zitter-local-release
description: Build, sign, notarize and package Zitter locally in Release mode and publish GitHub release assets. Use for local deploys, Apple notarization, new releases, or adding OS builds to an existing version.
---

# Zitter local release

Find the Zitter repository root and read `docs/local-releases.md` before building
or publishing. It is the shared workflow for Codex and Claude.

Classify compatibility before choosing a version. A change that breaks an
in-place upgrade, including a backend cutover without a verified migration,
requires a new major version even during 0.x development. Follow the
version-selection rules in `docs/local-releases.md` and preserve published tags.

One version has one source tag and one GitHub release. When another operating
system's build uses the same tag, upload its assets to that existing release;
do not create another release or move the tag.

For macOS notarization, follow the Apple notarization section in the shared
workflow. Use `tools/notarize_app.py` and the `zitter-notary` Keychain profile;
test the prepared Hardened Runtime app before submission. The user generates
and enters new Apple credentials privately. Use `tools/package_dmg.py` to wrap
the stapled app in a signed, notarized DMG with an Applications shortcut. Publish
the final DMG after image, mounted-app and quarantined-app checks pass; internal
ZIPs are notarization inputs, not the default user download.
