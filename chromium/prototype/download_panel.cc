#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

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
#include "ui/base/metadata/metadata_impl_macros.h"
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
class DownloadListView final : public views::View {
  METADATA_HEADER(DownloadListView,views::View)
 public:
  void ContentsChanged(){InvalidateLayout();PreferredSizeChanged();}
};
BEGIN_METADATA(DownloadListView)
END_METADATA

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
  raw_ptr<views::View> title, status, source;
  raw_ptr<views::View> progress;
  raw_ptr<views::Button> pause, cancel, reveal;
};
DownloadPanel::DownloadPanel() = default;
DownloadPanel::~DownloadPanel() { Disconnect(); }
void DownloadPanel::Disconnect() {
  CloseUI();
  for (auto& [guid, item] : items_) item->RemoveObserver(this);
  items_.clear();
  if (manager_) manager_->RemoveObserver(this);
  manager_ = nullptr; profile_ = nullptr;
}
void DownloadPanel::CloseUI(){
  weak_factory_.InvalidateWeakPtrs();timer_.Stop();refresh_pending_=false;
  scene_.reset();rows_.clear();list_=nullptr;scroll_=nullptr;widget_.reset();delegate_.reset();
}
views::View* DownloadPanel::MakeLabel(views::View* parent,std::u16string text){return scene_?parent->AddChildView(scene_->MakeText(std::move(text),true,false,0)):Label(parent,std::move(text));}
void DownloadPanel::SetText(views::View* view,std::u16string text){if(scene_)scene_->SetText(view,std::move(text));else static_cast<views::Label*>(view)->SetText(std::move(text));}
bool DownloadPanel::scene_visible_for_testing() const{return scene_&&scene_->visible();}
bool DownloadPanel::scene_contains_text_for_testing(std::string_view text) const{return scene_&&scene_->contains_text(text);}
bool DownloadPanel::scene_animating_for_testing() const{return scene_&&scene_->animating();}
bool DownloadPanel::scene_order_correct_for_testing() const{return scene_&&scene_->order_correct();}
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
  if(widget_&&widget_->IsClosed())CloseUI();
  if (widget_) { widget_->Show(); widget_->Activate(); Refresh(); return; }
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->SetTitle(u"Pobrania · " + BrowsingProfileName(profile));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetCanResize(true);
  delegate->set_use_custom_frame(false);
  delegate->set_draggable(true);
  auto content = std::make_unique<views::View>();
  scene_=CreatePanelScene(content.get());
  MakeLabel(content.get(), u"Pobrania tego profilu · ostatnie 200 i wszystkie aktywne");
  auto* scroll = content->AddChildView(std::make_unique<views::ScrollView>());
  scroll_=scroll;scroll->ClipHeightTo(100,480);
  SetSidebarPanelLayout(content.get(),true,gfx::Insets(16),12,scroll);
  if(scene_)scene_->ObserveScroll(scroll);
  auto list = std::make_unique<DownloadListView>();
  SetSidebarPanelLayout(list.get(),true,gfx::Insets(),16);
  list_ = scroll->SetContents(std::move(list));
  content->SetPreferredSize(gfx::Size(560, 500));
  delegate->SetContentsView(std::move(content));
  delegate_=std::move(delegate);auto* widget=views::DialogDelegate::CreateDialogWidget(delegate_.get(),parent->GetNativeWindow(),parent->GetNativeView());widget_.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<DownloadPanel> panel,views::Widget::ClosedReason){if(panel)panel->CloseUI();},weak_factory_.GetWeakPtr()));
  widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget,true);
#endif
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(600, area.width()), std::min(600, area.height()));
  bounds.set_origin(gfx::Point(parent->GetWindowBoundsInScreen().CenterPoint().x() - bounds.width()/2,
                              parent->GetWindowBoundsInScreen().CenterPoint().y() - bounds.height()/2));
  bounds.AdjustToFit(area); widget->SetBoundsConstrained(bounds);
  Refresh(); widget->Show();if(scene_)scene_->Schedule();
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
    if (ids.empty()) MakeLabel(list_, u"Nie ma jeszcze pobrań w tym profilu.");
    for (const auto& guid : ids) {
      auto* box = list_->AddChildView(std::make_unique<views::View>());
      SetSidebarPanelLayout(box,true,gfx::Insets(8),6);
      Row row; row.guid = guid;
      row.title=MakeLabel(box,u"");row.source=MakeLabel(box,u"");row.status=MakeLabel(box,u"");
      // Static copies skip event targeting by default; the filename keeps its
      // original full-path tooltip through the native hit-testing adapter.
      if(scene_)row.title->SetCanProcessEventsWithinSubtree(true);
      row.progress=box->AddChildView(scene_?scene_->MakeProgress():std::unique_ptr<views::View>(std::make_unique<views::ProgressBar>()));
      auto* buttons=box->AddChildView(scene_?scene_->MakeActions(8):std::make_unique<views::View>());
      if(!scene_)SetSidebarPanelLayout(buttons,false,gfx::Insets(),8);
      auto button=[&](std::u16string text,Action action)->views::Button*{
        auto callback=base::BindRepeating([](base::WeakPtr<DownloadPanel> panel,std::string guid,Action action){if(panel)panel->Act(std::move(guid),action);},weak_factory_.GetWeakPtr(),guid,action);
        return scene_?scene_->AddAction(buttons,std::move(callback),std::move(text)):buttons->AddChildView(std::make_unique<views::MdTextButton>(std::move(callback),text));
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
    SetText(row.title,name.empty()?u"Przygotowywanie pliku…":name);
    row.title->SetTooltipText(item->GetTargetFilePath().LossyDisplayName());
    SetText(row.source,base::UTF8ToUTF16(item->GetURL().host()));
    SetText(row.status,Status(item));
    const bool active = item->GetState() == Item::IN_PROGRESS;
    const bool safe = !Unsafe(item);
    row.progress->GetViewAccessibility().SetName(u"Postęp pobierania: " + name);
    row.progress->SetVisible(active);
    const double value=item->PercentComplete()<0?-1.0:item->PercentComplete()/100.0;if(scene_)scene_->SetProgress(row.progress,value);else static_cast<views::ProgressBar*>(row.progress.get())->SetValue(value);
    row.pause->SetVisible(safe && (active || item->CanResume()));
    const std::u16string pause_label=item->IsPaused()||item->GetState()==Item::INTERRUPTED?u"Wznów":u"Wstrzymaj";if(scene_)scene_->SetActionText(row.pause,pause_label);else static_cast<views::MdTextButton*>(row.pause.get())->SetText(pause_label);
    row.pause->SetEnabled((active && !item->IsPaused()) || item->CanResume());
    row.cancel->SetVisible(active || item->GetState() == Item::INTERRUPTED);
    row.reveal->SetVisible(safe && item->GetState() == Item::COMPLETE && !item->GetFileExternallyRemoved());
  }
  static_cast<DownloadListView*>(list_.get())->ContentsChanged();if(scene_)scene_->Schedule();
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
