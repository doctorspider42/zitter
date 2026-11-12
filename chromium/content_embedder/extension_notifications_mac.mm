// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#import <UserNotifications/UserNotifications.h>

#include <dlfcn.h>

#include "extension_notifications_mac.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_function_registry.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"

@interface ZitterExtensionNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

namespace zitter::content_embedder {
namespace {

constexpr char kDenied[] = "Notifications are disabled for Zitter in macOS.";

UNUserNotificationCenter* GetNotificationCenter() {
  const auto& command = *base::CommandLine::ForCurrentProcess();
  if (command.HasSwitch("zitter-content-extension-notification-delivery-probe")) {
    CHECK(command.HasSwitch("zitter-content-workspace-probe"));
    auto* install = reinterpret_cast<bool (*)()>(
        dlsym(RTLD_DEFAULT, "ZitterInstallNotificationFixture"));
    CHECK(install && install());
  }
  return [UNUserNotificationCenter currentNotificationCenter];
}

const extensions::Extension* FindNotificationExtension(content::BrowserContext* context,
                                                       const std::string& id) {
  if (!context || context->ShutdownStarted() || context->IsOffTheRecord())
    return nullptr;
  const auto* extension = extensions::ExtensionRegistry::Get(context)->enabled_extensions().GetByID(id);
  return extension && extension->permissions_data()->HasAPIPermission(
                          extensions::mojom::APIPermissionID::kNotifications)
             ? extension : nullptr;
}

bool IsAuthorized(UNAuthorizationStatus status) {
  return status == UNAuthorizationStatusAuthorized || status == UNAuthorizationStatusProvisional;
}

struct Notification {
  base::WeakPtr<content::BrowserContext> context;
  std::string extension;
  std::string id;
  base::DictValue options;
};

class Notifications {
 public:
  static Notifications& Get() {
    static base::NoDestructor<Notifications> instance;
    return *instance;
  }

  Notifications()
      : center_(GetNotificationCenter()),
        delegate_([[ZitterExtensionNotificationDelegate alloc] init]) {
    center_.delegate = delegate_;
  }

  auto Find(content::BrowserContext& context, std::string_view extension, std::string_view id) {
    return std::ranges::find_if(entries_, [&](const auto& pair) {
      const auto& entry = pair.second;
      return entry.context.get() == &context && entry.extension == extension && entry.id == id;
    });
  }

  void Remove(content::BrowserContext& context, std::string_view extension) {
    for (auto it = entries_.begin(); it != entries_.end();) {
      const auto& entry = it->second;
      if (entry.context.get() == &context && (extension.empty() || entry.extension == extension))
        it = Erase(it);
      else
        ++it;
    }
    UpdateCategories();
  }

  bool Clear(content::BrowserContext& context, std::string_view extension, std::string_view id) {
    auto it = Find(context, extension, id);
    if (it == entries_.end())
      return false;
    auto entry = std::move(it->second);
    Erase(it);
    UpdateCategories();
    Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLOSED, "notifications.onClosed",
             base::ListValue().Append(entry.id).Append(false));
    return true;
  }

  base::DictValue GetAll(content::BrowserContext& context, std::string_view extension) const {
    base::DictValue result;
    for (const auto& [_, entry] : entries_) {
      if (entry.context.get() == &context && entry.extension == extension)
        result.Set(entry.id, true);
    }
    return result;
  }

  const base::DictValue* Options(content::BrowserContext& context,
                                 std::string_view extension, std::string_view id) {
    auto it = Find(context, extension, id);
    return it == entries_.end() ? nullptr : &it->second.options;
  }

  void Show(Notification entry, std::string sender, base::OnceCallback<void(bool)> completion) {
    auto previous = Find(*entry.context, entry.extension, entry.id);
    if (previous != entries_.end())
      Erase(previous);
    const std::string identifier = base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto it = entries_.emplace(identifier, std::move(entry)).first;
    UpdateCategories();
    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    const auto& options = it->second.options;
    content.title = base::SysUTF8ToNSString(*options.FindString("title"));
    content.subtitle = base::SysUTF8ToNSString(sender);
    content.body = base::SysUTF8ToNSString(*options.FindString("message"));
    if (!options.FindBool("silent").value_or(false))
      content.sound = UNNotificationSound.defaultSound;
    content.categoryIdentifier = base::SysUTF8ToNSString(identifier);
    auto callback = base::BindPostTaskToCurrentDefault(base::BindOnce(
        &Notifications::OnDelivery, base::Unretained(this), identifier, std::move(completion)));
    auto shared_reply = std::make_shared<base::OnceCallback<void(bool)>>(std::move(callback));
    auto request = [UNNotificationRequest requestWithIdentifier:content.categoryIdentifier
                                                       content:content trigger:nil];
    [center_ addNotificationRequest:request withCompletionHandler:^(NSError* error) {
      std::move(*shared_reply).Run(error == nil);
    }];
  }

