# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

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

## Immediate preference work still remaining

Next, move preference row/column and outer allocation from the remaining
BoxLayouts into Rust, including centered row controls, wrapping labels, flexible
columns and scrolling constraints. Compare dimensions with Views before replacing
interaction/container ownership. Finish header/profile/status and managed-state
copy and coordination alongside that work.

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Static categories and all
52 row descriptions/types/options/order now use a Rust catalogue. Migrate remaining chrome/dynamic UI data and busy/status/form
coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Rust now also owns command/IME admission, recording transitions,
action outcomes, history result presentation and the clear-busy UI projection.
Timeline choices and shortcut/provider labels, active selection and draft-field
plans now also use Rust. A Rust node plan defines all four tools forms.
Remaining status coordination and service/progress wording remain open,
alongside other panels. History result text/backing
and column placement use Rust/Metal; native row interaction/AX and scrolling remain adapters.
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
   Static category/row descriptions, kinds/options/order now use Rust. Migrate
   remaining chrome/dynamic UI data and busy/status/form coordination,
   together with remaining tools status/service coordination, some service/
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
