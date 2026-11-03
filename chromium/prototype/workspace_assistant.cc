// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/assistant_panel.h"
#include "assistant_page_script.h"
#include <algorithm>
#include <array>
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/common/isolated_world_ids.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view_tracker.h"

namespace zitter::prototype {
namespace {
// Chrome starts its worlds at TRANSLATE (2). Reserve the first embedder slot
// (1), below all Chrome and dynamically allocated extension worlds.
constexpr int kAssistantWorld = content::ISOLATED_WORLD_ID_CONTENT_END;
static_assert(kAssistantWorld > content::ISOLATED_WORLD_ID_GLOBAL && kAssistantWorld < ISOLATED_WORLD_ID_TRANSLATE);
base::Value Error(std::string message) { return base::Value(base::DictValue().Set("error", std::move(message))); }
std::string SafeURL(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) return {};
  GURL::Replacements strip; strip.ClearUsername(); strip.ClearPassword(); strip.ClearQuery(); strip.ClearRef();
  return url.ReplaceComponents(strip).spec();
}
}

Profile* WorkspaceWindow::AssistantProfile() const {
  return model_.active() ? ProfileFor(model_.active()->profile_id) : nullptr;
}
std::string WorkspaceWindow::AssistantProfileId() const {
  return model_.active() ? model_.active()->profile_id : std::string();
}
void WorkspaceWindow::ShowAssistant() {
  if (!assistant_) {
    auto panel = std::make_unique<AssistantPanel>(this);
    assistant_ = panel.get();
    shell_->SetAssistantView(panel.release());
  }
  assistant_->Show();
}

base::DictValue WorkspaceWindow::AssistantContext(const AssistantPermissions& permissions) {
  base::DictValue context;
  if (!permissions.tabs || !AssistantProfile() || AssistantProfile()->IsOffTheRecord()) return context;
  std::erase_if(assistant_targets_, [](const auto& entry) { return !entry.second->view(); });
  base::ListValue tabs, pins;
  const auto profile = AssistantProfileId();
  for (const auto& host : hosts_) {
    if (host.profile_id != profile) continue;
    for (size_t i = 0; i < host.tabs.size(); ++i) {
      auto* view = host.tabs[i].get();
      const auto url = SafeURL(TabUrl(host, view));
      if (url.empty()) continue;
      std::string id;
      for (const auto& [key, target] : assistant_targets_) if (target->view() == view) { id = key; break; }
      if (id.empty()) {
        id = base::Uuid::GenerateRandomV4().AsLowercaseString();
        auto target = std::make_unique<views::ViewTracker>(); target->SetView(view);
        assistant_targets_[id] = std::move(target);
      }
      tabs.Append(base::DictValue().Set("id", id).Set("mention", "@tab:" + id)
          .Set("title", base::UTF16ToUTF8(TabTitle(host, view))).Set("url", url)
          .Set("current", host.workspace_id == active_id() && host.active_tab == i));
    }
  }
  for (const auto& workspace : model_.workspaces()) {
    if (workspace.profile_id != profile) continue;
    for (const auto& pin : pinned_items(workspace.id)) {
      if (pin.folder || SafeURL(pin.url).empty()) continue;
      pins.Append(base::DictValue().Set("id", pin.id).Set("mention", "@pin:" + pin.id)
          .Set("title", pin.title).Set("url", SafeURL(pin.url)));
    }
  }
  context.Set("tabs", std::move(tabs)); context.Set("pins", std::move(pins));
  return context;
}

