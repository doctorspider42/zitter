# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Current workflow — 2026-10-27

Implementation resumed by user instruction. The weekly 70% stop threshold is
revoked for this round. Do not run browser UI tests; accumulate compiled cases
for a later bulk checkpoint. Build, Rust/Python and SDK checks without windows
remain enabled. No shutdown is triggered by the former weekly threshold.
The previous pause is historical: [handoff](rust-metal-handoff-2026-10-26.md).

## Latest headless implementation: residual glyph hosts

Source `f6e91e5f099dad3fa5188fb54976967336718c5a` replaces the palette search decoration with SceneSearchIcon,
a plain image View without Button/ButtonController or an ActionControl allocation.
It retains 32x32 Rust glyph metrics, shared Metal search geometry and vector
fallback. It exposes image/name metadata, no focus/events/default action.
Import dismiss uses the existing RustActionView + ScenePreferenceControls shared
Metal group; the Views baseline retains GlyphButton. Busy admission, dismiss and
page-focus callbacks remain unchanged. Public control fields use View adapters.

Both app and browser-test targets compile. **293 Rust**, **180 SDK direction/font
cases plus raster**, **68 live and 68 clean-archive Python**, GN/boundaries and
Ninja no-op passed; all 53 committed/live/staged prototype inputs and frozen
engine/bootstrap identities match. **No UI cases were executed.** The new
ResidualGlyphHostsImageAndImportDismiss case is compiled for the bulk checkpoint,
along with palette chrome, first-run and import-form regressions.
[Headless evidence](benchmarks/rust-residual-glyph-headless-2026-10-27.json).

This establishes compilation and headless contracts only. Actual import canvas,
AX/default action, disabled/close/capture/focus, search glyph/fallback/theme and
real-tree Button inventory remain pending. Native View identity/layout and Views
popup/menu ownership, manual acceptance, controlled performance, compositor
issues and release/license/notices remain open. The existing preview bundle was
not replaced. Frozen UI-lab inputs exclude current-main network policy.

## Latest targeted follow-up: selector pointer policy

UI source `c7d0eabf53abe496801b447e5c3c8e99127b4b7d` closes the selector mouse-phase difference recorded in the
previous host migration. Rust ActionControl has opt-in press activation, enabled
for SceneSelect. It requires enabled/visible/inside/trigger admission and consumes
pending pointer state before activation. Late release/drag cannot activate again;
ordinary actions retain release activation and mode changes cancel pending input.

Rust SelectState owns real popup-close timestamps and applies a strict pointer
re-open guard supplied by the native mouse constant (currently 100 ms). Keyboard
and AX opens bypass the pointer guard, matching Combobox. Stale close callbacks
cannot change the current guard or close a newer popup. Selected popup dismissal
records the close even after selection marks its model collapsed; observing an
absent popup does not advance the clock. Existing identity/prefix/token policies
remain.

Scheduling synchronously closes a popup when its anchor becomes hidden or fully
clipped. Opening first reveals an offscreen anchor and guards against a runner
removed synchronously by scheduling. The shared popup test now asserts the actual
MenuController exists immediately after mouse press, before release, in both
modes. It retains AX/Down opening, live expanded/collapsed metadata and unchanged
value. Rust-specific branches close a real popup by clipping its anchor, return
and dispatch late release without reopening, then reveal/open the offscreen
anchor through AX. Native popup/focus/IME/window/AX and domain services remain.

Focus passed **11 Metal / 7 Views**; affected regressions passed
**61 Metal / 42 Views**, serial, retry zero. Checks passed **293 Rust**,
**180 SDK font/direction cases plus raster**, **68 live and 68 clean-archive
Python**, scoped GN/boundaries and final Ninja no-op. All 53 committed/live/staged
prototype inputs and compiled/frozen-bootstrap/native-engine identities were
checked. Compiled inventory remains **218 cases**; acceptance is targeted under
the requested cadence.
[Targeted pointer-policy report](benchmarks/rust-select-pointer-targeted-2026-10-26.json).

The initial Rust probe (291 pass / 2 fail) is retained with source hashes. It
caught using handled as activation admission; final code checks actual admission.
The late-release fixture also stopped requiring None and accepts the established
Cancel result while still rejecting activation. No GUI ran against that initial
source. Five new Rust cases cover pointer modes/admission, strict time boundaries,
backwards clock values, keyboard bypass, stale closes and popup observations.

Affected logs retain **2 Metal / 2 Views invalid-mailbox diagnostics**;
overlay counts are recorded separately. They remain unresolved and establish no
CPU/GPU/presentation/input-latency/memory gain. The previous full checkpoint
predates checked/selector/pointer changes. Remaining glyph/decorative Buttons,
native per-control View identity, stable Rust nodes, Views popup/menu ownership,
containers/placement/UI coordination, manual input/VoiceOver/RTL/scale/theme/GPU
recovery, controlled performance and final release/license/notices remain open.
Frozen UI-lab inputs exclude current-main network policy.

## Previous targeted follow-up: selector hosts

UI source `9908417dc1a3d55c76716bb660d8eff2ae9ddaee` moves SceneSelect from SceneGlyphButton/Button to the common
plain View RustActionView. Preference, tools, profile and import selector paths
construct no Button/ButtonController instances. Native comparison retains
Combobox/PreferenceCombobox. Existing Rust SelectState retains selected keys,
prefix matching, empty selection, model replacement and menu-generation tokens.

