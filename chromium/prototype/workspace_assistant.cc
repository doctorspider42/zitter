// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/assistant_panel.h"
#include "assistant_page_script.h"
#include <algorithm>
#include <array>
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_split.h"
#include "base/task/thread_pool.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/host_zoom_map.h"
#include "third_party/blink/public/common/page/page_zoom.h"
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
// What Settings can say about each backend without a network request.
struct BackendProbe { bool openai = false, codex = false, claude = false; std::string ollama; };
bool OnPath(const std::string& name) {
  auto env = base::Environment::Create();
  const auto path = env->GetVar("PATH").value_or("");
#if BUILDFLAG(IS_WIN)
  const std::string separator = ";";
  const std::vector<std::string> suffixes = {".exe", ".cmd", ".bat", ""};
#else
  const std::string separator = ":";
  const std::vector<std::string> suffixes = {""};
#endif
  for (const auto& dir : base::SplitString(path, separator, base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
    for (const auto& suffix : suffixes)
      if (base::PathExists(base::FilePath::FromUTF8Unsafe(dir).AppendASCII(name + suffix))) return true;
  return false;
}
BackendProbe ProbeBackends() {
  auto env = base::Environment::Create();
  auto set = [&](base::cstring_view name) { const auto value = env->GetVar(name); return value && !value->empty(); };
  auto file = [&](base::cstring_view name) { const auto value = env->GetVar(name); return value && base::PathExists(base::FilePath::FromUTF8Unsafe(*value)); };
  base::FilePath exe; base::PathService::Get(base::DIR_EXE, &exe);
  BackendProbe probe;
  probe.openai = set("OPENAI_API_KEY");
  probe.codex = file("ZITTER_CODEX_SDK_PATH") || base::PathExists(exe.AppendASCII("assistant").AppendASCII("node_modules")
      .AppendASCII("@openai").AppendASCII("codex-sdk").AppendASCII("package.json"));
  probe.claude = file("ZITTER_CLAUDE_PATH") || OnPath("claude");
  const GURL ollama(env->GetVar("ZITTER_OLLAMA_URL").value_or("http://127.0.0.1:11434"));
  probe.ollama = ollama.is_valid() ? std::string(ollama.host()) + (ollama.has_port() ? ":" + std::string(ollama.port()) : std::string()) : "127.0.0.1:11434";
  return probe;
}
}

WorkspaceWindow::AssistantBackgroundPage::AssistantBackgroundPage() = default;
WorkspaceWindow::AssistantBackgroundPage::AssistantBackgroundPage(AssistantBackgroundPage&&) = default;
WorkspaceWindow::AssistantBackgroundPage& WorkspaceWindow::AssistantBackgroundPage::operator=(AssistantBackgroundPage&&) = default;
WorkspaceWindow::AssistantBackgroundPage::~AssistantBackgroundPage() = default;

void WorkspaceWindow::SetAssistantSettings(AssistantSettings settings) {
  assistant_settings_ = std::move(settings);
  if (assistant_settings_.models.empty()) assistant_settings_.models = AssistantSettings().models;
  SaveConfiguration();
  shell_->SetAssistantAllowed(assistant_settings_.enabled);
  if (assistant_) assistant_->SettingsChanged();
  if (!assistant_settings_.enabled) {
    std::vector<std::string> keys;
    for (const auto& [key, page] : assistant_background_) keys.push_back(key);
    for (const auto& key : keys) AssistantDiscardBackground(key);
  }
  shell_->RefreshAssistantPreferences();
}
base::DictValue WorkspaceWindow::AssistantSettingsForShell() {
  if (!assistant_status_pending_ && assistant_backend_status_.empty()) {
    assistant_status_pending_ = true;
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ProbeBackends), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, BackendProbe probe) {
          if (!window) return;
          window->assistant_status_pending_ = false;
          // tone: 1 ready, 2 needs setup, 0 neutral.
          auto row = [](int tone, std::u16string state, std::u16string detail) {
            return base::DictValue().Set("tone", tone).Set("state", base::UTF16ToUTF8(state)).Set("detail", base::UTF16ToUTF8(detail));
          };
          base::ListValue status;
          status.Append(probe.openai ? row(1, T(u"Gotowy"), T(u"OPENAI_API_KEY ze środowiska")) : row(2, T(u"Brak klucza"), T(u"Ustaw OPENAI_API_KEY przed uruchomieniem Zittera")));
          status.Append(probe.codex ? row(1, T(u"Gotowy"), T(u"SDK w pakiecie · zaloguj się w Codex")) : row(2, T(u"Brak SDK"), T(u"Uruchom npm ci w katalogu assistant")));
          status.Append(probe.claude ? row(1, T(u"Gotowy"), T(u"claude w PATH")) : row(2, T(u"Nie znaleziono"), T(u"Zainstaluj Claude Code albo ustaw ZITTER_CLAUDE_PATH")));
          status.Append(row(0, T(u"Lokalny"), base::UTF8ToUTF16(probe.ollama)));
          window->assistant_backend_status_ = std::move(status);
          window->shell_->RefreshAssistantPreferences();
        }, weak_factory_.GetWeakPtr()));
  }
  base::ListValue models;
  for (const auto& model : assistant_settings_.models)
    models.Append(base::DictValue().Set("provider", model.provider).Set("id", model.id).Set("visible", model.visible)
        .Set("label", base::UTF16ToUTF8(AssistantProviderLabel(model.provider))));
  base::ListValue backends;
  for (size_t i = 0; i < kAssistantProviders.size(); ++i)
    backends.Append(base::DictValue().Set("provider", kAssistantProviders[i]).Set("label", base::UTF16ToUTF8(AssistantProviderLabel(kAssistantProviders[i])))
        .Set("enabled", assistant_settings_.backends[i]));
  return base::DictValue().Set("enabled", assistant_settings_.enabled && !configuration_invalid_).Set("backends", std::move(backends))
      .Set("status", assistant_backend_status_.Clone()).Set("models", std::move(models)).Set("default_model", assistant_settings_.default_model);
}
// Settings rows send small commands, validated here.
void WorkspaceWindow::WriteAssistantSettings(base::DictValue value) {
  if (configuration_invalid_ || !assistant_settings_.enabled) return;
  auto settings = assistant_settings_;
  const auto* command = value.FindString("command");
  const auto* provider = value.FindString("provider");
  const auto* id = value.FindString("id");
  auto find = [&]() -> AssistantModel* {
    for (auto& model : settings.models) if (provider && id && model.provider == *provider && model.id == *id) return &model;
    return nullptr;
  };
  if (!command) return;
  if (*command == "backend" && provider) {
    for (size_t i = 0; i < kAssistantProviders.size(); ++i)
      if (*provider == kAssistantProviders[i]) settings.backends[i] = value.FindBool("enabled").value_or(true);
  } else if (*command == "visible") {
    auto* model = find(); if (!model) return;
    model->visible = value.FindBool("visible").value_or(true);
  } else if (*command == "default") {
    auto* model = find(); if (!model) return;
    model->visible = true; settings.default_model = model->provider + "/" + model->id;
  } else if (*command == "remove") {
    if (!find() || settings.models.size() <= 1) return;
    std::erase_if(settings.models, [&](const auto& model) { return model.provider == *provider && model.id == *id; });
    if (settings.default_model == *provider + "/" + *id) settings.default_model = settings.models.front().provider + "/" + settings.models.front().id;
  } else if (*command == "add") {
    if (!provider || !id || std::ranges::find(kAssistantProviders, *provider) == kAssistantProviders.end()) return;
    const auto trimmed = std::string(base::TrimWhitespaceASCII(*id, base::TRIM_ALL));
    if (trimmed.empty() || trimmed.size() > 120 || settings.models.size() >= 32) return;
    for (const auto& model : settings.models) if (model.provider == *provider && model.id == trimmed) return;
    settings.models.push_back({*provider, trimmed, true});
  } else return;
  SetAssistantSettings(std::move(settings));
}

