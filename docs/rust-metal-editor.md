# Rust name editors and remaining service work

Workspace/profile name fields now use the Rust model and CoreText/Metal editor
when `--zitter-sidebar=rust-metal` is selected. They have one native View for
window/input/focus/AX services and no Textfield child/model/RenderText. The Views
backend keeps one real native Textfield through the same `EditorControl`
interface, without a wrapper or second model. Folder names, color/emoji, address
input and other fields still use Views.

This is the first live editor integration, not full textfield parity or a
performance acceptance. The remaining adapters/commands below are explicit.

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
existing behavior. The same CoreText line implementation supplies live input
geometry and
untruncated GPU/fallback raster output; selected text is recolored by clipping
the whole shaped line, preserving ligatures and bidi run geometry.

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

`scene_editor.cc` implements the native text-input contract: composition/selected
ranges, keep-selection confirmation, character/caret/selection rectangles,
read-only/disabled/hide admission, document range editing and native direction
requests. Rust supplies viewport/caret/scroll geometry and display-list
primitives. Native adapters provide key commands, mouse hit testing/selection,
clipboard, generation-guarded menu actions, atomic internal drag/copy and
stale-source/drop rejection, dictionary lookup and AX value/selection actions.
Native services never receive browser/profile handles through the Rust bridge.

Text tiles now accept editor UTF-16 and baseline/scroll offsets without ellipsis.
The shader's quad ABI is unchanged. Selection layers draw the complete shaped
line; only their clip/color differs. A missing Metal surface draws the same scene and
CoreText text through a CPU fallback, without recreating a Textfield. Caret
blinking runs only for focused editable visible fields.

The automated checkpoint checks real Cocoa typing/undo, composed-character
backspace, IME commit/cancel/candidate geometry, long-line caret visibility,
workspace/profile persistence, read-only/disabled/hide rejection, delayed paste
across reset, AX actions, native mouse hit routing, atomic drag/undo, stale drag
and drop rejection, and fallback text ink. Tests use fixture profiles. The
clipboard regression uses a restored asynchronous fake, so its copy/paste
operations do not access the system clipboard.

Remaining editor work before parity:

- Complete uncommon native commands (kill/yank, transpose, mark/unselect and page
  movement), word/paragraph drag expansion and continuous selection scrolling at
  the viewport edge; extend native key-policy/visual-word RTL coverage.
- Complete native AX text offsets/VoiceOver geometry and inspect a real IME
  candidate window, clause/background/underline styles, dictionary and menu
  lifecycles and paste-availability validation at different scales/themes/directions. Current clause geometry uses
  solid thin/thick underlines; complex decorations remain.
- Expand native mouse capture/drag/touch and GPU-loss/recovery checks, measure
  actual presentation/CPU/GPU/cache/idle work and optimize from those results.
- Migrate folder/color/emoji/address and other fields using the same services,
  retaining application persistence and domain authorization. Move remaining
  control policy out of native event adapters where appropriate.

No end-to-end speedup is inferred from passing tests. New project source remains
Apache-2.0; no new crate or production third-party library was added.
