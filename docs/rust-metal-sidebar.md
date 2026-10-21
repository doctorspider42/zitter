# Real workspace sidebar: Rust + Metal prototype

The optional `--zitter-sidebar=rust-metal` backend renders the **live tab/pin list** in the normal workspace window. It does not start the synthetic `--zitter-ui-lab` scene. It uses the same `ShellState`, native interaction nodes, controller callbacks, profiles and session persistence as the existing application.

## What moved

- Rust produces the display list for actual tab titles, site monograms, selected/hovered cards, folder rows, section labels, close affordances and both tiles of a Split View pair. It also handles next/previous tab index wrapping for the Metal backend. Row title/detail/close geometry and pointer drag state now come from Rust: the native interaction adapter and the GPU display list share the same row geometry. The platform still measures text and delivers events; Rust distinguishes clicks, latched drags and cancellation on capture loss.
- A visual-only AppKit view presents directly through an asynchronous `CAMetalLayer`. Rounded geometry is drawn by Metal; CoreText shapes/truncates real Unicode text, including system font fallback. Text tiles are cached, bounded to 256 entries / 16 MiB, and uploaded on cache misses. No web page pixels are copied into the sidebar.
- Rust also lays out the vertical columns of the live tab/pin/folder tree, with native adapters supplying preferred sizes and installing the returned rectangles. Hidden children are omitted from spacing; margins and full scroll extent are preserved. Viewport clipping, scroll offsets, indentation wrappers and split tile bounds still come from the native interaction tree. Paint and scroll notifications coalesce updates onto the UI task queue; identical frames skip GPU submission. There is no polling/redraw timer. The shared tab ScrollView now sizes its contents to their full preferred height, fixing overflow scrolling in both backends. In Metal mode, the native fading scrollbar occupies a gutter outside the GPU canvas so it stays visible and draggable. Its list offset stays on the UI thread to keep native hit testing synchronized with the separately presented Metal surface.

This is an incremental renderer prototype, **not a complete removal of Views**. Views still owns outer/container layout, address pill, New Tab action, workspace controls, event delivery, accessibility, context menus, drag-to-split, dialogs, palette editing and IME. Its tab interaction nodes remain in place; the Metal overlay returns `nil` from hit testing, cannot become first responder and adds no accessibility element. The existing paint path also remains available beneath the overlay for fallback and workspace animation snapshots. Consequently, this prototype still pays some Views layout/paint cost; it does not establish the performance of a future entirely Rust-owned sidebar.

Existing browser shortcuts continue to use the normal configurable bindings: Ctrl+Tab / Ctrl+Shift+Tab, Cmd+1…9, Cmd+T/W/L/K, workspace switching and split commands. Folder and tab menus, folder collapse/rename/delete, tab close, profile isolation, downloads, preferences, Peek and session restore retain their current controllers. A single pinned site still uses the existing monogram representation; this change does not add network favicon fetching. Folder rows use a system folder glyph in the Metal list.

## Build and run

From the repository root, using the existing pinned, configured Release checkout:

```sh
python3 tools/stage_performance_candidate.py --variant zitter_ui_lab
(cd .chromium/src && ../depot_tools/autoninja -C out/ZitterOptimized -j 6 zitter_ui_lab zitter_prototype_ui_lab sidebar_scene_unittests_exe)
python3 tools/package_app.py --out .chromium/src/out/ZitterOptimized --binary zitter_ui_lab --destination build/ZitterRustMetal.app --no-register --third-party-licenses build/license-audit/open-source/THIRD_PARTY_LICENSES.txt
python3 tools/run_metal_sidebar.py
```

The launcher seeds `build/rust-metal-profile` once with 24 real tabs, two profiles, a folder with a pinned page and a split pair. A loopback HTTP server serves the demo page; its button updates the actual document/tab title. Subsequent runs preserve this prototype session. Unmarked existing profile directories are rejected. The normal application profile is not used. Close the prototype before starting another launcher for the same profile/port. Keep the selected `--port` unchanged when reusing a demo profile.

For a functional comparison, close the app and run the launcher with `--backend views`. The ordinary application continues to default to Views. No synthetic lab flags are necessary.

## Validation

```sh
.chromium/src/out/ZitterOptimized/sidebar_scene_unittests
.chromium/src/out/ZitterOptimized/zitter_prototype_ui_lab '--gtest_filter=ZitterMetalSidebarBrowserTest.*' --test-launcher-jobs=1 --test-launcher-retry-limit=0
python3 -m unittest discover -s tests -p 'test_*.py'
```

The dedicated browser tests create real WebContents, change a Unicode document title, wait for the Metal overlay, send native tab/address shortcuts and a native mouse click through the overlay, exercise folder collapse/expand, Split View, sidebar resize, workspace switching and closing a split half. Separate cases verify an overflowing real-tab viewport and return to Metal after animated workspace switches. Existing sidebar, keyboard, folder and split regression tests can also run with `--zitter-sidebar=rust-metal`.

This prototype introduces no downloaded UI/font framework: the Rust bridge uses existing CXX infrastructure; Metal, CoreText and AppKit come from macOS. New original source is Apache-2.0. Existing Chromium and third-party notices continue to apply.

No new performance improvement is claimed by this change. The previous rectangle benchmark is not a measurement of this text-bearing workspace UI; a later comparison must measure this actual workload and presentation path.

Validated on 2026-10-21: **16/16 real-browser tests with Rust/Metal and Metal API Validation, 2/2 default Views checks, 4/4 Rust tests and 59/59 Python tests passed**. The overflowing-list test also verifies native hit testing and a native mouse click after scrolling. GN dependency checks, product boundaries and bundle signature verification passed; the packaged executable UUID matches the current build. Filtered test results and source hashes are recorded in [the functional validation artifact](benchmarks/metal-sidebar-functional-2026-10-21.json).

The [functional parity checklist](rust-metal-parity.md) tracks the rest of the
migration. Rust row geometry/gesture state also applies to the existing
`RowButton` controls in palettes when this backend is selected; the palettes
still use Views for rendering, editing and focus. The default Views backend
keeps its C++ layout and gesture path. Both backends keep the row hovered when
the pointer enters its close child, so a split-tile close button remains
clickable; selection changes also retain its visibility while hovered.

Row control migration validated on 2026-10-21: **24/24 Rust/Metal browser tests,
3/3 Views controls, 7/7 Rust tests and 59/59 Python tests passed**. The native
pointer regression drags a real tab into Split View and clicks close buttons
at both sidebar width limits (200 and 480 logical pixels). Palette, history,
settings and folder regressions also passed with the Rust row adapter enabled.
See the [row control validation artifact](benchmarks/rust-row-controls-functional-2026-10-21.json).

The subsequent vertical-column migration passed **25/25 Rust/Metal browser
regressions, 4/4 Views controls, 9/9 Rust tests and 59/59 Python tests**. This
includes the native pointer/keyboard matrix, animated workspace transitions and
the new real-tab/folder geometry case. The refreshed signed app was inspected
with Unicode text, Split View, folder collapse/expand and scrolling to tab 24;
its executable UUID matches the build. See the
[column validation artifact](benchmarks/rust-column-layout-functional-2026-10-21.json).
