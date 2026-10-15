# Local verification — 2026-10-15

## Environment and build

- Platform: macOS ARM64, 36 GiB RAM, 12 logical CPUs.
- Chromium revision: `1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`.
- Source: official Chromium GitHub mirror; the primary googlesource endpoint
  stalled during the initial fetch. Dependency revisions came from Chromium DEPS.
- GN: `2596 (0817afa57742)`; Chromium's pinned native ARM64 Clang toolchain.
- Build: debug, component libraries, symbols disabled, local Ninja, no remote build.
- Xcode's missing Metal Toolchain was installed using
  `xcodebuild -downloadComponent MetalToolchain`.
- Blink per-type generators were built with Ninja `-j 1` to avoid nesting many
  CPU-sized Python pools. C++ compilation then used `-j 12`.

The independent core, GN header check, Chromium compilation and final link passed.
The executable is `.chromium/src/out/Zitter/zitter_prototype`.

## Chromium runtime test

Command from the project root:

```sh
python3 tools/run_prototype.py --verify
```

Observed result in `.chromium/isolation-test.log`:

```text
[       OK ] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (14036 ms)
[  PASSED  ] 1 test.
SUCCESS: all tests passed.
```

The process exited with code 0. Assertions verified:

- Two distinct regular Profiles and BrowserContexts.
- Each selected content host belongs to the same top-level native window.
- Different cookies, localStorage, IndexedDB and sessionStorage at the same URL.
- Twenty work/personal switching cycles preserve the independent markers.
- Unknown workspace selection preserves the previous active workspace.
- Destroying and recreating both page hosts retains cookies/localStorage/IndexedDB
  and drops sessionStorage, as expected.

After the interactive launcher changes, the final executable was tested again:
`.chromium/isolation-recheck.log` reports one passing test (14312 ms),
`SUCCESS: all tests passed`, and exit code 0.

This is not a browser-process restart test. It does not verify history recording,
extensions integration, permission dialogs, downloads or DRM.

## Interactive window

```sh
python3 tools/run_prototype.py
```

After the user unlocked macOS, desktop inspection confirmed the native
"Zitter — profile/workspace probe" window, the rendered storage probe page and
the Work/Personal controls. Clicking Personal through accessibility changed the
status to `Workspace: personal | Profile: personal-profile`; clicking Work
changed it back to `Workspace: work | Profile: work-profile` in the same window.

The first interactive attempt did not deliver native shortcuts. The keyboard
integration below fixes their routing; standalone accessibility button switching
was not treated as keyboard verification. The manual test now explicitly uses
Chromium's `ShowAndFocusNativeWindow()` helper to activate the test window.

The macOS manual launcher creates a local `.app` identity in the build output
using a hard link to the executable and links to its resources. An explicit
`--browser-subprocess-path` retains the working standalone test subprocess path.
This is a development wrapper, not a signed/distributable application. Launch
through the Python command above, which supplies the required test arguments.
The manual session has a one-hour run-loop timeout; the command reopens it
without rebuilding if the session has ended.

The manual launcher uses `--single-process-tests`, which keeps the test/browser
in one launcher process. It does **not** use Chromium's `--single-process` mode;
renderer and GPU processes remain separate.

## Keyboard verification — 2026-10-15

Final command: `python3 tools/run_prototype.py --verify`.
The build and GN header dependency check passed. The final run exited with code 0;
`.chromium/keyboard-verified.log` reports:

```text
[1/3] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (16463 ms)
[2/3] ZitterProfilesBrowserTest.KeyboardWorkspaceSwitching (12758 ms)
[3/3] ZitterProfilesBrowserTest.NativeKeyboardWorkspaceSwitching (12715 ms)
SUCCESS: all tests passed.
```

The page-routing test sends keyboard events through the real RenderWidgetHost /
WebContents delegate path, focuses the text input, waits for renderer input ACKs,
types `x` and `y` in separate pages, and switches both ways while the page tries
to cancel Alt chords with preventDefault(). Reserved digit keydowns do not reach
the page. Synthetic Char and KeyUp events target the newly active host, checking
the window's shared shortcut-consumption state.

The macOS test uses Chromium's ui_controls to post native NSEvents to the AppKit
event queue. It first switches from the address field, then types into both page
inputs and verifies Cmd+Alt+1 from Personal to Work and Cmd+Alt+2 from Work to
Personal. Three further round trips verify `document.activeElement.id == 'marker'`
and preserved `x` / `y` values. This is native event-queue integration testing,
not a claim of physical-keyboard testing or Windows/Linux runtime verification.

The fixes are confined to the Zitter overlay: a WebView delegate extension
prehandles reserved workspace commands, and a small Cocoa dispatcher connects
standalone Views Command-key equivalents to the same FocusManager registry.
The native bridge owns that dispatcher. Other AppKit key handling is retained.
No upstream Chromium source change was required for the keyboard fix.

