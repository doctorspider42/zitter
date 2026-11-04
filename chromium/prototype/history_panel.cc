#include "chromium/prototype/history_panel.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::prototype {
namespace {
class HistoryListView final : public views::View {
  METADATA_HEADER(HistoryListView, views::View)
 public:
  void ContentsChanged() { InvalidateLayout(); PreferredSizeChanged(); }
};
BEGIN_METADATA(HistoryListView)
END_METADATA
// A visit row: a click (or Enter) opens it, or flips it while choosing. The
// scene paints the text; the row is the native target and its AX link.
class HistoryRowView final : public views::View {
  METADATA_HEADER(HistoryRowView, views::View)
 public:
  explicit HistoryRowView(base::RepeatingClosure pressed) : pressed_(std::move(pressed)) {
    SetFocusBehavior(FocusBehavior::ALWAYS); GetViewAccessibility().SetRole(ax::mojom::Role::kLink);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override { return event.IsLeftMouseButton(); }
  void OnMouseReleased(const ui::MouseEvent& event) override { if (event.IsLeftMouseButton() && HitTestPoint(event.location())) pressed_.Run(); }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() != ui::VKEY_RETURN && event.key_code() != ui::VKEY_SPACE) return false;
    pressed_.Run(); return true;
  }
 private:
  base::RepeatingClosure pressed_;
};
BEGIN_METADATA(HistoryRowView)
END_METADATA
std::u16string HostOf(const GURL& url) {
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) host = host.substr(4);
  return base::UTF8ToUTF16(host);
}
// „Dziś”, „Wczoraj”, „28 października”: the downloads window's day labels.
std::u16string DayLabel(base::Time time) {
  const auto now = base::Time::Now();
  const int days = std::max(0, static_cast<int>(std::lround((now.LocalMidnight() - time.LocalMidnight()).InSecondsF() / 86400)));
  base::Time::Exploded day, today; time.LocalExplode(&day); now.LocalExplode(&today);
  return base::UTF8ToUTF16(std::string(sidebar::download_day_label(days, day.day_of_month, day.month, day.year, day.year == today.year)));
}
// „21:20”: the design's 24-hour clock, whatever the system locale.
std::u16string Hour(base::Time time) { return base::TimeFormatTimeOfDayWithHourClockType(time, base::k24HourClock, base::kDropAmPm); }
// „2 min temu”, „3 h temu”, then the day and hour.
std::u16string Ago(base::Time when) {
  const auto delta = base::Time::Now() - when;
  if (delta < base::Minutes(1)) return T(u"przed chwilą");
  if (delta < base::Hours(1)) return TF(u"{} min temu", {base::NumberToString16(delta.InMinutes())});
  if (delta < base::Hours(12)) return TF(u"{} h temu", {base::NumberToString16(delta.InHours())});
  return DayLabel(when) + u" " + Hour(when);
}
enum : int { kNewTab = 1, kSplit, kPeek, kCopy, kSite, kDelete, kDeleteSite };
}  // namespace

// Design: Historia, wiersz — the site's initial on its tile (a checkbox while
// choosing), the title in `body`, the hour and host in `url`, and „⋯”.
struct HistoryPanel::Row {
  size_t index;
  raw_ptr<views::View> box, tile, title, meta, more, restore;
};

HistoryPanel::HistoryPanel() = default;
HistoryPanel::~HistoryPanel() { CloseUI(); }
void HistoryPanel::CloseUI() {
  weak_factory_.InvalidateWeakPtrs(); tracker_.TryCancelAll(); query_timer_.Stop(); querying_ = false;
  menu_runner_.reset(); menu_model_.reset(); rows_.clear(); layout_.clear();
  scene_.reset(); list_ = nullptr; count_ = nullptr; choose_ = nullptr; choosing_actions_ = nullptr; delete_ = nullptr;
  search_row_ = nullptr; search_ = nullptr; tabs_ = {}; scroll_ = nullptr;
  widget_.reset(); delegate_.reset();
  observation_.Reset(); history_ = nullptr; profile_ = nullptr;
}
views::View* HistoryPanel::MakeLine(views::View* parent, uint32_t style, int tone) {
  auto* line = parent->AddChildView(scene_->MakeText(u"", false, false, 0));
  scene_->SetTextStyle(line, style, static_cast<PanelScene::Tone>(tone)); return line;
}
bool HistoryPanel::scene_contains_text_for_testing(std::string_view text) const { return scene_ && scene_->contains_text(text); }
std::vector<GURL> HistoryPanel::listed_for_testing() const {
  std::vector<GURL> urls; for (const auto& visit : visits_) urls.push_back(visit.url); return urls;
}

