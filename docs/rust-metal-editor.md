# Rust editors and remaining service work

Workspace/profile name, color/emoji, folder-name, address/palette, eight preferences and seven tools fields now use the Rust model and CoreText/Metal editor
when `--zitter-sidebar=rust-metal` is selected. They have one native View for
window/input/focus/AX services and no Textfield child/model/RenderText. The Views
backend keeps one real native Textfield per field. Workspace/profile/appearance/
address/preferences/tools fields use `EditorControl`; folders retain the original DialogModel Textfield.
Neither has a wrapper or second editing model. All declared WorkspaceShell editor roles are migrated. The separate page JavaScript prompt still uses a Views Textfield.

This is a live editor migration checkpoint, not full textfield parity or a
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
- Migrate remaining custom dialog/page-prompt fields through the same service
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


## Live color and emoji fields

Workspace color and emoji now use the same single Rust-backed View as name fields
in the Metal backend. Views retains one real native Textfield for each field.
Preset-row Rust layout/input and the appearance stores remain unchanged; the
field surface now uses the existing shared Metal renderer and editor color provider.
Native font/background/border styling remains on the control backend only.

`EditorControl::SetCallbacks` exposes committed user changes and key admission.
Rust notifies from its committed update flag, including IME confirmation when the
display text already contains the preedit. Native Textfield forwards user changes
outside composition and suppresses cancellation notifications. Programmatic
SetText, preedit and cancellation are silent. Native fields with no callbacks
retain no controller; no wrapper or second native editing model is created.
The shell reads and copies current text in the callback, so no borrowed UTF-16
buffer survives synchronous normalization/reset.

Color validation/submission remains on Return. Emoji auto-applies the first
committed grapheme (including ZWJ sequences), using the existing workspace-scoped
normalization/save services. Return/Escape during composition stay with the input
method. Native composition cancellation no longer reaches emoji persistence;
this does not change the native model's documented selection-cancel semantics.
Callbacks detach before widget destruction and use weak shell references.

Shared tests cover silent programmatic/preedit/cancel operations, exactly one
notification for commit, callback removal and reentrant normalization. Actual Cocoa
Return/typing exercises validation and emoji save; synthetic IME checks confirm
and cancel, and workspace switching checks domain isolation. Existing theme/save,
preset-row clipping/cancellation and all applicable browser tests pass. The
kill-buffer test now creates a fixture-owned native control rather than relying
on an unmigrated production color field. No such test control ships in the app.
[Matching evidence](benchmarks/rust-appearance-editors-checkpoint-2026-10-23.json).


## Live folder-name editor in the modal form

The Metal folder form now uses `AddEditor(..., true)` instead of a Textfield.
Rust owns the text/selection/history/composition and the existing Rust folder form
owns copy, captured target, validation and geometry. A weak committed-edit callback
reconciles form text/errors; programmatic changes are read explicitly at submit.
Preedit does not publish a form edit. Cancel clears composition and cancels the
captured form, and the callback detaches before the host destroys its children.

The modal DialogModel host receives the editor's native View as its focus target.
Native title/buttons/default action and original create/rename/persistence services
remain adapters. Both folder modal variants now install the existing macOS workspace
keyboard router: the first focused gate showed that actual Cocoa letters and
Return were lost in both the original Views and Rust branches, while direct input
client calls worked. The final gate/full suite verifies the routing fix; initial
failures remain recorded in the checkpoint report. The original Views branch still uses its actual DialogModel
Textfield and validation path; it is not switched to a second Rust editing model.
Shared tests use the real input client for each backend and add no editing wrapper.

The new shared case exercises actual Cocoa typing/Return, Unicode insertion,
rename prefill, synthetic IME cancellation, destruction of the previous input
client and a default-prefilled next modal. Existing nested create/rename/persistence, rollback,
limits/error/IME/stale context, tree and menu cases remain in the full inventory.
The Rust editor/scene sources are unchanged from the prior verified 211-case unit
run; native compilation and the new focused/full GUI runs verify this binding.
[Matching checkpoint](benchmarks/rust-folder-editor-checkpoint-2026-10-23.json).


## Address and Command Center input

The palette now uses the same Rust editor in Metal and one native Textfield via
`NativeEditor` in Views. Committed/key callbacks replace the shell's Textfield
controller coupling for this field. Existing up/down selection, Return resolution,
Escape, command/keyword handling, history ranking, navigation and profile/preconnect
services remain. Fixture helpers use the actual input clients instead of invoking
controller methods after a programmatic SetText.

`SetPresentation` supplies font size, horizontal inset, frame and fill. The palette
retains `Font(3)`, zero inset and no field frame. Standard fields keep their original
13px/8px framed defaults. CoreText input geometry, placeholder measurement, dictionary
font and raster/cache metadata use the same requested size. Rust viewport padding
is finite/clamped, and borderless scene primitives omit the edge. Quad ABI and
production dependencies are unchanged. The window-free SDK harness now covers
13/14/16/17px samples in all three directions (180 line cases), plus raster checks.

Same-text nonempty insertion is now a user commit notification like native Views,
without changing text revision or adding an undo entry. Dependent palette state
therefore resets for a replacement paste even when its value is identical.
Programmatic reset, preedit, cancellation and empty no-op insertion stay silent.

