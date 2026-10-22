# Real workspace sidebar: Rust + Metal prototype

The optional `--zitter-sidebar=rust-metal` backend renders the **live sidebar, navigation strip and split pane controls** in the normal workspace window. It does not start the synthetic `--zitter-ui-lab` scene. It uses the same `ShellState`, native interaction nodes, controller callbacks, profiles and session persistence as the existing application.

## What moved

- Rust produces the display list for actual tab titles, site monograms, selected/hovered cards, folder rows, section labels, close affordances and both tiles of a Split View pair. It also handles next/previous tab index wrapping for the Metal backend. Row title/detail/close geometry and pointer drag state now come from Rust: the native interaction adapter and the GPU display list share the same row geometry. The platform still measures text and delivers events; a Rust input reducer owns row hover/pressed states, click eligibility, latched drags, cancellation and platform key activation. On macOS Space activates on key-down, while Return remains available for the native default action. Disabled rows cannot activate; capture loss and focus loss cancel pending gestures.
- A visual-only AppKit view presents directly through an asynchronous `CAMetalLayer`. Rounded geometry is drawn by Metal; CoreText shapes/truncates real Unicode text, including system font fallback. Text tiles are cached, bounded to 256 entries / 16 MiB, and uploaded on cache misses. Live tab/pin/folder rows no longer create hidden Views title Labels; their titles come directly from the row model and the Rust display list. No web page pixels are copied into the sidebar.
- Rust also lays out the vertical columns of the live tab/pin/folder tree, with native adapters supplying preferred sizes and installing the returned rectangles. Hidden children are omitted from spacing; margins and full scroll extent are preserved. Rust also sets folder indentation (12 logical pixels per level, capped at eight) and lays out indented pin wrappers and split tiles. Split tiles share a four-pixel frame/gap, including odd-width remainder distribution; compact preferred size is 200 × 46 logical pixels. Rust projects and clips the viewport from native local bounds and floating-point scroll offsets, normalizing offsets back into the physical input adapter. The native tree retains node identity and receives the Rust layout rectangles. Paint invalidation and scroll notifications coalesce updates onto the UI task queue, independently of whether the native compositor rasterizes a frame. Pending layouts are applied before collecting scene geometry; identical frames skip GPU submission. There is no polling/redraw timer. The shared tab ScrollView now sizes its contents to their full preferred height, fixing overflow scrolling in both backends. In Metal mode, Rust now sizes and drags an always-visible overflow thumb on the shared GPU canvas. Native ScrollView/ScrollBar services retain wheel, key/menu and AX delivery adapters. Its list offset stays on the UI thread to keep native hit testing synchronized with the separately presented Metal surface.

This is an incremental renderer prototype, **not a complete removal of Views**. Views still supplies outer/container layout, event delivery, focus, accessibility/default action, context menus, drag-to-split target policy, dialogs, palette editing and IME. Rust owns the migrated address/workspace/navigation control geometry, scene drawing and input decisions through those adapters. RowButton mouse/key decisions use Rust, including palette and New Tab rows, while application callbacks still own browser commands. Its tab interaction nodes remain in place; the Metal overlay returns `nil` from hit testing, cannot become first responder and adds no accessibility element. The fallback painter remains available for startup, missing drawables and workspace animation snapshots. While Metal is visible, the list paint root skips native child traversal instead of painting those rows underneath it. Hiding Metal for an animation explicitly invalidates native contents; losing a previously visible drawable also requests one fresh fallback paint. Views still supplies hierarchy, interaction nodes and some layout, so this does not establish the performance of a future entirely Rust-owned sidebar.

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

The native paint-pass counters verify which path runs; the Metal frame counter counts submitted command buffers, not presentation timestamps. Removing hidden Labels and native child painting removes that work from the steady Metal path. CPU, GPU and end-to-end presentation gains still require measurement of this actual workload; the previous rectangle benchmark is not a measurement of this text-bearing workspace UI.

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