void HistoryPanel::Show(Profile* profile, views::Widget* parent) {
  if (!profile || !parent) return;
  if (widget_ && (widget_->IsClosed() || profile_ != profile)) CloseUI();
  if (widget_) { widget_->Show(); widget_->Activate(); Query(); return; }
  profile_ = profile;
  history_ = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (history_) observation_.Observe(history_.get());
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->SetTitle(TF(u"Historia · {}", {BrowsingProfileName(profile)}));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetCanResize(true);
  delegate->set_use_custom_frame(false);
  delegate->set_draggable(true);
  auto content = std::make_unique<views::View>();
  scene_ = CreatePanelScene(content.get());
  auto weak = weak_factory_.GetWeakPtr();
  // `zt-pagetitle`: italic „Historia”, the count, then choosing.
  auto* heading = content->AddChildView(std::make_unique<views::View>());
  auto* title = MakeLine(heading, 2, static_cast<int>(PanelScene::Tone::kInk)); scene_->SetText(title, T(u"Historia"));
  count_ = MakeLine(heading, 8, static_cast<int>(PanelScene::Tone::kMuted));
  choose_ = scene_->AddIconAction(heading, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel) panel->SetChoosing(true); }, weak),
                                  T(u"Zaznacz wpisy"), &vector_icons::kChecklistIcon);
  choosing_actions_ = heading->AddChildView(scene_->MakeActions(8));
  auto* cancel = scene_->AddAction(choosing_actions_, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel) panel->SetChoosing(false); }, weak), T(u"Anuluj"));
  scene_->SetActionKind(cancel, 2);
  delete_ = scene_->AddAction(choosing_actions_, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel) panel->DeleteChosen(); }, weak), T(u"Usuń z historii"));
  scene_->SetActionKind(delete_, 5);
  SetSidebarPanelLayout(heading, false, gfx::Insets(), 8, count_, true);
  // The two lists, then clearing and Time Machine (design: nawigacja Historii).
  auto* nav = content->AddChildView(std::make_unique<views::View>());
  auto* lists = nav->AddChildView(scene_->MakeActions(6));
  tabs_[0] = scene_->AddAction(lists, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel) panel->SetShowClosed(false); }, weak), T(u"Lista"));
  tabs_[1] = scene_->AddAction(lists, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel) panel->SetShowClosed(true); }, weak), T(u"Ostatnio zamknięte"));
  for (auto& tab : tabs_) tab->GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  auto* tools = nav->AddChildView(scene_->MakeActions(4));
  auto* clear = scene_->AddAction(tools, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel && panel->clear_) panel->clear_.Run(); }, weak), T(u"Wyczyść dane…"));
  scene_->SetActionKind(clear, 3);
  auto* machine = scene_->AddAction(tools, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) { if (panel && panel->time_machine_) panel->time_machine_.Run(); }, weak), u"Time Machine");
  scene_->SetActionKind(machine, 3);
  SetSidebarPanelLayout(nav, false, gfx::Insets(), 8, lists, true);
  // Search by title or address.
  search_row_ = content->AddChildView(std::make_unique<views::View>());
  search_ = scene_->AddSearch(search_row_, T(u"Szukaj w historii — tytuł lub adres"), T(u"Szukaj w historii"));
  search_->SetCallbacks(base::BindRepeating([](base::WeakPtr<HistoryPanel> panel) {
    if (panel && panel->search_ && !panel->search_->IsIMEComposing()) panel->SetQuery(std::u16string(panel->search_->GetText())); }, weak),
      base::BindRepeating([](base::WeakPtr<HistoryPanel> panel, const ui::KeyEvent& event) {
        if (!panel || event.type() != ui::EventType::kKeyPressed || event.key_code() != ui::VKEY_ESCAPE || panel->query_.empty()) return false;
        panel->search_->SetText(u""); panel->SetQuery(u""); return true; }, weak));
  SetSidebarPanelLayout(search_row_, true, gfx::Insets(), 0);
  PanelViewport* viewport = nullptr;
  auto scroll_host = CreatePanelViewport(&viewport, scene_->ViewportChangedCallback(), {100, 560, false, true, true}); scroll_ = viewport;
  auto* scroll = content->AddChildView(std::move(scroll_host));
  auto list = std::make_unique<HistoryListView>();
  SetSidebarPanelLayout(list.get(), true, gfx::Insets(), 2);
  list_ = list.get(); viewport->SetPanelContents(std::move(list));
  SetSidebarPanelLayout(content.get(), true, gfx::Insets::TLBR(16, 16, 12, 16), 10, scroll);
  content->SetPreferredSize(gfx::Size(720, 700));
  delegate->SetContentsView(std::move(content));
  delegate_ = std::move(delegate);
  auto* widget = views::DialogDelegate::CreateDialogWidget(delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView());
  widget_.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<HistoryPanel> panel, views::Widget::ClosedReason) { if (panel) panel->CloseUI(); }, weak));
  widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget, true);
