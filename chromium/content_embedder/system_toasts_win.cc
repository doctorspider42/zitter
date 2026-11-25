// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The WinRT calls follow Chromium's BSD-licensed
// notification_platform_bridge_win.cc. Unlike Chrome, Zitter has no COM
// activator: clicks reach the running process through the toast's Activated
// event, and Zitter removes its toasts when it quits.

#include "system_toasts_win.h"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <windows.data.xml.dom.h>
#include <windows.foundation.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/sequence_bound.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/win/core_winrt_util.h"
#include "base/win/registry.h"
#include "base/win/scoped_hstring.h"
#include "chromium/app/windows_channel.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/win/icon_util.h"

namespace zitter::content_embedder {
namespace {

namespace mswr = Microsoft::WRL;
namespace winfoundtn = ABI::Windows::Foundation;
namespace winui = ABI::Windows::UI::Notifications;
namespace winxml = ABI::Windows::Data::Xml::Dom;
using base::win::ScopedHString;

using ActivatedHandler =
    winfoundtn::ITypedEventHandler<winui::ToastNotification*, IInspectable*>;
using DismissedHandler =
    winfoundtn::ITypedEventHandler<winui::ToastNotification*, winui::ToastDismissedEventArgs*>;
using FailedHandler =
    winfoundtn::ITypedEventHandler<winui::ToastNotification*, winui::ToastFailedEventArgs*>;

constexpr wchar_t kClickArgument[] = L"zitter-click";
constexpr wchar_t kActionArgument[] = L"zitter-action-";

template <unsigned int size>
HRESULT GetFactory(wchar_t const (&name)[size], const IID& iid, void** factory) {
  auto class_name = ScopedHString::Create(std::wstring_view(name, size - 1));
  return base::win::RoGetActivationFactory(class_name.get(), iid, factory);
}

std::wstring EscapeXml(std::u16string_view text) {
  std::wstring escaped;
  escaped.reserve(text.size());
  for (const char16_t c : text) {
    switch (c) {
      case u'&': escaped += L"&amp;"; break;
      case u'<': escaped += L"&lt;"; break;
      case u'>': escaped += L"&gt;"; break;
      case u'"': escaped += L"&quot;"; break;
      case u'\'': escaped += L"&apos;"; break;
      default:
        // XML 1.0 forbids most control characters; LoadXml rejects the toast.
        if (c >= 0x20 || c == u'\t' || c == u'\n' || c == u'\r')
          escaped += static_cast<wchar_t>(c);
    }
  }
  return escaped;
}

std::wstring ToastXml(const SystemToast& toast) {
  std::wstring xml = std::wstring(L"<toast launch=\"") + kClickArgument + L"\">";
  xml += L"<visual><binding template=\"ToastGeneric\">";
  xml += L"<text>" + EscapeXml(toast.title.empty() ? toast.attribution : toast.title) + L"</text>";
  if (!toast.body.empty())
    xml += L"<text>" + EscapeXml(toast.body) + L"</text>";
  if (!toast.attribution.empty())
    xml += L"<text placement=\"attribution\">" + EscapeXml(toast.attribution) + L"</text>";
  xml += L"</binding></visual>";
  if (!toast.actions.empty()) {
    xml += L"<actions>";
    for (size_t i = 0; i < toast.actions.size() && i < 2; ++i) {
      xml += L"<action content=\"" + EscapeXml(toast.actions[i]) + L"\" arguments=\"" +
             kActionArgument + base::NumberToWString(i) + L"\"/>";
    }
    xml += L"</actions>";
  }
  return xml + L"</toast>";
}

SystemToastState StateFromSetting(winui::NotificationSetting setting) {
  switch (setting) {
    case winui::NotificationSetting_Enabled:
      return SystemToastState::kEnabled;
    case winui::NotificationSetting_DisabledForApplication:
      return SystemToastState::kDisabledForApplication;
    case winui::NotificationSetting_DisabledForUser:
      return SystemToastState::kDisabledForUser;
    case winui::NotificationSetting_DisabledByGroupPolicy:
      return SystemToastState::kDisabledByGroupPolicy;
    case winui::NotificationSetting_DisabledByManifest:
      return SystemToastState::kDisabledByManifest;
  }
  return SystemToastState::kUnknown;
}

enum class Event { kClick, kAction, kDismissed };

// Lives on a COM STA thread; every WinRT object stays there. Toast events
// arrive on Windows' threads and hop back to the UI thread through `events_`.
class ToastWorker {
 public:
  using EventCallback = base::RepeatingCallback<void(std::string, Event, size_t)>;

