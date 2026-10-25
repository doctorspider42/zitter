# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `c1cf3333a7767f76cc0ff1ec8afa7c30129b4937`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 158 | 6 | 0 |
| Views | 118 | 6 | 0 |

The inventory contains 164 cases; Views excludes 40 dedicated Metal cases
(124 applicable cases). Four optional default-30-trial scenarios passed on each
backend: **162 Metal / 122 Views passes combined**. Real-user Arc import and the
interactive manual window remain excluded per backend. Metal API validation
remained enabled; these results establish no controlled CPU/GPU/presentation or
memory improvement.

The tools/history/provider and shared preference-chrome gate passed **21 Metal /
17 Views**. Checks passed **229 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-tools-chrome-checkpoint-2026-10-25.json).

Four Rust page descriptors now supply tools titles, intros, field/picker names,
section/action captions, history-range choices and initial status. Rust also
supplies clear-busy text. Only display names cross this copy ABI; native stores,
callback IDs, validation, IME and profile/workspace/generation checks remain.
One additional transparent Metal underlay draws header/body/status copy, sleeping
descriptions and empty/error history text. Native control/result rectangles are
holes, with the shared underlay below foreground canvases. There are no native
Labels outside controls in the Rust tools tree. Native history-row labels and
native static-text AX/layout nodes remain.

The same lightweight copy adapter serves preferences and tools. A standalone
cached gfx::RenderText supplies Label-compatible cursor-disabled, unelided,
truncated-long-word boundaries and native minimum line height. Rust places the
lines; CoreText/Metal presents them. No hidden Label measures or draws text.
The first probe found 300px instead of native 320px for two long descriptions
at width 80. The native oracle was preserved; replacing approximate wrapping
fixed it. The failed probe remains recorded. Tests compare four page titles/
descriptions and real Unicode multiline status at widths 80/160/320, and verify
page/profile changes, scene text, layer order, interactive/native-result holes,
scrolling, theme, hide/reopen and history error copy.

Prior editors/selectors/switches/actions, preference chrome/draft/pagination and
browser-service contracts remain covered by the complete matrix.
[Tools actions](benchmarks/rust-tools-actions-checkpoint-2026-10-25.json),
[tools selectors](benchmarks/rust-tools-choices-checkpoint-2026-10-25.json),
[preference drafts](benchmarks/rust-preference-drafts-checkpoint-2026-10-25.json)
and [preference chrome](benchmarks/rust-preference-chrome-checkpoint-2026-10-24.json)
retain their detailed evidence and earlier failed probes.
CPU fallback/native background painting and native interaction/layout nodes
remain. Normal installed apps/profiles were untouched; no bundle was signed.

## Immediate preference work still remaining

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Migrate UI descriptions/data
and remaining busy/status/form coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Remaining form/status decisions, some popup/empty-item/
service wording and native result rows remain, alongside other service panels.
Native ScrollView, BoxLayout, View groups, event/focus/AX adapters and outer placement still require
an explicit ownership decision and verification.

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
   together with remaining tools form/status decisions and native result rows;
   tools page data/copy rendering, five selectors, fourteen actions and dismiss
   already use Rust/Metal.
   Preserve native stores, managed-setting checks, profile/global scope,
   site-policy validation and data-clearing confirmation authorization.
4. **Browser service panels.** Migrate remaining history/search/pagination,
   downloads/passwords/permissions UI and Time Machine previews/restore. Preserve
   real service
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