The row-text ownership milestone removes a native title Label from each live
row while preserving its explicit accessibility name, tooltip and close child.
Its regression checks new rows without a resize, a live Unicode title change
with a new GPU submission and no native subtree paint, fallback painting during
workspace animation, resumption of paint suppression, and closing a tab without
leaving zero-size replacement rows. Dynamic child add/remove now explicitly
invalidates the Rust column adapter. This replaces the incidental invalidation
formerly supplied by native Labels. GPU submissions and native/skipped subtree
paint passes are separate diagnostics, not interchangeable frame timings.

Validated for row text ownership on 2026-10-21: **26/26 Metal browser regressions,
4/4 Views controls, 9/9 Rust tests and 59/59 Python tests passed**. The signed app
was inspected with live title changes and a new tab added through Cmd+T/address
palette, scrolled into view, then closed without resizing. See the
[row text ownership validation](benchmarks/rust-owned-row-text-functional-2026-10-21.json).

The row input migration replaces RowButton's Views mouse/key dispatch in the
opt-in backend with a Rust reducer. The native adapter delivers events, installs
the returned visual state and invokes the existing application callbacks. It
retains focus, accessibility/default-action, context-menu and gesture adapters;
Close glyph children now use the same Rust input reducer; drop target policy and native containers still need migration. Space/Return
policy is explicit in Rust, and tests distinguish disabled rows, lost capture,
release outside the row, stray release and key-down versus key-up activation.
Browser callbacks may delete a row or palette; the adapter does not read row
members after dispatching an action. No dependency or binary framework was added.

Validated for row input on 2026-10-21: **27/27 Metal browser regressions,
5/5 Views controls, 13/13 Rust tests and 59/59 Python tests passed**. A pre-existing
split geometry test now applies pending sidebar layout explicitly rather than
assuming RunUntilIdle delivered a compositor layout; the unchanged assertions
also passed five repeated Metal runs. The signed isolated app passed mouse
selection, Ctrl+Tab, folder collapse/expand and Command Center text/Enter checks.
See [row input validation](benchmarks/rust-row-input-functional-2026-10-21.json).

The split/folder geometry migration removes BoxLayout tile placement and
FillLayout pin wrappers from the Metal backend. A Rust split layout distributes
odd-width remainders and keeps tiles inside small containers; native adapters
apply the rectangles. The same Rust indentation policy is used by live pin rows
and the native swipe preview. Tree construction, collapse state and pin/pair
placement in the hierarchy remain with the application controller. Default Views
keeps its BoxLayout/FillLayout path and the same compact pair size.

A manual startup check also found stale split titles until another list action.
Model updates now explicitly schedule a coalesced scene refresh instead of
relying solely on native paint notifications. A regression changes a real split
page title while the entire native sidebar is paint-locked and checks the text
in the last submitted display-list signature. This reuses the existing signature
without adding per-frame diagnostic text copies. It verifies submission content,
not physical display presentation.

Validated for split/folder geometry and model-driven scene updates on 2026-10-21:
**29/29 Metal browser regressions, 6/6 Views controls, 16/16 Rust tests and 59/59
Python tests passed**. The signed app was relaunched to confirm correct initial
split titles without an unrelated list action. Exact width/indent/hit-target
checks are in the native browser regression; a CUA drag did not establish a
manual resize and is not counted as one. See
[split/folder validation](benchmarks/rust-split-folder-layout-functional-2026-10-21.json).

The tree migration moves pre-order traversal, parent/depth resolution, inherited
visibility and collapse state to Rust for the Metal backend. The ABI carries
workspace/item IDs and folder flags; URLs, titles, profiles and commands remain
in the application. State uses separate workspace and item keys, survives row
reconstruction/rename and is cleared when a workspace is removed. Empty or
duplicate IDs, orphans and unreachable cycles do not create interaction nodes;
the existing depth limit remains 64. Hidden descendants retain their native
nodes, preserving nested collapse state and accessibility identity during a
simple expand/collapse.

