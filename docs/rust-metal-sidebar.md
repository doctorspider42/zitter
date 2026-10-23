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
