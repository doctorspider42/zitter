# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `67bb21f11a769d47f2e6c0fcebfc707b84d84503`, 2026-10-24, passed the full compiled
regression inventory with one launcher job, no retries and temporary profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 142 | 6 | 0 |
| Views | 108 | 6 | 0 |

The inventory contains 148 cases; Views excludes 34 dedicated Metal cases
(114 applicable cases). Four opt-in performance scenarios also passed separately
on each backend with their default 30 measured trials. **Combined coverage is
146 Metal / 112 Views passes**, with real-user Arc import and the interactive
manual window excluded per backend. Metal API validation remained enabled.
These are execution results, not controlled CPU/GPU/presentation comparisons.

The switch/clear-data critical gate passed **4 Metal / 3 Views**. Fresh checks
passed **220 Rust tests**, **180 SDK font/direction cases plus raster**, **66
Python from a clean archive**, scoped GN/boundaries and optimized compilation.
The live checkout passed 68 Python cases including two concurrent cases outside
this UI commit. Final Ninja reported no work to do.
[Results, source/binary identities and retained initial failure](benchmarks/rust-preference-switches-checkpoint-2026-10-24.json).

Twenty-two profile switches, two global switches and three clear-data checkboxes
now use Rust checked state and Metal display lists, retaining native event/focus/
AX adapters and original preference, permission, confirmation and storage services.
Timed switch interpolation respects reduced motion and stops offscreen or hidden;
CPU fallback requests repaint when Metal is unavailable. The native 26x16 track
sits inside a 38x28 target to contain focus/hover drawing. Cocoa keyboard, profile/
global scope, actual cookies/localStorage/IndexedDB deletion and isolation,
press cancellation, viewport exclusion/reappearance and idle timer shutdown are
covered. A discovered native Button AX override bug is fixed: checked state
now remains model-owned while pressed, disabled, cancelled or hidden. The
initial misdiagnosis, failed reopening probe and final regression are retained.

Eleven preference selectors and the custom text editors remain on the previously
verified Rust/Metal path. Native buttons, sections, forms/controllers, containers,
platform acceptance and real profiling remain open. The installed app and normal
profiles were untouched; no bundle was packaged or signed.

## Immediate preference controls still to migrate

The source inventory at this checkpoint contains **19 fixed MdTextButton
instances**: six text-setting Save buttons, ten Open actions, download-directory
selection, site-rule Save and Clear. It also retains ten category RowButtons,
one header Close GlyphButton, variable per-rule Remove buttons and the
conditional More exceptions button. These are source counts for preferences,
not an estimate of the entire application's remaining UI.

Next, move these action labels/layout/input recipes to shared Rust scene controls
while retaining managed-setting enablement, text/IME commit admission, dynamic
busy labels, platform directory/proxy/certificate services, profile-bound
confirmation and deferred removal/pagination callbacks. Then move section/title/
status/row chrome and filtering/category/form state. The current ScrollView,
BoxLayout and native event/focus/AX nodes remain a separate container milestone.

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
   use Rust/Metal, along with 24 switches and three clear-data checkboxes. Migrate
   the remaining buttons, section navigation and form/controller state, along with
   tools-provider/shortcut pickers. Keep native stores, managed-setting checks, profile/global scope,
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
