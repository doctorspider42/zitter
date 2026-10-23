# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `651bc58cc631dda054d6209ef7c1bdf7e00b9747`, 2026-10-24, passed the full compiled
regression inventory with one launcher job, no retries and temporary profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 140 | 6 | 0 |
| Views | 107 | 6 | 0 |

The inventory contains 146 cases; Views excludes 33 dedicated Metal cases
(113 applicable cases). The four opt-in performance scenarios then passed
separately on each backend with their default 30 measured trials. **Combined
coverage is 144 Metal / 111 Views passes**, with two exclusions per backend:
real-user Arc import and the interactive manual window. Metal API validation
remained enabled. These samples are execution coverage, not controlled
CPU/GPU/drawable-presentation comparisons.

The selector/editor gate passed **37 Metal / 34 Views**. Fresh checks passed:
**216 Rust tests**, **180 SDK font/direction cases plus raster**, **66 Python
from a clean archive**, scoped GN/boundaries and optimized compilation. The live
checkout passed 68 Python cases, including two concurrent cases outside this UI
commit. The 61 editor model cases are included in the Rust suite; the separate
standalone model run was not repeated. Final Ninja reported no work to do.
[Results, commands, source/binary identities and retained failures](benchmarks/rust-preference-selectors-checkpoint-2026-10-24.json).

Eleven preference selectors now use Rust selection/prefix/menu-token state and
layout/display lists with Metal and matching CPU fallback. Native input, window,
focus, typography, AX, popup menus and browser preference/storage services remain
adapters. A blank choice remains distinct from the first preset. Actual Cocoa
input covers unsupported font values and profile scope; site-policy rebuilding,
clear-data confirmation admission, provider identity, hide/reopen and scrolling
are also covered.

The checkpoint fixes two discovered native boundaries: a Views Combobox key
CHECK for a nonempty model with no selection, and stale preferences scroll
content height after category/search changes. Native compositor scrolling waits
for the resized layout's successful presentation in the fixture. Earlier failed
runs are retained in the report. No normal installed app/profile was changed,
and no bundle was packaged or signed.

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
3. **Preferences.** Eleven preference choice selectors and the text fields now
   use Rust/Metal. Migrate the remaining switches, checkboxes, buttons, section
   navigation and form/controller state, along with tools-provider/shortcut
   pickers. Keep native stores, managed-setting checks, profile/global scope,
   site-policy validation and data-clearing confirmation authorization.
4. **Browser service panels.** Migrate history/search/pagination, downloads,
   passwords/permissions and Time Machine previews/restore. Preserve real service
   data, stale-response handling, confirmation and failure/rollback behavior.
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
