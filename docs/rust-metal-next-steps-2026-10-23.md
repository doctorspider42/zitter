# Rust + Metal: remaining work and GUI checkpoint

This is a work plan, not a claim of complete functional parity. The detailed
inventory is [the parity checklist](rust-metal-parity.md).

## Latest targeted stage

UI source `2c305645b9ec18d82e8174da67df5ec7973fd862` moves workspace settings and import to the shared
Rust-owned viewport. Rust owns height clipping, wrapping-width reservation,
allocation, offset and reveal/clamp. Native measurement, platform scrollbar/input,
accessibility/window adapters and per-control View identity remain. The native
Views baseline retains its original ScrollView and bounded sizing policy.

Focused new tests passed **2 Metal / 2 Views**; affected regressions passed
**27 Metal / 20 Views**, single attempt, retry limit zero.
Checks passed **274 Rust**, **180 SDK font/direction cases plus raster**, **68 live
and 68 clean-archive Python**, scoped GN/boundaries and final Ninja no-op.
All 53 committed/live/staged prototype inputs and binary/bootstrap/native-engine
identities were checked. See [the targeted report](benchmarks/rust-form-viewport-targeted-2026-10-26.json).

Both panels have coverage for actual successful compositor presentation,
offscreen/reveal, compact/wide wrapping, resize and shrink, wheel/AX commands and
retained draft text. Earlier fixture failures are retained separately: the native
settings opener requires a visible main widget, native scrolling is asynchronous,
and a fixed spacer inflated the import window minimum. The final responsive
spacer adds overflow at real layout widths without inflating its zero-width
minimum-size query. Production code was unchanged for those fixture corrections.

An earlier affected Views run had 19 passes and one successful-presentation wait
timeout. The final fixture explicitly schedules root paint before requesting the
frame; final focus and affected runs passed on both backends. Earlier results
remain in the report; elimination of all presentation flakiness is not established.

Full UI tests run **only after a larger block**, per user instruction. This stage
has targeted acceptance; the previous full matrix below belongs to UI source
`d23f232`. The lab retains frozen bootstrap/network inputs; current-main network
policy is outside this binary's acceptance. No controlled performance gain or
complete manual/release/license parity is claimed.

Previous targeted preference viewport acceptance is retained in
[its report](benchmarks/rust-preference-viewport-targeted-2026-10-26.json).

## Latest full checkpoint

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
native BoxLayout oracles. Tools, preferences, workspace settings and import now
use a Rust-owned viewport without ScrollView. Next, migrate other scroll surfaces and remaining
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
4. **Browser service panels.** Finish history native interaction/list and remaining scrollbar
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
