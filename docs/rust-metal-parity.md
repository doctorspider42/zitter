# Rust + Metal migration: functional parity

Goal: match the original workspace UI, with Rust owning migrated control layout
and behavior and Metal presenting it. Keep real Chromium WebContents and existing
application/domain commands; preserve profile/session isolation. Push verified
milestones to `origin/main` as they land. The opt-in switch remains
`--zitter-sidebar=rust-metal` until the migration and comparison are complete.

## Current execution policy

UI source `337968c1358966e3b50fa30e2358339221713b46`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 167 | 6 | 0 |
| Views | 125 | 6 | 0 |

The inventory contains 173 cases; Views excludes 42 dedicated Metal cases
(131 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The dialog/beforeunload and shared static-copy gate passed **17 Metal / 14 Views**.
Checks passed **243 Rust tests**, **180 SDK font/direction cases plus raster**,
**66 clean-archive Python tests**, scoped GN/boundaries and optimized compilation.
The live checkout passed 68 Python tests, including two concurrent cases outside
this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-page-dialog-actions-checkpoint-2026-10-25.json).

Rust/Metal now owns page-dialog action drawing/input and linked/trailing button
row allocation, alongside the prior copy/body text/layout. Alert, confirm, prompt
and beforeunload action controls have no MdTextButton or native/hidden Label in the
opt-in backend. A single group canvas draws the actions, with the same Rust
recipe available for CPU fallback. Native theme/font metrics, row insets/spacing,
platform order, focus/AX and input-protection services remain adapters.

The page-dialog client bypasses Chromium's private native-button allocator and
retains native ClientView contents adoption, Escape, protection, window theming
and observer teardown. Native DialogDelegate owns modal/close semantics and
metadata. Actual platform defaults register Return on the Rust nodes; macOS
beforeunload's stored Cancel default still does not make Return activate Cancel.
Prompt IME/generation/frame/profile authorization is unchanged. Native model
updates rebuild the Rust row, releasing old canvases/weak callbacks/accelerators
and restoring a matching enabled focused control. Native GetOkButton/CancelButton
are null for this custom client; PageDialogs has an explicit test adapter.

