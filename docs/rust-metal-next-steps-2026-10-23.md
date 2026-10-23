# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `4475ee78600cc38270943ead14b9289b2f84b999`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The full compiled automated browser/UI inventory passed for UI source
`4475ee78600cc38270943ead14b9289b2f84b999`:

| Backend | Passed | Explicit skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 116 | 6 | 0 |
| Views | 86 | 6 | 0 |

The two runs were serial, with one launcher job and no retries, using temporary
fixture-owned profiles. Metal API validation was enabled. The 30 dedicated Metal
cases are excluded from Views; every common fixture runs with both backends. The
scope exceeds the previous deferred 70 Metal / 34 Views matrix. **175 Rust and
61 Python tests** also passed, along with the window-free CoreText geometry/raster
harness, optimized compilation, scoped GN/boundaries and a final no-work Ninja check. [Full results and matching source/binary hashes](benchmarks/rust-editor-foundation-checkpoint-2026-10-23.json).

Each backend explicitly skipped four opt-in performance scenarios, the real-user
Arc import and the interactive one-hour manual harness. They are not counted as
UI passes. The normal app/profile was not used or changed; no app was packaged
or signed in this checkpoint. Real presentation/performance and manual app,
IME-candidate, VoiceOver, scale/RTL/long-interaction acceptance remain separate.

The preceding checkpoint fixed unintended dormant-WebContents creation during sidebar
identity extraction, hidden-window input/Metal lifecycle, emoji preedit saving,
native alert-role crashes and main-window Cocoa keyup routing. Older upload,
media and preconnect fixtures now seed their explicit two-workspace setup;
UI tests wait for real navigation/focus and reopen closed settings as a user would.
The older failed/intermediate runs remain recorded in the
[preceding checkpoint report](benchmarks/rust-ui-checkpoint-2026-10-23.json).

## Next implementation steps

1. **Text editing and IME.** Replace Views textfield layout/drawing and editing
   behavior in workspace/profile/folder names, color/emoji editors and address
   search. Preserve UTF-16 selection, grapheme/word navigation, bidi affinity,
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

## Editor foundation now completed

The [editor model and geometry](rust-metal-editor.md) are compiled into the
prototype. Rust owns the replacement UTF-16 text/selection, revisioned boundaries,
256-group delta undo/redo, admission and token-guarded atomic IME transactions.
The CXX bridge is compiled. CoreText supplies one immutable untruncated line for
caret affinity, hit testing, selection and drawing, with the same system-font
helper as the Metal renderer. The 27 new model tests and native window-free
geometry/raster harness passed, alongside the full existing automated UI matrix.

**Live fields still belong to Views.** The next concrete step is a replacement
editor view plus native text-input/AX/clipboard/focus adapters, beginning with
workspace/profile name fields. Connect the same shaped geometry to clipped GPU
text, cursor, selection, horizontal scroll and composition clauses/candidate
rectangles. Then migrate folder names, color/emoji and address search. Verify
real IME behavior and persistence before marking any field migrated. The existing
browser cases validate preserved behavior; they do not yet exercise a live Rust
editor. Base paragraph direction/alignment and full native service behavior must
be resolved in that adapter rather than inferred from the first glyph run.

The seven work areas above remain open. Passing the current suite establishes a
regression checkpoint, not a percentage of completed migration. No reliable
remaining-time estimate exists until the native editor adapter is proven.
