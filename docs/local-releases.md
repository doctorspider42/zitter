# Local releases

Use this workflow to build Zitter on a local machine and publish the resulting
application to `doctorspider42/zitter`. A request for a local build alone does not
authorize GitHub publication. A request to publish a release authorizes creating
the release and uploading its files without another confirmation.

The default macOS download is a signed and notarized **DMG**, containing the
signed/notarized `Zitter.app` and an `Applications` shortcut. Users open the DMG,
drag the app into Applications, eject the image and launch the installed app.
The `.app` is the actual application bundle; the `.dmg` is its distribution disk
image. No privileged installer package is needed for this drag-and-drop install.

## One version, one release

**Build every operating system's assets for a version from the same source tag
and attach them to the same GitHub release.** macOS can be published first and
Windows added later. Never create an OS-specific release for the same version,
move a published tag, or attach a different source revision under that version.
If Windows requires source fixes, publish a new version for that changed source.

Inspect the working tree, tags and remote releases before choosing a version:

```sh
git status --short
git rev-parse HEAD
git remote -v
gh auth status
gh release list --repo doctorspider42/zitter
git ls-remote --tags origin
```

For an existing version, fetch and inspect the tag. Build it in a clean checkout
or worktree rather than switching a working tree containing unrelated edits:

```sh
git fetch origin tag v0.1.0
git rev-parse 'v0.1.0^{commit}'
gh release view v0.1.0 --repo doctorspider42/zitter \
  --json tagName,targetCommitish,isDraft,isPrerelease,assets,url
```

Examples use `v0.1.0`; substitute the requested version throughout. Compare the
tag's actual commit with the build's recorded commit; `targetCommitish` alone is
not proof because it can contain a branch name. Do not label uncommitted product
changes as a build of a clean published tag. Documentation-only additions do not
require rebuilding an already published binary.

## macOS: build the optimized application

The local build uses `.chromium/src/zitter`, `.chromium/src/out/ZitterOptimized`,
and the standard `zitter` target. The former `zitter_ui_lab` overlay remains a
benchmark reference. Check for another active build before staging into these
shared directories; do not interrupt it.

```sh
pgrep -fl 'ninja|build.sh'
cat .chromium/src/out/ZitterOptimized/args.gn
./build.sh
```

Read the actual GN arguments, not just the output directory's name. A release
requires `is_debug = false`. The optimized setup used for v0.1.0 also has
`is_component_build = false`, ThinLTO, and PGO. `chromium/release.args.gn` is an
older component configuration, not the authoritative optimized configuration.
Add `proprietary_codecs = true` and `ffmpeg_branding = "Chrome"` from
`chromium/optimized.args.gn` to existing output directories. The local build
checks both settings so packaged builds support H.264 and AAC.
If PGO is enabled, its pinned profile must be available. Do not silently fall
back to a Debug build when a release configuration fails.

`build.sh` runs the guarded full overlay installer, including application/bootstrap
sources, assistant assets, renderer hooks and vendored ad-blocking crates. It
generates the standard target's graph with `--root-target=//zitter`, builds with
Chromium's own Ninja, audits the concrete bundle and repackages
`build/ZitterRustMetal.app` with fresh license notices and the local signature.
The standard target also matches the ad-blocking vendor's GN visibility; copying
only the UI into the old lab overlay no longer synchronizes the whole browser.

It requires the pinned Chromium checkout, optimized output and local signing
configuration; it is not a fresh-checkout toolchain bootstrap. Modified or
unowned overlay files are refused. Use `ZITTER_BUILD_TESTS=1 ./build.sh` to build
`zitter_prototype` too, and `ZITTER_BUILD_JOBS` to set Ninja concurrency (default
6). The depot-tools Python runtime is preferred; `ZITTER_PYTHON` can override it.
`--run` retains the owned isolated-profile launcher. A local build does not create
a release tag, submit to Apple, or upload distribution assets.

Audit the actual target and collect its license notices:

```sh
python3 tools/audit_licenses.py \
  --app build/ZitterRustMetal.app \
  --out .chromium/src/out/ZitterOptimized \
  --target //zitter:zitter \
  --destination build/releases/v0.1.0/audit
python3 tools/package_app.py \
  --out .chromium/src/out/ZitterOptimized \
  --binary zitter \
  --app-version 0.1.0 --hardened-runtime \
  --destination build/releases/v0.1.0/Zitter.app \
  --no-register \
  --third-party-licenses build/releases/v0.1.0/audit/THIRD_PARTY_LICENSES.txt
```