The new shared browser case compares native button and row preferred metrics,
order and AX/default state for all four roles, checks childless Rust controls,
actual Metal caption visibility and label/enable updates, rejects an injected
blocked activation, then uses real Cocoa mouse and Space input. Existing real
Return/Escape, prompt IME, navigation, CloseNow and beforeunload tests also pass.
Two Rust cases cover linking, long-label limits, trailing placement and narrow
clipping. The title/frame, modal/native containers and wider native ownership,
manual platform acceptance, profiling and release/license checks remain open.
Normal installed apps/profiles were untouched; no bundle was signed.

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
| Folder expand/collapse and tree visibility; create/rename/delete and nested pins | Rust tree and folder form/notice state, copy, layout, scene text and shared folder editor; native IME/AX/focus/modal title/buttons and application persistence (automated UI checked; manual acceptance pending) | Migrate remaining dialog controls; verify destructive defaults, paragraph typography, admission failure, persistence rollback, IME and stale context |
| Tab/pin context menus and drag to split | Rust menu composition, stable-identity target/action resolution and drop geometry; native event/popup and application adapters (automated UI checked; manual acceptance pending) | Verify menu/drag/stale-target/RTL coverage; replace native View identity lookup/interaction nodes |
| New Tab action and configurable shortcut hint | Rust text/icon/hint layout, width alignment and input; shared Metal surface; native outer placement/focus/AX adapter | Replace the native Button container; expand keyboard/appearance coverage |
| Address pill | Rust host/path layout, GPU icon/outline/text and shared input reducer; native outer placement/focus/AX adapter | Replace native Button container; retain address/search/IME service |
| Workspace heading and emoji text | Rust display list + Metal; native static-text accessibility adapter | Replace outer native View placement; expand VoiceOver/scale coverage |
| Footer pin/settings, workspace dots/emoji and horizontal overflow | Rust layout, input reducer and shared Metal canvas; native focus/AX/menu adapters | Replace native containers; expand focus, trackpad, RTL and scale coverage |
| Navigation/header controls | Rust strip layout/input and analytic GPU icons; main-window Metal canvas sharing sidebar GPU resources | Replace native Button/outer frame adapters; expand peek, accessibility, RTL and scale coverage |
| Sidebar resizing, pinning, auto-hide, edge reveal | Rust drag/width bounds, shell/sidebar/grip geometry and auto-hide guards; Metal grip; native pin/show commands | Replace native window/event/cursor/AX adapters and edge-indicator painting; broaden input/accessibility coverage |
| Workspace swipe/animation/cancellation | Rust gesture/preview/transition policy and Metal canvases; native event/CA completion and application commit adapters (automated UI checked; manual acceptance pending) | Run deferred browser/desktop acceptance; audit native fallback, cancellation, accessibility and scale |
| Split headers/divider, focus, orientation, swap/close | Rust pane/bar layout, title text, menu/close input and divider drag; shared Metal rendering | Replace native View/Button/ResizeArea event, cursor and AX adapters; expand gesture/RTL/scale coverage |
| Address/search palette, suggestions, providers, IME | Rust editor and result selection/layout/text/icons, palette/input/static chrome geometry, Metal input/hints/errors; explicit native IME/focus/AX and existing provider/search adapters | Migrate remaining native containers/frame presentation; expand composition/accessibility/scale coverage |
| Command Center tabs/pins/workspaces/history/actions | Rust result selection/drawing; existing search/ranking and application commands | Replace remaining native containers/frame presentation and command adapters; preserve stale results and identity |
| Profile management create/rename/delete | Rust controller, copy, validation, stable-ID selection, two-step delete and column/text layout; Metal static text and selectors; native input/popup/IME and storage services (automated UI checked; manual acceptance pending) | Migrate remaining native input nodes; verify IME, typography, action groups, stale confirmation, profile/session isolation and failed-write rollback |
| Workspace create/edit settings | Rust form mode/validation/requests and column/text layout; Metal headings/help/status; native input, header row, appearance/editor services and storage (automated UI checked; manual acceptance pending) | Migrate remaining native controls/header/action rows and outer panel; verify profile changes, IME, async errors and persistence |
| Workspace/profile form action buttons | Rust button/group geometry, shared pointer/key reducer and text/background/border/focus primitives; four Metal groups; native Button/AX/focus, platform font/metric/color and command adapters (automated UI checked; manual acceptance pending) | Replace native interaction nodes; verify dynamic labels, disabled styles, focus/keyboard/cancellation, clipping/fallback and service semantics |
| Workspace color/emoji presets | Rust preset data/selection, row geometry, pointer/key reducer and scene primitives; two shared-context Metal canvases; native Button/AX/focus and text/grapheme/storage adapters (automated UI checked; manual acceptance pending) | Color/emoji editor migration verified; replace remaining native preset Button interaction ownership and broaden capture/visibility/IME/AX/theme/scale/persistence acceptance |
| Workspace deletion confirmation | Rust captured-target notice state/copy and shared paragraph layout; Metal body/error text; native modal title/buttons and deletion/persistence services (automated UI checked; manual acceptance pending) | Migrate remaining host controls; verify default Cancel, beforeunload, stale targets, retained profile data, typography and screen bounds |
| General preferences, shortcuts and site preferences | Rust/Metal editors, eleven preference selectors, 24 switches, three clear-data checkboxes and 19 fixed actions plus dynamic rule controls; Rust category/dismiss recipes, category/filter state, text-draft/profile refresh/admission, rule paging and shared text/backing underlay; native event/focus/AX, normalization, menus and stores | Migrate UI descriptions/data, remaining busy/status/form state, tools option-to-field/form construction, service/progress wording and native outer-container ownership; tools panel/header/body/clear allocation is Rust; retain profile/global configuration and recorder behavior |
| History, downloads, passwords and permissions | Rust/Metal history query editor, clear-period selector and clear/more actions; shared Rust/Metal tools header/status/empty copy and committed-query/pagination/loaded state; Rust/Metal history result text/backing and column layout; native Unicode/IME/service and row event/focus/AX/scroll adapters, with other remaining Views lists/controls | Finish native history interaction/container ownership and other service forms/result controls; retain browser service and security semantics |
| Peek, promote/split, link/page menus and routing | Views/native adapters + application | Rust control routing, focus and dismissal; retain native page integration |
| Import form and first-run import | Rust UI option/debounce/admission/controller state, copy, columns and Metal static/status/Apply text; native Button/AX/focus, header nodes, native popup/IME adapters and application import services (automated UI checked; manual acceptance pending) | Migrate remaining native header and input nodes; verify latest options, busy recovery, credentials opt-in, source immutability, first-run/persistence/rollback and runtime accessibility |
| Import checkboxes | Rust checked state, shared pointer/key input, mark/label/RTL geometry and primitives; one Metal canvas; native Button/AX/focus/event and font/inset/color adapters (automated UI checked; manual acceptance pending) | Replace native interaction nodes; verify checked/default action, disabled/capture/context/visibility behavior, actual marks/typography, theme/scale and fallback |
| Import source selector | Rust selected state, prefix timeout/search, popup tokens, macOS key policy and closed-control geometry/scene; Metal canvas; native menu/IME/focus/AX/event adapters (automated UI checked; manual acceptance pending) | Verify popup lifecycle, typeahead, keyboard and VoiceOver, disabled/hide/capture, RTL/theme/scale and fallback; migrate remaining native adapters |
| Workspace/managed profile selectors | Rust stable-ID reconciliation, prefix/menu state and scene; two Metal canvases; native popup/IME/focus/AX/event and font/color adapters (automated UI checked; manual acceptance pending) | Verify profile CRUD/assignment persistence, renamed/reordered/removed choices, stale menu callbacks, native popup/IME/AX, clipping and theme/scale/RTL; migrate remaining native adapters |
| Time Machine | Rust/Metal name/filter editors, stable-ID snapshot/closed selectors and five actions, Rust command/IME admission and outcome presentation, page/filter state, page copy and shared Metal header/body/status text; native snapshot/restore services and remaining Views controls | Migrate remaining form/status decisions, rows and container ownership; preserve previews, validation, backups and restoration |
| Page prompt, confirm/alert and beforeunload | Rust/Metal prompt editor, shared body text/copy/layout, action drawing/input and linked/trailing row layout; native origin/profile/frame, title/window/modal, event/focus/AX/input-protection and generation-scoped callbacks | Migrate remaining title/frame/modal and event/scroll/AX/window ownership; retain native request identity, cancellation and beforeunload safety |
| Workspace/profile name, color/emoji, folder-name and address/palette editing | Rust UTF-16 state/history/IME, viewport and scene; CoreText geometry, Metal and CPU fallback; one native input/focus/AX/menu/clipboard/drag/dictionary adapter | Shared kill buffer, word drag/reversal and stationary edge scrolling verified; broaden actual Cocoa multi-click/vertical-pointer/IME/VoiceOver/complex bidi/scale/menu acceptance; preferences/tools/page-prompt editors are also migrated; inherited-browser fields and remaining controls still need inventory |
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

