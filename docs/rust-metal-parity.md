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
| Scroll offset normalization, viewport projection and clipping | Rust logical viewport; native ScrollView/Cocoa input/scrollbar adapters | Replace native scroll containers; expand real trackpad/scale coverage |
| Selection, close/reopen, title changes, pinned navigation | Rust row actions into existing application callbacks; native close child and AX action | Migrate close child and remaining command routing; preserve persistence and last-tab rules |
| Folder expand/collapse and tree visibility; create/rename/delete and nested pins | Rust tree state/layout; application persistence and native menu/editing adapters | Rust editing forms; preserve failed writes and validation rollback |
| Tab/pin context menus and drag to split | Views event routing + application commands | Rust target/action routing; retain native menus and drop policy |
| Address pill and New Tab action | Views rendering/layout; Rust New Tab RowButton mouse/key policy | Rust layout/drawing; address events and configurable shortcut hints |
| Navigation controls, workspace dots/emoji and footer | Views | Rust layout/drawing/events; enabled, selected, hover and keyboard focus states |
| Sidebar resizing, pinning, auto-hide, edge reveal | Views | Rust interaction state; preserve content bounds and pointer behavior |
| Workspace swipe/animation/cancellation | Views layers + existing workspace controller | Rust scene transitions without native-painted row snapshots |
| Split headers/divider, focus, orientation, swap/close | Views + application commands | Rust controls/layout; preserve independent WebContents and pane focus |
| Address/search palette, suggestions, providers, IME | Views | Rust palette layout and selection; platform text-input adapter |
| Command Center tabs/pins/workspaces/history/actions | Views | Rust palette and command routing, same application commands |
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
