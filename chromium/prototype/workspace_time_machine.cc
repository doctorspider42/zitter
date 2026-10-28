#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/page_media_access.h"
#include <algorithm>
#include <cmath>
#include "base/strings/string_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/i18n/time_formatting.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
namespace zitter::prototype {
namespace {
std::string Stamp() { return base::UTF16ToUTF8(base::TimeFormatShortDateAndTime(base::Time::Now())); }
}
base::DictValue WorkspaceWindow::CaptureHost(const Host& host) const {
  if (host.sleeping) return host.sleeping->Clone();
  base::DictValue entry;
  entry.Set("profile_id", host.profile_id); entry.Set("ratio", host.ratio);
  if (host.right && host.vertical) entry.Set("vertical", true);
  entry.Set("active", static_cast<int>(host.active_tab));
  int left = host.active_tab, right = -1; base::ListValue tabs;
  for (size_t i = 0; i < host.tabs.size(); ++i) {
    auto* view = host.tabs[i].get();
    if (view == host.left) left = i;
    if (view == host.right) right = i;
    const GURL url = TabUrl(host, view);
    base::DictValue tab;
    tab.Set("url", url.SchemeIsHTTPOrHTTPS() ? url.spec() : "");
    tab.Set("title", base::UTF16ToUTF8(TabTitle(host, view)));
    for (const auto& [id, pinned] : host.pin_tabs) if (pinned == view) tab.Set("pin", id);
    tabs.Append(std::move(tab));
  }
  entry.Set("left", left); entry.Set("right", right); entry.Set("tabs", std::move(tabs));
  return entry;
}
void WorkspaceWindow::ReleaseHost(Host& host) {
  const bool was_closing = closing_; closing_ = true;
  if (host.workspace_id == active_id()) shell_->SetPanes(nullptr, nullptr, 0.5);
  host.view = nullptr; host.left = nullptr; host.right = nullptr;
  host.pin_tabs.clear(); host.deferred_urls.clear(); host.deferred_titles.clear(); host.popup_tabs.clear();
  auto tabs = std::move(host.tabs); host.tabs.clear();
  for (const auto& view : tabs) shell_->content_host()->RemoveChildViewT(view.get());
  closing_ = was_closing;
}
bool WorkspaceWindow::RestoreHost(Host& host, const base::DictValue& entry) {
  const auto* profile = entry.FindString("profile_id"); const auto* tabs = entry.FindList("tabs");
  if (!profile || *profile != host.profile_id || !tabs || tabs->empty() || tabs->size() > 500) return false;
  for (const auto& value : *tabs) {
    const auto* tab = value.GetIfDict(); if (!tab) return false;
    const auto* url = tab->FindString("url");
    if (!url || url->size() > 32768 || (!url->empty() && !GURL(*url).SchemeIsHTTPOrHTTPS())) return false;
  }
  const bool was_closing = closing_; closing_ = true;
  ReleaseHost(host); host.sleeping.reset();
  for (const auto& value : *tabs) {
    const auto& tab = value.GetDict();
    auto* view = CreatePage(host.profile, nullptr, nullptr, false); host.tabs.push_back(view);
    const auto& url = *tab.FindString("url");
    if (!url.empty()) host.deferred_urls[view] = GURL(url);
    if (const auto* title = tab.FindString("title"); title && title->size() <= 32768)
      host.deferred_titles[view] = base::UTF8ToUTF16(*title);
    if (const auto* pin = tab.FindString("pin")) {
      const auto& pins = pinned_items(host.workspace_id);
      if (std::any_of(pins.begin(), pins.end(), [&](const auto& item) { return item.id == *pin && !item.folder; })) host.pin_tabs[*pin] = view;
    }
  }
  const int count = host.tabs.size();
  const int left = std::clamp(entry.FindInt("left").value_or(0), 0, count - 1);
  const int right = entry.FindInt("right").value_or(-1);
  host.left = host.tabs[left]; host.right = right >= 0 && right < count && right != left ? host.tabs[right].get() : nullptr;
  size_t active = entry.FindInt("active").value_or(left);
  if (active >= host.tabs.size()) active = left;
  host.active_tab = active; host.view = host.tabs[active];
  const double ratio = entry.FindDouble("ratio").value_or(0.5);
  host.ratio = std::isfinite(ratio) ? std::clamp(ratio, 0.2, 0.8) : 0.5;
  host.vertical = host.right && entry.FindBool("vertical").value_or(false);
  closing_ = was_closing; return true;
}
bool WorkspaceWindow::SaveSnapshot(std::string name, bool automatic) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_ || restoring_session_ || closing_) return false;
  name = base::TrimWhitespaceASCII(name, base::TRIM_ALL);
  if (name.empty() || name.size() > 160) return false;
  for (const auto& host : hosts_) if (host.workspace_id == active_id()) {
    auto snapshot = CaptureHost(host);
    snapshot.Set("id", base::Uuid::GenerateRandomV4().AsLowercaseString());
    snapshot.Set("name", name); snapshot.Set("time", Stamp()); snapshot.Set("automatic", automatic);
    base::ListValue pins;
    for (const auto& pin : pinned_items(host.workspace_id)) {
      base::DictValue value; value.Set("id", pin.id); value.Set("parent_id", pin.parent_id);
      value.Set("title", pin.title); value.Set("url", pin.url.spec()); value.Set("folder", pin.folder); pins.Append(std::move(value));
    }
    snapshot.Set("pins", std::move(pins));
    auto* space = time_machine_.EnsureDict(host.workspace_id);
    auto* list = space->EnsureList("snapshots");
    list->EraseIf([](const auto& item) { return !item.is_dict(); });
    list->Append(std::move(snapshot));
    size_t same_kind = 0;
    for (const auto& item : *list) if (item.GetDict().FindBool("automatic").value_or(false) == automatic) ++same_kind;
    while (same_kind > 30) {
      auto oldest = std::find_if(list->begin(), list->end(), [&](const auto& item) { return item.GetDict().FindBool("automatic").value_or(false) == automatic; });
      list->erase(oldest); --same_kind;
    }
    if (automatic) last_automatic_snapshot_ = base::Time::Now();
    SaveTimeMachine(); return true;
  }
  return false;
}
bool WorkspaceWindow::RestoreSnapshot(std::string id) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_) return false;
  const auto* space = time_machine_.FindDict(active_id());
  const auto* list = space ? space->FindList("snapshots") : nullptr;
  if (!list) return false;
  const base::DictValue* found = nullptr;
  for (const auto& item : *list) if (item.is_dict() && item.GetDict().FindString("id") && *item.GetDict().FindString("id") == id) found = &item.GetDict();
  if (!found || !found->FindString("profile_id") || *found->FindString("profile_id") != model_.active()->profile_id) return false;
  auto snapshot = found->Clone();
  std::vector<PinnedItem> pins; std::set<std::string> ids;
  const auto* stored_pins = snapshot.FindList("pins"); if (!stored_pins || stored_pins->size() > 5000) return false;
  for (const auto& item : *stored_pins) {
    const auto* pin = item.GetIfDict(); if (!pin) return false;
    const auto* pin_id = pin->FindString("id"); const auto* parent = pin->FindString("parent_id");
    const auto* title = pin->FindString("title"); const auto* url = pin->FindString("url");
    const bool folder = pin->FindBool("folder").value_or(false);
    if (!pin_id || pin_id->empty() || !ids.insert(*pin_id).second || !parent || !title || !url || (!folder && !GURL(*url).SchemeIsHTTPOrHTTPS())) return false;
    pins.push_back({*pin_id, *parent, *title, GURL(*url), folder});
  }
  if (GuardPages(WorkspacePages(active_id(), true, true), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->RestoreSnapshot(id); }, weak_factory_.GetWeakPtr(), id))) return true;
  ClosePeek(); SaveSnapshot("Przed przywróceniem układu", true);
  for (auto& host : hosts_) if (host.workspace_id == active_id()) {
    // Validate the page payload before modifying the pinned catalog.
    const auto previous = pinned_items(active_id());
    pins_[active_id()] = std::move(pins);
    if (!RestoreHost(host, snapshot)) { pins_[active_id()] = previous; return false; }
    RefreshPanes(); UpdateShell(); SaveConfiguration(); SaveSession(); return true;
  }
  return false;
}
bool WorkspaceWindow::DeleteSnapshot(std::string id) {
  auto* space = time_machine_.FindDict(active_id()); auto* list = space ? space->FindList("snapshots") : nullptr;
  if (!list) return false;
  const size_t count = list->size();
  list->EraseIf([&](const auto& item) { return item.is_dict() && item.GetDict().FindString("id") && *item.GetDict().FindString("id") == id; });
  if (count == list->size()) return false;
  SaveTimeMachine(); return true;
}
void WorkspaceWindow::RecordClosedTab(const Host& host, size_t index) {
  auto entry = CaptureHost(host); auto* tabs = entry.FindList("tabs");
  if (!tabs || index >= tabs->size()) return;
  auto tab = (*tabs)[index].GetDict().Clone();
  tab.Set("id", base::Uuid::GenerateRandomV4().AsLowercaseString()); tab.Set("profile_id", host.profile_id); tab.Set("time", Stamp());
  auto* closed = time_machine_.EnsureDict(host.workspace_id)->EnsureList("closed"); closed->Append(std::move(tab));
  while (closed->size() > 100) closed->erase(closed->begin());
  SaveTimeMachine();
}
bool WorkspaceWindow::ReopenClosedTab(std::string id) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_) return false;
  auto* space = time_machine_.FindDict(active_id()); auto* closed = space ? space->FindList("closed") : nullptr;
  if (!closed || closed->empty()) return false;
  for (size_t i = closed->size(); i > 0; --i) {
    const auto* tab = (*closed)[i - 1].GetIfDict(); if (!tab) continue;
    const auto* tab_id = tab->FindString("id"); const auto* profile = tab->FindString("profile_id"); const auto* url = tab->FindString("url");
    if (!tab_id || (!id.empty() && *tab_id != id) || !profile || *profile != model_.active()->profile_id || !url || (!url->empty() && !GURL(*url).SchemeIsHTTPOrHTTPS())) continue;
    const GURL destination(*url); const std::string pin = tab->FindString("pin") ? *tab->FindString("pin") : "";
    for (auto& host : hosts_) if (host.workspace_id == active_id()) {
      auto* view = CreatePage(host.profile); host.tabs.push_back(view);
      if (destination.SchemeIsHTTPOrHTTPS()) host.deferred_urls[view] = destination;
      if (!pin.empty() && !host.pin_tabs.contains(pin) && std::any_of(pinned_items(active_id()).begin(), pinned_items(active_id()).end(), [&](const auto& item) { return item.id == pin && !item.folder; })) host.pin_tabs[pin] = view;
      closed->erase(closed->begin() + (i - 1)); SaveTimeMachine(); SelectTab(host.tabs.size() - 1); SaveSession(); return true;
    }
  }
  return false;
}
bool WorkspaceWindow::workspace_sleeping(std::string_view id) const {
  for (const auto& host : hosts_) if (host.workspace_id == id) return host.sleeping.has_value();
  return false;
}
bool WorkspaceWindow::SleepWorkspace(std::string id) {
  const auto discarded_pages = WorkspacePages(id, true, id == active_id());
  for (auto* page : discarded_pages) if (PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured()) {
    shell_->SetOperationStatus(WorkspaceShell::OperationStatusEvent::kStopCapture); return false;
  }
  if (GuardPages(discarded_pages, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->SleepWorkspace(id); }, weak_factory_.GetWeakPtr(), id))) return true;
  if (!configuration_ready_ || profiles_busy() || configuration_invalid_ || model_.workspaces().size() < 2) return false;
  if (id == active_id()) {
    SaveSnapshot("Przed uśpieniem przestrzeni", true);
    const auto other = std::find_if(model_.workspaces().begin(), model_.workspaces().end(), [&](const auto& space) { return space.id != id; });
    if (other == model_.workspaces().end() || !Select(other->id)) return false;
  }
  if (peek_workspace_id_ == id) ClosePeek();
  for (auto& host : hosts_) if (host.workspace_id == id && !host.sleeping) {
    auto stored = CaptureHost(host); stored.Set("sleeping", true);
    ReleaseHost(host); host.sleeping = std::move(stored);
    SaveSession(); UpdateShell(); return true;
  }
  return false;
}
TimeMachineState WorkspaceWindow::TimeMachine() const {
  TimeMachineState result;
  if (!model_.active()) return result;
  const auto* space = time_machine_.FindDict(active_id());
  auto fill = [&](const char* key, std::vector<TimeMachineItem>& output) {
    const auto* list = space ? space->FindList(key) : nullptr; if (!list) return;
    for (auto it = list->rbegin(); it != list->rend(); ++it) {
      const auto* entry = it->GetIfDict(); if (!entry) continue;
      const auto* id = entry->FindString("id"); if (!id) continue;
      const auto* name = entry->FindString(key == std::string_view("closed") ? "title" : "name");
      const auto* url = entry->FindString("url"); const auto* time = entry->FindString("time");
      sidebar::TimeMachineFacts facts{};
      if (name) facts.name = *name;
      if (url) facts.url = *url;
      if (time) facts.time = *time;
      const auto* tabs = entry->FindList("tabs"); facts.tabs = tabs ? static_cast<int64_t>(tabs->size()) : -1;
      facts.automatic = entry->FindBool("automatic").value_or(false);
      const auto* profile = entry->FindString("profile_id"); facts.other_profile = profile && *profile != model_.active()->profile_id;
      const auto copy = sidebar::time_machine_entry(facts);
      output.push_back({*id, base::UTF8ToUTF16(std::string(copy.title)), base::UTF8ToUTF16(std::string(copy.detail))});
    }
  };
  fill("snapshots", result.snapshots); fill("closed", result.closed);
  for (const auto& host : hosts_) if (host.sleeping) {
    const auto* workspace = model_.Find(host.workspace_id);
    if (!workspace) continue;
    auto detail = base::UTF8ToUTF16(std::string(sidebar::time_machine_sleeping_detail()));
    result.sleeping.push_back({workspace->id, base::UTF8ToUTF16(workspace->name), std::move(detail)});
  }
  return result;
}
void WorkspaceWindow::SaveTimeMachine() {
  // CaptureHost already took the pre-change snapshot synchronously. Batch disk
  // payloads without deferring that capture or serializing history on the UI.
  if (!time_machine_timer_.IsRunning())
    time_machine_timer_.Start(FROM_HERE, base::Milliseconds(350),
        base::BindOnce(&WorkspaceWindow::FlushTimeMachine, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::FlushTimeMachine() {
  time_machine_timer_.Stop();
  ++pending_config_writes_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, base::DictValue timeline) {
    std::string json;
    return base::JSONWriter::Write(timeline, &json) &&
        base::ImportantFileWriter::WriteFileAtomically(path, json);
  }, data_directory_.AppendASCII("ZitterTimeMachine.json"), time_machine_.Clone()), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, bool ok) {
    if (!window) return;
    --window->pending_config_writes_; if (!ok) window->shell_->SetConfigurationError(true);
  }, weak_factory_.GetWeakPtr()));
}
}  // namespace zitter::prototype