  explicit ToastWorker(EventCallback events) : events_(std::move(events)) {}
  ~ToastWorker() { CloseAll(); }

  std::pair<bool, SystemToastState> Show(const std::string& id, std::wstring xml) {
    if (!EnsureNotifier())
      return {false, SystemToastState::kUnavailable};
    winui::NotificationSetting setting = winui::NotificationSetting_Enabled;
    const auto state = SUCCEEDED(notifier_->get_Setting(&setting))
                           ? StateFromSetting(setting) : SystemToastState::kUnknown;
    if (state != SystemToastState::kEnabled && state != SystemToastState::kUnknown)
      return {false, state};

    Close(id);
    mswr::ComPtr<IInspectable> inspectable;
    auto document_class = ScopedHString::Create(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument);
    if (FAILED(base::win::RoActivateInstance(document_class.get(), &inspectable)))
      return {false, state};
    mswr::ComPtr<winxml::IXmlDocumentIO> io;
    mswr::ComPtr<winxml::IXmlDocument> document;
    auto text = ScopedHString::Create(xml);
    if (FAILED(inspectable.As(&io)) || FAILED(io->LoadXml(text.get())) ||
        FAILED(inspectable.As(&document))) {
      LOG(WARNING) << "Zitter could not build a Windows toast";
      return {false, state};
    }
    mswr::ComPtr<winui::IToastNotificationFactory> factory;
    mswr::ComPtr<winui::IToastNotification> toast;
    if (FAILED(GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotification,
                          IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateToastNotification(document.Get(), &toast))) {
      return {false, state};
    }

    Entry entry{toast};
    const auto events = events_;
    toast->add_Activated(
        mswr::Callback<ActivatedHandler>(
            [events, id](winui::IToastNotification*, IInspectable* args) {
              std::wstring arguments;
              mswr::ComPtr<IInspectable> inspectable(args);
              mswr::ComPtr<winui::IToastActivatedEventArgs> activated;
              HSTRING value = nullptr;
              if (inspectable && SUCCEEDED(inspectable.As(&activated)) &&
                  SUCCEEDED(activated->get_Arguments(&value))) {
                arguments = ScopedHString(value).Get();
              }
              size_t index = 0;
              if (base::StartsWith(arguments, kActionArgument) &&
                  base::StringToSizeT(std::wstring_view(arguments).substr(
                                          std::size(kActionArgument) - 1), &index)) {
                events.Run(id, Event::kAction, index);
              } else {
                events.Run(id, Event::kClick, 0);
              }
              return S_OK;
            }).Get(),
        &entry.activated);
    toast->add_Dismissed(
        mswr::Callback<DismissedHandler>(
            [events, id](winui::IToastNotification*, winui::IToastDismissedEventArgs* args) {
              winui::ToastDismissalReason reason = winui::ToastDismissalReason_TimedOut;
              // A timeout only moves the toast into the notification center.
              if (args && SUCCEEDED(args->get_Reason(&reason)) &&
                  reason == winui::ToastDismissalReason_UserCanceled) {
                events.Run(id, Event::kDismissed, 0);
              }
              return S_OK;
            }).Get(),
        &entry.dismissed);
    toast->add_Failed(
        mswr::Callback<FailedHandler>(
            [](winui::IToastNotification*, winui::IToastFailedEventArgs* args) {
              HRESULT error = S_OK;
              if (args)
                args->get_ErrorCode(&error);
              LOG(WARNING) << "Windows failed to show a Zitter toast: 0x" << std::hex << error;
              return S_OK;
            }).Get(),
        &entry.failed);
    if (FAILED(notifier_->Show(toast.Get()))) {
      LOG(WARNING) << "Windows refused a Zitter toast";
      return {false, state};
    }
    toasts_.insert_or_assign(id, std::move(entry));
    return {true, state};
  }

