# Rust + Metal migration: functional parity

Goal: match the original workspace UI, with Rust owning migrated control layout
and behavior and Metal presenting it. Keep real Chromium WebContents and existing
application/domain commands; preserve profile/session isolation. Push verified
milestones to `origin/main` as they land. The opt-in switch remains
`--zitter-sidebar=rust-metal` until the migration and comparison are complete.

## How to read progress

“Preserved” means a feature still works through its existing Views controller.
It does **not** mean the feature has been migrated. Native system facilities
(AppKit menus, IME, accessibility and file dialogs) can remain platform adapters;
Views-specific layout and control behavior must have an explicit replacement.

| Surface / behavior | Current owner | Remaining migration / acceptance |
| --- | --- | --- |
| Live tab/pin/folder/split text and card drawing | Rust display list + Metal; CoreText text shaping | Expand focus/disabled/theme/scale coverage; replace native animation snapshots |
| Row title/detail/close geometry | Rust in opt-in backend, shared with display list | Replace native interaction containers; keep explicit platform font/accessibility adapters |
| Row mouse/key activation, drag threshold, latched drag, cancellation and close visibility | Rust input reducer in opt-in backend; native event/focus/AX adapter | Preserve native default action; migrate drop target policy and replace Button containers |
| Next/previous tab wrapping | Rust; existing configurable key dispatch | Preserve page shortcut precedence and editing/IME behavior |
| Vertical list/folder columns and full content extent | Rust via native View measurement/bounds adapter | Verify collapse/resize against Views; replace the native hierarchy with stable Rust item identities |
| Folder hierarchy, collapse state, indentation wrappers and split tile geometry/placement | Rust tree/tab plans keyed by workspace/item IDs; native View/measurement/AX adapters | Replace native node containers and viewport/clipping adapters |
| Scroll offset normalization, viewport projection and clipping | Rust logical viewport; native ScrollView input/scroll-service adapters | Replace native scroll containers; expand real trackpad/scale coverage |
| Vertical/horizontal scrollbar geometry, range normalization, thumb drag/snap/cancellation and drawing | Rust + shared Metal canvas; native ScrollBar/View, key/menu/AX and platform color adapters | Replace native nodes and track-repeat/menu dispatch; dedicated menu/VoiceOver/scale coverage |
| Selection, close/reopen, title changes, pinned navigation | Rust row/close input into existing application callbacks; native Button/AX adapters | Migrate remaining command routing and containers; preserve persistence and last-tab rules |
| Folder expand/collapse and tree visibility; create/rename/delete and nested pins | Rust tree state/layout; application persistence and native menu/editing adapters | Rust editing forms; preserve failed writes and validation rollback |
| Tab/pin context menus and drag to split | Views event routing + application commands | Rust target/action routing; retain native menus and drop policy |
| New Tab action and configurable shortcut hint | Rust text/icon/hint layout, width alignment and input; shared Metal surface; native outer placement/focus/AX adapter | Replace the native Button container; expand keyboard/appearance coverage |
| Address pill | Rust host/path layout, GPU icon/outline/text and shared input reducer; native outer placement/focus/AX adapter | Replace native Button container; retain address/search/IME service |
| Workspace heading and emoji text | Rust display list + Metal; native static-text accessibility adapter | Replace outer native View placement; expand VoiceOver/scale coverage |
| Footer pin/settings, workspace dots/emoji and horizontal overflow | Rust layout, input reducer and shared Metal canvas; native focus/AX/menu adapters | Replace native containers; expand focus, trackpad, RTL and scale coverage |
| Navigation/header controls | Rust strip layout/input and analytic GPU icons; main-window Metal canvas sharing sidebar GPU resources | Replace native Button/outer frame adapters; expand peek, accessibility, RTL and scale coverage |
| Sidebar resizing, pinning, auto-hide, edge reveal | Rust drag/width bounds, shell/sidebar/grip geometry and auto-hide guards; Metal grip; native pin/show commands | Replace native window/event/cursor/AX adapters and edge-indicator painting; broaden input/accessibility coverage |
| Workspace swipe/animation/cancellation | Rust gesture/preview content geometry and Metal page canvases; native timers/compositor settle and application commit adapter (GUI checkpoint pending) | Migrate regular workspace-switch animation and native fallback snapshots; run deferred browser/desktop acceptance |
| Split headers/divider, focus, orientation, swap/close | Rust pane/bar layout, title text, menu/close input and divider drag; shared Metal rendering | Replace native View/Button/ResizeArea event, cursor and AX adapters; expand gesture/RTL/scale coverage |
| Address/search palette, suggestions, providers, IME | Rust result selection/layout/text/icons, palette/input/static chrome geometry, Metal hints/errors; native Textfield/IME, provider/search controllers | Migrate remaining native containers/frame presentation; expand composition/accessibility/scale coverage |
| Command Center tabs/pins/workspaces/history/actions | Rust result selection/drawing; existing search/ranking and application commands | Replace remaining native containers/frame presentation and command adapters; preserve stale results and identity |
| Workspace/profile create/edit/delete and settings | Views + persistent application model | Rust forms, validation, focus; preserve isolation and failed-write behavior |
| General preferences, shortcuts and site preferences | Views + application services | Rust forms; use the same persisted configuration |
| History, downloads, passwords and permissions | Views/native service adapters | Rust presentation/control layer; retain browser service and security semantics |
| Peek, promote/split, link/page menus and routing | Views/native adapters + application | Rust control routing, focus and dismissal; retain native page integration |
| Imports, first run and Time Machine | Views + application services | Rust presentation; preserve previews, validation, backups and restoration |
| Keyboard accessibility, VoiceOver, focus and tooltips | Native Views nodes | Explicit native accessibility adapter for Rust controls; no duplicate nodes |

