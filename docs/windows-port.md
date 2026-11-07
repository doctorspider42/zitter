# Windows port

Plan and status of the Windows (x64) version of Zitter. About 95% of the code is
shared with macOS. The new work, roughly 2–3k lines, is in the text layer, the
Windows entry point, the native frame and packaging.

## Decisions

- **x64 only** for now. ARM64 comes later.
- **Windows 10 is the minimum.** Mica and Snap Layouts are used only on Windows 11.
- **Workspace switching is Alt+1/2/3**, because Ctrl+Alt is AltGr on the Polish
  layout. Ctrl+Alt+←/→ goes to the previous/next workspace (⌥⌘←/→ on macOS) and
  wraps around after the last one.
- **No cookie import from Chrome.** App-Bound Encryption blocks it, so the import
  covers bookmarks, history, open tabs and passwords from a Chrome-exported CSV.
  Users sign in to sites again.
- **The design comes first.** Every screen that differs from macOS is in the
  [Windows design canvas](https://claude.ai/artifact/V1fAXXfmYYMv3QMbAURWjJ):
  - the title bar with caption buttons on the right and the |z⟩ Zitter menu;
  - Command Center shortcuts and the macOS → Windows copy table;
  - tool windows and the taskbar jump list;
  - the installer and the first-run wizard.

## Phase 0: environment

The checkout lives in the ignored `.chromium/` directory, the same place
`build.sh` uses on macOS, at the revision in
[chromium/revision.json](../chromium/revision.json).

Prerequisites:

- Visual Studio 2026 (18.x; Build Tools are enough) with **Desktop development
  with C++** and **C++ ATL and MFC**. VS 2022 is too old for this revision.
- Windows 11 SDK **10.0.28000.2270**, plus SDK Debugging Tools 10.0.26100.3323 or
  newer.
- About 150 GB free for the checkout and one build directory.

Steps:

```powershell
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
# Only when VS 2026 sits under Program Files (x86), where Chromium doesn't look.
$env:vs2026_install = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
$env:PATH = "F:\Projects\zitter\.chromium\depot_tools;$env:PATH"
# Chromium needs LF checkouts; override autocrlf only for this shell.
$env:GIT_CONFIG_COUNT = '1'; $env:GIT_CONFIG_KEY_0 = 'core.autocrlf'; $env:GIT_CONFIG_VALUE_0 = 'false'
cd F:\Projects\zitter\.chromium
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
# .gclient: solution "src" at https://chromium.googlesource.com/chromium/src.git, target_os = ["win"]
gclient sync --no-history --nohooks -D --revision src@<revision from revision.json>
gclient runhooks
```

Then:

1. Build and run a plain `chrome.exe`. This separates toolchain problems from
   Zitter problems.
2. Check that `tools/install_chromium_overlay.py` works on Windows (paths, line
   endings). The Keychain patch applies only to macOS.
3. Add `chromium/windows.args.gn` with `target_cpu = "x64"` and a component
   Release build for development.

If ninja stops on a MIDL output mismatch (the local `midl.exe` writes different
`.tlb` bytes than Chromium's checked-in ones), run the `copy /y` command it prints
to rebaseline `third_party/win_build_output/midl` in the checkout, then resume.

**Done when** vanilla Chromium runs and the overlay installs.

## Phase 1: the non-Mac build (shared with Linux)

- Replace CoreText with a shared text layer on `gfx::RenderText`, HarfBuzz and
  Skia:
  - `EditorTextLayout`: graphemes, words, carets, `HitTest`, `Spans` and `Draw`;
  - `ShapeSidebarParagraph`;
  - `PaintEditorSceneFallback`.

  `Draw(CGContextRef)` becomes a neutral `Draw(gfx::Canvas*)`.
- Move `editor_text_layout_test.mm` to a cross-platform test.
- Choose the font: the system font on macOS; Segoe UI Variable or the fonts in
  `assets/fonts` on Windows.
- `BUILD.gn`: build the `zitter` target for `is_win` too. Then compile and link
  `:shell` and fix whatever breaks, since this path has never been built.

**Done when** the Rust tests (`sidebar_scene`), `network_policy_tests` and the new
text tests pass on Windows.

## Phase 2: first launch of `zitter.exe`

- Entry point in [main.cc](../chromium/app/main.cc): `wWinMain`, sandbox info,
  plus the `install_static` and `chrome_elf` pieces that `//chrome/browser`
  requires on Windows.
  - Start with a single-exe spike, as in content_shell.
  - If that fails, split into `zitter.exe` and `zitter.dll`, as Chrome does.

  **This is the biggest risk of the port.**
- Store data (profiles, cache) in `%LOCALAPPDATA%\Zitter`.
- `Runtime`: generalize the macOS-only shutdown logic (`ConfirmDownloadShutdown`),
  because closing the last window exits the app on Windows. Moved to phase 3: it
  needs a dialog in Zitter's design; until then closing cancels downloads.
- `i18n.cc`: keep the language choice outside `CFPreferences`. Elsewhere it is
  Chromium's own `intl.app_locale` in Local State, which also switches the
  Chromium-owned UI on the next start, as in Chrome on Windows and Linux.
- `network_policy.cc`: check the Safe Browsing key file's permissions through
  ACLs instead of POSIX modes.
- Use the plain Views frame for now.

Notes from the first launch:

- `zitter.exe` is one executable for every process, as content_shell; only
  `chrome_elf.dll` stays separate. On Windows the crash reporter client lives in
  `chrome_elf`, so the `zitter` target compiles it only on POSIX.
- The sidebar, palette and panels are separate translucent windows. PrintWindow
  leaves them blank, so screenshots copy the screen with the window in front.
- The window has no app icon yet (`.ico` from `assets/icon`, phase 3 or 6).

**Done when** the window works, pages load, and profiles and workspaces survive a
restart with the session restored.

## Phase 3: native Windows look

- `workspace_frame_win.cc`: a custom title bar without the system frame.
  - Caption buttons are 46×38; the close button hovers red `#C42B1C`.
  - Hit tests return `HTCAPTION`, and `HTMAXBUTTON` for Snap Layouts on Windows 11.
  - Dark mode and Mica come from `DwmSetWindowAttribute`.
- Lay out the header as designed: caption buttons on the right, and the |z⟩ menu
  replacing the macOS menu bar.
- Keyboard:
  - Alt+1/2/3 and Ctrl+Alt+←/→ for workspaces;
  - panels without a `workspace_keyboard_mac` counterpart: Alt, F6, Ctrl+Tab and
    Alt+F4;
  - emoji through Win+. (`ui::ShowEmojiPanel`).
- HiDPI at 125% and 150%, and sharpness of the Rust scene.
- Finish with the `verify-ui` skill.

Notes:

- The client area covers the titlebar (`workspace_frame_win.cc`, as Chrome's
  frame does). Caption buttons are Views buttons that answer HTCLIENT: with the
  system frame, HTMINBUTTON/HTMAXBUTTON/HTCLOSE make Windows draw its classic
  buttons. Snap Layouts on Windows 11 needs HTMAXBUTTON, so there the frame
  host answers it over the maximize button and handles the non-client hover,
  press and click itself, which keeps DefWindowProc from drawing the classic
  button. `--zitter-force-snap-layouts` takes that path on Windows 10 (no
  flyout there): hover, press, maximize and restore checked; the flyout itself
  is not, for want of a Windows 11 machine.
- The |z⟩ menu follows the design: 16 px line icons per row, shortcuts in mono
  `ink-muted` (drawn as the row's minor icon, since Views draws minor text in
  the row's font; the accessible name keeps the chord), each space's dot in its
  color, and „O Zitter” with Zitter's and Chromium's versions.
- Scene dialogs (remove a space, quit while downloading, About) are bubbles
  without an anchor; macOS shows them as sheets, Windows put them at the
  screen's corner. They now center over the window.
- verify-ui: `ui.py launch --theme light` (Papier) and `--scale 1.25`/`1.5`.
  Checked: Noc and Papier with the menu open, 125% and 150% (Rust text and the
  1 px caption glyphs stay sharp), the inactive window's faint glyphs.
- Design fonts (Instrument Serif, Schibsted Grotesk, JetBrains Mono in
  `assets/fonts`) are registered from the bundle on macOS. On Windows GN copies
  them to `Fonts\` beside `zitter.exe`, which sideloads them into the browser
  process's DirectWrite collection before gfx creates it (Chromium's web-test
  hook); the overlay installer patches `ui/gfx/win/direct_write.cc` so variable
  fonts bring every named weight. Web pages don't see them. Text that asks for
  the default UI font stays Segoe UI, as it stays SF on macOS.
- The icon sits in chrome.dll's `IDR_MAINFRAME` slot, patched by the overlay
  installer: every icon must be in that one `.rc`, since icon image IDs collide
  across `.res` files.
## Phase 4: import and security

- `arc_importer.cc` paths:
  - Chrome: `%LOCALAPPDATA%\Google\Chrome\User Data`;
  - Arc for Windows: its MSIX package;
  - optionally Edge.
- Import bookmarks, history, tabs and CSV passwords. There is no cookie import
  (see Decisions).
- Zitter's own OSCrypt uses DPAPI, the default, with no patch.
- Check the sandbox (`chrome://sandbox`) and repeat the network audit
  (`audit_background_network.mjs`).

Notes:

- Arc for Windows is the MSIX package `TheBrowserCompany.Arc_*`; its
  `LocalCache\Local\Arc` holds `StorableSidebar.json` and `User Data` as on
  macOS. Its sidebar also holds Little Arc windows (`littleBrowser` containers)
  whose spaces have no pinned tabs; the importer skips them on both systems.
  `arc_importer_unittests` covers the format; its disabled tests read this
  computer's Arc, Chrome, Edge and Brave and print only counts.
- Chrome, Edge and Brave: `%LOCALAPPDATA%\<vendor>\<browser>\User Data`.
- The cookie option is hidden on Windows (App-Bound Encryption).
- Passwords come over as a CSV export: Saved sign-ins has Import passwords
  from CSV, which runs Chromium's `PasswordImporter` (as its
  `StartupPasswordsImporter`) into the profile's store
  (`password_csv_import.cc`). Checked with a two-entry Chrome-format file.
- Sandbox: renderers and the storage service run Untrusted, the GPU process Low,
  nothing with `--no-sandbox`. The network service runs at Medium, as in
  Chromium: `kNetworkServiceSandbox` is off upstream (Chrome turns it on by
  field trial). Turning it on is a decision for later.
- Network audit (four scenarios, 60 s): only `update.googleapis.com`
  (component updates), as on macOS. It found two Windows-only issues, fixed:
  - Chromium installed the extensions other apps register for Google Chrome
    (`Software\Google\Chrome\Extensions`, here Google Docs Offline from
    `clients2.google.com`). A guarded overlay patch skips that provider when
    `zitter.exe` passes `--zitter-ignore-chrome-registry-extensions`.
  - Chromium's own quit (DevTools `Browser.close`, the session ending) closed
    all browsers but not Zitter, whose keep-alive outlived them; Runtime now
    closes its window then. `WM_ENDSESSION` saves the session as Chrome does.
## Phase 5: tests and tools

- Run `zitter_prototype` on Windows. The macOS native keyboard tests (12 `IS_MAC`
  blocks in `profile_workspace_browsertest.cc`) get Windows counterparts.
- Replace `tools/ui_capture.swift` with a window capture through
  Windows.Graphics.Capture or `PrintWindow`, and add a Windows variant of the
  `verify-ui` skill.
- Add Windows sections to `CLAUDE.md` and the README.

Notes:

- `zitter_prototype` builds and runs on Windows (271 tests). The first full run
  failed 41, all for test-environment reasons, now fixed: the upload, media and
  preconnect fixtures turn the network sandbox off too; a mock TSF bridge keeps
  TSF from committing the compositions the editor tests inject (keys still type
  through `WM_CHAR`); primary-modifier chords send Ctrl; the Arc package path's
  parents exist before test data is copied; the profile tests run at
  200%, since a high-DPI desktop is small in DIPs (300% of 4K is 1280 × 720;
  at a forced 100% the harness renders extension `setIcon` images empty, while
  the app at 100% swaps them, checked by hand); cookie
  import and Return in the permission prompt follow Windows. Four tests that
  compare with macOS overlay scrollers or sidebar key focus run on macOS only:
  Windows scrollers are classic and take layout space, and its sidebar never
  takes key focus.
  The full suite then passes: 226 of 226, none flaky (four disabled).
- Browser tests run with `fieldtrial_testing_config`, which turns the network
  sandbox on, and its AppContainer can't read an `out\` directory no installer
  granted access to (`lpacChromeInstallFiles`); they turn the sandbox off on
  Windows. (The app itself had it off: Zitter passes
  `--disable-field-trial-config`. Phase 6 turns it on.)
- Windows-only fixes the tests found: shortcuts saved with Ctrl+H (History) were
  rejected on load, since H, M and Q were reserved as on macOS; `near` is a macro
  in the Windows SDK.
- Real input finds what injected events don't. Driving the window with real
  mouse and keyboard events showed that Windows ate every click on the sidebar
  and the undo toast: Views answers `WM_MOUSEACTIVATE` with
  `MA_NOACTIVATEANDEAT` for a non-activatable window that has a non-client
  view. Those panels now get a host that answers `MA_NOACTIVATE`
  (`ConfigureNonActivatingPanel`). Checked with real input since: tab and
  space context menus, New tab, Ctrl+T with typing and Enter, Alt+1/2, Ctrl+W,
  Ctrl+J, Ctrl+H, Ctrl+Shift+H and Ctrl+,.
- `ui_capture.swift` measures UI Lab frame timing, which is macOS-only (Metal); it
  has no Windows counterpart. `verify-ui` has: `ui_win.py` (launch, windows, click,
  shot) and `build_app_win.ps1`; `run_metal_sidebar.py` starts `zitter.exe`.
- The macOS-only Python tests (shebang stubs) skip elsewhere; the launcher writes
  its UTF-8 fixtures explicitly, so all 79 pass on Windows.
- The Windows sections in `CLAUDE.md` and the README wait for the rewrite of
  those files that is in progress.
## Phase 6: packaging and distribution

- `tools/package_win.py`:
  - first a portable ZIP: exe, `.pak` files, `locales`, `icudtl.dat`, V8
    snapshots, and the ANGLE and SwiftShader DLLs;
  - then the installer from the design.
- The installer:
  - detects an existing installation and offers update or removal;
  - lets the user choose per-user (`%LOCALAPPDATA%\Programs\Zitter`) or machine
    (`C:\Program Files\Zitter`).
- Icon (`.ico`), version resource (`.rc`), manifest (per-monitor DPI v2) and an
  AppUserModelID for the taskbar and jump list.
- Authenticode signing (or Azure Trusted Signing) to limit SmartScreen warnings.
- Registration as the default browser (ProgId, Capabilities).
- License audit of the Windows binaries (`audit_licenses.py`).
- Optimized x64 build with ThinLTO and PGO (`win64.pgo`).
  - Installed Windows copies support automatic updates starting with 0.3.0;
    see [windows-updates.md](windows-updates.md).

Notes:

- `tools/package_win.py` packs GN's runtime_deps of `//zitter:zitter` (without
  symbols, generated sources and test data) into
  `build/Zitter-<version>-win-x64-portable.zip`, and with `--setup` appends the
  same files to `zitter_setup.exe` as `Zitter-<version>-win-x64-setup.exe`. The
  version is `chromium/app/zitter_version.h`; `zitter.exe` carries it and the
  AppUserModelID `Zitter.Browser`.
- `zitter_setup.exe` (`chromium/installer/`) is the design's installer: a
  Direct2D window in Noc with the design fonts embedded, no Chromium code. It
  installs just for the user or for everyone (relaunching itself elevated),
  updates or removes what it finds, and on removal can delete browsing data.
  It writes the files, `zitter_setup.exe` as the uninstaller (without the
  payload), Start menu and desktop shortcuts with the AppUserModelID, Apps &
  features, and the browser registration Default apps lists (`ZitterHTML`,
  `StartMenuInternet\Zitter`, `RegisteredApplications`; http, https and web
  files). It grants the install folder to the two install-files capabilities,
  as Chrome's installer does, and `NetworkServiceSandbox` is now on: installed,
  the network service runs in its AppContainer.
- Checked end to end for one user on Windows 10: install, update over it,
  pages loading with the sandboxed network service, removal from Apps &
  features leaving nothing behind. For everyone, with UAC: install to Program
  Files (HKLM entry, all-users Start menu shortcut, browser registration, the
  install-files capabilities on the folder), Zitter started from there as the
  user with its network service in the AppContainer, and a quiet removal
  through `QuietUninstallString` leaving nothing. The elevated copy starts a
  moment after UAC (Windows checks the unsigned file first), so scripts should
  wait for it rather than for the first process.
- A component build's installer needs its DLLs beside it; the standalone one
  comes from the static release build in `out/ZitterWinRelease`
  (`chromium/windows-release.args.gn`, about 8 hours from scratch on this
  machine). Its package is 191 MB (ZIP and installer alike) against 430 MB from
  the component build: `zitter.exe` is 325 MB uncompressed, and the packager
  leaves out what Chrome's installer leaves out too (Vulkan validation layer,
  mock driver, symbol readers, pseudolocales).