## Historical headless policy (superseded by the full GUI checkpoints)

The following records describe the earlier headless phase; the current execution
policy above supersedes it. At that time, development ran without windowed tests.
Rust/Python checks and compilation continued while browser/manual/presentation
checks awaited an explicit checkpoint. The earlier palette-chrome stage
(`3cd95d8`, 43 Metal / 17 Views cases) was the last complete GUI acceptance then.
The following checkpoint requirements were recorded before the complete repeat:

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

- Import source selection now uses Rust state, prefix matching/timeouts, popup
  tokens and macOS menu-key policy, with one Metal canvas for the closed control.
  Native menu and IME/AX/focus/event protocols remain. **140 Rust and 61 Python
  tests passed** without windows; the compiled new regression brings the deferred
  matrix to **69 Metal cases and 34 Views controls**. See
  [source selector verification](benchmarks/rust-source-picker-headless-2026-10-23.json).
  Native popup/keyboard/IME/accessibility behavior and actual rendering await the
  checkpoint; this stage establishes no presentation-performance gain.

- Workspace assignment and managed-profile selectors now use Rust stable-ID
  reconciliation and the shared Metal selector recipe. Menus capture their own
  keys/generation, including close callbacks; replaced models cannot reuse a new
  popup's token. Native popup/IME/AX/focus and profile services remain.
  **148 Rust and 61 Python tests passed** without windows; the new compiled case
  brings the deferred matrix to **70 Metal cases and 34 Views controls**. See
  [profile selector verification](benchmarks/rust-profile-pickers-headless-2026-10-23.json).
  Actual popup behavior, rendering, persistence and CPU/GPU/presentation comparisons
  remain pending; this stage does not complete functional parity.