Rust ActionControl owns pointer/gesture, enabled/focused/visual state and capture
cancellation. The existing Rust menu-key policy, native PrefixSelector/IME
protocol, native menu/font/theme/measurement adapters and domain callbacks remain.
The open AX verb disappears while disabled and restores correctly on enable;
disabling still cancels menu/prefix state. Scheduling checks clipped pending input
synchronously before GPU work is coalesced. No dependency or browser service,
admission/store/persistence policy changed.

Focus passed **11 Metal / 7 Views**; affected regressions passed
**61 Metal / 42 Views**, serial, retry zero. The new shared case opens and
cancels real Chromium Views popup menus using AX, Down and root-view mouse routing. It verifies
host ownership, role, unchanged selected value, disabled/open-verb transitions and
live expanded/collapsed state in both modes. A Rust-only branch scrolls an armed
selector out/back and verifies late release does not open the menu. Existing
physical Cocoa prefix typing, empty selection, durable preference/site policy,
tools drafts/history/restore, profile stable IDs and import lifecycle remain.

Three focus probes retain fixture corrections in the report: the initial case
watched Cocoa NSMenu instead of Views MenuController; the next clicked too soon
after closing and hit the native guard; the third opened the native popup but
incorrectly required RootView capture to remain set. The final fixture checks
the actual controller and menu outcomes. Production code was unchanged after the
probes. Native Combobox opens on press; SceneSelect retains its earlier prototype
release activation. Pointer-phase parity remains an explicit follow-up.

Checks passed **288 Rust**, **180 SDK font/direction cases plus raster**, **68 live
and 68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and compiled/frozen-bootstrap/native-engine
identities were checked. Compiled inventory is **218 cases**; this source has
**targeted acceptance only** under the requested cadence.
[Targeted selector-host report](benchmarks/rust-select-host-targeted-2026-10-26.json).

Affected logs retain **2 Metal / 2 Views invalid-mailbox diagnostics**;
overlay counts are recorded separately. Nonfatal compositor diagnostics remain
unresolved. No controlled CPU/GPU/presentation/input-latency/memory gain is
established. The full text-action checkpoint below predates checked and selector
host changes. Remaining decorative/glyph Button controls, native per-control View
identity, stable Rust nodes, Views popup/menu ownership, containers/placement/UI coordination, manual input/
VoiceOver/RTL/scale/theme/GPU recovery, controlled performance and final release/
license/notices remain open. Frozen UI-lab inputs exclude current-main network
policy.

## Previous targeted follow-up: toggle and checkbox hosts

UI source `d75b117ec3c51344ed27df477212cde4b821e814` moves SceneToggle and SceneCheckbox to the common plain View
RustActionView, with no Button/ButtonController instances on their Rust paths.
Native comparison retains ToggleButton/Checkbox. Existing Rust ToggleState and
CheckboxState retain checked values, toggle hover/progress and scene recipes.
Rust ActionControl now owns pointer/key/gesture, enabled/focused/visual state and
cancellation for these controls. Toggle and checkbox-group scheduling check
clipped input synchronously before GPU work is coalesced.

Checked state changes before ElementTracker activation observers run, matching
native checked controls. A ViewTracker guard still prevents domain callback
access after a notification deletes the host. Callback-free clear-data checkboxes
also activate through Rust. Checkbox AX check/uncheck verbs derive from checked
state, disappear while disabled and return correctly after re-enabling. Native
focus/window/input/AX, font/theme/layout and browser/profile/import commands remain
adapters; no dependency or persistence/admission policy changed.

Focus passed **9 Metal / 6 Views**; affected regressions passed
**59 Metal / 41 Views**, serial, retry zero. The new shared case
checks host ownership, checked/default AX verbs, accessible activation, disabled
admission, re-enable and the checked state seen by an activation observer.
Rust-specific branches cover hide and individual child clipping with late release,
and switch viewport scroll out/back. Existing physical Cocoa preference Space,
import Space, profile/status/capture cancellation, durable clear-data/import and
shared action/row outcomes remain asserted through View adapters.

Checks passed **288 Rust**, **180 SDK font/direction cases plus raster**, **68 live
and 68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and compiled/frozen-bootstrap/native-engine
identities were checked. Compiled inventory is **217 cases**; this source has
**targeted acceptance only** under the requested cadence.
[Targeted checked-host report](benchmarks/rust-checked-host-targeted-2026-10-26.json).

Affected logs retain **2 Metal / 2 Views invalid-mailbox diagnostics**;
matching overlay counts are recorded separately. Nonfatal compositor diagnostics
remain unresolved. No controlled CPU/GPU/presentation/input-latency/memory gain is
established. The full text-action checkpoint below predates these checked-host
changes. Select/decorative Button controls, native per-control View identity,
stable Rust nodes, containers/placement/UI coordination, manual input/VoiceOver/
RTL/scale/theme/GPU recovery, controlled performance and release/license/notices
remain open. Frozen UI-lab inputs exclude current-main network policy.

## Latest full checkpoint: text action hosts

UI source `dadea9bbc3a2664d4569d530babe35c8b15f9d46` moves SceneTextButton from SceneGlyphButton/Button to the
common plain View RustActionView. Dialog, download, workspace/profile/import,
preference and tools text actions no longer construct Button/ButtonController
instances on their Rust paths. Native comparison retains MdTextButton.

