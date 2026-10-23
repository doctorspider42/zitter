# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `bb32e6f253953c5090a24bebf1d3ef8a382a74e9`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The full compiled automated browser/UI inventory passed for UI source
`bb32e6f253953c5090a24bebf1d3ef8a382a74e9`:

| Backend | Passed | Explicit skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 133 | 6 | 0 |
| Views | 101 | 6 | 0 |

The complete run was serial, with one launcher job and no retries, using temporary
fixture-owned profiles. Metal API validation was enabled. The 32 dedicated Metal
cases are excluded from Views (139 Metal / 107 Views inventory). **66 Python tests
from a clean archive** and 68 in the live checkout passed (the latter includes two
concurrent tests outside this UI commit). Rust/editor/scene/geometry sources are
unchanged from the preceding **214 Rust / 61 standalone model / 180 SDK font and
direction plus raster** checks; those runs were not repeated for this tools-adapter migration.
Optimized compilation, scoped GN/boundaries and a final no-work Ninja check passed.
[Results and source/binary hashes](benchmarks/rust-tools-editors-checkpoint-2026-10-23.json).

The native/Metal gate passed 17 tests per backend. Three new full-matrix cases cover tools editors: actual Cocoa snapshot/provider entry/save, composition suppression for button/key actions, old-client destruction, stale form/profile actions, history committed-query progress during preedit and recorder admission/cancellation/save. Existing preferences, editing and browser/service cases remain included. No new gate/UI failure occurred in this milestone. Historical intermediate failures remain in their own reports.

Each backend explicitly skipped four opt-in performance scenarios, the real-user
Arc import and the interactive one-hour manual harness. They are not counted as
UI passes. The normal app/profile was not used or changed; no app was packaged
or signed in this checkpoint. Real presentation/performance and manual app,
IME-candidate, VoiceOver, scale/RTL/long-interaction acceptance remain separate.

An earlier full-UI checkpoint fixed unintended dormant-WebContents creation during sidebar
identity extraction, hidden-window input/Metal lifecycle, emoji preedit saving,
native alert-role crashes and main-window Cocoa keyup routing. Older upload,
media and preconnect fixtures now seed their explicit two-workspace setup;
UI tests wait for real navigation/focus and reopen closed settings as a user would.
The older failed/intermediate runs remain recorded in the
[preceding checkpoint report](benchmarks/rust-ui-checkpoint-2026-10-23.json).

## Next implementation steps

1. **Text editing and IME.** Finish remaining native service/interaction parity for the live
   Rust workspace/profile name fields, then replace Views textfield layout/drawing
   and editing in remaining custom dialog/page-prompt fields. Preserve UTF-16 selection, grapheme/word navigation, bidi affinity,
   horizontal scrolling, composition/candidate geometry, undo/redo, clipboard,
   mouse/keyboard selection, drag/drop, context commands, focus and accessibility.
   Keep native IME/clipboard/accessibility as explicit platform adapters.
2. **Finish migrated forms and dialogs.** Replace remaining header/close/modal
   controls and outer placement for workspace/profile/import/folder/deletion
   surfaces. Finish the address/command palette input/frame/container layer.
   Verify stale context, defaults, cancellation, validation and persistence.
3. **Preferences.** Move general settings, advanced Chromium preferences, search
   providers, configurable shortcuts, site exceptions and data-clearing controls
   into Rust form state and Metal presentation. Retain existing settings stores,
   profile scope and service authorization.
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
then migrate the separate page prompt and continue settings/tools controls/form/container and
service-panel ownership. VoiceOver, multi-click/bidi/pointer/menu/clipboard/scale/
theme/GPU recovery, actual profiling and signed/manual release checks remain.

The seven work areas above remain open, including all remaining form/service UI,
Views container ownership, manual acceptance and real presentation profiling.
Passing the current inventory does not establish a completion percentage.


## Remaining native text-input roles

All declared WorkspaceShell text-input roles now use EditorControl in the Rust/Metal backend, including all seven tools roles. WorkspaceShell no longer inherits TextfieldController. The separate page JavaScript prompt in `page_dialogs.cc` still owns a Views Textfield. The native folder DialogModel field remains the deliberate Views baseline. Continue inventorying custom dialogs and inherited Chromium UI; this is not a claim that all application text input or all controls are migrated.

Native service stores, profile/global scope, authorization and accelerator/IME/clipboard/accessibility adapters remain explicit boundaries. Tools/preferences form state, pickers, buttons, lists, scroll and outer container ownership still require migration.
