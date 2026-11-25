# Logs and crash reports

Zitter keeps the output of its recent runs and a report for each crash in one
standard directory:

| System  | Directory                         |
| ------- | --------------------------------- |
| macOS   | `~/Library/Logs/Zitter`           |
| Windows | `%LOCALAPPDATA%\Zitter\Logs`      |

Nothing leaves the computer. Zitter has no crash uploader.

## What is written

- **`zitter-<date>-<time>-<pid>.log`**: one file per run. It holds stdout and
  stderr of the browser process and of every child process (renderers, GPU,
  utilities). That covers failed `CHECK`/`DCHECK` messages and the in-process
  stack dumps that non-official builds print on a crash. The first line marks
  the start; a clean quit appends `[zitter] clean exit <code>`, and a
  `SIGTERM` (a relaunch from the dev tools, `kill`) appends
  `[zitter] ended by SIGTERM`.
- **`Crashes/`**: logs of runs that crashed, together with what the system
  recorded for them:
  - macOS: copies of the `Zitter*.ips` reports from
    `~/Library/Logs/DiagnosticReports` made during that run, named
    `<run log name without .log>-<report name>`.
  - Windows: minidumps written by Windows Error Reporting. On each start
    Zitter registers its executable under
    `HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps`
    with `DumpFolder` set to `Crashes`, `DumpType` 1 (minidump) and
    `DumpCount` 5.

A run counts as crashed when its log lacks both end lines, or when its
last 64 KB contain `:FATAL:`, `Check failed` or `Received signal`, which a
crashing child process prints as well. Zitter checks this on the next start,
for logs whose process no longer runs.

The run log is opened only when nothing else collects the app's output: a
start from Finder, the Dock or Explorer, where stderr goes to `/dev/null` or
nowhere. A terminal, `./build.sh --run` and the `verify_content_*` checks keep
their own output. The zitter-dev mod's Build & run discards the output, so its
runs log to this directory too.

## Limits

The directory cleans itself on every start (`crash_logs.cc`):

| Limit                                   | Value  |
| --------------------------------------- | ------ |
| Run logs kept                           | 10 newest |
| An ended run log larger than 16 MB      | cut to its last 4 MB |
| A running log larger than 64 MB         | emptied and restarted (checked every minute) |
| Crash reports kept                      | 20 newest runs |
| All of `Crashes/`                       | 100 MB, oldest deleted first; the newest is always kept |
| Age of a crash report                   | 30 days |

Together these keep the directory under about 400 MB in the worst case, and
usually under a few megabytes.