Signing uses `ZITTER_CODESIGN_IDENTITY` or `.zitter-signing.json` and the macOS
Keychain. Do not commit credentials. Pass `--app-version` to set both bundle
version fields. `--hardened-runtime` signs nested code from the inside out using
the Chromium browser/renderer/GPU entitlement split in `chromium/signing/`.
These entitlements match the pinned Chromium signing configuration; renderer
and GPU helpers, including Aperitif variants, receive `allow-jit`.

Inspect the audit's unmatched binaries, dependency paths and metadata diagnostics.
The repository's `docs/licenses/supplements.json` covers known metadata gaps;
do not describe scanner output as a complete compliance certificate. Include
`LICENSE`, `NOTICE`, fonts and their licenses, and `THIRD_PARTY_LICENSES.txt` in
the app. Check for absolute checkout dependencies and broken symlinks: the
packager's development-bundle description does not promise a portable installer.

Run the packaged application with an isolated profile. The existing short
navigation probe exercises page loading, new tabs, Split View and clean exit:

```sh
python3 tools/benchmark_app.py \
  --app build/releases/v0.1.0/Zitter.app \
  --scenario navigation --iterations 1 --tabs 2 \
  --label v0.1.0-release-smoke --results build/releases/v0.1.0/smoke
```

This probe expects the packager's ownership marker. After verification, replace
`Contents/Resources/zitter-development-bundle` with release build metadata so
the distributed app does not expose the local checkout path. Make all resource
and version changes before signing the outer bundle again with the configured
identity. Verify the final seal:

```sh
codesign --verify --deep --strict build/releases/v0.1.0/Zitter.app
```

Validate the archive and, when portability is uncertain, launch an extracted copy
outside the checkout. Developer ID signing and Apple notarization are separate:
report the actual notarization status and possible first-launch Gatekeeper block.
Do not claim a notarized installer or working updater unless verified.

For distribution, use the automated app notarization and DMG workflow below.
It removes the development marker itself. ZIPs produced during app notarization
are intermediate artifacts and are not uploaded as the default user download.

## Apple notarization

The existing team is `7L5KF39DAR`; the local Developer ID Application identity
is already configured. An App Store listing, new App ID, provisioning profile,
or new Developer ID certificate is not needed for this workflow. Notarization
uses the existing certificate and a separate credential for Apple's service.

### One-time account setup (user action)

