# Windows updates

Open **About Zitter** in the Zitter menu or go to **Preferences → Downloads**
and select **Check for updates**. The result appears in place. Preferences
also let you control automatic checks and downloads independently.

Automatic checks are on by default. They run 30 seconds after startup and
every six hours while the browser is running. Zitter notifies you when a newer version is available.
Automatic downloads are on by default for installed Windows x64 copies and
off by default on macOS. With automatic downloads off, Windows offers a
**Download update** action. macOS links to the release page. Installation is
always a separate action.

Portable Windows copies and executables outside their registered installation
cannot use the updater. Launching with `--disable-zitter-updates` or running an
application benchmark disables update checks for that run.

The updater accepts only a strictly newer three-component version and the exact
`Zitter-vVERSION-windows-x64-setup.exe` asset from
`doctorspider42/zitter`. Drafts, prereleases, missing Windows installers and
assets without GitHub's SHA-256 digest are ignored. HTTPS requests use the system
proxy and certificate validation; HTTPS-to-HTTP redirects are refused. Downloads
are bounded to the advertised size, with a 600 MiB upper limit, and their size
and SHA-256 must match before they become installable. Partial downloads are
never executed. Network errors leave the running browser untouched.

The installer is cached in the current user-data directory's `Updates` folder.
A notification offers an Install action. A persistent indicator on the Zitter
menu and a versioned Install entry remain available after the notification
expires. Ordinary shutdown does not install a downloaded update.

Clicking Install requests normal browser shutdown, including download and
unsaved-form checks. After shutdown completes, a separate helper waits for the
browser to exit, checks the cached installer again, and runs setup against the
same registered installation. Setup waits for the helper to exit and refuses to
overwrite a running browser. An update preserves existing shortcuts and browsing
data. After setup succeeds, it starts the updated browser. If shutdown is cancelled,
the update stays available; it is installed when the requested shutdown finally
completes. If the browser is killed or another instance prevents installation,
the next successful check can reuse the verified cached installer and offer it
again.

An installation for the current user updates without elevation. An all-users
installation requires the normal Windows UAC prompt; declining it leaves the
installed version in place. Builds currently have no Authenticode certificate.
The digest verifies the downloaded GitHub artifact; it does not constitute an
Authenticode signature.

Versions before 0.3.0 have no updater and need one manual installer update.

## macOS updates

Zitter selects the newest recent stable release with a disk image for the
running architecture, regardless of which release GitHub labels Latest. With automatic downloads enabled, it verifies the disk
image against GitHub’s size and SHA-256 digest before making it available in the
user-data directory’s `Updates` folder. **Show installer** reveals the downloaded
disk image in Finder for manual installation.

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

## Manual installation while Zitter is running

Interactive setup asks windows belonging to the selected installation to close
normally and waits up to 30 seconds for its processes to exit before replacing
files. This lets the browser save its session and display any unsaved-form
confirmation. If closing is cancelled or the browser does not exit, setup stops
without replacing files and asks the user to close Zitter and retry. Other
installation directories are left running. Unattended (`--quiet`) updates never
close a browser that has been reopened while the update was being prepared.

Each archive entry is extracted to a temporary file and checked before replacing
the installed file. If Windows still locks an unchanged font, setup verifies
that its complete contents match and leaves it in place. A different locked
file fails the installation instead of being silently skipped.

After an all-users installation, setup asks the desktop Explorer process to
launch Zitter as the signed-in desktop user, rather than as the administrator
who approved the UAC prompt.

Build and run `//zitter:setup_runtime_tests` on Windows to exercise graceful
shutdown, a blocked close, installation-directory isolation, and an actual
Explorer COM launch. The test uses temporary copies of its own executable and
does not close installed browsers or modify browser profiles.
