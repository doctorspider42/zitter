# Rust + Metal migration: functional parity

Goal: match the original workspace UI, with Rust owning migrated control layout
and behavior and Metal presenting it. Keep real Chromium WebContents and existing
application/domain commands; preserve profile/session isolation. Push verified
milestones to `origin/main` as they land. The opt-in switch remains
`--zitter-sidebar=rust-metal` until the migration and comparison are complete.

## Current execution policy

At the user's request, continue development, non-windowed Rust/Python tests,
GN/dependency checks and compilation. Do not launch the application or execute
windowed browser/desktop tests until the user requests the later GUI checkpoint.
Compiled GUI regressions remain `awaiting_gui_checkpoint`; earlier GUI passes do
not accept subsequent source changes.

## How to read progress

“Preserved” means a feature still works through its existing Views controller.
It does **not** mean the feature has been migrated. Native system facilities
(AppKit menus, IME, accessibility and file dialogs) can remain platform adapters;
Views-specific layout and control behavior must have an explicit replacement.

| Surface / behavior | Current owner | Remaining migration / acceptance |
| --- | --- | --- |
| Live tab/pin/folder/split text and card drawing | Rust display list + Metal; CoreText text shaping | Expand focus/disabled/theme/scale coverage; replace native animation snapshots |
| Row title/detail/close geometry | Rust in opt-in backend, shared with display list | Replace native interaction containers; keep explicit platform font/accessibility adapters |
| Row mouse/key activation, drag threshold, latched drag, cancellation and close visibility | Rust input reducer in opt-in backend; native event/focus/AX adapter | Preserve native default action; replace Button containers and verify migrated drop/menu routing |
| Next/previous tab wrapping | Rust; existing configurable key dispatch | Preserve page shortcut precedence and editing/IME behavior |
| Vertical list/folder columns and full content extent | Rust via native View measurement/bounds adapter | Verify collapse/resize against Views; replace the native hierarchy with stable Rust item identities |
| Folder hierarchy, collapse state, indentation wrappers and split tile geometry/placement | Rust tree/tab plans keyed by workspace/item IDs; native View/measurement/AX adapters | Replace native node containers and viewport/clipping adapters |
| Scroll offset normalization, viewport projection and clipping | Rust logical viewport; native ScrollView input/scroll-service adapters | Replace native scroll containers; expand real trackpad/scale coverage |
| Vertical/horizontal scrollbar geometry, range normalization, thumb drag/snap/cancellation and drawing | Rust + shared Metal canvas; native ScrollBar/View, key/menu/AX and platform color adapters | Replace native nodes and track-repeat/menu dispatch; dedicated menu/VoiceOver/scale coverage |
| Selection, close/reopen, title changes, pinned navigation | Rust row/close input into existing application callbacks; native Button/AX adapters | Migrate remaining command routing and containers; preserve persistence and last-tab rules |
| Folder expand/collapse and tree visibility; create/rename/delete and nested pins | Rust tree and folder form/notice state, copy, layout and scene text; native text input/modal title/buttons and application persistence (GUI checkpoint pending) | Migrate remaining dialog controls; verify destructive defaults, paragraph typography, admission failure, persistence rollback, IME and stale context |
| Tab/pin context menus and drag to split | Rust menu composition, stable-identity target/action resolution and drop geometry; native event/popup and application adapters (GUI checkpoint pending) | Verify menu/drag/stale-target/RTL coverage; replace native View identity lookup/interaction nodes |
| New Tab action and configurable shortcut hint | Rust text/icon/hint layout, width alignment and input; shared Metal surface; native outer placement/focus/AX adapter | Replace the native Button container; expand keyboard/appearance coverage |
| Address pill | Rust host/path layout, GPU icon/outline/text and shared input reducer; native outer placement/focus/AX adapter | Replace native Button container; retain address/search/IME service |
| Workspace heading and emoji text | Rust display list + Metal; native static-text accessibility adapter | Replace outer native View placement; expand VoiceOver/scale coverage |
| Footer pin/settings, workspace dots/emoji and horizontal overflow | Rust layout, input reducer and shared Metal canvas; native focus/AX/menu adapters | Replace native containers; expand focus, trackpad, RTL and scale coverage |
| Navigation/header controls | Rust strip layout/input and analytic GPU icons; main-window Metal canvas sharing sidebar GPU resources | Replace native Button/outer frame adapters; expand peek, accessibility, RTL and scale coverage |
| Sidebar resizing, pinning, auto-hide, edge reveal | Rust drag/width bounds, shell/sidebar/grip geometry and auto-hide guards; Metal grip; native pin/show commands | Replace native window/event/cursor/AX adapters and edge-indicator painting; broaden input/accessibility coverage |
| Workspace swipe/animation/cancellation | Rust gesture/preview/transition policy and Metal canvases; native event/CA completion and application commit adapters (GUI checkpoint pending) | Run deferred browser/desktop acceptance; audit native fallback, cancellation, accessibility and scale |
| Split headers/divider, focus, orientation, swap/close | Rust pane/bar layout, title text, menu/close input and divider drag; shared Metal rendering | Replace native View/Button/ResizeArea event, cursor and AX adapters; expand gesture/RTL/scale coverage |
| Address/search palette, suggestions, providers, IME | Rust result selection/layout/text/icons, palette/input/static chrome geometry, Metal hints/errors; native Textfield/IME, provider/search controllers | Migrate remaining native containers/frame presentation; expand composition/accessibility/scale coverage |
| Command Center tabs/pins/workspaces/history/actions | Rust result selection/drawing; existing search/ranking and application commands | Replace remaining native containers/frame presentation and command adapters; preserve stale results and identity |
| Profile management create/rename/delete | Rust controller, copy, validation, stable-ID selection, two-step delete and column/text layout; Metal static text; native input/picker and storage services (GUI checkpoint pending) | Migrate remaining native input/picker nodes; verify IME, typography, action groups, stale confirmation, profile/session isolation and failed-write rollback |
| Workspace create/edit settings | Rust form mode/validation/requests and column/text layout; Metal headings/help/status; native input, header row, appearance/editor services and storage (GUI checkpoint pending) | Migrate remaining native controls/header/action rows and outer panel; verify profile changes, IME, async errors and persistence |
| Workspace/profile form action buttons | Rust button/group geometry, shared pointer/key reducer and text/background/border/focus primitives; four Metal groups; native Button/AX/focus, platform font/metric/color and command adapters (GUI checkpoint pending) | Replace native interaction nodes; verify dynamic labels, disabled styles, focus/keyboard/cancellation, clipping/fallback and service semantics |
| Workspace color/emoji presets | Rust preset data/selection, row geometry, pointer/key reducer and scene primitives; two shared-context Metal canvases; native Button/AX/focus and text/grapheme/storage adapters (GUI checkpoint pending) | Replace native input nodes and editor presentation; verify capture/visibility/workspace cancellation, clipping, keyboard/IME/AX, theme/scale and persistence |
| Workspace deletion confirmation | Rust captured-target notice state/copy and shared paragraph layout; Metal body/error text; native modal title/buttons and deletion/persistence services (GUI checkpoint pending) | Migrate remaining host controls; verify default Cancel, beforeunload, stale targets, retained profile data, typography and screen bounds |
| General preferences, shortcuts and site preferences | Views + application services | Rust forms; use the same persisted configuration |
| History, downloads, passwords and permissions | Views/native service adapters | Rust presentation/control layer; retain browser service and security semantics |
| Peek, promote/split, link/page menus and routing | Views/native adapters + application | Rust control routing, focus and dismissal; retain native page integration |
| Import form and first-run import | Rust UI option/debounce/admission/controller state, copy, columns and Metal static/status/Apply text; native Button/AX/focus, source/header nodes and application import services (GUI checkpoint pending) | Migrate remaining native source/header and input nodes; verify latest options, busy recovery, credentials opt-in, source immutability, first-run/persistence/rollback and runtime accessibility |
| Import checkboxes | Rust checked state, shared pointer/key input, mark/label/RTL geometry and primitives; one Metal canvas; native Button/AX/focus/event and font/inset/color adapters (GUI checkpoint pending) | Replace native interaction nodes; verify checked/default action, disabled/capture/context/visibility behavior, actual marks/typography, theme/scale and fallback |
| Time Machine | Views + application services | Rust presentation; preserve previews, validation, backups and restoration |
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
`SwipePreviewUsesMetalWithoutNativeSnapshots`,
`WorkspaceSwitchUsesPresentedMetalSceneAndCancels`,
`RowMenuPlansAndQueuedIdentitySurviveReindex`,
`FolderFormScenesValidationCompositionAndStaleContext`,
`FolderNoticesWrapPreserveCancelAndRejectStaleTargets`, sidebar hide/cancel, workspace changes
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