- The complete compiled automated UI inventory now passes in both backends:
  **116 Metal cases and 86 Views cases**, plus six explicit opt-in skips per
  backend, without retries. It covers the entire previously deferred 70/34
  matrix. The checkpoint repaired lazy-tab realization caused by UI identity
  extraction, hidden-window input/Metal lifecycle, emoji IME updates, alert
  roles and main-window Cocoa plain-key/keyup routing, and corrected stale
  fixture/navigation/focus/animation assumptions. **148 Rust and 61 Python
  tests**, optimized app/test compilation, scoped GN/boundaries and a no-work
  Ninja check also passed. See
  [the full checkpoint report](benchmarks/rust-ui-checkpoint-2026-10-23.json).
  This establishes current automated behavior; full Rust ownership, manual
  signed-app acceptance and actual performance comparison are still incomplete.

- Editor foundation (`4475ee7`): replacement UTF-16 state, delta history, atomic
  token-guarded composition and CXX bridge, plus shared-font CoreText geometry.
  **175 Rust / 61 Python tests and the native geometry/raster harness passed**;
  the full automated UI repeat passed **116 Metal / 86 Views**, six explicit skips
  each. Live editors remain Views; this is not field migration acceptance. See
  [the matching report](benchmarks/rust-editor-foundation-checkpoint-2026-10-23.json).

- Live workspace/profile name editors: Rust UTF-16 model, revisioned boundaries,
  history/IME and viewport/scene; CoreText/Metal text, selection, clauses and caret;
  one native input/focus/AX/menu/dictionary/clipboard/drag adapter per field. The
  Views baseline retains native Textfields. **182 Rust / 61 Python and full UI
  119 Metal / 87 Views passed**, six explicit skips each. Native uncommon commands,
  text offsets/manual acceptance, other editors and actual profiling remain open.
  [Matching report](benchmarks/rust-name-editors-checkpoint-2026-10-23.json).

- macOS editor command/selection checkpoint: Retain/Extend/Caret, undirected
  first-motion resolution, page/paragraph/document movement, shared Rust kill/
  yank and transpose; forced CoreText direction/cache keys and normalized RTL
  whitespace coordinates. Actual native chords and shared command cases pass in
  both backends. **201 Rust / 61 Python and full 121 Metal / 89 Views passed**,
  six skips each. Transitional native/Rust kill interoperability and remaining
  drag/IME/VoiceOver/bidi/menu/scale acceptance stay open. Native unsupported
  mark/unselect and Windows-only offset arrays are not presumed macOS gaps.
  [Source and binary evidence](benchmarks/rust-editor-commands-checkpoint-2026-10-23.json).

- **Shared native kill-buffer service (2026-10-23):** Rust no longer owns a shadow
  buffer. Fixed-size pre-edit ranges/revisions let its native adapter publish to
  the existing Views store without another fragment allocation or editing model.
  Actual Cocoa kill/yank works Rust-to-Views and Views-to-Rust, including Unicode,
  empty/readonly cases. The guarded fork API is integrated into standard overlay
  and isolated staging, with conflict/idempotence/rollback coverage. No new
  production library or crate. Full rerun: **122 Metal / 90 Views**, six skips
  each; **203 Rust / 66 committed Python**. The first Metal teardown timeout
  remains recorded. [Report](benchmarks/rust-shared-kill-checkpoint-2026-10-23.json).