void WorkspaceWindow::AssistantTool(std::string tool, base::DictValue args, AssistantPermissions permissions,
                                    base::OnceCallback<void(base::Value)> done, bool user_confirmed) {
  auto fail = [&](std::string text) { std::move(done).Run(Error(std::move(text))); };
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord()) { fail("Assistant is unavailable in private profiles"); return; }
  if (tool == "list_tabs") {
    if (!permissions.tabs) { fail("Tab discovery permission is disabled"); return; }
    std::move(done).Run(base::Value(AssistantContext(permissions))); return;
  }
  if (tool == "history") {
    if (!permissions.history) { fail("History permission is disabled"); return; }
    const auto* query = args.FindString("query");
    if (!query || query->empty() || query->size() > 600) { fail("A history query is required"); return; }
    auto* service = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
    if (!service) { fail("History is unavailable"); return; }
    history::QueryOptions options; options.max_count = 30;
    service->QueryHistory(base::UTF8ToUTF16(*query), options, base::BindOnce(
        [](base::OnceCallback<void(base::Value)> done, history::QueryResults rows) {
          base::ListValue entries;
          for (const auto& row : rows) {
            const auto url = SafeURL(row.url()); if (url.empty()) continue;
            entries.Append(base::DictValue().Set("title", base::UTF16ToUTF8(row.title())).Set("url", url));
          }
          std::move(done).Run(base::Value(std::move(entries)));
        }, std::move(done)), &history_tasks_);
    return;
  }
  if (tool == "browser_command") {
    if (!permissions.actions) { fail("Browser actions permission is disabled"); return; }
    const auto* id = args.FindString("id");
    static constexpr auto commands = std::to_array<std::string_view>({"new_tab", "split", "close_split", "reload", "back", "forward", "find",
        "settings", "downloads", "history", "timeline", "save_snapshot", "reopen", "sidebar"});
    if (!id || std::ranges::find(commands, *id) == commands.end()) { fail("Unknown or protected browser command"); return; }
    if (*id == "save_snapshot") { std::move(done).Run(base::Value(base::DictValue().Set("ok", SaveSnapshot("AI assistant")))); return; }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", RunShortcut(*id)))); return;
  }
  const bool read = tool == "read_page";
  const bool mutation = tool == "open_tab" || tool == "navigate" || tool == "activate_tab" || tool == "close_tab" ||
                        tool == "click" || tool == "type" || tool == "scroll" || tool == "pin_tab" || tool == "unpin_tab";
  if (!read && !mutation) { fail("Unknown browser tool"); return; }
  if (read ? !permissions.pages : !permissions.actions) { fail("Required page/action permission is disabled"); return; }
  if (tool == "open_tab") {
    const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", NavigateFromBar(url, true)))); return;
  }
  if (!permissions.tabs) { fail("Enable tab discovery to target browser pages"); return; }
  const auto* target = args.FindString("tab");
  if (!target) { fail("Missing tab reference"); return; }
  std::string id = *target;
  if (id.starts_with("@tab:")) id.erase(0, 5);
  if (id.starts_with("@pin:")) {
    if (!permissions.actions) { fail("Opening a pin requires browser actions permission"); return; }
    for (const auto& workspace : model_.workspaces()) {
      if (workspace.profile_id != AssistantProfileId()) continue;
      for (const auto& pin : pinned_items(workspace.id)) if (pin.id == id.substr(5) && !pin.folder) {
        if (!Select(workspace.id)) { fail("Workspace is not ready"); return; }
        OpenPinnedItem(pin.id);
        std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("detail", "Pin opened. List tabs before reading it."))); return;
      }
    }
    fail("Pin no longer exists in this profile"); return;
  }
  views::WebView* view = nullptr;
  if (id == "@current" || id == "current") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) view = host.view;
  } else {
    const auto found = assistant_targets_.find(id);
    if (found != assistant_targets_.end() && found->second->view()) view = static_cast<views::WebView*>(found->second->view());
  }
  Host* owner = nullptr; size_t index = 0;
  for (auto& host : hosts_) {
    if (host.profile_id != AssistantProfileId()) continue;
    for (size_t i = 0; i < host.tabs.size(); ++i) if (host.tabs[i] == view) { owner = &host; index = i; }
  }
  if (!owner || !view) { fail("Tab expired or belongs to another profile"); return; }
  if (tool == "pin_tab" || tool == "unpin_tab") {
    if (!Select(owner->workspace_id)) { fail("Workspace is not ready"); return; }
    if (tool == "pin_tab") { std::move(done).Run(base::Value(base::DictValue().Set("ok", PinTab(index, {}, {})))); return; }
    for (const auto& [pin, pinned_view] : owner->pin_tabs) if (pinned_view == view) {
      const auto pin_id = pin;
      std::move(done).Run(base::Value(base::DictValue().Set("ok", UnpinItem(pin_id, owner->tabs.size())))); return;
    }
    fail("Tab is not pinned"); return;
  }
  if (tool == "activate_tab") {
    if (!Select(owner->workspace_id)) { fail("Workspace is not ready"); return; }
    SelectTab(index); std::move(done).Run(base::Value(true)); return;
  }
  if (tool == "close_tab") {
    CloseWorkspaceTab(owner->workspace_id, index);
    std::move(done).Run(base::Value(base::DictValue().Set("detail", "Close requested. List tabs to verify; before-unload confirmation may keep it open."))); return;
  }
  auto* page = EnsurePage(view);
  if (!page || !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() || page->IsCrashed()) { fail("Page is not ready or is protected"); return; }
  if (tool == "navigate") {
    const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
    page->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("detail", "Navigation started; read again when ready"))); return;
  }
  args.Set("tool", tool);
  // This bit is native user consent, never a model-supplied argument.
  args.Set("confirmed", user_confirmed || !permissions.confirm_clicks);
  std::string script = kAssistantPageScript;
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS", base::WriteJson(args).value_or("{}"));
  page->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script),
      base::BindOnce([](base::OnceCallback<void(base::Value)> done, base::Value value) { std::move(done).Run(std::move(value)); },
                     std::move(done)), kAssistantWorld);
}
}
