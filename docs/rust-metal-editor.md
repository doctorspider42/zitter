# Rust editor foundation

The new editor model and CoreText geometry are compiled into the isolated
Rust/Metal prototype. **Live fields still use `views::Textfield`.** This milestone
adds the replacement's state/geometry and bridge; it does not claim a migrated
control, functional parity, physical presentation or a performance improvement.

`sidebar_editor.rs` owns UTF-16 text, directional selection/affinity, revisioned
platform boundaries, enabled/read-only admission, editing, delta undo/redo (256
groups), and token-guarded composition. Preedit/cancel update display state without
publishing committed application edits. Confirmation or final insertion commits
one transaction; cancellation restores the original selected text and selection.
An old IME callback cannot change a reset/new context. Native exact selections
cannot split surrogate pairs; interactive movement/deletion uses platform grapheme
stops, including when a service caret falls inside a composed character.

The CXX bridge returns updated metadata with each admitted/rejected operation.
Its borrowed text slice is valid only until mutation. A native adapter must copy
or cache that text by revision; no browser/profile handles enter the Rust model.
A changed revision invalidates all movement/deletion geometry until fresh
boundaries arrive. The model trusts the platform's Unicode segmentation rather
than implementing an incomplete Unicode table. Arbitrary native UTF-16 roundtrips;
edits joining lone surrogate halves across an edit/marked boundary are rejected.

`EditorTextLayout` creates one immutable, untruncated CoreText line for drawing,
UTF-16 composed/word boundaries, visual caret stops/secondary affinity, hit testing
and selection spans. It shares the system-font helper with the Metal text tile
renderer. Drawing accepts a viewport clip and scroll transform instead of
reshaping truncated strings. The current tile renderer otherwise retains its
existing behavior. The new line is **not yet used by a live input control**.

Window-free validation:

```sh
python3 tools/check_editor_text_layout.py
.chromium/src/third_party/rust-toolchain/bin/rustc --test --edition=2021 \
  chromium/prototype/sidebar_editor.rs -o build/sidebar_editor_tests
build/sidebar_editor_tests
```

The Rust module's tests also run in the existing Chromium
`sidebar_scene_unittests` target. The native SDK harness creates no application or
window and uses no profile. It checks empty/long lines, ligatures, composed text,
emoji, CJK/Thai, RTL/mixed-direction caret geometry, selection, raster colors and
scrolled clipping. These checks do not establish native IME/VoiceOver acceptance.

Next: connect an editor view and native text-input/accessibility adapters, starting
with workspace/profile name fields. Use the same shaped line for input geometry
and GPU output; add clipped editor-text primitives rather than overlaying
CoreText text on HarfBuzz hit geometry. Preserve composition clauses/candidate
rectangles, clipboard/context commands, keyboard/mouse/drag selection, focus,
scrolling, accessibility and persistence, then extend to folder/color/emoji and
address input. Test undo grouping and IME behavior against real interactions;
the atomic composition transaction must be validated with the adapter.

No new crate or third-party library was added. New project source is Apache-2.0;
the platform layer uses already-linked macOS frameworks.