- **Pointer selection and edge scrolling (2026-10-23):** Rust owns word expansion,
  original-word retention through reversals and revision-bound pointer sessions.
  Native code adapts capture/coordinates and a 100ms stationary edge-scroll timer,
  stopped on release/capture/focus/context/lifecycle changes. Triple-click behavior
  matches the native single-line control. Shared synthetic pointer tests and eight
  new model cases pass, with **211 Rust / 66 committed Python** and full **124 Metal
  / 92 Views**, six explicit skips each. Actual Cocoa multi-click/vertical pointer,
  complex bidi/manual editor acceptance and all other migration areas remain.
  [Evidence](benchmarks/rust-editor-pointer-checkpoint-2026-10-23.json).

- **Live color/emoji editors and commit callbacks (2026-10-23):** Both fields use
  Rust editing/CoreText/Metal under the opt-in backend, preserving preset-row Rust
  layout/input and the original workspace settings services. The explicit commit/
  key contract excludes preedit/cancel/programmatic resets, supports synchronous
  first-grapheme normalization and leaves color submission on Return. Views remains
  one native Textfield per field. New shared tests cover callback lifecycle,
  actual Cocoa validation/typing, synthetic IME and workspace isolation; full
  **126 Metal / 94 Views**, six skips each, **211 Rust / 66 committed Python**.
  Folder/address, remaining UI, manual IME/VoiceOver and actual profiling remain.
  [Evidence](benchmarks/rust-appearance-editors-checkpoint-2026-10-23.json).

- **Live modal folder editor (2026-10-23):** The Metal folder form uses the shared
  Rust/CoreText/Metal editor with committed-edit/error callbacks and explicit modal
  focus routing. Original Views retains its actual DialogModel Textfield. Actual
  Cocoa typing/Return, Unicode, IME cancel/modal lifetime and existing folder/tree/
  menu/persistence/rollback/stale-target cases pass. Full **127 Metal / 95 Views**,
  six skips each, **66 committed Python**; Rust sources match the previous 211-test
  run. Address/settings/service fields, remaining containers/forms, manual and
  performance acceptance remain. [Evidence](benchmarks/rust-folder-editor-checkpoint-2026-10-23.json).

- **Address/palette Rust editor (2026-10-23):** Input state/drawing migrates with
  larger borderless presentation and explicit committed/key callbacks. Suggestions,
  keyword/command routing, actual navigation, profile/preconnect and stale-result
  handling remain. Preedit is excluded from dispatch/results. Same-value insertion
  notifies UI without redundant undo/revision changes. Full **128 Metal / 96 Views**,
  six skips each, **214 Rust / 66 committed Python**, 180 SDK font/direction cases
  plus raster passed. Initial Views synthetic IME key routing was corrected at the
  application policy seam and remains recorded; real candidate windows/VoiceOver,
  remaining settings/services/containers, performance and release checks stay open.
  [Evidence](benchmarks/rust-address-editor-checkpoint-2026-10-23.json).

- **Empty marked-text termination (2026-10-23):** Both platform adapters end active
  preedit and preserve idle text/selection on empty updates, without domain commits.
  Native callbacks are suppressed through marked updates; Rust composition range
  availability matches native editable/readonly behavior. A subsequent normal
  confirmation still commits. Shared tests cover emoji autosave protection. Full
  **129 Metal / 97 Views**, six skips each, **66 committed Python**; unchanged Rust/
  geometry match prior unit/SDK evidence. Broader native termination/menu/focus,
  candidate windows/VoiceOver, remaining fields/containers and performance/release
  work stay open. [Evidence](benchmarks/rust-ime-empty-marked-checkpoint-2026-10-23.json).

## Settings text-input milestone