Rust ActionControl owns default-action state and admits only visible, enabled,
default, unmodified Return accelerators. Native FocusManager registration, Mac
Space policy, focus, accessibility, font/theme/measurement adapters and kind-19
shared-canvas text remain. Native dialog input protection, delegate admission,
OK/Cancel identifiers and browser/profile/download commands are preserved.
ElementTracker activation precedes callbacks; a ViewTracker guard handles an
observer deleting the action host. Group scheduling cancels clipped child input
synchronously before shared/GPU work is coalesced.

Focus passed **19 Metal / 14 Views**. Full inventory has **216 cases**: **210 Metal /
162 Views passed**, with the same **six optional skips** each, serial and retry
zero. Checks passed **288 Rust**, **180 SDK font/direction cases plus raster**,
**68 live and 68 clean-archive Python**, scoped GN/boundaries and final Ninja
no-op. All 53 committed/live/staged prototype inputs and compiled/frozen-bootstrap/
native-engine identities were checked.
[Full text-action report](benchmarks/rust-text-action-checkpoint-2026-10-26.json).

New tests compare physical Return/Space, disabled default admission, dynamic
labels/defaults and ElementTracker notifications. A Rust-only branch deletes the
host in an activation observer and verifies the domain callback is not invoked.
Existing dialog input-protection, action geometry/AX, modal editors, busy actions,
download metrics/keyboard/capture and shared-canvas outcomes remain asserted.
Test callers use View/AX adapters for migrated hosts and retain ButtonTestApi for
unchanged native controls.

Three earlier focus probes each failed the same new keyboard fixture in both
modes; the report retains their input hashes, source snapshots and results.
Modality and asynchronous waits alone were insufficient. The synthetic fixture
omitted the standalone panel keyboard-routing adapter installed in production.
Adding that same adapter fixed the fixture; production policy was unchanged.

Full logs retain **15 Metal / 10 Views invalid-mailbox diagnostics**;
missing-overlay counts are recorded separately. These nonfatal compositor
diagnostics remain unresolved. Functional tests establish no controlled CPU/GPU/
presentation/input-latency/memory gain.

This full source includes the preceding appearance and preference-host work.
Frozen UI-lab bootstrap/network inputs remain from d23f232; current-main network
policy and actual release acceptance remain outside this binary. Other toggle/
checkbox/select hosts, native per-control View identity, stable Rust nodes,
containers/placement/UI coordination, manual input/VoiceOver/RTL/scale/theme/GPU
recovery, controlled performance and final shipped license/notices remain open.
No complete functional parity is claimed.

## Previous targeted follow-up: preference navigation and dismiss hosts

UI source `be87af8e93417d2df384688b301392575b72a367` moves ten preference category controls
and preference/tools dismiss controls onto RustActionView/View hosts through
ScenePreferenceButton. These paths construct no Button/ButtonController
instances. Native comparison retains RowButton/GlyphButton. Selection moves from
the former native selected_ field into Rust ActionControl on state.

The shared parent retains kind-22/category and kind-23/dismiss scenes, Rust action-
group geometry, native fonts and no child Label/FocusRing instances. Scene reads
Rust enabled/focused/on/visual snapshots. Native input/focus/AX/window adapters
and existing category/filter/close callbacks remain.

Group scheduling checks individual clipped controls synchronously, including
when the parent group remains visible and GPU work is pending. The common host
also skips paint scheduling for unchanged pointer visuals. A pending press can
retire while its visual is already normal; owners still receive that lifecycle
change so row-close input watches are cleaned up.

Focus passed **5 Metal / 4 Views**; affected regressions passed
**48 Metal / 33 Views**, serial, retry zero. The new shared case checks actual
host types and AX metadata, selection, Mac Return/disabled admission, preference
AX close and physical tools-close Space. Rust-specific branches check clipping
of a child while its group remains visible, hide/disable late release and the
actual category surface frame count remaining unchanged for twenty unchanged
hover moves after a hover frame. This verifies scene submission behavior, not a
controlled CPU/GPU/presentation/input-latency/memory gain.

Existing physical Cocoa category/filter/responsive choice, retained-query/profile/
hide lifecycle, tools keyboard/close, layout hierarchy/measurement and shared
header/footer/split/row action outcomes remain. Test callers use View/AX adapters;
Views does not exercise the Rust-only clipping/submission branches.

Checks passed **286 Rust**, **180 SDK font/direction cases plus raster**, **68 live
and 68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and compiled/frozen-bootstrap/native-engine
identities were checked.
[Targeted preference-host report](benchmarks/rust-preference-host-targeted-2026-10-26.json).
The source includes the preceding
[appearance preset and AX-name correction](benchmarks/rust-appearance-action-targeted-2026-10-26.json).

Retained affected logs contain **0 Metal / 2 Views invalid-mailbox diagnostics**
with matching missing-overlay counts. All cases passed; these Chromium compositor
diagnostics remain unresolved.

Per user cadence this control family has targeted acceptance. The full checkpoint
below predates appearance/preference-host changes and is not a full matrix of this
source. Other Button controls, native per-control View identity, stable Rust nodes,
containers/placement/UI coordination, manual trackpad/IME/VoiceOver/RTL/scale/theme/
GPU recovery, controlled performance comparison and final release/license/notice
acceptance remain open. Frozen UI-lab inputs exclude current-main network policy.
No complete functional parity is claimed; no dependency or browser service/store/
admission/persistence policy changed.

