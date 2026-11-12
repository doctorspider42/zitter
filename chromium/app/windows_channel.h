// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_APP_WINDOWS_CHANNEL_H_
#define ZITTER_CHROMIUM_APP_WINDOWS_CHANNEL_H_

namespace zitter::windows {

constexpr wchar_t kStableAppId[] = L"Zitter.Browser";
constexpr wchar_t kDevAppId[] = L"Zitter.Dev.Browser";

#if defined(ZITTER_DEV_BUILD)
constexpr bool kDevBuild = true;
constexpr wchar_t kProductName[] = L"Zitter Dev";
#else
constexpr bool kDevBuild = false;
constexpr wchar_t kProductName[] = L"Zitter";
#endif

constexpr const wchar_t* kAppId = kDevBuild ? kDevAppId : kStableAppId;
constexpr char kDevChannelSwitch[] = "zitter-dev-channel";
constexpr char kUserDataDirSwitch[] = "user-data-dir";
constexpr char kCleanProfileSwitch[] = "zitter-dev-clean-profile";
constexpr wchar_t kCleanProfileArgument[] = L"--zitter-dev-clean-profile";

}  // namespace zitter::windows

#endif
