// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/extension_tab_events.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/tabs.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_event_histogram_value.h"

namespace zitter::prototype {
namespace {

using extensions::ExtensionTabUtil;

// The tab as one extension may see it.
base::DictValue TabValue(content::WebContents* contents, extensions::mojom::ContextType context,
                         const extensions::Extension* extension) {
  return ExtensionTabUtil::CreateTabObject(contents, ExtensionTabUtil::GetScrubTabBehavior(extension, context, contents),
                                           extension)
      .ToValue();
}

void Dispatch(content::WebContents* contents, extensions::events::HistogramValue histogram, const char* name,
              extensions::Event::WillDispatchCallback arguments) {
  auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  auto* router = extensions::EventRouter::Get(profile);
  if (!router || !router->HasEventListener(name)) return;
  auto event = std::make_unique<extensions::Event>(histogram, name, base::ListValue(), profile);
  event->user_gesture = extensions::EventRouter::UserGestureState::kNotEnabled;
  event->will_dispatch_callback = std::move(arguments);
  router->BroadcastEvent(std::move(event));
}

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(ExtensionTabEvents);

// static
void ExtensionTabEvents::Attach(content::WebContents* contents) {
  if (contents->GetBrowserContext()->IsOffTheRecord()) return;
  if (!FromWebContents(contents)) CreateForWebContents(contents);
}

ExtensionTabEvents::ExtensionTabEvents(content::WebContents* contents)
    : content::WebContentsObserver(contents), content::WebContentsUserData<ExtensionTabEvents>(*contents) {
  // The page's tab helpers (its tab id among them) attach after this one.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ExtensionTabEvents::Created, weak_factory_.GetWeakPtr()));
}

ExtensionTabEvents::~ExtensionTabEvents() = default;

void ExtensionTabEvents::Created() {
  auto* contents = web_contents();
  Dispatch(contents, extensions::events::TABS_ON_CREATED, extensions::api::tabs::OnCreated::kEventName,
           base::BindRepeating([](content::WebContents* contents, content::BrowserContext*,
                                  extensions::mojom::ContextType context, const extensions::Extension* extension,
                                  const base::DictValue*, std::optional<base::ListValue>& arguments,
                                  extensions::mojom::EventFilteringInfoPtr&, bool*) {
             arguments.emplace();
             arguments->Append(TabValue(contents, context, extension));
             return true;
           }, contents));
}

// As TabsEventRouter: changeInfo carries only the changed properties of the
// tab object, so an extension without access never sees the url or title.
void ExtensionTabEvents::Updated(std::set<std::string> changed) {
  auto* contents = web_contents();
  Dispatch(contents, extensions::events::TABS_ON_UPDATED, extensions::api::tabs::OnUpdated::kEventName,
           base::BindRepeating([](content::WebContents* contents, const std::set<std::string>& changed,
                                  content::BrowserContext*, extensions::mojom::ContextType context,
                                  const extensions::Extension* extension, const base::DictValue*,
                                  std::optional<base::ListValue>& arguments,
                                  extensions::mojom::EventFilteringInfoPtr&, bool*) {
             auto tab = TabValue(contents, context, extension);
             base::DictValue info;
             for (const auto& property : changed)
               if (const auto* value = tab.Find(property)) info.Set(property, value->Clone());
             if (info.empty()) return false;
             arguments.emplace();
             arguments->Append(ExtensionTabUtil::GetTabId(contents));
             arguments->Append(std::move(info));
             arguments->Append(std::move(tab));
             return true;
           }, contents, std::move(changed)));
}

void ExtensionTabEvents::DidStartLoading() { Updated({"status"}); }
void ExtensionTabEvents::DidStopLoading() { Updated({"status"}); }
void ExtensionTabEvents::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame() && navigation->HasCommitted() && !navigation->IsSameDocument())
    Updated({"url", "status"});
}
void ExtensionTabEvents::TitleWasSet(content::NavigationEntry*) { Updated({"title"}); }

void ExtensionTabEvents::WebContentsDestroyed() {
  auto* contents = web_contents();
  const int tab = ExtensionTabUtil::GetTabId(contents);
  base::ListValue arguments;
  arguments.Append(tab);
  arguments.Append(base::DictValue().Set("windowId", ExtensionTabUtil::GetWindowIdOfTab(contents))
                                    .Set("isWindowClosing", false));
  auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (auto* router = extensions::EventRouter::Get(profile)) {
    router->BroadcastEvent(std::make_unique<extensions::Event>(extensions::events::TABS_ON_REMOVED,
        extensions::api::tabs::OnRemoved::kEventName, std::move(arguments), profile));
  }
  Observe(nullptr);
}

// static
void ExtensionTabEvents::DispatchActivated(content::WebContents* contents) {
  if (!contents || !FromWebContents(contents)) return;
  base::ListValue arguments;
  arguments.Append(base::DictValue().Set("tabId", ExtensionTabUtil::GetTabId(contents))
                                    .Set("windowId", ExtensionTabUtil::GetWindowIdOfTab(contents)));
  auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (auto* router = extensions::EventRouter::Get(profile)) {
    router->BroadcastEvent(std::make_unique<extensions::Event>(extensions::events::TABS_ON_ACTIVATED,
        extensions::api::tabs::OnActivated::kEventName, std::move(arguments), profile));
  }
}

}  // namespace zitter::prototype