## Previous full checkpoint: result rows

UI source `ea549864ae1a4c5a1a043ae336910a472c4c55e3` extends the common plain View row host,
now named RustRowView, to palette suggestions, two-line history results and the
sidebar address pill. Sidebar tab/pin/folder/New Tab rows retain that same host.
These production paths construct no Button/ButtonController instances. Native
comparison retains RowButton/Label policy; remaining preference fallback rows and
other control families are separately inventoried.

Palette chip and address geometry/metrics/fallback fonts retain the existing Rust
row/address recipes and native font/vector adapters. History keeps its kind-25
shared-canvas recipe and no Label/FocusRing children. SceneHistoryRow and UrlPill
subclasses are removed. The native tooltip-to-AX-name rule is preserved: tooltip
updates replace an empty name or one equal to the previous tooltip, while custom
names remain. History keeps URL names and date/URL descriptions; palette chips
keep detail descriptions; address updates retain their named AX adapter and
host/path description.

One pending-input watch registry now serves sidebar, palette and tools. Each
viewport callback checks only pending row/close input synchronously before GPU
coalescing, permits reentrant cancellation and removes destroyed watches. This
replaces the history whole-tree scan. Rust input policy is unchanged; unchanged
pointer visuals retain the previous no-repaint behavior. Native row/spec/domain
identity, font/AX/window/capture adapters, preferred-size constants and browser
commands remain explicit adapters or remaining ownership work.

The compiled inventory contains **212 cases**. Metal passed **206 regular
cases**, Views **158 regular cases**, with the same **six optional skips** each.
One launcher job, retry zero, no failures/crashes/timeouts. Dedicated Metal cases
are excluded from Views. Focus passed **10 Metal / 6 Views**. Checks passed
**286 Rust**, **180 SDK font/direction cases plus raster**, **68 live and 68
clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and compiled/frozen-bootstrap/native-
engine identities were checked.
[Full result-row report](benchmarks/rust-result-row-checkpoint-2026-10-26.json).

New shared tests verify host types/AX metadata and offscreen AX dispatch to the
intended URL. Rust-specific branches cover palette/history immediate scroll
out/back, widget hide, palette disable and replacement at a reused result index.
Existing physical Cocoa history Space, palette keyboard/IME/selection/mouse,
shared history canvas/fallback and sidebar tests retain their original outcomes
through View adapters. Views does not exercise Rust-only cancellation branches.

Retained full logs contain **9 Metal / 7 Views invalid-mailbox diagnostics**
with matching missing-overlay counts. All cases passed; these Chromium compositor
diagnostics remain unresolved. No controlled CPU/GPU/presentation/input-latency/
memory gain is established by this automated acceptance.

This full source includes the preceding
[sidebar row checkpoint](benchmarks/rust-sidebar-row-checkpoint-2026-10-26.json)
and earlier split/header/footer/resize/viewport work. Frozen UI-lab bootstrap/
network inputs remain from `d23f232`; current-main network policy is outside this
binary's acceptance. No dependency, browser admission/store or persistence policy
changed.

Remaining control/container/menu/window/input ownership, stable Rust node identity,
UI coordination/placement, manual hardware trackpad/IME candidates/VoiceOver/RTL/
scale/theme/GPU recovery, controlled performance comparison and release/license/
notice acceptance remain open. No complete functional parity is claimed. Full
matrices remain reserved for larger blocks; this closes the address/result-row
host family.

## Previous full checkpoint: sidebar rows

UI source `878732ea7a9e8637d4df4f36254703d2d4428cdd` moves sidebar tabs, pinned pages, folders
and New Tab to plain RustSidebarRow hosts without Button/ButtonController
instances. Their close children use RustActionView. Rust ActionControl owns
pointer/key/gesture, enabled/focused/on/visual state and drag-versus-click
admission. Address, palette/history/preference row hosts and other control/
container families retain their separately inventoried native adapters.

Pending row/close input is registered only while the Rust controller reports a
pending press/key/gesture. Viewport callbacks snapshot those watches before
dispatch, allow safe erasure/reentrant paint, and synchronously retire clipped
input even while GPU work is queued or fallback is active. Destruction removes
watches. This adds no whole-row-tree scan per frame. Unchanged pointer visuals
avoid scheduling another row paint. These are implementation properties, not a
measured performance gain.

Rust row layout/close visibility and existing native font/vector fallback are
preserved. Metal and workspace animation previews read either row host through
common adapters. Native View identity, FocusRing, domain/spec data, browser
callbacks, preferred-size constants, fonts, menus, capture/focus/AX/window events
remain explicit adapters or remaining ownership work. Metadata changes preserve
live nodes and a pending click on the same target.

The compiled inventory contains **210 cases**. Metal passed **204 regular
cases**, Views **156 regular cases**, with the same **six optional skips** in
both modes. One launcher job, retry zero, no failures/crashes/timeouts. Dedicated
Metal cases are excluded from Views. Focus passed **8 Metal / 4 Views**. Checks
passed **286 Rust**, **180 SDK font/direction cases plus raster**, **68 live and
68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op. All 53
committed/live/staged prototype inputs and compiled/frozen-bootstrap/native-
engine identities were checked.
[Full sidebar-row report](benchmarks/rust-sidebar-row-checkpoint-2026-10-26.json).

