# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `e539b17f92c8395368f8a20bd8fe87df94b12e58`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 161 | 6 | 0 |
| Views | 120 | 6 | 0 |

The inventory contains 167 cases; Views excludes 41 dedicated Metal cases
(126 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The tools/history/provider/shared preference-chrome gate passed **24 Metal /
19 Views**. Checks passed **233 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-history-rows-checkpoint-2026-10-25.json).

History result rows now use Rust text/background/focus recipes on the existing
shared tools Metal underlay. There are no row Label children or per-row Metal
drawables. Rust column layout keeps 60px rows and 4px spacing. The native 36px
text lead, measured/capped primary column, 10px gap, tail title and middle
date/URL elision remain. Native event/focus/AX nodes, formatting/font services
and profile-bound posted navigation callbacks remain adapters. Views retains
its original RowButton/Label/BoxLayout baseline. CPU fallback uses the same Rust
recipe, including font sizes, title/detail elision and focus drawing, while the
shared underlay is absent.

Only visible projected rows enter the scene; other native controls remain
cutouts. Bounds/paint/input/theme/scroll changes refresh the shared layer.
Explicit scroll synchronization and release visibility guards cancel offscreen
input; hidden rows and widget hide also cancel pending input. The new shared
browser test seeds real history and verifies Unicode/empty-title URL fallback,
AX name/description, row geometry and real Cocoa Space opening the expected
new-tab URL. A dedicated Metal test seeds 80 rows and verifies scene text/order,
clipping/culling, scroll and hide press cancellation, reconstruction, dark theme
and profile isolation. Existing history/search/clear/paging, controllers,
preferences and other browser-service contracts remain in the full matrix.

The initial probe exposed offscreen-release navigation. Explicit scroll
synchronization and release visibility checks fixed it, preserving the
no-navigation assertion. Failed probes and the native tooltip-URL AX/focus-ring
test corrections remain recorded in the report.

The prior [tools controller checkpoint](benchmarks/rust-tools-form-checkpoint-2026-10-25.json)
retains page/recording/committed-query/pagination/loaded-state evidence. The
[tools copy checkpoint](benchmarks/rust-tools-chrome-checkpoint-2026-10-25.json)
retains shared text geometry/cutouts and earlier optional performance coverage.
Native row interaction nodes, scrolling and outer Views containers remain;
rendering alone does not complete that ownership migration. Tools action/status
coordination, busy state, other forms/service panels, manual acceptance,
profiling and release/license checks remain open. Normal installed apps/profiles
were untouched; no bundle was signed.

## Immediate preference work still remaining

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Migrate UI descriptions/data
and remaining busy/status/form coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Remaining action/status decisions, busy state, some popup/empty-
item/service wording remain, alongside other panels. History result text/backing
and column placement use Rust/Metal; native row interaction/AX and scrolling
remain adapters.
Native ScrollView, BoxLayout, View groups, event/focus/AX adapters and outer
placement still require an explicit ownership decision and verification.

## Next implementation steps

1. **Text editing and IME acceptance.** The inventoried custom fields, including
   page prompts, use the shared editor adapter. Complete native candidate-window,
   menu/undo/focus/hide, VoiceOver, multi-click/drag, complex bidi and scale/theme
   acceptance. Continue inventorying inherited Chromium fields. Preserve native
   IME, clipboard and accessibility services as explicit platform adapters.
2. **Finish migrated forms and dialogs.** Replace remaining header/close/modal
   controls and outer placement for workspace/profile/import/folder/deletion
   surfaces. Finish the address/command palette input/frame/container layer.
   Verify stale context, defaults, cancellation, validation and persistence.
3. **Preferences.** Editors, eleven selectors, 24 switches, three clear-data
   checkboxes, fixed/dynamic actions, category/dismiss controls and category/filter
   state, text/backing, text-draft refresh/admission and pagination use Rust/Metal.
   Migrate UI descriptions/data and remaining busy/status/form coordination,
   together with remaining tools action/status decisions, busy state and native
   row interaction nodes. Tools page data/copy rendering, five selectors, fourteen
   actions and dismiss already use Rust/Metal. Tools page/recording/query/pagination/loaded state
   also uses Rust.
   Preserve native stores, managed-setting checks, profile/global scope,
   site-policy validation and data-clearing confirmation authorization.
4. **Browser service panels.** Finish history native interaction/list/scroll
   ownership and remaining service coordination. Result text/backing and column
   layout use Rust/Metal. Migrate downloads/passwords/permissions UI and Time
   Machine previews/restore. Preserve real service data, stale-response handling,
   confirmation and failure/rollback behavior.
   Complete Peek/promote/split and link/page menu UI routing.
5. **Remove Views interaction/container ownership.** Replace remaining tab/tree,
   scroll/viewport, native Button/View/ResizeArea hierarchy, outer layout and
   animation-snapshot dependencies with stable Rust node state. Keep Chromium
   WebContents, domain commands and explicit native event/window/cursor/menu/AX
   adapters. Verify native hit testing, focus order, scroll/trackpad, drag/split,
   workspace gestures, RTL, scale/theme and missing-drawable fallback.
6. **Measure and select the final rendering path.** Compare equivalent real Views
   and Rust/Metal workloads using CPU profiles, GPU timings and actual drawable
   presentation timestamps. Include cold/warm behavior, text/cache uploads,
   selection, scrolling, resize, workspace/profile switching, frame misses,
   memory and idle work. Compilation, unit durations and canvas counts prove no
   end-to-end speedup.
7. **Release acceptance.** Repeat the complete regression matrix on final code,
   inspect and sign the matching isolated bundle, verify persistent sessions and
   profile isolation/rollback, then validate the shipped dependency/license
   manifest and Apache-2.0/upstream notices. Keep the opt-in backend until parity
   and the performance comparison are established.

## Empty marked-text adapter contract now verified

Empty platform marked text ends existing composition and is otherwise a no-op.
Native preedit/cancellation never reaches domain commit callbacks through this
entry point, and idle updates cannot delete a selection. Rust range availability
matches native editable/readonly behavior. The underlying Rust transaction model
and its empty-preedit capability are unchanged.
[Implementation and remaining editor gaps](rust-metal-editor.md).

Next: broaden undo/menu/focus/hide/native candidate-window termination acceptance,
then continue dialog/settings/tools controls/form/container and
service-panel ownership. VoiceOver, multi-click/bidi/pointer/menu/clipboard/scale/
theme/GPU recovery, actual profiling and signed/manual release checks remain.

The seven work areas above remain open, including all remaining form/service UI,
Views container ownership, manual acceptance and real presentation profiling.
Passing the current inventory does not establish a completion percentage.


## Text-input inventory and remaining ownership

All declared WorkspaceShell roles and the separate page JavaScript prompt now use EditorControl in the opt-in backend. Source inventory finds no direct custom prototype Textfield allocation outside the native adapter, except the deliberate Views folder DialogModel baseline. This is not proof that all inherited Chromium UI or all application controls are migrated.

Continue custom-dialog and inherited-browser inventory, broader IME/native service acceptance and dialog/settings/tools form state, pickers, buttons, lists, scrolling and outer container migration. Frame/origin/profile authorization, persisted stores and native IME/clipboard/accessibility/accelerator adapters remain explicit boundaries.
