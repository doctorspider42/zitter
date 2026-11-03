# Windows updates

Starting with 0.3.0, an installed Windows x64 copy checks the latest stable
GitHub release 30 seconds after startup and every six hours while running.
Portable copies and executables outside their registered installation do not
check or install updates. Launching with `--disable-zitter-updates` disables the
updater for that run. Benchmark runs also disable it.

The updater accepts only a strictly newer three-component version and the exact
`Zitter-vVERSION-windows-x64-setup.exe` asset from
`doctorspider42/zitter`. Drafts, prereleases, missing Windows installers and
assets without GitHub's SHA-256 digest are ignored. HTTPS requests use the system
proxy and certificate validation; HTTPS-to-HTTP redirects are refused. Downloads
are bounded to the advertised size, with a 600 MiB upper limit, and their size
and SHA-256 must match before they become installable. Partial downloads are
never executed. Network errors leave the running browser untouched.

The installer is cached in the current user-data directory's `Updates` folder.
A short notification announces that it will be installed on exit. Closing
Zitter normally starts a separate helper, which waits for the browser to exit,
checks the cached installer again, and runs setup against the same registered
installation. Setup waits for the helper to exit and refuses to overwrite a
running browser. An update preserves existing shortcuts and browsing data.
It does not restart the browser automatically. If the browser is killed or
another instance prevents installation, the next successful check can reuse the
verified cached installer and retry on a later normal exit.

An installation for the current user updates without elevation. An all-users
installation requires the normal Windows UAC prompt; declining it leaves the
installed version in place. Builds currently have no Authenticode certificate.
The digest verifies the downloaded GitHub artifact; it does not constitute an
Authenticode signature.

Versions before 0.3.0 have no updater and need one manual installer update.
macOS automatic updates are not implemented.

## Publishing

Publish the installer, checksums and build metadata under the same source tag,
following [local-releases.md](local-releases.md). The exact installer asset name
above is part of the updater contract. Stage assets in a draft release, then
publish once every required Windows asset has uploaded successfully. GitHub's
asset `digest` must contain `sha256:` followed by the installer hash.

Run `updater_unittests` to cover release/version selection, rejected download
origins, malformed or absent hashes, size limits, duplicate assets and corrupted
or truncated installers. Smoke-test the packaged application and the setup wait
and update paths before publication.

The opt-in `WindowsUpdater.DISABLED_PublishedInstallerDownloadsAndVerifies`
test exercises real HTTPS requests, redirects, streaming, hash validation and
cache reuse against the latest published installer. Run it with that exact
`--gtest_filter` and `--gtest_also_run_disabled_tests`; it downloads approximately
200 MB into a temporary directory and never executes the installer.