New shared acceptance covers host types, AX metadata, a real Cocoa row context
menu, Space/AX selection, folder collapse/expand, New Tab palette opening and
title mutation while pressed. The Rust-specific cancellation case covers row
and close-child scroll-out-and-back, disable/hide and late release on a replacement
node; Views explicitly leaves that Rust-only branch unexercised. Existing actual
mouse drag/drop/close at minimum/maximum sidebar width, tree/space/split/session,
text/fallback and selection assertions remain. Row test callers now use View/AX
adapters instead of bypassing input with ButtonTestApi.

Retained full logs contain **6 Metal / 4 Views invalid-mailbox diagnostics**
with matching missing-overlay counts. All cases passed; these Chromium
compositor diagnostics remain unresolved.

This full source also includes the preceding
[split-pane action and queued-close identity correction](benchmarks/rust-split-action-targeted-2026-10-26.json),
header/footer/resize and main/palette/footer viewport migrations. The lab retains
frozen bootstrap/network inputs from `d23f232`; current-main network policy is
outside this binary's acceptance. No dependency, browser admission/store or
persistence policy changed.

Remaining native row/control/node/container/menu/input/window ownership, manual
hardware trackpad/IME candidates/VoiceOver/global RTL/scale/theme/GPU recovery,
controlled CPU/GPU/drawable-presentation/input-latency/memory comparison and final
release/license/notice acceptance remain open. No complete functional parity or
controlled performance gain is claimed. Full matrices remain reserved for larger
blocks; this sidebar ownership family is such a checkpoint.

## Previous full checkpoint: footer actions

UI source `c8e219585d9d7652d829e64bb2c1eb71e34cb84e` moves footer pin/settings and all
workspace dots to plain RustActionView hosts, without Button/ButtonController
instances. Header actions retain their preceding Rust host; other button/row/
control families remain. Rust owns action enabled/focused/on/visual/input state,
normal/emoji/chrome preferred-size metrics and idempotent cancellation. Existing
workspace/profile/emoji/service stores and command callbacks remain native.
Native View identity, FocusRing, window/focus/AX/input and fallback vector paint
remain explicit adapters.

The full compiled inventory has **206 cases**. Metal passed **200 regular
cases**, Views **152 regular cases**; both explicitly skipped the same six
optional manual/live-import/performance scenarios. One launcher job, retry zero,
no failures/crashes/timeouts. Dedicated Metal fixture cases are excluded from
Views. Focus passed **7 Metal / 5 Views**. Checks passed **284 Rust**, **180 SDK
font/direction cases plus raster**, **68 live and 68 clean-archive Python**,
scoped GN/boundaries and final Ninja no-op. All 53 committed/live/staged prototype
inputs and compiled/frozen-bootstrap/native-engine identities were checked.
[Full footer-action report](benchmarks/rust-footer-action-checkpoint-2026-10-26.json).

The shared real-footer case verifies host identity, AX names/roles, 24x28 normal
and 30x28 emoji metrics, view replacement, inactive-dot menu/profile target,
disabled/pin AX, workspace Space/AX selection, capture cancellation after a
scroll out and immediate return, offscreen AX reveal/selection and idle frame
stability. A real callback deletes its own action host in both backends. Existing
native mouse footer scrollbar/emoji/select/settings/pin/fallback tests keep their
semantic assertions. Dot/profile callers use View/AX adapters instead of
ButtonTestApi; menu and selection/swipe projections retain original domain IDs.

Layer transforms do not notify descendant visibility. The viewport callback
synchronously retires clipped Rust footer input even if a GPU update is queued;
it also works in fallback. Native comparison skips this Rust scan. Mouse/gesture
admission checks visible bounds; a drawn/enabled AX default target is revealed
before ordinary event dispatch. Idempotent cancel avoids an idle paint loop.

Chromium compositor mailbox diagnostics remain in retained logs (6 invalid-
mailbox messages per backend). Drag/width cases and both launchers passed; the
messages are not diagnosed or resolved by this checkpoint.

The first probe timed out in the new native-menu case in both modes because
Cocoa intentionally blocks Chromium application tasks while tracking a context
menu. The final helper observes the real NSMenu, inspects in Cocoa tracking/
default modes and closes it. Late inspection is suppressed after the helper
returns. Failing sources, logs and a stack sample are retained; production menu
nesting policy is unchanged.

This full source includes the earlier
[resize-host](benchmarks/rust-resize-host-targeted-2026-10-26.json) and
[header-action](benchmarks/rust-header-action-targeted-2026-10-26.json) migrations,
plus main/palette/footer viewport and raw-trackpad/capture corrections. The lab
retains frozen bootstrap/network inputs from `d23f232`; current-main network
policy is outside this binary's acceptance. No dependency, browser service/store/
admission or persistence policy changed.

Remaining button/row/control/node/container/menu/input/window ownership, manual
hardware trackpad/IME candidates/VoiceOver/global RTL/scale/theme/GPU recovery,
controlled CPU/GPU/drawable-presentation/input-latency/memory comparison and final
release/license/notice acceptance remain open. No complete functional parity or
controlled performance gain is claimed. Full matrices remain reserved for larger
blocks, per user cadence.

