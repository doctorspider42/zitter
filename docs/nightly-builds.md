# Overnight builds

One command on the build PC builds the checked-out commit for Windows and macOS
at the same time and leaves the release files ready in the morning:

```powershell
python tools/nightly/fire.py --check   # preflight; run it while you are at the Mac
python tools/nightly/fire.py           # start in the background
python tools/nightly/fire.py --status  # the last run's summary
```

Nothing is published. Uploading the files to a GitHub release stays a separate,
explicit step ([local-releases.md](local-releases.md)).

## Before you fire

- All three machines on: the build PC (Docker Desktop with the NativeLink
  server), the laptop worker and the Mac ([remote-builds.md](remote-builds.md)).
- The Mac logged in and on power. Signing, notarization and the DMG's Finder
  layout need the user's session; on battery the Mac can sleep.
- A clean working tree on the PC. The run builds `HEAD`; the Mac gets the same
  commit through a git bundle, so it does not have to be pushed.
- For a release, a new version in `chromium/app/zitter_version.h`. The preflight
  warns when that version is already published: such a run is a rehearsal.
- No other build on either machine (the preflight checks it).

`--check` also starts a short job in the Mac's session that runs `notarytool`
and asks Finder for its startup disk. The first time, macOS asks whether
python3 may control Finder: click Allow on the Mac within two minutes, or
allow it later in System Settings > Privacy & Security > Automation and run
`--check` again.

## What a run does

| Step | Where | How |
|---|---|---|
| Windows | build PC | `stage_content_embedder.py`, `remote_build/build.py` (C++ on the cluster), a first package for `audit_licenses.py`, `package_win.py` with the license collection, `verify_content_windows_bundle.py` on the portable ZIP |
| macOS | Mac | `~/Projects/zitter-nightly` (a worktree of `~/Projects/zitter` with its `.chromium` linked) at the same commit; `tools/nightly/mac_job.py` as the LaunchAgent `com.zitter.nightly`: `./build.sh`, `notarize_app.py` (prepare, verify, submit), `package_dmg.py`, `verify_content_bundle.py` on the DMG |

Both halves run in parallel. The PC stays awake while `fire.py` runs; the Mac
stays awake through its NativeLink worker's `caffeinate` while on power. The
Mac's build also lends its worker to the Windows compiles, so its own build
shares the CPU with them.

Options: `--skip-mac`, `--skip-windows`, `--no-notarize` (macOS: an ad hoc
signed ZIP instead of the DMG), `--no-verify` (skip the package suites),
`--foreground` (run in the console), `--smoke` (check the pipeline without
building: Windows stages and runs a Siso dry run, macOS runs
`./build.sh --plan`).

## Output

`build/nightly/<YYYYMMDD-HHMM>-<commit>/`:

- `release/`: the files for `gh release upload`, named like published releases:
  `Zitter-v<version>-windows-x64-setup.exe`, `Zitter-v<version>-windows-x64.zip`,
  `SHA256SUMS-windows-x64.txt`, `Zitter-v<version>-windows-x64-build.json`,
  `Zitter-v<version>-macos-content-arm64.dmg`,
  `SHA256SUMS-macos-content-arm64-dmg.txt`,
  `Zitter-v<version>-macos-content-arm64-dmg-build.json`.
- `summary.md`: the state and timing of every step.
- `status.json`, `fire.log`, `windows/*.log`: details. The Mac keeps its step
  logs in `~/zitter-nightly/runs/<run id>/`, and its notarization state in
  `~/Projects/zitter-nightly/build/notarization/nightly-<run id>/`.

A run is `succeeded`, `partial` (one platform failed) or `failed`. A failed
notarization keeps its state: rerunning the same `notarize_app.py` or
`package_dmg.py` command on the Mac resumes it (local-releases.md).

The Windows build record lists only the checks the run made. Before
publishing, do the release checks the run cannot do (an upgrade over the
previous version, the app smoke tests in local-releases.md) and add them to
the record.
