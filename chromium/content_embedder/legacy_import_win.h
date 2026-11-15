// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_LEGACY_IMPORT_WIN_H_
#define ZITTER_CONTENT_LEGACY_IMPORT_WIN_H_

namespace base {
class FilePath;
}

namespace zitter::content_embedder {

// Imports the Chrome-based Zitter data in `legacy_root` (the directory with
// ZitterWorkspaces.json, normally %LOCALAPPDATA%\Zitter) into the empty
// Content Workspace directory `destination` using the windows-dpapi layout.
// Returns false and leaves `destination` untouched (no partial writes; stage
// into a sibling temporary directory and rename at the end, as the Python
// does) when the source is missing, invalid or unsafe.
//
// This is the native port of import_workspace_profiles() in
// tools/profile_migration_trial.py for the windows-dpapi layout. It reads
// `legacy_root` directly instead of a verified trial snapshot, so the legacy
// browser must not be running. `destination` must be absent or an empty
// directory, and must not be inside `legacy_root` or another legacy data
// directory. Performs blocking file and SQLite I/O.
bool ImportLegacyWindowsWorkspace(const base::FilePath& legacy_root,
                                  const base::FilePath& destination);

// True when `legacy_root` looks like a Chrome-based Zitter data directory.
bool HasLegacyWindowsWorkspace(const base::FilePath& legacy_root);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_LEGACY_IMPORT_WIN_H_
