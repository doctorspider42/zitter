// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_CONTENT_EMBEDDER_CRASH_LOGS_H_
#define ZITTER_CHROMIUM_CONTENT_EMBEDDER_CRASH_LOGS_H_

#include "base/files/file_path.h"

namespace zitter::content_embedder {

// The standard log directory: ~/Library/Logs/Zitter on macOS and
// %LOCALAPPDATA%\Zitter\Logs on Windows. Crash reports go to its Crashes
// subdirectory.
base::FilePath RunLogDirectory();

// Call in the browser process before ContentMain. When nothing collects the
// app's output (a Finder, Dock or Explorer start), sends stdout and stderr of
// the browser and its child processes to a log for this run. It then moves
// the logs of earlier runs that crashed, with the system crash reports of
// that time, to Crashes/ and deletes what exceeds the directory's limits
// (docs/logs-and-crashes.md).
void StartRunLog();

// Marks a SIGTERM ending in the run log. Call in the browser process after
// ContentMain set up its signals (PreBrowserMain).
void WatchRunTermination();

// Marks the run log as ended cleanly. Logs without this mark count as crashes.
void EndRunLog(int exit_code);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CHROMIUM_CONTENT_EMBEDDER_CRASH_LOGS_H_
