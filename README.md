# Zitter

C++ + Chromium fork + Views. This repository contains a native browser development app and a separate
workspace/profile test harness. The current macOS bundle is a local component
build, not a signed release installer.

## Run the desktop app (macOS)

After preparing the pinned Chromium checkout and installing the overlay below:

```sh
cd .chromium/src
gn check out/Zitter //zitter:zitter
autoninja -C out/Zitter -j 12 zitter
cd ../..
python3 tools/package_app.py
open build/Zitter.app
```

`build/Zitter.app` is the normal app. It uses Chromium's regular bootstrap,
network service, sandboxed helpers and profile services, with Zitter's Views
window. It has no browser-test runner or one-hour test timeout. Cmd+L opens the
address/search palette, Cmd+T adds a tab, and Cmd+Alt+1/2 switches workspaces.
The sidebar holds workspace names, tabs, pinning and editable accent colors.

The default data directory is `~/Library/Application Support/Zitter`:

- `ZitterWork/` and `ZitterPersonal/`: separate persistent Chromium profiles;
- `ZitterAppearance.json`: workspace colors and sidebar pinning;
- cache: `~/Library/Caches/Zitter`.

macOS protects the cookie encryption key in the separate **Zitter Safe Storage**
Keychain item. The installer guards a two-string upstream patch for this identity;
it retains Chromium's native random-key generation and encryption. The other
upstream patch is the optional browser-test build dependency. Product code is
installed under `src/zitter/`; the production target needs no test-runner hook.

The packager uses hard links for component dylibs and places real helper bundles
inside the outer app, preserving Chromium's framework lookup and sandbox startup.
Repackage after rebuilding. This development app depends on the local build and
has no signing/notarization or standalone redistribution workflow yet. The native
bootstrap also has a Linux target, but only macOS ARM64 has been built and run;
a Windows production entry point remains to be implemented.

## Current verification status

- Standalone C++ product model and activation contracts: built and tested.
- Chromium integration: **built and linked on macOS ARM64** against
  `chromium/revision.json`.
- `StorageIsolationAndSwitching`: **passed** on 2026-10-15 (real Chromium profiles,
  same-origin storage, workspace switching and page-host recreation).
- Local checkout, tools and executable are in the ignored `.chromium/` directory;
  these large build artifacts are not part of the source overlay.
- The native window and Work/Personal button switching were visually verified.
- Workspace shortcuts: **passed** on macOS ARM64, including native AppKit events
  with focus in each page's text input, repeated switching and preserved text.
  `python3 tools/run_prototype.py --verify` runs the automated storage, keyboard and native-shell tests.
- Native Views shell: **built and tested** with an auto-hiding/pinnable sidebar,
  Cmd+L/Cmd+T floating address/search bar and saved workspace accent colors.
- No claim of full Chrome extensions, history recording, permissions, downloads,
  popups, fullscreen, or DRM support is made by this probe.

The experiment uses **two regular Chromium Profile objects and independently
owned page hosts in one visible Views window**. Each workspace starts with one
page; Cmd+T can add pages bound to that workspace's profile. Switching only changes visibility; it
never changes a live page's BrowserContext. A profile keep-alive outlives its pages.
In the separate browser-test harness, its normal Chrome window is hidden.
The desktop target starts only the Zitter window.

This directly exercises native content hosting and profile storage separation.
It deliberately does not pretend that standalone WebContents implement Chrome's
tab/window service integration. That is the next experiment described in
[docs/prototype.md](docs/prototype.md).
See [docs/verification.md](docs/verification.md) for the observed test result and
the exact scope of the verification.