The native adapter builds its groups from the Rust plan and forwards folder
activation to Rust. It no longer reads native group visibility to decide the
next state. The swipe preview consumes the same plan, with native painting and
spacing retained. Adding a child expands its parent before reconstruction; an
immediately rejected command restores the prior UI state. Default Views uses a
C++ implementation of the plan/state contract for baseline comparison. Native
node containers, pin/split replacement policy, menus, editing and persistence
still remain; this is not full Views removal.

Validated for Rust tree/state ownership on 2026-10-21: **32/32 Metal browser
regressions, 9/9 Views controls, 19/19 Rust tests and 59/59 Python tests passed**.
The signed isolated app retained folder collapse across a profile/workspace
round trip, restored its pin on expansion and preserved the Unicode split pair.
See [tree validation](benchmarks/rust-sidebar-tree-functional-2026-10-21.json).

The tab-plan migration moves ordinary-tab order, split membership, pane order,
pinned-pair anchor selection and original-pin visibility into Rust. The plan
contains indices and UI pin IDs only. A valid pair appears once at the first
member's tab-list position; its tiles follow pane order. If either member has a
reachable pin, the first pinned pane in pane order anchors the pair in that
pin's folder, including when the folder is collapsed. Both original pin rows
stay hidden until the pair is dissolved. Invalid/missing pair members fall back
to ordinary unique rows rather than creating duplicate or incomplete controls.

Native adapters instantiate rows and pair groups from the returned slots and
apply the returned pin visibility. The cached plan is reused on metadata-only
updates and consumed by swipe previews, removing their independent C++ pair
selection. Swapping panes can move a two-pin pair between folder positions;
application WebContents/session commands remain unchanged. Default Views uses a
C++ implementation of the same plan contract. Native containers, clipping,
menu/input/focus/AX adapters and animation painting remain.

Validated for tab/pin grouping on 2026-10-21: **33/33 Metal browser regressions,
10/10 Views controls, 22/22 Rust tests and 59/59 Python tests passed**. The signed
app's native menu swapped the real pages and GPU tiles in the same order, then
restored the demo. See [tab-plan validation](benchmarks/rust-tab-plan-functional-2026-10-21.json).

The viewport migration gives Rust the logical viewport state, offset
normalization, parent-local projection and inherited clipping. Native adapters
supply mirrored local bounds, visibility and floating-point scroll offsets;
they no longer convert each row to screen coordinates or decide offscreen
rejection. Invisible native subtrees remain shallow placeholders so inactive
workspace caches are not traversed.
Rust rejects offscreen/hidden/invalid geometry before title/color preparation.
Its ancestry scratch buffer is reused across updates. Out-of-range native
programmatic offsets are normalized back into the native input adapter as well.

Projected card bounds remain untrimmed, preserving text layout and rounded
geometry at viewport edges. Each display primitive carries its inherited clip;
Metal applies a device-pixel scissor, rounded out and clamped to drawable bounds.
Identical consecutive clips reuse scissor state, and frame signatures include
clips so clipping-only changes submit a new frame. Native wheel/trackpad delivery,
scrollbar behavior, focus/AX hit testing and main-thread scroll synchronization
remain adapters. Native animations still use their existing fallback path.

Validated for viewport projection/clipping on 2026-10-21: **34/34 Metal browser
regressions, 10/10 Views controls, 26/26 Rust tests and 59/59 Python tests passed**.
Fractional native geometry and the submitted GPU bounds/clip agree, including
partial-row hit testing and normalized programmatic overscroll. The signed app
was scrolled to tab 24, selected and restored to its original split pair. See
[viewport validation](benchmarks/rust-viewport-functional-2026-10-21.json).

The New Tab migration extends the same opaque Metal canvas upward to include
the New Tab action. It adds no second CAMetalLayer or renderer. Rust aligns the
action width with the list viewport, leaving the native scrollbar gutter outside
the canvas, and emits its plus icon, title and configurable shortcut hint.
The action no longer creates native title/detail Labels in the Metal backend.
The platform supplies title font metrics; Rust lays out both strings. Shortcut
hints use middle truncation in CoreText, included in the texture cache key.

