# Local verification — 2026-10-15

## Environment and build

- Platform: macOS ARM64, 36 GiB RAM, 12 logical CPUs.
- Chromium revision: `1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`.
- Source: official Chromium GitHub mirror; the primary googlesource endpoint
  stalled during the initial fetch. Dependency revisions came from Chromium DEPS.
- GN: `2596 (0817afa57742)`; Chromium's pinned native ARM64 Clang toolchain.
- Build: debug, component libraries, symbols disabled, local Ninja, no remote build.
- Xcode's missing Metal Toolchain was installed using
  `xcodebuild -downloadComponent MetalToolchain`.
- Blink per-type generators were built with Ninja `-j 1` to avoid nesting many
  CPU-sized Python pools. C++ compilation then used `-j 12`.

The independent core, GN header check, Chromium compilation and final link passed.
The executable is `.chromium/src/out/Zitter/zitter_prototype`.

## Chromium runtime test

Command from the project root:

```sh
python3 tools/run_prototype.py --verify
```

Observed result in `.chromium/isolation-test.log`:

```text
[       OK ] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (14036 ms)
[  PASSED  ] 1 test.
SUCCESS: all tests passed.
```

The process exited with code 0. Assertions verified:

- Two distinct regular Profiles and BrowserContexts.
- Each selected content host belongs to the same top-level native window.
- Different cookies, localStorage, IndexedDB and sessionStorage at the same URL.
- Twenty work/personal switching cycles preserve the independent markers.
- Unknown workspace selection preserves the previous active workspace.
- Destroying and recreating both page hosts retains cookies/localStorage/IndexedDB
  and drops sessionStorage, as expected.

After the interactive launcher changes, the final executable was tested again:
`.chromium/isolation-recheck.log` reports one passing test (14312 ms),
`SUCCESS: all tests passed`, and exit code 0.

This is not a browser-process restart test. It does not verify history recording,
extensions integration, permission dialogs, downloads or DRM.

## Interactive window

```sh
python3 tools/run_prototype.py
```

After the user unlocked macOS, desktop inspection confirmed the native
"Zitter — profile/workspace probe" window, the rendered storage probe page and
the Work/Personal controls. Clicking Personal through accessibility changed the
status to `Workspace: personal | Profile: personal-profile`; clicking Work
changed it back to `Workspace: work | Profile: work-profile` in the same window.

The first interactive attempt did not deliver native shortcuts. The keyboard
integration below fixes their routing; standalone accessibility button switching
was not treated as keyboard verification. The manual test now explicitly uses
Chromium's `ShowAndFocusNativeWindow()` helper to activate the test window.

The macOS manual launcher creates a local `.app` identity in the build output
using a hard link to the executable and links to its resources. An explicit
`--browser-subprocess-path` retains the working standalone test subprocess path.
This is a development wrapper, not a signed/distributable application. Launch
through the Python command above, which supplies the required test arguments.
The manual session has a one-hour run-loop timeout; the command reopens it
without rebuilding if the session has ended.

The manual launcher uses `--single-process-tests`, which keeps the test/browser
in one launcher process. It does **not** use Chromium's `--single-process` mode;
renderer and GPU processes remain separate.

## Keyboard verification — 2026-10-15

Final command: `python3 tools/run_prototype.py --verify`.
The build and GN header dependency check passed. The final run exited with code 0;
`.chromium/keyboard-verified.log` reports:

```text
[1/3] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (16463 ms)
[2/3] ZitterProfilesBrowserTest.KeyboardWorkspaceSwitching (12758 ms)
[3/3] ZitterProfilesBrowserTest.NativeKeyboardWorkspaceSwitching (12715 ms)
SUCCESS: all tests passed.
```

The page-routing test sends keyboard events through the real RenderWidgetHost /
WebContents delegate path, focuses the text input, waits for renderer input ACKs,
types `x` and `y` in separate pages, and switches both ways while the page tries
to cancel Alt chords with preventDefault(). Reserved digit keydowns do not reach
the page. Synthetic Char and KeyUp events target the newly active host, checking
the window's shared shortcut-consumption state.

The macOS test uses Chromium's ui_controls to post native NSEvents to the AppKit
event queue. It first switches from the address field, then types into both page
inputs and verifies Cmd+Alt+1 from Personal to Work and Cmd+Alt+2 from Work to
Personal. Three further round trips verify `document.activeElement.id == 'marker'`
and preserved `x` / `y` values. This is native event-queue integration testing,
not a claim of physical-keyboard testing or Windows/Linux runtime verification.

The fixes are confined to the Zitter overlay: a WebView delegate extension
prehandles reserved workspace commands, and a small Cocoa dispatcher connects
standalone Views Command-key equivalents to the same FocusManager registry.
The native bridge owns that dispatcher. Other AppKit key handling is retained.
No upstream Chromium source change was required for the keyboard fix.

The installer tests (4/4) and product-boundary check also passed. The native Views shell verification below supersedes the earlier diagnostic UI.

## Observed upstream/runtime diagnostics

The successful test log also contains component-build diagnostics about a duplicate
`ANGLESwapCGLLayer` Objective-C class, Metal shader-cache sandbox extensions and an
unimplemented actor tool definition. These did not fail this storage test. The
probe page was visibly rendered; broader GPU behavior was not tested. No
`--no-sandbox` or certificate-bypass flags were added to the launcher.


## Native Views shell — 2026-10-15

