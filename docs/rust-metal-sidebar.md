# Real workspace sidebar: Rust + Metal prototype

The optional `--zitter-sidebar=rust-metal` backend renders the **live sidebar, navigation strip and split pane controls** in the normal workspace window. It does not start the synthetic `--zitter-ui-lab` scene. It uses the same `ShellState`, native interaction nodes, controller callbacks, profiles and session persistence as the existing application.

## What moved

- Rust produces the display list for actual tab titles, site monograms, selected/hovered cards, folder rows, section labels, close affordances and both tiles of a Split View pair. It also handles next/previous tab index wrapping for the Metal backend. Row title/detail/close geometry and pointer drag state now come from Rust: the native interaction adapter and the GPU display list share the same row geometry. The platform still measures text and delivers events; a Rust input reducer owns row hover/pressed states, click eligibility, latched drags, cancellation and platform key activation. On macOS Space activates on key-down, while Return remains available for the native default action. Disabled rows cannot activate; capture loss and focus loss cancel pending gestures.
- A visual-only AppKit view presents directly through an asynchronous `CAMetalLayer`. Rounded geometry is drawn by Metal; CoreText shapes/truncates real Unicode text, including system font fallback. Text tiles are cached, bounded to 256 entries / 16 MiB, and uploaded on cache misses. Live tab/pin/folder rows no longer create hidden Views title Labels; their titles come directly from the row model and the Rust display list. No web page pixels are copied into the sidebar.
- Rust also lays out the vertical columns of the live tab/pin/folder tree, with native adapters supplying preferred sizes and installing the returned rectangles. Hidden children are omitted from spacing; margins and full scroll extent are preserved. Rust also sets folder indentation (12 logical pixels per level, capped at eight) and lays out indented pin wrappers and split tiles. Split tiles share a four-pixel frame/gap, including odd-width remainder distribution; compact preferred size is 200 × 46 logical pixels. Rust projects and clips the viewport from native local bounds and floating-point scroll offsets, normalizing offsets back into the physical input adapter. The native tree retains node identity and receives the Rust layout rectangles. Paint invalidation and scroll notifications coalesce updates onto the UI task queue, independently of whether the native compositor rasterizes a frame. Pending layouts are applied before collecting scene geometry; identical frames skip GPU submission. There is no polling/redraw timer. The shared tab ScrollView now sizes its contents to their full preferred height, fixing overflow scrolling in both backends. In Metal mode, Rust now sizes and drags an always-visible overflow thumb on the shared GPU canvas. Native ScrollView/ScrollBar services retain wheel, key/menu and AX delivery adapters. Its list offset stays on the UI thread to keep native hit testing synchronized with the separately presented Metal surface.

This is an incremental renderer prototype, **not a complete removal of Views**. Views still supplies outer/container layout, event delivery, focus, accessibility/default action, context menus, drag-to-split target policy, dialogs, remaining service-field editing and native IME delivery. Rust owns the migrated address/workspace/navigation control geometry, scene drawing and input decisions through those adapters. RowButton mouse/key decisions use Rust, including palette and New Tab rows, while application callbacks still own browser commands. Its tab interaction nodes remain in place; the Metal overlay returns `nil` from hit testing, cannot become first responder and adds no accessibility element. The fallback painter remains available for startup, missing drawables and workspace animation snapshots. While Metal is visible, the list paint root skips native child traversal instead of painting those rows underneath it. Hiding Metal for an animation explicitly invalidates native contents; losing a previously visible drawable also requests one fresh fallback paint. Views still supplies hierarchy, interaction nodes and some layout, so this does not establish the performance of a future entirely Rust-owned sidebar.

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

Static palette search/escape decoration, hints and errors now use Rust layout
and Metal rendering. The four small chrome canvases share the existing GPU
context. They update even when the result list is empty; the hint canvas covers
only its text so the native editor, separator and rounded corners stay visible.
These controls create no hidden Labels and retain native static-text AX nodes.
Default Views keeps its original Labels and glyph control.

Rust supplies the rounded frame and hint separator recipe, rasterized through
the native compositor behind the editor. Caret-only editor repaints skip scene
updates; text/data/theme changes still invalidate explicitly, including updates
under a native paint lock. Native editing/IME, containers and application services
remain adapters. See
[palette chrome validation](benchmarks/rust-palette-chrome-functional-2026-10-22.json).

Final validation on 2026-10-22 passed **43/43 Metal browser regressions,
17/17 Views controls, 50/50 Rust tests and 59/59 Python tests**, with no browser
retries. GN dependency checks, product boundaries and strict bundle signature
verification passed; bundle/build UUIDs match. On the unlocked desktop, the
matching signed isolated app displayed command rows, Unicode results, keyboard
selection and both unknown-command/invalid-address errors. Escape restored the
original demo without navigating or changing its tabs/split pair. This newer
check completes the desktop palette inspection that was blocked in the previous
milestone. Physical input, broader accessibility/IME/scale coverage and real
CPU/GPU/presentation comparisons remain pending.


Workspace swipe state and preview placement now belong to Rust in the opt-in
backend. The native adapter supplies scroll phases, dimensions and the current
workspace index; Rust owns the movement threshold, axis lock, accumulation,
reversal, multi-workspace position, bounded overscroll and nearest target. It
also returns up to three neighboring preview indices and their fractional
translations. Native workspace identities, commit callbacks, timers, cached paint
records and compositor animation remain. The default Views controller retains
its previous gesture policy through the same adapter.

