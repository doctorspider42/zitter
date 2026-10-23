# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `488faf5d7df2ef02f2a4144ef8546c336d7a650c`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The full compiled automated browser/UI inventory passed for UI source
`488faf5d7df2ef02f2a4144ef8546c336d7a650c`:

| Backend | Passed | Explicit skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 126 | 6 | 0 |
| Views | 94 | 6 | 0 |

The complete run was serial, with one launcher job and no retries, using temporary
fixture-owned profiles. Metal API validation was enabled. The 32 dedicated Metal
cases are excluded from Views (132 Metal / 100 Views inventory). **211 Rust tests
(including 60 editor model tests) and 66 Python tests from a clean archive** passed.
The live checkout passed 68 Python tests, including two concurrent tests outside
this UI commit. Optimized compilation, scoped GN/boundaries and a final no-work
Ninja check passed. CoreText geometry/raster code is unchanged from the preceding
verified checkpoint. [Full results and source/binary hashes](benchmarks/rust-appearance-editors-checkpoint-2026-10-23.json).

The callback and appearance gate covers both backends, including the migrated
fields, committed-edit notifications, actual Cocoa typing/Return, synthetic IME,
preset rows, theme/save behavior and shared kill-buffer interoperability with a
fixture-owned native field. Historical intermediate failures remain in their
preceding reports; the latest gate and complete suite passed.

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
   and editing in folder names and address search. Preserve UTF-16 selection, grapheme/word navigation, bidi affinity,
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

## Color and emoji editor migration now verified

Workspace/profile names, color and emoji fields use the Rust editor in Metal.
Explicit commit/key callbacks preserve color-on-Return and automatic first-grapheme
emoji save without persisting preedit/cancel/programmatic changes. The Views
control backend remains native; both use the same shell/domain settings services.
Pointer word selection and stationary edge scrolling remain covered.
[Implementation and remaining editor gaps](rust-metal-editor.md).

Next: migrate folder and address fields and broaden actual Cocoa multi-click/
vertical pointer/complex bidi/menu/IME/VoiceOver acceptance. The preferences and
browser-service panels, form/container ownership, real presentation profiling and
manual/release checks remain separate work areas.

The seven work areas above remain open, including all remaining form/service UI,
Views container ownership, manual acceptance and real presentation profiling.
Passing the current inventory does not establish a completion percentage.