  void HandleAction(std::string identifier, std::string action) {
    auto it = entries_.find(identifier);
    if (it == entries_.end())
      return;
    auto entry = std::move(it->second);
    Erase(it);
    UpdateCategories();
    if (action == base::SysNSStringToUTF8(UNNotificationDismissActionIdentifier)) {
      Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLOSED, "notifications.onClosed",
               base::ListValue().Append(entry.id).Append(true));
    } else if (action == base::SysNSStringToUTF8(UNNotificationDefaultActionIdentifier)) {
      Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLICKED, "notifications.onClicked",
               base::ListValue().Append(entry.id));
    } else if (action == "0" || action == "1") {
      Dispatch(entry, extensions::events::NOTIFICATIONS_ON_BUTTON_CLICKED,
               "notifications.onButtonClicked", base::ListValue().Append(entry.id).Append(action == "1" ? 1 : 0));
    }
  }

  UNUserNotificationCenter* center() const { return center_; }

 private:
  using Entries = std::map<std::string, Notification>;

  Entries::iterator Erase(Entries::iterator it) {
    NSArray* identifiers = @[base::SysUTF8ToNSString(it->first)];
    [center_ removePendingNotificationRequestsWithIdentifiers:identifiers];
    [center_ removeDeliveredNotificationsWithIdentifiers:identifiers];
    return entries_.erase(it);
  }

  void UpdateCategories() {
    NSMutableSet<UNNotificationCategory*>* categories = [NSMutableSet set];
    for (const auto& [id, entry] : entries_) {
      NSMutableArray<UNNotificationAction*>* actions = [NSMutableArray array];
      if (const auto* buttons = entry.options.FindList("buttons")) {
        for (const auto& button : *buttons) {
          [actions addObject:[UNNotificationAction
              actionWithIdentifier:actions.count == 0 ? @"0" : @"1"
              title:base::SysUTF8ToNSString(*button.GetDict().FindString("title"))
              options:UNNotificationActionOptionForeground]];
        }
      }
      [categories addObject:[UNNotificationCategory
          categoryWithIdentifier:base::SysUTF8ToNSString(id)
          actions:actions intentIdentifiers:@[] options:UNNotificationCategoryOptionCustomDismissAction]];
    }
    [center_ setNotificationCategories:categories];
  }

  void OnDelivery(const std::string& identifier, base::OnceCallback<void(bool)> reply, bool success) {
    auto it = entries_.find(identifier);
    if (it == entries_.end()) {
      [center_ removeDeliveredNotificationsWithIdentifiers:@[base::SysUTF8ToNSString(identifier)]];
      success = false;
    } else if (!success) {
      Erase(it);
      UpdateCategories();
    }
    std::move(reply).Run(success);
  }

  static void Dispatch(const Notification& entry, extensions::events::HistogramValue value,
                       std::string name, base::ListValue args) {
    auto* context = entry.context.get();
    if (!FindNotificationExtension(context, entry.extension)) {
      return;
    }
    extensions::EventRouter::Get(context)->DispatchEventToExtension(
        entry.extension, std::make_unique<extensions::Event>(value, std::move(name), std::move(args), context));
  }

  Entries entries_;
  UNUserNotificationCenter* const center_;
  ZitterExtensionNotificationDelegate* const delegate_;
};

Notifications* g_notifications = nullptr;

class NotificationFunction : public ExtensionFunction {
 protected:
  ~NotificationFunction() override = default;

