// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_INSTALLER_SETUP_RUNTIME_H_
#define ZITTER_CHROMIUM_INSTALLER_SETUP_RUNTIME_H_

#include <windows.h>
#include <string>

namespace zitter::setup {

bool IsRunning(const std::wstring& directory);
// Send WM_CLOSE to this installation's windows and wait for all its processes.
// A cancelled beforeunload dialog or a hung browser must prevent replacement.
bool CloseBrowser(const std::wstring& directory, DWORD timeout_ms = 30000);
// Commit a complete extracted file. Unchanged files can remain in place when
// Windows still holds a font mapping after the browser has exited.
DWORD CommitInstalledFile(const std::wstring& temporary, const std::wstring& target);
// COM must be initialized on the calling thread. Explorer launches as the
// desktop user, even when setup was elevated using another account's password.
HRESULT LaunchFromExplorer(const std::wstring& executable,
                        const std::wstring& directory,
                        const std::wstring& arguments = L"");

}  // namespace zitter::setup
#endif
