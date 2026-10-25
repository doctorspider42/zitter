# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `186faaf7cfb01f78b1a1bfce0661743d1063e5a5`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 171 | 6 | 0 |
| Views | 127 | 6 | 0 |

The inventory contains 177 cases; Views excludes 44 dedicated Metal cases
(133 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The download/modal/folder/workspace-deletion/page-dialog/beforeunload/shared-
chrome gate passed **37 Metal / 28 Views**. Checks passed **245 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68
Python tests, including two concurrent cases outside this UI commit. Final
Ninja reported no work to do. All 53 tracked prototype files match the committed
source and staged overlay. [Source/binary identities, commands and results](benchmarks/rust-download-scene-checkpoint-2026-10-25.json).

The opt-in download panel now uses Rust/Metal text, progress and actions, with
Rust outer/list/row/action allocation. No native or hidden Label, MdTextButton
or ProgressBar is instantiated for these managed roles. Common PanelScene lives
in the existing sidebar Metal interface; page dialogs keep the same alias and
factory behavior. All managed progress/actions join the transparent panel
underlay, with no per-row drawable. Native typography/color, event/focus/AX,
window/frame and scroll services remain adapters. Views keeps its original
controls and BoxLayout baseline.

Rust renders flat determinate bars with native pixel rounding/RTL and the
original two-second indeterminate segment keyframes. A single panel timer runs
only for visible projected indeterminate nodes, stops for hidden/offscreen or
nonanimated content and requests CPU fallback paint when Metal is absent.
Progress preferred metrics, AX names and percentage/indeterminate value behavior
remain native-compatible. Child-size changes explicitly refresh scroll extent.
Download widgets and delegates are directly owned and synchronously destroyed
in widget-then-delegate order, after scene/row/subscription cleanup. Closing
invalidates stale UI callbacks/timers; profile changes detach observers while
leaving real transfers intact. Existing registered accelerator routing now also
serves the download window, including actual Cocoa Escape.

The real-transfer browser case compares text heights/widths, action preferred/
minimum sizes and progress metrics/AX with original native controls. It checks
unknown progress, hide/show timer behavior and actual Cocoa Space pause/resume,
then Escape. Full-path filename tooltips retain native hit testing, explicitly
checked after target resolution and layout; only filename copies opt into events.
A dedicated Metal case populates 80 manager history rows and checks
childlessness, actual scroll culling, theme, CloseNow/reopen and stale GUID/profile
isolation. Two Rust cases cover determinate rounding/RTL/nonfinite input and
indeterminate keyframes. Existing unsafe-history, transfer/Finder interception,
cancellation, profile isolation and common chrome/dialog cases also pass.

Chromium still owns DownloadItem/DownloadManager facts, status/byte/time
formatting, active-first/history-cap ordering, unsafe/missing/terminal checks,
observers and Act commands. A drawn or enabled Rust action never authorizes a
transfer/file operation. Native control/scroll/window/AX node ownership,
download presentation/list policy, other service panels, manual platform
acceptance, profiling and release/license checks remain open. Normal installed
apps/profiles were untouched; no bundle was signed and no dependency was added.

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
   layout use Rust/Metal. Finish download presentation/interaction ownership;
   migrate password/permission UI and Time Machine previews/restore. Preserve
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
