# Profile/workspace integration experiment

## Decision being tested

Can one Views window host persistent page instances from two different regular
Chromium profiles and switch them without sharing website storage or recreating
the active session?

The initial implementation retains two WebViews. Each creates and owns its own
WebContents using a distinct Profile as BrowserContext. Each view stays associated
with that profile for its entire lifetime. Only one is visible at a time.

## Ownership and ordering

1. Chromium's ProfileManager loads the two profiles asynchronously.
2. The harness acquires a ScopedProfileKeepAlive for each profile before use.
3. WorkspaceWindow builds two profile-bound page hosts and the selection controller.
4. UI actions use the controller; successful host activation commits product selection.
5. Window destruction destroys the views and their WebContents.
6. Only then are profile keep-alives released.

All host operations and profile loading callbacks run on Chromium's UI sequence.
The core performs no I/O, profile loading, or Chromium calls. The synchronous
activation contract is limited to already prepared hosts; future asynchronous
loading needs a separate ready/loading/failed state, not blocking activation.

The two profiles are isolation boundaries for website data, not protection from
a malicious OS user or a compromise of the shared browser process.

## Assertions verified on macOS ARM64 (2026-10-15)

- Distinct regular profiles, distinct paths and BrowserContexts.
- Same-origin cookies, localStorage, IndexedDB and sessionStorage remain separate.
- Repeated switching preserves profile bindings and storage markers.
- Unknown workspace activation preserves current selection.
- Hidden/visible page lifecycle transitions follow selection.
- Host recreation keeps persistent website storage and drops sessionStorage.
- Each content host, when selected, reports the same top-level native window.

The Chromium browser test passed all assertions above. The separate CMake test
checks product routing, idempotence and failure handling only. The native window
and workspace button switching were visually verified. Cmd+Alt+1/2 also passed
native macOS event tests in both directions with focus inside each page's input.
See verification.md for the exact scope and observed results.

## Keyboard integration

Workspace commands share the same accelerator target across the main window and
both panel FocusManagers: Cmd+Alt+1/2 on macOS,
Ctrl+Alt+1/2 on other desktop builds. A small WebView subclass prehandles those
reserved chords before a page can cancel them with preventDefault(). It retains
WebView's original delegate ownership and lifecycle behavior. Key-release state
belongs to the window because switching moves focus to another WebContents.

On macOS, a standalone Views window also needs a Cocoa CommandDispatcherDelegate.
The generic dispatcher does not provide Chrome's routing of Command-key
equivalents into the Views accelerator registry. `workspace_keyboard_mac.mm`
connects only the window's high-priority handlers, retaining default AppKit
handling for other keys. It does not import Chrome's full command dispatcher.
The native bridge owns this delegate and clears it with the window. Panel
delegates also own an application-local NSEvent monitor enabled only while their own
panel is key. Events queued against the previous main window are retargeted to
that active panel. It forwards panel keys through NSWindow's responder/text-input path,
and gives Command equivalents their normal routing first. This fixes observed
AppKit application dispatch swallowing panel keydowns; it is not a global keyboard
hook. The monitor is removed with the delegate. IME composition and alternate
keyboard layouts still require manual platform QA.

## Window design direction

The selected visual direction is a dark, minimal window with an auto-hiding
vertical sidebar. The native Views sidebar overlays content, opens from the left
edge or an explicit button, and can be pinned. Workspace and profile names appear
only inside this sidebar. The compact window frame does not repeat them or the
"New tab" title. Cmd+L / Cmd+T opens a slim floating address/search bar in the
Arc/Spotlight style, replacing a full-page welcome screen.

Each workspace has its own custom accent color and subtle surface tint. These
belong to workspace appearance settings, independently of the assigned Chromium
profile; two workspaces sharing a profile can still have different colors.
Names and selection states must remain legible regardless of the chosen accent.
`WorkspaceShell` implements this layout in Views. The main window owns its page
hosts; transient owned child widgets place the sidebar and address palette above
native WebContents without reparenting or recreating them. Pinning deliberately
reserves content width. Workspace metadata stays inside the sidebar.

Cmd+L edits the current address; Cmd+T opens a new page in the active profile only
after Enter. Escape returns focus to the page. Both overlay focus managers share
the browser shortcut registry. Bare addresses use Chromium URL fixup; text queries
use DuckDuckGo. The prototype accepts HTTP(S) navigation only.

Preset colors and custom #RRGGBB values update the selected workspace immediately.
`ZitterAppearance.json` under the harness user-data directory stores both colors
and sidebar pinning. A sequenced blocking task runner loads and atomically patches
settings off the UI thread. The browser-test harness uses temporary profiles, so
this proves persistence across window recreation within a session, not a stable
installed application's user-data path. The manual launcher remains a test harness.

## Remaining architecture risk

A views::WebView is a content host, not a Chrome tab. This probe does not register
its pages in TabStripModel / TabInterface / BrowserWindowInterface or install the
full set of browser tab features. Therefore the following remain **unproven**:

- Chrome extension tabs/windows APIs, action popups, and profile-specific extensions;
- visited-URL recording in Chrome history services;
- window.open and external/new-tab navigation routing;
- permissions and device selection dialogs;
- downloads, fullscreen, beforeunload and renderer crash recovery;
- accessibility/focus correctness across platform-native hosts;
- durability across process restart;
- Windows and Linux build/runtime compatibility.

Do not infer production readiness or extensions compatibility from storage passing.
Do not patch a single TabStripModel to accept mixed profiles to make a test pass.

## Next gate: real browser context ownership

Use the outcome of this probe to choose how profile-specific browser window/tab
contexts map onto our single visible shell. The integration must preserve the
upstream assumptions of profile-scoped browser services. A host must not simply
steal a WebContents native view from a second BrowserView while its original
window continues observing and controlling it.

Before choosing that integration, add contract tests for two MV3 extensions with
different per-profile state, extension-initiated tabs, action popup anchoring,
history queries, user-gesture window.open, permission prompts and beforeunload.
Document which upstream files need hooks and their affected ownership contracts.

## Updating upstream

Change the pinned revision deliberately. Inspect affected ProfileManager,
keep-alive, WebView and Widget APIs, then adapt the integration. Run GN checks,
browser tests and the manual window on every desktop platform. An overlay that
installs cleanly is not evidence that its API or runtime contracts still work.