Native RowButton input, focus, AX and the existing OpenAddress(new_tab=true)
callback remain adapters. SetDetail now updates the row model as well as native
Labels, so changed key bindings reach the GPU scene. Native painting is gated
while Metal is visible; the initial fallback record is invalidated when GPU
ownership begins, and hide/animation/drawable failure requests a fresh native
fallback with title and shortcut. Outer paint invalidations coalesce scene
updates without depending on native rasterization. At that milestone the address
pill, workspace heading and footer still used native rendering.

Validated for New Tab on 2026-10-22: **35/35 Metal browser regressions, 10/10 Views
controls, 29/29 Rust tests and 59/59 Python tests passed**. The local package was
verified and exercised with create/close/scroll. It used the configured permanent
certificate without a trusted timestamp after the Apple timestamp service failed;
default packaging remains unchanged. This is a development artifact. See
[New Tab validation](benchmarks/rust-new-tab-functional-2026-10-22.json).

The address/heading migration extends the same canvas to the address pill. Rust
aligns fixed control widths with the list viewport, keeping the scrollbar gutter
outside the opaque layer. The address button now inherits the shared Rust input
reducer. It creates no host/path Labels in Metal mode: native font metrics feed
Rust adjacent host/path layout, CoreText shapes the bold host and tail-elided
path, and analytic GPU strokes draw the pill outline and search/lock icon. Stroke
width uses the existing 32-byte quad ABI and participates in frame fingerprints.

The heading stores text directly and exposes one native static-text accessibility
node; its inset bold Unicode/emoji text belongs to the scene. The address button
retains its accessible name/action and explicitly describes the displayed host
and path. The Views backend retains native Labels. Fixed fallback records are
refreshed when Metal yields to animations, swipe previews or drawable failure.
Native outer placement, Button/View containers, panel focus policy and address
editing/search/IME remain adapters, with no new third-party dependency.

Validated on 2026-10-22: **36/36 Metal browser regressions, 14/14 Views controls,
32/32 Rust tests and 59/59 Python tests passed**. Tests cover actual pointer
activation, native main-window Cmd+L, HTTPS content, accessibility description,
width limits, dark/light scene updates and workspace/emoji round trips. Manual
inspection used the matching signed, timestamped prototype; normal packaging
succeeded after Apple's timestamp service recovered. These are functional
results, not a CPU/GPU/presentation performance comparison. See
[address/heading validation](benchmarks/rust-address-heading-functional-2026-10-22.json).

The vertical scrollbar migration supersedes the earlier outside-canvas gutter:
the same opaque Metal surface now includes the scrollbar's area. Rust owns range
normalization, proportional/minimum thumb geometry and the pointer grip, including
clamped endpoints, 128-point cross-axis snap-back, capture cancellation and content
shrink during a drag. Native view bounds receive the same geometry that the GPU
scene presents. The platform supplies normal/hover/pressed colors.

The native ScrollBar adapter keeps wheel/fling services, track click/autorepeat,
key/menu dispatch and accessibility delivery. It adds a named numeric range/value
and increment/decrement actions. Native thumb painting is gated under Metal; its
fallback paints the same Rust display primitive. Visibility/animation/swipe/failure
paths invalidate that fallback. The overflow thumb stays visible, intentionally
replacing Cocoa fading/expansion in the opt-in backend without an animation timer.
Horizontal workspace scrolling and native node/track-controller removal remain.

Validated on 2026-10-22: **37/37 Metal browser regressions, 14/14 Views controls,
35/35 Rust tests and 59/59 Python tests passed**. Native input tests drag to both
ends with snap-back, click the track, dispatch Home/End and invoke accessibility
decrement; bitmap tests verify GPU/native paint routing. The matching signed,
timestamped app passed AX value, scroll-to-tab-24, selection and restore checks.
Scrollbar context-menu input and full VoiceOver/trackpad/RTL/scale coverage remain
unproven. These are functional results, not presentation timing measurements. See
[scrollbar validation](benchmarks/rust-scrollbar-functional-2026-10-22.json).