views::WebView* WorkspaceWindow::AssistantTabView(std::string id) {
  if (id.starts_with("@tab:")) id.erase(0, 5);
  if (id == "@current" || id == "current") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) return host.view;
    return nullptr;
  }
  const auto found = assistant_targets_.find(id);
  return found != assistant_targets_.end() ? static_cast<views::WebView*>(found->second->view()) : nullptr;
}
bool WorkspaceWindow::AssistantStartBackground(const std::string& key, const std::string& from_tab, const GURL& url) {
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord() || key.empty() || assistant_background_.contains(key)) return false;
  AssistantBackgroundPage page;
  page.profile_id = AssistantProfileId(); page.workspace_id = active_id();
  if (!from_tab.empty()) {
    // Move the visible tab itself, keeping forms and history, out of the user's view.
    auto* view = AssistantTabView(from_tab);
    Host* source = nullptr;
    for (auto& host : hosts_) if (host.profile_id == page.profile_id && std::ranges::find(host.tabs, view) != host.tabs.end()) source = &host;
    if (!view || !source || source->tabs.size() < 2) return false;
    for (const auto& [target, tracker] : assistant_targets_) if (tracker->view() == view) page.alias = target;
    source->popup_tabs.erase(view);
    std::erase_if(source->pin_tabs, [&](const auto& item) { return item.second == view; });
    std::erase(source->tabs, view);
    auto* survivor = LeaveSplit(*source, view);
    source->view = survivor ? survivor : source->tabs[std::min(source->active_tab, source->tabs.size() - 1)].get();
    source->active_tab = std::find(source->tabs.begin(), source->tabs.end(), source->view) - source->tabs.begin();
    page.workspace_id = source->workspace_id;
    page.view = view;
    if (source->workspace_id == active_id()) SelectTab(source->active_tab);
    view->SetVisible(false);
  } else {
    page.view = CreatePage(profile);
  }
  auto* contents = EnsurePage(page.view);
  // Hidden, but sized like the page area so layout and visibility checks hold.
  page.view->SetBoundsRect(gfx::Rect(shell_->content_host()->size()));
  contents->Resize(gfx::Rect(shell_->content_host()->size()));
  page.rendering = contents->IncrementCapturerCount(shell_->content_host()->size(), /*stay_hidden=*/true, /*stay_awake=*/true, /*is_activity=*/false);
  contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
  if (from_tab.empty() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password())
    contents->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
  assistant_background_[key] = std::move(page);
  UpdateShell();
  return true;
}
bool WorkspaceWindow::AssistantTakeOver(const std::string& key) {
  auto found = assistant_background_.find(key);
  if (found == assistant_background_.end()) return false;
  auto page = std::move(found->second); assistant_background_.erase(found);
  page.rendering.RunAndReset();
  const auto target = model_.Find(page.workspace_id) ? page.workspace_id : active_id();
  if (!Select(target)) { assistant_background_[key] = std::move(page); return false; }
  for (auto& host : hosts_) if (host.workspace_id == target && host.profile_id == page.profile_id) {
    host.tabs.push_back(page.view.get());
    SelectTab(host.tabs.size() - 1);
    return true;
  }
  // The space now belongs to another profile: never show the page there.
  shell_->content_host()->RemoveChildViewT(page.view.get());
  return false;
}
void WorkspaceWindow::AssistantDiscardBackground(const std::string& key) {
  auto found = assistant_background_.find(key);
  if (found == assistant_background_.end()) return;
  auto page = std::move(found->second); assistant_background_.erase(found);
  page.rendering.RunAndReset();
  if (page.view && page.view->parent()) page.view->parent()->RemoveChildViewT(page.view.get());
  UpdateShell();
}
content::WebContents* WorkspaceWindow::assistant_background_for_testing(const std::string& key) const {
  const auto found = assistant_background_.find(key);
  return found == assistant_background_.end() ? nullptr : found->second.view->web_contents();
}
void WorkspaceWindow::AssistantElementBounds(std::string tab, std::string element,
    base::OnceCallback<void(std::optional<gfx::Rect>)> done) {
  auto* view = AssistantTabView(std::move(tab));
  auto* page = view && view->GetVisible() ? view->web_contents() : nullptr;
  if (!page) { std::move(done).Run(std::nullopt); return; }
  std::string script = kAssistantPageScript;
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS",
      base::WriteJson(base::DictValue().Set("tool", "rect").Set("element", element)).value_or("{}"));
  const double zoom = blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(page));
  auto tracker = std::make_unique<views::ViewTracker>(); tracker->SetView(view);
  page->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script), base::BindOnce(
      [](base::OnceCallback<void(std::optional<gfx::Rect>)> done, std::unique_ptr<views::ViewTracker> tracker, double zoom, base::Value value) {
        const auto* dict = value.GetIfDict();
        if (!dict || !tracker->view() || !dict->FindDouble("width")) { std::move(done).Run(std::nullopt); return; }
        gfx::Rect rect(std::lround(dict->FindDouble("x").value_or(0) * zoom), std::lround(dict->FindDouble("y").value_or(0) * zoom),
                       std::lround(dict->FindDouble("width").value_or(0) * zoom), std::lround(dict->FindDouble("height").value_or(0) * zoom));
        rect.Intersect(tracker->view()->GetLocalBounds());
        if (rect.IsEmpty()) { std::move(done).Run(std::nullopt); return; }
        views::View::ConvertRectToScreen(tracker->view(), &rect);
        std::move(done).Run(rect);
      }, std::move(done), std::move(tracker), zoom), kAssistantWorld);
}

