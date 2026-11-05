# Product boundary: macOS integration validation

The performance comparison in `docs/product-boundary-performance-2026-11-05.md`
freezes the extraction before the subsequent assistant/ad-blocking main changes.
The local app build separately integrates main at `d51f69d` and includes the
macOS fixes through `510445a`.

The local helper now stages the complete standard `//zitter` overlay rather than
the earlier UI laboratory target. It includes renderer hooks and vendored
ad-blocking dependencies. The build uses the existing arm64 non-component
Release configuration, ThinLTO and Chromium PGO. It packages the concrete binary,
collects its license notices, then packages and signs it again with those notices.

Two macOS integration failures were corrected: Aura-only character constructors
were replaced with portable character events, and assistant JavaScript files
were moved from the bundle's code directory to `Contents/Resources/assistant`.
The native runtime selects this resource location for app bundles and retains
the adjacent runtime for unbundled tests. The isolated macOS launcher passes the
locally discovered Node path explicitly through LaunchServices.

## Automated checks

- 120 Python tests, including a packaging regression that checks resource
  placement before signing and removes the legacy code-directory copy.
- 27 Node assistant tests and both standalone C++ core contracts.
- 24 native browser integration tests after the main merge covering the extraction,
  assistant keyboard handling, permissions, optional panel and all six ad-blocking
  fixtures. After the resource-path fix, the keyboard and Node round-trip tests
  were rebuilt and rerun successfully.
- A native assistant/browser/Node round trip against an embedded local model
  fixture, using Node 24.21.0 at `/usr/local/bin/node`; no external model call.

`gn check out/ZitterOptimized '//zitter:*' --error-limit=200` reports 15 existing
include/dependency errors involving printing, extensions, password import,
authentication, media preferences and the importer test's i18n include. All
reported includes exist on integrated main; this branch's GN changes only add
core sources and the importer's core dependency. The full GN include check is
not green. Evidence is retained locally in
`build/boundary-merged-gn-check-full.log` and
`build/boundary-gn-inherited-evidence.json`.

## Packaged app and local launch

`ZITTER_BUILD_JOBS=10 ZITTER_BUILD_TESTS=1 ./build.sh` completed. The final
`build/ZitterRustMetal.app` passed `codesign --verify --deep --strict` with the
configured Developer ID Application identity. Its packaged smoke test passed
page navigation, new-tab creation, Split View and clean exit using a private
profile (`build/boundary-mac-final-smoke/result.json`).

The license manifest contains 343 components and no unmatched binaries. Metadata
diagnostics remain in the manifest; this is not a complete compliance certificate.
Fresh third-party notices are included in the signed bundle. This is a local
build, with no new tag, GitHub release or Apple notarization submission.

The app was then launched through `tools/run_metal_sidebar.py` with compositor
raster and the owned isolated profile `build/boundary-local-profile-2026-11-05`,
served only on `127.0.0.1:8769`. The browser process and fixture server were
confirmed running. Manual accessibility/screenshot verification was blocked by
the locked macOS session; no visual verification is claimed. The existing user
profiles were not used. Reproduction:

```sh
.chromium/depot_tools/python-bin/python3 tools/run_metal_sidebar.py \
  --app build/ZitterRustMetal.app \
  --profile build/boundary-local-profile-2026-11-05 --port 8769
```

Local evidence includes `build/local-boundary-mac-build.log`,
`build/boundary-merged-native-results.json`,
`build/boundary-mac-final-native-results.json`,
`build/boundary-mac-final-smoke/result.json`,
`build/boundary-mac-codesign-verify.log`,
`build/license-audit/mac-local/manifest.json`, and
`build/boundary-mac-build-receipt.json`.