## Verified milestones

- `b5af24a`: live text-bearing sidebar renderer, bounded text cache, real scroll
  fix, 16 native Metal browser regressions, 2 Views controls, 4 Rust unit tests.
  See [prototype details](rust-metal-sidebar.md) and the linked validation report.
- Rust row geometry, close visibility policy and pointer state, shared with the
  native interaction adapter: 24 Metal browser tests, 3 Views controls and 7 Rust
  tests passed. Includes native drag-to-split and close clicks at width limits.
  See [validation](benchmarks/rust-row-controls-functional-2026-10-21.json).
  This is a control migration step, not full Views removal.
- Rust-owned vertical list/folder columns, including cached workspace trees,
  visible-child spacing, margins and full scroll extent: 25 native Metal browser
  regressions, 4 Views controls, 9 Rust tests and 59 Python tests passed. Signed
  app inspected with Unicode, Split View, folder collapse/expand and scrolling.
  See [column validation](benchmarks/rust-column-layout-functional-2026-10-21.json).
- Row text now belongs to the scene, with no hidden Views title Labels. The
  native list skips child painting while Metal is visible, and restores fresh
  native fallback content for animations. Invalidation is forwarded before
  rasterization; new/removed rows explicitly invalidate layout and scene
  collection applies pending layouts. Native event/accessibility nodes remain.
  26 Metal browser regressions, 4 Views controls, 9 Rust tests and 59 Python tests
  passed; signed app checked with title updates and adding/closing a real tab.
  See [text ownership validation](benchmarks/rust-owned-row-text-functional-2026-10-21.json).
- Rust now owns RowButton mouse/key activation, visual input state, cancellation
  and platform key policy. Native event/focus/AX/default-action/menu adapters and
  application callbacks remain. 27 Metal browser regressions, 5 Views controls,
  13 Rust tests and 59 Python tests passed; the split lifecycle case also passed
  5 repeated runs after explicit test layout synchronization. Signed app checked
  with mouse selection, Ctrl+Tab, folder collapse and Command Center text/Enter.
  See [row input validation](benchmarks/rust-row-input-functional-2026-10-21.json).

- Rust now lays out split tiles and indented pin wrappers, including odd-width
  remainder distribution and the eight-level indentation cap. Native hierarchy
  construction and folder collapse state remain. Model updates also invalidate
  the scene directly, preserving split title updates while native paints are
  locked. 29 Metal browser regressions, 6 Views controls, 16 Rust tests and 59
  Python tests passed. Signed app startup titles were rechecked after the fix.
  See [split/folder validation](benchmarks/rust-split-folder-layout-functional-2026-10-21.json).

- Rust now owns the folder hierarchy plan and collapse state, keyed by workspace
  and item IDs. The live native tree and swipe preview consume the same ordered
  plan; native containers, folder editing/persistence and pin/split replacement
  policy remain. 32 Metal browser regressions, 9 Views controls, 19 Rust tests
  and 59 Python tests passed, including hidden-folder rename/rebuild and large
  warmed sidebars. Signed app collapse state survived workspace switching.
  See [tree validation](benchmarks/rust-sidebar-tree-functional-2026-10-21.json).

- Rust now owns tab slots, split membership/pane order, pinned-pair anchoring and
  original-pin visibility. Native row/group adapters and swipe previews consume
  the same cached plan. 33 Metal browser regressions, 10 Views controls, 22 Rust
  tests and 59 Python tests passed, including two pinned panes at different
  folder depths, swapping while collapsed and restoring both original pins.
  Signed app pane/menu order was checked and restored. See
  [tab-plan validation](benchmarks/rust-tab-plan-functional-2026-10-21.json).