1. Open [Apple Account](https://account.apple.com/) and sign in using the same
   developer account. Apple Developer and Apple Account can require separate
   sign-ins. Complete any two-factor authentication yourself.
2. Open **Sign-In and Security**, then **App-Specific Passwords**.
3. Generate an app-specific password named `Zitter notarization`. Complete any
   password confirmation yourself. Keep the generated value private; do not put
   it in chat, a shell command, repository file, environment file or logs.
4. In a local Terminal, run the following command, substituting your Apple
   Account email. Paste the generated app-specific password into its secure
   prompt. This validates and saves it under the `zitter-notary` Keychain profile:

   ```sh
   xcrun notarytool store-credentials zitter-notary \
     --apple-id YOUR_APPLE_ACCOUNT_EMAIL --team-id 7L5KF39DAR
   ```

5. Tell the agent that the Keychain profile is ready. The script needs only its
   name, not the password. App Store Connect API credentials can also be stored
   with `notarytool store-credentials` if the user already has an appropriate key;
   do not create or broaden account access just to avoid the interactive setup.

Apple requires two-factor authentication for app-specific passwords. Changing
the primary Apple Account password revokes those passwords, so repeat setup
when needed. Browser automation can inspect the existing team/certificate and
navigate to the settings, but the user must generate and enter the new credential.

### Prepare, test, submit and resume

Prepare a private copy without contacting Apple:

```sh
python3 tools/notarize_app.py \
  --app build/releases/v0.1.0/Zitter.app \
  --destination build/notarization/v0.1.0 --prepare-only
python3 tools/smoke_release_app.py \
  --app build/notarization/v0.1.0/work/Zitter.app \
  --results build/notarization/v0.1.0-smoke
```

`--results` must be a new directory. This smoke probe exercises navigation,
new tabs, Split View and clean exit using normal Chromium helpers and an isolated
profile. It does not modify signed app resources or require a development marker.

The preparer preserves the source application and build outputs, removes local
development markers, signs all nested code with Hardened Runtime and a timestamp,
then writes `submission.zip` and `notarization-state.json`. The source must
already include its license notices. Use `--sign-identity` only to deliberately
select another configured certificate. No signing credentials are written to
the state file.

Every physical Mach-O path is signed and checked for Developer ID, secure
timestamp and Hardened Runtime, including duplicate hard-linked library copies.
`codesign` can replace one hard-link path without updating its aliases; inode
deduplication during signing leaves those copies ad-hoc signed. A successful
outer `codesign --verify --deep` alone does not catch this notarization failure.

After the Keychain setup and smoke test, run the same command without
`--prepare-only`:

```sh
python3 tools/notarize_app.py \
  --app build/releases/v0.1.0/Zitter.app \
  --destination build/notarization/v0.1.0 \
  --keychain-profile zitter-notary
```

It validates the Keychain credential, submits the ZIP to Apple, records the
submission ID, polls for acceptance, retrieves Apple's log, staples and validates
the ticket on the `.app`, verifies signatures, checks Gatekeeper, and only then
creates the final `*-notarized.zip` and its SHA-256 file. ZIP archives cannot
themselves be stapled; they must be regenerated after stapling the app.

The default polling timeout is 20 minutes. Exit code 2 means Apple is still
processing; rerun the same command to resume the saved submission without another
upload. Exit code 1 means failure: inspect the error and, for rejection,
`notarization-log.json`. Do not retry rejected submissions without fixing their
cause. Preserve the work directory while a submission is pending. A changed
prepared app/archive is refused; use a new destination when intentionally
preparing another candidate. If an upload is interrupted before its ID is saved,
inspect `notarytool history` and reconcile the submission before rerunning.
The script refuses to upload again after an interrupted upload; pass
`--submission-id THE_MATCHING_ID` to recover it. Before stapling, it checks that
the SHA-256 in Apple's accepted submission log matches the prepared archive.

Keep Apple's logs and state locally; a
public build report may include the submission ID, final checksum, verified
signing/notarization status and checks performed, with local paths omitted.
Do not change app resources or signatures after notarization. The automated
script does not upload or replace GitHub release assets.

### Create the downloadable DMG

After app notarization completes, package the stapled app:

```sh
python3 tools/package_dmg.py \
  --app build/notarization/v0.1.0/work/Zitter.app \
  --destination build/releases/v0.1.0-dmg \
  --keychain-profile zitter-notary
```

This command validates the source app and its ticket, copies it without changing
the signature, adds an `Applications` shortcut, renders the window background
(`tools/render_dmg_background.swift`, the "Papier" design with the version and
architecture in its footer) and the volume icon, lets Finder save the window
layout, creates a compressed read-only disk image, signs it with Developer ID, and
submits the DMG itself to Apple. It uses the same submission/resume logic as app
notarization. `--prepare-only` stops before submission; `--submission-id` recovers
an interrupted upload after reconciliation with Apple's history.
The layout step runs `osascript` against Finder, so package in a logged-in
session; the first run asks to let the terminal control Finder. The background
is light on purpose: Finder draws icon labels in black over a background
picture, even in Dark Mode. Finder places the icons at (200, 236) and
(520, 236) in a 720×432 window.
Transient DNS/connection failures on Apple status reads are retried up to three
times. Persistent failures stop with the submission ID preserved; rerun to resume.
Uploads and authentication failures are never retried automatically.

After acceptance, the script staples the DMG and checks its checksum, signature,
ticket and Gatekeeper assessment. It mounts the final image read-only, verifies
that its app signature and ticket match the original, then copies that app to a
temporary directory, applies browser-download quarantine and runs
`syspolicy_check distribution`. This adds quarantine protection; never remove
quarantine attributes or tell users to bypass Gatekeeper as a packaging fix.
Mounted test volumes are detached automatically.

Publish only these final outputs by default:

- `Zitter-v0.1.0-macos-arm64.dmg`
- `SHA256SUMS-macos-arm64-dmg.txt`
- `Zitter-v0.1.0-macos-arm64-dmg-build.json`

For an existing version, append the DMG to that same release and make its direct
download link prominent in the release notes. Clearly label earlier ZIPs as
legacy; an unnotarized ZIP must never be the recommended download. Do not delete
published assets without authorization. Product source changes still require a
new version; changing distribution packaging alone does not move the source tag.

If a user sees “Apple cannot check it for malicious software”, inspect the exact
downloaded copy with `codesign -dvv` and `stapler validate`, and compare its code
hash with the published build. A valid local development build is not proof that
the downloaded copy is notarized. In v0.1.0, the first ZIP remained available
alongside the later notarized ZIP; the unnotarized copy in Downloads had the
earlier signature and no stapled ticket. Avoid that ambiguity in future releases.

References: [Apple notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
[Apple app-specific passwords](https://support.apple.com/en-us/102654).

## Windows: build the same tagged source

Use a Windows machine with Chromium's Windows toolchain and the revision pinned
in `chromium/revision.json`. Inspect the checkout's Windows build support and
use its actual native target with `is_debug=false` and the intended architecture.
The macOS `build.sh` and `tools/package_app.py` do not build or package Windows.
Read `docs/windows-port.md` for the current toolchain, build and validation
instructions. The current main branch provides component, static Release and
ThinLTO/PGO configurations, plus `tools/package_win.py` for a portable ZIP and a
standalone setup EXE. Prefer the standalone installer as the user download;
include the audited license collection. For an already built official target:

```powershell
python tools/package_win.py --out .chromium/src/out/ZitterWinOfficial --setup .chromium/src/out/ZitterWinOfficial/zitter_setup.exe --destination build/official --third-party-licenses build/license-audit/windows/THIRD_PARTY_LICENSES.txt
```

Inspect the actual outputs and Authenticode status; the documented installer
currently ships unsigned. Never upload a lone browser EXE without its runtime
resources. Earlier tags, including the original v0.1.0 source, predate this port:
Windows changes on current main require a new product version rather than being
attached as if built from the older tag. The version embedded in the executable
alone does not establish that its source matches the release.

Package and smoke-test the application on Windows with an isolated profile,
including its required DLLs, Chromium resources and license notices. Name files
with the version, platform and architecture, for example
`Zitter-v0.1.0-windows-x64.zip`. Record the exact source commit, Chromium revision,
GN arguments, signature status and checks performed.

## Publish or append assets

Create platform-specific checksum and build metadata files, for example
`SHA256SUMS-macos-arm64.txt` and `Zitter-v0.1.0-macos-arm64-build.json`.
Use SHA-256 (`shasum -a 256` on macOS or `Get-FileHash -Algorithm SHA256` in
PowerShell). Include artifact basenames in checksum files. Platform-specific
names let Windows add its checksums without replacing the macOS file.

Write release notes to a file in English and use `--notes-file` to preserve
newlines. Describe included platforms, installation, relevant changes and actual
limitations. Keep artifacts and local logs under ignored `build/releases/`.

If the release does not exist, create it against the exact built commit:

```sh
gh release create v0.1.0 \
  build/releases/v0.1.0-dmg/Zitter-v0.1.0-macos-arm64.dmg \
  build/releases/v0.1.0-dmg/SHA256SUMS-macos-arm64-dmg.txt \
  build/releases/v0.1.0-dmg/Zitter-v0.1.0-macos-arm64-dmg-build.json \
  --repo doctorspider42/zitter --target FULL_SOURCE_COMMIT_SHA \
  --title 'Zitter v0.1.0' --notes-file build/releases/v0.1.0/release-notes.md
```

If it already exists, **upload to that release**. For example, from Windows:

```powershell
gh release upload v0.1.0 build/releases/v0.1.0/Zitter-v0.1.0-windows-x64.zip build/releases/v0.1.0/SHA256SUMS-windows-x64.txt build/releases/v0.1.0/Zitter-v0.1.0-windows-x64-build.json --repo doctorspider42/zitter
```

Do not use `--clobber` or replace another platform's assets by default. Inspect
an asset-name collision: an identical uploaded digest means the file is already
present; a different digest requires resolving the discrepancy before replacing
anything. Preserve existing notes and release flags; update platform availability
in the notes when adding Windows. Do not recreate a published release.

If GitHub rejects an upload, inspect the error and current assets before retrying.
Check `gh api repos/doctorspider42/zitter/immutable-releases` when immutability is
relevant. Published immutable releases cannot accept additional assets: explain
the constraint without changing repository settings or moving tags. For future
immutable versions, collect every platform's assets in a draft before publishing.

Finally, inspect `gh release view` and verify the published tag commit, draft
status, asset sizes and SHA-256 digests against the local files. Report the release
URL, included platform, checks performed and any material packaging limitation.
