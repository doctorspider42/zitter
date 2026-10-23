# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `e17b856ce11e6d6994370998036b15f99e2f2c37`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The full compiled automated browser/UI inventory passed for UI source
`e17b856ce11e6d6994370998036b15f99e2f2c37`:

| Backend | Passed | Explicit skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 128 | 6 | 0 |
| Views | 96 | 6 | 0 |

The complete run was serial, with one launcher job and no retries, using temporary
fixture-owned profiles. Metal API validation was enabled. The 32 dedicated Metal
cases are excluded from Views (134 Metal / 102 Views inventory). **214 Rust tests
(including 61 editor-model and five editor-scene tests), a standalone 61-case model
run and 66 Python tests from a clean archive** passed. The live checkout passed
68 Python tests including two concurrent cases outside the UI commit. Fresh
CoreText SDK checks cover 180 font/direction cases plus raster; optimized build,
GN/boundaries and final no-work Ninja checks passed.
[Results and source/binary hashes](benchmarks/rust-address-editor-checkpoint-2026-10-23.json).

The address/palette/keyword/shortcut/preconnect/editor gate passed in both backends.
The first Metal gate was green; the initial Views fixture directly sent synthetic
composing keys to the entire Textfield key handler, bypassing its OS IME filter.
The fixture now checks the application key-policy seam during composition, while
retaining actual Cocoa typing/clear/navigation/Escape. The failed attempt remains
recorded. Actual candidate-window acceptance is still manual.

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
   and editing in remaining settings/service fields. Preserve UTF-16 selection, grapheme/word navigation, bidi affinity,
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

## Address/Command Center editor migration now verified

The Metal address/palette field uses Rust editing, CoreText/Metal and its original
larger borderless presentation. Committed/key callbacks retain suggestions,
keyboard selection, command/keyword dispatch, navigation and profile/preconnect
services. Preedit and stale text cannot dispatch new searches or enter results;
same-value insertion notifies dependent UI without redundant undo/revision changes.
[Implementation and remaining editor gaps](rust-metal-editor.md).

Next: reconcile empty marked-string/IME termination sequences with the native
adapter, then migrate remaining settings/service fields and continue form/container and
service-panel ownership. Complex native IME/VoiceOver, multi-click/bidi/pointer/
menu/clipboard/scale/theme/GPU-recovery acceptance, real profiling and signed/manual
release checks remain separate work areas.

The seven work areas above remain open, including all remaining form/service UI,
Views container ownership, manual acceptance and real presentation profiling.
Passing the current inventory does not establish a completion percentage.