Search dispatch checks current text and composition; result acceptance rejects
preedit as well as stale query/profile. Composition keys belong to the platform
IME. The synthetic test checks the application's `HandleAddressKey` policy seam;
calling native Textfield::OnKeyPressed directly bypasses the OS filter and executes
native edit commands, so it is not an equivalent IME simulation. That incorrect
initial fixture failed in Views and was corrected. Real Cocoa typing, clear,
Return/navigation and Escape remain tested in both backends; actual candidate-window
acceptance is still manual. Empty marked-string updates are now reconciled in the platform adapter (below);
other undo/menu/focus/hidden and real candidate-window termination paths still
require broader acceptance. The policy rejects hidden
palette input.
[Matching evidence](benchmarks/rust-address-editor-checkpoint-2026-10-23.json).


## Empty platform marked text and range availability

Both adapters now interpret SetCompositionText(empty) as termination of existing
preedit. With no preedit it is an idle no-op, preserving text and service selection.
The Rust adapter cancels its existing transaction before considering a new one;
the underlying Rust model retains its lower-level empty-preedit/token capability.
The native adapter avoids forwarding idle empty text to TextfieldModel, whose
normal implementation would delete a selected range even without active preedit.
All native marked-text updates are callback-suppressed; explicit final insertion
and ConfirmCompositionText still publish committed edits normally.

After termination HasCompositionText is false, a normal editable client's range
query succeeds with gfx::Range::InvalidRange, and confirmation returns zero. The
Rust adapter now matches that native availability contract, including rejecting
range queries for readonly/unavailable input. Cancellation publishes no domain
commit, so an empty emoji draft cannot erase the saved emoji through autosave.

The new shared native/Metal test covers idle reversed selection, active termination,
range/readonly semantics, repeated empty updates, a subsequent normal confirmation
and saved-emoji protection. It adds no native shadow editing model. When preedit
already replaced selected text, the original native cancellation-selection behavior
remains; Rust restores its atomic original value/selection as documented earlier.
Undo/menu/focus/hide sequences and real OS candidate-window acceptance remain open.
[Matching checkpoint](benchmarks/rust-ime-empty-marked-checkpoint-2026-10-23.json).

## Preferences search, text values and site origin

The general preferences search, six per-key Chromium text preferences (font families, accepted languages, DNS template) and site-origin field now use EditorControl. The opt-in backend owns editing/history/composition/layout/raster through Rust and Metal; Views uses one actual native Textfield, without a shadow model. Font size, fill and framed inset are set through shared presentation. Native stores, HTTP(S)/credential/path validation, per-profile scope and application-wide DNS semantics remain unchanged.

Only committed edits filter sections or mark a preference dirty. Programmatic reset, preedit and cancellation are silent. Enter/Escape use an application policy callback that rejects composition; both button/key saves also reject preedit. General refresh preserves a same-profile active composition even before it becomes dirty; profile changes and unavailable/managed values reset from the store as before. Switching workspace closes the panel; its cached controls refresh on reopening, as in Views.

The new shared test uses actual Cocoa typing and Return/Escape, UTF-16 input clients, preedit suppression, animation refresh, canonical/invalid language values, personal/work reopening and a fixture-owned site exception. It verifies real Metal presentation and absence of a native child editor in the migrated language field. Managed-policy transition injection, broader OS candidate/undo/menu/focus acceptance and remaining service fields are still open. [Matching checkpoint](benchmarks/rust-preference-editors-checkpoint-2026-10-23.json).

## History, Time Machine, provider and shortcut editors

Seven tools roles now use the shared editor contract. WorkspaceShell has no TextfieldController inheritance or manual ContentsChanged entry point. Callbacks are detached before rebuilding the tools subtree, and tests verify old input-client weak references expire. Views retains one true native Textfield per role. Browser/profile history, snapshot, provider and shortcut stores remain existing native domain services.

History and timeline maintain committed service-query strings. These are query identity, not shadow editing state. Preedit/cancel/reset cannot change them. The 150 ms history request can query and accept its committed value while the editor displays marked text; response admission still checks visibility, profile, committed query and limit, with existing service generation/cancellation protection. Timeline filtering changes only on committed edits.

Posted button/key actions carry tools-generation and workspace/profile identity, and still require a visible current panel. Reopening a form invalidates the old action. Snapshot/shortcut saves reject active composition; provider actions check all three fields. The shortcut recorder explicitly cancels preedit and sets read-only/TEXT_INPUT_TYPE_NONE while recording so an IME cannot consume a chord as text. Native accelerator validation/capture, reserved-key rejection, Escape cancellation and explicit save remain; capture/cancel restore text input.

Three shared scenarios exercise actual Cocoa events, Unicode snapshots/providers, validation, composing key/button suppression, stale form/profile actions, destruction, delayed committed history during preedit and recorder admission/cancel/save. The separate page prompt, form/control containers, broader native menu/undo/focus/IME/AX acceptance and actual performance remain open. [Checkpoint](benchmarks/rust-tools-editors-checkpoint-2026-10-23.json).
