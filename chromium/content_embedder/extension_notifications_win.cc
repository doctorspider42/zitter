// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "extension_notifications.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_function_registry.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "profile_context.h"

namespace zitter::content_embedder {
namespace {

const extensions::Extension* FindNotificationExtension(content::BrowserContext* context,
                                                       const std::string& id) {
  if (!context || context->ShutdownStarted() || context->IsOffTheRecord())
    return nullptr;
  const auto* extension = extensions::ExtensionRegistry::Get(context)->enabled_extensions().GetByID(id);
  return extension && extension->permissions_data()->HasAPIPermission(
                          extensions::mojom::APIPermissionID::kNotifications)
             ? extension : nullptr;
}

enum class Interaction { kClick, kDismiss, kFirstButton, kSecondButton };

struct Notification {
  base::WeakPtr<content::BrowserContext> context;
  std::string extension;
  std::string id;
  base::DictValue options;
};

class Notifications {
 public:
  static Notifications& Get() {
    static base::NoDestructor<Notifications> service;
    return *service;
  }

  Notifications();
  ~Notifications();

  const base::DictValue* Options(content::BrowserContext& context,
                                std::string_view extension, std::string_view id) {
    const auto entry = Find(context, extension, id);
    return entry == entries_.end() ? nullptr : &entry->second.options;
  }

  base::DictValue GetAll(content::BrowserContext& context,
                         std::string_view extension) const {
    base::DictValue result;
    for (const auto& [token, entry] : entries_) {
      if (entry.context.get() == &context && entry.extension == extension)
        result.Set(entry.id, true);
    }
    return result;
  }

  bool Clear(content::BrowserContext& context, std::string_view extension,
             std::string_view id) {
    const auto it = Find(context, extension, id);
    if (it == entries_.end())
      return false;
    auto entry = Erase(it);
    Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLOSED, "notifications.onClosed",
             base::ListValue().Append(entry.id).Append(false));
    return true;
  }

  void Remove(content::BrowserContext& context, std::string_view extension) {
    for (auto it = entries_.begin(); it != entries_.end();) {
      const auto& entry = it->second;
      if (entry.context.get() == &context && (extension.empty() || entry.extension == extension))
        Erase(it++);
      else
        ++it;
    }
  }

  void Show(ProfileContext& profile, std::string extension, std::string id,
            base::DictValue options, std::string_view sender) {
    const auto previous = Find(profile, extension, id);
    if (previous != entries_.end())
      Erase(previous);
    const std::string token =
        "extension:" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto [it, inserted] = entries_.emplace(token, Notification{
        profile.GetWeakPtr(), std::move(extension), std::move(id), std::move(options)});
    CHECK(inserted);
    const auto& data = it->second.options;
    std::vector<std::pair<std::u16string, base::RepeatingClosure>> actions;
    if (const auto* buttons = data.FindList("buttons")) {
      for (const auto& button : *buttons) {
        const auto action = actions.empty() ? Interaction::kFirstButton
                                            : Interaction::kSecondButton;
        actions.emplace_back(base::UTF8ToUTF16(*button.GetDict().FindString("title")),
                             Callback(token, action));
      }
    }
    profile.notification_presenter().Run(
        token, base::UTF8ToUTF16(sender), base::UTF8ToUTF16(*data.FindString("title")),
        base::UTF8ToUTF16(*data.FindString("message")),
        Callback(token, Interaction::kClick), std::move(actions),
        Callback(token, Interaction::kDismiss));
  }

 private:
  using Entries = std::map<std::string, Notification>;

  Entries::iterator Find(content::BrowserContext& context,
                         std::string_view extension, std::string_view id) {
    return std::ranges::find_if(entries_, [&](const auto& pair) {
      const auto& entry = pair.second;
      return entry.context.get() == &context && entry.extension == extension && entry.id == id;
    });
  }

  Notification Erase(Entries::iterator it) {
    auto node = entries_.extract(it);
    auto& entry = node.mapped();
    if (auto* context = entry.context.get())
      static_cast<ProfileContext*>(context)->RemovePresentedNotification(node.key());
    return std::move(entry);
  }

  base::RepeatingClosure Callback(const std::string& token, Interaction action) {
    return base::BindRepeating(&Notifications::HandleAction, base::Unretained(this),
                               token, action);
  }

  void HandleAction(const std::string& token, Interaction action) {
    const auto it = entries_.find(token);
    if (it == entries_.end())
      return;
    auto entry = Erase(it);
    switch (action) {
      case Interaction::kClick:
        Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLICKED, "notifications.onClicked",
                 base::ListValue().Append(entry.id));
        break;
      case Interaction::kDismiss:
        Dispatch(entry, extensions::events::NOTIFICATIONS_ON_CLOSED, "notifications.onClosed",
                 base::ListValue().Append(entry.id).Append(true));
        break;
      case Interaction::kFirstButton:
      case Interaction::kSecondButton:
        Dispatch(entry, extensions::events::NOTIFICATIONS_ON_BUTTON_CLICKED,
                 "notifications.onButtonClicked",
                 base::ListValue().Append(entry.id).Append(
                     action == Interaction::kFirstButton ? 0 : 1));
        break;
    }
  }

  static void Dispatch(const Notification& entry, extensions::events::HistogramValue event,
                       std::string name, base::ListValue args) {
    auto* context = entry.context.get();
    if (!FindNotificationExtension(context, entry.extension))
      return;
    extensions::EventRouter::Get(context)->DispatchEventToExtension(
        entry.extension,
        std::make_unique<extensions::Event>(event, std::move(name), base::MakeRefCounted<extensions::EventArgs>(std::move(args)), context));
  }

  Entries entries_;
};

Notifications::Notifications() = default;
Notifications::~Notifications() = default;

class NotificationFunction : public ExtensionFunction {
 protected:
  ~NotificationFunction() override = default;

  ResponseAction Run() override {
    auto* context = browser_context();
    if (!extension() || !FindNotificationExtension(context, extension_id())) {
      return RespondNow(Error("Notifications are unavailable in this profile."));
    }
    auto& service = Notifications::Get();
    auto* profile = static_cast<ProfileContext*>(context);
    if (!profile->notification_presenter())
      return RespondNow(Error("The notification center is unavailable."));
    const std::string_view method(name());
    if (method == "notifications.getAll")
      return RespondNow(WithArguments(service.GetAll(*context, extension_id())));
    if (method == "notifications.getPermissionLevel")
      return RespondNow(WithArguments("granted"));
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
    service.Show(*profile, extension_id(), notification_id_, std::move(options_),
                 extension()->name());
    return update ? RespondNow(WithArguments(true))
                  : RespondNow(WithArguments(notification_id_));
  }

 private:
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

void RemoveExtensionNotifications(content::BrowserContext& context,
                                  std::string_view extension_id) {
  Notifications::Get().Remove(context, extension_id);
}
}  // namespace zitter::content_embedder
