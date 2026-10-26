# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

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

## Immediate preference work still remaining

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Migrate UI descriptions/data
and remaining busy/status/form coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Rust now also owns command/IME admission, recording transitions,
action outcomes, history result presentation and the clear-busy UI projection.
Timeline choice filtering/labels/empty state and target maps now also use Rust.
Remaining other option-to-field/form construction and service/progress
wording remain, alongside other panels. History result text/backing and column
placement use Rust/Metal; native row interaction/AX and scrolling remain adapters.
Tools panel/header/body/clear geometry also uses Rust; their Views mode retains
BoxLayout. Native ScrollView/View groups, other BoxLayouts, event/focus/AX
adapters and outer placement still require ownership migration and verification.

## Next implementation steps

1. **Text editing and IME acceptance.** The inventoried custom fields, including
   page prompts, use the shared editor adapter. Complete native candidate-window,
   menu/undo/focus/hide, VoiceOver, multi-click/drag, complex bidi and scale/theme
   acceptance. Continue inventorying inherited Chromium fields. Preserve native
   IME, clipboard and accessibility services as explicit platform adapters.
2. **Finish migrated forms and dialogs.** Page alert/confirm/prompt/beforeunload
   copy/body text, actions and layout now use Rust/Metal; native title/frame and
   modal containers remain. Folder create/rename/deletion/save errors and workspace
   deletion now also use scene actions and Rust outer-body allocation. Replace
   remaining header/close/modal controls and outer placement for workspace/profile/
   import and other surfaces. Finish the address/command palette input/frame/
   container layer.
   Verify stale context, defaults, cancellation, validation and persistence.
3. **Preferences.** Editors, eleven selectors, 24 switches, three clear-data
   checkboxes, fixed/dynamic actions, category/dismiss controls and category/filter
   state, text/backing, text-draft refresh/admission and pagination use Rust/Metal.
   Migrate UI descriptions/data and remaining busy/status/form coordination,
   together with remaining tools option-to-field/form construction, some service/
   progress wording and native row interaction nodes. Tools page data/copy
   rendering, five selectors, fourteen actions and dismiss already use Rust/Metal.
   Tools page/recording/query/pagination/loaded state also uses Rust.
   Preserve native stores, managed-setting checks, profile/global scope,
   site-policy validation and data-clearing confirmation authorization.
4. **Browser service panels.** Finish history native interaction/list/scroll
   ownership and remaining service coordination. Result text/backing and column
   layout use Rust/Metal. Finish download presentation/interaction ownership;
   finish password/permission and Time Machine presentation/interaction ownership.
   Snapshot/closed-tab choice projection and scoped targets now use Rust. Preserve
   real service data, stale-response handling,
   confirmation and failure/rollback behavior.
   Download text/progress/actions and row/body allocation now use Rust/Metal on
   one shared canvas; native formatting/list policy and control/scroll/window/AX
   nodes remain. Complete Peek/promote/split and link/page menu UI routing.
5. **Remove Views interaction/container ownership.** Tools outer/header/body/
   clear geometry now uses Rust, while native groups/scroll/windows remain.
   Replace remaining tab/tree, scroll/viewport, native Button/View/ResizeArea
   hierarchy, outer layout and
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
