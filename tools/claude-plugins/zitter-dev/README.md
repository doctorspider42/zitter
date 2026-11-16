# zitter-dev: a Claude Code mod for Zitter development

`zitter-dev` gives a Claude Code session in a Zitter checkout one-click dev
builds, a live build panel and hard guards for the repository's rules. It
runs on macOS and Windows and stays off in any other repository.

## Install

You need Claude Code, Python 3 on `PATH` and a Zitter checkout with its
Chromium tree set up (see the repository README).

At the prompt of a Claude Code terminal session:

```
/plugin install zitter-dev --marketplace doctorspider42/zitter
```

Answer `y` to add the marketplace, then pick the **user** scope so the mod
loads in every session. From a shell instead:

```sh
claude plugin install zitter-dev --marketplace doctorspider42/zitter
```

In the Claude desktop app, where `/plugin` does not run: click **+** next to
the prompt box, then **Plugins** › **Add marketplace** with
`doctorspider42/zitter`, then **Add plugin** › `zitter-dev`. A mod installed
from a terminal at the user scope also loads in the desktop app's sessions.

To update later: `/plugin marketplace update zitter`, then reinstall.

## What it does

**Panel.** The Zitter panel opens by itself the first time a session in a
Zitter checkout shows up in the app (`/zitter` opens it again). It shows the
branch, whether the dev app is up to date, the running job with ninja's
progress, an ETA and the current edge, the queue, recent jobs with build
times, the shared build directory, free disk space and the guards. A failed
job lists its error lines and offers **Ask Claude to fix** and **Retry**.
Minimized, the panel becomes a one-line widget; a band above the prompt
carries the same state.

**Jobs.** Each job runs detached in its own process group, survives a reload
of the mod and waits in a queue while any session holds the shared build
directory.

| Command | Job |
| --- | --- |
| `/dev` | stage `//zitter` and build the app |
| `/dev-run` | build, then relaunch the app with its isolated profile |
| `/dev-launch` | relaunch the last build |
| `/dev-full` | app, `zitter_prototype` and Rust test binaries (`verify-ui` build) |
| `/dev-test` | build and run the Rust and Python tests |
| `/dev-uitest <filter>` | browser tests one `--gtest_filter` names (25 at most) |
| `/dev-content [run]` | the Content lab (macOS) |
| `/dev-stop`, `/dev-log [n]` | stop the running job and clear the queue; the end of the last log |
| `/dev-allow …` | let the person approve, for 30 minutes, what a guard refuses |

| | macOS | Windows |
| --- | --- | --- |
| Build | `./build.sh` → `build/ZitterRustMetal.app` | overlay + `autoninja -C out/ZitterWin zitter:zitter` |
| Run | `verify-ui/scripts/ui.py launch` | `verify-ui/scripts/ui_win.py launch` |
| App + tests | `verify-ui/scripts/build_app.sh` | `verify-ui/scripts/build_app_win.ps1` |
| Job logs | `~/Library/Caches/zitter-dev/jobs` | `%LOCALAPPDATA%\zitter-dev\jobs` |

**Tools for Claude.** `zitter_dev_start` and `zitter_dev_wait` let Claude run
the same jobs and read their error lines instead of running the build in a
shell, where a long build times out.

**Guards.** Bash commands that break the repository's rules are refused with
the reason: staging into the frozen `zitter_ui_lab`, Homebrew's `ninja` on
macOS, the full browser UI suite without the person's OK, a new
`.chromium/src/out/*` directory, recursive deletes of output directories or
of `.chromium`, `robocopy /MIR`, a second build on a busy build directory,
and direct `build.sh` runs. The person lifts one for 30 minutes with
`/dev-allow full-ui`, `/dev-allow outdir <name>` or
`/dev-allow delete-out <name>`.

## Developing the mod

```sh
claude --plugin-dir tools/claude-plugins/zitter-dev
claude plugin validate tools/claude-plugins/zitter-dev
claude plugin test tools/claude-plugins/zitter-dev
```

`hooks/register.tsx` holds the hooks and the drawing, `hooks/guards.ts` the
guard rules, `hooks/jobs.ts` the job scripts per platform, `hooks/draw.ts` the
panel's SVG and numbers, and `helper/zjob.py` everything that touches
processes and files on the host.

The panel draws with what the engine's elements take today: Text takes bold,
colours and wrap but no size, weight or monospace, so headings and monospace
go through `Markdown` and `Code`, and spacing is in cells. SVG pieces (the
ring, badges, the build-time bars) use mid-tone colours that read on both the
light and the dark theme.