  void Close(const std::string& id) {
    const auto entry = toasts_.find(id);
    if (entry == toasts_.end())
      return;
    auto& toast = entry->second.toast;
    toast->remove_Activated(entry->second.activated);
    toast->remove_Dismissed(entry->second.dismissed);
    toast->remove_Failed(entry->second.failed);
    // Hide() also removes the toast from the notification center.
    if (notifier_)
      notifier_->Hide(toast.Get());
    toasts_.erase(entry);
  }

  void CloseAll() {
    while (!toasts_.empty())
      Close(toasts_.begin()->first);
  }

  // Toasts a previous run left behind (a crash, a forced quit) have no page
  // to handle their click any more.
  void ClearHistory() {
    mswr::ComPtr<winui::IToastNotificationManagerStatics2> manager;
    mswr::ComPtr<winui::IToastNotificationHistory> history;
    if (FAILED(GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                          IID_PPV_ARGS(&manager))) ||
        FAILED(manager->get_History(&history))) {
      return;
    }
    auto app_id = ScopedHString::Create(windows::kAppId);
    history->ClearWithId(app_id.get());
  }

 private:
  struct Entry {
    mswr::ComPtr<winui::IToastNotification> toast;
    EventRegistrationToken activated{};
    EventRegistrationToken dismissed{};
    EventRegistrationToken failed{};
  };

  bool EnsureNotifier() {
    if (notifier_)
      return true;
    mswr::ComPtr<winui::IToastNotificationManagerStatics> manager;
    if (FAILED(GetFactory(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                          IID_PPV_ARGS(&manager)))) {
      return false;
    }
    auto app_id = ScopedHString::Create(windows::kAppId);
    return SUCCEEDED(manager->CreateToastNotifierWithId(app_id.get(), &notifier_));
  }

  const EventCallback events_;
  mswr::ComPtr<winui::IToastNotifier> notifier_;
  std::map<std::string, Entry> toasts_;
};

// The UI-thread side: callbacks per toast and the worker.
class Toasts {
 public:
  static Toasts& Get() {
    static base::NoDestructor<Toasts> toasts;
    return *toasts;
  }

  Toasts() {
    worker_ = base::SequenceBound<ToastWorker>(
        base::ThreadPool::CreateCOMSTATaskRunner(
            {base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
            base::SingleThreadTaskRunnerThreadMode::DEDICATED),
        base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                           base::BindRepeating(&Toasts::OnEvent, weak_factory_.GetWeakPtr())));
  }

  void Show(SystemToast toast, base::OnceCallback<void(bool, SystemToastState)> shown) {
    auto xml = ToastXml(toast);
    const std::string id = toast.id;
    callbacks_.insert_or_assign(id, std::move(toast));
    worker_.AsyncCall(&ToastWorker::Show)
        .WithArgs(id, std::move(xml))
        .Then(base::BindOnce(
            [](base::WeakPtr<Toasts> self, std::string id,
               base::OnceCallback<void(bool, SystemToastState)> shown,
               std::pair<bool, SystemToastState> result) {
              if (self && !result.first)
                self->callbacks_.erase(id);
              if (shown)
                std::move(shown).Run(result.first, result.second);
            }, weak_factory_.GetWeakPtr(), id, std::move(shown)));
  }

  void Close(const std::string& id) {
    if (callbacks_.erase(id))
      worker_.AsyncCall(&ToastWorker::Close).WithArgs(id);
  }

  void CloseAll() {
    callbacks_.clear();
    worker_.AsyncCall(&ToastWorker::CloseAll);
  }

  void ClearHistory() { worker_.AsyncCall(&ToastWorker::ClearHistory); }