- Rust now owns logical viewport state, offset normalization, local-to-viewport
  projection and inherited clipping; Metal applies the clips with scissor state.
  Native scrolling/input containers remain adapters. 34 Metal browser tests,
  10 Views controls, 26 Rust tests and 59 Python tests passed. The new regression
  matches native and submitted fractional bounds/clips, verifies hit targets and
  normalizes an out-of-range physical offset. Signed app scrolling/selection was
  inspected and restored. See
  [viewport validation](benchmarks/rust-viewport-functional-2026-10-21.json).

- New Tab title/icon/hint drawing and layout now belong to Rust on the existing
  Metal surface, with no native text Labels. Configured hints update the scene;
  native fallback is gated and refreshed across ownership changes. 35 Metal
  browser tests, 10 Views controls, 29 Rust tests and 59 Python tests passed.
  The signed local package passed create/close/scroll checks; its trusted
  timestamp was omitted after a remote service outage, with the same certificate
  and unchanged default packaging. See
  [New Tab validation](benchmarks/rust-new-tab-functional-2026-10-22.json).

- Address host/path, analytic search/lock icon and workspace heading now belong
  to Rust on the existing Metal canvas, without hidden text Labels. The address
  action uses the shared Rust reducer and exposes its displayed text through a
  native accessibility description. Native outer placement, system adapters and
  editing remain. **36 Metal browser tests, 14 Views controls, 32 Rust tests and
  59 Python tests passed**. The signed, timestamped app passed address/keyboard
  and workspace round-trip inspection. See
  [address/heading validation](benchmarks/rust-address-heading-functional-2026-10-22.json).

- Rust now owns vertical scrollbar range/geometry, pointer grip, drag snap-back
  and cancellation, plus drawing on the existing Metal canvas. Native fallback
  uses the same primitive; the overflow thumb stays visible rather than fading.
  Native scroll services, track repeat/menu/key and AX adapters remain. **37 Metal
  browser tests, 14 Views controls, 35 Rust tests and 59 Python tests passed**;
  signed app scrolling, selection and AX value updates were inspected. Footer
  and navigation migration remain next. See
  [scrollbar validation](benchmarks/rust-scrollbar-functional-2026-10-22.json).

- Rust now owns footer slot/strip layout, control input, pin/settings icon and
  workspace dot/emoji drawing. Horizontal overflow uses the shared Rust scrollbar
  and viewport projection, reserving a separate gutter to preserve emoji height.
  **38 Metal browser tests, 14 Views controls, 37 Rust tests and 59 Python tests
  passed**. Signed app workspace/settings/emoji controls were inspected, and the
  demo state was restored. Native nodes/services remain; navigation is still
  awaiting migration. See
  [footer validation](benchmarks/rust-footer-functional-2026-10-22.json).

- Navigation now uses Rust strip layout/input and analytic GPU back/forward/reload
  icons. Its main-window canvas stays visible independently of the sidebar panel;
  the two surfaces share their device, queue and pipeline. Native frame/drag,
  Button/AX and application command adapters remain. **39 Metal browser tests,
  14 Views controls, 38 Rust tests and 59 Python tests passed**. Signed app history,
  actual reload and sidebar-toggle behavior were inspected and restored. See
  [navigation validation](benchmarks/rust-navigation-functional-2026-10-22.json).
- Split pane/header geometry, title text, menu/close input and divider drag state
  now use Rust. Three clipped Metal UI surfaces share the existing GPU context
  and exclude page bounds. Native View/Button/ResizeArea, cursor, event/AX and
  application command adapters remain. **40 Metal browser tests, 14 Views
  controls, 41 Rust tests and 59 Python tests passed**. Physical drags in both
  orientations, gesture/cancellation, title updates under paint lock, fallback
  and split-half close passed. Signed app menus switched both layouts and
  restored the demo. Broader input/accessibility/scale and performance comparison
  remain pending. See
  [split pane validation](benchmarks/rust-split-panes-functional-2026-10-22.json).
- Sidebar pointer/gesture drag state, width clamping, shell/sidebar/grip geometry
  and auto-hide guards now use Rust. A narrow Metal grip shares the existing GPU
  context; native window/event/cursor/AX, pin/show/persistence and edge-indicator
  painting remain adapters. **41 Metal browser tests, 14 Views controls, 44 Rust
  tests and 59 Python tests passed**. Physical dragging, gestures, persistence,
  half-window sizing, auto-hide/edge reveal, settings guards, cancelled drags and
  animation restoration passed. Signed app pinned/overlay and hide/show checks
  preserved the original demo. See
  [sidebar sizing validation](benchmarks/rust-sidebar-sizing-functional-2026-10-22.json).
