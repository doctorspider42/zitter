// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Windows integration of installed web apps: the launch switches, the
// per-app AppUserModelID and the Start menu shortcut.

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/i18n/file_util_icu.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/win/shortcut.h"
#include "chromium/app/windows_channel.h"
#include "chromium/prototype/jump_list_win.h"
#include "crypto/sha2.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/win/icon_util.h"
#include "web_apps.h"

namespace zitter::content_embedder {

namespace {

constexpr size_t kMaxShortcutName = 80;

// Workspace profile ids are directory names: letters, digits, '-' and '_'.
bool IsProfileId(std::string_view id) {
  return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_';
  });
}

}  // namespace

void TranslateWebAppLaunch(base::CommandLine& command) {
  if (!command.HasSwitch(kWebAppSwitch) && !command.HasSwitch(kWebAppProfileSwitch))
    return;
  const std::string app = command.GetSwitchValueASCII(kWebAppSwitch);
  const std::string profile = command.GetSwitchValueASCII(kWebAppProfileSwitch);
  command.RemoveSwitch(kWebAppSwitch);
  command.RemoveSwitch(kWebAppProfileSwitch);
  if (!IsWebAppId(app) || !IsProfileId(profile))
    return;
  command.RemoveSwitch(prototype::kZitterCommandSwitch);
  command.AppendSwitchASCII(prototype::kZitterCommandSwitch,
                            std::string(kWebAppCommandPrefix) + profile + "/" + app);
}

std::wstring WebAppUserModelId(std::string_view profile_id, std::string_view app_id) {
  const std::string hash = base::HexEncode(crypto::SHA256HashString(
      std::string(profile_id) + "/" + std::string(app_id)));
  return std::wstring(windows::kAppId) + L".App." + base::ASCIIToWide(hash.substr(0, 24));
}

base::CommandLine WebAppLaunchCommand(std::string_view profile_id, std::string_view app_id) {
  base::FilePath exe;
  base::PathService::Get(base::FILE_EXE, &exe);
  base::CommandLine command(exe);
  command.AppendSwitchASCII(kWebAppSwitch, app_id);
  command.AppendSwitchASCII(kWebAppProfileSwitch, profile_id);
  return command;
}

base::FilePath CreateWebAppShortcut(const std::string& profile_id,
                                    const std::string& app_id,
                                    const std::u16string& name,
                                    const SkBitmap& icon,
                                    const base::FilePath& icon_file) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  base::FilePath programs;
  base::FilePath exe;
  if (!base::PathService::Get(base::DIR_START_MENU, &programs) ||
      !base::PathService::Get(base::FILE_EXE, &exe)) {
    return {};
  }
  bool has_icon = false;
  if (!icon.drawsNothing() && base::CreateDirectory(icon_file.DirName())) {
    gfx::ImageFamily family;
    family.Add(gfx::Image::CreateFrom1xBitmap(icon));
    has_icon = IconUtil::CreateIconFileFromImageFamily(family, icon_file);
  }
  const auto folder =
      programs.Append(std::wstring(windows::kProductName) + L" Apps");
  if (!base::CreateDirectory(folder))
    return {};
  std::wstring file = base::UTF16ToWide(name).substr(0, kMaxShortcutName);
  base::i18n::ReplaceIllegalCharactersInPath(&file, '_');
  file = std::wstring(base::TrimString(base::TrimWhitespace(file, base::TRIM_ALL), L".",
                                       base::TRIM_ALL));
  if (file.empty())
    file = L"App";
  base::FilePath shortcut;
  for (int copy = 1; copy <= 20; ++copy) {
    const std::wstring suffix = copy == 1 ? L"" : L" (" + base::NumberToWString(copy) + L")";
    const auto candidate = folder.Append(file + suffix + L".lnk");
    if (!base::PathExists(candidate)) {
      shortcut = candidate;
      break;
    }
  }
  if (shortcut.empty())
    return {};

  const auto launch = WebAppLaunchCommand(profile_id, app_id);
  base::win::ShortcutProperties properties;
  properties.set_target(exe);
  properties.set_working_dir(exe.DirName());
  properties.set_arguments(launch.GetArgumentsString());
  properties.set_description(base::UTF16ToWide(name).substr(0, 200));
  properties.set_icon(has_icon ? icon_file : exe, 0);
  properties.set_app_id(WebAppUserModelId(profile_id, app_id));
  if (!base::win::CreateOrUpdateShortcutLink(shortcut, properties,
                                             base::win::ShortcutOperation::kCreateAlways)) {
    LOG(ERROR) << "Could not create the Start menu shortcut of a web app";
    return {};
  }
  ::SHChangeNotify(SHCNE_CREATE, SHCNF_PATH | SHCNF_FLUSHNOWAIT, shortcut.value().c_str(),
                   nullptr);
  return shortcut;
}

void DeleteWebAppShortcut(const base::FilePath& shortcut, const base::FilePath& icon_file) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  if (!shortcut.empty() && shortcut.Extension() == FILE_PATH_LITERAL(".lnk")) {
    base::FilePath programs;
    // Only shortcuts in Zitter's own Start menu folder.
    if (base::PathService::Get(base::DIR_START_MENU, &programs) &&
        programs.IsParent(shortcut) && base::DeleteFile(shortcut)) {
      ::SHChangeNotify(SHCNE_DELETE, SHCNF_PATH | SHCNF_FLUSHNOWAIT,
                       shortcut.value().c_str(), nullptr);
      const auto folder = shortcut.DirName();
      if (folder != programs && base::IsDirectoryEmpty(folder))
        base::DeleteFile(folder);
    }
  }
  if (!icon_file.empty())
    base::DeleteFile(icon_file);
}

}  // namespace zitter::content_embedder
