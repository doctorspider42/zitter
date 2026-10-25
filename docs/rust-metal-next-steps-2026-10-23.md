# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `503178da15c475dd63aa7a6c5a714c7a7c6f7a17`, 2026-10-25, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 164 | 6 | 0 |
| Views | 123 | 6 | 0 |

The inventory contains 170 cases; Views excludes 41 dedicated Metal cases
(129 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Optional scenarios
remain earlier-checkpoint evidence, not current-commit passes. Metal API
validation remained enabled. No controlled CPU/GPU/presentation or memory
improvement is established.

The tools/history/provider/shared preference-chrome gate passed **27 Metal /
22 Views**. Checks passed **239 Rust tests**, **180 SDK font/direction cases plus
raster**, **66 clean-archive Python tests**, scoped GN/boundaries and optimized
compilation. The live checkout passed 68 Python tests, including two concurrent
cases outside this UI commit. Final Ninja reported no work to do.
[Source/binary identities, commands and results](benchmarks/rust-tools-layout-checkpoint-2026-10-25.json).

Four tools layouts now allocate in Rust: the outer panel (20px interior/12px
spacing, flexible scroll), header (8px gap, flexible title), scrolling body
(bottom/right 8px and 10px spacing, no flex), and history-clear row (8px gap,
flexible period selector). Native font/control services measure preferred sizes;
Rust handles natural/flex allocation, zero-size spacing, clipping and reported
host/child geometry. Shrink remeasurement preserves the initial flex budget.
Views uses its original configured BoxLayout. The allocator covers these
start/stretch, noncollapsing roles with an optional single flexible child;
it does not implement every BoxLayout alignment/multiple-flex policy.

A native BoxLayout oracle passed 24 orientation/flex/visibility/margin
configurations across seven widths and six heights (1,008 bounded combinations),
plus natural/minimum/height-for-width checks. It uses independent native probe
measurement and covers zero/narrow/odd bounds, wrapping, margins, host borders,
hidden and ignored children. A second shared test cycles all four real pages at
two sizes with long Unicode status, checking header/viewport/footer geometry,
scroll extent and actual Metal ordering/editor/dismiss cutouts. The first probe
passed the allocator oracle but targeted the outer widget wrapper in the panel
test, failing on both backends. It now locates the true panel through the
real editor's scroll ancestor, preserving all geometry requirements. That failed
probe and intermediate compile corrections remain in the report.

The frozen UI override and identity manifest explicitly include sidebar_layout
source/header now. No rendering recipe, dependency or domain operation changed.
[Prior tools command/status policy](benchmarks/rust-tools-policy-checkpoint-2026-10-25.json)
and [history presentation](benchmarks/rust-history-rows-checkpoint-2026-10-25.json)
retain their detailed contracts and evidence. Native View/ScrollView/window/
event/focus/AX and outer placement ownership remain, alongside other forms/
panels/layouts, remaining form construction/service wording, manual acceptance,
profiling and release/license checks. Normal installed apps/profiles were
untouched; no bundle was signed.

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
2. **Finish migrated forms and dialogs.** Replace remaining header/close/modal
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