- `--quiet` with `--install-now` or `--uninstall` runs without questions or a
  launch afterwards and reports through the exit code (1 when Zitter is running
  or the work failed); Apps & features gets it as `QuietUninstallString`.
- Checked with the release installer: a fresh install from a folder holding only
  the setup, an update over it, a quiet update, a quiet removal refused while
  Zitter runs, and a quiet removal that leaves no folder, Apps & features entry,
  shortcut or browser registration.
- `tools/audit_licenses.py` also reads the Windows package (its folder or ZIP):
  binaries match their link rules by content, and their imports, delay-loaded
  ones included, come from the PE headers. The release package
  ([summary](licenses/windows-audit-2026-10-31.json),
  [manifest](licenses/windows-manifest.json.gz),
  [license texts](licenses/windows-THIRD_PARTY_LICENSES.txt.gz)) has 344
  components, 253 with direct link evidence; 7 of 8 binaries match a link rule,
  and the eighth, `d3dcompiler_47.dll`, is the Windows SDK's redistributable.
  The copyleft and restricted set is the macOS one (FFmpeg LGPL, hunspell,
  Symphonia MPL, UnRAR, WebKit LGPL) without libusb, plus three Windows-only
  entries: DirectX Shader Compiler (`dxcompiler.dll`; its GPL-2.0 file carries
  the autoconf exception), ISimpleDOM (MPL-1.1, accessibility interfaces) and
  WTL (MS-PL, headers with no link evidence). `zitter.exe` imports no MSVC
  runtime DLL: the static build carries the CRT. `package_win.py
  --third-party-licenses` ships the collection beside `zitter.exe`.
  ```
  python tools/audit_licenses.py --app build/release/Zitter-0.1.0-win-x64-portable.zip --out .chromium/src/out/ZitterWinRelease --destination build/license-audit/windows
  python tools/package_win.py --out .chromium/src/out/ZitterWinRelease --setup .chromium/src/out/ZitterWinRelease/zitter_setup.exe --destination build/release --third-party-licenses build/license-audit/windows/THIRD_PARTY_LICENSES.txt
  ```
