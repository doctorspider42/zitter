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

## macOS command and selection checkpoint

Rust now owns macOS Retain/Extend/Caret reversal and directed/undirected selection
state. A service selection chooses its active edge on the first keyboard motion;
word movement without Shift advances beyond the appropriate selection edge.
The native adapter supplies platform commands and CoreText visual positions.
Page/document/paragraph movement, selected-range deletion, kill/yank and transpose
are implemented. Native `INSERT_TEXT`, `SET_MARK` and `UNSELECT` edit-command enum
values remain disabled, matching the current macOS Views baseline.

The existing Chromium Views kill buffer is now canonical for both native and
Rust fields. Rust has no shadow buffer. A successful kill returns a fixed-size
pre-edit UTF-16 range and revision; the adapter checks its existing text cache,
publishes that range to the native service and then applies the updated text.
Yank borrows the native fragment on the UI thread; Rust copies it before events
can invalidate the borrowed view. Undo restores the deletion selection.

The fork API exposes two accessors backed by the existing private native buffer.
It creates no Textfield, RenderText or native editing model. The standard overlay
installer and isolated performance stager apply a guarded, idempotent patch to
`ui/views/BUILD.gn`, `textfield_model.cc` and one new service header. Preparation
validates all anchors before mutation; conflict/symlink/intervening-edit checks and
rollback preserve unrelated changes. Chromium additions retain BSD-style notices;
the project tool and adapter/model changes use Apache-2.0. No production library
or crate was added.

Actual Cocoa Ctrl+K/Y transfers text in both directions between the Rust workspace
name field and a real native color Textfield. Regression coverage includes reversed
selection, Unicode/surrogates, empty kills retaining the fragment, readonly commands
and empty-buffer yank over selection. The same cases pass in the Views control run.
[Shared-buffer checkpoint](benchmarks/rust-shared-kill-checkpoint-2026-10-23.json).

Forced paragraph direction now reaches both cached input geometry and text tile
rasterization, including the tile cache key. Toggling the same direction restores
natural direction and retains explicit alignment as the native control does.
The renderer normalizes CoreText's negative origins for RTL edge whitespace;
caret, hit testing, selected spans and drawing use one bias. Hit testing uses
binary search and preserves the stable affinity choice at coincident carets.

The shared UI tests exercise actual Cocoa Ctrl+T, Ctrl+K, Ctrl+Y and Command+Z,
selection reversal/page/paragraph/word motion, shared kill/yank across forms,
transpose/undo/surrogates/combining sequences, RTL words, visual character motion
inside mixed text, forced direction toggle and AX writing direction. The native
SDK harness checks 15 line samples in natural/LTR/RTL (45 configurations),
nearest-caret affinity, selection extents and forced-RTL raster/geometry alignment.
[Exact source/binary evidence](benchmarks/rust-editor-commands-checkpoint-2026-10-23.json).

The pinned Chromium `ui/views/features.gni` enables character-offset computation
only on Windows (`supports_ax_text_offsets = is_win`). Those Windows arrays are
not an unfinished requirement of the macOS Textfield baseline. This does not
replace manual macOS VoiceOver and platform range/selection acceptance.

Remaining editor work before parity:

- Broaden complex visual-word bidi, actual Cocoa multi-click timing/aggregation,
  vertical pointer policy, long-word edge scrolling and native key-policy cases.
- Inspect actual macOS VoiceOver and real IME candidate windows, complex clause/
  background/underline styles, dictionary and menu lifecycles, paste availability,
  focus/capture/touch, different scales/themes/directions and GPU loss/recovery.
- Migrate folder/color/emoji/address and other fields through the same service
  contracts, preserving persisted data and domain authorization. Move remaining
  control policy out of native adapters where appropriate.
- Measure actual CPU/GPU/cache/idle and drawable-presentation work. No end-to-end
  speedup is inferred from passing tests or from hit-test algorithm complexity.

New project source remains Apache-2.0; no new crate or production third-party
library was added.


## Pointer word selection and stationary edge scrolling

The Rust model now owns the active pointer selection unit, original double-click
word and anchor/reversal policy. Double-click drag expands through word boundaries
and retains the initially selected word after repeated direction changes. Gaps
between native word tokens, including whitespace and punctuation, select as one
unit. `SELECT_WORD` at end-of-text selects the preceding unit; the public command
still rejects an existing selection, matching native admission. Expansion of
nonempty ranges is used internally during word dragging.

A pointer session is tied to the text revision. Reset (even identical text),
external selection/navigation, availability transitions, composition and edits
invalidate it. Native code supplies coordinates and current CoreText grapheme/word
boundaries. Boundary lookup is logarithmic; the new session is fixed-size and
introduces no text copy or production dependency.

The adapter has a 100ms timer for selection beyond horizontal view edges. Each
tick hit-tests through the scrolled viewport, applies Rust selection and reveals
the caret. It stops at text ends, on release/capture loss/focus/lifecycle changes,
and when the model no longer admits the pointer session. Single-line triple click
selects all, then dragging retains its native initial anchor; this is not a claim
about multiline paragraph editing.

Eight new model regressions and two shared browser tests cover reversal, gaps,
Unicode/graphemes, readonly/stale sessions, undo restoration and stationary edge
scrolling/cancellation. The browser comparisons dispatch explicit UI MouseEvents
in both backends; actual Cocoa multi-click acceptance remains separate. The first
gate exposed an incorrect test assumption that native SELECT_WORD admits a
nonempty selection; only that expectation was corrected, with the failed logs
retained locally. Existing Cocoa typing/commands and other browser cases still run.
[Full checkpoint](benchmarks/rust-editor-pointer-checkpoint-2026-10-23.json).
