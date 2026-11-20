# Zitter Dev for Codex

A local Codex plugin for Content builds on macOS and Windows. The **Zitter**
tab shows Ninja progress, an estimated time remaining, queued jobs, recent jobs
and logs. It can build, build and relaunch, launch the last build, run the
Python test suite, stop a job and retry failed jobs.

## Install

Install Python 3.10 or later and make sure the `python3` command is available
to Codex. Use a local Codex chat in a Zitter checkout.

From the repository root, run:

```sh
codex plugin marketplace add .
codex plugin add zitter-dev@zitter-tools
```

In the desktop app, you can also install **Zitter Dev** from the **Zitter**
source in the Plugins Directory. Open **Zitter** from the sidebar, from
the tabs in a fresh local Codex chat, or via the
[desktop link](codex://plugins/zitter-dev@zitter-tools/app/zitter_dev_panel).
The panel starts with the active checkout when Codex provides it. Otherwise
it uses your saved choice or the local marketplace's source checkout. To
select a worktree explicitly, use **Change checkout** and enter its absolute
path. Check the path and branch above the build buttons.

After updating the plugin, quit and reopen the desktop app, then open a fresh
local chat. A running desktop process can keep looking for the previous
version's files, and a chat opened before installation can lack the
`zitter-dev` server.

## Use

The panel and agent use the same local queue. From chat, ask Codex to use
`zitter_dev_start` with the checkout's absolute path, then check the job with
`zitter_dev_status` or `zitter_dev_log`. The current job can continue after the
panel closes. **Ask Codex to fix** includes the checkout, job ID and log path.
When the host supports editable drafts, the button fills the composer for
review. On Windows, keep the Codex session open during builds because Codex
can terminate its MCP process tree when the session closes.

Builds require an existing `.chromium/src/out/ZitterContentBundle` on macOS or
`.chromium/src/out/ZitterWinContent` on Windows, both with `args.gn`. See
[Remote builds](../../../docs/remote-builds.md). The Codex and updated Claude
mods serialize builds across checkouts that share the same Chromium source
tree. The lock covers staging as well as compilation.

The bundled `PreToolUse` hook recognizes common direct build commands,
destructive Chromium operations, output directory creation and bare Ninja on
macOS. Review and trust the hook in Codex to enable it. It applies to matching
Codex shell calls in Zitter checkouts.
