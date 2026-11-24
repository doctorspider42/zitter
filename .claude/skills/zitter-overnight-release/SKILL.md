---
name: zitter-overnight-release
description: Prepare and run a Zitter release of Windows and macOS from the Windows build PC with one overnight command (tools/nightly/fire.py), optionally after updating to the newest stable Chromium. Use when the user wants a release "z jednego komputera", an overnight or nightly build of both platforms, "fajer"/"odpal na noc", a release on both systems at once, or a Chromium update followed by a release ("zaktualizuj chromium i wydaj", "release z najnowszym chromium").
---

# Zitter overnight release

Read these first; they hold the details this skill only sequences:

- `docs/nightly-builds.md`: what `fire.py` checks, runs and leaves behind.
- `docs/local-releases.md`: versioning, the one-version-one-release rule,
  notarization, and how to publish or append assets.
- `docs/remote-builds.md`: the build cluster (build PC, laptop `zitter-node`,
  Mac `zitter-mac`).

The run builds and packages; it publishes nothing. Publishing needs the user's
explicit request for that release (local-releases.md, first paragraph). Building,
`--check` and `--smoke` need no confirmation.

Pick the path:

- **A. Release**: the pinned Chromium (`chromium/revision.json`) stays.
- **B. Release with the newest Chromium**: first update Chromium on both
  machines and make Zitter build and pass its tests on it, then do A.

## A. Release

1. **Version.** Choose it by the rules in local-releases.md and check it is
   free: `gh release list --repo doctorspider42/zitter`, `git ls-remote --tags
   origin`. Set all three defines in `chromium/app/zitter_version.h`
   (`ZITTER_VERSION`, `ZITTER_VERSION_STRING`, `ZITTER_VERSION_SHORT`) and commit
   "Set the version to X.Y.Z". The macOS version comes from the same header.
   The working tree must be clean: the run builds `HEAD`.
2. **Preflight.** Ask the user to turn on the build PC (Docker Desktop with the
   NativeLink server), the laptop and the Mac, logged in and on power. Then:
   ```powershell
   python tools/nightly/fire.py --check
   ```
   Fix every ERROR. If the Mac probe times out, a permission prompt is waiting
   on the Mac's screen: ask the user to click Allow there, then rerun. A warning
   that the version is already published means this run is only a rehearsal.
3. **Fire.**
   ```powershell
   python tools/nightly/fire.py
   ```
   It returns at once; the run continues in the background and keeps the PC
   awake. Tell the user the run id and roughly how long it takes: the Windows
   build is incremental with remote-cache hits (a fresh one after a Chromium
   update takes hours), the Mac builds locally, then notarization usually takes
   minutes but can take longer.
4. **Morning.**
   ```powershell
   python tools/nightly/fire.py --status
   ```
   For a failed step, read its log (`build/nightly/<run>/windows/<step>.log`, on
   the Mac `~/zitter-nightly/runs/<run>/<step>.log`). Fix the cause, commit, and
   fire again; `--skip-windows` or `--skip-mac` rebuilds only the platform that
   failed, but both platforms of one version must come from the same commit, so
   a source fix means firing both again. A notarization that is still pending
   resumes by rerunning the same `notarize_app.py`/`package_dmg.py` command on
   the Mac (local-releases.md).
5. **Before publishing.** Do the checks the run cannot do and that
   local-releases.md asks for: an in-place upgrade from the previous published
   version on each platform when the version choice depends on it, the app's
   smoke tests, and the change-specific checks. Add what you checked to the
   `verification` list in the Windows build record. Show the user the summary
   and the checks, and get the go-ahead for the release if they have not given it.
6. **Publish** (only on the user's request): push the commit, then create the
   release against that exact commit with every file from
   `build/nightly/<run>/release/` and English notes written to a file, as in
   "Publish or append assets" in local-releases.md. Then verify the release's
   tag commit, assets, sizes and SHA-256 digests against the local files, and
   report the URL.

## B. Release with the newest Chromium

Updating Chromium is real work, not a sync: Zitter patches Chromium sources
while staging (`tools/stage_content_embedder.py` and `tools/*_patch.py`), and the
first build after an update recompiles everything. Plan for one or more days.

1. **Find the newest stable release.**
   ```powershell
   (Invoke-RestMethod 'https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1')[0] | Select-Object version, @{n='commit'; e={$_.hashes.chromium}}
   ```
   Compare its version with `status` in `chromium/revision.json` and the Mac
   platform's release (`platform=Mac`): use a version both platforms ship. If
   the pinned revision is already the newest, go to A.
2. **Ask before touching the checkouts**, with the cost: both `.chromium`
   checkouts are shared with other sessions and the user's own work (on the Mac,
   `~/Projects/zitter/.chromium` also serves the user's main checkout). Each sync
   takes about an hour; the first full build takes hours on the cluster for
   Windows and many hours locally on the Mac. On the PC, `fresh_build.py` keeps
   the old `src` aside, which needs about 100 GB free. Check that no build runs
   on either machine (`fire.py --check` does). Agree the window with sessions
   that use the checkouts (CLAUDE.md, "Shared build directory").
3. **Pin the revision.** Update `chromium/revision.json` (`revision`, `status`
   with the Chrome version and tag, `verified_on`) on a branch.
4. **Windows checkout.** If `TOOLCHAIN_HASH` in the new `build/vs_toolchain.py`
   differs from the old one, package the toolchain first
   (`tools/remote_build/package_toolchain.py`, docs/remote-builds.md). Then, in
   the background with a log:
   ```powershell
   python tools/remote_build/fresh_build.py --keep-going
   ```
   It checks out the new revision fresh, stages Zitter, runs `setup_siso.py`
   (hermetic toolchain and the matching Linux clang for the workers), generates
   `out/ZitterWinContent` and builds, collecting every compile error.
5. **Mac checkout.** Over SSH (`ssh zitter-mac`), with depot_tools on PATH:
   ```sh
   cd ~/Projects/zitter/.chromium && gclient sync -D --revision src@<commit> && gclient runhooks
   ```
   Use the worktree `~/Projects/zitter-nightly` at the branch's commit for the
   first Mac builds (`./build.sh --build-only`), so the user's main checkout
   stays untouched.
6. **Mac worker clang.** If `CLANG_REVISION` or `CLANG_SUB_REVISION` in
   `tools/clang/scripts/update.py` changed, the Mac worker needs the new clang:
   copy `tools/remote_build/install_mac_worker.sh`, `mac_worker_action.sh` and
   `nativelink_worker.json5` to `~/zitter-nl` on the Mac and rerun the installer
   with the new package and the SHA-256 of
   `.chromium/src/third_party/llvm-build/Release+Asserts_linux/bin/clang`
   (docs/remote-builds.md, "A Mac as a worker"). Until then the Mac worker fails
   its compiles and the scheduler retries them on the others.
7. **Make it build and pass.** Fix staging patches that no longer apply and
   compile errors on both platforms; commit the fixes on the branch. Then run the
   tests CLAUDE.md lists (Rust `sidebar_scene_unittests`,
   `zitter_network_policy_tests`, `arc_importer_unittests`, the Python suite)
   and the `verify_content_*` checks the update touches, and check the app with
   the `verify-ui` skill on both platforms.
8. **Release.** Merge the branch, bump the version (an updated Chromium with the
   same profile format is at least a minor version; local-releases.md decides),
   and follow A. A `fire.py --smoke` run first is a cheap check that both
   machines are at the new revision.