The footer now shares the same Metal canvas. Rust lays out pin/settings slots and
workspace strips, draws their icons, selected/plain dots and centered emoji, and
uses the existing input reducer for footer pointer/key state. Native controls
retain focus, default AX actions, tooltips, menus and application callbacks. Native
steady-state button drawing is gated; fallback paint is refreshed on ownership,
visibility, animation, swipe and drawable-failure changes.

Workspace overflow now measures the full preferred content width. Rust reserves
an additional 15-pixel horizontal scrollbar gutter only when needed, preserving
emoji height. The shared Rust scrollbar works in both axes, with synchronized
native hit testing and Rust viewport clipping/offset normalization. CoreText
centering is included in texture cache keys and frame fingerprints. No additional
Metal surface, queue or third-party dependency was introduced.

Validated on 2026-10-22: **38/38 Metal browser regressions, 14/14 Views controls,
37/37 Rust tests and 59/59 Python tests passed**. The new native regression covers
12 spaces, horizontal thumb drag, selecting the final workspace, settings access,
native Backspace emoji clearing, disabled pin input and unpinning. Manual signed
app inspection covered workspace/settings controls and centered emoji, followed
by restoring the original empty emoji and split pair. Navigation, native node
removal and the broader input/accessibility/scale matrix remain. These are
functional results, not CPU/GPU/presentation timings. See
[footer validation](benchmarks/rust-footer-functional-2026-10-22.json).

The main-window navigation strip now uses Rust layout/input and analytic GPU
back/forward/reload icons. Native outer placement and empty-header window dragging
remain intact. The main window and sidebar are separate native windows, so each
has its own small opaque canvas. A shared weak context cache reuses their device,
command queue and pipeline, releasing strong GPU ownership with the last surface.
Header rendering coalesces native invalidations before rasterization and tracks
visible bounds, theme, window movement and hide/show independently of the sidebar.

Rust preserves four 32-point controls with 2-point gaps. The shader evaluates line
and arc signed distances without changing the 32-byte quad ABI or adding icon
assets/dependencies. Native nodes retain names, disabled state, focus/AX actions
and the existing browser commands. Native glyph children, including focus rings,
are gated under Metal; fallback painting returns on ownership loss. Identical
scenes skip GPU submission, with no redraw timer.

Validated on 2026-10-22: **39/39 Metal browser regressions, 14/14 Views controls,
38/38 Rust tests and 59/59 Python tests passed**. Native input tests cover disabled
Forward, history Back/Forward, actual document reload, focus paint routing, sidebar
hide/show, window movement and main-window restore. Signed app inspection confirmed
history and title-reset reload while the sidebar was hidden, then restored the
original page, split pair and pinning. Native frame/control removal, peek-specific
coverage, the broader accessibility/input/scale matrix and controlled performance
comparison remain. See
[navigation validation](benchmarks/rust-navigation-functional-2026-10-22.json).

Split pane geometry, title text and divider drag state now come from Rust.
Both side-by-side and stacked layouts preserve independent Chromium pages.
Pane titles have no hidden Labels: native group names retain accessibility,
while cached font measurements bound the GPU text tiles. Empty titles emit no
ink. Menu/close controls and row close children use the shared Rust input reducer.

Three clipped Metal surfaces draw the two pane bars and divider grip, sharing
the existing device, queue and pipeline. Parent-relative rounded backing retains
the content card corners. Their bounds exclude both WebContents. Rust clamps
drag ratios, commits on release/capture loss and cancels stale drags when pane
identity or orientation changes. Native cursor, event, focus/AX and menu services
remain adapters; fallback painting returns when GPU ownership is unavailable.

