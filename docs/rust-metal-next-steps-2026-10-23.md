# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `d0f90cf9fe34392f6b79215e11221f03c207dc79`, 2026-10-24, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary profiles.
The first Views matrix had one timeout during native teardown; the unchanged test
then passed alone and in a complete Views rerun. The failed run is retained in
the report; its root cause is not fully established. Final results:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 149 | 6 | 0 |
| Views | 112 | 6 | 0 |

The inventory contains 155 cases; Views excludes 37 dedicated Metal cases
(118 applicable cases). Four optional default-30-trial scenarios then passed on
each backend. **Combined execution is 153 Metal / 116 Views passes**, with real-user
Arc import and the interactive manual window excluded per backend. Metal API
validation remained enabled. These execution results establish no controlled
CPU/GPU/drawable-presentation speedup.

The preference chrome/form/control gate passed **16 Metal / 11 Views**. Checks
passed **225 Rust tests**, **180 SDK font/direction cases plus raster**, **66
clean-archive Python tests**, scoped GN/boundaries and optimized compilation.
The live checkout passed 68 Python tests, including two concurrent cases outside
this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-preference-chrome-checkpoint-2026-10-24.json).

Preference branding/header/section/row/status copy and row/navigation backing now
use Rust recipes and one additional transparent Metal underlay. Rust subtracts
interactive control cutouts; Cocoa ordering keeps the underlay below existing
control canvases. The Rust preference tree has no native Label instances.
Native text wrapping, font metrics and static-text AX/layout nodes remain
adapters. Native minimum line height and long-word handling preserve the original
Label geometry, checked at three widths. Navigation keeps CPU backdrop painting
for control fallback; row/text CPU recipes remain fallback when the canvas is
unavailable. This does not remove all native nodes or CPU drawing.

Dynamic Unicode error/status/profile labels, themes, no-results filtering, scroll
and full last-editor visibility, cutouts, layer order and hide/reopen pass.
Earlier editor/selector/switch/action/navigation and browser-service contracts
remain covered. Normal installed apps/profiles were untouched; no bundle signed.

## Immediate preference work still remaining

Text/backing, category/dismiss controls and category/filter decisions now use the
Rust scene/controller path. Migrate UI descriptions/data and remaining draft/
admission/busy/form state. Preserve normalization, IME, managed-setting admission,
profile-bound confirmation and deferred rule mutation semantics. Tools-provider/
shortcut pickers and browser/service panels remain separate surfaces.
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
   state, preference text and backing now use Rust/Metal. Migrate UI data and draft/
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