Eight preferences fields now use the same Rust/Metal editor contract as the palette and workspace fields, retaining one real native Textfield per field in the Views baseline. Committed callbacks own search/dirty tracking; composing keys and save buttons cannot persist preedit. Unrelated refreshes preserve drafts/composition, while opening another profile reloads its authoritative values. The actual Cocoa/IME/profile test and complete matrix passed **130 Metal / 98 Views**, with six explicit skips per backend. Controls, containers, remaining service fields, manual platform acceptance and performance comparison remain open. [Evidence](benchmarks/rust-preference-editors-checkpoint-2026-10-23.json).

## Tools text-input milestone

All seven tools editor roles now use EditorControl: snapshot name, timeline filter, history query, shortcut chord and provider name/keyword/URL. WorkspaceShell no longer inherits TextfieldController. History/timeline keep a committed service query independent of marked text; a pending history request can finish during preedit without losing the committed query on cancellation. Deferred actions carry a form generation and workspace/profile identity. Recording exposes no text input while still receiving native accelerators and restores editing after capture/cancel.

The 17-case gate and full **133 Metal / 101 Views** matrix passed in both backends, six explicit skips each. Three new scenarios cover actual Cocoa entry/save, composing buttons/keys, client destruction, stale-form/profile writes, committed history during preedit and reserved/valid/cancelled shortcut capture. Forms/containers, page prompt, broader OS acceptance and profiling remain open. [Evidence](benchmarks/rust-tools-editors-checkpoint-2026-10-23.json).

## Page prompt editor milestone

JavaScript prompt editing uses the same Rust/Metal control, native font metric and live theme-color defaults. Page origin/profile identity, request-frame/navigation/visibility cancellation and beforeunload defaults remain native domain boundaries. Composing UI acceptance stays open; explicit automation override preserves raw UTF-16. Editor, acceptance, close and posted-key callbacks carry a dialog generation so an old prompt cannot answer its replacement. Negative answers detach callbacks and cancel marked text; client-owned synchronous widget destruction automatically handles forced native closure once before explicit cleanup.

The 30-case gate and full **137 Metal / 105 Views** matrix passed, with six explicit skips each. Actual Cocoa beforeunload Return stays open on macOS, while Escape preserves both tabs/draft by canceling, including repeated dialogs. Unicode prompt input/Return/Escape, long-message bounds, composing acceptance, override/stale/CloseNow lifecycle and hidden-page/subframe cancellation are covered. The initial cross-profile fixture lacked an explicit navigation before EvalJs; NavigateBoth fixes that precondition in both backends. That fixture repair changed no production code. A later beforeunload test incorrectly assumed that Return activates Cancel on macOS. The native PlatformStyle intentionally prevents that; the corrected test requires Return to stay open and Escape to cancel. A separate repeated-dialog Escape timeout required registered native accelerator dispatch for textless dialogs. Unregistered Return stays unchanged and active editors retain IME precedence. Native sheet lifetime failures in the first full run were fixed with client-owned page/deletion widgets and synchronous C++ destruction; native animations remain enabled. The Views modal/form/label/button/scroll layer, broader OS acceptance and profiling remain open. [Evidence](benchmarks/rust-page-prompt-editor-checkpoint-2026-10-24.json).

## Preference selector milestone

Eleven selectors use Rust state and select layout/scene recipes with shared-context
Metal: theme, default search, compact category, zoom, three font-size choices,
DNS mode, site type/policy and clear period. Nil selection supports Chromium
values outside the preset list. Provider keys invalidate old interactions even
when labels duplicate. Unchanged refreshes preserve prefix/menu interaction;
profile changes cancel it before synchronizing new values. Native popup/input/
focus/AX/typography adapters and stores remain explicit boundaries.

The full checkpoint and remaining native container/control scope are described in
[the current report](benchmarks/rust-preference-selectors-checkpoint-2026-10-24.json). Actual popup/candidate-window,
VoiceOver, complex bidi/scale/gesture and signed-app acceptance, as well as real
CPU/GPU/presentation profiling, remain open.


## Preference switch and clear-data milestone

Twenty-two profile switches and two global switches use Rust checked/animation
state and clipped Metal primitives. Native 26x16 track geometry is retained inside
an expanded 38x28 focus/hover target. Three clear-data checkboxes reuse the Rust
checkbox model and one Metal canvas. Native Button event/focus/AX nodes, stores,
permission services, profile binding and deletion confirmation remain adapters.