## Previous full checkpoint: horizontal footer viewport

UI source `d44351ca2ad2c3d4125139e35e2354096dca7899` moves the horizontal workspace footer
viewport to Rust. Together with the main sidebar and palette, all three shell
scroll surfaces now use Rust-owned extent/allocation, authoritative fractional
primary-axis offset, reveal and clamp in the opt-in backend. The injected custom
Rust-backed horizontal scrollbar keeps thumb/track/key/AX and Metal rendering.
WorkspaceShell no longer owns ScrollView subscriptions/synchronizers or concrete
ScrollView hosts; Views retains native ScrollView through the common adapter.
Native control/node identity, measurement, global gesture arbitration, event/
focus/AX/window/frame/bar and animation adapters remain.

The full compiled inventory has **202 cases**. Metal passed **196 regular
cases**, Views **148 regular cases**; each explicitly skipped the same six
optional manual/live-import/performance scenarios. One launcher job, retry zero,
no failures/crashes/timeouts. Dedicated Metal fixture cases are excluded from
Views. Focus passed **6 Metal / 3 Views**. The full matrix also covers the
vertical viewport/palette/editor regressions. Checks passed **280 Rust**, **180 SDK font/direction
cases plus raster**, **68 live and 68 clean-archive Python**, scoped GN/boundaries
and final Ninja no-op. All 53 committed/live/staged prototype inputs and compiled/
frozen-bootstrap/native-engine identities were checked.
[Full footer viewport report](benchmarks/rust-footer-viewport-checkpoint-2026-10-26.json).

The horizontal native oracle compares extent, offsets/clamps and minimum/
oversized-target reveal at multiple sizes. The actual shared footer case checks
ownership and sidebar width changes, derives native extent from live geometry,
and exercises reveal/Home/End/AX for positive ranges. Metal must retain full-dot
overflow at both widths; fitted native ranges retain zero offset/no bar. Both check
clamp after removing ten workspaces. A separate Widget routes fractional/momentum horizontal raw events, checks exact
pixels, vertical propagation, velocity rejection, both clamps and collapse.
The real-footer mouse thumb drag, emoji projection, click selection, settings,
pinning and fallback cases retain their assertions. Two new Rust cases cover
horizontal bar reservation/overlay, edge geometry, reveal/resize/empty extents.

A source audit confines typed ScrollView references in product prototype C++ to
the native comparison adapter. Chromium compositor mailbox diagnostics remain
in both backend logs (6 Metal / 2 Views invalid-mailbox messages); the drag/width
cases and launchers passed, but those messages are not diagnosed or resolved here.

Initial probes passed all six Metal cases but exposed overstrict assumptions in
the new real-footer Views oracle: native contents-preferred-size=false may fit
or retain an overflowing content width after resize. Final acceptance derives
the native range from live geometry and still requires full-dot Metal overflow
at both widths. Failing sources/logs are retained; no production behavior changed
for these fixture corrections.

This full source includes the preceding palette migration and its
[targeted acceptance](benchmarks/rust-palette-viewport-targeted-2026-10-26.json),
main-sidebar migration and earlier shared raw-trackpad/capture corrections.
The lab retains frozen bootstrap/network inputs from `d23f232`; current-main
network policy is outside this binary's acceptance. No dependency, browser
service/store/admission or persistence policy changed.

Remaining native control/node/container/menu/input/window ownership, manual
hardware trackpad/IME candidates/VoiceOver/global RTL/scale/theme/GPU recovery,
controlled CPU/GPU/drawable-presentation/input-latency/memory comparison and
release/license/notice acceptance remain open. Global workspace swipe arbitration
still belongs to the shell adapter; the isolated horizontal raw-event test does
not establish physical trackpad acceptance. No complete functional parity or
controlled performance gain is claimed. Full matrices remain reserved for larger
blocks, per user cadence.

## Previous full checkpoint: main sidebar viewport

UI source `9037db7bb4caf3d187145e410224d6c791ca6fcc` moves the main tab/tree sidebar viewport to
Rust in the opt-in backend. Rust owns extent/allocation, fractional offset,
reveal and clamp. The existing Rust-backed sidebar scrollbar is injected into
the shared viewport, retaining thumb/track/key/AX state and Metal presentation.
Native ScrollView subscriptions/synchronization for this surface are removed.
Views retains its bounded ScrollView path. Native View/node identity, measurement,
input/focus/AX/window/frame and animation adapters remain.

The full compiled inventory has **198 cases**. Metal passed **192 regular
cases**, Views passed **145 regular cases**; both explicitly skipped the same
six optional manual/live-import/performance scenarios. One launcher job, retry
limit zero, no failures/crashes/timeouts. Dedicated Metal fixture cases are
excluded from Views. Focus passed **5 Metal / 2 Views**; affected regressions
**76 Metal / 29 Views**. Checks passed **278 Rust**, **180 SDK font/direction
cases plus raster**, **68 live and 68 clean-archive Python**, scoped GN/boundaries
and final Ninja no-op. All 53 committed/live/staged prototype inputs and binary/
frozen-bootstrap/native-engine identities were checked.
[Full main-sidebar viewport report](benchmarks/rust-main-viewport-checkpoint-2026-10-26.json).

