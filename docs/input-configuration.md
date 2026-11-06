# Keyboard shortcuts and mouse gestures

Open **Settings → Search → Keyboard shortcuts**, or find “keyboard shortcuts” or
“mouse gestures” in Command Center. The same panel contains keyboard and mouse
bindings. Settings search also finds the panel by “gestures”.

## Keyboard

Search by action name, stable command ID or combination, and narrow the list to
Navigation, Tabs, Workspaces, Split View or Browser. Select an action, record a
combination or type it, then save. An empty combination disables the action.
F1–F24 work without a modifier; letters, digits and navigation keys require
Cmd, Ctrl or Alt. Common text editing and operating system combinations remain
reserved. On some keyboards, macOS requires Fn to send an actual function key.

A conflicting combination identifies the action that already owns it. Saving
never silently steals another binding. Disable or remap the named action first.
Reset the selected action independently, or restore the complete default keyboard
scheme. Resetting one action also checks for conflicts.

The native accelerator registrations, page input handling, Command Center hints
and macOS menu equivalents use the same binding catalogue. On Windows, F5 is a
compatibility alias for Reload only while Reload retains its default Ctrl+R.
An explicit F5 assignment takes precedence; changing or disabling Reload removes
the compatibility alias.

## Mouse

Gestures are off by default. Enable them in the same panel, then hold **Alt and
the right mouse button**, move in the indicated direction and release. Escape,
releasing Alt, hiding the page, an ambiguous diagonal or a drag longer than 2.5
seconds cancels the action. The first stroke needs 48 device-independent pixels;
subsequent turns need 32 pixels. Short clicks and hand jitter execute no command.

| Gesture | Default action |
| --- | --- |
| ← | Back |
| → | Forward |
| ↑ | New tab / Command Center |
| ↓ | Reload |
| ↑ ↓ | Reopen closed tab |
| ↓ ↑ | Close active tab / Peek |
| ← → | Previous tab |
| → ← | Next tab |

Select a gesture and its action, then choose **Save gesture**. Each gesture can
also be disabled independently. Resetting gestures restores these assignments
without enabling the feature. Reusing an action across several gestures is
allowed. The available actions deliberately exclude bulk tab closure and other
broad destructive operations.

Gestures only start on HTTP(S) pages outside page fullscreen. Ordinary mouse
buttons, selection, scrolling and context menus retain their normal behavior.
The explicit Alt + right-button interaction is reserved while gestures are
enabled, including a short Alt + right click. No page JavaScript is injected.
The page delegate recognizes input before the renderer; commands run after input
dispatch to permit safe tab closure, and target the originating active pane.

## Persistence and implementation

`BrowserTools` owns both catalogues. Bindings are stored atomically in the existing
`ZitterTools.json` beside keyboard shortcuts and search providers. Startup dirty
checks and file merging preserve edits made while the initial read is pending.
Private windows follow the existing policy and do not write persistent settings.
Malformed gesture lists fall back atomically to defaults; unknown commands and
duplicate directions are rejected. Existing files without gesture fields retain
their keyboard settings and receive disabled gestures with default assignments.

The UI reuses the existing Rust-backed editor, select, switch, action group,
typography and theme tokens. Filters rebuild the existing Rust form projection;
there is no second shortcut dispatcher. The keyboard action bar has one primary
Save shortcut action, compact secondary controls and a separate quiet reset row.
The gesture save/reset actions share one row. Short visible labels retain full
accessible names and tooltips. Theme changes explicitly repaint the viewport's
composited contents layer and refresh the Metal scene, so the body cannot retain
colors from the previous theme. `zitter/core/mouse_gesture.h` is the
platform-independent recognizer; `WorkspaceWebView::PreHandleMouseEvent` is the
shared macOS/Windows integration.

## Verification

Core contracts in `tests/product_logic_tests.cc` cover jitter, short drags,
diagonals, timeout, Escape cancellation, excessive turns and recognizer reuse.
The browser tests `InputConfigurationPersistenceAndSafety` and
`InputConfigurationFiltersRecorderAndLifecycle` cover function keys, native F5
execution, gesture admission and execution, persistence, filters, recording,
reset and repeated switching between tools pages followed by window destruction.
The latter can write light/dark previews with
`--zitter-test-input-render-dir=/absolute/output/path`.

### Validated checkpoint — 2026-11-06

On macOS ARM64, both optimized targets (`zitter` and `zitter_prototype`) built
successfully. Validation passed:

- 15 browser tests for the compositor renderer and 4 browser tests for Metal;
- 25 Python tests for localization, design tokens, boundaries and overlay staging;
- both CTest product/workspace contracts;
- the focused GN header check for `//zitter:profile_workspace_browser_tests`.

The input configuration test exports full native-window snapshots for both
sections in both themes. It checks the exported panel dimensions and probes the
editor fill pixels after theme and viewport changes, detecting an editor that
incorrectly disappears into the panel background. The snapshots were also
visually reviewed in both renderers. Local review files are in
`build/input-configuration-review/`, with Metal copies in its `metal/` directory.

The full `//zitter:*` GN audit still reports existing dependency violations in
printing, extension navigation and password import. Native Windows execution
was not available in this macOS session. No release was packaged or published.

### Native editor viewport clipping

Metal editor surfaces explicitly intersect their visible screen rectangle with
ancestor layers that mask to bounds. They crop partially visible editor content
and hide the native surface when the editor is completely outside the viewport.
This also protects the tools panel's fixed heading, close control and footer.

The rendering regression additionally uses a 360 px panel and scrolls the search
editor halfway and completely out of the viewport in both themes. It compares
all pixels outside the scroll viewport against a fixed-region reference and
checks that fully clipped Metal surfaces are hidden. The normal editor-fill
pixel checks remain in place. The follow-up passed 15 compositor browser tests
and 7 Metal browser tests, including existing name-editor and viewport tests.
Both optimized application/test binaries built, and the focused GN check passed.

The latest normal and clipping-stress snapshots are in
`build/input-configuration-clipping-review/`; Metal snapshots are in its `metal/`
subdirectory. `clip-partial-*.png` and `clip-hidden-*.png` demonstrate viewport
boundaries while retaining the fixed header and footer. All snapshots were
visually reviewed after the correction.