- Regular click/shortcut workspace transitions now also have a direct Metal
  viewport overlay. Rust owns direction, displacement, duration and static
  motion guards. The overlay reuses the preview scene-data adapter, starts after
  a one-shot Metal drawable-presented callback and finishes through an asynchronous
  Core Animation delegate. Generation guards reject callbacks from cancelled or
  superseded transitions; native row painting remains a fallback for unavailable
  GPU resources. Native input/AX and workspace commands remain adapters. This
  stage has non-windowed verification only; see
  [switch verification](benchmarks/rust-workspace-switch-headless-2026-10-23.json).

- Tab/pin menu composition, split-side availability, typed tab/split command
  selection and the physical right-half drop target now use Rust. Opaque 128-bit
  page tokens let queued actions follow a tab after index shifts and reject
  deleted/replaced pages or foreign workspaces. Native adapters keep popup
  presentation, View-to-row lookup, URL/rule semantics and browser callbacks.
  The new regression is compiled but deferred; see
  [row menu verification](benchmarks/rust-row-menu-headless-2026-10-23.json).

- Folder create/rename bodies now use Rust copy, normalization, validation,
  target/submission state, layout and scene text, with a native Textfield/IME and
  modal-host/button adapter. Static label/error nodes contain no hidden Labels;
  two small Metal canvases exclude the editor. Immediate admission rejection
  keeps the form open and restores a temporarily expanded parent; accepted
  asynchronous storage still uses the existing persistence/rollback and save-error
  dialog. Delete and asynchronous-error presentation remain native. Add the three
  folder create/rename/failed-write cases to the Views controls at the checkpoint
  (20 total); the new compiled Metal case is deferred. See
  [folder form verification](benchmarks/rust-folder-form-headless-2026-10-23.json).