Validated on 2026-10-22: **40/40 Metal browser regressions, 14/14 Views controls,
41/41 Rust tests and 59/59 Python tests passed**, without browser-test retries.
The new case covers physical divider drags in both orientations, gestures,
cancelled drags, Unicode title updates without native paint, page non-overlap,
fallback and split-half close. The signed app's pane menu switched both layouts
and restored the original demo state. Full trackpad/VoiceOver/IME/RTL/scale
validation, remaining Views controls and real presentation/CPU/GPU comparison
are still pending. See
[split pane validation](benchmarks/rust-split-panes-functional-2026-10-22.json).

Sidebar sizing now uses a Rust pointer/gesture state machine. It anchors the
stored preferred width, clamps to 200–480 points, reverses horizontal deltas for
RTL and commits on release/capture loss. Hiding the native control cancels a
pending drag. Programmatic cumulative-delta resizing retains its existing API
and uses the same Rust clamp in the Metal backend.

Rust also computes sidebar-window, content, edge-target and resize-handle bounds.
The visible sidebar keeps the original half-window cap without overwriting the
preferred width. Auto-hide guards are evaluated in Rust from native pointer,
button, pin and overlay state; deactivation keeps its original settings guard.
Pin/show/toggle commands, persistence and edge-enter delivery remain native.

The resize indicator is drawn by Rust + Metal on a two-point-wide surface that
shares the existing GPU context. It covers only the inset indicator rectangle,
preserving the sidebar outline, corners and existing canvas bounds. Native
drawing is gated during GPU ownership and invalidated when animation fallback
takes over. No redraw timer or new third-party dependency was added.

Validated on 2026-10-22: **41/41 Metal browser regressions, 14/14 Views controls,
44/44 Rust tests and 59/59 Python tests passed**, without browser-test retries.
Native cases cover physical sidebar dragging, gesture updates, persistence,
the half-window cap, auto-hide/edge reveal, settings guards, stale-drag rejection
and return of both Metal surfaces after workspace animation. The matching signed
app passed pinned/overlay and hide/show inspection, with its original demo state
restored. Native containers, edge-indicator paint, broader input/accessibility
coverage and actual presentation/CPU/GPU comparison remain. See
[sidebar sizing validation](benchmarks/rust-sidebar-sizing-functional-2026-10-22.json).

The address/Command Center result list now uses Rust scene text and analytic GPU
search/arrow/gear icons. Result rows do not create title/detail Labels; native
button names and detail descriptions retain accessibility information. Native
font measurements are cached and invalidated on title, active-state and theme
changes. The result canvas covers only the clipped scroll viewport and its
scrollbar, leaving the native editor and caret visible and interactive.

Rust owns result selection/wrapping/reconciliation, list and input-row geometry,
palette placement and the result viewport height cap. Application search/ranking
still supplies result identities; asynchronous updates preserve a deliberately
selected result. Native scrolling stays synchronized with GPU geometry and
native hit testing. Title/detail drawing returns when Metal is unavailable.

The native Textfield retains editing, text selection, clipboard and IME. While
it reports active composition, the Metal backend's palette controller leaves
navigation, commit and cancel keys to the editor. Static palette chrome, error
and hint Labels, native containers, services and application commands remain to
be migrated. A direct TextInputClient composition regression does not establish
coverage of every installed OS input method or candidate window.

Automated validation on 2026-10-22 passed **42/42 Metal browser regressions,
17/17 Views controls, 47/47 Rust tests and 59/59 Python tests**, with no retries
in the final browser matrices. The new case verifies asynchronous selection
identity, stale-result guards, wrap/scroll, composition key routing, fallback
paint and activation through native application events. The signed package
matches the built app and passed strict signature/timestamp checks.

**Manual visual inspection is pending:** computer use reported a locked Mac and
the user was asked to unlock it. The matching prototype was launched only on the
isolated demo profile. Automated in-process events do not replace that visual
check, physical input/VoiceOver/scale coverage or actual presentation/CPU/GPU
measurement. See
[palette validation](benchmarks/rust-palette-functional-2026-10-22.json).