- Address/Command Center results now use Rust selection/wrap/reconciliation,
  list/input geometry and palette placement, with scene text and analytic GPU
  icons. Native editing/IME, data/ranking/commands and static palette chrome
  remain. **42 Metal browser tests, 17 Views controls, 47 Rust tests and 59 Python
  tests passed**. Native application events verify async identity, stale guards,
  scrolling, composition routing, fallback and tab activation. The matching
  signed isolated app was launched, but **manual visual inspection is pending
  because the Mac is locked**. See
  [palette validation](benchmarks/rust-palette-functional-2026-10-22.json).

- Static palette search/escape decoration, hint text and validation errors now
  use Rust geometry and Metal rendering without hidden Labels. Native static-text
  accessibility and the Textfield/IME remain adapters. Rust also supplies the
  rounded frame and separator recipe, rasterized behind the native editor.
  Empty-result errors, theme changes, updates under paint lock and fallback are
  covered by a dedicated browser regression. **43 Metal browser tests, 17 Views
  controls, 50 Rust tests and 59 Python tests passed**. The matching signed app
  passed the unlocked-desktop palette check, completing the inspection blocked
  in the previous milestone. Physical input/IME/accessibility/scale and real
  CPU/GPU/presentation comparison remain pending. See
  [palette chrome validation](benchmarks/rust-palette-chrome-functional-2026-10-22.json).

## Current checkpoint policy (2026-10-23)

At the user's request, development currently runs without windowed tests. Rust
and Python tests, dependency checks and compilation continue. Browser regressions,
manual app inspection and presentation benchmarks are deferred until an explicit
GUI checkpoint. A compiled stage is not accepted as runtime parity on that basis.
The last complete GUI matrix is the palette-chrome stage (`3cd95d8`): 43 Metal and
17 Views cases. Do not apply those results to newer code.

The next checkpoint must run both complete browser matrices against the rebuilt
binaries and inspect the matching signed isolated app. In particular, exercise
`SidebarSwipeTracksCancelsAndSkipsSpaces`,
`CocoaSwipesRepeatWithoutMouseMovement`, `SidebarRemainsOpenWhileSpacesSlide`,
`MetalReturnsAfterWorkspaceAnimation`,
`SwipePreviewUsesMetalWithoutNativeSnapshots`, sidebar hide/cancel, workspace changes
while committing and the prior palette regressions. Verify vertical scrolling,
repeated stationary-cursor gestures, momentum, reduced motion, Unicode previews
and the return of Metal after settling. Preserve the normal application/profile.

- Workspace swipe input and preview placement now use Rust in the Metal backend.
  Thresholds/axis lock, reversible multi-space tracking, edge rubber band,
  momentum/phase-less completion and target rounding are unit tested without
  windows. Native snapshot painting, compositor animation, timers, guards and
  workspace-ID command routing remain. This stage awaits GUI checkpoint
  acceptance; see [non-windowed verification](benchmarks/rust-workspace-swipe-headless-2026-10-23.json).

- Workspace gesture previews now have a direct Metal scene path. Rust lays out
  visible preview rows/section labels/split pairs, reuses live indentation and
  split geometry and clips the scrolled source before GPU preparation. At most
  three clipped page canvases share the existing device/queue/pipeline. Native
  Core Animation translates cached drawables and settles them; unchanged scenes
  skip display-list work and GPU submission during movement. Native paint records
  are created only if the complete GPU preview is unavailable. The new compiled
  GUI regression is deferred; this stage is **not runtime accepted**. Regular
  non-gesture workspace-switch animation still uses native row painting. See
  [non-windowed scene verification](benchmarks/rust-workspace-preview-scenes-headless-2026-10-23.json).

## Completion gates

1. Every row above has a Rust implementation or a documented platform adapter,
   with no hidden Views implementation required to preserve its behavior.
2. Real-browser regressions exercise actual tabs, profiles, sessions, menus,
   keyboard/IME, pointer/drag/trackpad, accessibility and error/rollback paths.
3. Package and run the signed app on an isolated profile; inspect Unicode,
   scaling, appearance and workspace transitions. Keep the normal profile intact.
4. Compare the real text-bearing workload against Views, measuring CPU, GPU and
   actual presentation separately. Synthetic rectangle timings do not establish
   the performance of the migrated application.
5. Retain Apache-2.0 for original code and ship required upstream license notices;
   audit any newly introduced binary dependency before adoption.

The active goal is intentionally not complete while preserved Views surfaces
remain. Passing the current tests establishes the tested milestone only.