The new shared case covers actual host ownership/AX, compact/wide bounds, bottom
scroll and first-row reveal, workspace shrink and return. The existing fractional
projection case now sends an actual ScrollEvent through the sidebar Widget,
then checks exact fractional clipping and native mouse hit testing. Existing
thumb/track/key/AX, real-tab wheel, folder/tree, live-tab/split, swipe/repeat and
presented-Metal/native-fallback animation contracts retain their assertions.

Chromium compositor mailbox diagnostics remain in both backend logs (4 Metal /
14 Views invalid-mailbox messages). The drag/width cases and full launchers
succeeded; this checkpoint does not diagnose or resolve those messages.

The initial probe passed all five Metal cases but exposed the native layer's
pre-commit resize limitation in the new Views test (live maximum 590, offset 430).
The final fixture requires successful presentation of resized geometry before
scrolling to the bottom. The failing probe is retained; no production behavior
was changed for that fixture correction.

This full source includes the earlier shared-viewport capture cancellation and
raw-trackpad corrections. Their historical
[capture](benchmarks/rust-service-capture-targeted-2026-10-26.json) and
[trackpad](benchmarks/rust-viewport-trackpad-targeted-2026-10-26.json) reports retain
their original scope. The lab retains frozen bootstrap/network inputs from
`d23f232`; current-main network policy is outside this binary's acceptance.

Footer workspace dots and palette suggestions still use native ScrollView.
These and remaining native control/node/container/menu/input ownership must
continue migrating. Manual hardware trackpad, IME candidates, VoiceOver, global
RTL, scale/theme and GPU recovery acceptance, controlled CPU/GPU/drawable-
presentation/input-latency/memory comparison and release/license/notice checks
remain open. No complete functional parity or controlled performance gain is
claimed. Full UI matrices remain reserved for larger blocks, per user cadence.

## Previous full checkpoint: service viewports

UI source `bde6809f25e1499aa6241bfe7492c27a5e7e44f4` moves downloads, page JavaScript/beforeunload
message bodies and shared scene modal bodies to the Rust-owned viewport.
Together with tools, preferences, workspace settings and import, these now share
Rust extent/allocation, bounded height, wrapping-width reservation, offset and
reveal/clamp policy. Rust also owns overflow-edge geometry and visibility.
Native measurement, separator paint, platform scrollbar/events/AX, window/frame/
focus/IME and per-control View identity remain adapters.

The full compiled inventory has **196 cases**. Metal passed **190 regular
cases**, Views passed **144 regular cases**; each explicitly skipped the same
six optional manual/live-import/performance scenarios. One launcher job, retry
limit zero, no failures/crashes/timeouts. Dedicated Metal fixture cases are
excluded from the Views comparison. Focus passed **2 Metal / 2 Views**, affected
regressions **40 Metal / 33 Views**. Checks also passed **276 Rust**, **180 SDK
font/direction cases plus raster**, **68 live and 68 clean-archive Python**,
scoped GN/boundaries and final Ninja no-op. All 53 committed/live/staged prototype
inputs and binary/bootstrap/native-engine identities were checked.

[Full service viewport report](benchmarks/rust-service-viewport-checkpoint-2026-10-26.json)
retains exact commands, inventory/results, source hashes, UUIDs and log digests.
The lab retains its recorded frozen bootstrap/network inputs from `d23f232`;
current-main network policy is outside this binary's acceptance.

The native oracle checks bounded preferred size, wrapping, offsets and overflow
separators at multiple sizes and in both themes. Actual download/message scene
scrolling, clipping, Cocoa prompt/AX actions, theme changes, forced close,
editor lifetime and profile isolation remain covered. Modal bodies preserve the
native provider-specific height cap and 2px scroll workaround. Weak scene
callbacks replace ScrollView synchronization/subscriptions. No service, store,
policy/admission/validation or persistence behavior changed; no dependency added.

This is automated acceptance of a larger shared scrolling block, following the
instruction to reserve full UI matrices for larger additions. Complete native
control/node ownership, manual acceptance, controlled CPU/GPU/drawable-presentation
and memory comparison, release/license/notice verification remain open. No
controlled performance gain or complete functional parity is claimed.

Earlier targeted [preferences](benchmarks/rust-preference-viewport-targeted-2026-10-26.json)
and [workspace/import](benchmarks/rust-form-viewport-targeted-2026-10-26.json)
reports retain their historical fixture probes and acceptance scope.

## Previous full checkpoint: tools viewport

UI source `d23f2326fea4902aedeee786585ab11c45558359`, 2026-10-26, passed the full compiled
regression inventory with one launcher job, no automatic retries and temporary
profiles:

| Backend | Regular matrix passed | Regular skips | Failed / crashed / timed out |
| --- | ---: | ---: | ---: |
| Rust + Metal | 184 | 6 | 0 |
| Views | 138 | 6 | 0 |

The inventory contains 190 cases; Views excludes 46 dedicated Metal cases
(144 applicable cases). Four opt-in performance scenarios, real-user Arc import
and the interactive manual window are skipped per backend. Earlier optional
performance scenarios remain historical evidence, not current-commit passes.
Metal API validation remained enabled. No controlled CPU/GPU/presentation or
memory improvement is established.

