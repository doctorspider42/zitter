# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `b17b87a02f689e51daf3245fd7cb8c14f80f333a`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The full compiled automated browser/UI inventory passed for UI source
`b17b87a02f689e51daf3245fd7cb8c14f80f333a`:

| Backend | Passed | Explicit skips | Failed / crashed |
| --- | ---: | ---: | ---: |
| Rust + Metal | 122 | 6 | 0 |
| Views | 90 | 6 | 0 |

The accepted full rerun was serial, with one launcher job and no retries, using
temporary fixture-owned profiles. Metal API validation was enabled. The 32 dedicated
Metal cases are excluded from Views (128 Metal / 96 Views inventory). **203 Rust,
52 standalone editor model and 66 Python tests from a clean committed archive**
passed. The working checkout passed 68 Python tests including two concurrent tests
outside this commit. CoreText geometry/raster, optimized compilation, scoped
GN/boundaries and a final no-work Ninja check passed.
[Full results and matching source/binary hashes](benchmarks/rust-shared-kill-checkpoint-2026-10-23.json).

The first full run had one Metal process timeout in `RealTabViewportScrolls`;
Views passed. The termination stack was in AppKit/FrontBoard scene invalidation
inside browser-test teardown. A complete independent rerun on identical source
and binaries passed without retries or code changes. This observation does not
prove a root cause or eliminate flakiness; the report preserves the initial result.

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

1. **Text editing and IME.** Finish remaining native service/interaction parity for the live
   Rust workspace/profile name fields, then replace Views textfield layout/drawing
   and editing in folder names, color/emoji editors and address search. Preserve UTF-16 selection, grapheme/word navigation, bidi affinity,
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

## Shared native/Rust editing service now verified

Workspace/profile name fields use the Rust editor. macOS selection policy,
page/paragraph/document movement, kill/yank, transpose and forced CoreText direction
remain covered. The latest stage removes the separate Rust kill buffer and shares
the existing native Views store, including with fields not yet migrated. Actual
Cocoa Ctrl+K/Y interoperability passes in both directions. Standard installation
and isolated staging apply the small guarded fork hook; rollback and idempotence
have dedicated Python coverage.
[Detailed implementation and remaining editor gaps](rust-metal-editor.md).

Next: finish word/paragraph mouse-drag expansion and continuous edge-selection
scrolling, then broaden complex bidi/menu/IME/VoiceOver acceptance. Migrate folder,
color, emoji and address fields using the same native-service/domain boundaries.
Windows-only character-offset arrays are not a missing macOS baseline requirement;
manual macOS accessibility acceptance remains required.

The seven work areas above remain open, including all remaining form/service UI,
Views container ownership, manual acceptance and real presentation profiling.
Passing the current inventory does not establish a completion percentage.