- `chromium/windows-official.args.gn` (`out/ZitterWinOfficial`) is the release
  build with `is_official_build`, which brings ThinLTO, and PGO from Chromium's
  win64 profile (gclient `checkout_pgo_profiles`): about 7 hours from scratch
  here, the LTO link last. `zitter.exe` is 296 MB against 325 MB without them,
  and the package 188 MB (189 MB installer). Checked: quiet install from the
  installer alone, the network service in its AppContainer, a page loading,
  the first-run wizard, quiet removal leaving nothing. This is the build to
  ship:
  ```
  python tools/package_win.py --out .chromium/src/out/ZitterWinOfficial --setup .chromium/src/out/ZitterWinOfficial/zitter_setup.exe --destination build/official --third-party-licenses build/license-audit/windows/THIRD_PARTY_LICENSES.txt
  ```
- Not done: Authenticode signing (no certificate yet).
- The installer draws its own controls, so it describes them to UI Automation
  itself (`setup_accessibility.cc`): headings and sentences as text, the two
  placements as radio buttons, the switches with the Toggle pattern, buttons
  with Invoke, progress as a progress bar, each with a name, help text and an
  automation id, in reading order. Focus, toggles, selection and page changes
  raise events. The providers use COM threading, so UIA calls them on the
  window's thread. Checked with a UIA client on the install and remove pages:
  every control is listed with its state, and Toggle, Select, SetFocus and
  Invoke work. Keyboard: Tab, Enter, Space, Esc.