## Test the independent core

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python3 tools/check_boundaries.py
python3 -m unittest discover -s tests -p 'test_*.py'
```

## Prepare Chromium

Use a dedicated Chromium checkout. Follow the official prerequisites for your OS:

- [macOS](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md)
- [Windows](https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md)
- [Linux](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md)

Chromium requires a large checkout and build directory. Check disk requirements
before fetching; these commands do not use installed Google Chrome or its profile.
With `depot_tools` on PATH, create the checkout outside this repository (or in the
ignored `.chromium` directory):

```sh
mkdir -p .chromium
cd .chromium
fetch --nohooks chromium
cd src
git checkout --detach 1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35
gclient sync -D
```

`gclient sync -D` is intended for this dedicated checkout; it can remove obsolete
dependency directories. Windows commands require adapting directory creation to
your shell. Use Chromium's own fetched compiler, GN, and dependency revisions.

Recent Xcode versions ship the Metal shader compiler as an optional component.
If `xcrun metal --version` reports a missing toolchain, install it before building:

```sh
xcodebuild -downloadComponent MetalToolchain
```

From the Zitter repository, install the overlay. The installer checks the exact
revision, refuses to replace unowned or locally modified overlay files, copies
our sources into `src/zitter`, adds **one dependency** to desktop browser_tests, and gives macOS OSCrypt
its own Zitter Keychain service/account names.
It does not stage or commit changes.

```sh
python3 tools/install_chromium_overlay.py .chromium/src --dry-run
python3 tools/install_chromium_overlay.py .chromium/src
```

From Chromium's `src` directory:

```sh
gn gen out/Zitter --args='is_debug=true is_component_build=true symbol_level=0 blink_symbol_level=0 v8_symbol_level=0 use_siso=false use_remoteexec=false'
gn check out/Zitter //zitter:profile_workspace_browser_tests
autoninja -C out/Zitter -j 1 generate_bindings_all
autoninja -C out/Zitter -j 8 zitter_prototype
out/Zitter/zitter_prototype --gtest_filter=ZitterProfilesBrowserTest.StorageIsolationAndSwitching --test-launcher-jobs=1
```

Windows uses `out\Zitter\zitter_prototype.exe`; Linux requires a graphical session
or the normal Chromium test Xvfb setup. macOS requires the SDK/Xcode specified by
the pinned Chromium revision. The dedicated `zitter_prototype` executable uses Chromium's browser-test runner
without building the entire upstream test collection. This is a desktop target,
not Android or ChromeOS.

Generate Blink bindings separately on the first local build: each per-type
generator creates its own CPU-sized Python worker pool. Running many of these
actions concurrently can oversubscribe the machine even with a modest Ninja
`-j` setting. The `-j 1` step above still uses the generator's internal worker pool.

## Open the manual prototype

From this repository, after building the local checkout:

```sh
python3 tools/run_prototype.py --verify
python3 tools/run_prototype.py
```

Or invoke the executable from Chromium's `src` directory:

```sh
out/Zitter/zitter_prototype --gtest_filter=ZitterProfilesBrowserTest.ManualWindow --zitter-interactive --single-process-tests --enable-pixel-output-in-tests --test-launcher-timeout=3600000
```

Close the Zitter window to finish the test. Test profiles are temporary and
managed by the harness; the manual run is not a persistent daily browser.

- Click Work / Personal or use Cmd+Alt+1 / Cmd+Alt+2 on macOS,
  Ctrl+Alt+1 / Ctrl+Alt+2 on Windows/Linux.
- Both workspaces open the same test-server URL. Save `work` in one and `personal`
  in the other. Read values after switching repeatedly.
- The output order is `cookie|localStorage|IndexedDB|sessionStorage`.
- The address field accepts HTTP(S) URLs. It is a probe control, not a complete
  omnibox or trustworthy security-origin display.

The automated test also destroys and recreates both page hosts: cookie,
localStorage, and IndexedDB values should remain; sessionStorage should disappear.
This is **not** a test of persistence across browser-process restarts.

## Layout

```text
zitter/core/          product model, no Chromium imports
zitter/ports/         runtime contract
zitter/application/   selection commands and failure handling
chromium/prototype/   Chromium profiles and Views window
chromium/BUILD.gn     GN targets installed as src/zitter/BUILD.gn
chromium/revision.json exact upstream revision
testdata/            same-origin storage diagnostic page
tools/               boundary check and guarded overlay installer
tests/               independent contracts and installer checks
docs/prototype.md    ownership, experiment scope and acceptance criteria
```