#endif
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(760, area.width()), std::min(760, area.height()));
  bounds.set_origin(parent->GetWindowBoundsInScreen().CenterPoint() - gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
  bounds.AdjustToFit(area); widget->SetBounds(bounds);
  Refresh(); widget->Show(); scene_->Schedule();
  Query();
}

void HistoryPanel::SetQuery(std::u16string query) {
  query = std::u16string(base::TrimWhitespace(query, base::TRIM_ALL));
  if (query_ == query) return;
  query_ = std::move(query);
  query_timer_.Start(FROM_HERE, base::Milliseconds(150), base::BindOnce(&HistoryPanel::Query, weak_factory_.GetWeakPtr()));
}
void HistoryPanel::SetShowClosed(bool closed) { if (show_closed_ == closed) return; show_closed_ = closed; choosing_ = false; chosen_.clear(); Refresh(); }
void HistoryPanel::SetChoosing(bool choosing) { if (choosing_ == choosing) return; choosing_ = choosing && !show_closed_; chosen_.clear(); Refresh(); }
void HistoryPanel::Choose(size_t index, bool chosen) {
  if (index >= visits_.size()) return;
  if (chosen) chosen_.insert(index); else chosen_.erase(index);
  Refresh();
}
// The newest 500 visits matching the search, one per page and day (as Chrome's History).
void HistoryPanel::Query() {
  if (!history_) return;
  tracker_.TryCancelAll(); querying_ = true;
  history::QueryOptions options;
  options.max_count = 500;
  options.duplicate_policy = history::QueryOptions::REMOVE_DUPLICATES_PER_DAY;
  history_->QueryHistory(query_, options, base::BindOnce(&HistoryPanel::OnResults, weak_factory_.GetWeakPtr()), &tracker_);
}
void HistoryPanel::OnResults(history::QueryResults results) {
  querying_ = false; visits_.clear(); chosen_.clear(); layout_.clear();
  for (const auto& result : results) {
    if (!result.url().SchemeIsHTTPOrHTTPS()) continue;
    visits_.push_back({result.url(), result.title().empty() ? base::UTF8ToUTF16(result.url().spec()) : result.title(), result.visit_time()});
  }
  Refresh();
}
void HistoryPanel::OnHistoryDeletions(history::HistoryService*, const history::DeletionInfo&) { query_timer_.Start(FROM_HERE, base::Milliseconds(150), base::BindOnce(&HistoryPanel::Query, weak_factory_.GetWeakPtr())); }
void HistoryPanel::OnURLVisited(history::HistoryService*, const history::VisitedURLInfo&) {
  if (widget_ && widget_->IsVisible()) query_timer_.Start(FROM_HERE, base::Seconds(1), base::BindOnce(&HistoryPanel::Query, weak_factory_.GetWeakPtr()));
}
void HistoryPanel::HistoryServiceBeingDeleted(history::HistoryService*) { CloseUI(); }

