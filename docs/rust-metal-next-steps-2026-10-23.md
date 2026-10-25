# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `eabd5a5c531cab6ada7d49e2678054c625262419`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 169 | 6 | 0 |
| Views | 126 | 6 | 0 |

The inventory contains 175 cases; Views excludes 43 dedicated Metal cases
(132 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The modal/folder/workspace-deletion/page-dialog/beforeunload/shared-chrome gate
passed **33 Metal / 25 Views**. Checks passed **243 Rust tests**, **180 SDK font/
direction cases plus raster**, **66 clean-archive Python tests**, scoped GN/
boundaries and optimized compilation. The live checkout passed 68 Python tests,
including two concurrent cases outside this UI commit. Final Ninja reported no
work to do. All 53 tracked prototype files match the committed source and staged
overlay. [Source/binary identities, commands, results and retained failed
probe](benchmarks/rust-scene-modals-checkpoint-2026-10-25.json).

Folder create/rename, folder deletion/save-error and workspace deletion now use
shared Rust/Metal actions and Rust outer-body allocation. Their opt-in branches
no longer instantiate DialogModel/BubbleDialogModelHost or native button Labels.
The existing Rust form/notice state and body scenes remain, alongside unchanged
native domain validation, captured targets, stores and commands. Views mode
retains the original model-host baseline.

The shared scene client keeps native event/focus/AX/input protection and Escape.
SceneModalDialog retains the native bubble title/frame, autosizing, theme,
rounded client layer, fixed width and capped vertical ScrollView. Rust body
allocation preserves original text/control margins, side insets and the 2px
scroll workaround. Native model-host preferred/minimum sizes, margins, fixed
width and actual body rectangles are checked using the same measured body.
Fixed-width minimum sizing adds the action row once; empty action rows have
zero natural size. Default accelerators are removed when a control stops being
the default. Page and migrated modal delegates are directly owned and destroyed
after their client-owned widgets; forced folder close promptly releases IME
clients. The old legacy unique-pointer helper did not automatically own a plain
page delegate; PageDialogs now keeps it explicitly.

The first gate reproduced missing Escape dispatch in the new Cocoa modal case
on **both** backends: 32/33 Metal and 24/25 Views passed. Original sources,
source hashes, binary identity and logs are retained. Installing the existing
registered-accelerator route for folder deletion/save errors and workspace
notices fixes delivery without inventing Return for macOS's Cancel default.
The same unchanged default/cancel test then passes. The dedicated lifetime
case additionally uses actual Cocoa Return to acknowledge a one-button error.
It closes three composing editors with CloseNow, reopens a clean editor and
checks client destruction on Detach. Existing validation, stale-target,
beforeunload, profile-retention and screen-bound cases remain in both gates.

Native title/frame/modal/scroll/window and event/focus/AX containers, other
forms/service panels, manual platform acceptance, profiling and release/license
checks remain open. Normal installed apps/profiles were untouched; no bundle
was signed.

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