The complete automated checkpoint passed 142 Metal / 108 Views regular cases and
four default-30-trial optional scenarios per backend (146/112 combined). Actual
Cocoa input, global/profile scope, durable cookie/localStorage/IndexedDB deletion,
profile isolation, disabled admission, stale press cancellation, compositor
viewport exclusion/reappearance and idle timer shutdown are covered. Remaining
buttons/sections/controllers/containers, manual platform acceptance, device-loss
fallback acceptance and controlled CPU/GPU/presentation profiling remain open.
[Evidence](benchmarks/rust-preference-switches-checkpoint-2026-10-24.json).


## Preference action milestone

Nineteen fixed actions plus dynamic site-rule Remove/More controls reuse Rust
input, action layout and primitive recipes with Metal text and CPU fallback.
Native event/focus/AX adapters and original stores, IME/managed admission, platform
services and deferred profile-bound callbacks remain. Horizontal row actions
preserve intrinsic measurement; the More list action retains stretch. The first
version's group measurement regression is fixed without weakening the original
editor/selector visibility and final-font reachability checks.

The checkpoint passed 145 Metal / 110 Views regular cases and four optional
scenarios each (149/114 combined), including real Cocoa save/pagination,
IME/disabled admission, busy labels, deferred cross-profile removal and
hide/offscreen cancellation. Ten category controls, header close, section/row
chrome, form state, native nodes/containers, other service UI, manual platform
acceptance, release checks and real CPU/GPU/presentation profiling remain.
[Evidence](benchmarks/rust-preference-actions-checkpoint-2026-10-24.json).


## Preference navigation and filter controller milestone

Ten category nodes share one Metal canvas and header dismiss has another. Rust
owns category/query/row/section/navigation visibility, consuming the same native
trimmed/lowercased UTF-16 units as the Views baseline. Dynamic rule display keys
are refreshed per filter. Native event/focus/AX, typography, normalization,
placement and browser service/authorization adapters remain. Analytic Rust
symbols replace native icon paths; the CPU and GPU recipe is shared.

The critical gate passed 14 Metal / 10 Views; the full regular matrix passed
147/111, plus four optional scenarios each (151/115 combined). Real Cocoa
category/search/compact-choice synchronization and dismiss, profile/resize/hide
cancellation, query retention and all prior forms/editors/services pass. No new
crate/library was introduced. Header/branding/section/row/status text, data,
draft/admission/busy/form state, other service UI, native nodes/containers,
manual platform/reattachment acceptance and controlled CPU/GPU/presentation
profiling remain. [Evidence](benchmarks/rust-preference-navigation-checkpoint-2026-10-24.json).


## Preference text and backing milestone

Preference branding/header/section/row/status text and rounded row/navigation
backing now use Rust recipes in one additional transparent Metal underlay.
Interactive control cutouts are subtracted from primitive clips; actual Cocoa
ordering keeps it below existing control canvases. No native Label remains in
the Rust preference tree. Native static-text AX/layout nodes, wrapping and font
metrics remain adapters. Exact font weights are cached and rendered in Metal.
Navigation CPU backdrop painting remains for control fallback; row/text fallback
is retained. This is not removal of all native views or CPU painting.

The critical gate passed 16 Metal / 11 Views. Native Label geometry comparisons
at three widths, dynamic Unicode status/profile text, theme/filter/scroll,
last-editor visibility, control cutouts and underlay order pass. Full regular
execution passed 149/112, plus four optional scenarios each (153/116 combined).
The first Views matrix had one native teardown timeout; the unchanged isolated
test and full Views rerun passed. That initial failure remains recorded.
Rust tests passed 225; SDK geometry 180 plus raster; clean Python 66; GN and
boundaries passed. Initial typography/visibility/presentation/scroll failures
remain recorded alongside the final successful run. UI data/draft/form state,
other surfaces/native containers, manual acceptance, controlled profiling and
release/license acceptance remain open.
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