The approved window design is now compiled into `zitter_prototype`. It uses a
minimal dark frame, an edge/button-triggered sidebar with pinning, a floating
address/search textfield and independent workspace colors (presets or #RRGGBB).
Workspace/profile labels appear only inside the sidebar. The start page is blank;
the browser controls are native Views rather than page HTML.

Build, GN header checks, the core contract test, four installer tests and the
product-boundary check passed. Final command `python3 tools/run_prototype.py
--verify` exited 0. `.chromium/native-shell-verified.log` reports:

```text
[1/4] ZitterProfilesBrowserTest.StorageIsolationAndSwitching (16551 ms)
[2/4] ZitterProfilesBrowserTest.KeyboardWorkspaceSwitching (12558 ms)
[3/4] ZitterProfilesBrowserTest.NativeKeyboardWorkspaceSwitching (12837 ms)
[4/4] ZitterProfilesBrowserTest.NativeShellAndAppearance (14953 ms)
SUCCESS: all tests passed.
```

The native shell test opens the address palette with Cmd+L, types `x` from an event queued against the previous main window, selects
all with Cmd+A and replaces it with `y`, closes it with Escape, and opens a new
tab through Cmd+T / Enter. HTTP navigation creates a second distinct WebContents
in the same workspace profile and retains the original page. A rejected file URL
leaves the palette open without adding a tab. Native Return in the color field
rejects invalid input and accepts #D49771. Work/personal colors and pinning survive
window recreation after pending writes complete. A resize checks panel anchoring.

The macOS panel integration required an application-local NSEvent monitor to
forward panel key events through NSWindow's standard responder path. It accepts
application events while its own panel is key, retargets keys queued for the
previous main window, preserves Command-equivalent handling,
and is removed with the bridge-owned delegate. The original storage and keyboard
regression tests passed with this routing enabled. IME/alternate layouts,
Windows/Linux runtime behavior and animation polish remain unverified. Appearance
persistence is checked across window recreation in the temporary test harness;
the launcher still does not provide a production user-data installation.

After compacting menu/color-button sizes, `NativeShellAndAppearance` was rerun
and passed (13496 ms, launcher exit 0); see
`.chromium/native-shell-layout-verified.log`.

Live desktop verification after launch confirmed the native sidebar, compact
address palette, text entry and Enter navigation. Work was changed to #D49771
through the color field/save button. Cmd+Alt+2 selected Personal with its separate
#84B6A4 color; Cmd+Alt+1 restored Work and #D49771 in the same visible window.
The final window remains open with the sidebar pinned and the address palette
visible above the local blank start page. The manual harness closes after one
hour; `python3 tools/run_prototype.py` starts another session.

An attempted navigation to https://example.com failed with a DNS error in this
browser-test environment. Local HTTP navigation succeeded. External Internet
browsing is therefore not verified by this run; the final display uses the
embedded test server's blank start page. The development harness must be replaced
or explicitly configured before treating it as an everyday browser.


## Standalone desktop app — 2026-10-15

The separate `//zitter:zitter` executable built successfully on macOS ARM64.
`gn check out/Zitter //zitter:zitter` passed. It uses the normal Chrome bootstrap,
not the browser-test runner, and opens `build/Zitter.app` with stable profile paths.

Native helper packaging was corrected to keep real helper bundles under the
outer app's Frameworks directory, including component dylibs needed by the
sandbox launcher and framework. A two-string, guarded upstream patch gives
OSCrypt a separate Zitter Safe Storage Keychain item. Before this change, cookie
key retrieval blocked inside macOS Security while accessing Chromium's item.
After rebuilding with the independent item, the real app loaded the local HTTP
probe and reported a successful write of `work-20261015` to localStorage, a
persistent cookie and IndexedDB in the Work profile. The rendered diagnostic
page was also verified through native accessibility.

Core CTest, product dependency boundaries and seven Python installer/packager
tests passed. A fresh browser-test binary was being rebuilt when the user asked
to push the current state and stop. That regression run, external HTTPS
navigation and full-process restart checks of both profiles and colors have
not been completed for this standalone version. Earlier harness results above
remain distinct from standalone-app verification.

## Dynamic workspaces and profile assignment — 2026-10-16

The workspace hosts and selector now use the saved workspace list rather than a
fixed pair. `ZitterWorkspaces.json` records stable IDs, display names, existing
profile assignments and the active workspace. Defaults retain the original Work
and Personal profile directories. Colors retain compatibility with the existing
appearance file and also support dynamically generated workspace IDs.

Both `zitter` and `zitter_prototype` built on macOS ARM64; GN header checks for
the desktop and browser-test targets passed. Core CTest, the dependency-boundary
check, and seven Python installer/packager tests passed. The five-test browser
suite passed, including storage isolation, both keyboard switching tests, native
shell appearance, and the new `DynamicWorkspacesAndProfiles` test.

The dynamic test was then expanded to invoke the actual Views add/edit/delete
buttons and workspace combobox. This final version passed separately (17829 ms).
It rejects blank names and invalid profile IDs, creates a named workspace sharing
the Work profile, verifies same-origin shared storage, reassigns the workspace to
Personal with tab recreation, deletes the original Work workspace, and recreates
the window. The removed space stays absent; the custom name, profile assignment,
selection and color survive. Removing spaces retains the shared profile's site
storage; removal of the final workspace is rejected.

`build/Zitter.app` was repackaged from the new desktop executable. Manual visual
inspection was unavailable because macOS was locked. Persistence was checked
across window recreation in the test harness, not a full desktop-process restart.
The profile picker exposes the existing company/personal profiles; creating and
deleting Chromium profiles is outside this UI.

## Dynamic profile catalogue and Arc import — 2026-10-16

Profile ownership moved into the workspace window. Startup reads the saved
catalogue before loading Chromium profiles; the original company/personal
profiles are migrated without moving their data, but removed defaults are no
longer recreated. New profiles have UUID directory names and independent
Chromium BrowserContexts. The profile manager UI blocks deletion of profiles
referenced by workspaces and requires an explicit second click before clearing
an unused profile's data. The catalogue/deletion marker is written atomically
before Chromium's normal DeleteProfileHelper is invoked; interrupted deletion
is retried on startup. A failed write restores the catalogue entry.

The Arc importer reads local sidebar metadata plus profile names from Local
State. It handles Swift's alternating dictionary arrays, explicit Space order,
profile-shared Favorites, nested lists and supported Split View children. It
imports only pinned HTTP(S) links and creates isolated destination profiles.
The source Arc files, cookies, credentials and browsing history are not modified
or copied. Tabs load lazily from the native, scrollable sidebar. Existing spaces
remain; repeated imports skip spaces identified by their Arc IDs. Import adds
are rolled back if their configuration cannot be saved. The first-run offer can
be skipped; its dismissal is persisted.

The focused four-test run passed: DynamicProfiles,
ArcImportStructureAndPersistence, FirstRunImportOffer and the explicitly enabled
LiveArcImport. The live test used the user's local Arc installation with their
permission, imported into the harness's temporary user-data directory, recreated
the window, and compared every saved pin's ID, parent, title and URL. It verified
**2 profiles, 4 Spaces, 59 folders and 374 pins**. The one unsupported item was
Arc's internal `welcomeToArc` placeholder; no pinned web page was omitted.
Private titles and URLs were not added to repository fixtures or test logs.

Synthetic fixtures cover nested folders, a Split View, shared profiles and
Favorites, an unused profile, an empty Space, and a rejected JavaScript URL.
They also verify lazy pin reuse, persistence and duplicate prevention. Profile
lifecycle tests verify real Chromium storage after recreation, deletion of an
original default profile, and protection of used/last profiles.

The final failure-injection test replaces the destination configuration file with
a directory so atomic writes fail. An attempted profile deletion restores the
entry and retains its cookie/localStorage/IndexedDB marker; failed profile
creation does not leave a visible catalogue entry, and failed Arc import rolls
back its added profiles/workspaces. The test passed with real Chromium storage.

The final full Chromium regression run passed all **10 tests**, including both
native keyboard tests and NativeShellAndAppearance (launcher exit 0). Core CTest,
product dependency boundaries, both GN header checks and seven Python tests also
passed. The desktop app was rebuilt and repackaged.

With user authorization, the native first-run import UI was then exercised in
`build/Zitter.app` against the actual Arc installation, adding the same 2
profiles and 4 Spaces to the user's ordinary Zitter data directory. Native UI
checks verified the preview, import completion, Space selection and folder
collapse. A large-list layout issue found in this check was corrected by giving
the flexed ScrollView a zero preferred height; the workspace selector now stays
inside the main window while its pins scroll separately. Folders also use a
folder icon. ArcImportStructureAndPersistence and LiveArcImport were rerun after
this fix and passed, with explicit viewport bounds checks on every live Arc
Space. Finally, closing the desktop window and launching the app again restored
the imported catalogue, pins, folders and selected Space without repeating the
first-run offer. The updated app remains open on an imported Space.

## Workspace dots, profile context menu and switching — 2026-10-16

The footer uses dynamic workspace dots again. Right-clicking a dot opens a native
context menu with a **Set profile** submenu containing checked profile choices,
profile editing and new-profile creation. Assignment and creation target the
clicked workspace even when inactive, without changing the selected workspace.
The editor can rename a profile while retaining its directory, ID and storage.
The context-menu contract test covers reassignment, renaming, creation with
assignment, dot selection and persistence after recreating the window.

The sidebar now retains a native row tree for each workspace. Pin equality is
checked before changing its tree; child relationships are indexed once rather
than repeatedly scanning all pins. Trees are assembled off the live hierarchy,
and folder toggles only hide/show their retained child groups. Active highlights
are updated in place. Appearance changes no longer rebuild tabs, and switching
saves just the selected ID through the sequenced I/O runner rather than
serializing all pinned metadata on the UI sequence.

All **11 Chromium tests passed**, including the new WorkspaceDotProfileMenu and
the opt-in live Arc import test. After warming the four imported Spaces, 40
successive Select calls did not rebuild any pinned lists. The slowest measured
call took **27.63 ms** on this macOS development build (this measures the UI
selection call, not asynchronous renderer presentation). Both GN header checks
passed. Native desktop checks confirmed all six dots, the real right-click
Set profile menu, the edit/new actions, and switching between imported Spaces.
The app was repackaged and remains open on Work with the restored dots.

## Split View and workspace sessions — 2026-10-16

The native content host lays out one or two WebViews with a captured ResizeArea
separator, constrained to a 20–80% ratio. The split button toggles the second
pane; pane buttons and direct page interaction choose the target for navigation
and the address palette. Sidebar tabs and pins support a right-click **Otwórz
obok w Split View** action and dragging into the right half of the content area.
Deferred actions use weak shell references and check the originating workspace.

Each workspace keeps its own left/right selection, active tab and proportions.
Closing a visible tab collapses to the survivor; turning the split off keeps the
other tab available. Reassigning the workspace profile discards its old page
hosts and split. Both panes use that workspace's profile.

`ZitterSession.json` stores page URLs, pin associations and layout indices,
separately from the imported pin catalogue. Writes are debounced, atomic and
ordered on the existing sequenced blocking-shutdown I/O runner. Shutdown also
flushes the current snapshot. Restoration checks the workspace's profile ID and
only accepts HTTP(S) URLs; background pages navigate lazily when displayed.
This does not restore forms, sessionStorage or the navigation stack. The desktop
startup leaves a restored page visible instead of always opening the palette.

Renderer/native focus notifications during restored page attachment must not
change the selected pane. The selected pane follows explicit shell commands and
WebContents user interaction instead. The session-restoration regression test
checks that the chosen left pane remains selected after both pages load.

Source inspection confirms that WebViews create standalone WebContents without
Chrome history/password-manager/autofill tab helpers. Persistent profile/site
storage is active; global history recording, password save prompts, autofill and
Chrome settings UI remain outside the current shell integration.

Verification: all **13 Chromium tests passed** (418 seconds), including real Arc
import, isolation, catalogue rollback, native keyboard routing and both new split
regressions. The cached Arc switching call peaked at **29.79 ms**, with four pin
list builds unchanged. After the final URL-label and current-page-copy changes,
both split tests passed again (34 seconds). The lifecycle test was then extended
to hit-test the actual ResizeArea and deliver press/drag/release events through
it; the resulting ratio changed and that test passed (11.5 seconds).
Both GN dependency checks, the independent core test, seven Python tests and the
boundary checker passed; `git diff --check` was clean.

The production app was rebuilt and repackaged. Native desktop observation
confirmed the split header, two panels, collapse without closing the other tab,
and session persistence/reopening. The Mac subsequently locked, and the CUA
server could not unlock it, so a full physical drag/drop check remains pending
manual unlock. The separator's direct Views event path is covered by the test
above; a successful physical drag/drop is not claimed here.

## Chromium history, passwords and command palette — 2026-10-16

Every page attaches Chromium's session, history, autofill and password clients
before its first navigation. A standalone-host adapter records committed HTTP(S)
main-frame visits through HistoryService and HistoryTabHelper's navigation
metadata. Chromium's normal helper excludes pages without BrowserWindow, so the
adapter does not duplicate its records. History recording respects the profile's
history policy/preferences, and queries use only the active profile.

Cmd+K merges local tabs, pinned links with folder paths, workspace names and
asynchronous profile history. It switches workspace when selecting a tab or pin.
Queries are debounced and canceled; generation/profile/text checks discard stale
responses. Results are scrollable and retain keyboard selection during updates.
Cmd+L/T retain the typed address/search action first, while Cmd+K prefers matches.

Password form parsing, storage, filling and account suggestions use Chromium's
real password manager. Native dialogs ask before saving/updating, identify the
site/account/profile, and never expose password plaintext. The password header
button and menu open a save fallback or saved-account metadata overview. No
passwords enter Zitter's JSON. Pending form managers are owned by their page;
dialog callbacks hold weak references so closing a page or profile cannot retain
profile-dependent password metrics. Navigation and hiding dismiss pending consent.

Full address/card save UI requires Chrome's BrowserWindow. Payment support is
disabled using Chromium's per-page embedder capability; a narrow idempotent
upstream guard disables address UI for hosts without TabInterface. Neither changes
profile preferences. Single-field autocomplete remains enabled. Password
reveal/edit/delete, downloads management and chrome://settings are outside this
stage.

Verification: all **15 Chromium scenarios passed across the final runs**. The
full suite covered real Arc import, split/session persistence, profile storage,
dynamic profile/workspace controls, rollback and native keyboard/appearance
behavior (14 passed in that run). The expanded password test initially exposed
a harness race: the first user gesture accepted Chromium's outstanding suggested
username after the test had cleared it, creating `alicebob` rather than `bob`.
The test now waits for autofill and commits the first gesture before replacing
fields, checks submitted username/password values, and acknowledges the click
on the emptied username field. Its final run passed (22 seconds), covering
reject/save/update, actual filling, two-account native suggestions, metadata
overview, hidden consent cancellation and profile isolation. NativeShellAndAppearance
also passed after adding a real Cmd+K keystroke, empty palette and Escape check
(13 seconds). No production code changed during those final harness repairs.

Both GN header checks, eight Python tests, the core contract test and dependency
boundary checker passed. `git diff --check` was clean. Production and test binaries
built successfully and `build/Zitter.app` was repackaged. The Mac remained locked,
and CUA reported that it could not unlock it, so a manual visual inspection of
the new palette/password dialogs is pending. The previously running application
must be restarted to load the new executable; launching the new bundle by hand
or manually inspecting it while locked is not claimed here.


## Peek and domain link routing — 2026-10-16

Cmd + left-click (Ctrl on Linux) is identified from the native mouse modifiers
and Chromium's new-background-tab disposition. Middle-click remains a background
tab. Page/link, sidebar tab/pin and address-pill menus expose Peek and domain rules.
The preview is a child Views widget with navigation controls, owner-workspace and
host label, promotion buttons and Escape/close support. Promotion reparents the
same WebView instead of reloading its URL, preserving form and navigation state.
The preview is excluded from the session snapshot and closes on workspace switch,
target-profile reassignment and application teardown. The profile cannot change
under an existing WebContents. Deferred input/menu actions and weak callbacks
avoid deleting the source page on its renderer callback stack.

Exact canonical hosts map to workspace IDs in ZitterWorkspaces.json. The current
profile is resolved from the workspace, so assigning a different profile does not
leave a stale profile in a rule. Deleting the workspace prunes those rules. The
context menu can replace or remove a domain rule, and shows profile names beside
workspace choices. External URLs enter the same routing path, and the development
bundle declares HTTP/HTTPS URL handling for macOS.

LinkNavigation registers a throttle only for Zitter-owned pages. Production's
BrowserClient and the browser-test client install the same throttle factory.
Only explicit renderer-initiated primary-frame GET links are rerouted, before a
network request starts; forms, frames, redirects, same-document/history
navigations retain their request semantics. New WebContents receive the clients
before navigation. Routed target=_blank links discard an empty provisional
source-profile tab after cancellation. A routed Peek is created with the target
workspace's profile before loading and only switches workspace on promotion.

Verification: **all 18 Chromium browser tests passed in one full run** (268
seconds), including the opt-in live Arc import and the existing history,
password, split/session, profile, workspace, rollback and native keyboard tests.
The three new scenarios verify actual Cmd-click and native Escape, unchanged
source form/tab state, live-page promotion into a tab and Split View, target=_blank
hosting, rule selection/removal via the menu model, restoration across restart,
external-URL entry, profile reassignment and deletion of a rule target. A server
request monitor confirms a routed link sends exactly one request with the
personal profile's cookie and never the source work cookie. The form/frame/
redirect regression confirms those navigations retain the source workspace.

Both GN dependency checks, eight Python tests (including the HTTP/HTTPS bundle
metadata assertion), the core contract and product-boundary checks passed.
`git diff --check` was clean. Production and browser-test binaries were rebuilt;
`build/Zitter.app` was repackaged and its executable verified against the current
build. The Mac remains locked and CUA cannot unlock it; manual visual inspection
of the new panels/menus remains pending, despite the automated native event
coverage. The previously running app must be restarted to use this build.

### Native desktop follow-up after manual unlock — 2026-10-16

Restarted the packaged production app through CUA and checked the actual Peek
panel using the loopback links_probe page. Cmd-click opened the preview with the
owner-workspace/host title and visible promotion/close controls. Text entered into
the preview form (`peek-ui-check`) survived promotion into a full tab. A real
right-click on the page showed the Peek/new-tab/Split actions, exact-host rule
submenu with all six workspace/profile labels and disabled removal for an
unconfigured domain. Layout and text were visible without clipped controls at
the current window size. No persistent domain rule was created in the user's
profile during this visual check. The test-created promoted tab was closed with
its sidebar close button; the source tab was left on Example Domain. The new
production app remains running. A synthetic Cmd+W did not close the promoted tab
in this desktop run, so successful physical Cmd+W dispatch is not claimed.


## Themes, general settings and persistent sidebar transitions — 2026-10-16

General settings live in their own scrollable native panel, reached through the
header gear, Cmd+, and the macOS application menu. It contains system/light/dark
appearance, typed-search provider, sidebar pinning and workspace animation, plus
links to the existing workspace/profile/password/import controls. Typed searches
use the selected provider. Appearance writes patch individual fields on the
existing sequenced I/O runner and retain workspace colors; startup merges avoid
overwriting preferences changed before asynchronous loading finishes.

Widgets share an optional explicit color-mode override; system mode follows
NativeTheme notifications. Dynamic custom paints and labels use the same dark
palette, and the native AppKit window appearance is scoped to Zitter. Native
controls/dialogs use grayscale text on translucent layers. This fixes a
pixel-enabled test crash caused by subpixel text on a transparent native-control
layer. WebView's normal ColorProviderSource propagation updates the renderer's
preferred color scheme without a forced CSS inversion. Peek and password
metadata/save dialogs inherit the mode too.

Workspace selection closes secondary popups while retaining a visible sidebar,
without changing auto-hide/pinning state. A 180 ms directional transition moves
incoming sidebar rows on their own layer and shifts native page bounds into
place without resizing or reloading documents. Rapid selections stop previous
motion before targeting the newest workspace; the compositor tree is reused.
Animations honor the app preference and OS reduced motion. Text configuration is
performed on panel creation/theme changes, rather than walking all cached rows
on every workspace selection.

Validation: production and browser-test binaries rebuilt successfully and the
packaged app was refreshed. All 20 pixel-enabled browser integration tests passed
in the full serial run (478 seconds), including the two new theme/settings and
sidebar/animation regressions. They cover live renderer color-scheme changes,
system-mode updates, settings persistence, search dispatch, dark Peek/password
dialogs, rapid workspace changes, cached rows and reduced motion. The read-only
live Arc fixture retained its four cached pin trees; maximum measured switching
work was 47.3 ms. Both GN dependency checks, all eight Python tests, the core
contract and product-boundary checks passed.

Native desktop verification: restarted build/Zitter.app through CUA, opened the
general panel with its header gear, selected Ciemny, and checked the readable
dark panel and browser/sidebar appearance. Switched from Osobiste (Arc) to
Prywatne and back using the footer dots with the sidebar unpinned: it stayed
visible. Cmd+, reopened settings with Ciemny selected, DuckDuckGo unchanged and
animations enabled. The latest production app remains running in dark mode.
Animation timing/intermediate frames are covered by automated tests rather than
claimed from settled desktop screenshots.

## Split tabs grouped in the sidebar — 2026-10-16

Removed the numbered 1/2 pane buttons from the header. A split now contributes
one outlined sidebar row containing both tabs horizontally, in left/right pane
order. Each half retains its title, icon, close action, drag and context menu;
the active page is highlighted. Clicking a half focuses that pane and preserves
the open sidebar. Ending the split returns ordinary tabs to separate rows.
Pinned pages temporarily join the pair without a duplicate pinned row, and
return to their original folder when the split ends. Cached pin trees remain
intact. The shell receives explicit pane indices rather than deriving them from
the currently focused tab. The shell GN target also declares its existing
profile-util header dependency directly.

The first pixel run exposed a missing accessibility group role; assigning kGroup
before its name fixed the DCHECK. The subsequent eight-test regression run
passed (117 seconds), including settings/theme, workspace slides, Peek, split
lifecycle/restore, fixture/live Arc import and native shell appearance. Live Arc
switching retained all four cached pin trees (maximum 25.516 ms). After adding an
explicit compact preferred width, all three split lifecycle/restore and pinned
Arc tests passed again (50 seconds), including assertions that both columns fit
their group and sidebar. Native CUA confirmed the header has no numbered pane
buttons, restored the existing Arc split, and showed the two tab buttons side by
side in a test split in Prywatne. The extra empty tab used for this visual check
was closed and the previously expanded Arc folder restored. A final horizontal
scroll restriction keeps cached wide folder rows from widening the split group.
Parallel preferences work was retained; its temporary compilation mismatches
were resolved while rebuilding the shared checkout.

The final pixel-enabled SplitViewLifecycleAndIsolation run passed after the
horizontal restriction. Repackaged build/Zitter.app and restarted the production
window through CUA with the numbered buttons absent. No test split/tab was left
in the user's Prywatne workspace.

## Time Machine, configurable shortcuts and keyword search — 2026-10-16

Time Machine captures per-workspace URL layouts, active tab, pinned items/folders,
split participants/ratio and profile identity. Named and automatic snapshots have
separate 30-item bounds; closed tabs have a 100-item bound. Recovery points are
created before restore, close, split edits and sleep, plus periodic session-save
points. Restoring validates URLs/profile identity before replacing live pages;
it retains Chromium profile isolation and never rolls cookies/passwords/history
back. A scrollable native panel supports naming/filtering/restoring/deleting
snapshots, searching/reopening closed tabs and sleeping the active workspace.

Sleeping hosts retain a serialized layout and release every WebView/WebContents.
Selecting their workspace recreates pages in its existing profile. Both sleep
state and timeline metadata persist across app restart on the same sequenced
atomic-write runner used by session/catalog storage. Form values and per-page
navigation stacks are deliberately not serialized and this is explained in UI.

BrowserTools owns editable HTTP(S) search templates and keyboard bindings. Search
prefixes choose an explicit provider before URL/default-search resolution and
remain the first palette result. Query substitution encodes text in either a
query or path and rejects origin/credential substitution. Built-ins include
Google, Polish Wikipedia, YouTube, GitHub, DuckDuckGo and Bing; custom providers
can be default, renamed or removed, retaining at least one. The preferences
default picker includes the custom providers too.

The shortcut scheme is based on the official
[Arc keyboard-shortcut reference](https://resources.arc.net/hc/en-us/articles/20595231349911-Keyboard-Shortcuts).
Bindings can be recorded, edited, disabled and reset, with duplicate/reserved-key
validation. All native panels and the main window share the updated registration;
macOS menu equivalents and the sidebar new-tab hint are updated on remapping.
The recorder temporarily registers valid unassigned chords only on its own
panel so AppKit can deliver command chords that previously had no action. Page
shortcut actions are deferred until after the renderer keyboard callback returns,
because close/restore operations can destroy the source WebContents. Consumed
keydown/character/keyup events are tracked across workspace focus changes.

The two BrowserTools sections use dirty guards and field-patch writes so startup
loading does not roll back user edits. Both JSON files are local to Zitter's data
directory. Existing Arc import, profile/session and parallel preferences work
remain integrated in the shared checkout.

Focused checks: keyword search/provider restart persistence passed, as did the
snapshot → close/reopen → restore → sleep → restart → wake scenario. The Arc
fixture test additionally snapshots/restores its imported folder tree and pinned
split participants. The native shortcut test validates conflicts, remapping a
space action, disabling an action, recording an unassigned Cmd+Shift+U chord,
saving it and retaining the configuration after recreating the window.

Desktop CUA inspection confirmed readable dark Time Machine, provider and
shortcut panels at the current window size. Cmd+Shift+Y opened Time Machine;
saved a named “Punkt startowy” in Prywatne. Inspected the Wikipedia `w` prefix and
template without changing the default search engine. Recorded Cmd+Shift+U in the
editor without saving it, leaving the user's default bindings intact. Real
workspace sleep/restore was tested only in isolated browser-test profiles so the
user's live forms were not discarded. CUA exposed an upstream Combobox cached AX
value remaining stale after a model swap at index zero; clearing the selection
before replacing the model fixes the accessible snapshot label and has a
regression assertion. Enter now submits tool editors through a deferred action;
Enter in a filter cannot accidentally activate a stale address-bar suggestion.

The full serial, pixel-enabled browser regression passed **27/27** in 406 seconds,
including live read-only Arc import (four cached pin trees retained; maximum
measured workspace switch 30.355 ms). Both GN dependency checks, eight Python
tests, the core workspace contract and product-boundary checks passed. A final
key-event cleanup avoids rearming consumed key state in the deferred action and
clears stale state on a new physical keydown, including when the earlier keyup
went to a palette field. The renderer shortcut test now checks that an ordinary
digit press after a workspace chord still delivers its keyup to the page.

After that key-state cleanup, the four focused native/renderer keyboard and Peek
regressions passed again (56 seconds). The production bundle was repackaged and
its executable confirmed to be the current build. `git diff --check` is clean.
The desktop locked before the final restart; the earlier manual panel checks
used this feature implementation before the final AX/status/key-state polish.
The packaged final build is ready; the running production process still needs
to be restarted after manual unlock.

## Preferences centre (2026-10-16)

- Native seven-category preferences, global search, responsive category picker,
  light/dark surfaces and profile-backed controls build in `zitter` and
  `zitter_prototype`; `gn check //zitter:zitter` passes.
- `ProfilePreferencesAndSearch` passes: real toggle dispatch, case/whitespace
  search, empty results, Enter isolation, invalid-setting rejection, JavaScript
  blocking in a loaded document, live sound muting, zoom/font values, profile
  isolation and preferences retained across window recreation.
- `GeneralSettingsThemeAndPersistence` and `PeekLifecycleAndPromotion` pass
  with the new per-page preference/content-setting helpers attached.
- Core CTest, eight Python overlay/package tests, dependency boundaries and
  `git diff --check` pass. The development app was repackaged.
- The initial desktop layout was visually inspected. The final native download
  folder sheet was not manually verified because the UI automation service
  timed out on the isolated production preview.

## Chromium preference expansion (2026-10-16)

Added native language/spelling, browsing-data and advanced sections; site-rule
editing; font families/fixed-width size; caret browsing; app-wide DoH and GPU
preferences; and Chromium's system proxy/certificate entry points.

Verification completed:

- Production and browser-test targets compile; GN header checks pass.
- `AdvancedChromiumPreferences`: canonicalized/deduplicated language tags,
  renderer `navigator.language`, text-edit preservation, profile separation,
  font/spelling preferences, invalid input rejection, strict DoH validation,
  and managed DNS settings. DNS tests use an isolated preference store.
- `SitePreferenceExceptionsAndIsolation`: JavaScript blocked by default yet
  enabled for an explicit origin; resetting/removing rules; invalid origins
  and policies rejected; profile separation; live sound exceptions.
- `ClearBrowsingDataConfirmationAndScope`: cancel preserves all markers;
  confirmation binds the named profile across a workspace switch; cookies,
  localStorage and IndexedDB clear only in that profile. Retested successfully
  after fixing native dialog lifecycle tracking.
- Existing `ProfilePreferencesAndSearch` and
  `GeneralSettingsThemeAndPersistence` pass with the expanded controls.
- Core CTest, eight Python tests, dependency boundaries and whitespace checks
  pass. `build/Zitter.app` was repackaged.

The final incremental build reports no pending work. A bundled manual preview
was attempted after the automated checks; its browser-test startup did not reach
a controllable app window, so no final visual screenshot was recorded.

## Command Center and feature audit — 2026-10-17

Cmd+K now combines synchronous contextual commands with the existing debounced
history/tab/pin search. `>` enters command-only mode without producing a web
search fallback or querying history. Command filtering tokenizes Polish/English
keywords and folds Polish diacritics. Cmd+L/Cmd+T remain navigation palettes;
explicit search-provider prefixes keep their search result first in Cmd+K.
The native menu and configurable shortcut name use Command Center too.

Commands cover quick snapshots, Time Machine, settings, profiles/spaces, import,
shortcut/provider editors, Split/pane focus, navigation, closed-tab recovery,
sleeping background workspaces and waking/selecting them. Same-profile moves
transfer the existing WebView between host lists, preserve forms/history and
maintain the source's nonempty-page invariant. The focused split pane is replaced
without destroying its partner. Pin catalogue entries remain in the source;
the live moved page becomes a regular destination tab. Sleeping destinations are
woken first. Cross-profile copies open only the URL in a fresh page with the
explicit destination's profile; they never migrate the source BrowserContext.

Command execution is posted after the palette row callback unwinds, closes the
palette and revalidates current availability, source workspace, selected index,
URL and Chromium SessionTabHelper page identity. A removed page cannot be
substituted by another page at the same index/URL. This prevents queued commands
from acting on a subsequently selected workspace or replacement tab.

`docs/feature-audit.md` records local-code findings and priorities: file uploads,
media/device consent, JavaScript dialogs/beforeunload, downloads UI, Find/print,
release/update infrastructure, security-path verification and extensions. It
explicitly distinguishes missing shell integration from Chromium engine support
and unverified behavior. No user browsing data was changed for this audit.

Final verification: production and browser-test builds passed; all three GN
header checks passed. The focused serial, pixel-enabled suite passed **8/8 in
109 seconds**: CommandCenterSearchAndDispatch, CommandCenterMoveCopyAndSleep,
CommandCenterMoveSplitAndLastPage, ChromiumHistoryAndCommandPalette,
TimeMachineSnapshotsClosedTabsAndSleep, ConfigurableNativeKeyboardShortcuts,
SplitViewLifecycleAndIsolation and SplitViewSessionRestore. This includes a
same-index/same-URL replacement-page rejection and moving a final source page.
Eight Python tests, the CMake workspace contract, boundary checks and
`git diff --check` passed. `build/Zitter.app` was repackaged and its executable
confirmed to match the current production build. The Mac was locked during the
CUA availability check; final manual Command Center visual verification and
restarting the older running app require manual unlock. No UI lock bypass was
attempted, and live user workspaces were not used for move/sleep tests.

## Webpage uploads, dialogs and capture — 2026-10-17

Native file/multiple-file/folder upload now uses Chromium's selection pipeline,
with document-bound cancellation and profile-local remembered directories.
JavaScript alert/confirm/prompt and beforeunload sheets identify their origin and
profile. Destructive batches obtain consent from every affected page, recheck
document identity and newly installed handlers, and commit together. This includes
routed Peek pages, profile reassignment, snapshot restore, sleep and app exit.

Camera/microphone consent runs through Chromium's permission and device pipeline.
Display capture registers a Zitter-only handler in the upstream dispatcher, so
capture security/output-protection accounting is retained. The guarded installer
patch adds only that registration hook. Screen/window sources use the native
Chromium picker; Zitter tab sources are limited to the same BrowserContext and
currently have titles without thumbnails. Persistent origin/profile controls can
stop capture from another workspace. Capturing and captured pages cannot sleep.
The app bundle declares camera/microphone purpose strings.

The five upload tests passed, including actual selected-file reads, relative
folder paths, native-sheet occlusion, cancellation and stale-result rejection.
All six dialog/lifecycle tests passed, covering native dialogs, atomic
restore, handlers added while consent is pending, profile/sleep/window close,
Peek, routed cross-profile Peek and background batches. Controlled capture tests passed for navigation
teardown, cross-origin attribution, picker cancellation, same-profile tab
filtering, pending-picker document replacement and explicit macOS denial.
Media tests use synthetic devices and mock screen-picker cancellation; no real
camera, microphone or desktop source was recorded.

The complete serial pixel-enabled regression, excluding the separately diagnosed
combined-device startup case, passed **47/47 in 618 seconds**. It includes real
read-only Arc import into isolated test profiles, existing password/history,
settings, Command Center, workspace/profile switching, Split/Peek and all new
upload/lifecycle cases.

The combined camera/microphone test subsequently passed actual JavaScript stream
startup, profile-specific denial/consent, capture controls across workspace
switching, sleep rejection, browser-initiated Stop with both tracks ended, and
one-time grant revocation after leaving the origin. The cross-origin camera test
also waits for actual JavaScript stream startup before checking attribution.
The fixture uses CPU video buffers and an embedded FakeAudioManager/service:
Chromium's usual fake-device switch still queried native CoreAudio parameters,
which blocked on this machine. This test isolation preserves the real Zitter
permission UI and production device pipeline. It exposed and fixed an invalid
all-screens flag in Stop; standard user/display capture now uses only the flags
supported by MediaStreamCaptureIndicator.

Final media rerun: **7/7 passed in 109 seconds** on build12, with the strengthened
startup/Stop assertions. Together with the 47-case regression this covers all
**48 distinct automated browser tests** (six media cases overlap). The final
production and browser-test builds and GN header checks pass; the bundle was
repackaged after the Stop fix and its executable matches that production build.

### Unlocked-machine follow-up

The packaged app was launched through CUA after the user unlocked the Mac. Native
toolbar, palette and sidebar responded, but page navigation remained pending,
including an isolated localhost fixture. A browser process sample located the
block in `os_crypt_async::GetKeyTask` → `KeychainPassword::GetPassword` →
`SecItemCopyMatching`/SecurityServer decrypt. Cookie encryption waits for that
key, explaining stalled page requests; browser tests use a mock keychain.
Temporary navigation diagnostics confirmed that no navigation throttle deferred
the localhost request and were then removed. No keychain entries, trust settings
or encrypted data were changed. CUA refuses access to SecurityAgent, so the user
must handle any native request for **Zitter Safe Storage** locally. Visual dialog
verification is still pending that system response.

After the user approved Keychain access locally, the final packaged build loaded
both localhost and an existing external tab successfully. Manual CUA checks in
the dark theme passed alert/OK, confirm/Cancel (JavaScript returned `false`),
prompt/OK (the page displayed the entered test value), native file-picker/Cancel,
camera permission origin/profile attribution and Block (`NotAllowedError`), and
beforeunload/Stay (the original URL and protected form state remained). No camera
permission was granted and no user file was selected. Local test tabs were closed;
the clean production app remains running in its normal view.

Production/browser-test builds and all three GN header checks pass. Nine Python
tests, the CMake workspace contract, product boundary checks and whitespace checks
pass. The development bundle was repackaged and its executable verified against
the production build. Final native visual verification/restart remains pending
because the Mac was locked during CUA access.

## Download panel and password-window placement — 2026-10-17

A native, movable/resizable download window is available from the toolbar, File
menu, Command Center and configurable Cmd+J. It observes the selected profile's
DownloadManager/DownloadItems, explicitly initializes Chromium's deferred download
history, and shows all active downloads plus the latest 200 historical entries.
Progress updates retain existing row controls; item callbacks coalesce off the
observer/button stack. Actions resolve a GUID against the currently bound manager;
profile changes invalidate old callbacks and detach item/manager observers.
Closing the window leaves transfers running. Chromium owns transfer persistence,
target selection, interruption/resume capability and file safety. Dangerous or
insecure items cannot be resumed or revealed through this panel. The UI exposes
no protection-bypass or implicit file-open action.

The key icon opens saved-login metadata for the focused page's immutable profile.
The overview now uses a draggable native frame and display-work-area bounds;
password save/update sheets have the actual native parent rather than only a
creation context. Password values remain absent from the account overview.

Verification: production and browser-test targets compile; all three GN header
checks pass. The focused serial pixel-enabled suite passed **7/7 in 131 seconds**:
DownloadsTransferControlsAndProfileIsolation (actual streamed file, UI pause/resume,
profile switch, byte-for-byte completion, intercepted Finder dispatch, UI cancel
and panel reopening), DownloadsDangerousHistoryCannotRevealOrResume,
PasswordOverviewVisibleAndDraggable (window moved to a display edge),
ChromiumPasswordSaveAndFill, GeneralSettingsThemeAndPersistence,
CommandCenterSearchAndDispatch and ConfigurableNativeKeyboardShortcuts. Shortcut
coverage also checks migration when an older custom binding already uses Cmd+J.
Nine Python tests, the CMake workspace contract, dependency boundaries and
whitespace checks passed. The packaged executable matches the production build.

CUA launched the packaged app and checked the visible dark native downloads
window, including its title bar and drag interaction. The saved-password overview
and a final real-profile download preview were blocked by a renewed native
Keychain consent after rebuilding: the sampled production worker again waits in
GetKeyTask/KeychainPassword/SecItemCopyMatching/SecurityServer decrypt. The user
was asked to handle that system prompt locally; no Keychain settings were changed.

## Workspace settings, context actions and Arc emoji — 2026-10-21

Workspace dots now expose checked color and emoji presets, a plain-dot reset,
custom appearance fields, settings and deletion. Presets work on inactive spaces
without selecting them. Settings use grouped appearance/management sections,
scroll within the window, and distinguish saving a name/profile from automatic
appearance saves. Creating a workspace has a separate Create/Cancel mode.
Deletion opens a named native confirmation with Cancel as the default and binds
the original workspace ID, including across before-unload continuation. Deleting
an inactive workspace preserves the active workspace; the last cannot be deleted.

Arc import reads `customInfo.iconType.emoji_v2` sequences and legacy numeric
`emoji` scalars. Appearance is applied after the catalogue commit succeeds and
only for newly imported spaces, preserving local changes on repeat imports.

Built and linked `zitter` and `zitter_prototype` in `out/Zitter`; header dependency
check and the standalone CTest contract passed. Browser tests passed:
`DynamicWorkspacesAndProfiles`, `WorkspaceContextAppearanceAndDeletion`,
`WorkspaceDotProfileMenu`, and `ArcImportStructureAndPersistence`. Coverage
includes cancel/accept, default Cancel, target identity after selection changes,
inactive color/icon changes, final-space protection, composite emoji persistence
and repeat-import preservation. The packaged `build/ZitterDebug.app` was also
inspected with isolated temporary browsing data for settings and creation mode.

Additional browser regressions passed: `NativeShellAndAppearance` (native
keyboard input and appearance persistence) and
`FailedConfigurationWritePreservesProfiles` (including failed Arc import
rollback). All six targeted browser tests passed.

## Stable macOS signing — 2026-10-21

The normal packager now requires the locally pinned signing identity (or an
explicit identity override); ad hoc packaging requires `--unsigned`. On this
machine, both `build/Zitter.app` (optimized) and `build/ZitterDebug.app`
(component) were packaged and signed with the existing Developer ID Application
certificate for team `7L5KF39DAR`. Nested Mach-O libraries are signed before
helper/framework/application bundles, followed by strict deep verification.
The installed app is replaced only after that verification succeeds.

Both packages passed verification and have identical designated requirements:
stable `dev.zitter.browser` identifier, Apple Developer ID certificate chain and
team identity, with no per-build cdhash requirement. Each package also satisfies
the other's requirement despite different browser executables. Original compiler
outputs retain their ad hoc signatures; private clones and package-internal
hardlinks prevent signing from mutating build output. Legacy root development
markers migrate to sealed Resources, with benchmark compatibility retained.

All 37 Python tool tests passed, including signed-copy isolation, nested signing
order/strict verification, pinned identity without ad hoc fallback, and retention
of the installed app/configuration after signing failure. Keychain item contents
and access controls were not changed. An initial user approval can still be
needed when moving from the previous ad hoc identity to Developer ID; testing
that approval across real launches requires the user to answer macOS's prompt.

## Browser import choices and first-run workspaces — 2026-10-21

A fresh catalogue now has one personal profile and one `Moja przestrzeń` starter.
Skipping import persists that single workspace. A successful first-run import
replaces the untouched starter workspace/profile; the committed catalogue
already excludes them. Failed import commits leave the starter and its live
page intact for retry. Existing/customized workspaces are preserved. Existing
integration fixtures explicitly seed their legacy two-workspace installation.

The native import panel supports Arc, Chrome, Edge and Brave. Checkboxes select
separate destination profiles, bookmarks/pins/folders and Arc workspace emoji.
When separate profiles are disabled, spaces from a browser share one new
profile. Browser/options changes invalidate the preview before Apply is allowed.
Chrome/Edge/Brave read regular local profile names and nested Bookmarks trees;
source data is not modified. Unsupported URL schemes are skipped, malformed
bookmark files abort the preview, and profiles-only imports do not read them.
The panel scrolls vertically with its contents constrained to the viewport width.

Workspace deletion confirmations now receive the actual native parent view,
allowing macOS to attach and position the window-modal sheet correctly rather
than displaying an unparented dialog near the screen origin. The regression
checks settings and context-menu entry points at all four display corners.

Eight targeted browser scenarios passed: `WorkspaceContextAppearanceAndDeletion`,
`WorkspaceDeletionConfirmationStaysOnScreen`, `ArcImportStructureAndPersistence`,
`FailedConfigurationWritePreservesProfiles`,
`ChromiumBrowserImportSelectionAndPersistence`, `ImportOptionsInvalidatePreview`,
`FirstRunImportReplacesStarterAndRollsBackOnFailure`, and `FirstRunImportOffer`.
The toolchain header dependency check also passed. The final signed
`build/ZitterDebug.app` passed strict deep signature verification and was
restarted with the existing browsing data. Visual inspection confirmed wrapped
import descriptions, controls within the panel width, and a fully visible native
confirmation sheet; the deletion was cancelled. The new importer was left open.

## Automatic import preparation — 2026-10-21

The importer now always retains Arc workspace emoji. Its emoji checkbox and the
manual `Sprawdź dane` action were removed, along with the explanatory footer
about excluded data and profile sharing. Opening the panel or changing a browser
or data checkbox schedules preparation after a short debounce. Only the latest
scan can publish results or enable Import; older replies leave current loading
state untouched. Source/options remain editable while reading. Applying an
import locks those controls and Skip; reopening the panel cannot invalidate the
in-flight commit. Closing the panel cancels pending debounce work.

The regression fixture redirects source-browser paths to temporary Arc/Chrome
data and exercises automatic readiness, option changes without a manual step,
retained composite emoji, rapid source changes, stale in-flight replies, direct
Import, and reopening while applying. History feasibility was checked against
Chromium's `ProfileWriter::AddHistoryPage` / `HistoryService::AddPagesWithDetails`.
Cookie storage decrypts `encrypted_value` through its crypto delegate; the macOS
OSCrypt key is obtained from that browser's Keychain service. History and cookie
import implementations are not part of this UI change.

The final build passed `ArcImportStructureAndPersistence`,
`ImportOptionsInvalidatePreview`, `FirstRunImportReplacesStarterAndRollsBackOnFailure`
and `FirstRunImportOffer`, plus the header dependency check. The signed Debug
package passed strict deep verification and was restarted. Visual inspection
confirmed two data checkboxes, no emoji checkbox, no manual preparation button,
no excluded-data footer, and an automatically enabled Import action after the
source read. The panel was left open without applying another import.

## History, cookies and importer close control — 2026-10-21

History and cookies are separate opt-in selections for Arc, Chrome, Edge and
Brave. Preparation retains each source-profile directory and destination ID even
when profiles are merged. Preview only reads sidebar/bookmark metadata; selected
browsing databases and the source Keychain entry are read after the explicit
Import action. The header × replaces the Pomiń / Zamknij button, keeps its
first-run dismissal behavior, and is disabled while applying.

Database reads use temporary SQLite snapshots, copying DB/WAL and checking source
metadata around the copy. The encrypted-cookie regression reads an update that
exists only in a live WAL, preserves its partition key, decrypts a v24 `v10`
cookie with the expected domain hash, rejects denied key access and a mismatched
domain hash, and checks that the original cookie database remains byte-identical.
macOS key lookup uses FindGenericPassword rather than the key-creating OSCrypt
helper. It derives the compatibility key using the Chromium macOS parameters
(PBKDF2 SHA-1 / saltysalt / 1003 / AES-128-CBC) and never changes source keys.

Destination writes start after a successful catalogue save. History uses
AddPagesWithDetails with imported visit provenance and Chromium's 90-day
retention; existing pages are preserved and merged-source URL duplicates are
collapsed. This is page history with last-visit dates rather than a copy of every
individual visit. CookieManager applies destination validation/encryption,
retains cookie attributes and partitions, preserves existing cookies, and flushes
before reporting completion. Invalid/expired/rejected records are counted as
skipped; a destination-service failure is reported without rolling back committed
workspaces.

The destination regression imports a workspace first, later adds history and
cookies to its existing profile, checks history search and profile isolation,
changes a destination cookie, and imports again to ensure the local value wins.
It then recreates the window, waits for asynchronous profile realization and
checks the persistent cookie value. The local replacement fixture explicitly
uses Max-Age: session cookies correctly follow Chromium's normal lifetime.

Seven targeted browser scenarios passed on the final implementation:
`ArcImportStructureAndPersistence`, `BrowsingImportEncryptedCookiesAndSelection`,
`BrowsingImportPersistsInDestinationServices`,
`ChromiumBrowserImportSelectionAndPersistence`, `ImportOptionsInvalidatePreview`,
`FirstRunImportReplacesStarterAndRollsBackOnFailure`, and `FirstRunImportOffer`.
The header dependency check and diff whitespace check passed. The Debug package
passed strict deep signature verification with the pinned Team ID `7L5KF39DAR`.
It was restarted and inspected visually: the × is at the top right, the old
bottom dismissal button is absent, and both new unchecked data selections and
Import are fully visible. The existing user catalogue was retained and no live
source-history/cookie import was applied; real Keychain approval is left to the
user's explicit import.

## Profile history browser and clearing — 2026-10-21

Added History in the sidebar, Privacy preferences and command palette, with the
configurable Cmd+Y / Ctrl+Y shortcut. The shared native tools panel shows the
active profile name, a debounced search field, title/URL/local visit-time rows,
and a 200-visit initial page with Show More. A row opens in a new tab under the
same profile. The query tracker is separate from command-palette history;
generation, profile, query and result-limit checks reject stale responses.
Search controls remain mounted while the result list updates.

The history panel offers hour/day/week/all-time clearing through the existing
BrowsingDataRemover. Its confirmation captures the profile and selection, keeps
Cancel as default, and now receives the visible parent widget's actual native
view, including when opened from History. Completion refreshes the visible list;
cookie/cache/password data is not selected by the History clear action. The
broader preferences data-clear form remains available.

Regression coverage seeds both profiles with distinct history, checks ordering,
search and empty results, opens a result as a new tab, cancels the clear prompt,
checks its screen bounds, then clears the recent hour and all time while checking
profile isolation. A separate case seeds 205 visits, verifies 200/205 pagination
and rejects a stale search reply. Existing configurable shortcuts, command
center, history search and browsing-data scope scenarios are also exercised.

All six targeted scenarios passed: `BrowsingHistoryPanelSearchOpenAndClear`,
`BrowsingHistoryPaginationAndStaleSearch`, `ClearBrowsingDataConfirmationAndScope`,
`ConfigurableNativeKeyboardShortcuts`, `CommandCenterSearchAndDispatch`, and
`ChromiumHistoryAndCommandPalette`. Header dependency and diff whitespace checks
passed. The signed Debug app passed strict deep signature verification, was
restarted, and opened History with Cmd+Y. Visual inspection confirmed the search,
profile label, interval picker, clear action and empty state. Its confirmation
sheet was opened, inspected and cancelled; no live user data was cleared. History
was left open for the user.

## Sidebar utility menu and folder creation — 2026-10-21

Removed the Time Machine, History, Downloads and Settings utility rows above the
sidebar's tabs. Their native macOS commands are grouped in the Zitter app menu;
Downloads is no longer duplicated under File. History and Time Machine dispatch
to Zitter's existing panels, and all four menu shortcuts follow the configured
bindings.

The sidebar/scroll background now has a context-menu controller. Ordinary tab
and pin menus offer New Folder; folder rows are also registered in the pin-view
cache and offer New Subfolder. The modal name prompt captures the workspace and
parent folder, trims the supplied name and preserves Unicode. A bounded ancestor
check rejects invalid/deep parents. Adding a child expands its parent. Folders
are saved through the workspace catalogue; failed writes roll back the new item
and show a retry message. Cancel leaves the catalogue untouched.

The folder regression checks the simplified sidebar, context controllers, cancel,
a named Unicode folder, a nested folder, validation, profile/workspace isolation
and restoration after reopening. A second case forces atomic-write failure and
checks rollback plus the error dialog. Existing native-shortcut, workspace-menu
and Arc-import scenarios passed as well; the two folder scenarios were rerun
following the final workspace-target capture adjustment.

Live inspection confirmed the four native Zitter menu entries and History's
menu dispatch. Right-clicking an existing folder opened New Subfolder and its
name sheet; the sheet was cancelled. Inspection also found that blank section
headings and scroll/layout descendants require their own context controllers,
so background controllers are installed once throughout the sidebar tree while
button label subtrees retain their normal event handling. The regression now
checks those descendants and the metadata-row stability case remains passing.

The final signed Debug package was restarted. A right-click in blank sidebar
heading space opened New Folder, and selecting it opened the native name sheet.
The sheet was cancelled. Root-folder and subfolder prompts were both inspected
without adding test data to the user's catalogue. Strict deep signature and
header dependency verification passed; final sidebar folder/rollback and stable
row regressions passed. The simplified sidebar was left visible.

## Folder editing and workspace switching — 2026-10-21

Folder context menus now include rename and delete. Rename retains stable IDs and
nested relationships. Delete confirms the named folder, defaults to Cancel and
removes all pinned descendants. Opened pins remain alive as ordinary tabs.
Catalogue writes are transactional: rename/delete restore the previous pin tree
on failure and show the existing save-error sheet. Tests cover cancel, Unicode
rename, persistence, recursive removal, write failures for all three folder
operations and preserving opened imported pins after their folder is removed.

Workspace switches skip global theme refresh and hidden-panel measurement.
Inactive pinned lists are built one workspace per delayed UI task after startup
(or import), without creating WebContents. Cached lists are reused. Content/list
transitions take 120 ms with a smaller page offset. In a native Debug fixture with
300 pins, synchronous first Select measured 250.152 ms before and 34.005 ms after
allowing sidebar preparation; twenty warm switches averaged 21.9684 ms before
and 17.036 ms after. These measurements exclude animation completion/paint and
are not a guarantee for cold profile loading or clicks before preparation.

Horizontal gestures over the sidebar select one adjacent workspace per gesture,
without wrapping. Axis locking preserves vertical scrolling; momentum cannot
select further spaces. Modified scrolling is ignored except Shift+wheel's
horizontal mapping. Native ScrollEvent-to-wheel fallback is deduplicated by
timestamp. Page content is outside this handler. Gesture tests cover direction,
axis lock, one switch per gesture, inertia/fallback, boundaries and hidden panel.
The final gesture regression also dispatches a ScrollEvent through the sidebar
Widget and verifies that its registered pre-target handler selects the space;
this final native-dispatch rerun passed. Automated live horizontal scrolling did
not change the selected space, so physical trackpad behavior remains a manual
check; synthetic native widget dispatch is covered by the regression.

Native regression passes: SidebarMetadataUpdatesPreserveRows,
StartupPrioritizesActiveProfileWithoutReorderingCatalogue,
SidebarRemainsOpenWhileSpacesSlide, WorkspaceSwitchCostWithImportedSidebar,
SidebarFoldersCreateNestAndPersist, SidebarFolderRenameAndDeleteContents,
SidebarSwipeSwitchesOnceAndIgnoresInertia, SidebarFolderFailedWriteRollsBack and
FolderRemovalKeepsOpenedPinsAsTabs. The hidden-sidebar case explicitly unpins
the test panel, because CloseOverlays intentionally leaves pinned sidebars open.
Header dependency and whitespace checks passed. The Debug app was packaged,
signed with the pinned development identity, strictly verified and restarted.
Live folder-menu, prefilled rename sheet and named deletion sheet were inspected
and cancelled without changing the user's folder catalogue.

## Workspace switching hot-path correction — 2026-10-21

The earlier Select benchmark omitted subsequent animation frames. Inspection
found that every shell update recursively registered context menus throughout
all cached sidebar trees, and checked every pin against every displayed tab.
Context registration now runs only for new/changed trees; pin visibility is
updated only when pin/tab/split structure changes, using a set of paired pins.
Only the previous and next cache roots change visibility. Catalogue pruning and
full sidebar context installation run on catalogue changes. Hidden workspace
settings are refreshed when opened. Building ShellState indexes pin views once
instead of searching the pin map for each tab.

Removed the PaneHost SlideAnimation which invalidated layout and moved native
WebViews every animation frame. Pages now immediately occupy their final bounds.
The 120 ms animation moves just the clipped list ScrollView in the compositor
by 32 px; no layer spans the complete imported pin tree. Reduced motion,
disabling animations, direction and rapid switching remain supported.

The imported-sidebar benchmark now asserts that warm switching performs zero
context-controller visits and zero pin-visibility updates, in addition to zero
pin-tree builds. A second fixture puts 300 pins in each of two workspaces. Debug
measurements were 34.174 ms first / 16.6125 ms warm for the original fixture and
75.917 ms first / 30.4928 ms warm with both large lists. The original warm result
was previously 17.036 ms, so synchronous Select improvement alone is small;
these measurements do not measure frame presentation. The structural correction
removes native per-frame layout work rather than proving a particular frame
rate. The animation regression verifies viewport-bounded layer size, no layers
on cached full list roots, repeated switching, final page bounds, disabled
animations and reduced motion.

All eight native cases passed: both switching benchmarks,
SidebarRemainsOpenWhileSpacesSlide, SidebarMetadataUpdatesPreserveRows,
SidebarFoldersCreateNestAndPersist, SidebarFolderRenameAndDeleteContents,
SidebarSwipeSwitchesOnceAndIgnoresInertia and ArcImportStructureAndPersistence.
App/test header checks and diff whitespace check passed. The new Debug package
passed strict signature verification and was restarted. Native UI clicks switched
from cc-member to Work and back, with the sidebar remaining visible. The user
catalogue was preserved.

## Smooth sidebar animation and repeated Cocoa swipes — 2026-10-21

Chromium's Cocoa GetScrollOffsets maps NSEvent phase Began/MayBegin to
EventMomentumPhase::MAY_BEGIN, while ScrollEvent leaves scroll_event_phase at
kNone. The workspace handler previously classified MAY_BEGIN as inertia, so a
second gesture could retain the first gesture's switched latch. MAY_BEGIN now
resets the gesture and processes any initial delta. END without delta ends the
gesture, while phase-less wheel END events with delta remain usable. Actual
inertia never switches spaces. The new native Widget-dispatch regression uses
only Cocoa momentum phases at a fixed point, with no mouse movements or idle
gaps, and checks six consecutive changes, a new touch interrupting inertia and
phase-less wheel deltas. It passed alongside the existing axis/boundary/inertia
regression.

Sidebar animation now prepares layout and the offset viewport, then waits for
successful presentation before starting its 180 ms compositor transition. This
keeps initial layout/raster time outside the animation clock. Render-surface
caching is requested for the animation's lifetime. Pages still switch directly
to final bounds. A generation invalidates callbacks from superseded transitions;
hiding the sidebar, disabling animations and detaching cancel both pending and
running transitions. Hidden sidebars do not enqueue presentation callbacks.
The animation regression verifies pending-before-presentation then running,
viewport-only layering, rapid switching, completion and reduced motion, and
passed. Header checks for app/tests and whitespace verification passed.

Five native regressions passed in total, including metadata row stability and
the two-large-sidebars hot-path checks. The Debug package passed strict deep
signature verification with the pinned identity, was restarted and switched
Work → cc-member → Work through native UI clicks. The sidebar was left visible.
Physical trackpad frame cadence was not measured by the UI automation.

## Interactive workspace scrubbing — 2026-10-21

Horizontal sidebar gestures now drive continuous position directly from finger
distance / viewport width. No selection happens while fingers remain down.
Reversing can return to the source; a longer gesture can cross several spaces.
Finger-up selects the nearest space and settles the preview for 160 ms. A small
rubber-band offset resists both ends without wrapping. Cocoa MAY_BEGIN/END,
interrupted settling, omitted END followed by inertia, and phase-less wheel idle
completion are supported. Inertia cannot commit another workspace. Escape or
hiding the panel cancels; an external selection/catalogue change cancels stale
preview state. Click-driven transitions retain their presentation-gated animation.

Preview pages are compositor-only decorative layers with viewport-sized paint
records. Visible rows use the same row decoration painter as native sidebar
buttons, including folders, indentation, monograms, active pins and split tiles.
No full native tree layout/visibility changes or profile/page activation are
needed to create a preview. At most three surfaces are alive; up to eight paint
records are kept for reversing direction within the gesture. Finger updates
move layers without View accessibility location notifications. Headings/dots
change only when the nearest candidate changes. The committed catalogue,
WebContents and profile remain unchanged until release. A committed swipe does
not trigger a second click-style workspace animation.

Native gesture tests cover reversing/cancelling, crossing three spaces in one
motion, fixed-cursor repetition, boundaries, vertical axis locking, inertia,
hiding mid-drag, phase-less wheels and no page creation for an intermediate
preview. The 300-pins-per-space case also measures preparation and twenty
tracking events, checks bounded previews and no native pin-tree reconstruction.

The six distinct regressions passed; all three gesture/performance cases were
rerun after the compositor-only layer implementation. The final 300-pins-per-
space measurement was 48.789 ms for initial preparation through native Widget
dispatch and 0.01185 ms mean for twenty direct gesture-handler updates. A prior
measurement including full Widget dispatch averaged 17.3455 ms per update;
handler timing is not end-to-end presentation latency or a frame-rate claim.
Header dependency and whitespace checks passed. The final Debug package passed
strict deep signature verification with the pinned identity and was restarted.
Native automation does not synthesize held trackpad phases; reversal and release
are verified by native Widget-dispatched regression events at a fixed cursor.