## Zitter Dev for side-by-side testing

Enable `zitter_dev_build = true` in a Windows build directory's `args.gn`. Start
from `chromium/windows-release.args.gn` and save the arguments to
`.chromium/src/out/ZitterWinDev/args.gn`. Use the toolchain environment above.
Setup must come from a static build so it can start before extracting the
browser's DLLs. For faster local linking, you can build the browser in a separate
component directory with the same Dev option and package it with the static Dev
setup.

```powershell
python tools/install_chromium_overlay.py .chromium/src
cd .chromium/src
gn gen out/ZitterWinDev
autoninja -C out/ZitterWinDev zitter zitter_setup
cd ../..
python tools/package_win.py --out .chromium/src/out/ZitterWinDev --setup .chromium/src/out/ZitterWinDev/zitter_setup.exe --destination build/dev
```

The unsigned `Zitter-Dev-*-win-x64-setup.exe` installs for the current user under
`%LOCALAPPDATA%\Programs\Zitter Dev`. Dev has its own Start menu shortcuts,
taskbar identity, Apps & features entry and browsing data under
`%LOCALAPPDATA%\Zitter Dev`. Explicit `--user-data-dir` paths must be inside
that directory. Install a newer local build over it to keep Dev data.

Open **Zitter Dev - Clean profile** from the Start menu to test first run.
Each launch starts with a fresh profile. The test profiles stay in
`%LOCALAPPDATA%\Zitter Dev\Clean Profiles` for inspection. To reset the installation
itself, uninstall **Zitter Dev** and select **Also remove browsing data** before
reinstalling. Build and run setup again to refresh Dev. The Dev build uses manual
updates and is intended for local testing.

