#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/browser_data.h"

#include <algorithm>
#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/display/screen.h"
#include "ui/display/display.h"
#include "ui/views/border.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {
using Item = download::DownloadItem;
views::Label* Label(views::View* parent, std::u16string text) {
  auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetSubpixelRenderingEnabled(false);
  label->SetMultiLine(true);
  return label;
}
std::u16string Bytes(int64_t value) { return ui::FormatBytes(base::ByteSize(static_cast<uint64_t>(std::max<int64_t>(0, value)))); }
bool Unsafe(Item* item) { return item->IsDangerous() || item->IsInsecure(); }
std::u16string Status(Item* item) {
  if (Unsafe(item)) return u"Zablokowane przez ochronę Chromium — plik nie jest gotowy do użycia.";
  if (item->GetState() == Item::CANCELLED) return u"Anulowano";
  if (item->GetState() == Item::COMPLETE)
    return item->GetFileExternallyRemoved() ? u"Plik został przeniesiony lub usunięty" : u"Ukończono · " + Bytes(item->GetReceivedBytes());
  if (item->GetState() == Item::INTERRUPTED)
    return item->CanResume() ? u"Pobieranie przerwane — możesz wznowić" : u"Pobieranie przerwane — rozpocznij ponownie ze strony";
  auto text = item->IsPaused() ? std::u16string(u"Wstrzymano · ") : std::u16string();
  text += Bytes(item->GetReceivedBytes());
  if (item->GetTotalBytes() > 0) text += u" / " + Bytes(item->GetTotalBytes());
  if (!item->IsPaused() && item->CurrentSpeed() > 0) text += u" · " + Bytes(item->CurrentSpeed()) + u"/s";
  base::TimeDelta remaining;
  if (!item->IsPaused() && item->TimeRemaining(&remaining))
    text += u" · pozostało " + base::UTF8ToUTF16(std::to_string(std::max<int64_t>(1, remaining.InSeconds()))) + u" s";
  return text;
}
}  // namespace
struct DownloadPanel::Row {
  std::string guid;
  raw_ptr<views::Label> title, status, source;
  raw_ptr<views::ProgressBar> progress;
  raw_ptr<views::MdTextButton> pause, cancel, reveal;
};
DownloadPanel::DownloadPanel() = default;
DownloadPanel::~DownloadPanel() { Disconnect(); }
void DownloadPanel::Disconnect() {
  weak_factory_.InvalidateWeakPtrs();
  timer_.Stop(); refresh_pending_ = false;
  if (widget_) widget_->Close();
  widget_.reset(); rows_.clear(); list_ = nullptr;
  for (auto& [guid, item] : items_) item->RemoveObserver(this);
  items_.clear();
  if (manager_) manager_->RemoveObserver(this);
  manager_ = nullptr; profile_ = nullptr;
}
void DownloadPanel::SetProfile(Profile* profile) {
  if (profile_ == profile) return;
  Disconnect();
  profile_ = profile;
  if (!profile) return;
  manager_ = profile->GetDownloadManager();
  manager_->AddObserver(this);
  DownloadCoreServiceFactory::GetForBrowserContext(profile)->InitializeHistory(
      DownloadCoreService::DownloadHistoryLoadTrigger::kDownloadBubble);
  content::DownloadManager::DownloadVector items;
  manager_->GetAllDownloads(&items);
  for (auto item : items) Observe(item.get());
}
void DownloadPanel::Show(Profile* profile, views::Widget* parent) {
  SetProfile(profile);
  if (!profile_ || !parent) return;
  if (widget_ && widget_->IsClosed()) { widget_.reset(); rows_.clear(); list_ = nullptr; }
  if (widget_) { widget_->Show(); widget_->Activate(); Refresh(); return; }
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetTitle(u"Pobrania · " + BrowsingProfileName(profile));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetCanResize(true);
  delegate->set_use_custom_frame(false);
  delegate->set_draggable(true);
  auto content = std::make_unique<views::View>();
  auto* layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 12));
  Label(content.get(), u"Pobrania tego profilu · ostatnie 200 i wszystkie aktywne");
  auto* scroll = content->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(100, 480);
  layout->SetFlexForView(scroll, 1);
  auto list = std::make_unique<views::View>();
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(), 16));
  list_ = scroll->SetContents(std::move(list));
  content->SetPreferredSize(gfx::Size(560, 500));
  delegate->SetContentsView(std::move(content));
  auto* widget = views::DialogDelegate::CreateDialogWidget(std::move(delegate), parent->GetNativeWindow(), parent->GetNativeView());
  widget_ = widget->GetWeakPtr();
  widget->SetColorModeOverride(parent->color_mode_override());
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(600, area.width()), std::min(600, area.height()));
  bounds.set_origin(gfx::Point(parent->GetWindowBoundsInScreen().CenterPoint().x() - bounds.width()/2,
                              parent->GetWindowBoundsInScreen().CenterPoint().y() - bounds.height()/2));
  bounds.AdjustToFit(area); widget->SetBoundsConstrained(bounds);
  Refresh(); widget->Show();
  timer_.Start(FROM_HERE, base::Seconds(1), base::BindRepeating(&DownloadPanel::Refresh, weak_factory_.GetWeakPtr()));
}
void DownloadPanel::Observe(Item* item) {
  if (!items_.emplace(item->GetGuid(), item).second) return;
  item->AddObserver(this);
}
std::vector<std::string> DownloadPanel::visible_guids() const {
  std::vector<Item*> items;
  for (const auto& [guid,item] : items_) if (!item->IsTransient()) items.push_back(item);
  std::sort(items.begin(), items.end(), [](auto* a, auto* b) {
    const bool active_a = a->GetState() == Item::IN_PROGRESS;
    const bool active_b = b->GetState() == Item::IN_PROGRESS;
    return active_a != active_b ? active_a : a->GetStartTime() != b->GetStartTime() ? a->GetStartTime() > b->GetStartTime() : a->GetGuid() < b->GetGuid();
  });
  std::vector<std::string> result;
  size_t history = 0;
  for (auto* item : items) {
    if (item->GetState() != Item::IN_PROGRESS && history++ >= 200) continue;
    result.push_back(item->GetGuid());
  }
  return result;
}
void DownloadPanel::Refresh() {
  refresh_pending_ = false;
  if (!widget_ || widget_->IsClosed()) { timer_.Stop(); rows_.clear(); list_ = nullptr; return; }
  const auto ids = visible_guids();
  std::vector<std::string> previous;
  for (const auto& row : rows_) previous.push_back(row.guid);
  if (ids != previous || list_->children().empty()) {
    rows_.clear(); list_->RemoveAllChildViews();
    if (ids.empty()) Label(list_, u"Nie ma jeszcze pobrań w tym profilu.");
    for (const auto& guid : ids) {
      auto* box = list_->AddChildView(std::make_unique<views::View>());
      box->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 6));
      Row row; row.guid = guid;
      row.title = Label(box, u""); row.source = Label(box, u""); row.status = Label(box, u"");
      row.progress = box->AddChildView(std::make_unique<views::ProgressBar>());
      auto* buttons = box->AddChildView(std::make_unique<views::View>());
      buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
      auto button = [&](std::u16string text, Action action) {
        return buttons->AddChildView(std::make_unique<views::MdTextButton>(base::BindRepeating(
          [](base::WeakPtr<DownloadPanel> panel, std::string guid, Action action) {
            if (panel) panel->Act(std::move(guid), action);
          }, weak_factory_.GetWeakPtr(), guid, action), text));
      };
      row.pause = button(u"Wstrzymaj", Action::kPauseResume);
      row.cancel = button(u"Anuluj", Action::kCancel);
      row.reveal = button(u"Pokaż w Finderze", Action::kReveal);
      rows_.push_back(row);
    }
  }
  for (auto& row : rows_) {
    auto* item = items_.at(row.guid).get();
    auto name = item->GetFileNameToReportUser().LossyDisplayName();
    row.title->SetText(name.empty() ? u"Przygotowywanie pliku…" : name);
    row.title->SetTooltipText(item->GetTargetFilePath().LossyDisplayName());
    row.source->SetText(base::UTF8ToUTF16(item->GetURL().host()));
    row.status->SetText(Status(item));
    const bool active = item->GetState() == Item::IN_PROGRESS;
    const bool safe = !Unsafe(item);
    row.progress->GetViewAccessibility().SetName(u"Postęp pobierania: " + name);
    row.progress->SetVisible(active);
    row.progress->SetValue(item->PercentComplete() < 0 ? -1.0 : item->PercentComplete()/100.0);
    row.pause->SetVisible(safe && (active || item->CanResume()));
    row.pause->SetText(item->IsPaused() || item->GetState() == Item::INTERRUPTED ? u"Wznów" : u"Wstrzymaj");
    row.pause->SetEnabled((active && !item->IsPaused()) || item->CanResume());
    row.cancel->SetVisible(active || item->GetState() == Item::INTERRUPTED);
    row.reveal->SetVisible(safe && item->GetState() == Item::COMPLETE && !item->GetFileExternallyRemoved());
  }
  list_->InvalidateLayout();
}
bool DownloadPanel::Act(std::string guid, Action action) {
  auto found = items_.find(guid);
  if (found == items_.end()) return false;
  auto* item = found->second.get();
  if (action == Action::kCancel) {
    if (item->GetState() != Item::IN_PROGRESS && item->GetState() != Item::INTERRUPTED) return false;
    item->Cancel(true);
  } else if (action == Action::kPauseResume) {
    if (Unsafe(item)) return false;
    if (item->IsPaused() || item->GetState() == Item::INTERRUPTED) {
      if (!item->CanResume()) return false;
      item->Resume(true);
    } else if (item->GetState() == Item::IN_PROGRESS) item->Pause();
    else return false;
  } else {
    if (Unsafe(item) || item->GetState() != Item::COMPLETE || item->GetFileExternallyRemoved()) return false;
    item->ShowDownloadInShell();
  }
  ScheduleRefresh(); return true;
}
void DownloadPanel::ScheduleRefresh() {
  if (refresh_pending_ || !widget_) return;
  refresh_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&DownloadPanel::Refresh, weak_factory_.GetWeakPtr()));
}
void DownloadPanel::OnDownloadCreated(content::DownloadManager*, Item* item) { Observe(item); ScheduleRefresh(); }
void DownloadPanel::OnManagerInitialized() { ScheduleRefresh(); }
void DownloadPanel::ManagerGoingDown(content::DownloadManager*) { Disconnect(); }
void DownloadPanel::OnDownloadUpdated(Item*) { ScheduleRefresh(); }
void DownloadPanel::OnDownloadRemoved(Item* item) { item->RemoveObserver(this); items_.erase(item->GetGuid()); ScheduleRefresh(); }
void DownloadPanel::OnDownloadDestroyed(Item* item) { OnDownloadRemoved(item); }
}  // namespace zitter::prototype
