#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/timeline_store.h"
#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/i18n.h"
#include "build/build_config.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "components/sessions/content/session_tab_helper.h"
#include "ui/views/controls/webview/webview.h"
#include "chromium/prototype/sidebar_scene.rs.h"

namespace zitter::prototype {
namespace {
std::u16string Fold(std::u16string text) {
  text = base::i18n::ToLower(text);
  constexpr std::u16string_view accents = u"ąćęłńóśźż";
  constexpr std::u16string_view ascii = u"acelnoszz";
  for (auto& letter : text) {
    const auto index = accents.find(letter);
    if (index != std::u16string_view::npos) letter = ascii[index];
  }
  return text;
}
}  // namespace

std::vector<PaletteResult> WorkspaceWindow::CommandResults(std::u16string query) const {
  std::vector<PaletteResult> results;
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return results;
  const auto source = active_id();
  auto* page = contents(source);
  const bool only = query.starts_with(u">");
  if (only) query.erase(0, 1);
  query = Fold(std::u16string(base::TrimWhitespace(query, base::TRIM_ALL)));
  // Binds an entry to the current page/tab and appends its configured chord.
  auto bind = [&](std::string id, std::u16string title, std::u16string detail, std::string target) {
    PaletteResult result;
    result.title = std::move(title); result.detail = std::move(detail);
    result.workspace_id = source; result.command_id = std::move(id); result.target_workspace_id = std::move(target);
    // Bind page actions to the selected tab, so a delayed click cannot act on a replacement.
    for (const auto& host : hosts_) if (host.workspace_id == source) result.tab = host.active_tab;
    if (page) {
      result.url = page->GetVisibleURL();
      result.command_page_id = sessions::SessionTabHelper::IdForTab(page).id();
    }
    for (const auto& shortcut : shell_->browser_tools().shortcuts) if (shortcut.id == result.command_id) {
      const auto chord = DisplayShortcut(shortcut.accelerator);
      if (!chord.empty()) result.detail += u" · " + base::UTF8ToUTF16(chord);
    }
    results.push_back(std::move(result));
  };
  auto has_closed = [&] {
    if (std::any_of(closed_tabs_.begin(), closed_tabs_.end(), [&](const auto& tab) { return tab.workspace_id == source; })) return true;
    return timeline_enabled_ && timeline_->current->timeline_last_closed(source, {}).valid;
  };
  // Rust owns the catalogue, wording and matching; native code supplies facts.
  sidebar::CommandContext context{};
  context.page = page; context.http_page = page && page->GetVisibleURL().SchemeIsHTTPOrHTTPS(); context.split = split();
  context.can_back = page && page->GetController().CanGoBack(); context.can_forward = page && page->GetController().CanGoForward();
  context.tab_count = tab_count(source); context.reopen = has_closed();
  context.assistant = assistant_settings_.enabled;
  for (const auto& workspace : model_.workspaces()) {
    if (workspace.id == source) continue;
    const auto pages = WorkspacePages(workspace.id);
    context.workspaces.push_back({workspace.id, workspace.name, workspace_sleeping(workspace.id), workspace.profile_id == model_.active()->profile_id,
        std::any_of(pages.begin(), pages.end(), [](auto* page) { return PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured(); })});
  }
  // Fetch all matching entries before applying learned order and the initial cap.
  std::u16string raw = u">" + query;
  for (const auto& entry : sidebar::command_catalogue(context, base::UTF16ToUTF8(raw)))
    bind(std::string(entry.id), base::UTF8ToUTF16(std::string(entry.title)), base::UTF8ToUTF16(std::string(entry.detail)), std::string(entry.target));
  const auto now = base::Time::Now();
  const auto& tools = shell_->browser_tools();
  // Score each candidate once, rather than repeating lookups and decay in
  // every sort comparison on the UI sequence. Equal scores retain catalogue order.
  std::vector<std::pair<double, PaletteResult>> ranked;
  ranked.reserve(results.size());
  for (auto& result : results) {
    const double score = tools.CommandScore(result.command_id, result.target_workspace_id, now);
    ranked.emplace_back(score, std::move(result));
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    return a.first > b.first;
  });
  results.clear();
  for (auto& entry : ranked) results.push_back(std::move(entry.second));
  if (!only && query.empty() && results.size() > 6) results.resize(6);
  return results;
}

