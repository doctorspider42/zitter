// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_INSTALLER_SETUP_INSTALL_H_
#define ZITTER_CHROMIUM_INSTALLER_SETUP_INSTALL_H_

#include "chromium/installer/setup_runtime.h"

#include <functional>
#include <optional>
#include <string>

namespace zitter::setup {

// Where Zitter goes: just for this user (HKCU, %LOCALAPPDATA%\Programs) or for
// everyone (HKLM, Program Files; needs elevation).
struct Placement {
  bool system = false;
  std::wstring directory;
};

struct Existing {
  Placement placement;
  std::wstring version;
};

std::wstring DefaultDirectory(bool system);
// An installation registered in Apps & features, the user's first.
std::optional<Existing> FindExisting();
// The version of the zitter.exe this installer carries ("0.1.0").
std::wstring PayloadVersion();
bool IsElevated();

// |progress| gets 0..1; both return an empty string or the reason they failed.
std::wstring Install(const Placement& placement, bool desktop_shortcut,
                     const std::function<void(double)>& progress,
                     bool preserve_shortcuts = false,
                     bool update_only = false,
                     bool close_running_browser = true);
std::wstring Uninstall(const Placement& placement, bool remove_data,
                       const std::function<void(double)>& progress);

// After uninstalling: deletes the running copy of the installer once it exits,
// and |directory| with it when it ran from there.
void DeleteSelfLater(const std::wstring& directory);

}  // namespace zitter::setup

#endif
