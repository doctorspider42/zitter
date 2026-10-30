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
  because closing the last window exits the app on Windows.
- `i18n.cc`: keep the language choice in Zitter's own settings instead of
  `CFPreferences`. This also works on Linux.
- `network_policy.cc`: check the Safe Browsing key file's permissions through
  ACLs instead of POSIX modes.
- Use the plain Views frame for now.

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

## Phase 5: tests and tools

- Run `zitter_prototype` on Windows. The macOS native keyboard tests (12 `IS_MAC`
  blocks in `profile_workspace_browsertest.cc`) get Windows counterparts.
- Replace `tools/ui_capture.swift` with a window capture through
  Windows.Graphics.Capture or `PrintWindow`, and add a Windows variant of the
  `verify-ui` skill.
- Add Windows sections to `CLAUDE.md` and the README.

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
- Auto-update is out of scope for now.

## Status

| Phase | State |
| --- | --- |
| Design | Done ([canvas](https://claude.ai/artifact/V1fAXXfmYYMv3QMbAURWjJ)) |
| 0 — environment | Done: vanilla `chrome.exe` runs (`out/ZitterWin`), the overlay installs |
| 1 — non-Mac build | Done: `:shell` compiles on `text_services_skia.cc`; `sidebar_scene_unittests` (532), `zitter_network_policy_tests` (3) and `editor_text_layout_unittests` (6) pass. The `zitter` target for `is_win` moves to phase 2 |
| 2–6 | Not started |