## Windows integration

- Links from other apps: a second `zitter.exe` forwards its command line to
  the running one (Chrome's process singleton). `main()` used to add
  `--no-startup-window` to every process, so the forwarded line carried it and
  Chrome dropped the URLs; the switch now goes on in `PreBrowserStart`, in the
  process that owns the profile. Forwarded links open as external links (the
  active space, domain rules) through `ReplaceStockWindow`, and a named startup
  profile keeps Chrome's profile picker away from launches without a URL.
- Jump list (`jump_list_win.cc`): the spaces, then New tab and Command
  Center. Each entry runs `zitter.exe --zitter-command=…`; a running Zitter
  takes it through `WM_COPYDATA` before the second process starts Chromium,
  and a fresh start runs it once the window is ready. Windows refuses custom
  categories while „Show recently opened items in Jump Lists” is off
  (`Start_TrackDocs`), so the spaces then join the tasks above a separator.
- First run (`ShowWelcome`): welcome, default browser, then import or a new
  first space. The default-browser button opens Zitter's Default apps page for
  either a per-user or machine installation, with instructions for Windows 10
  and 11. Returning to the wizard checks both HTTP and HTTPS associations.
  Import runs inside the wizard and reports success or a retryable error there.
  Selecting import hides the first-space name/color step because the imported
  data supplies the spaces. Skipping import keeps that step.
- The installer offers “Launch Zitter after installation”, enabled by default
  for interactive installs and updates. `--no-launch` disables it; `--quiet`
  always suppresses launch. After an elevated install, the desktop shell launches
  the browser as the signed-in user.
- Web notifications: a page's request goes through Zitter's site permission
  prompt, and once allowed `new Notification(…)` shows as a native Windows
  toast (Chromium's Windows notification platform, attributed to the site).
- Passwords open Chrome's password manager (`chrome://password-manager`) as a
  tab of the active space, as History opens `chrome://history`. The |z⟩ menu
  has „Importuj z przeglądarki…” instead of the passwords entry.
- Main's Time Machine timeline and fingerprint protection build and pass their
  tests on Windows. Windows needed three changes:
  - `tools/fingerprint_protection_patch.py` reads its targets as bytes and
    closes its temporary file before replacing (text mode turned CRLF into LF;
    Windows can't replace through an open handle).
  - Its browser test disables the network sandbox, as the other fixtures do.
  - The test enables pixel output: Windows browser tests otherwise read blank
    canvases, so noise had nothing to change.
- Find in page and printing (all systems; design: [Find & Print](https://claude.ai/artifact/TcySm8V2L3jZaWZzjXaivj)):
  - Ctrl+F opens a bar over the page's top right. It drives Chromium's
    `FindTabHelper`; the counter, Enter / Shift+Enter, match case and Esc are
    Zitter's.
  - Ctrl+P opens Zitter's print sheet:
    - The page goes to PDF in the renderer (`PrintToPdf`).
    - The sandboxed printing service rasterizes the preview (PDF to PWG raster).
    - Printers get the PDF through Chrome's print job queue (`PrintForPrintPreview`).
    - Save as PDF writes it after a save dialog.
    - Ctrl+Shift+P opens the system dialog.
  - Chrome's own Print Preview needs a Chrome browser window, so Zitter starts
    with `--disable-print-preview`. A page's `window.print()` reaches
    `ZitterPrintManager` (a `PrintViewManager`), which ends the page's call and
    opens the sheet for the page on screen.
  - Match highlights take the design's colors: the current one on `zing`, the
    others on light violet. The overlay patches Blink's `layout_theme.cc` for
    this. The scrollbar's match ticks keep Chromium's color.
  - Checked on Windows: the preview, Save as PDF, `window.print()`, and a real
    job through Microsoft Print to PDF.

## Media playback and browser controls

Windows protocol launches forward HTTP(S) URLs directly to the running workspace
window through the native command channel. Both the executable and user-data
directory must match. This avoids depending on a temporary stock Chromium window
to deliver the URL; requests arriving during profile loading wait until it is
ready. The registered `--single-argument %1` command preserves the whole URL.

The Windows development, release, and official GN configurations enable
`proprietary_codecs` with `ffmpeg_branding = "Chrome"`. These settings provide
H.264/AAC playback for common MP4 and MOV recordings and enable Chromium's
platform HEVC support where the device supports decoding it. Existing output
directories must receive the updated arguments and be rebuilt; installing the
source overlay alone does not change the codecs in an existing executable.

HTML fullscreen requests use the workspace widget's fullscreen mode. Browser
chrome and split-pane headers disappear while the requesting page fills the
window. Esc restores the previous window mode, sidebar preference, and pane
layout. Peek temporarily hosts its page in the main window and restores the
preview after fullscreen ends.

F5 reloads the active page on Windows. The refresh icon rotates during loading,
with a short minimum animation for fast reloads, respecting reduced motion.
The sidebar address row also offers a link button to copy the current URL.
The address palette accepts platform clipboard shortcuts; Tab fills the selected
suggestion's URL without opening it, and Enter opens it.

The auto-hidden sidebar slides over 180 ms. A 28-DIP margin beyond its right edge
and a 250-ms leave delay keep its resize handle reachable. The sidebar remains
open during resizing, dragging, and its menus or settings.

## Status

| Phase | State |
| --- | --- |
| Design | Done ([canvas](https://claude.ai/artifact/V1fAXXfmYYMv3QMbAURWjJ)) |
| 0 — environment | Done: vanilla `chrome.exe` runs (`out/ZitterWin`), the overlay installs |
| 1 — non-Mac build | Done: `:shell` compiles on `text_services_skia.cc`; `sidebar_scene_unittests` (532), `zitter_network_policy_tests` (3) and `editor_text_layout_unittests` (6) pass. The `zitter` target for `is_win` moves to phase 2 |
| 2 — first launch | Done: `zitter.exe` opens the window, loads pages and restores the session and its space after a restart; data in `%LOCALAPPDATA%\Zitter`. The download shutdown prompt moves to phase 3 |
| 3 — native look | Done: own titlebar on the system frame (shadow, snap, resize), 46 × 38 caption buttons, Snap Layouts (HTMAXBUTTON, Windows 11), the Zitter menu with icons and mono shortcuts, Windows shortcuts and Kbd words, the emoji panel, the downloads prompt on close, centered dialogs, clickable sidebar and toasts, the jump list, the first-run wizard, links from other apps, the app icon, design fonts. verify-ui: Noc and Papier, restored and maximized, 125% and 150%. The Snap Layouts flyout itself awaits a Windows 11 machine |
| 4 — import and security | Done: Arc (MSIX), Chrome, Edge and Brave paths, real Arc and Chrome data read, CSV passwords, no cookie option, sandbox checked, network audit clean after two fixes |
| 5 — tests and tools | Done but the docs: the full browser suite passes (226), Rust (532) and Python (83) tests pass, verify-ui works on Windows. Open: Windows sections in CLAUDE.md and README, waiting on their rewrite |
| 6 — packaging | Done but signing: static release and ThinLTO + PGO builds (188 MB package), portable ZIP and standalone installer (191 MB), install, update and quiet remove checked for one user and for everyone, browser registration, network sandbox, license audit, installer accessibility. Open: Authenticode signing (no certificate) |
