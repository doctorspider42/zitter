# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `209ca2dfcd0fc3e29acb236c9443e5640f8342ae`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 159 | 6 | 0 |
| Views | 119 | 6 | 0 |

The inventory contains 165 cases; Views excludes 40 dedicated Metal cases
(125 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
passed in the previous checkpoint; those results are not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The tools/history/provider/shared preference-chrome gate passed **22 Metal /
18 Views**. Checks passed **232 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-tools-form-checkpoint-2026-10-25.json).

Rust ToolsForm now owns selected page, shortcut-recording state, committed
history/timeline queries, history limit and loaded state. History starts at 200;
More adds 200 with saturation. Edits/requests clear loaded state. Matching query/
limit responses mark loaded, including errors; stale query/limit results do not.
Native visibility/current-profile checks run before Rust admission. Native
Unicode services normalize committed UTF-16; IME preedit remains excluded from
query edits. Page rebuild clears transient queries/recording/loaded state.
Native timers, service calls, accelerator policy, stores and posted-action
workspace/profile/generation guards remain adapters.

The new shared browser test verifies error completion, wrong query/limit/profile,
Unicode query edits during debounce, whitespace normalization, valid replacement,
hidden-page rejection, page clamping and reconstruction. Existing real 205-row
pagination, committed-query/preedit, timeline filtering/identity, Cocoa recorder,
provider/actions and Metal rendering/cutout tests remain in the gate.
No rendering recipe or dependency changed.

Four Rust tools page descriptors and shared Metal header/body/status copy were
verified in the [previous checkpoint](benchmarks/rust-tools-chrome-checkpoint-2026-10-25.json),
including text geometry, underlay ordering and native control/result cutouts.
Editors/selectors/switches/actions, preference chrome/drafts/pagination and
browser-service contracts remain covered by the full matrix. Native history
result rows, action/status decisions, busy state and other form/service UI,
container ownership and manual acceptance remain open. Normal installed apps/
profiles were untouched; no bundle was signed.

## Immediate preference work still remaining

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Migrate UI descriptions/data
and remaining busy/status/form coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Remaining action/status decisions, busy state, some popup/empty-
item/service wording and native result rows remain, alongside other panels.
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
   result rows. Tools page data/copy rendering, five selectors, fourteen actions and dismiss
   already use Rust/Metal. Tools page/recording/query/pagination/loaded state
   also uses Rust.
   Preserve native stores, managed-setting checks, profile/global scope,
   site-policy validation and data-clearing confirmation authorization.
4. **Browser service panels.** Migrate remaining history result/list presentation
   and service coordination, downloads/passwords/permissions UI and Time Machine
   previews/restore. Preserve real service data, stale-response handling,
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
