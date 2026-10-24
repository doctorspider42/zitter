# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `84bbc52875079a96d2cc6750e5f9b328bcb11cbc`, 2026-10-24, passed the full compiled regression
inventory with one launcher job, no retries and temporary profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 147 | 6 | 0 |
| Views | 111 | 6 | 0 |

The inventory contains 153 cases; Views excludes 36 dedicated Metal cases
(117 applicable cases). Four optional default-30-trial scenarios then passed
on each backend. **Combined execution is 151 Metal / 115 Views passes**, with
real-user Arc import and the interactive manual window excluded per backend.
Metal API validation remained enabled. These are execution results, not
controlled CPU/GPU/drawable-presentation speedup measurements.

The preference navigation/form/action/editor/selector gate passed **14 Metal /
10 Views**. Checks passed **223 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-preference-navigation-checkpoint-2026-10-24.json).

Ten category controls share one Metal canvas; the dismiss control uses another.
Rust PreferencesForm owns selection, normalized UTF-16 query and row/section/
navigation visibility. Native Unicode normalization, event/focus/AX, placement,
pref stores and domain services remain adapters. Category/search/compact-picker
synchronization, Cocoa activation/dismissal, profile/resize/hide cancellation,
query retention and scene text are verified. Analytic Rust symbols preserve the
semantic icon mapping with the same CPU/GPU recipe; they are newly drawn symbols
rather than pixel-identical native vector paths. Dedicated detach/reattach,
complete RTL/typography/platform/device-loss and manual acceptance remain open.

Previously migrated editors, eleven selectors, 24 switches, three clear-data
checkboxes, 19 fixed actions and dynamic rule actions remain verified in the full
matrix. Normal installed apps/profiles were untouched. No bundle was signed.

## Immediate preference work still remaining

Category controls, dismiss, category selection and filter decisions now use the
Rust scene/controller path. Next migrate header/branding/section/row/status text,
row surface chrome, UI descriptions/data and remaining draft/admission/busy/form
state. Preserve normalization, IME and managed-setting admission, native platform
services, profile-bound confirmation and deferred rule mutation semantics.
Tools-provider/shortcut pickers and browser/service panels are separate surfaces.
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
   state now use Rust/Metal. Migrate remaining text/chrome, UI data and draft/
   admission/busy/form state, together with tools-provider/shortcut pickers.
   Preserve native stores, managed-setting checks, profile/global scope,
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
