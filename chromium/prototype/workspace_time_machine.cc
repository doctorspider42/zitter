#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/rss_reader.h"
#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/i18n.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include "base/strings/string_util.h"
#include "base/files/file.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/strings/string_split.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/timeline_store.h"
#include "chromium/prototype/workspace_session.h"
namespace zitter::prototype {
base::DictValue WorkspaceWindow::CaptureHost(const Host& host) const {
  if (host.sleeping) return host.sleeping->Clone();
  base::DictValue entry;
  entry.Set("profile_id", host.profile_id);
  if (!host.split.empty() && host.vertical) entry.Set("vertical", true);
  entry.Set("active", static_cast<int>(host.active_tab));
  base::ListValue tabs, split, weights;
  for (const auto& member : host.split)
    split.Append(static_cast<int>(std::find(host.tabs.begin(), host.tabs.end(), member) - host.tabs.begin()));
  for (double weight : host.weights) weights.Append(weight);
  for (size_t i = 0; i < host.tabs.size(); ++i) {
    auto* view = host.tabs[i].get();
    auto tab = WriteWorkspaceSessionTab(TabUrl(host, view), TabTitle(host, view));
    if (!CustomTabTitle(view).empty())
      tab.Set("custom_title", base::UTF16ToUTF8(CustomTabTitle(view)));
    for (const auto& [id, pinned] : host.pin_tabs) if (pinned == view) tab.Set("pin", id);
    tabs.Append(std::move(tab));
  }
  entry.Set("split", std::move(split)); entry.Set("weights", std::move(weights)); entry.Set("tabs", std::move(tabs));
  return entry;
}
void WorkspaceWindow::ReleaseHost(Host& host) {
  const bool was_closing = closing_; closing_ = true;
  if (host.workspace_id == active_id()) shell_->SetPanes({}, {});
  host.view = nullptr; host.split.clear(); host.weights.clear();
  host.pin_tabs.clear(); host.deferred_urls.clear(); host.deferred_titles.clear(); host.popup_tabs.clear();
  auto tabs = std::move(host.tabs); host.tabs.clear();
  for (const auto& view : tabs) shell_->content_host()->RemoveChildViewT(view.get());
  closing_ = was_closing;
}
bool WorkspaceWindow::RestoreHost(Host& host, const base::DictValue& entry) {
  auto tabs = ReadWorkspaceSessionTabs(entry, host.profile_id);
  if (!tabs) return false;
  const bool was_closing = closing_; closing_ = true;
  ReleaseHost(host); host.sleeping.reset();
  for (auto& tab : *tabs) {
    auto* view = CreatePage(host.profile, nullptr, nullptr, false); host.tabs.push_back(view);
    if (!tab.url.is_empty()) host.deferred_urls[view] = std::move(tab.url);
    if (tab.title) host.deferred_titles[view] = std::move(*tab.title);
    if (tab.custom_title) SetCustomTabTitle(view, std::move(*tab.custom_title));
    if (tab.pin) {
      const auto& pins = pinned_items(host.workspace_id);
      if (std::any_of(pins.begin(), pins.end(), [&](const auto& item) { return item.id == *tab.pin && !item.folder; })) host.pin_tabs[*tab.pin] = view;
    }
  }
  const int count = host.tabs.size();
  // Earlier snapshots stored a left/right pair and the left pane's ratio.
  std::vector<int> members;
  std::vector<double> weights;
  if (const auto* split = entry.FindList("split")) {
    for (const auto& value : *split) if (value.is_int()) members.push_back(value.GetInt());
    if (const auto* stored = entry.FindList("weights"))
      for (const auto& value : *stored) if (value.is_double() || value.is_int()) weights.push_back(value.GetDouble());
  } else if (const int right = entry.FindInt("right").value_or(-1); right >= 0) {
    const double ratio = entry.FindDouble("ratio").value_or(0.5);
    members = {entry.FindInt("left").value_or(0), right};
    weights = {ratio, 1.0 - ratio};
  }
  std::set<int> seen;
  for (int member : members) {
    if (member < 0 || member >= count || !seen.insert(member).second || host.split.size() == kMaxSplitPanes) { host.split.clear(); break; }
    host.split.push_back(host.tabs[member]);
  }
  if (host.split.size() < 2) host.split.clear();
  const auto normalized = sidebar::split_normalized(rust::Slice<const double>(weights.data(), weights.size()), host.split.size());
  host.weights.assign(normalized.begin(), normalized.end());
  const int fallback = host.split.empty() ? 0 : members.front();
  size_t active = entry.FindInt("active").value_or(fallback);
  if (active >= host.tabs.size()) active = fallback;
  host.active_tab = active; host.view = host.tabs[active];
  host.vertical = !host.split.empty() && entry.FindBool("vertical").value_or(false);
  closing_ = was_closing; return true;
}
namespace {
// One JSON object per line. Frames: {"t","w","p","a","tabs":[[url,title]...]}, plus
// "r":true when the line replaces the workspace's previous frame. Marks: {"t","w","m"}.
// Times are milliseconds since the Unix epoch, exact as JSON doubles.
std::string FrameLine(const sidebar::TimelineFrame& frame, bool replace) {
  base::DictValue line;
  line.Set("t", static_cast<double>(frame.t_ms)); line.Set("w", std::string(frame.workspace));
  line.Set("p", std::string(frame.profile)); line.Set("a", frame.active);
  base::ListValue tabs;
  for (const auto& tab : frame.tabs) tabs.Append(base::ListValue().Append(std::string(tab.url)).Append(std::string(tab.title)));
  line.Set("tabs", std::move(tabs));
  if (replace) line.Set("r", true);
  std::string json; base::JSONWriter::Write(line, &json); return json + "\n";
}
std::string MarkLine(const sidebar::TimelineMark& mark) {
  base::DictValue line;
  line.Set("t", static_cast<double>(mark.t_ms)); line.Set("w", std::string(mark.workspace)); line.Set("m", std::string(mark.name));
  std::string json; base::JSONWriter::Write(line, &json); return json + "\n";
}
int64_t Milliseconds(base::Time time) { return time.InMillisecondsSinceUnixEpoch(); }
// Retention applies once the shell has read its settings; until then keep the
// longest choice so an early prune never drops what the person chose to keep.
constexpr int kLongestRetentionDays = 90;
constexpr size_t kTimelineMaxBytes = 20 * 1024 * 1024;
}  // namespace

const sidebar::Timeline& WorkspaceWindow::timeline() const { return *timeline_->current; }
WorkspaceWindow::TimelineMomentForTesting WorkspaceWindow::timeline_moment_for_testing(std::string_view workspace_id, int64_t t_ms, int64_t quantum_ms) const {
  const auto moment = timeline_->current->timeline_query(std::string(workspace_id), t_ms, quantum_ms);
  TimelineMomentForTesting result;
  for (const auto& tab : moment.tabs) result.urls.emplace_back(std::string(tab.url));
  result.open = std::count(moment.open_at_t.begin(), moment.open_at_t.end(), true);
  return result;
}
std::vector<std::string> WorkspaceWindow::timeline_marks_for_testing() const {
  std::vector<std::string> names;
  for (const auto& mark : timeline_->current->timeline_marks()) names.emplace_back(std::string(mark.name));
  return names;
}
size_t WorkspaceWindow::timeline_frames_for_testing() const { return timeline_->current->timeline_stats().frames; }
bool WorkspaceWindow::timeline_recorded_for_testing(std::string_view workspace_id) const {
  return timeline_->current->timeline_bounds(std::string(workspace_id)).valid;
}
void WorkspaceWindow::LoadTimeline(std::string data) {
  size_t lines = 0;
  for (const auto piece : base::SplitStringPiece(data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    ++lines;
    const auto line = base::JSONReader::ReadDict(piece, base::JSON_PARSE_RFC);
    if (!line) continue;
    const auto t = line->FindDouble("t"); const auto* workspace = line->FindString("w");
    if (!t || !workspace) continue;
    if (const auto* name = line->FindString("m")) {
      timeline_->current->timeline_mark({static_cast<int64_t>(*t), *workspace, *name});
      continue;
    }
    const auto* profile = line->FindString("p"); const auto* tabs = line->FindList("tabs");
    if (!profile || !tabs) continue;
    sidebar::TimelineFrame frame{static_cast<int64_t>(*t), *workspace, *profile, line->FindInt("a").value_or(-1), {}};
    for (const auto& value : *tabs) {
      const auto* pair = value.GetIfList();
      if (!pair || pair->size() != 2 || !(*pair)[0].is_string() || !(*pair)[1].is_string()) continue;
      frame.tabs.push_back({(*pair)[0].GetString(), (*pair)[1].GetString()});
    }
    timeline_->current->timeline_load(frame, line->FindBool("r").value_or(false));
  }
  const int days = timeline_settings_known_ ? timeline_days_ : kLongestRetentionDays;
  const bool pruned = timeline_->current->timeline_prune(Milliseconds(Now() - base::Days(days)), kTimelineMaxBytes);
  const auto stats = timeline_->current->timeline_stats();
  // Compact replaced, broken or expired lines away.
  if (pruned || lines != stats.frames + stats.marks) RewriteTimeline();
}
void WorkspaceWindow::AppendTimeline(std::string lines) {
  if (is_private()) return;
  ++pending_config_writes_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, std::string lines) {
    base::File file(path, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
    return file.IsValid() && file.WriteAtCurrentPosAndCheck(base::as_byte_span(lines));
  }, data_directory_.AppendASCII("ZitterTimeline.jsonl"), std::move(lines)), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, bool ok) {
    if (!window) return;
    --window->pending_config_writes_; if (!ok) window->shell_->SetConfigurationError(true);
  }, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::RewriteTimeline() {
  if (is_private()) return;
  // Marks first: loading replays frames and marks independently.
  std::string data;
  for (const auto& mark : timeline_->current->timeline_marks()) data += MarkLine(mark);
  for (const auto& frame : timeline_->current->timeline_frames()) data += FrameLine(frame, false);
  ++pending_config_writes_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, std::string data) {
    return base::ImportantFileWriter::WriteFileAtomically(path, data);
  }, data_directory_.AppendASCII("ZitterTimeline.jsonl"), std::move(data)), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, bool ok) {
    if (!window) return;
    --window->pending_config_writes_; if (!ok) window->shell_->SetConfigurationError(true);
  }, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::RecordTimeline(bool force, std::string_view workspace_id) {
  if (is_private()) return;
  if (!timeline_enabled_ || !configuration_ready_ || configuration_invalid_ || restoring_session_ || profiles_busy_) return;
  std::string lines;
  const int64_t now = Milliseconds(Now());
  for (const auto& host : hosts_) {
    if (host.sleeping || (!workspace_id.empty() && host.workspace_id != workspace_id)) continue;
    // Private profiles leave no history.
    if (!host.profile || host.profile->IsOffTheRecord()) continue;
    sidebar::TimelineFrame frame{now, host.workspace_id, host.profile_id, static_cast<int32_t>(host.active_tab), {}};
    for (const auto& view : host.tabs)
      frame.tabs.push_back({TabUrl(host, view).spec(), base::UTF16ToUTF8(TabTitle(host, view))});
    const auto record = timeline_->current->timeline_record(frame, force);
    if (record != sidebar::TimelineRecord::Skip) lines += FrameLine(frame, record == sidebar::TimelineRecord::Replace);
  }
  if (!lines.empty()) AppendTimeline(std::move(lines));
}
void WorkspaceWindow::SetTimelineSettings(bool enabled, int days) {
  timeline_enabled_ = enabled; timeline_days_ = std::clamp(days, 1, kLongestRetentionDays); timeline_settings_known_ = true;
  if (timeline_->current->timeline_prune(Milliseconds(Now() - base::Days(timeline_days_)), kTimelineMaxBytes)) RewriteTimeline();
  if (enabled) RecordTimeline(false);
}
bool WorkspaceWindow::ClearTimeline() {
  // Kept in memory until the toast's undo expires with the next clear or exit.
  timeline_->cleared = std::exchange(timeline_->current, sidebar::new_timeline());
  closed_tabs_.clear();
  RewriteTimeline(); RecordTimeline(true);
  shell_->ShowToast(T(u"Wyczyszczono "), T(u"historię kart"), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w) {
    if (!w || !w->timeline_->cleared) return;
    w->timeline_->current = std::move(*w->timeline_->cleared); w->timeline_->cleared.reset();
    w->RewriteTimeline();
  }, weak_factory_.GetWeakPtr()));
  return true;
}
bool WorkspaceWindow::SaveSnapshot(std::string name) {
  if (!timeline_enabled_ || !configuration_ready_ || profiles_busy_ || configuration_invalid_ || restoring_session_ || closing_) return false;
  name = base::TrimWhitespaceASCII(name, base::TRIM_ALL);
  if (name.empty() || name.size() > 160 || !model_.active()) return false;
  RecordTimeline(true, active_id());
  const sidebar::TimelineMark mark{Milliseconds(Now()), active_id(), name};
  if (!timeline_->current->timeline_mark(mark)) return false;
  AppendTimeline(MarkLine(mark));
  shell_->ShowToast(T(u"Zapisano snapshot "), base::UTF8ToUTF16(name), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w) {
    if (w) w->shell_->OpenTimeMachine();
  }, weak_factory_.GetWeakPtr()), T(u"Pokaż"));
  return true;
}
bool WorkspaceWindow::RestoreTimeline(int64_t t_ms, int64_t quantum_ms) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_ || !model_.active()) return false;
  const auto moment = timeline_->current->timeline_query(active_id(), t_ms, quantum_ms);
  if (moment.tabs.empty() || std::string(moment.profile) != model_.active()->profile_id) return false;
  if (GuardPages(WorkspacePages(active_id(), true, true), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, int64_t t_ms, int64_t quantum_ms) {
    if (w) w->RestoreTimeline(t_ms, quantum_ms);
  }, weak_factory_.GetWeakPtr(), t_ms, quantum_ms))) return true;
  ClosePeek();
  for (auto& host : hosts_) if (host.workspace_id == active_id()) {
    // The state being replaced joins the timeline, and its exact layout (splits
    // included) is what the toast's undo brings back.
    RecordTimeline(true, host.workspace_id);
    auto undo = CaptureHost(host);
    base::DictValue entry;
    entry.Set("profile_id", host.profile_id); entry.Set("active", std::max(0, static_cast<int>(moment.active)));
    base::ListValue tabs;
    for (const auto& tab : moment.tabs) tabs.Append(base::DictValue().Set("url", std::string(tab.url)).Set("title", std::string(tab.title)));
    entry.Set("tabs", std::move(tabs));
    if (!RestoreHost(host, entry)) return false;
    RefreshPanes(); UpdateShell(); SaveSession();
    const auto copy = sidebar::timeline_clock(t_ms, Milliseconds(Now()), TimelineUtcOffsetMinutes(base::Time::FromMillisecondsSinceUnixEpoch(t_ms)), moment.tabs.size(), quantum_ms);
    shell_->ShowToast(T(u"Przywrócono karty z: "), base::UTF8ToUTF16(std::string(copy.handle)), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, base::DictValue undo) {
      if (w) w->RestoreLayout(std::move(id), std::move(undo));
    }, weak_factory_.GetWeakPtr(), host.workspace_id, std::move(undo)));
    return true;
  }
  return false;
}
bool WorkspaceWindow::RestoreLayout(std::string workspace_id, base::DictValue entry) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_) return false;
  if (GuardPages(WorkspacePages(workspace_id, true, workspace_id == active_id()), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, base::DictValue entry) {
    if (w) w->RestoreLayout(std::move(id), std::move(entry));
  }, weak_factory_.GetWeakPtr(), workspace_id, entry.Clone()))) return true;
  for (auto& host : hosts_) if (host.workspace_id == workspace_id) {
    RecordTimeline(true, workspace_id);
    if (!RestoreHost(host, entry)) return false;
    RefreshPanes(); UpdateShell(); SaveSession(); return true;
  }
  return false;
}
void WorkspaceWindow::RecordClosedTab(const Host& host, size_t index) {
  const GURL url = TabUrl(host, host.tabs[index]);
  if (!IsWorkspaceTabUrl(url) || !host.profile || host.profile->IsOffTheRecord()) return;
  closed_tabs_.push_back({host.workspace_id, host.profile_id, url, TabTitle(host, host.tabs[index]), base::Time::Now()});
  if (closed_tabs_.size() > 50) closed_tabs_.erase(closed_tabs_.begin());
}
bool WorkspaceWindow::ReopenClosedTab() {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_ || !model_.active()) return false;
  for (auto& host : hosts_) if (host.workspace_id == active_id() && !host.sleeping) {
    std::vector<rust::String> open;
    for (const auto& view : host.tabs) open.emplace_back(TabUrl(host, view).spec());
    GURL url; std::u16string title;
    // This run's closed tabs first, then the timeline (earlier runs).
    for (size_t i = closed_tabs_.size(); i > 0 && url.is_empty(); --i) {
      const auto& tab = closed_tabs_[i - 1];
      if (tab.workspace_id != host.workspace_id || tab.profile_id != host.profile_id ||
          std::any_of(open.begin(), open.end(), [&](const auto& spec) { return std::string(spec) == tab.url.spec(); })) continue;
      url = tab.url; title = tab.title; closed_tabs_.erase(closed_tabs_.begin() + (i - 1));
    }
    if (url.is_empty() && timeline_enabled_) {
      RecordTimeline(false, host.workspace_id);
      const auto closed = timeline_->current->timeline_last_closed(host.workspace_id, rust::Slice<const rust::String>(open.data(), open.size()));
      if (closed.valid) { url = GURL(std::string(closed.tab.url)); title = base::UTF8ToUTF16(std::string(closed.tab.title)); }
    }
    if (!IsWorkspaceTabUrl(url)) return false;
    auto* view = CreatePage(host.profile); host.tabs.push_back(view);
    host.deferred_urls[view] = url;
    if (IsRssTabUrl(url)) AssociateReaderPin(host, view);
    if (!title.empty()) host.deferred_titles[view] = title;
    SelectTab(host.tabs.size() - 1); SaveSession(); return true;
  }
  return false;
}
bool WorkspaceWindow::ReopenClosedTabAt(size_t newest) {
  if (!configuration_ready_ || profiles_busy_ || configuration_invalid_ || newest >= closed_tabs_.size()) return false;
  const size_t index = closed_tabs_.size() - 1 - newest;
  const auto tab = closed_tabs_[index];
  // It comes back in its own space, which comes to the front.
  if (tab.workspace_id != active_id() && !Select(tab.workspace_id)) return false;
  for (auto& host : hosts_) if (host.workspace_id == tab.workspace_id && !host.sleeping && host.profile_id == tab.profile_id) {
    closed_tabs_.erase(closed_tabs_.begin() + index);
    auto* view = CreatePage(host.profile); host.tabs.push_back(view);
    host.deferred_urls[view] = tab.url;
    if (IsRssTabUrl(tab.url)) AssociateReaderPin(host, view);
    if (!tab.title.empty()) host.deferred_titles[view] = tab.title;
    SelectTab(host.tabs.size() - 1); SaveSession(); return true;
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
    RecordTimeline(true, id);
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
}  // namespace zitter::prototype
