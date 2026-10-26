# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current checkpoint

UI source `efff9570e27cf98834b9fbb22c550552332b871d`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 182 | 6 | 0 |
| Views | 136 | 6 | 0 |

The inventory contains 188 cases; Views excludes 46 dedicated Metal cases
(142 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The preference/tools/timeline/Time-Machine/restored-tab/media/password/download/
modal/shared-chrome gate passed **88 Metal / 70 Views**; the focused geometry
gate passed **2 Metal / 2 Views**. Checks passed **267 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-preference-layout-checkpoint-2026-10-26.json).

Rust now allocates preference root/main/navigation/header/header-text/body/category
columns, all 52 catalogue row label columns and centered controls, plus the saved
site-rule list and centered dynamic rule rows. Native adapters provide control/font
measurement, visibility/ignored flags, margins, explicit preferred sizes and the
single flexible child. Views retains its reference BoxLayouts. Native scroll/
viewport, interaction/AX/window identity, responsive visibility and window placement
still remain adapters requiring further ownership migration.

A separate centered allocator preserves the shared stretch entry point, uses a
common cross axis and odd-size bias, handles asymmetric margins and remeasures
wrapped content after main-axis shrink. Zero/unbounded cross extent uses the
initial natural cross measurement. Native stores, command/context admission,
profile/global scope, deferred rule mutation, confirmation and persistence are
unchanged. Existing catalogue/filter/draft/paging state remains in Rust.

Three Rust cases cover centering/margins, flex remeasurement and fixed controls,
and empty/tiny/zero/saturated extents. A shared browser oracle compares actual
Chromium BoxLayout preferred/minimum/height-for-width/bounded measurements and
child rectangles for 1260 orientation/flex/visibility/ignored/margin/size
combinations. Another uses native preferred-size proxies for real controls and
compares visible layout managers/child rectangles across all ten categories and
all 52 catalogue rows at compact and wide native-window sizes. The explicit
external 212x1 navigation preferred size remains intact; its internal allocator
is checked when navigation is visible.

The initial geometry probe failed both Metal cases and one Views case. It exposed
zero-cross-size remeasurement in the new adapter and a test setup error comparing
the navigation external preferred-size override with its internal layout size.
The adapter now uses natural cross constraints and the oracle compares actual
layout-manager measurements. The original matrix/category/size coverage remains;
initial sources, binary identity and failed logs are retained separately from the
passing final binary. No failed probe is counted as a current pass.

No dependency was added. Remaining interaction/container/scroll/window ownership,
header/profile/status/managed copy and service coordination, other surfaces,
manual platform acceptance, controlled profiling and release/license checks stay
open. Normal installed apps/profiles were untouched; no bundle was signed.
[Previous catalogue checkpoint](benchmarks/rust-preference-catalogue-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.

## Immediate preference work still remaining

Preference root/column/centered-row allocation now uses Rust and matches the
native BoxLayout oracles. Next, replace remaining native scroll/viewport and
interaction/container identity with Rust node ownership, retaining explicit
platform window/event/AX adapters. Finish header/profile/status/managed-state
copy, responsive placement and service coordination alongside that work.

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Static categories and all
52 row descriptions/types/options/order now use a Rust catalogue. Migrate
remaining chrome/dynamic UI data and busy/status/form coordination. Preserve normalization, IME,
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
   Static category/row descriptions, kinds/options/order and root/column/centered-
   row allocation now use Rust. Migrate
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
   clear and preference root/column/centered-row geometry now use Rust, while
   native groups/scroll/windows remain.
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
