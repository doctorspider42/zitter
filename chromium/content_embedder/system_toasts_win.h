// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Windows toasts for site and extension notifications. The in-app
// notification center stays the source of truth; a toast mirrors one entry in
// the Windows notification center so the person sees it while Zitter is in
// the background, minimized or showing another profile.
#ifndef ZITTER_CONTENT_SYSTEM_TOASTS_WIN_H_
#define ZITTER_CONTENT_SYSTEM_TOASTS_WIN_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"

namespace zitter::content_embedder {

// When Zitter mirrors a notification as a Windows toast (Settings →
// Appearance → Windows notifications). Stored as an int in Settings.json.
enum class SystemNotificationMode {
  kNever = 0,         // Only the in-app banner and the Notifications panel.
  kInBackground = 1,  // A toast when the person can't see the in-app banner.
  kAlways = 2,        // A toast for every notification.
};
inline constexpr int kDefaultSystemNotificationMode = 1;

// Whether Windows lets Zitter show toasts, as `IToastNotifier::get_Setting`
// reports it, plus the cases where no notifier could be made at all.
enum class SystemToastState {
  kUnknown,
  kEnabled,
  kDisabledForApplication,  // Settings → System → Notifications → Zitter.
  kDisabledForUser,         // Notifications are off for the whole account.
  kDisabledByGroupPolicy,
  kDisabledByManifest,
  kUnavailable,  // No WinRT notifier (old Windows, broken registration).
};

struct SystemToast {
  std::string id;
  std::u16string title;
  std::u16string body;
  std::u16string attribution;  // The site or extension that sent it.
  std::vector<std::u16string> actions;  // At most two buttons.
  base::RepeatingClosure click;
  base::RepeatingCallback<void(size_t)> action;
  // The person closed the toast (not a timeout or a removal by Zitter).
  base::RepeatingClosure dismissed;
};

// Shows a toast. `shown` reports, on the UI thread, whether Windows accepted
// it and the notifier state at that moment.
void ShowSystemToast(SystemToast toast,
                     base::OnceCallback<void(bool, SystemToastState)> shown);
// Removes a toast from the screen and from the Windows notification center.
void CloseSystemToast(const std::string& id);
// Removes every toast Zitter showed in this session (at shutdown, so a stale
// toast can't outlive the page that would handle its click).
void CloseAllSystemToasts();
// Removes toasts an earlier run of Zitter left in the notification center.
void ClearPreviousSystemToasts();
// Registers Zitter's AppUserModelID with a display name and icon under
// HKCU\Software\Classes\AppUserModelId when the Start menu shortcut is
// missing (Dev builds, portable copies). Without that, Windows drops toasts
// from an unpackaged app without an error.
void EnsureSystemToastRegistration();
std::string_view SystemToastStateName(SystemToastState state);
// Whether a window of this process has the keyboard focus.
bool ZitterIsForeground();
// Settings → System → Notifications.
void OpenWindowsNotificationSettings();

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_SYSTEM_TOASTS_WIN_H_