As of 2026-10-23, the user has deferred windowed tests to a later checkpoint.
This stage therefore has non-windowed verification only; it does not extend the
last accepted 43 Metal / 17 Views runtime matrix. See the
[checkpoint policy](rust-metal-parity.md#current-checkpoint-policy-2026-10-23) and
[verification report](benchmarks/rust-workspace-swipe-headless-2026-10-23.json).


Workspace gesture previews now have a direct Metal rendering path. The native
adapter resolves cached tab/pin/folder data and font metrics; Rust computes row,
section and split-pair bounds, applies source scroll offset, and culls/clips
against the preview viewport. The preview then renders the established Rust
row display list, including Unicode text, monograms and active close controls.
Split tiles use the live Rust geometry, including odd-width distribution.

Up to three visual-only page canvases share the existing Metal context. Each
canvas has a native viewport clipping adapter and translates its cached drawable
under the gesture. An immutable cached scene only redraws on capture, changed
bounds/scale or drawable loss; movement alone skips display-list regeneration
and GPU submission. Core Animation provides the 160 ms final settle, respecting
the existing reduced-motion/workspace-animation policy at the call site. Page
scenes are bounded to eight cached entries and clear on viewport size changes.

Native paint-record previews remain a complete fallback: if any required canvas
is unavailable, all GPU pages hide before native preview layers are used. A
successful Metal preview skips native snapshot capture. Closing/cancelling the
preview destroys the page canvases/clipping adapters. Existing native input,
accessibility and application workspace commits remain unchanged adapters.

This stage passed 61 Rust and 59 Python tests and compiled both app and browser
test binaries. The new `SwipePreviewUsesMetalWithoutNativeSnapshots` browser case
is compiled **but not run**, following the user's no-windowed-tests instruction.
It is intended to check scene text, zero native captures, drawable reuse,
commit/cancellation and return to the normal sidebar at the later checkpoint.
Actual clipping, settle animation and GPU ownership are still unverified in a
running window. Regular click/shortcut workspace-switch animation continues to
use native-painted content. See
[scene verification](benchmarks/rust-workspace-preview-scenes-headless-2026-10-23.json).


Regular workspace switches (clicks and configured keyboard actions) now use a
single Metal viewport overlay in the opt-in backend. Rust decides whether motion
is allowed and supplies the existing 32-point directional displacement and
180 ms duration. The scene-data adapter is shared with gesture previews; no
native row snapshot is required on the successful GPU path. Non-Metal Views and
missing-GPU fallback retain the established native layer animation.

The overlay registers a one-shot `MTLDrawable` presented handler before submitting
its first frame. That handler posts back to the UI sequence before starting the
Core Animation translation. Animation completion also posts to the UI sequence;
it never tears down an active delegate synchronously. Generation guards reject
stale presentation/completion callbacks after a new switch, hide, disable or
teardown. This adds neither a polling timer nor a synchronous GPU wait. The
ordinary sidebar canvas is restored after the transition overlay is removed.

Underlying row painting is gated while either GPU transition owns the viewport.
Removing a transition explicitly invalidates native rows so missing-drawable
fallback can paint fresh content. Native event/focus/AX nodes and browser/profile
commands remain adapters.

Non-windowed verification passed **63 Rust tests and 59 Python tests**. The new
`WorkspaceSwitchUsesPresentedMetalSceneAndCancels` browser regression is compiled
but not run; it covers target scene text, normal completion, rapid replacement,
disable, hide and window reflow, followed by ordinary Metal restoration. Actual presentation
ordering, CA callbacks, cancellation, missing drawables and visual quality still
require the requested GUI checkpoint. This is not a measured performance gain.
See [switch verification](benchmarks/rust-workspace-switch-headless-2026-10-23.json).


Tab/pin context menus now consume a Rust command/label plan in the opt-in backend.
The plan preserves folder/Peek ordering, pair/unpaired actions, orientation-aware
labels and HTTP-only rule entries. URL and rule semantics remain native; only
an HTTP eligibility flag crosses the Rust boundary. Native popup presentation
and native View-to-item lookup remain adapters.

Rust also owns target resolution, split-side action availability, typed command
selection and the existing physical right-half drop policy. Menu and drop tasks
revalidate configuration/busy state, source workspace and item existence before
calling application commands. Queued menu actions resolve a surviving page at
its current index and reject a removed page even if its old index/URL is reused.
They respect split membership/direction and the last-tab close guard.

The native page-data adapter now carries the full opaque 128-bit WebContents
unique token as two fixed-size integers, avoiding per-update string formatting
and allocation. No browser object, URL or profile crosses into Rust. Native row
caches include that identity in structure comparisons so replacement pages get
fresh callbacks. Pins retain their existing stable IDs; folders cannot become
split-drop targets. Cached drop callbacks also retain their source workspace.

Non-windowed verification passed **68 Rust tests and 59 Python tests**. The new
`RowMenuPlansAndQueuedIdentitySurviveReindex` browser case is compiled but not run.
It builds menu models without showing a popup, but its browser fixture opens a
window, so it remains deferred with the other GUI tests. It checks index shifts,
removed/replaced targets, workspace changes, split-side availability and vertical
labels. Actual popup interaction, drags, AX/keyboard behavior and profile/session
regressions still require the checkpoint. The default Views menu plan and native
command path remain for control comparison. See
[row menu verification](benchmarks/rust-row-menu-headless-2026-10-23.json).


Folder creation and rename now use a Rust-owned form body in the opt-in backend.
Rust supplies the title/field/button copy, Unicode trimming, the existing 256-byte
UTF-8 name limit, live parent/rename-target checks, source-workspace/readiness/busy
guards, duplicate-submit/cancel state and layout. The field remains a native
Textfield for selection, clipboard and IME; active composition cannot submit.
The modal host retains its native title/button presentation, initial focus,
Enter/Escape handling and window lifetime. At that milestone, delete confirmation and asynchronous save-error bodies
still used native presentation; the notice milestone below replaces those bodies.

The form body creates static-text AX Views and no hidden Labels. Rust emits the
field label and validation error text on two small shared-context Metal canvases
whose bounds exclude the editor. Native fallback consumes the same scene recipe;
validation errors update AX names and send an alert event. Caret-only paint
invalidations do not schedule scene work, and unchanged error text does not
resize/redraw the form. Error visibility changes resize the modal through its
existing host adapter.

Submission rechecks the live context and then calls existing create/rename
services. A parent is expanded before admission and restored if the application
rejects immediately. Acceptance closes the form and prevents duplicate admission;
it is **not** proof that asynchronous disk storage completed. The application
retains its existing configuration write, rollback and separate error dialog.

Non-windowed verification passed **74 Rust tests and 59 Python tests**. The new
`FolderFormScenesValidationCompositionAndStaleContext` browser case is compiled
but not run. It checks scene/AX structure, empty/long names, IME composition,
Unicode create/rename, removed parents and workspace changes. Existing create,
rename/delete and failed-write cases must also run in both backends at the GUI
checkpoint; the Views control set expands from 17 to 20. Native modal behavior,
GPU fallback, focus, alert announcement, sizing, theme/scale and persistence
integration remain unaccepted until then. See
[folder form verification](benchmarks/rust-folder-form-headless-2026-10-23.json).


Folder deletion confirmation and save-error message bodies now use the same
Rust/Metal approach. Rust supplies the complete warning, Unicode folder title,
button copy, default-cancel policy, acknowledgement/deletion state and live
workspace/configuration/folder guards. A rejected delete can retry; duplicate,
cancelled and accepted admissions cannot dispatch again. Save-error acknowledgement
is independent of workspace/busy state and never emits a deletion target.

A CoreText typography adapter breaks complete paragraph text into UTF-8 line
fragments using the same system font as the existing GPU text tiles. It falls
back to a composed-character boundary if a line is narrower than one cluster.
Rust places all body/error lines and computes their extents/gap. The resulting
ordinary text primitives use the existing shader and 32-byte quad ABI. Native
fallback paints the same line plan, and a full-paragraph static-text AX node
preserves the warning for assistive technology without hidden Labels.

Body/error canvases share the existing GPU context. Typography results cache by
width/font/error text; unchanged scenes keep the existing fingerprint skip.
Validation errors update AX names and alert events. Native title/button drawing,
modal focus/default-button behavior and widget lifetime remain host adapters.
Application deletion, preservation of already open tabs, configuration writes,
async rollback and error delivery remain unchanged services. Acknowledgement of
admission remains distinct from durable storage success.

Non-windowed verification passed **80 Rust tests and 59 Python tests**. The new
`FolderNoticesWrapPreserveCancelAndRejectStaleTargets` case is compiled but not
run. It checks wrapped warning/AX structure, default Cancel, cancellation,
foreign/removed targets, confirmed deletion and save-error acknowledgement.
The checkpoint must run the 48-case Metal matrix and 20 Views controls, including
all existing folder/persistence cases. Actual CoreText wrapping, canvas clipping,
GPU fallback, focus/keyboard/AX/default-button behavior, theme/scale and failed
writes remain unaccepted until then. See
[notice verification](benchmarks/rust-folder-notices-headless-2026-10-23.json).


## Profile management controller and static text (GUI checkpoint pending)

The opt-in profile panel now keeps its selection, attach-workspace context,
submission state, status and destructive confirmation in Rust. Selection follows
profile IDs across catalogue reordering, rename and removal; adding a profile
selects the new entry as before. Deletion rechecks the current ID, usage, catalogue
size and readiness on both clicks. Selection/catalogue changes, busy state or a
new workspace usage disarm confirmation. Create/rename normalize Unicode
whitespace, preserve the existing ASCII-only case-insensitive duplicate policy,
and recheck the attach workspace. The native editor retains IME; actions wait
until composition ends.

Rust supplies panel copy, button labels, column layout and paragraph line
placement. Static headings, help, labels and feedback are plain accessibility
nodes plus clipped Metal canvases sharing the existing device/queue/pipeline.
CoreText shapes the same regular/medium system font used by GPU tiles; native
fallback consumes the same scene recipe. Paragraph metrics cache by text/width/
font and unchanged scene fingerprints skip GPU submission. No hidden native
Labels measure panel copy. Native Textfield, Combobox, MdTextButton, focus and
outer popup placement/lifetime remain adapters and are not yet migrated.

Typed Rust requests call existing application profile services. Profile creation,
assignment, leases, directories, async persistence and rollback remain there;
accepted UI admission does not mean a completed durable write. Native service
status is retained after admission, and controls resynchronize after synchronous
callbacks as well as later busy-state updates. No dependency or framework was
added; the existing license/notice arrangement is unchanged.

**87 Rust and 60 Python tests passed** without windows. GN dependency checks,
boundary checks and both optimized app/browser-test builds passed. The new
`ProfilePanelScenesValidationIMEAndStableDeletion` case is compiled but unrun.
The deferred checkpoint now lists **53 Metal cases and 24 Views controls**, adding
`DynamicProfiles`, `DynamicWorkspacesAndProfiles`, `WorkspaceDotProfileMenu` and
`FailedConfigurationWritePreservesProfiles` to both modes. It must verify actual
font/wrapping/clipping, native/Metal overlap and fallback, IME/keyboard/AX/focus,
stable selection/destructive confirmation, async failure/retry, profile data
isolation and durable restore. Nothing from this stage has GUI acceptance and
no presentation/CPU/GPU speedup was measured. See
[profile panel verification](benchmarks/rust-profile-panel-headless-2026-10-23.json).


## Workspace create/edit settings (GUI checkpoint pending)

Rust now owns edit/create/cancel mode, current workspace identity, readiness and
pending-admission guards, captions, validation/error feedback and typed requests
for workspace creation or name/profile changes. Empty names, missing profiles,
changed/removed workspace context and busy/unready state cannot dispatch. IME
composition waits for completion. Names retain the original ASCII-only trimming
policy: Unicode whitespace, duplicate names and long names have no new restriction.
The application still allocates new workspace IDs and owns profile assignment,
beforeunload guards, BrowserContext isolation, tab replacement and persistence.
A create callback can select its allocated workspace before returning admission;
the Rust completion path preserves that refreshed identity. Admission is not a
completed configuration write or beforeunload decision.

The settings body and appearance section use the existing Rust column adapter,
preserving 20-point panel padding and the one-point rounded border. The paragraph
adapter previously used by profiles is now shared as `SceneParagraph`. Settings
headings, labels, profile description, help/warnings and configuration/appearance
errors use CoreText shaping, Rust line placement and clipped shared-context Metal
canvases, with full-text accessibility nodes and the same native fallback recipe.
The warning that profile changes close workspace tabs remains intact. Async
configuration and appearance errors reach these text nodes and resize the panel.
Existing text/metrics caches and frame fingerprints still apply.

Native Textfield/IME, Combobox, buttons, header/action/color/emoji row layout,
color parsing, Unicode grapheme extraction, appearance storage, outer panel and
workspace deletion modal remain. This is not complete workspace control migration.
No shader ABI, binary dependency, crate or framework was added.

**95 Rust and 61 Python tests passed** without windows, along with scoped GN,
boundary checks and optimized app/browser-test compilation. The new
`WorkspaceFormScenesCreateCancelIMEAndProfileAssignment` regression is compiled
but unrun. The checkpoint now has **55 Metal cases and 25 Views controls**; it
also adds `WorkspaceDeletionConfirmationStaysOnScreen` in both modes. Existing
profile assignment/isolation, native appearance, deletion and persistence cases
remain required. The checkpoint must verify actual wrapping, clipping/overlap,
keyboard/IME/AX/focus, native/Metal fallback, create/cancel/stale context, profile
change/beforeunload semantics, async error delivery and durable restore.

Build inputs use committed `6b4723d` bootstrap/build tools plus only the four UI
source overrides for this stage, excluding the concurrent network-policy task's
uncommitted bootstrap/tool edits. The shared Chromium updater patch is unchanged
from the preceding milestone and identified in the report. No GUI acceptance or
CPU/GPU/presentation gain is established. See
[workspace form verification](benchmarks/rust-workspace-form-headless-2026-10-23.json).


## Workspace deletion notice and shared rendering (GUI checkpoint pending)

Workspace deletion now has a Rust notice controller. It captures the target ID
when opened and deliberately permits deleting a non-active workspace; changing
the active workspace never redirects the request. Each admission rechecks the
configuration/busy state, target existence and remaining workspace count. Missing
and last-workspace targets reject with inline feedback. Pending, cancelled and
accepted notices cannot dispatch twice; immediate service rejection can retry.
The complete irreversible-deletion warning and separate paragraph stating that
the browsing profile, passwords, history and cookies are retained remain intact.
Cancel remains the native default button.

Folder and workspace notices now share `NoticeCopy`, `notice_layout` and the
`NoticeBodyView` rendering adapter. Their Rust controllers and typed requests
remain distinct. CoreText line shaping, Rust body/error placement, clipped Metal
canvases sharing the GPU context, full static-text AX nodes, alert events and
native fallback consume the same paragraph recipe. The extracted renderer keeps
existing text/metric caches, fingerprint submission skips and weak scheduled
updates. There are no hidden native body Labels and no shader/quad ABI change.

The native modal host still owns title/button drawing, focus/keys, placement and
widget lifetime. The captured Rust Delete request invokes the existing workspace
removal callback. Beforeunload handling, tab/pin disposal, retained profile data
and configuration writes stay in application services. Accepted admission can
still be cancelled later by beforeunload and is not proof of deletion or durable
storage. Immediate rejection keeps the notice open with an inline error and also
preserves the existing settings error delivery.

**101 Rust and 61 Python tests passed** without windows. Scoped GN checks,
boundaries and optimized app/browser-test builds passed. The new compiled case,
`WorkspaceNoticeCancelCapturedTargetUnloadAndRetainedProfile`, awaits execution.
It covers Unicode title/default Cancel, complete warning/AX body without Labels,
selection changes behind the dialog, rejected/accepted beforeunload, retained
profile storage and a removed target with inline Metal error. Both backends also
add `BeforeUnloadProfileSleepAndWindowClose`, bringing the deferred checkpoint to
**57 Metal cases and 26 Views controls**. Shared folder notice regressions and
workspace modal bounds cases remain required.

Actual paragraph/blank-line shaping, clipping, fallback, theme/scale, keyboard,
focus/accessibility, lifetime/retry, beforeunload and durable restore remain
unaccepted until the requested GUI checkpoint. Build inputs use committed
`bd363ab` tools/bootstrap plus this stage's four UI overrides, excluding unrelated
uncommitted network-policy work. No app was launched and no new dependency,
framework or license was introduced. No CPU/GPU/presentation gain is claimed. See
[workspace notice verification](benchmarks/rust-workspace-notice-headless-2026-10-23.json).


## Workspace appearance preset rows (GUI checkpoint pending)

Rust now supplies the existing four-color/six-emoji preset data and labels,
selection matching, row geometry, editor exclusion and analytic color swatch
primitives. Selection preserves RGB matching and accepts custom colors or emoji
with no preset selected. The row keeps original 32×32 color and 30×28 emoji
controls, an eight-point editor gap and centered native editing bounds. Extremely
narrow synthetic widths distribute control bounds without negative sizes or
editor overlap; real window/theme/scale behavior still requires the checkpoint.

Each row batches its controls on one clipped Metal canvas sharing the existing
GPU context. Existing glyph primitives draw centered emoji, selection/hover wash
and focus feedback; the new ordinary swatch kind emits a raw-color circle,
optional stroked selection/hover ring and focus marker. No shader/quad ABI change
or new dependency is needed. The canvas excludes native editor bounds. Native
fallback paints the same Rust recipe, including the primitive's text-centering
flag (now respected by the shared fallback helper).

Preset buttons reuse the Rust pointer/key reducer through native event/focus/AX
nodes. Their native focus ring and click ink effects are removed; hover/focus/
selection invalidations schedule the row scene directly, including before native
rasterization. Capture loss, workspace changes and hiding the appearance section
cancel armed gestures; a later release cannot mutate a new/hidden workspace.
Visibility and teardown hide/drop canvases; unchanged fingerprints skip GPU work.
Native color parsing, Textfield/IME/caret/selection, Unicode grapheme extraction,
appearance storage and async error delivery remain services. Composition in
color/emoji editors waits before handling form keys. No editor or storage service
was replaced in this stage.

**108 Rust and 61 Python tests passed** without windows. Scoped GN/dependency and
boundary checks and optimized app/browser-test builds passed. The compiled
`AppearanceRowsSceneInputClippingCompositionAndWorkspaceChange` regression remains
unrun. It checks submitted canvas/editor separation, native focus-ring removal,
mouse/capture cancellation, Space activation, preset/custom values, composition,
workspace-change and visibility cancellation, and row restoration. The deferred
checkpoint now has **58 Metal cases and 27 Views controls**, adding the existing
`WorkspaceContextAppearanceAndDeletion` case to Views. It must also recheck shared
footer/navigation/row input and fallback/palette rendering after adapter changes.

Actual clipping/presentation, focus/keyboard/AX, pointer capture, IME, first
Unicode grapheme, theme/scale, fallback, visibility/teardown and durable appearance
restore remain unaccepted until the requested GUI checkpoint. Build inputs use
committed `498af7d` tools/bootstrap plus four UI overrides, excluding unrelated
network-policy edits. No app/window was launched and no CPU/GPU/presentation
speedup is established. Native Button containers, editors, action/header rows,
outer panels and other Views surfaces remain. See
[appearance verification](benchmarks/rust-appearance-headless-2026-10-23.json).


## Profile/workspace form actions (GUI checkpoint pending)

Nine actions now use a shared text-button adapter and Rust geometry/input/drawing:
workspace save/create/cancel, profile management/import/workspace removal, and
profile create/rename/delete/back. Adjacent controls share four Metal canvases
(two workspace groups and two profile groups), excluding native input controls.
Native MdTextButton and its text Labels are absent for these opt-in actions.

Rust consumes supplied platform text width, control height, horizontal padding
and minimum-width metrics to calculate button bounds. Vertical groups preserve
full-width controls and original gaps; horizontal groups preserve natural sizes
and distribute insufficient width safely. The ordinary text-action primitive
supplies pill backing, hover overlay, optional border, centered text and focus
feedback using the existing shader/quad ABI. Platform typography/color services
still supply fonts and enabled/disabled/prominent/text colors. Existing CoreText
regular/medium text tiles and the native recipe fallback render the same display
list; actual typography/colors remain subject to GUI acceptance.

`SceneTextButton` reuses the existing Rust pointer/key reducer through the shared
native event adapter. Native focus-ring, click-ink and state animation drawing
are disabled for these controls. Caption changes cancel armed input, refresh AX
names/tooltips/metrics and explicitly schedule scene updates. Group visibility,
workspace switches, profile catalogue/selection changes and reopening profile
management cancel pending activation. Mac Space follows the existing key-down
policy; Return remains available to the native default action. Native font,
accessibility, focus/event delivery and command/storage services remain adapters.
The legacy Views path retains MdTextButton controls, grouped with the same gaps.

**115 Rust and 61 Python tests passed** without windows, along with scoped GN,
boundary checks and optimized app/browser-test builds. The compiled regression,
`FormActionGroupsSceneTextDynamicLabelsAndCancelledTargets`, awaits execution.
It covers group text/visibility and absence of MdTextButton/Labels/focus rings,
create/cancel captions, stale workspace/profile gestures, mouse and Space actions,
profile validation, two-step deletion and configuration completion. The common
FindButton browser-test helper now also resolves non-Md buttons by their native
AX names; all curated cases must be rechecked after that change.

The checkpoint now has **59 Metal cases and 27 Views controls**. It must inspect
real typography/ellipsis, enabled/disabled/default/prominent/text styles,
hover/focus/keyboard/AX, clipping/scale/theme, fallback and group visibility,
repeated/cancelled gestures and existing persistence/isolation/beforeunload cases.
Build inputs use committed `fd51257` tools/bootstrap plus four UI overrides,
excluding unrelated network-policy edits. No app/window was launched and no
CPU/GPU/presentation speedup was established. Native interaction containers,
editors/pickers, header/outer panel and modal-host controls and other Views
surfaces remain. See
[form action verification](benchmarks/rust-form-actions-headless-2026-10-23.json).


## Import form controller and presentation (GUI checkpoint pending)

Import now has a Rust UI controller for the four browser choices and existing
option defaults, option snapshots, debounce tokens, waiting/scanning/applying
state, control availability and Apply admission. Scans keep source/options and
close editable; submitted/applying operations lock them. No data category is
newly required: profile/workspace-only selections remain possible. A timer can
consume only the latest visible selection once; hidden/cancelled or superseded
tokens cannot start a read. Programmatic option changes without a native callback
request a fresh preview before Apply. A scan blocked by configuration readiness
or another profile operation waits and resumes from the native state-change
notification when the panel is visible.

Native preview service startup supplies only its UI option snapshot to the Rust
controller through `BeginImportPreview`. Source paths, imported URLs/history/
cookies/profile objects and credentials remain in native services. Existing
`WorkspaceWindow` generation checks still reject obsolete I/O replies; Rust owns
UI debounce rather than replacing that filter. Native status remains authoritative
and can unlock retry after a read/authorization error. A synchronous completion
cannot be overwritten by the callback's later admission return. Duplicate and
hidden Apply requests are rejected; admission still differs from durable import
completion.

Rust supplies fixed panel copy/source labels, column geometry and control state.
Shared paragraph canvases render title/help/headings/status without native static
Labels, and a shared action group renders Apply without MdTextButton. Native
checkboxes and their labels, source Combobox, header close button/row placement,
outer popup and import-summary/error formatting remain. The body preserves its
20-point padding, one-point border and twelve-point spacing. Native source I/O,
read-only preview, explicit cookie authorization, data import, profile isolation,
first-run behavior and persistence/rollback remain services; only one UI metadata
notification was added before the existing source read.

**125 Rust and 61 Python tests passed** without windows, with scoped GN,
boundaries and optimized app/browser-test builds. The compiled case
`ImportFormScenesLatestOptionsLocksAndAdmission` is unrun. It uses owned source
fixtures and checks static/Apply scene text, native selector retention, rapid
source changes, callback-free option mutation/rescan, error text, applying locks,
Space admission and final configuration state. Seven existing import cases add
source immutability, encrypted-cookie opt-in/denial, destination persistence,
option invalidation and first-run rollback/offer to both backends. The deferred
checkpoint now totals **67 Metal cases and 34 Views controls**.

The checkpoint must verify real rendering/wrapping/clipping, keyboard/AX and
native selector behavior, busy/wait/resume and callback/visibility races, stale
I/O replies, single Apply admission, authorization denial/retry, source data
retention, first-run and durable restore. Build inputs use committed `cc0023a`
tools/bootstrap plus five UI/metadata-notification source overrides, excluding
unrelated network-policy edits. No app/window was launched, new dependency introduced or shader/quad ABI changed.
No CPU/GPU/presentation speedup was established. See
[import form verification](benchmarks/rust-import-form-headless-2026-10-23.json).


## Import checkboxes (GUI checkpoint pending)

The four import checkboxes now keep their checked value in Rust and use the
existing Rust pointer/key reducer through native Button adapters. A user action
checks availability, toggles once, updates native checked/default-action AX state
and then invokes the existing form callback. Programmatic SetChecked remains
idempotent, can set disabled controls and does not dispatch that callback. Disabled
user activation cannot mutate the value or dispatch. Capture loss, visibility and
option/source context changes cancel armed input; changing a label also cancels
its pending gesture.

Rust places a sixteen-point mark, eight-point label gap, platform insets and all
CoreText-shaped label lines, including RTL mark/label mirroring. One Metal canvas
batches the four controls and excludes other panel controls. The ordinary checkbox
mark primitive supplies backing/outline, centered check glyph, hover and focus;
label lines use the existing text primitive. The check glyph uses the existing
CoreText tile path, so no shader, quad ABI, dependency or icon asset was added.
Native fallback consumes the same recipe. Native Checkbox/Label/image/focus-ring/
ink drawing is absent for these opt-in controls; platform fonts/insets/colors and
native Button/event/focus/AX nodes remain.

Legacy Views keeps actual Checkbox controls. Internal import checkbox accessors
now return the common Button interface so native test clicks still target the
real control in either backend. Option values remain available through
`import_options()`; callback-free test mutation uses the explicit
`SetImportOptionCheckedForTesting`. The two existing checkbox-specific browser-test
calls were adapted to that state/AX interface; their behavior remains required at
the checkpoint.

**132 Rust and 61 Python tests passed** without windows, along with scoped GN,
boundaries and optimized app/browser-test builds. The new compiled case,
`ImportCheckboxScenesAXSpaceCaptureDisabledAndVisibility`, awaits execution. It
checks default flags, absence of native Checkbox/Labels/focus rings, native AX
role/checked state, capture cancellation, Space/default-AX activation, disabled
mutation, source-caption cancellation and hide/reopen. The deferred matrix now
contains **68 Metal cases and 34 Views controls**, retaining all import option,
credential opt-in, persistence and first-run cases.

Actual check glyphs, complete wrapping, RTL, theme/scale, clipping, fallback,
keyboard/focus/AX announcements, capture/visibility/context transitions and import
persistence remain unaccepted. Build inputs use committed `6311e94` tools/bootstrap
plus four UI source overrides, excluding unrelated network-policy work. No app or
window was launched and no CPU/GPU/presentation gain was established. Native
source/header/outer controls and other Views surfaces remain. See
[checkbox verification](benchmarks/rust-checkboxes-headless-2026-10-23.json).


## Import source selector (GUI checkpoint pending)

The opt-in import source list now uses a `SceneSelect` native event/focus/AX node
with Rust selected-index state and immutable UI label/search options. The closed
control has one Metal canvas and a Rust recipe for backing, border, focus, label
and arrow; the native fallback consumes the same recipe. It contains no native
Combobox/Label/arrow-button/focus-ring/ink painting. Legacy Views retains its
Combobox. The two dynamic profile pickers are unchanged.

Rust mirrors the existing macOS Combobox policy: Up, Down, Space, Home and End
open a popup rather than advancing selection directly. A native `MenuRunner`
with `COMBOBOX` handles the popup itself. Each opening captures a Rust generation;
commands from dismissed, hidden, disabled or superseded menus cannot change the
selection. Selecting the same item through a menu still invokes the existing
source-change callback, matching native Combobox behavior.

A `PrefixSelector` subclass supplies the native text-input protocol, including
its existing non-inline composition behavior. Committed InsertText/InsertChar
input is normalized with Chromium ICU, then Rust owns prefix state, the original
one-second timeout, wraparound matching and selected index. Prefix extension
starts at the current row; a new search starts at the next row. Focus/input-method
changes reset the prefix. Disabled or hidden controls cannot change the source.
The UI carries labels/options only: native source paths, parsers, imported data,
credentials and destination services never enter Rust.

Native platform font, color and minimum-control-height services remain. Rust
places the text and ordinary text-tile arrow with bounded and mirrored RTL
geometry. The existing shader and GPU quad ABI are unchanged. Weak scheduled
scene invalidation updates independently of native painting; surface fingerprints
skip identical recipes. This does not establish an end-to-end speedup.

The internal source accessor now exposes a common View node. Browser tests use
`SelectImportBrowserForTesting` to dispatch a source action for either backend;
production menu/typeahead callbacks retain the existing import invalidation and
debounce path. `import_source_metal_visible` and scene-text inspection are
checkpoint diagnostics. Source reading/import service behavior is unchanged.

**140 Rust and 61 Python tests passed** without windows, with scoped GN and
boundary checks, optimized app/test compilation and a final no-work Ninja check.
Eight new Rust cases cover popup admission/staleness, disabled/empty/out-of-range
values, prefix extension/timeout/reset/control filtering, native macOS menu-key
policy, saturated/RTL geometry and closed-control primitives/clipping.
`ImportSourceScenePrefixAXDisabledAndVisibility` was compiled only: it checks the
absence of a native Combobox/Labels, AX role/value/collapsed state, committed
prefix input, disabled/invalid selection and hidden/reopened surface behavior.
The deferred matrix is **69 Metal cases and 34 Views controls**.

No application or windowed test was launched. The later checkpoint must exercise
actual popup open/selection/cancel and lifetime, pointer/capture/focus, keyboard,
IME/VoiceOver announcements, text/arrow rasterization, full labels and narrow
bounds, theme/scale/RTL, native fallback and import persistence. Existing post-
baseline GUI results remain unaccepted. Packaging/signing and CPU/GPU/actual
presentation comparisons remain separate pending work. See
[source selector verification](benchmarks/rust-source-picker-headless-2026-10-23.json).


## Profile selectors (GUI checkpoint pending)

Workspace assignment and managed-profile selection now use `SceneSelect` in the
opt-in backend, sharing the Rust/Metal closed-control recipe and native popup,
IME, focus, AX and event adapters introduced for the import source. Each has one
Metal canvas with no native Combobox/Label/arrow-button/focus-ring/ink painting.
Legacy Views retains both native Combobox controls. Native platform typography,
colors/control-height metrics, profile creation/deletion/storage, browser-context
isolation and workspace persistence remain application services.

Rust selector options now carry stable UI keys. Replacing a model preserves the
selected key across reorder and rename, even when labels are equal. Workspace
selection falls back to the first entry if its key disappears; management falls
back to the last, and the existing newly-created-profile rule selects the last
entry. The existing Rust ProfileForm reconciler remains the source of management
form intent; it synchronizes the selector after updates. An empty model has no
selection, no AX value and no admissible popup. Rust rejects duplicate or empty
keys without mutating the option list.

Model replacement and programmatic selection cancel armed pointer/key input,
prefix search and pending menu identity before updating presentation. Programmatic
selection does not invoke a user callback. Managed-profile user selection retains
the original callback: it clears deletion confirmation/new-profile workspace
intent, fills the name editor, selects the profile in Rust form state and refreshes
controls/status. Workspace selection remains unsaved until the existing form's
Save/Create action. Reopening workspace settings selects the active workspace's
actual profile as before.

Each native menu now owns a snapshot of keys and its opening's generation.
Enabled/activation callbacks pass those captured values to Rust, rather than
reading a newer menu token or applying an old numeric index to a new model. Close
callbacks also carry the captured generation: a delayed old close cannot collapse
or reset a new popup. This hardening also applies to the import source selector.
Invalid test-selection indices are rejected before opening any Rust menu state.

Internal getters expose common View nodes and optional selection/count helpers;
explicit test setters preserve callback-free native SetSelectedIndex semantics,
while a separate activation helper dispatches the real selection callback for
either backend. Existing browser tests were adapted without retaining hidden
native Comboboxes in the opt-in UI. Scene-text and surface diagnostics now include
both profile selectors.

**148 Rust and 61 Python tests passed** without windows, with scoped GN and
boundary checks, optimized app/test compilation and a final no-work Ninja check.
Eight new Rust cases exercise stable IDs with equal labels/reorder/rename,
new-profile selection and deletion/empty fallbacks, invalid-key rejection,
replaced-model command rejection, old-close/new-popup ordering, prefix reset,
same-value single admission and Unicode normalized search.
`ProfilePickerScenesStableIDsPrefixAndModelCancellation` was compiled only. It
covers both Metal selectors, no native Combobox/Labels, optional selected index
and AX value, workspace reopen reset, profile creation/rename, deletion-confirmation
cancellation, committed prefix input/name prefilling, disabled activation,
model-change capture cancellation and hide/reopen behavior. Existing profile CRUD,
workspace/profile assignment, menu routing, failed-write and import cases remain
in the deferred matrix: **70 Metal cases and 34 Views controls**.

No application/windowed test was launched, packaged or signed. Actual popup
selection/cancel/staleness/destruction, keyboard/IME/VoiceOver and focus return,
profile CRUD/workspace persistence and isolation, Metal text/arrow output,
truncation/narrow bounds, RTL/theme/scale/clipping and native fallback await the
checkpoint. GPU quad/shader ABI and binary dependencies are unchanged; the CPU
Rust/C++ selector bridge was extended for stable keys and model lifecycle.
CPU/GPU/actual presentation still need a separate comparison. See
[profile selector verification](benchmarks/rust-profile-pickers-headless-2026-10-23.json).


## Full automated UI checkpoint (2026-10-23)

UI source `c902e77f4983932ddb89b2cc1f531ffe9f21bd81` passed the complete compiled automated
suite: **116 Metal cases and 86 Views cases**, with six explicit opt-in skips per
backend and no failures/retries. All 122 compiled tests were selected with Metal;
the applicable 92 common tests ran with Views, excluding the 30 dedicated Metal
cases. This covers and exceeds every previously deferred 70/34 case. Tests used
isolated temporary profiles, ran serially and enabled Metal API validation in the
Metal run. The normal app/profile was untouched.

The first full run exposed regressions beyond the earlier hand-selected matrix.
The repaired UI now uses a WebView-lifetime row token without creating dormant
WebContents, observes host-widget visibility for form canvases/input cancellation,
defers emoji persistence during IME composition, gives error alerts native alert
roles, and sends main-window plain keydowns/keyups through the normal NSWindow
responder chain. Main command equivalents retain AppKit menu handling; panel
command routing stays explicit. Native typing and renderer keyup after workspace
chords are checked together. New explicit fixture configuration replaces stale
onboarding assumptions, and navigation/focus/closed-settings/Metal-transition
checks synchronize to the behavior they assert. Wrapped paragraph diagnostics
require all shaped fragments of the logical value in the submitted scene.

Four performance scenarios, the opt-in real Arc import and the manual one-hour
harness were skipped per backend and are not counted as passes. **148 Rust and
61 Python tests**, scoped GN/boundaries, optimized app/test build and a no-work
Ninja check also passed. Source hashes, compiled UUIDs, all individual outcomes
and the failed initial/intermediate runs are in
[the checkpoint artifact](benchmarks/rust-ui-checkpoint-2026-10-23.json).

Earlier “GUI checkpoint pending” milestone notes describe their creation-time
status. This checkpoint supersedes that deferred automated matrix for the current
matching sources; it does not retroactively accept older source versions. Full
Views removal, native editor replacement, manual signed-app/IME-candidate/
VoiceOver/theme/scale/RTL acceptance and CPU/GPU/actual presentation comparisons
remain work. See [the remaining plan](rust-metal-next-steps-2026-10-23.md).


## Editor foundation and full UI repeat (2026-10-23)

UI source `4475ee78600cc38270943ead14b9289b2f84b999` adds Rust UTF-16 editing state,
a compiled CXX bridge and immutable CoreText geometry sharing the Metal font
helper. Live fields remain `views::Textfield`; the replacement native input view
and clipped GPU editor scene are the next implementation step. See
[the editor foundation](rust-metal-editor.md).

The matching optimized binaries passed **175 Rust / 61 Python tests**, the
window-free CoreText geometry/raster harness and the full serial automated GUI
repeat: **116 Metal / 86 Views passes**, six explicit opt-in skips per backend,
zero failures/retries, Metal API validation enabled. Temporary fixture profiles
were used; no manual signed-app or physical-presentation benchmark was performed.
[Source hashes, binary UUIDs and test results](benchmarks/rust-editor-foundation-checkpoint-2026-10-23.json).


## Live workspace/profile name fields (2026-10-23)

UI source `c27d3e66f28842fff10e41999bd1dc121a8f2563` connects the replacement editor to the
real workspace/profile name forms. Rust owns UTF-16 editing, history, composition,
viewport and scene; CoreText supplies matching geometry/drawing; Metal supplies
presentation. Native View/input/focus/clipboard/menu/dictionary/drag/AX adapters
remain explicit. The Views baseline keeps one native Textfield per field.

**182 Rust / 61 Python tests**, CoreText raster/geometry and the complete automated
UI repeat passed: **119 Metal / 87 Views**, six opt-in skips each, no retries or
failures, isolated profiles, Metal API validation enabled. The new cases cover
native typing/undo/mouse routing, composition, long text, persistence, admission,
asynchronous fake clipboard, AX, stale/atomic drag/drop and CPU fallback ink.
[Report and exact source/binary hashes](benchmarks/rust-name-editors-checkpoint-2026-10-23.json).
[Remaining editor work](rust-metal-editor.md) includes uncommon commands, full AX/
IME/RTL/scale/drag acceptance, remaining fields and actual presentation profiling.


## macOS editor commands and direction (2026-10-23)

UI source `8633a64f84b5b09ac7cb5a39307c8fa08044cf27` adds native-compatible selection
Retain/Extend/Caret and undirected state, page/paragraph/document commands, Rust
kill/yank and transpose, native Ctrl+T/K/Y checks, RTL/mixed visual character
motion and forced-direction AX metadata. CoreText geometry and raster share
paragraph direction/cache identity and normalized RTL whitespace origins; hit
testing is logarithmic with stable affinity. Rust and Views kill buffers remain
separate during the mixed transition and must be unified for full interoperability.

**201 Rust / 61 Python**, 45 native line-direction configurations/raster checks
and full **121 Metal / 89 Views** passed, six explicit opt-in skips each, no
failures/retries, isolated profiles and Metal API validation. Manual acceptance,
other editor fields, drag/edge scrolling/complex bidi/native menu services and
actual CPU/GPU/presentation comparison remain. The native Mac baseline disables
mark/unselect; Windows-only AX offset arrays are not a presumed macOS feature gap.
[Matching report](benchmarks/rust-editor-commands-checkpoint-2026-10-23.json).


## Shared native/Rust kill buffer (2026-10-23)

UI source `b17b87a02f689e51daf3245fd7cb8c14f80f333a` replaces the transitional Rust
kill store with the existing Chromium Views buffer. The adapter publishes validated
pre-edit ranges from its current UTF-16 cache before refreshing; no hidden native
editor/model or extra Rust fragment allocation is introduced. Cocoa Ctrl+K/Y
interoperability between workspace names and the native color field passes in both
directions. Both build installation paths apply a guarded, reversible fork hook.

**203 Rust / 66 clean-archive Python tests** and the full independent rerun of
**122 Metal / 90 Views** passed, six explicit skips each. The first full run had
one viewport-test process timeout with a stack in AppKit/FrontBoard teardown;
Views passed. The report retains it alongside the identical-binary successful
rerun. No performance gain or full UI parity is claimed. Word/paragraph drag,
edge-selection scrolling, remaining editors/forms/service panels/container
ownership and manual/performance/release acceptance remain open.
[Matching report](benchmarks/rust-shared-kill-checkpoint-2026-10-23.json).


## Rust pointer selection and edge scrolling (2026-10-23)

UI source `5e236d2b634168c0f42a0b03f750e853446b177c` moves word-range expansion and double-click drag
retention/reversal into the Rust editor, with fixed-size revision-bound pointer
sessions. A native 100ms timer drives continuous selection beyond horizontal
edges, including without new motion events; release/capture/focus/context changes
stop it. Triple-click behavior follows the existing single-line Views control.

**211 Rust / 66 committed Python tests** and the complete **124 Metal / 92 Views**
inventory passed, six explicit skips each. Shared synthetic MouseEvents compare
real controls; actual Cocoa multi-click/vertical pointer/complex bidi and manual
IME/VoiceOver acceptance remain open. The initial focused gate had a mistaken
SELECT_WORD admission expectation in both backends; the corrected final gate and
full suite passed. Remaining fields, forms, settings/services, Views containers,
manual release acceptance and actual presentation profiling are still required.
[Matching report](benchmarks/rust-editor-pointer-checkpoint-2026-10-23.json).


## Rust color/emoji editing and committed input (2026-10-23)

UI source `488faf5d7df2ef02f2a4144ef8546c336d7a650c` moves workspace color and emoji to the shared Rust
editor with CoreText/Metal drawing. The Views control backend retains one native
Textfield per field. Explicit committed-edit/key callbacks keep color validation
on Return and emoji first-grapheme autosave in the existing domain service, while
preedit/cancellation/programmatic updates are silent. Reentrant normalization,
callback detachment, actual Cocoa typing/Return, synthetic IME and workspace
isolation have shared regressions; preset-row and persistence/theme cases remain.

The complete inventory passed **126 Metal / 94 Views**, six explicit skips each,
with **211 Rust / 66 committed Python**. No new library or licensing change and
no presentation speedup claim. Folder/address and other fields, forms/preferences/
service panels, Views containers, manual platform/release checks and actual
CPU/GPU/drawable presentation profiling remain open.
[Matching report](benchmarks/rust-appearance-editors-checkpoint-2026-10-23.json).


## Rust folder-name editor in the modal host (2026-10-23)

UI source `84d032bf2f0d87c7196fe843347119c1cd8078d9` replaces the Metal folder form's Textfield with the
shared Rust/CoreText/Metal editor. A weak committed-edit callback updates the Rust
form's text/errors; submit retains captured-target validation and existing domain
save behavior. Native modal title/buttons/default action and focus adapter remain.
The original Views DialogModel Textfield stays intact as the control backend.

Actual Cocoa typing/Return, Unicode, synthetic IME cancel, old input-client lifetime
and default-prefilled modal reopen are covered, alongside existing folder/tree/menu/save cases.
Full **127 Metal / 95 Views**, six explicit skips each, **66 committed Python**
passed. Rust editor/scene sources match the preceding 211-case unit run; no repeat
Rust run or new geometry/performance claim. Address/settings/service fields and
broader container/form/manual/release/performance work remain.
[Matching report](benchmarks/rust-folder-editor-checkpoint-2026-10-23.json).


## Rust address/Command Center input (2026-10-23)

UI source `e17b856ce11e6d6994370998036b15f99e2f2c37` migrates palette editing to Rust/CoreText/Metal with
its original larger borderless presentation. Explicit commit/key callbacks retain
up/down selection, command/keyword/Return resolution, Escape, navigation and the
existing profile/history/preconnect services. Search timers/results exclude preedit
and stale text. Same-value insertion is a commit notification without a redundant
text revision/undo record, matching native UI refresh behavior.

Full **128 Metal / 96 Views**, six explicit skips each, **214 Rust / 66 committed
Python**, 61 standalone model and 180 SDK font/direction cases plus raster passed.
The initial Views synthetic composing-key fixture bypassed its OS IME filter and
was corrected to test the application policy seam; actual Cocoa input/navigation
remain covered and the failure is preserved. Real candidate windows, VoiceOver,
remaining fields/forms/services/containers and performance/release checks remain.
[Matching report](benchmarks/rust-address-editor-checkpoint-2026-10-23.json).


## Empty marked-text platform termination (2026-10-23)

UI source `a6d994b91dd92b42d34a540df6203ac6a69df930` interprets empty marked updates as active preedit
cancellation or idle no-ops in both backends. It suppresses native preedit callbacks,
protects idle selections from native empty-marker deletion and aligns composition
range queries for editable/readonly input. The shared test also verifies repeated
updates, later confirmation and saved emoji protection. No Rust model or shadow
native editing state was added; existing native selected-text cancellation behavior
remains distinct from Rust's atomic restoration.

Full **129 Metal / 97 Views**, six explicit skips each, **66 committed Python**,
GN/boundaries and optimized build passed. Rust/geometry sources match the preceding
214/61/180 unit/SDK evidence; no repeat of those unchanged checks. Broader native
termination, real candidate windows/VoiceOver, remaining UI, profiling and release
acceptance remain open. [Matching report](benchmarks/rust-ime-empty-marked-checkpoint-2026-10-23.json).

## Preferences editors checkpoint

UI source `425a23b0dc423b307fa95018e7772f841596ea2a` migrates preferences search, six Chromium preference text values and site origin to the shared Rust/Metal editor. Existing native preference/domain stores and profile/global scope remain. Actual Cocoa input/save, composing guards, draft refresh and profile reopening are covered in both backends. Full **130 Metal / 98 Views**, six explicit skips each; **66 clean-archive Python** tests and GN/boundaries passed. Rust model/scene/SDK sources are unchanged, so earlier 214/61/180 evidence is inherited, not rerun. Remaining service fields, settings controls, Views container ownership, signed/manual platform acceptance and CPU/GPU/presentation profiling remain open. [Report](benchmarks/rust-preference-editors-checkpoint-2026-10-23.json).

## Tools editors checkpoint

UI source `bb32e6f253953c5090a24bebf1d3ef8a382a74e9` moves seven tools text roles onto the shared Rust/Metal editor and removes WorkspaceShell TextfieldController ownership. Committed service queries survive pending history during marked text; form generation and workspace/profile identity protect deferred actions. Shortcut capture retains native accelerator policy with explicit text-input suppression while recording. The 17-case gate and full **133 Metal / 101 Views** inventory passed, six explicit skips each; **66 clean-archive Python** plus scoped GN/boundaries passed. Earlier Rust/model/SDK evidence is inherited for unchanged source. Separate page prompt, remaining form/control/container ownership, broader OS acceptance and CPU/GPU/presentation profiling remain open. [Report](benchmarks/rust-tools-editors-checkpoint-2026-10-23.json).

## Page prompt editor checkpoint

UI source `b59df50b21cba35e9dca66d16ba311548f812393` migrates JavaScript prompt input to Rust/Metal with native baseline font/theme services, generation-guarded keys and composing-aware acceptance. Frame/origin/profile ownership and renderer reply/cancellation semantics remain. The 30-case gate and complete **137 Metal / 105 Views** matrix passed, six explicit skips each; **66 clean-archive Python** and GN/boundaries passed. The initial fixture needed both pages navigated before cross-profile EvalJs; its failed logs are retained. Rust/model/SDK evidence is inherited for unchanged sources. Modal controls/containers, inherited-browser inventory, broader OS acceptance and actual presentation profiling remain open. [Report](benchmarks/rust-page-prompt-editor-checkpoint-2026-10-24.json).

## Preference selector checkpoint

UI source `651bc58cc631dda054d6209ef7c1bdf7e00b9747` moves eleven preference selectors to the shared
Rust state/layout and Metal scene path. The native Combobox baseline handles
blank-value keyboard input without a shadow model. Preferences resize their
scroll content after filtering, and Metal follows the clipped viewport. The
37/34-case gates, complete 140 Metal / 107 Views regular matrix and four opt-in
performance scenarios per backend passed (144/111 combined). Fresh Rust/SDK and
clean-archive Python checks passed. Controls, form/container ownership, platform
manual acceptance and real presentation profiling remain open.
[Evidence](benchmarks/rust-preference-selectors-checkpoint-2026-10-24.json).


## Preference switches and clear-data checkpoint

UI source `67bb21f11a769d47f2e6c0fcebfc707b84d84503` moves 24 preference switches and three clear-data
checkboxes to Rust checked/animation state and Metal display lists, retaining
native event/focus/AX adapters and original browser/domain commands. Reversible
slide/hover animation, reduced motion, clipped offscreen/hide handling, CPU
fallback repaint, global/profile scope and real clear-data confirmation are
preserved. The native track sits in an expanded target for focus/hover drawing.

Full automated results: 142 Metal / 108 Views regular cases plus four optional
performance scenarios each (146/112 combined); 220 Rust, 180 SDK font/direction
cases plus raster and 66 clean-archive Python cases passed. The initial native Button checked-AX
regression, failed reopening probe and final model-owned override are retained. Native nodes, remaining form/
container/service UI, manual acceptance, final packaging and real presentation
profiling remain open. [Evidence](benchmarks/rust-preference-switches-checkpoint-2026-10-24.json).


## Preference actions checkpoint

UI source `bab9feb8152e02917e5aa23d2585bd8a5db4ef74` moves 19 fixed preference actions and dynamic rule
removal/pagination to the existing Rust action geometry/input/primitive path with
Metal text and CPU fallback. Native Button/View/focus/AX/event nodes and browser
stores/services remain explicit adapters. Intrinsic row measurement, original
editor visibility, IME/disabled admission, busy labels, profile-bound deferred
commands and hide/offscreen cancellation are covered.

The critical gate passed 8 Metal / 7 Views; the full regular matrix passed
145/110 plus four optional default-30-trial scenarios per backend (149/114
combined). Fresh Rust/SDK and clean-archive Python checks passed. Initial failed
layout/inventory probes are retained. Remaining navigation/chrome/form/native
container ownership, other service UI, manual/release acceptance and controlled
CPU/GPU/presentation profiling remain open.
[Evidence](benchmarks/rust-preference-actions-checkpoint-2026-10-24.json).


## Preference navigation and controller checkpoint

UI source `84bbc52875079a96d2cc6750e5f9b328bcb11cbc` migrates ten category controls (one Metal canvas),
header dismiss and category/filter UI state to Rust. Original normalized UTF-16
search semantics, compact-choice synchronization and browser/profile service
admission remain intact. Native event/focus/AX, typography/normalization and
placement remain adapters. Analytic Rust category/dismiss symbols share the
clipped CPU/GPU recipe; exact native vector-icon pixel parity is not claimed.

Tests passed: 14 Metal / 10 Views critical cases; 147/111 regular cases plus four
optional default-30-trial scenarios each (151/115 combined); 223 Rust, 180 SDK
font/direction cases plus raster, 66 clean-archive Python, GN/boundaries and an
optimized build with final Ninja no-op. Normal installed apps/profiles were not
touched. Remaining text/chrome/data/draft/form and native container ownership,
other service UI, manual/native reattachment/release acceptance and real
CPU/GPU/presentation profiling remain open.
[Evidence](benchmarks/rust-preference-navigation-checkpoint-2026-10-24.json).


## Preference shared chrome checkpoint

Preference branding/header/section/row/status copy and backing now render from
Rust recipes in one additional transparent Metal underlay. Interactive holes are
subtracted from clips, and the underlay is ordered below existing control
canvases. Native layout/static-text AX and font/wrapping metrics remain adapters;
no native Label remains in this Rust preference tree. Navigation CPU backdrop
painting and row/text fallback remain available.

The critical gate passed 16 Metal / 11 Views, including native Label geometry
oracles, dynamic Unicode text, themes, filtering, scroll, cutouts and ordering.
The final full regular matrix passed 149/112 plus four optional scenarios each
(153/116 combined). The initial Views matrix had one teardown timeout; isolated
and full Views reruns passed without code changes. The initial failure is retained.
Checks passed 225 Rust tests, 180 SDK cases plus raster,
66 clean-archive Python tests, GN/boundaries and final Ninja no-op. No controlled
performance gain or complete UI parity is claimed. Form/data state, other
surfaces/native containers, manual acceptance, profiling and release remain.
[Evidence](benchmarks/rust-preference-chrome-checkpoint-2026-10-24.json).


## Preference draft and pagination controller checkpoint

Rust PreferencesForm now owns dirty-key and opaque-profile binding, text-refresh
replacement decisions, text-save UI admission and site-rule pagination. Actual
UTF-16 drafts live in the shared Rust editor. Failed saves retain dirty state;
successful saves release only that key. Ordinary refresh preserves unsaved and
composing text; managed values and profile changes override it as in Views.
Missing values disable fields without deleting drafts. Profile changes clear
dirty keys and reset paging; same-profile refresh preserves both. Committed search edits reset
the limit to 50; More advances by 50 with saturation. Detach clears profile/draft
binding, retaining category selection; native reattachment acceptance is open.
Native stores, authorization, validation and deferred domain mutations remain
adapters. This adds no dependency or license obligation.

The preference gate passed 17 Metal / 12 Views. A new shared runtime case covers
independent drafts, failed saves, normalized success, external pref changes,
active IME and profile changes. An initial Metal probe edited invisible fields;
the corrected test reveals fields through search first, preserving all draft/
save/profile assertions. The failed probe remains recorded. Full regular
execution passed 150/113 plus four optional cases each (154/117 combined).
Checks passed 228 Rust tests, 180 SDK cases plus raster, 66 clean-archive Python,
GN/boundaries and final Ninja no-op. UI descriptions/data, remaining busy/status/
form coordination, other panels/native containers, manual acceptance, real
profiling and release/license checks remain open.
[Evidence](benchmarks/rust-preference-drafts-checkpoint-2026-10-25.json).


## Tools selector checkpoint

Five tools selectors now use the existing Rust selection/prefix/menu-token
controller and shared Metal/CPU recipe: snapshot, closed-tab, shortcut-action,
provider-editor and history-clear period. Timeline models carry stable item IDs,
reset to index zero on refresh and retain the original disabled empty item/AX
value. Shortcut callbacks update the chord and stop recording; provider callbacks
populate all three fields; history-clear selection still reaches the native
confirmation/service path. Rebuild/profile/workspace changes cancel selector
interaction, retaining existing deferred-command identity guards.

Tools contents use preferred sizing. The Rust backend synchronizes scrolling
and updates GPU viewport clips; native scroll/layout, menu/IME/focus/AX and
browser service adapters remain. Native Views retains actual Combobox controls.
No new Rust dependency or license was introduced.

The critical gate passed 14 Metal / 13 Views. New tests cover real Cocoa prefix
keys, recorder/provider/history callbacks, duplicate-name snapshot identity,
filter reset/empty recovery and all five childless GPU selectors. Metal coverage
checks clipped screen bounds, offscreen hide/reappearance, window hide/show,
resize/theme and reconstruction. Two initial offscreen-geometry probes failed
to establish enough scroll distance;
the final fixture adds test-only scroll space after the real controls, retaining
full hide/reappearance assertions. All probes remain recorded.
Full regular execution passed 153/115 plus four optional scenarios each
(157/119 combined); Rust 228, SDK 180 plus raster, clean
Python 66, GN/boundaries and final Ninja no-op passed. Tools copy/actions/form
state, result lists, other panels/native containers, manual platform acceptance,
controlled profiling and release/license checks remain open.
[Evidence](benchmarks/rust-tools-choices-checkpoint-2026-10-25.json).


## Tools action and dismiss checkpoint

Fourteen fixed actions across Time Machine, shortcuts, provider editing and
history now use Rust pointer/key input, button/group geometry and the shared
Metal/CPU primitive recipe. Adjacent actions share 4/1/1 groups across the first
three pages; history clear/more use two standalone groups. Tools dismiss reuses
the existing analytic close recipe/canvas. Native Button/event/focus/AX, native
menu/IME and browser-service/confirmation adapters remain; Views retains its
native button hierarchy. Pixel-identical native close artwork is not claimed.

Individual offscreen children cancel pressed input even while their shared group
remains visible. Root rebuild/profile/workspace/hide cancellation is retained.
History busy text uses native/scene dispatch and survives reopening history
while an operation is active. Hiding More also hides its scene group, avoiding
an extra empty row. Native generation/profile/workspace guards and IME/value/
confirmation admission remain unchanged. This introduces no dependency.

New Cocoa Space tests save one named Unicode snapshot, save/default/delete a
provider and dismiss tools. The existing real 205-row history test now presents,
scrolls and uses Space to load beyond 200 rows, retaining stale-result checks.
Dedicated Metal checks cover all fourteen childless actions, batching, scene
text, clipped bounds and partial-group press cancellation with a fixture-only
trailing spacer. Busy labels, hide and reconstruction also pass. Initial shared
save probes counted total snapshots and failed on both backends when a background
restore point also appeared. Final acceptance checks exactly one newly named
snapshot and its ID; the failed probes/diagnostics remain in the report.
A separate Views presentation-wait failure reproduced in isolation. Final
readiness focuses the actual button, checks visible/nonempty compositor state
and invalidates real contents before requiring successful presentation; the
platform-level root cause is not fully established.

Tests passed: 17 Metal / 15 Views critical cases, 156/117 regular plus four
optional scenarios each (160/121 combined), 228 Rust, 180 SDK cases plus raster,
66 clean-archive Python, GN/boundaries and final Ninja no-op. Tools text/copy,
form/controller coordination, result lists, other panels/native containers,
manual platform acceptance, actual profiling and release/license checks remain.
[Evidence](benchmarks/rust-tools-actions-checkpoint-2026-10-25.json).


## Tools page data and shared text checkpoint

Four Rust page-copy descriptors supply titles/intros, field/picker names,
headings/action captions, history ranges and initial status; Rust also supplies
clear-busy text. Native callback IDs, stores, validation, IME and profile/
workspace/generation checks remain. One additional transparent Metal underlay
renders tools headers, descriptions, status, sleeping descriptions and empty/
error history copy. Control/native-history-result rectangles are holes, and
Cocoa ordering keeps the underlay below foreground canvases. No native Label
remains outside controls in the Rust tools tree; native result labels remain.

The shared preference/tools static-text adapter uses standalone cached
RenderText line boundaries matching Label's cursor-disabled, unelided,
truncated-long-word/minimum-line-height policy. Rust places the lines and
CoreText/Metal draws them. It creates no hidden measuring Label. An initial
native oracle found 300px versus 320px at width 80 for two long intros; the
approximate wrapping was replaced and the unchanged native requirement passed.
The failed probe and intermediate compile correction are retained in the report.

New shared tests compare four page titles/descriptions and Unicode multiline
status at widths 80/160/320, with reconstruction and profile-context changes.
Metal checks cover static AX nodes, scene text, underlay ordering, editor/
selector/dismiss/native-result cutouts, scrolling, theme, hide/reopen and history
error text. Shared preference-chrome tests remain in the critical gate.

Tests passed: 21 Metal / 17 Views critical cases, 158/118 regular plus four
optional scenarios each (162/122 combined), 229 Rust, 180 SDK cases plus raster,
66 clean-archive Python, GN/boundaries and final Ninja no-op. CPU recipe fallback
and native background/interaction/layout remain. Tools form/status decisions,
some popup/empty-item/service wording, native result rows, other panels/
containers, manual acceptance, real profiling and release/license checks remain.
No new dependency was introduced; RenderText already belongs to Chromium.
[Evidence](benchmarks/rust-tools-chrome-checkpoint-2026-10-25.json).


## Tools controller checkpoint

Rust ToolsForm owns page selection, shortcut recording, committed history and
Time Machine filter queries, history pagination and loaded state in the opt-in
backend. Native Unicode services trim/lowercase committed UTF-16; IME preedit does
not replace the committed query. New requests reset to 200; More adds 200 with
saturation. Edits/requests clear loaded state. Matching query/limit responses,
including errors, mark loaded. Native visibility/current-profile checks run
first. Page reconstruction resets transient queries/recording/loaded state.
Native timers, accelerator policy, editor read-only/IME handling, domain stores/
operations and posted-action generation/workspace/profile guards remain.

The shared browser test covers completed errors, wrong query/limit/profile,
Unicode edits during debounce, whitespace normalization, valid results, hidden
page rejection, page clamping and reconstruction. Existing real 205-row paging,
committed-query/preedit, timeline stable-ID/filtering, Cocoa recording and Metal
rendering tests remain covered. Tests passed: 22/18 critical, 159/119 regular
Metal/Views, six explicit skips each, 232 Rust, 180 SDK plus raster, 66 clean-
archive Python, GN/boundaries and final Ninja no-op. Optional performance cases
were not rerun here; the previous report retains that earlier coverage.
No new dependency or rendering recipe was introduced, and no controlled speedup
is claimed. Native result rows, action/status decisions, busy state, remaining
forms/service panels/containers, manual platform acceptance, profiling and
release/license checks remain open. [Evidence](benchmarks/rust-tools-form-checkpoint-2026-10-25.json).


## History result presentation checkpoint

History result title/date/URL text, hover backing and focus recipes now use Rust
on the existing shared tools Metal underlay. Rows create no Label children or
per-row drawables. Rust column layout preserves 60px rows and 4px spacing. The
native 36px text lead, measured/capped primary width, 10px gap, tail title and
middle date/URL elision remain; native font/time formatting and event/focus/AX
nodes remain adapters. Native GURL/profile-bound posted navigation still opens
a new tab and restores page focus. Views retains RowButton/Label/BoxLayout.
CPU fallback consumes the same Rust recipe with font sizes, title/detail elision
and focus drawing while the shared Metal underlay is absent.

The collector admits migrated rows before native Button cutouts. Only projected
visible rows enter the scene; other interactive controls remain holes. Paint,
bounds, input, theme and scrolling schedule updates. Offscreen/hidden rows and
widget hide cancel pending input. Shared tests seed real history, check Unicode
and empty-title URL fallback, AX/row geometry and real Cocoa Space navigation.
A Metal test uses 80 rows for scene/order/cutouts, culling/scroll, offscreen/hide
press cancellation, rebuild, dark theme and profile isolation. The earlier
shared-chrome test now requires actual scene row text and childlessness rather
than the former native-row cutout. Other requirements remain.

Tests passed: 24/19 critical and 161/120 regular Metal/Views, six explicit skips
each, 233 Rust, 180 SDK plus raster, 66 clean-archive Python, GN/boundaries and
final Ninja no-op. The initial bool/WeakPtr callback binding compile error was
corrected with an explicit owner-check lambda; its log remains. The initial GPU probe exposed offscreen-release navigation; explicit scroll
synchronization and release visibility guards fixed it without relaxing the
no-navigation assertion. The shared test now preserves native tooltip-URL AX
naming and checks two native Labels separately from the native focus-ring node.
Migrated rows disable that ring and use Rust focus drawing, including CPU
fallback. Failed probe identities/logs remain. No dependency was added. No controlled speedup is claimed. Native row interaction/list/scroll
and outer ownership, remaining forms/service UI, manual acceptance, profiling
and release/license checks remain. [Evidence](benchmarks/rust-history-rows-checkpoint-2026-10-25.json).


## Tools command and result policy checkpoint

Rust ToolsForm chooses form command/IME admission, recorder transitions and
action outcomes from UI command IDs plus field/composition/selection facts.
Native widget visibility, posted workspace/profile/generation admission, actual
object/index bounds, domain validation, stores and browser-service commands
remain. Snapshot save, shortcut save/disable and provider composition gates
match Views. Record cancels preedit natively before readonly; other actions
cancel recording as before. Rust decides failed/done/pending copy and form
reconstruction. Native pending status remains queried after reconstruction;
successful sleep retains its early return. External service/disk errors remain
native notifications.

History empty/error/count/retry text and More visibility/enabled state now come
from Rust. Nonempty errors retain rows but hide/disable More. Native shared
service busy state projects through Rust into history clear caption/enabled,
including reconstruction; preferences clear controls remain native. No rendering
recipe, dependency, store or service operation changed.

Three Rust cases cover command/context/composition, recorder/outcomes and
history/clear projections. A shared browser case checks failed real provider/
shortcut saves without mutation/reconstruction, Record during IME and Escape,
empty profile/filter/error/result status and More, including nonempty errors.
Tests passed: 25/20 critical and 162/121 regular Metal/Views, six explicit skips
each, 236 Rust, 180 SDK plus raster, 66 clean-archive Python, GN/boundaries and
final Ninja no-op. Earlier real Cocoa success, preedit/stale actions, paging/
clearing, busy reconstruction and scene/scroll contracts remain covered.
Remaining option-to-field/form construction, some service/progress wording,
other forms/panels, native event/AX/scroll/container ownership, manual acceptance,
profiling and release/license checks remain open. No controlled speedup is
claimed. [Evidence](benchmarks/rust-tools-policy-checkpoint-2026-10-25.json).


## Tools panel layout checkpoint

Rust allocates the tools outer panel, header, scrolling form body and history-
clear row. The native adapter supplies preferred child sizes, margins, visibility/
ignore flags and host insets; Rust owns natural/flex allocation, zero-size gap
handling, clipping and reported geometry. Shrink remeasurement retains the
initial flex budget. Roles preserve 20px outer interior/12px spacing, 8px header
and clear gaps, and body bottom/right 8px with 10px spacing. The scroll, title
and period selector are their respective flexible children. Views retains its
original start/stretch BoxLayout policy. This is a configured single-flex/no-
flex allocator, not all BoxLayout policies.

Three Rust cases cover allocation, shrink/clipping, zero children, empty lists,
margins and saturating sizes. A shared native BoxLayout oracle checks 1,008
bounded orientation/flex/visibility/margin/size combinations plus natural,
minimum and height-for-width, including wrapping, hidden/ignored children,
host borders and zero/narrow/odd bounds. A real-panel test cycles four pages at
two sizes with long Unicode status, checking header/scroll/footer geometry,
scroll extent and actual Metal ordering/editor/dismiss cutouts. Its initial
probe used the one-child outer contents wrapper instead of the three-child
panel, failing on both backends; locating the panel via the editor's scroll
ancestor preserved the original requirements. Production allocation code did
not change after that probe. Initial FFI placement/probe metadata compile
corrections and failed probe identities/logs remain in the report.

Tests passed: 27/22 critical and 164/123 regular Metal/Views, six explicit skips
each, 239 Rust, 180 SDK plus raster, 66 clean-archive Python, GN/boundaries and
final Ninja no-op. Frozen staging/source identities now explicitly include
sidebar_layout.cc/h. No rendering recipe, dependency or domain operation changed.
Native container/window/event/focus/AX/scroll and outer placement ownership,
other forms/panels/layouts, manual acceptance, profiling and release/license
checks remain open. No controlled speedup is claimed.
[Evidence](benchmarks/rust-tools-layout-checkpoint-2026-10-25.json).


## Page-dialog body checkpoint

Rust supplies alert/confirm/prompt/beforeunload display title/profile/message,
button captions and prompt/cancel/default metadata. Native frame/profile/window
validation, generation checks and browser callbacks remain authoritative.
The message cap stays 8192 UTF-16 units, preserving the original substring
boundary behavior; prompt initial/answer text remains independent in its editor.

Profile/message text now share one transparent Metal underlay per body using the
existing static-copy renderer. Native label fonts/colors, centered identity,
left multiline text, SizeToFit(440) preferred dimensions, 220px message scroll
cap and 480px/280px body preferences remain. No Label or hidden measuring Label
is created for these text roles. Native text/AX/layout adapters and Rust body
allocation remain. Native scrolling synchronizes scene clips; the underlay
stays below the prompt canvas and excludes its rectangle. Native dialog title/
buttons, modality and platform Return/Escape/default-cancel behavior remain.
Weak callbacks/scroll subscriptions are invalidated and the scene owner is
destroyed before the widget and prompt text client disappear.

Two Rust cases cover metadata, UTF-16 cap and center/left primitives. Shared
browser checks cover four dialog roles, metadata/AX, childlessness, native Label
preferred-size/height oracles and once-only closure. Metal checks cover scene
text/order, prompt cutout, start/end culling, real scrolling, theme changes,
CloseNow/client destruction and fresh reopen. Existing Cocoa IME, override,
stale-frame/navigation and beforeunload scope/default semantics remain in the
gate; shared tools/preferences adapter modes remain checked.

Tests passed: 16/13 critical and 166/124 regular Metal/Views, six explicit skips
each, 241 Rust, 180 SDK plus raster, 66 clean-archive Python, GN/boundaries and
final Ninja no-op. No dependency or domain operation changed; no controlled
speedup is claimed. Native title/buttons/modal container ownership, other forms/
panels, native interaction/window/AX/scroll, manual acceptance, profiling and
release/license checks remain. [Evidence](benchmarks/rust-page-dialog-scene-checkpoint-2026-10-25.json).

## Page-dialog action checkpoint

Page alert/confirm/prompt/beforeunload actions now use childless Rust/Metal
controls and Rust linked/trailing row placement. The page-dialog client replaces
the private native-button allocator; native font/theme/order/inset, focus/AX,
input protection, Escape and modal/close services remain explicit adapters.
Native GetOkButton/GetCancelButton are null for this custom client. Platform
default Return registration and prompt IME/generation authorization remain.
Dynamic label/enabled model changes rebuild the row and restore matching focus.
The native title/frame, modal containers and wider Views ownership remain open.

The full checkpoint passed **167 Metal / 125 Views**, six skips each, with
Metal validation and zero failures. The critical gate passed **17 / 14**;
**243 Rust**, **180 SDK font/direction plus raster**, **66 clean-archive Python**,
scoped GN/boundaries and optimized compilation passed. The new shared browser
case checks native button/row size oracles, order/AX/default state, childlessness,
Metal captions, model updates, blocked-input protection and actual Cocoa mouse/
Space activation. Existing Return/Escape/IME/beforeunload/lifetime cases pass.
Optional performance cases were not rerun; no controlled speedup is claimed.
[Evidence](benchmarks/rust-page-dialog-actions-checkpoint-2026-10-25.json).

## Folder and workspace modal action checkpoint

Folder create/rename, folder deletion/save-error and workspace deletion now reuse
the scene action client, Rust linked/trailing button rows and Rust outer-body
allocation, without DialogModel/BubbleDialogModelHost in their opt-in branches.
Native bubble frame/autosize/theme/scroll/focus/AX and unchanged domain services
remain adapters. The original model host is an independent size/margin/body-
rectangle oracle. Client-owned widgets close before their directly owned
modal delegates; PageDialogs now explicitly owns its delegate too.

A new Cocoa case first reproduced missing Escape delivery in both backends.
Installing the existing registered-accelerator route fixes folder deletion,
save errors and workspace notices while leaving macOS Cancel-default Return
unbound. The original failing probe and exact source snapshot are retained.
Another case verifies three composing editor CloseNow lifetimes, actual Return
on a one-button error, fresh reopen and shell Detach.

The full checkpoint passed **169 Metal / 126 Views**, six skips each, with
Metal validation and zero failures. The critical gate passed **33 / 25**;
**243 Rust**, **180 SDK font/direction plus raster**, **66 clean-archive Python**,
scoped GN/boundaries and optimized compilation passed. Optional performance
cases were not rerun; no controlled speedup is claimed. Native frames and
containers, other panels, manual acceptance, profiling and release/license
checks remain open. [Evidence](benchmarks/rust-scene-modals-checkpoint-2026-10-25.json).

## Download shared-scene checkpoint

Download title/source/status/empty text, progress and action controls now use
one shared PanelScene underlay, with Rust outer/list/row/action allocation.
There are no native or hidden Labels, MdTextButtons or ProgressBars for these
managed roles, and no per-row drawable. Common controls retain native typography,
color, event/focus/AX, window and scroll adapters. Existing page-dialog/prefs/tools
scenes keep their previous modes; Views retains its original controls.

Rust progress geometry matches native default rounding, RTL, flat track and
indeterminate keyframes. One panel timer runs only for projected visible unknown
progress and stops while hidden/offscreen/nonanimated. Matching CPU recipes remain
available. Direct widget/delegate ownership clears the scene and UI callbacks
before destruction; real transfers, status/list policy and safety checks remain
in Chromium. Actual Cocoa Escape now uses the existing registered-accelerator route.

The full checkpoint passed **171 Metal / 127 Views**, six skips each, with
Metal validation and zero failures. The critical gate passed **37 / 28**;
**245 Rust**, **180 SDK font/direction plus raster**, **66 clean-archive Python**,
scoped GN/boundaries and optimized compilation passed. New browser coverage
compares native text/action/progress metrics/AX on a real unknown-size transfer,
uses Cocoa Space pause/resume and Escape, verifies full-path filename tooltip
hit testing, then checks 80 real history rows, scroll culling, theme, close/reopen
and profile isolation. Optional performance
cases were not rerun; no controlled speedup or memory improvement is claimed.
Native nodes and remaining service panels, manual acceptance, actual profiling
and release/license checks remain open. [Evidence](benchmarks/rust-download-scene-checkpoint-2026-10-25.json).


## Password consent and account overview checkpoint (2026-10-25)

UI source `d4a9c5ff03ea1966a327a3715687d7b864a29e27`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 172 | 6 | 0 |
| Views | 128 | 6 | 0 |

The inventory contains 178 cases; Views excludes 44 dedicated Metal cases
(134 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The password/download/modal/folder/workspace/page-dialog/shared-chrome gate passed
**40 Metal / 31 Views**. Checks passed **245 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-password-scene-checkpoint-2026-10-25.json).

Password save/update consent now uses childless scene text, Rust panel allocation
and common Rust/Metal dialog actions. The saved-account overview uses scene text
for profile/empty/error/instruction copy and the existing Rust selector with Metal
closed-control drawing, native popup and prefix input. That selector retains its
own canvas; body and dialog actions also use separate surfaces. Native bubble/
window/frame/scroll/focus/AX/menu services remain adapters. Views retains its
original native controls and model host.

Only origin and username metadata is projected into scene/selector nodes. Password
values remain in Chromium FormManager/PasswordStore results. Native save/update,
decline/ignore, visible HTTP(S)/blocklist admission, autofill and profile-scoped
storage remain authoritative. PageServices now directly owns widgets/delegates,
destroys scenes before widgets and delegates after them, and invalidates pending
store/UI callbacks independently of stable Chromium client callbacks. Repeated
pending overview commands coalesce. Navigation, hidden pages and forced closes
release the UI and pending forms.

One initial Views run timed out in AppKit status-item scene dispatch during
PreRunTestOnMainThread, before the test body. The unchanged binary passed a targeted
recheck and then a complete Views repetition, without changing timeout limits or
assertions. The original result/stack remains in the evidence report. The startup
stall's underlying cause is not established or fixed; investigate it before final
native acceptance.

The existing real save/fill/update browser case now uses actual Cocoa Escape,
Space and Return after successful native presentation. A new shared case inserts
two real store accounts and checks AX metadata/secret exclusion, prefix input,
Escape, CloseNow/reopen, a read followed by hiding, empty personal-profile copy
and navigation dismissal. Existing native placement, actual autofill, hidden-page
cancellation, profile isolation and common UI regressions remain covered.

Native control/container ownership, remaining service panels and form coordination,
manual platform acceptance, controlled profiling and final release/license checks
remain open. Normal installed apps/profiles were untouched; no bundle was signed
and no dependency was added. The prior download checkpoint remains documented in
[its evidence report](benchmarks/rust-download-scene-checkpoint-2026-10-25.json).


## Permission and capture scene checkpoint (2026-10-26)

UI source `5b3eff639e405e703b92d6b48003686e59870dd4`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 173 | 6 | 0 |
| Views | 129 | 6 | 0 |

The inventory contains 179 cases; Views excludes 44 dedicated Metal cases
(135 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The media/password/download/modal/folder/workspace/page-dialog/shared-chrome gate
passed **48 Metal / 39 Views**. Checks passed **245 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-media-scene-checkpoint-2026-10-26.json).

Permission consent now uses shared scene paragraphs, Rust panel allocation and
common Rust/Metal dialog actions. Original requesting/permission/embedding origins,
capability strings, profile metadata, temporary-grant eligibility, default Cancel
and tab-switch policy remain native inputs. Chromium PermissionRequestManager,
device/OS checks, stores and State::Decide remain authoritative; rendering never
grants a request. Views keeps its original DialogModelHost and controls.

Capture controls also use Rust/Metal paragraphs/actions and body allocation. The
common scene bubble now supports the original nonmodal mode: native close button,
fixed width, no close-on-deactivate and no modal scroll wrapper. Modal mode retains
its existing scroll workaround and margins for passwords/folders/workspaces.
Native titles/frames, window/focus/AX/event services and the close affordance remain
adapters. Both backends use the existing registered Cocoa dialog key routing.

A client-owned adapter bundle destroys its scene before closing and defers widget/
delegate destruction off the executing decision/button callback stack. Permission
decisions and native Stop remain synchronous. Capture callbacks retain their weak
page target and active generation token; programmatic refresh/close disarms Stop
first, and old close callbacks cannot clear a newer indicator.

The initial media gate is retained: the new case inspected native frame title
Labels outside the managed body and could close capture before the page installed
ended listeners. Only the new test changed: body-only inspection, a page-running
precondition before stopping and a stricter stage-reporting timeout. Production
source was unchanged after that probe; the corrected gates and full matrices are
recorded separately.

A new shared browser case verifies default Cancel and unbound macOS Return, actual
Cocoa Escape denial and Space allow after successful presentation, visible origin
text, childless controls, nonmodal close affordance, forced close ending synthetic
tracks, fresh same-origin capture and Space stop. The original combined camera/
microphone/profile/one-time-grant test now also verifies the capture scene remains
visible across workspace switching and stops it using Cocoa Space. Existing
navigation teardown, cross-origin attribution, desktop picker cancel/filter/
document lifetime and OS-denial regressions remain covered. Devices are synthetic;
the permission manager and UI are real.

Native control/container ownership, remaining permission copy/coordination, other
service panels and forms, manual platform acceptance, controlled profiling and
final release/license checks remain open. Normal installed apps/profiles were
untouched; no bundle was signed and no dependency was added. The earlier AppKit
startup timeout remains recorded in the [password checkpoint](benchmarks/rust-password-scene-checkpoint-2026-10-25.json);
its underlying cause is not established or fixed.


## Tools timeline projection checkpoint (2026-10-26)

UI source `3d5131c5e9887fdedc0e56ec2100bdd0c73338df`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 175 | 6 | 0 |
| Views | 130 | 6 | 0 |

The inventory contains 181 cases; Views excludes 45 dedicated Metal cases
(136 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The tools/timeline/Time-Machine/restored-tab/media/password/download/modal/shared-
chrome gate passed **65 Metal / 52 Views**. Checks passed **249 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-tools-timeline-checkpoint-2026-10-26.json).

Rust ToolsForm now owns snapshot/closed-tab filtering, title/em-dash/detail label
projection, empty copy, enabled state and current control-key-to-opaque-ID maps.
The opt-in path no longer builds native SimpleComboboxModel items or fills the
parallel C++ snapshot_ids_/closed_ids_ arrays. Original source order and reset-to-
first selection remain. SceneSelect retains Rust prefix/menu/input state and
Metal drawing, with native metric/event/focus/AX/menu adapters.

ICU normalization stays native. Rust matches raw UTF-16 units against the original
title/space/detail search text, preserving partial surrogates, dotted I and other
Unicode behavior; it does not search the rendered em dash or trim input. Display
metadata can include formatted URLs, but browser URL objects, stores, profile/
workspace validation and operations remain native.

Projection keys include group/epoch/index and are independent of display labels
and native IDs. Filtering, reprojection, reset and page changes invalidate old
keys; duplicate labels/IDs have distinct control keys and groups cannot borrow
one another's targets. Epoch exhaustion fails closed. Buttons capture a selected
key before posting, and Rust resolves that captured key at admission, so an older
click cannot operate on a newer filter's index-zero item. Native visibility,
workspace/profile/form-generation checks, beforeunload, persistence, validation
and rollback remain authoritative. Views retains its original arrays, filtering
and native model baseline.

Four Rust cases cover UTF-16/native search boundaries, stale/reordered/reset/page/
cross-group keys, independent lists/duplicate/empty IDs and epoch exhaustion.
A new shared browser case compares both lists with the original native ICU/filter/
label/AX oracle for Greek, dotted I, emoji, a partial surrogate, empty and no-match
queries, then uses actual Cocoa Space after successful presentation to reopen a
real closed tab and restore a real snapshot. A dedicated Metal case changes the
filter while deletion is queued, requires both targets to survive, and verifies a
fresh delete succeeds. Existing duplicate-title deletion/identity, empty/disabled/
hidden picker, tools action/editor/choice and native Time Machine tests also pass.

The first focused probe is retained. Its new helper treated empty InsertText as
clear-selection, which is a no-op; it now uses explicit range deletion. Integration
review also added the captured-key admission check and its stale-action case
before final validation. Compile diagnostics and intermediate source/binary
identities are retained separately from final passing runs.

Other tools/preferences form construction, service wording, native node/container
ownership, manual platform acceptance, controlled profiling and release/license
checks remain open. Normal installed apps/profiles were untouched; no bundle was
signed and no dependency was added. Prior permission/capture work remains in
[its checkpoint](benchmarks/rust-media-scene-checkpoint-2026-10-26.json). The earlier
AppKit startup stall remains recorded in the password checkpoint and is not claimed
fixed by this migration.


## Tools shortcut/provider choice forms checkpoint (2026-10-26)

UI source `6cee01c5513c88d49d10083aeee2c9bf4d1b361e`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 177 | 6 | 0 |
| Views | 131 | 6 | 0 |

The inventory contains 183 cases; Views excludes 46 dedicated Metal cases
(137 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The tools/timeline/Time-Machine/restored-tab/media/password/download/modal/shared-
chrome gate passed **67 Metal / 53 Views**; the focused selection/editor/
action gate passed **8 Metal / 6 Views**. Checks passed **255 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-tools-choice-forms-checkpoint-2026-10-26.json).

Rust ToolsForm now projects shortcut and search-provider choices and owns active
choice/draft resolution. Shortcut labels retain title/em-dash/native-formatted
chord. Provider labels retain keyword/name/default marking and explicit add-new
copy. Rust returns field plans for chord/name/keyword/template text and recording
cancellation. The native adapter applies those plans to the existing editors and
translates projected keys/labels into SceneSelect. The opt-in form no longer
formats native configuration into label lists or fills fields by native indices.
Views keeps its original index/model/field baseline.

Generation/page/role/active-choice checks invalidate stale drafts. Duplicate
labels/IDs have distinct control keys, malformed empty IDs fail closed and the
add-new provider is an explicit valid create draft. Exhaustion disables projection
without key reuse. Posted shortcut save/disable and provider save/default/delete
capture selection keys and must still match the active Rust choice at admission.
Switching selection cannot lend another row's fields or target to an older action.
Existing timeline captured-key admission remains unchanged.

Native shortcut parsing, reserved/system/editing keys, conflict checks, actual ID
lookup, provider HTTP(S)/template/uniqueness/limit/last-provider checks, key
registrations, configuration scope and persistence remain authoritative. Rust
receives native-formatted chords and UTF-16 display/draft metadata; URL templates
are text, not browser URL objects or navigation authority. Native visibility,
form/workspace/profile generation and IME guards still apply. Native configuration
vectors remain service data, while UI choice/field projection is Rust state.

Six Rust cases cover labels/chords/record cancellation, provider create/default/
field plans, stale/reset/page/role drafts, raw UTF-16/duplicate/empty IDs, epoch
exhaustion and current-choice command admission. A new shared browser case compares
every shortcut/provider label and selected field with the native formatter/config,
including duplicate Unicode names and default marking. A dedicated Metal case
changes provider/shortcut selection while delete/disable is queued, requires both
old/new targets to survive, then verifies fresh commands succeed. Existing actual
Cocoa prefix, recording/editor/provider and timeline regressions also pass.

Other form construction/status coordination, service wording, native node/container
ownership, manual platform acceptance, controlled profiling and release/license
checks remain open. Normal installed apps/profiles were untouched; no bundle was
signed and no dependency was added. Earlier timeline ownership and its initial
probe remain in [the timeline checkpoint](benchmarks/rust-tools-timeline-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.


## Tools form structure checkpoint (2026-10-26)

UI source `4ddd40f55bd9cebbab8b83bd5a3d76425b25ea9b`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 178 | 6 | 0 |
| Views | 132 | 6 | 0 |

The inventory contains 184 cases; Views excludes 46 dedicated Metal cases
(138 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The tools/timeline/Time-Machine/restored-tab/media/password/download/modal/shared-
chrome gate passed **68 Metal / 54 Views**; the focused form/selection/editor/
action gate passed **10 Metal / 8 Views**. Checks passed **260 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-tools-form-structure-checkpoint-2026-10-26.json).

Rust ToolsForm now describes the structure of all four tools pages: seven editor
fields, five choices, fourteen actions, headings/hints, semantic roles and their
order. Consecutive action groups and spacing, the history period/clear row and
flexible choice, history result-column spacing, busy clear state and initially
hidden more action are declared in Rust. Sleeping-workspace hint joining also
preserves raw UTF-16. The native adapter instantiates the plan using the existing
editor, selection and action factories; Views keeps its original form builder.

Form description is read-only: it cannot clear queries, cancel recording or
invalidate active choice keys/drafts. Existing native editor/IME callbacks, key
registrations, captured-key command admission, timers, context checks, browser
services and persistence remain authoritative. Existing Rust choice/timeline
projection and stale-action guards remain in place. No dependency was added.

Five Rust cases cover control/command order, balanced grouping/row topology and
spacing, history busy/hidden states, Unicode/raw UTF-16 and read-only description.
A shared browser case verifies the ordered accessible control names on every
page against the original UI and rebuilds history while clearing is busy. Existing
actual Cocoa editing/selection/actions, recorder, timeline and scene geometry/
clipping cases also pass in the same compiled inventory.

Other form/status/service coordination and native interaction/container ownership,
manual platform acceptance, controlled profiling and release/license checks stay
open. Normal installed apps/profiles were untouched; no bundle was signed.
[Previous selection/draft checkpoint](benchmarks/rust-tools-choice-forms-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.


## Preference catalogue checkpoint (2026-10-26)

UI source `6142576afad1b8367a7c11eba75b2c6e54f3f8a2`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 180 | 6 | 0 |
| Views | 134 | 6 | 0 |

The inventory contains 186 cases; Views excludes 46 dedicated Metal cases
(140 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The preference/tools/timeline/Time-Machine/restored-tab/media/password/download/
modal/shared-chrome gate passed **86 Metal / 68 Views**; the focused catalogue
and route gate passed **2 Metal / 2 Views**. Checks passed **264 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-preference-catalogue-checkpoint-2026-10-26.json).

Rust now describes all ten preference categories and the original 52-row macOS
catalogue. It owns row order, control kinds, registered UI preference keys,
titles/descriptions, numeric choices, editor placeholders, action roles and
save/open/directory accessibility copy. Site-editor type/policy labels and
clear-data periods/checkbox copy also come from Rust. A native capability fact
gates only the system-certificates row. The opt-in native builder consumes this
catalogue; Views retains the original reference row builder.

Native adapters retain actual preference values, admitted writes, managed-setting
checks, normalization, profile/global scope, browser/platform operations,
site-rule validation, data-clearing confirmation and persistence. Existing Rust
draft/filter/paging state and scene editor/selector/switch/checkbox/action
factories remain. Dynamic provider options still come from the native service.
Descriptors are UI data and never authorize arbitrary keys or platform actions.

Four Rust regressions cover counts/platform gating, exact key/kind mapping,
editor/action accessibility and numeric/site/period option ordering. A shared
browser case compares every original row title/detail/order in all ten categories,
checks site labels against native service constants and filters by description.
Another activates all four tools navigation roles and checks actual destination
controls and closed preferences. Existing native Cocoa editing/choice/action,
profile isolation, managed settings and scene cases also pass.

The first compile probe found a missing test-only preference-label accessor; a
bounds-checked accessor matching the tools helper was added. Its failed compile
log is retained in the report, separate from the final passing source/binary.
No dependency was added. Native compound child containers, event/focus/AX/menu/
scroll/window nodes, header/profile/status copy and remaining busy/service
coordination remain open, alongside other surfaces, manual acceptance, controlled
profiling and release/license checks. Normal installed apps/profiles were
untouched; no bundle was signed.
[Previous tools structure checkpoint](benchmarks/rust-tools-form-structure-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.


## Preference allocation checkpoint (2026-10-26)

UI source `efff9570e27cf98834b9fbb22c550552332b871d`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 182 | 6 | 0 |
| Views | 136 | 6 | 0 |

The inventory contains 188 cases; Views excludes 46 dedicated Metal cases
(142 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The preference/tools/timeline/Time-Machine/restored-tab/media/password/download/
modal/shared-chrome gate passed **88 Metal / 70 Views**; the focused geometry
gate passed **2 Metal / 2 Views**. Checks passed **267 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-preference-layout-checkpoint-2026-10-26.json).

Rust now allocates preference root/main/navigation/header/header-text/body/category
columns, all 52 catalogue row label columns and centered controls, plus the saved
site-rule list and centered dynamic rule rows. Native adapters provide control/font
measurement, visibility/ignored flags, margins, explicit preferred sizes and the
single flexible child. Views retains its reference BoxLayouts. Native scroll/
viewport, interaction/AX/window identity, responsive visibility and window placement
still remain adapters requiring further ownership migration.

A separate centered allocator preserves the shared stretch entry point, uses a
common cross axis and odd-size bias, handles asymmetric margins and remeasures
wrapped content after main-axis shrink. Zero/unbounded cross extent uses the
initial natural cross measurement. Native stores, command/context admission,
profile/global scope, deferred rule mutation, confirmation and persistence are
unchanged. Existing catalogue/filter/draft/paging state remains in Rust.

Three Rust cases cover centering/margins, flex remeasurement and fixed controls,
and empty/tiny/zero/saturated extents. A shared browser oracle compares actual
Chromium BoxLayout preferred/minimum/height-for-width/bounded measurements and
child rectangles for 1260 orientation/flex/visibility/ignored/margin/size
combinations. Another uses native preferred-size proxies for real controls and
compares visible layout managers/child rectangles across all ten categories and
all 52 catalogue rows at compact and wide native-window sizes. The explicit
external 212x1 navigation preferred size remains intact; its internal allocator
is checked when navigation is visible.

The initial geometry probe failed both Metal cases and one Views case. It exposed
zero-cross-size remeasurement in the new adapter and a test setup error comparing
the navigation external preferred-size override with its internal layout size.
The adapter now uses natural cross constraints and the oracle compares actual
layout-manager measurements. The original matrix/category/size coverage remains;
initial sources, binary identity and failed logs are retained separately from the
passing final binary. No failed probe is counted as a current pass.

No dependency was added. Remaining interaction/container/scroll/window ownership,
header/profile/status/managed copy and service coordination, other surfaces,
manual platform acceptance, controlled profiling and release/license checks stay
open. Normal installed apps/profiles were untouched; no bundle was signed.
[Previous catalogue checkpoint](benchmarks/rust-preference-catalogue-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.


## Tools viewport ownership checkpoint (2026-10-26)

UI source `d23f2326fea4902aedeee786585ab11c45558359`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 184 | 6 | 0 |
| Views | 138 | 6 | 0 |

The inventory contains 190 cases; Views excludes 46 dedicated Metal cases
(144 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The preference/tools/timeline/Time-Machine/restored-tab/media/password/download/
modal/shared-chrome gate passed **90 Metal / 72 Views**; the focused viewport
gate passed **2 Metal / 2 Views**. Checks passed **272 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-tools-viewport-checkpoint-2026-10-26.json).

This is the frozen UI-lab build of UI commit `d23f232`, with recorded baseline
bootstrap/network inputs. Later current-main network-policy changes are not
covered by this binary or its UI acceptance.

The opt-in tools panel now replaces Views ScrollView with a Rust-owned viewport
and a native clipping/event/window adapter. Rust owns viewport/content/bar
allocation, overlay versus reserved width, extent, authoritative offset and
maximum, resize/rebuild clamps and minimum-movement reveal policy. Native Views
mode keeps ScrollView through the same interface and its original paint path,
without an extra Metal refresh observer. The existing platform scrollbar
keeps appearance and wheel/trackpad/momentum/key/AX delivery; its position requests
are admitted and applied by Rust. Platform scrollbar and per-control View identity
remain explicit future ownership work, not migrated claims.

Native adapters supply preferred metrics and scrollbar style facts, translate
content, clip descendants and refresh the existing Metal control canvases. The
tools shell no longer owns ScrollView synchronization/subscription state. Browser
stores, profile/workspace/generation/captured-key admission, confirmation, timers,
form/catalogue/geometry/input state and persistence remain unchanged. Preferences
and other scroll surfaces remain native.

Five Rust cases cover overlay/reserved geometry, resize/rebuild clamp, native
minimum-movement and oversized reveal rules, fractional/nonfinite requests and
empty/tiny/extreme metrics. A shared browser oracle compares real ScrollView
extents, offsets and visible rectangles at four sizes with 20 position and 60
reveal requests, including wrapped content and transition to no overflow. Another
checks actual tools ownership, absence of ScrollView in the opt-in host, wheel/
Home/accessible page commands, clipping, editor reveal, AX range and reopen bounds.
Existing tools resize, history-row, action and selector clipping/capture cases
retain their semantic assertions through the viewport interface. Actual Cocoa
keyboard/IME and presentation-backed scene regressions also pass.

The first probe passed wheel/key/AX/reveal on both backends but failed one range
assertion after overflow disappeared. Chromium retains a hidden scrollbar's old
maximum; the effective live content/viewport range is zero. The oracle now checks
live extents and also the scrollbar maximum whenever visible. All four sizes and
80 requests remain. Initial source/binary identities and failed logs are retained
separately and are not counted as current passes.

No dependency was added. Preferences/other scrolling, platform scrollbar/event/
AX/window adapters, stable Rust interaction/node ownership, remaining chrome/
service coordination, manual acceptance, controlled profiling and release/license
checks remain open. Normal installed apps/profiles were untouched; no bundle was
signed. [Previous allocation checkpoint](benchmarks/rust-preference-layout-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.


## Preference viewport targeted stage (2026-10-26)

UI source `7896cf3c3b4a70696f6f8fec39bdafec071c476c` adds the Rust-owned viewport to
preferences. The opt-in preferences host no longer uses Views ScrollView; the
native baseline retains it. Category/filter reset remains ScrollTo(0), and theme
background updates preserve the existing palette and content opacity contract.
Native platform scrollbar/event/AX/window adapters and per-control View identity
remain. No browser store, profile/global scope or persistence policy changed.

Focused new tests passed **2 Metal / 2 Views**. Affected preference/control
regressions passed **27 Metal / 22 Views**, one launcher job and no
retries. Checks also passed 272 Rust cases, 180 SDK font/direction cases plus
raster, 68 live and 68 clean-archive Python cases, scoped GN/boundaries and final
Ninja no-op. Source/binary identities and the two earlier failing test probes are
retained in [the targeted report](benchmarks/rust-preference-viewport-targeted-2026-10-26.json).

Tests cover deterministic offscreen/reveal behavior, empty-filter collapse/reset,
compact/wide resize, wheel/AX commands, native theme-reset behavior with retained
query, profile-specific field values, existing actual Cocoa editor/IME, selectors,
actions, switches and scene clipping. The initial probes exposed incorrect test
assumptions about overflow/theme reset and stale hidden-native-scrollbar AX
metadata; production viewport behavior did not change for those corrections.

Per the updated user instruction, **full UI tests run only after a larger block**.
This stage uses targeted affected-case coverage. The previous full matrix below
belongs to UI source `d23f232` and is not counted as a full pass for this source.
The lab retains its recorded frozen bootstrap/network inputs; current-main network
policy changes are not part of this UI binary's acceptance. No performance or
full manual/release/license completion is claimed.

## Workspace and import viewport — targeted stage, 2026-10-26

UI source `2c305645b9ec18d82e8174da67df5ec7973fd862` moves workspace settings and import to the shared
Rust-owned viewport. Rust owns height clipping, wrapping-width reservation,
allocation, offset and reveal/clamp. Native measurement, platform scrollbar/input,
accessibility/window adapters and per-control View identity remain. The native
Views baseline retains its original ScrollView and bounded sizing policy.

Focused new tests passed **2 Metal / 2 Views**; affected regressions passed
**27 Metal / 20 Views**, single attempt, retry limit zero.
Checks passed **274 Rust**, **180 SDK font/direction cases plus raster**, **68 live
and 68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op.
All 53 committed/live/staged prototype inputs and binary/bootstrap/native-engine
identities were checked. See [the targeted report](benchmarks/rust-form-viewport-targeted-2026-10-26.json).

Both panels have coverage for actual successful compositor presentation,
offscreen/reveal, compact/wide wrapping, resize and shrink, wheel/AX commands and
retained draft text. Earlier fixture failures are retained separately: the native
settings opener requires a visible main widget, native scrolling is asynchronous,
and a fixed spacer inflated the import window minimum. The final responsive
spacer adds overflow at real layout widths without inflating its zero-width
minimum-size query. Production code was unchanged for those fixture corrections.

An earlier affected Views run had 19 passes and one successful-presentation wait
timeout. The final fixture explicitly schedules root paint before requesting the
frame; final focus and affected runs passed on both backends. Earlier results
remain in the report; elimination of all presentation flakiness is not established.

Full UI tests run **only after a larger block**, per user instruction. This stage
has targeted acceptance; the previous full matrix below belongs to UI source
`d23f232`. The lab retains frozen bootstrap/network inputs; current-main network
policy is outside this binary's acceptance. No controlled performance gain or
complete manual/release/license parity is claimed.

Previous targeted preference viewport acceptance is retained in
[its report](benchmarks/rust-preference-viewport-targeted-2026-10-26.json).

## Full service viewport checkpoint — 2026-10-26

UI source `bde6809f25e1499aa6241bfe7492c27a5e7e44f4` moves downloads, page JavaScript/beforeunload
message bodies and shared scene modal bodies to the Rust-owned viewport.
Together with tools, preferences, workspace settings and import, these now share
Rust extent/allocation, bounded height, wrapping-width reservation, offset and
reveal/clamp policy. Rust also owns overflow-edge geometry and visibility.
Native measurement, separator paint, platform scrollbar/events/AX, window/frame/
focus/IME and per-control View identity remain adapters.

The full compiled inventory has **196 cases**. Metal passed **190 regular
cases**, Views passed **144 regular cases**; each explicitly skipped the same
six optional manual/live-import/performance scenarios. One launcher job, retry
limit zero, no failures/crashes/timeouts. Dedicated Metal fixture cases are
excluded from the Views comparison. Focus passed **2 Metal / 2 Views**, affected
regressions **40 Metal / 33 Views**. Checks also passed **276 Rust**, **180 SDK
font/direction cases plus raster**, **68 live and 68 clean-archive Python**,
scoped GN/boundaries and final Ninja no-op. All 53 committed/live/staged prototype
inputs and binary/bootstrap/native-engine identities were checked.

[Full service viewport report](benchmarks/rust-service-viewport-checkpoint-2026-10-26.json)
retains exact commands, inventory/results, source hashes, UUIDs and log digests.
The lab retains its recorded frozen bootstrap/network inputs from `d23f232`;
current-main network policy is outside this binary's acceptance.

The native oracle checks bounded preferred size, wrapping, offsets and overflow
separators at multiple sizes and in both themes. Actual download/message scene
scrolling, clipping, Cocoa prompt/AX actions, theme changes, forced close,
editor lifetime and profile isolation remain covered. Modal bodies preserve the
native provider-specific height cap and 2px scroll workaround. Weak scene
callbacks replace ScrollView synchronization/subscriptions. No service, store,
policy/admission/validation or persistence behavior changed; no dependency added.

This is automated acceptance of a larger shared scrolling block, following the
instruction to reserve full UI matrices for larger additions. Complete native
control/node ownership, manual acceptance, controlled CPU/GPU/drawable-presentation
and memory comparison, release/license/notice verification remain open. No
controlled performance gain or complete functional parity is claimed.

Earlier targeted [preferences](benchmarks/rust-preference-viewport-targeted-2026-10-26.json)
and [workspace/import](benchmarks/rust-form-viewport-targeted-2026-10-26.json)
reports retain their historical fixture probes and acceptance scope.

## Service viewport capture — targeted follow-up, 2026-10-26

UI source `8df84b24fcdaed163a78b4a925d54dfac4bf0265` explicitly refreshes shared-action visible bounds
when a service viewport moves, then schedules the scene. Actions scrolled fully
offscreen cancel their pressed/capture state. Weak callback lifetime and existing
scene-before-widget teardown remain; no browser service or persistence changed.

The real 80-download regression presses the first Reveal action, verifies the
pressed state, scrolls it fully out of view and verifies cancellation without
executing a file/Finder action. Existing clipping, theme, forced close, reopen and
profile-isolation assertions remain. Focus passed **3 Metal / 2 Views**; affected
regressions passed **40 Metal / 33 Views**, single attempt, no retries. Checks
passed **276 Rust**, **180 SDK font/direction cases plus raster**, **68 live and
68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and binary/frozen-bootstrap/native-engine
identities were checked. [Targeted capture report](benchmarks/rust-service-capture-targeted-2026-10-26.json).

Per the user's cadence, this small follow-up has targeted acceptance. The full
matrix below belongs to source `bde6809`, before the explicit capture refresh and
new assertion; it is not counted as a full pass of this source. Frozen lab network
scope, platform adapters, manual/performance/release/license limitations remain.

## Shared viewport trackpad — targeted follow-up, 2026-10-26

UI source `c1218949b559690373981036b89316f159531080` corrects raw trackpad input in the shared Rust
viewport. ScrollBar::ObserveScrollEvent updates feedback, not content position;
the adapter now sends finite kScroll pixel deltas to Rust-owned offset/clamp
state. Fractional pixels and Cocoa momentum updates are retained. Fling-start
velocities are not treated as position deltas. Horizontal-only ScrollEvent and
MouseWheelEvent remain unconsumed by a vertical viewport; handled vertical raw
events are not applied again by Widget's synthesized wheel fallback.

A dedicated case sends fractional and momentum updates through the real
Widget::OnScrollEvent path after successful compositor presentation, then checks
axis routing, velocity rejection, both clamps and content collapse. Focus passed
**3 Metal / 2 Views**, affected regressions **41 Metal / 33 Views**, single attempt,
retry limit zero. Checks passed **278 Rust**, **180 SDK font/direction cases plus
raster**, **68 live and 68 clean-archive Python**, scoped GN/boundaries and final
Ninja no-op. All 53 committed/live/staged prototype inputs and binary/frozen-
bootstrap/native-engine identities were checked.
[Targeted trackpad report](benchmarks/rust-viewport-trackpad-targeted-2026-10-26.json).

This prerequisite was found while auditing the main sidebar. Its native
ScrollView has not yet been migrated. That stage must retain the existing custom
Rust scrollbar, fractional scene projection/native hit testing, track/thumb/key/
AX behavior, workspace swipe and animation contracts. Native control/node
ownership and final manual/performance/release/license acceptance remain open.

Per user cadence, this small correction has targeted acceptance; the earlier
full matrix below is not counted as a full pass of this source. Physical hardware
trackpad behavior still needs manual acceptance. The earlier
[capture cancellation report](benchmarks/rust-service-capture-targeted-2026-10-26.json)
retains the preceding follow-up's evidence. No dependency, service/store/admission
or persistence change; no controlled performance gain is claimed.

## Main sidebar viewport — full checkpoint, 2026-10-26

UI source `9037db7bb4caf3d187145e410224d6c791ca6fcc` moves the main tab/tree sidebar viewport to
Rust in the opt-in backend. Rust owns extent/allocation, fractional offset,
reveal and clamp. The existing Rust-backed sidebar scrollbar is injected into
the shared viewport, retaining thumb/track/key/AX state and Metal presentation.
Native ScrollView subscriptions/synchronization for this surface are removed.
Views retains its bounded ScrollView path. Native View/node identity, measurement,
input/focus/AX/window/frame and animation adapters remain.

The full compiled inventory has **198 cases**. Metal passed **192 regular
cases**, Views passed **145 regular cases**; both explicitly skipped the same
six optional manual/live-import/performance scenarios. One launcher job, retry
limit zero, no failures/crashes/timeouts. Dedicated Metal fixture cases are
excluded from Views. Focus passed **5 Metal / 2 Views**; affected regressions
**76 Metal / 29 Views**. Checks passed **278 Rust**, **180 SDK font/direction
cases plus raster**, **68 live and 68 clean-archive Python**, scoped GN/boundaries
and final Ninja no-op. All 53 committed/live/staged prototype inputs and binary/
frozen-bootstrap/native-engine identities were checked.
[Full main-sidebar viewport report](benchmarks/rust-main-viewport-checkpoint-2026-10-26.json).

The new shared case covers actual host ownership/AX, compact/wide bounds, bottom
scroll and first-row reveal, workspace shrink and return. The existing fractional
projection case now sends an actual ScrollEvent through the sidebar Widget,
then checks exact fractional clipping and native mouse hit testing. Existing
thumb/track/key/AX, real-tab wheel, folder/tree, live-tab/split, swipe/repeat and
presented-Metal/native-fallback animation contracts retain their assertions.

Chromium compositor mailbox diagnostics remain in both backend logs (4 Metal /
14 Views invalid-mailbox messages). The drag/width cases and full launchers
succeeded; this checkpoint does not diagnose or resolve those messages.

The initial probe passed all five Metal cases but exposed the native layer's
pre-commit resize limitation in the new Views test (live maximum 590, offset 430).
The final fixture requires successful presentation of resized geometry before
scrolling to the bottom. The failing probe is retained; no production behavior
was changed for that fixture correction.

This full source includes the earlier shared-viewport capture cancellation and
raw-trackpad corrections. Their historical
[capture](benchmarks/rust-service-capture-targeted-2026-10-26.json) and
[trackpad](benchmarks/rust-viewport-trackpad-targeted-2026-10-26.json) reports retain
their original scope. The lab retains frozen bootstrap/network inputs from
`d23f232`; current-main network policy is outside this binary's acceptance.

Footer workspace dots and palette suggestions still use native ScrollView.
These and remaining native control/node/container/menu/input ownership must
continue migrating. Manual hardware trackpad, IME candidates, VoiceOver, global
RTL, scale/theme and GPU recovery acceptance, controlled CPU/GPU/drawable-
presentation/input-latency/memory comparison and release/license/notice checks
remain open. No complete functional parity or controlled performance gain is
claimed. Full UI matrices remain reserved for larger blocks, per user cadence.

## Palette viewport — targeted acceptance, 2026-10-26

UI source `6e61945bff6e40a092f91c605ca2f01fb60bf395` moves palette suggestion scrolling to
Rust-owned extent/allocation, fractional offset, reveal and clamp. The existing
custom Rust-backed scrollbar and Metal projection remain; palette-specific
ScrollView synchronization/subscriptions are removed. A shared height-range
setter keeps the dynamic 90..300px result cap. Separate overflow-indicator policy
preserves edge indicators without imposing a dialog background. Views retains
its native ScrollView and existing contents-preferred-size behavior.

The new shared actual-palette case injects 30 Unicode results, checks ownership,
compact/wide height caps, bottom/first-row reveal, keyboard wrapping and selected
row visibility, shrinking to one result, no-result hiding and reopening. Existing
palette cases retain async/stable-selection, stale query/profile, IME, actual
mouse activation after scroll, text/error/theme/fallback and editor contracts.
Focus passed **5 Metal / 2 Views**; affected regressions
**18 Metal / 13 Views**, single attempt, retry limit zero.
Checks passed **278 Rust**, **68 live and 68 clean-archive Python**, scoped
GN/boundaries and final Ninja no-op. All 53 committed/live/staged prototype inputs
and compiled/frozen-bootstrap/native-engine identities were checked. Unchanged
CoreText sources retain the prior full checkpoint's 180 SDK cases plus raster;
that harness was not rerun for this adapter change.
[Targeted palette viewport report](benchmarks/rust-palette-viewport-targeted-2026-10-26.json).

Initial failing probe sources/logs are retained in the report. Final acceptance
waits for real debounced search before injecting data, checks minimal row reveal
below the 9px top inset, and waits for queued Metal scene visibility updates.
No production behavior changed for those fixture corrections.

Per user cadence this stage has targeted acceptance. The earlier full matrix
below predates this source. Horizontal footer scrolling, native control/node/
container/menu/input ownership, manual hardware trackpad/IME/VoiceOver/RTL/scale/
theme/GPU-recovery acceptance, controlled CPU/GPU/drawable-presentation/input-
latency/memory comparison and release/license/notice checks remain open. The lab
retains frozen bootstrap/network inputs; current-main network policy is outside
this binary's acceptance. No complete parity or controlled performance gain is
claimed; no dependency, service/store/admission or persistence change.

## Horizontal footer viewport — full checkpoint, 2026-10-26

UI source `d44351ca2ad2c3d4125139e35e2354096dca7899` moves the horizontal workspace footer
viewport to Rust. Together with the main sidebar and palette, all three shell
scroll surfaces now use Rust-owned extent/allocation, authoritative fractional
primary-axis offset, reveal and clamp in the opt-in backend. The injected custom
Rust-backed horizontal scrollbar keeps thumb/track/key/AX and Metal rendering.
WorkspaceShell no longer owns ScrollView subscriptions/synchronizers or concrete
ScrollView hosts; Views retains native ScrollView through the common adapter.
Native control/node identity, measurement, global gesture arbitration, event/
focus/AX/window/frame/bar and animation adapters remain.

The full compiled inventory has **202 cases**. Metal passed **196 regular
cases**, Views **148 regular cases**; each explicitly skipped the same six
optional manual/live-import/performance scenarios. One launcher job, retry zero,
no failures/crashes/timeouts. Dedicated Metal fixture cases are excluded from
Views. Focus passed **6 Metal / 3 Views**. The full matrix also covers the
vertical viewport/palette/editor regressions. Checks passed **280 Rust**, **180 SDK font/direction
cases plus raster**, **68 live and 68 clean-archive Python**, scoped GN/boundaries
and final Ninja no-op. All 53 committed/live/staged prototype inputs and compiled/
frozen-bootstrap/native-engine identities were checked.
[Full footer viewport report](benchmarks/rust-footer-viewport-checkpoint-2026-10-26.json).

The horizontal native oracle compares extent, offsets/clamps and minimum/
oversized-target reveal at multiple sizes. The actual shared footer case checks
ownership and sidebar width changes, derives native extent from live geometry,
and exercises reveal/Home/End/AX for positive ranges. Metal must retain full-dot
overflow at both widths; fitted native ranges retain zero offset/no bar. Both check
clamp after removing ten workspaces. A separate Widget routes fractional/momentum horizontal raw events, checks exact
pixels, vertical propagation, velocity rejection, both clamps and collapse.
The real-footer mouse thumb drag, emoji projection, click selection, settings,
pinning and fallback cases retain their assertions. Two new Rust cases cover
horizontal bar reservation/overlay, edge geometry, reveal/resize/empty extents.

A source audit confines typed ScrollView references in product prototype C++ to
the native comparison adapter. Chromium compositor mailbox diagnostics remain
in both backend logs (6 Metal / 2 Views invalid-mailbox messages); the drag/width
cases and launchers passed, but those messages are not diagnosed or resolved here.

Initial probes passed all six Metal cases but exposed overstrict assumptions in
the new real-footer Views oracle: native contents-preferred-size=false may fit
or retain an overflowing content width after resize. Final acceptance derives
the native range from live geometry and still requires full-dot Metal overflow
at both widths. Failing sources/logs are retained; no production behavior changed
for these fixture corrections.

This full source includes the preceding palette migration and its
[targeted acceptance](benchmarks/rust-palette-viewport-targeted-2026-10-26.json),
main-sidebar migration and earlier shared raw-trackpad/capture corrections.
The lab retains frozen bootstrap/network inputs from `d23f232`; current-main
network policy is outside this binary's acceptance. No dependency, browser
service/store/admission or persistence policy changed.

Remaining native control/node/container/menu/input/window ownership, manual
hardware trackpad/IME candidates/VoiceOver/global RTL/scale/theme/GPU recovery,
controlled CPU/GPU/drawable-presentation/input-latency/memory comparison and
release/license/notice acceptance remain open. Global workspace swipe arbitration
still belongs to the shell adapter; the isolated horizontal raw-event test does
not establish physical trackpad acceptance. No complete functional parity or
controlled performance gain is claimed. Full matrices remain reserved for larger
blocks, per user cadence.

## Sidebar/split resize hosts — targeted acceptance, 2026-10-26

UI source `0471902fcd5e008d8c7b72724cc875bf80a7d8f9` removes ResizeArea ownership from the
Rust sidebar grip and split divider. The opt-in backend now creates a plain View
host for event/paint/cursor/AX delivery and uses its existing Rust SidebarDrag or
SplitDrag controller for active/anchor/update/delta/end/cancel. Only the applicable
controller is allocated. Separate NativeSidebarGrip/NativeSplitDivider classes
preserve the Views comparison path. Native shell/pane adapters consume a small
ResizeHandle interface; sidebar active projection reads Rust state.

The new shared browser case checks actual host type in both backends, splitter
AX role/name, horizontal/vertical and disabled cursors. Its Rust branch checks
hide and orientation cancellation followed by late input. Existing split
lifecycle and real native mouse/gesture/sidebar-autohide/split-orient-title-close
cases retain their assertions. The Rust host also cancels split resize on hide;
late capture loss cannot commit an obsolete gesture.

Focus passed **4 Metal / 2 Views**; affected regressions
**28 Metal / 21 Views**, single attempt, retry limit zero.
Checks passed **280 Rust**, **68 live and 68 clean-archive Python**, scoped
GN/boundaries and final Ninja no-op. All 53 committed/live/staged prototype inputs
and compiled/frozen-bootstrap/native-engine identities were checked. Unchanged
CoreText sources retain the preceding full checkpoint's 180 SDK font/direction
cases plus raster; that harness was not rerun for this host change.
[Targeted resize-host report](benchmarks/rust-resize-host-targeted-2026-10-26.json).

Per user cadence this smaller ownership block has targeted acceptance. The full
matrix below predates this source and is not counted as its full pass. Other
native control/node/container/menu/input/window ownership and final manual/
performance/release/license acceptance remain open. The lab retains frozen
bootstrap/network inputs; current-main network policy is outside this binary's
acceptance. No complete parity or controlled performance gain is claimed; no
dependency, service/store/admission or persistence policy change.