  ResponseAction Run() override {
    auto* context = browser_context();
    if (!extension() || !FindNotificationExtension(context, extension_id())) {
      return RespondNow(Error("Notifications are unavailable in this profile."));
    }
    auto& service = Notifications::Get();
    g_notifications = &service;
    const std::string_view method(name());
    if (method == "notifications.getAll")
      return RespondNow(WithArguments(service.GetAll(*context, extension_id())));
    if (method == "notifications.getPermissionLevel") {
      auto reply = base::BindPostTaskToCurrentDefault(
          base::BindRepeating(&NotificationFunction::ReplyWithPermission, this));
      [service.center() getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
        reply.Run(IsAuthorized(settings.authorizationStatus));
      }];
      return RespondLater();
    }
    const auto& arguments = args();
    const auto* id = arguments.empty() ? nullptr : arguments[0].GetIfString();
    if (method == "notifications.clear") {
      EXTENSION_FUNCTION_VALIDATE(id && arguments.size() == 1);
      return RespondNow(WithArguments(service.Clear(*context, extension_id(), *id)));
    }
    EXTENSION_FUNCTION_VALIDATE(!arguments.empty() && arguments.back().is_dict());
    const bool update = method == "notifications.update";
    notification_id_ = id && !id->empty() ? *id : base::Uuid::GenerateRandomV4().AsLowercaseString();
    if (notification_id_.size() > 500)
      return RespondNow(Error("Notification ID is too long."));
    options_ = arguments.back().GetDict().Clone();
    if (update) {
      EXTENSION_FUNCTION_VALIDATE(id);
      const auto* existing = service.Options(*context, extension_id(), *id);
      if (!existing)
        return RespondNow(WithArguments(false));
      auto merged = existing->Clone();
      merged.Merge(std::move(options_));
      options_ = std::move(merged);
    }
    const auto* type = options_.FindString("type");
    if (!type || *type != "basic" || !options_.FindString("title") ||
        !options_.FindString("message") || !options_.FindString("iconUrl")) {
      return RespondNow(Error("Basic notifications require a title, message and icon URL."));
    }
    if (const auto* buttons = options_.FindList("buttons");
        buttons && (buttons->size() > 2 || !std::ranges::all_of(*buttons, [](const auto& button) {
          return button.is_dict() && button.GetDict().FindString("title");
        }))) {
      return RespondNow(Error("A notification supports at most two named actions."));
    }
    update_ = update;
    auto reply = base::BindPostTaskToCurrentDefault(
        base::BindRepeating(&NotificationFunction::OnAuthorization, this));
    UNUserNotificationCenter* center = service.center();
    [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
      if (settings.authorizationStatus != UNAuthorizationStatusNotDetermined) {
        reply.Run(IsAuthorized(settings.authorizationStatus));
        return;
      }
      [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                           completionHandler:^(BOOL granted, NSError*) { reply.Run(granted); }];
    }];
    return RespondLater();
  }

 private:
  void ReplyWithPermission(bool granted) {
    Respond(WithArguments(granted ? "granted" : "denied"));
  }

  void OnAuthorization(bool granted) {
    auto* context = browser_context();
    if (!granted || !FindNotificationExtension(context, extension_id())) {
      Respond(Error(kDenied));
      return;
    }
    auto& service = Notifications::Get();
    if (update_ && !service.Options(*context, extension_id(), notification_id_)) {
      Respond(WithArguments(false));
      return;
    }
    service.Show(
        {context->GetWeakPtr(), extension_id(), notification_id_, std::move(options_)},
        extension()->name(), base::BindOnce(&NotificationFunction::OnDelivery, this));
  }

  void OnDelivery(bool success) {
    if (!success)
      Respond(Error("macOS did not deliver the notification."));
    else if (update_)
      Respond(WithArguments(true));
    else
      Respond(WithArguments(notification_id_));
  }

  bool update_ = false;
  std::string notification_id_;
  base::DictValue options_;
};

#define ZITTER_NOTIFICATION_FUNCTION(Class, Method, Histogram) \
  class Class final : public NotificationFunction { \
   public: \
    DECLARE_EXTENSION_FUNCTION("notifications." Method, Histogram) \
   private: \
    ~Class() override = default; \
  }

ZITTER_NOTIFICATION_FUNCTION(CreateNotificationFunction, "create", NOTIFICATIONS_CREATE);
ZITTER_NOTIFICATION_FUNCTION(UpdateNotificationFunction, "update", NOTIFICATIONS_UPDATE);
ZITTER_NOTIFICATION_FUNCTION(ClearNotificationFunction, "clear", NOTIFICATIONS_CLEAR);
ZITTER_NOTIFICATION_FUNCTION(GetNotificationsFunction, "getAll", NOTIFICATIONS_GET_ALL);
ZITTER_NOTIFICATION_FUNCTION(GetNotificationPermissionFunction, "getPermissionLevel", NOTIFICATIONS_GETPERMISSIONLEVEL);
#undef ZITTER_NOTIFICATION_FUNCTION

}  // namespace

void RegisterExtensionNotificationFunctions(ExtensionFunctionRegistry& registry) {
  registry.RegisterFunction<CreateNotificationFunction>();
  registry.RegisterFunction<UpdateNotificationFunction>();
  registry.RegisterFunction<ClearNotificationFunction>();
  registry.RegisterFunction<GetNotificationsFunction>();
  registry.RegisterFunction<GetNotificationPermissionFunction>();
}

void RemoveExtensionNotifications(content::BrowserContext& context, std::string_view extension_id) {
  if (g_notifications)
    g_notifications->Remove(context, extension_id);
}

void DispatchMacExtensionNotificationAction(std::string identifier, std::string action) {
  if (g_notifications)
    g_notifications->HandleAction(std::move(identifier), std::move(action));
}

}  // namespace zitter::content_embedder

@implementation ZitterExtensionNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion {
  completion(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
             UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
              withCompletionHandler:(void (^)(void))completion {
  dispatch_async(dispatch_get_main_queue(), ^{
    zitter::content_embedder::DispatchMacExtensionNotificationAction(
        base::SysNSStringToUTF8(response.notification.request.identifier),
        base::SysNSStringToUTF8(response.actionIdentifier));
  });
  completion();
}

@end