- Folder deletion confirmation and save-error message bodies now use Rust
  copy/state/layout and Metal text. A CoreText typography adapter supplies complete
  line fragments; Rust places those lines and errors without native Label layout.
  Deletion keeps Cancel as the default and revalidates context/target before
  admission. Error acknowledgement emits no delete target/action. Native modal
  title/buttons and application deletion/async persistence remain adapters.
  This stage is compiled and unit tested only; see
  [notice verification](benchmarks/rust-folder-notices-headless-2026-10-23.json).

- Profile management now uses a Rust controller for stable-ID selection, readiness,
  busy/submission guards, name normalization/duplicates, create-and-attach context,
  service feedback and same-target two-click deletion. Rust also owns panel copy,
  column geometry and paragraph placement; CoreText/Metal draw static copy without
  native Labels. Native editors, picker, buttons, outer popup and profile/storage
  services remain. **87 Rust tests and 60 Python tests passed** without windows.
  The new compiled GUI case and four existing profile lifecycle/isolation/write
  cases expand the deferred checkpoint to **53 Metal cases and 24 Views controls**.
  No GUI acceptance or performance gain is established. See
  [profile panel verification](benchmarks/rust-profile-panel-headless-2026-10-23.json).

- Workspace create/edit settings now use Rust form mode, captions/feedback,
  readiness/submission guards, original ASCII-only name trimming, current
  workspace/profile validation and typed requests. Settings/appearance vertical
  columns and static text use the shared Rust/CoreText/Metal paragraph adapter;
  native header/action rows, inputs/buttons/color/emoji controls and persistence
  remain. **95 Rust and 61 Python tests passed** without windows. The new compiled
  GUI case and existing workspace-dialog bounds case extend the deferred matrix
  to **55 Metal cases and 25 Views controls**. See
  [workspace form verification](benchmarks/rust-workspace-form-headless-2026-10-23.json).

