---
name: zitter-dev-install
description: Build, install, and launch the local Windows Zitter Dev channel alongside stable. Use for hands-on testing, refreshing Dev after changes, and offering a Dev install at the end of Zitter app work.
---

# Install Zitter Dev locally

Run from the Zitter repository root on Windows. Use the pinned Chromium checkout
and the configured Windows toolchain from `docs/windows-port.md`. Keep the
existing Dev output directories so incremental builds can reuse their objects.

## Build and package

1. Check for an active build using the shared checkout before staging the overlay.
   Run `python tools/install_chromium_overlay.py .chromium/src`. Resolve any
   ownership or revision mismatch before proceeding.
2. For a new output directory only, initialize `out/ZitterWinDev/args.gn` from
   `chromium/windows.args.gn` (component browser) and
   `out/ZitterDevInstaller/args.gn` from `chromium/windows-release.args.gn`
   (standalone static setup). Both output directories are under `.chromium/src`.
   Add `zitter_dev_build = true` to both. For existing directories, check their
   arguments and reuse them. Keep the stable build directories separate.
3. From `.chromium/src`, run the following in the configured toolchain shell.
   Stop on a failed command. When only browser code changes, the setup target
   should be up to date and Ninja will skip rebuilding it.

   ```powershell
   gn gen out/ZitterWinDev
   autoninja -C out/ZitterWinDev zitter
   gn gen out/ZitterDevInstaller
   autoninja -C out/ZitterDevInstaller zitter_setup
   ```

4. Back at the repository root, combine the browser and static setup.

   ```powershell
   python tools/package_win.py --out .chromium/src/out/ZitterWinDev --setup .chromium/src/out/ZitterDevInstaller/zitter_setup.exe --destination build/dev
   ```

## Install and launch

Run the `Zitter-Dev-*-win-x64-setup.exe` printed by the packager when the user
asks to install or try Dev. `--install-now` skips the wizard pages, shows install
progress, and launches Dev on success. If setup reports
that Dev is running, close that channel normally and retry. The installer uses
its own per-user location and retains the existing Dev browsing data. Use the
**Zitter Dev - Clean profile** Start menu shortcut for first-run testing.

Verify that the resulting window is **Zitter Dev**, the lime icon appears on
its shortcut or taskbar button, and the existing stable installation can still
open independently. This local workflow requires no release tag or upload.

After changing browser behavior, UI, or the installer, offer to build and launch
Zitter Dev if the current change has yet to be tried in an installed Dev build.