void HistoryPanel::Refresh() {
  if (!list_) return;
  auto weak = weak_factory_.GetWeakPtr();
  const auto closed = show_closed_ && closed_ ? closed_.Run() : std::vector<Closed>();
  // Visits sit under their day, closed tabs under their space.
  std::vector<std::string> layout;
  std::u16string group;
  const size_t count = show_closed_ ? closed.size() : visits_.size();
  for (size_t i = 0; i < count; ++i) {
    const auto label = show_closed_ ? closed[i].space : DayLabel(visits_[i].time);
    if (label != group) { group = label; layout.push_back("#" + base::UTF16ToUTF8(label)); }
    layout.push_back(base::NumberToString(i));
  }
  layout.push_back(show_closed_ ? "~closed" : choosing_ ? "~choosing" : "~list");
  if (layout != layout_) {
    layout_ = layout; rows_.clear(); list_->RemoveAllChildViews();
    if (!count) {
      auto* empty = list_->AddChildView(std::make_unique<views::View>());
      auto* title = MakeLine(empty, 2, static_cast<int>(PanelScene::Tone::kInk));
      scene_->SetText(title, querying_ ? u"" : T(u"Nic tu nie ma"));
      empty->AddChildView(scene_->MakeText(show_closed_ ? T(u"Zamknięte w tej sesji karty pojawią się tutaj. Starsze są w Time Machine.")
                                           : query_.empty() ? T(u"Historia tego profilu jest pusta.") : TF(u"Nic w historii nie pasuje do „{}”.", {query_}), true, false, 0));
      SetSidebarPanelLayout(empty, true, gfx::Insets::VH(48, 24), 12);
    }
    for (const auto& key : layout) {
      if (key.starts_with("~")) continue;
      if (key.starts_with("#")) {
        auto* label = MakeLine(list_, 7, static_cast<int>(PanelScene::Tone::kMuted)); scene_->SetText(label, base::UTF8ToUTF16(key.substr(1)));
        label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(12, 12, 4, 12))); continue;
      }
      size_t index = 0; base::StringToSizeT(key, &index);
      auto* box = list_->AddChildView(std::make_unique<HistoryRowView>(base::BindRepeating([](base::WeakPtr<HistoryPanel> panel, size_t index) {
        if (panel) panel->Activate(index); }, weak, index)));
      Row row{index, box};
      row.tile = box->AddChildView(scene_->MakeFileTile());
      auto* column = box->AddChildView(std::make_unique<views::View>());
      row.title = MakeLine(column, 5, static_cast<int>(PanelScene::Tone::kInk));
      row.meta = MakeLine(column, 8, static_cast<int>(PanelScene::Tone::kMuted));
      SetSidebarPanelLayout(column, true, gfx::Insets(), 2);
      auto* buttons = box->AddChildView(std::make_unique<views::View>());
      if (show_closed_) {
        auto* group_actions = buttons->AddChildView(scene_->MakeActions(0));
        row.restore = scene_->AddAction(group_actions, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel, size_t index) {
          if (panel && panel->reopen_) { panel->reopen_.Run(index); panel->Refresh(); } }, weak, index), T(u"Przywróć"));
        scene_->SetActionKind(row.restore, 2);
      } else {
        row.more = scene_->AddMoreAction(buttons, base::BindRepeating([](base::WeakPtr<HistoryPanel> panel, size_t index) {
          if (panel) panel->ShowRowMenu(index, nullptr); }, weak, index), T(u"Więcej"));
      }
      SetSidebarPanelLayout(buttons, false, gfx::Insets(), 2);
      SetSidebarPanelLayout(box, false, gfx::Insets::VH(6, 12), 12, column, true);
      rows_.push_back(row);
    }
  }
  for (auto& row : rows_) {
    const auto url = show_closed_ ? closed[row.index].url : visits_[row.index].url;
    const auto host = HostOf(url);
    scene_->SetFileTile(row.tile, host.empty() ? u"·" : host.substr(0, 1));
    scene_->SetFileTileChoice(row.tile, choosing_, chosen_.contains(row.index), base::BindRepeating([](base::WeakPtr<HistoryPanel> panel, size_t index) {
      if (panel) panel->Choose(index, !panel->chosen_.contains(index)); }, weak, row.index));
    const auto title = show_closed_ ? closed[row.index].title : visits_[row.index].title;
    scene_->SetText(row.title, title.empty() ? host : title);
    scene_->SetText(row.meta, show_closed_ ? host + u" · " + Ago(closed[row.index].when) : Hour(visits_[row.index].time) + u" · " + host);
    row.box->GetViewAccessibility().SetName(title.empty() ? host : title);
    row.box->SetTooltipText(base::UTF8ToUTF16(url.spec()));
    if (row.more) row.more->SetVisible(!choosing_);
  }
  RefreshChrome();
  static_cast<HistoryListView*>(list_.get())->ContentsChanged(); scene_->Schedule();
}
void HistoryPanel::RefreshChrome() {
  scene_->RefreshSearch(search_);
  search_row_->SetVisible(!show_closed_);
  // „3 zaznaczone” while choosing; the visits found while searching.
  const size_t n = chosen_.size(), tens = n % 100, ones = n % 10;
  const auto chosen = base::NumberToString16(n) + u" " + (n == 1 ? T(u"zaznaczony") : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? T(u"zaznaczone") : T(u"zaznaczonych"));
  scene_->SetText(count_, choosing_ ? chosen : !query_.empty() && !show_closed_ ? TF(u"{} wyników", {base::NumberToString16(visits_.size())}) : std::u16string());
  scene_->SetTextStyle(count_, 8, choosing_ ? PanelScene::Tone::kInk : PanelScene::Tone::kMuted);
  choose_->SetVisible(!choosing_ && !show_closed_); choosing_actions_->SetVisible(choosing_);
  delete_->SetEnabled(!chosen_.empty());
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool on = show_closed_ == (i == 1);
    scene_->SetActionKind(tabs_[i], on ? 7 : 6); tabs_[i]->GetViewAccessibility().SetIsSelected(on);
  }
}
void HistoryPanel::Activate(size_t index) {
  if (show_closed_) { if (reopen_) { reopen_.Run(index); Refresh(); } return; }
  if (index >= visits_.size()) return;
  if (choosing_) { Choose(index, !chosen_.contains(index)); return; }
  if (open_) open_.Run(visits_[index].url, Open::kNewTab);
}
// Design: Historia, menu wpisu.
void HistoryPanel::ShowRowMenu(size_t index, views::View* anchor) {
  if (index >= visits_.size() || !widget_) return;
  menu_index_ = index;
  if (!anchor) for (const auto& row : rows_) if (row.index == index) anchor = row.more;
  if (!anchor) return;
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddItem(kNewTab, T(u"Otwórz w nowej karcie"));
  model->AddItem(kSplit, T(u"Otwórz obok w Split View"));
  model->AddItem(kPeek, T(u"Otwórz w Peek"));
  model->AddItem(kCopy, T(u"Kopiuj adres"));
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kSite, TF(u"Więcej z {}", {HostOf(visits_[index].url)}));
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kDelete, T(u"Usuń z historii"));
  model->AddItem(kDeleteSite, T(u"Usuń całą witrynę z historii…"));
  menu_model_ = std::move(model);
  menu_runner_ = std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::NO_FLAGS);
  menu_runner_->RunMenuAt(widget_.get(), nullptr, anchor->GetBoundsInScreen(), views::MenuAnchorPosition::kTopRight,
                          ui::mojom::MenuSourceType::kMouse, gfx::NativeView(), gfx::RoundedCornersF(12));
}
void HistoryPanel::ExecuteCommand(int id, int) {
  if (menu_index_ >= visits_.size()) return;
  const auto visit = visits_[menu_index_];
  switch (id) {
    case kNewTab: if (open_) open_.Run(visit.url, Open::kNewTab); break;
    case kSplit: if (open_) open_.Run(visit.url, Open::kSplit); break;
    case kPeek: if (open_) open_.Run(visit.url, Open::kPeek); break;
    case kCopy: ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(base::UTF8ToUTF16(visit.url.spec())); break;
    case kSite: { const auto host = HostOf(visit.url); if (search_) search_->SetText(host); SetQuery(host); break; }
    case kDelete: DeleteVisits({menu_index_}); break;
    case kDeleteSite: {
      const auto host = std::string(visit.url.host());
      if (!confirm_) break;
      confirm_.Run(widget_.get(), TF(u"Usunąć {} z historii?", {HostOf(visit.url)}), T(u"Wszystkie wizyty na tej witrynie znikną z historii tego profilu."),
          T(u"Tego nie da się cofnąć."), T(u"Usuń witrynę"),
          base::BindOnce([](base::WeakPtr<HistoryPanel> panel, std::string host, bool yes) { if (panel && yes) panel->DeleteSite(std::move(host)); }, weak_factory_.GetWeakPtr(), host));
      break;
    }
  }
}
void HistoryPanel::DeleteChosen() {
  if (chosen_.empty() || !confirm_) return;
  std::vector<size_t> indexes(chosen_.begin(), chosen_.end());
  const size_t n = indexes.size(), tens = n % 100, ones = n % 10;
  const auto entries = base::NumberToString16(n) + u" " + (n == 1 ? T(u"wpis") : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? T(u"wpisy") : T(u"wpisów"));
  confirm_.Run(widget_.get(), TF(u"Usunąć {} z historii?", {entries}), std::u16string(), T(u"Tego nie da się cofnąć."), T(u"Usuń z historii"),
      base::BindOnce([](base::WeakPtr<HistoryPanel> panel, std::vector<size_t> indexes, bool yes) {
        if (!panel || !yes) return; panel->SetChoosing(false); panel->DeleteVisits(std::move(indexes)); }, weak_factory_.GetWeakPtr(), std::move(indexes)));
}
// As Chrome's History: a row removes its page's visits on that day only.
void HistoryPanel::DeleteVisits(std::vector<size_t> indexes) {
  if (!history_) return;
  std::vector<history::ExpireHistoryArgs> expire;
  for (size_t index : indexes) {
    if (index >= visits_.size()) continue;
    history::ExpireHistoryArgs args; args.urls.insert(visits_[index].url); args.SetTimeRangeForOneDay(visits_[index].time);
    expire.push_back(std::move(args));
  }
  if (expire.empty()) return;
  history_->ExpireHistory(expire, base::BindOnce(&HistoryPanel::Query, weak_factory_.GetWeakPtr()), &tracker_);
}
// Every page of the host, not only the listed ones: a full search first.
void HistoryPanel::DeleteSite(std::string host) {
  if (!history_) return;
  history::QueryOptions options;
  options.max_count = 0;
  history_->QueryHistory(base::UTF8ToUTF16(host), options, base::BindOnce([](base::WeakPtr<HistoryPanel> panel, std::string host, history::QueryResults results) {
    if (!panel || !panel->history_) return;
    std::vector<GURL> urls;
    for (const auto& result : results) if (result.url().host() == host || result.url().host() == "www." + host) urls.push_back(result.url());
    if (!urls.empty()) panel->history_->DeleteURLs(urls);
    panel->Query();
  }, weak_factory_.GetWeakPtr(), host), &tracker_);
}
}  // namespace zitter::prototype