bool WorkspaceWindow::ExecuteCommand(const PaletteResult& result, bool record_usage) {
  if (result.workspace_id != active_id() || result.command_id.empty()) return false;
  const auto available = CommandResults(u">");
  const auto found = std::find_if(available.begin(), available.end(), [&](const auto& current) {
    return current.command_id == result.command_id && current.target_workspace_id == result.target_workspace_id &&
        current.tab == result.tab && current.url == result.url && current.command_page_id == result.command_page_id;
  });
  if (found == available.end()) return false;
  if (record_usage) shell_->RecordCommandUse(result.command_id, result.target_workspace_id);
  const auto& id = result.command_id;
  if (id == "assistant") { ShowAssistant(); return true; }
  if (id == "assistant_settings") { shell_->OpenAssistantSettings(); return true; }
  if (id == "shortcuts" || id == "engines") { shell_->OpenTools(id == "shortcuts" ? 1 : 2); return true; }
  if (id == "spaces") { shell_->OpenWorkspaceSettings(); return true; }
  if (id == "profiles") { shell_->OpenProfiles(); return true; }
  if (id == "import") { shell_->OpenImport(); return true; }
  if (id == "downloads") { ShowDownloads(); return true; }
  if (id == "history") { ShowHistory(); return true; }
  if (id == "passwords") { ShowPasswords(); return true; }
  if (id == "save_snapshot") {
    if (!SaveSnapshot("Command Center")) return false;
    shell_->OpenTimeMachine(); return true;
  }
  if (id == "select_space") return Select(result.target_workspace_id);
  if (id == "move_tab") return MoveActiveTab(result.target_workspace_id);
  if (id == "copy_tab") {
    // Explicit target, bypass domain routing: the command names the destination profile.
    if (DeferForProfile(result.target_workspace_id, base::BindOnce(
        [](base::WeakPtr<WorkspaceWindow> w, PaletteResult result) { if (w) w->ExecuteCommand(result, false); },
        weak_factory_.GetWeakPtr(), result))) return true;
    if (!Select(result.target_workspace_id)) return false;
    for (auto& host : hosts_) if (host.workspace_id == active_id()) {
      auto* view = CreatePage(host.profile); host.tabs.push_back(view);
      host.deferred_urls[view] = result.url;
      SelectTab(host.tabs.size() - 1); return true;
    }
    return false;
  }
  if (id == "sleep_space") return SleepWorkspace(result.target_workspace_id);
  if (id == "sleep_others") {
    std::vector<content::WebContents*> pages;
    for (const auto& workspace : model_.workspaces()) if (workspace.id != active_id()) {
      auto space_pages = WorkspacePages(workspace.id);
      if (std::any_of(space_pages.begin(), space_pages.end(), [](auto* page) { return PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured(); })) continue;
      pages.insert(pages.end(), space_pages.begin(), space_pages.end());
    }
    if (GuardPages(std::move(pages), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, PaletteResult result) { if (w) w->ExecuteCommand(result, false); }, weak_factory_.GetWeakPtr(), result))) return true;
    const auto workspaces = model_.workspaces(); bool changed = false;
    for (const auto& workspace : workspaces) if (workspace.id != active_id() && !workspace_sleeping(workspace.id))
      changed |= SleepWorkspace(workspace.id);
    return changed;
  }
  return RunShortcut(id);
}

bool WorkspaceWindow::MoveActiveTab(std::string target_id) {
  if (!configuration_ready_ || profiles_busy_ || target_id == active_id()) return false;
  Host* source = nullptr; Host* target = nullptr;
  for (auto& host : hosts_) {
    if (host.workspace_id == active_id()) source = &host;
    if (host.workspace_id == target_id) target = &host;
  }
  if (!source || !target || source->profile != target->profile || !source->view) return false;
  if (target->sleeping) {
    const auto stored = target->sleeping->Clone();
    if (!RestoreHost(*target, stored)) return false;
  }
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string target) { if (w) w->MoveActiveTab(target); }, weak_factory_.GetWeakPtr(), target_id))) return true;
  ClosePeek();
  RecordTimeline(true, active_id());
  auto* moving = source->view.get();
  shell_->SetPanes({}, {});
  const auto pending = source->deferred_urls.find(moving);
  if (pending != source->deferred_urls.end()) { target->deferred_urls[moving] = pending->second; source->deferred_urls.erase(pending); }
  if (auto title = source->deferred_titles.find(moving); title != source->deferred_titles.end()) {
    target->deferred_titles[moving] = std::move(title->second);
    source->deferred_titles.erase(title);
  }
  source->popup_tabs.erase(moving);
  std::erase_if(source->pin_tabs, [&](const auto& item) { return item.second == moving; });
  std::erase(source->tabs, moving);
  if (source->tabs.empty()) source->tabs.push_back(CreatePage(source->profile));
  auto* survivor = LeaveSplit(*source, moving);
  source->view = survivor ? survivor : source->tabs.front().get();
  source->active_tab = std::find(source->tabs.begin(), source->tabs.end(), source->view) - source->tabs.begin();
  // Keep an existing split, replacing only its focused pane.
  if (SplitShown(*target)) *std::find(target->split.begin(), target->split.end(), target->view) = moving;
  target->tabs.push_back(moving);
  target->active_tab = target->tabs.size() - 1;
  target->view = moving;
  return Select(target_id);
}
}  // namespace zitter::prototype