The preference/tools/timeline/Time-Machine/restored-tab/media/password/download/
modal/shared-chrome gate passed **90 Metal / 72 Views**; the focused viewport
gate passed **2 Metal / 2 Views**. Checks passed **272 Rust tests**,
**180 SDK font/direction cases plus raster**, **66 clean-archive Python tests**,
scoped GN/boundaries and optimized compilation. The live checkout passed 68 Python
tests, including two concurrent cases outside this UI commit. Final Ninja reported
no work to do. All 53 tracked prototype files match committed source and the staged
overlay. [Source/binary identities, commands and results](benchmarks/rust-tools-viewport-checkpoint-2026-10-26.json).

This is the frozen UI-lab build of UI commit `d23f232`, with recorded baseline
bootstrap/network inputs. Later current-main network-policy changes are not
covered by this binary or its UI acceptance.

The opt-in tools panel now replaces Views ScrollView with a Rust-owned viewport
and a native clipping/event/window adapter. Rust owns viewport/content/bar
allocation, overlay versus reserved width, extent, authoritative offset and
maximum, resize/rebuild clamps and minimum-movement reveal policy. Native Views
mode keeps ScrollView through the same interface and its original paint path,
without an extra Metal refresh observer. The existing platform scrollbar
keeps appearance and wheel/trackpad/momentum/key/AX delivery; its position requests
are admitted and applied by Rust. Platform scrollbar and per-control View identity
remain explicit future ownership work, not migrated claims.

Native adapters supply preferred metrics and scrollbar style facts, translate
content, clip descendants and refresh the existing Metal control canvases. The
tools shell no longer owns ScrollView synchronization/subscription state. Browser
stores, profile/workspace/generation/captured-key admission, confirmation, timers,
form/catalogue/geometry/input state and persistence remain unchanged. Preferences
and other scroll surfaces remain native.

Five Rust cases cover overlay/reserved geometry, resize/rebuild clamp, native
minimum-movement and oversized reveal rules, fractional/nonfinite requests and
empty/tiny/extreme metrics. A shared browser oracle compares real ScrollView
extents, offsets and visible rectangles at four sizes with 20 position and 60
reveal requests, including wrapped content and transition to no overflow. Another
checks actual tools ownership, absence of ScrollView in the opt-in host, wheel/
Home/accessible page commands, clipping, editor reveal, AX range and reopen bounds.
Existing tools resize, history-row, action and selector clipping/capture cases
retain their semantic assertions through the viewport interface. Actual Cocoa
keyboard/IME and presentation-backed scene regressions also pass.

The first probe passed wheel/key/AX/reveal on both backends but failed one range
assertion after overflow disappeared. Chromium retains a hidden scrollbar's old
maximum; the effective live content/viewport range is zero. The oracle now checks
live extents and also the scrollbar maximum whenever visible. All four sizes and
80 requests remain. Initial source/binary identities and failed logs are retained
separately and are not counted as current passes.

No dependency was added. Preferences/other scrolling, platform scrollbar/event/
AX/window adapters, stable Rust interaction/node ownership, remaining chrome/
service coordination, manual acceptance, controlled profiling and release/license
checks remain open. Normal installed apps/profiles were untouched; no bundle was
signed. [Previous allocation checkpoint](benchmarks/rust-preference-layout-checkpoint-2026-10-26.json).
The earlier AppKit startup stall is not claimed fixed by this migration.

## Immediate preference work still remaining

Preference root/column/centered-row allocation now uses Rust and matches the
native BoxLayout oracles. The main sidebar, palette, footer, tools, preferences, workspace settings, import, downloads,
page message bodies and scene modal bodies use Rust-owned viewports. Next,
migrate remaining
interaction/container identity to Rust node ownership, retaining explicit
platform window/event/AX adapters. Finish header/profile/status/managed-state
copy, responsive placement and service coordination alongside that work.

Text/backing, category/dismiss controls, filtering, text-draft refresh/admission
and pagination now use Rust scene/controller state. Static categories and all
52 row descriptions/types/options/order now use a Rust catalogue. Migrate
remaining chrome/dynamic UI data and busy/status/form coordination. Preserve
normalization, IME, managed-setting admission, profile-bound confirmation and deferred rule mutation.
Tools page copy, static/dynamic copy rendering, pickers, fourteen actions and
dismiss are migrated. Rust also owns tools page/recording/query/pagination/
loaded state. Rust now also owns command/IME admission, recording transitions,
action outcomes, history result presentation and the clear-busy UI projection.
Timeline choices and shortcut/provider labels, active selection and draft-field
plans now also use Rust. A Rust node plan defines all four tools forms.
Remaining status coordination and service/progress wording remain open,
alongside other panels. History result text/backing, column placement and tools
viewport state use Rust/Metal; native row interaction/AX and platform scrollbar delivery remain adapters.
Tools panel/header/body/clear geometry and tools viewport extent/offset/reveal
now use Rust; their Views mode retains BoxLayout. Native ScrollView/View groups, other BoxLayouts, event/focus/AX
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
4. **Browser service panels.** History result row hosts and viewport state now use
   Rust with native View/AX/window/scrollbar delivery adapters; stable Rust node
   identity, list/service coordination and final acceptance remain. Result text,
   backing and column layout use Rust/Metal. Finish download presentation/interaction ownership;
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
   Replace remaining Button controls, native per-control View identity, outer
   containers/placement and animation-snapshot coordination with stable Rust node
   state. Migrated rows/viewports/resize controls already use plain View adapters. Keep Chromium
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
