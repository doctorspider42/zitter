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

The selected direction (design canvas "Zitter Browser") is a light, slim
Arc/Zen-style window: a frame tinted by the active workspace's color, with the
page as a rounded white card. On macOS the window keeps its native traffic
lights in a transparent, full-size titlebar (`workspace_frame_mac.mm`); the
44 px header row next to them holds sidebar, back, forward and reload commands,
and empty frame space drags the window.

Tabs live in a vertical sidebar on the left, visible (pinned) by default. It
holds the address pill, the workspace name, "New tab", the tab list (monogram
favicon stand-ins, close on hover), the workspace color and the workspace dots.
Unpinned, it auto-hides and peeks over the page as a floating card from the left
edge. Cmd+L / Cmd+T opens a command palette: typed text resolves to an address
or a DuckDuckGo search, and matching open tabs can be switched to; arrow keys
select, Enter opens.

Each workspace has its own custom accent color, which tints the frame. These
belong to workspace appearance settings, independently of the assigned Chromium
profile; two workspaces sharing a profile can still have different colors.
`WorkspaceShell` implements this layout in Views. The main window owns its page
hosts; transient owned child widgets place the sidebar and palette above native
WebContents without reparenting or recreating them. Pinning reserves the
sidebar column. Workspace metadata stays inside the sidebar.

Cmd+L edits the current address; Cmd+T opens a new page in the active profile only
after Enter. Escape returns focus to the page. Both overlay focus managers share
the browser shortcut registry. Bare addresses use Chromium URL fixup; text queries
use DuckDuckGo. The prototype accepts HTTP(S) navigation only.

Preset colors and custom #RRGGBB values update the selected workspace immediately.
`ZitterAppearance.json` under the user-data directory stores both colors and
sidebar pinning (pinned unless saved otherwise). A sequenced blocking task runner
loads and atomically patches settings off the UI thread.

On macOS Zitter owns the app-level entry points that would otherwise open stock
Chrome Browser windows (`chromium/app/app_shell_mac.mm`): Dock reopen raises the
Zitter window, the Dock menu offers "Nowa karta", links and Handoff from other
apps open as tabs (queued until the window exists), and Zitter's own main menu
replaces Chrome's (no New Window / Incognito / History items). AppController
stays `NSApp.delegate`, as Chrome CHECKs; only those methods are replaced, and a
CHECK flags them if an upstream update renames one. As a last resort `Runtime`
closes any stock normal Browser window that still appears and logs an error.

The app icon is the Dirac ket |z⟩ ("E1 Klasyczny"), sourced from
`assets/icon/zitter.svg`; `tools/build_icon.py` renders `Zitter.icns`, which the
packager installs. Onboarding (welcome, import, setup), Chromium settings and the
DMG installer from the design canvas are not implemented yet.

## Remaining architecture risk

A views::WebView is a content host, not a Chrome tab. This probe does not register
its pages in TabStripModel / TabInterface / BrowserWindowInterface or install the
full set of browser tab features. Therefore the following remain **unproven**:

- Chrome extension tabs/windows APIs (see "Extensions" below);
- visited-URL recording in Chrome history services;
- window.open and external/new-tab navigation routing;
- permissions and device selection dialogs;
- downloads, fullscreen, beforeunload and renderer crash recovery;
- accessibility/focus correctness across platform-native hosts;
- durability across process restart;
- Windows and Linux build/runtime compatibility.

Do not infer production readiness or extensions compatibility from storage passing.
Do not patch a single TabStripModel to accept mixed profiles to make a test pass.

## Extensions

Each Chromium profile keeps its own extensions; Zitter adds no state of its
own. `ExtensionsBar` (`extensions_bar.cc`, design: Extensions) sits at the
right end of the header band and always follows the profile of the page on
screen: its pinned actions come from that profile's `ToolbarActionsModel`, the
menu lists that profile's extensions, and switching to a space with another
profile swaps them. Icons, titles, badges and the enabled state are read per
tab from `ExtensionAction` through `ExtensionActionIconFactory`, and every
`OnToolbarActionUpdated` repaints the action, so `action.setIcon`,
`action.disable` or `setBadgeText` (a password manager going grey when signed
out) show at once.

Every page gets the extension tab helpers (`PageServices`): content scripts,
`activeTab`, the action runner and webNavigation work in Zitter tabs. A click
runs `ExtensionActionRunner::RunAction`; popups are `ExtensionViewHost`s with a
Zitter delegate (no Browser) in a bubble under the icon. Extension pages,
`chrome://extensions` and the Web Store open as tabs of a space using the
extension's profile; tabs an extension opens through a stock window move into
Zitter before that window closes. Chrome's post-install bubble is disabled (it
needs a stock window); the puzzle trembles instead.

The tabs API sees Zitter tabs through one guarded Chromium hook,
`ExtensionTabUtil::EmbedderTabs` (`patch_extension_tabs` in
`tools/install_chromium_overlay.py`). `WorkspaceWindow` provides each profile's
tabs (every space of that profile, in sidebar order) and the page on screen.
`GetTabById` finds them for callers that need no window, so
`tabs.sendMessage`, `scripting.executeScript({tabId})` and
`action.setIcon/setBadgeText({tabId})` work; `tabs.get` falls back to them, and
`tabs.query` lists them as one window per profile: the current window when the
caller has no browser window (popups, service workers), the last focused one
while no browser window is active. `extension_tab_events.cc` sends
`tabs.onCreated`, `onUpdated` (loading, url, title), `onRemoved` and
`onActivated`, scrubbed per extension as Chromium's TabsEventRouter does.

`chrome.windows` sees each profile's Zitter tabs as one window of type
`normal`: a `WindowController` per profile (as Document PiP has one), backed by
the Zitter widget and registered in `WindowControllerList`. Every page carries
its window id. It is the current window when the profile has no Browser window,
and the window of the profile's popups. `tabs.create`, `tabs.update` (url,
active, muted), `tabs.move` within the space on screen and `tabs.remove` act on
Zitter tabs through the embedder; `windows.getAll/get/getCurrent/
getLastFocused` describe the window. `windows.remove` is refused: the window
holds every profile's spaces.

`windows.create` for a normal window opens its pages as tabs of that window
(Zitter keeps one window) and `windows.update` focuses, minimizes, maximizes,
restores, moves and resizes it.

Not covered: tab groups (Zitter has none), Split View and multi-tab
highlighting through the tabs API, and moving a tab between spaces or
windows. A real BrowserWindowInterface remains the gate for those.

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