 private:
  void OnEvent(std::string id, Event event, size_t index) {
    const auto entry = callbacks_.find(id);
    if (entry == callbacks_.end())
      return;
    // Copy the callback: running it usually closes this toast.
    switch (event) {
      case Event::kClick:
        if (auto click = entry->second.click)
          click.Run();
        break;
      case Event::kAction:
        if (auto action = entry->second.action)
          action.Run(index);
        break;
      case Event::kDismissed:
        if (auto dismissed = entry->second.dismissed)
          dismissed.Run();
        break;
    }
    Close(id);
  }

  std::map<std::string, SystemToast> callbacks_;
  base::SequenceBound<ToastWorker> worker_;
  base::WeakPtrFactory<Toasts> weak_factory_{this};
};

bool HasStartMenuShortcut() {
  const std::wstring name = std::wstring(windows::kProductName) + L".lnk";
  for (const int key : {base::DIR_START_MENU, base::DIR_COMMON_START_MENU}) {
    base::FilePath folder;
    if (base::PathService::Get(key, &folder) && base::PathExists(folder.Append(name)))
      return true;
  }
  return false;
}

std::optional<base::FilePath> WriteToastIcon() {
  base::FilePath folder;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &folder))
    return std::nullopt;
  const auto file = folder.Append(windows::kProductName).Append(L"notification-icon.png");
  if (base::PathExists(file))
    return file;
  HICON icon = static_cast<HICON>(::LoadImage(::GetModuleHandle(nullptr), MAKEINTRESOURCE(1),
                                              IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
  if (!icon)
    return std::nullopt;
  const SkBitmap bitmap = IconUtil::CreateSkBitmapFromHICON(icon);
  ::DestroyIcon(icon);
  const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (!png || !base::CreateDirectory(file.DirName()) || !base::WriteFile(file, *png))
    return std::nullopt;
  return file;
}

}  // namespace

void ShowSystemToast(SystemToast toast,
                     base::OnceCallback<void(bool, SystemToastState)> shown) {
  Toasts::Get().Show(std::move(toast), std::move(shown));
}

void CloseSystemToast(const std::string& id) {
  Toasts::Get().Close(id);
}

void CloseAllSystemToasts() {
  Toasts::Get().CloseAll();
}

void ClearPreviousSystemToasts() {
  Toasts::Get().ClearHistory();
}

void EnsureSystemToastRegistration() {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce([] {
        base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
        // The installer's Start menu shortcut carries the AppUserModelID and
        // the icon; a registry entry would only shadow it.
        if (HasStartMenuShortcut())
          return;
        const std::wstring key = std::wstring(L"Software\\Classes\\AppUserModelId\\") +
                                 windows::kAppId;
        base::win::RegKey registration(HKEY_CURRENT_USER, key.c_str(), KEY_READ | KEY_WRITE);
        if (!registration.Valid() &&
            registration.Create(HKEY_CURRENT_USER, key.c_str(), KEY_READ | KEY_WRITE) !=
                ERROR_SUCCESS) {
          return;
        }
        registration.WriteValue(L"DisplayName", windows::kProductName);
        if (const auto icon = WriteToastIcon())
          registration.WriteValue(L"IconUri", icon->value().c_str());
      }));
}

bool ZitterIsForeground() {
  DWORD process = 0;
  ::GetWindowThreadProcessId(::GetForegroundWindow(), &process);
  return process == ::GetCurrentProcessId();
}

void OpenWindowsNotificationSettings() {
  ::ShellExecuteW(nullptr, L"open", L"ms-settings:notifications", nullptr, nullptr,
                  SW_SHOWNORMAL);
}

std::string_view SystemToastStateName(SystemToastState state) {
  switch (state) {
    case SystemToastState::kUnknown: return "unknown";
    case SystemToastState::kEnabled: return "enabled";
    case SystemToastState::kDisabledForApplication: return "disabled-for-application";
    case SystemToastState::kDisabledForUser: return "disabled-for-user";
    case SystemToastState::kDisabledByGroupPolicy: return "disabled-by-group-policy";
    case SystemToastState::kDisabledByManifest: return "disabled-by-manifest";
    case SystemToastState::kUnavailable: return "unavailable";
  }
  return "unknown";
}

}  // namespace zitter::content_embedder
