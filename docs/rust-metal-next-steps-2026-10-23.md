# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `4a217fcd9f877e16c38b738b8bc392a2db78c308`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 166 | 6 | 0 |
| Views | 124 | 6 | 0 |

The inventory contains 172 cases; Views excludes 42 dedicated Metal cases
(130 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The page-dialog/beforeunload and shared static-copy gate passed **16 Metal /
13 Views**. Checks passed **241 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-page-dialog-scene-checkpoint-2026-10-25.json).

Rust now supplies alert/confirm/prompt/beforeunload title/profile/message copy,
button captions, prompt/cancel presence and stored default-cancel metadata.
Only display strings, kind/unload flags and message units cross; active frame/
window/profile authority, generation checks and browser callbacks stay native.
The message cap remains exactly 8192 UTF-16 units, including a split surrogate
at the boundary. Prompt initial/answer text remains independent in its editor.

Profile and message text share one transparent Metal underlay per dialog body,
with static AX/layout adapters and Rust body allocation. Native label fonts/
colors, centered profile identity, left multiline message, SizeToFit(440)
preferred-size behavior, 220px scroll cap and 480px/280px body preferences remain.
There are no native or hidden measuring Labels for these two body text roles.
The message viewport synchronizes native scrolling and scene clips. The canvas
stays below the prompt surface and excludes its input rectangle. Native dialog
window/title/button painting and default Return/Escape/platform Cancel behavior
remain adapters. The scene/scroll owner is destroyed before the widget, with
weak callbacks invalidated before text clients/views disappear.

Two Rust cases cover metadata/UTF-16 limits and explicit center/left primitives.
A shared browser test checks all four dialog roles, title/button/default/prompt
metadata, static AX copy and preferred-size/height oracles against native
Labels, plus once-only closure. A Metal test checks scene text/order, prompt
cutout, long-message start/end culling, actual scrolling, theme changes,
CloseNow/client destruction and fresh reopen. Existing Cocoa IME, override,
stale frame/navigation and beforeunload default/scope behavior remain covered.
Shared tools/preferences copy tests cover unchanged adapter modes.

[Prior tools layouts](benchmarks/rust-tools-layout-checkpoint-2026-10-25.json)
and [history presentation](benchmarks/rust-history-rows-checkpoint-2026-10-25.json)
retain their evidence. No dependency or browser-service operation changed.
Native page-dialog title/buttons and modal containers, other forms/panels,
remaining construction/service wording, native interaction/AX/scroll/window
ownership, manual acceptance, profiling and release/license checks remain open.
Normal installed apps/profiles were untouched; no bundle was signed.

## Immediate preference work still remaining

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Migrate UI descriptions/data
and remaining busy/status/form coordination. Preserve normalization, IME,
managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Rust now also owns command/IME admission, recording transitions,
action outcomes, history result presentation and the clear-busy UI projection.
Remaining option-to-field/form construction and popup/empty-item/service/progress
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
   copy/body text and layout now use Rust/Metal; native title/buttons and modal
   containers remain. Replace remaining header/close/modal
   controls and outer placement for workspace/profile/import/folder/deletion
   surfaces. Finish the address/command palette input/frame/container layer.
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
   layout use Rust/Metal. Migrate downloads/passwords/permissions UI and Time
   Machine previews/restore. Preserve real service data, stale-response handling,
   confirmation and failure/rollback behavior.
   Complete Peek/promote/split and link/page menu UI routing.
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