Profile* WorkspaceWindow::AssistantProfile() const {
  return model_.active() ? ProfileFor(model_.active()->profile_id) : nullptr;
}
std::string WorkspaceWindow::AssistantProfileId() const {
  return model_.active() ? model_.active()->profile_id : std::string();
}
void WorkspaceWindow::ShowAssistant(bool hover) {
  if (!assistant_settings_.enabled) return;
  if (!assistant_) {
    auto panel = std::make_unique<AssistantPanel>(this);
    assistant_ = panel.get();
    shell_->SetAssistantView(panel.release());
  }
  assistant_->Show(hover);
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
                                    base::OnceCallback<void(base::Value)> done, bool user_confirmed, std::string background) {
  auto fail = [&](std::string text) { std::move(done).Run(Error(std::move(text))); };
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord()) { fail("Assistant is unavailable in private profiles"); return; }
  if (!assistant_settings_.enabled) { fail("Assistant is turned off in Settings"); return; }
  const AssistantBackgroundPage* hidden = nullptr;
  if (!background.empty()) {
    const auto found = assistant_background_.find(background);
    if (found == assistant_background_.end() || found->second.profile_id != AssistantProfileId()) { fail("Background page expired or belongs to another profile"); return; }
    hidden = &found->second;
  }
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
  // In the background the session's own page replaces @current, and the
  // user's visible tabs stay read-only.
  const auto* requested = args.FindString("tab");
  std::string requested_id = requested ? *requested : std::string();
  if (requested_id.starts_with("@tab:")) requested_id.erase(0, 5);
  const bool own_page = hidden && (requested_id.empty() || requested_id == "@current" || requested_id == "current" || requested_id == hidden->alias);
  if (hidden && (tool == "open_tab" || own_page)) {
    if (tool == "activate_tab" || tool == "close_tab" || tool == "pin_tab" || tool == "unpin_tab") { fail("The background page cannot be activated, closed or pinned; the user takes it over"); return; }
    auto* page = EnsurePage(hidden->view);
    if (tool == "open_tab" || tool == "navigate") {
      const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
      if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
      page->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
      std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("detail", "Loading in the background page (@current); read again when ready"))); return;
    }
    if (!page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() || page->IsCrashed()) { fail("Background page is not ready; navigate @current first"); return; }
    if (page->GetSize().IsEmpty()) page->Resize(gfx::Rect(shell_->content_host()->size()));
    args.Set("tool", tool);
    // This bit is native user consent, never a model-supplied argument.
    args.Set("confirmed", user_confirmed);
    std::string script = kAssistantPageScript;
    base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS", base::WriteJson(args).value_or("{}"));
    page->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script),
        base::BindOnce([](base::OnceCallback<void(base::Value)> done, base::Value value) { std::move(done).Run(std::move(value)); },
                       std::move(done)), kAssistantWorld);
    return;
  }
  if (hidden && mutation) { fail("In the background act only in @current; the user's visible tabs are read-only"); return; }
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