The installer tests (4/4) and product-boundary check also passed. The native Views shell verification below supersedes the earlier diagnostic UI.

## Observed upstream/runtime diagnostics

The successful test log also contains component-build diagnostics about a duplicate
`ANGLESwapCGLLayer` Objective-C class, Metal shader-cache sandbox extensions and an
unimplemented actor tool definition. These did not fail this storage test. The
probe page was visibly rendered; broader GPU behavior was not tested. No
`--no-sandbox` or certificate-bypass flags were added to the launcher.


## Native Views shell — 2026-10-15

The approved window design is now compiled into `zitter_prototype`. It uses a
minimal dark frame, an edge/button-triggered sidebar with pinning, a floating
address/search textfield and independent workspace colors (presets or #RRGGBB).
Workspace/profile labels appear only inside the sidebar. The start page is blank;
the browser controls are native Views rather than page HTML.

Build, GN header checks, the core contract test, four installer tests and the
product-boundary check passed. Final command `python3 tools/run_prototype.py
--verify` exited 0. `.chromium/native-shell-verified.log` reports:

```text
[1/4] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (16551 ms)
[2/4] ZitterProfilesBrowserTest.KeyboardWorkspaceSwitching (12558 ms)
[3/4] ZitterProfilesBrowserTest.NativeKeyboardWorkspaceSwitching (12837 ms)
[4/4] ZitterProfilesBrowserTest.NativeShellAndAppearance (14953 ms)
SUCCESS: all tests passed.
```

The native shell test opens the address palette with Cmd+L, types `x` from an event queued against the previous main window, selects
all with Cmd+A and replaces it with `y`, closes it with Escape, and opens a new
tab through Cmd+T / Enter. HTTP navigation creates a second distinct WebContents
in the same workspace profile and retains the original page. A rejected file URL
leaves the palette open without adding a tab. Native Return in the color field
rejects invalid input and accepts #D49771. Work/personal colors and pinning survive
window recreation after pending writes complete. A resize checks panel anchoring.

The macOS panel integration required an application-local NSEvent monitor to
forward panel key events through NSWindow's standard responder path. It accepts
application events while its own panel is key, retargets keys queued for the
previous main window, preserves Command-equivalent handling,
and is removed with the bridge-owned delegate. The original storage and keyboard
regression tests passed with this routing enabled. IME/alternate layouts,
Windows/Linux runtime behavior and animation polish remain unverified. Appearance
persistence is checked across window recreation in the temporary test harness;
the launcher still does not provide a production user-data installation.

After compacting menu/color-button sizes, `NativeShellAndAppearance` was rerun
and passed (13496 ms, launcher exit 0); see
`.chromium/native-shell-layout-verified.log`.

Live desktop verification after launch confirmed the native sidebar, compact
address palette, text entry and Enter navigation. Work was changed to #D49771
through the color field/save button. Cmd+Alt+2 selected Personal with its separate
#84B6A4 color; Cmd+Alt+1 restored Work and #D49771 in the same visible window.
The final window remains open with the sidebar pinned and the address palette
visible above the local blank start page. The manual harness closes after one
hour; `python3 tools/run_prototype.py` starts another session.

An attempted navigation to https://example.com failed with a DNS error in this
browser-test environment. Local HTTP navigation succeeded. External Internet
browsing is therefore not verified by this run; the final display uses the
embedded test server's blank start page. The development harness must be replaced
or explicitly configured before treating it as an everyday browser.


## Standalone desktop app — 2026-10-15

The separate `//zitter:zitter` executable built successfully on macOS ARM64.
`gn check out/Zitter //zitter:zitter` passed. It uses the normal Chrome bootstrap,
not the browser-test runner, and opens `build/Zitter.app` with stable profile paths.

Native helper packaging was corrected to keep real helper bundles under the
outer app's Frameworks directory, including component dylibs needed by the
sandbox launcher and framework. A two-string, guarded upstream patch gives
OSCrypt a separate Zitter Safe Storage Keychain item. Before this change, cookie
key retrieval blocked inside macOS Security while accessing Chromium's item.
After rebuilding with the independent item, the real app loaded the local HTTP
probe and reported a successful write of `work-20261015` to localStorage, a
persistent cookie and IndexedDB in the Work profile. The rendered diagnostic
page was also verified through native accessibility.

Core CTest, product dependency boundaries and seven Python installer/packager
tests passed. A fresh browser-test binary was being rebuilt when the user asked
to push the current state and stop. That regression run, external HTTPS
navigation and full-process restart checks of both profiles and colors have
not been completed for this standalone version. Earlier harness results above
remain distinct from standalone-app verification.
