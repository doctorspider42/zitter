# Rust + Metal: remaining work and GUI checkpoint

Snapshot: `408d2d1d0486776580f9dc8589be28b5ef2c9dab`, 2026-10-23.
This is a work plan, not a claim of functional parity. The detailed inventory is
[the parity checklist](rust-metal-parity.md). Existing native behavior is preserved
in many places where Views presentation and interaction still need replacement.

## Current checkpoint

The user requested all UI tests after the current investigation. Run the full
compiled automated browser/UI suite with Rust + Metal, then the applicable suite
with default Views, serially so native focus/mouse events do not contend. Use
fixture-owned temporary profiles and the isolated `zitter_ui_lab` build. Record
actual outcomes, skips and failures; the earlier deferred matrix (70 Metal and
34 Views cases) is the minimum scope, not the full inventory.

The opt-in real-user Arc import and interactive one-hour manual window require
their own explicit switches. Default skips must be reported as skips, rather than
counted as successful UI validation. Do not use the normal app or its profile.
Performance scenarios with explicit benchmark switches are a separate measurement
phase and must not be reported as functional UI passes.

Before this checkpoint, the latest non-windowed verification was 148 Rust and
61 Python tests plus optimized app/browser-test compilation and GN/boundary checks.
The last accepted GUI source revision was `3cd95d82019b239c6c9cc6e84331a4f9f5ef0c55`;
compilation of later regressions did not establish a runtime pass.

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

## Textfield investigation completed for this checkpoint

The current fields are native `views::Textfield` controls. Their protected
`GetRenderText()` uses Chromium's RenderText/HarfBuzz layout for hit testing,
selection, caret and composition geometry. `Textfield::OnPaint` also draws native
background/text/border, with a separate native cursor view. Metal text tiles use
CoreText. Repainting the same string with the existing tile path alone would not
establish matching glyph advances, bidi caret affinity or IME candidate positions.

The next editor implementation therefore needs one authoritative text/selection
model and matching shaping geometry for both input and GPU output. The required
bridge includes UTF-16 grapheme/word boundaries, visual caret stops/affinity,
selection spans, scroll offsets and composition underlines, alongside the native
text-input protocol. Native editor painting/behavior has **not** been marked as
migrated by this investigation. No source changes to editors were made before the
requested GUI checkpoint.
