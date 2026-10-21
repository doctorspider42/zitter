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
| Live tab/pin/folder/split text and card drawing | Rust display list + Metal; CoreText text shaping | Match focus, hover, disabled and theme states; remove duplicate native painting safely |
| Row title/detail/close geometry | Rust in opt-in backend, shared with display list | Replace remaining native row containers and typography measurement adapter |
| Row drag threshold, latched drag, cancellation and close visibility | Rust in opt-in backend; native event delivery | Native drag-to-split verified; capture-loss unit coverage; migrate target policy |
| Next/previous tab wrapping | Rust; existing configurable key dispatch | Preserve page shortcut precedence and editing/IME behavior |
| Tab list hierarchy, indentation, split tile placement | Views | Rust layout from stable tab/pin identities, retaining scroll and AX correctness |
| Scroll, viewport, scrollbar | Native ScrollView/Cocoa scrollbar | Native input adapter + Rust offset/clipping; native wheel and trackpad coverage |
| Selection, close/reopen, title changes, pinned navigation | Existing application callbacks; native interaction nodes | Rust control actions into the same callbacks; persistence and last-tab rules |
| Folder expand/create/rename/delete; nested pins | Existing application + Views | Rust interaction state and layout; native menu/editing adapters; rollback tests |
| Tab/pin context menus and drag to split | Views event routing + application commands | Rust target/action routing; retain native menus and drop policy |
| Address pill and New Tab action | Views | Rust layout/drawing/events; configurable shortcut hints |
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