- Workspace deletion confirmation now uses Rust captured-target notice state,
  full irreversible/profile-retention copy, default Cancel policy and typed
  deletion requests. A shared notice body supplies Rust paragraph geometry,
  CoreText/Metal rendering, native AX/fallback and retryable inline feedback for
  both workspace and folder notices. Native deletion/beforeunload/persistence
  remain services. **101 Rust and 61 Python tests passed** without windows; the
  new compiled GUI regression and existing beforeunload/profile case expand the
  deferred matrix to **57 Metal cases and 26 Views controls**. See
  [workspace notice verification](benchmarks/rust-workspace-notice-headless-2026-10-23.json).

- Color/emoji preset rows now use Rust preset data/selection, bounded row/editor
  geometry, the shared Rust pointer/key reducer and scene primitives. Two Metal
  canvases batch their controls and exclude native text editors. Native preset
  focus-ring/click effects are removed; the same Rust recipe supplies fallback
  and focus feedback. Pending input cancels on capture loss, workspace changes
  and hiding the appearance section; IME composition is guarded. **108 Rust and
  61 Python tests passed** without windows. The new compiled regression extends
  the deferred matrix to **58 Metal cases and 27 Views controls**, adding the
  existing workspace appearance/context case to Views. See
  [appearance verification](benchmarks/rust-appearance-headless-2026-10-23.json).

- Nine profile/workspace actions now use Rust button/group layout and shared
  input, with scene text and background/border/hover/focus primitives batched on
  four Metal canvases. Native MdTextButton/Label drawing is removed for those
  opt-in actions; platform font/metrics/colors and native event/AX/focus nodes
  remain. Text/context/catalogue/visibility changes cancel pending activation.
  **115 Rust and 61 Python tests passed** without windows; the new compiled GUI
  case expands the deferred matrix to **59 Metal cases and 27 Views controls**.
  See [form action verification](benchmarks/rust-form-actions-headless-2026-10-23.json).

- Import now uses a Rust form controller for option snapshots, latest-only
  debounce, hidden/cancelled scans, busy/readiness waiting, control locks and
  matching-preview Apply admission. Native service status remains authoritative;
  source I/O, stale I/O reply filtering, credentials/profile data and persistence
  remain native. Static/status copy and Apply use Metal with Rust column layout;
  selectors, checkbox labels and header close remain native. **125 Rust and 61
  Python tests passed** without windows. The new compiled case and seven existing
  import/first-run/source/persistence cases expand the deferred checkpoint to
  **67 Metal cases and 34 Views controls**. See
  [import form verification](benchmarks/rust-import-form-headless-2026-10-23.json).

- Four import checkboxes now use Rust checked state, programmatic/user mutation
  policy, wrapped/RTL mark-label layout and scene primitives on one shared Metal
  canvas. Native Checkbox/Label/focus-ring/ink drawing is absent in the opt-in
  backend; native Button/AX/focus and platform typography/colors remain adapters.
  **132 Rust and 61 Python tests passed** without windows. The new compiled case
  expands the deferred matrix to **68 Metal cases and 34 Views controls**. See
  [checkbox verification](benchmarks/rust-checkboxes-headless-2026-10-23.json).

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
