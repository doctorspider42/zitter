#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

#include <algorithm>
#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
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

std::u16string Bytes(int64_t value) { return ui::FormatBytes(base::ByteSize(static_cast<uint64_t>(std::max<int64_t>(0, value)))); }
bool Unsafe(Item* item) { return item->IsDangerous() || item->IsInsecure(); }
// Facts for the Rust status wording; byte formatting stays with the locale service.
sidebar::DownloadRow RustRow(Item* item) {
  sidebar::DownloadFacts facts{};
  facts.phase = item->GetState() == Item::COMPLETE ? sidebar::DownloadPhase::Complete : item->GetState() == Item::CANCELLED ? sidebar::DownloadPhase::Cancelled :
      item->GetState() == Item::INTERRUPTED ? sidebar::DownloadPhase::Interrupted : sidebar::DownloadPhase::InProgress;
  facts.blocked = Unsafe(item); facts.removed = item->GetFileExternallyRemoved(); facts.can_resume = item->CanResume(); facts.paused = item->IsPaused();
  facts.received = base::UTF16ToUTF8(Bytes(item->GetReceivedBytes()));
  if (item->GetTotalBytes() > 0) facts.total = base::UTF16ToUTF8(Bytes(item->GetTotalBytes()));
  if (item->CurrentSpeed() > 0) facts.speed = base::UTF16ToUTF8(Bytes(item->CurrentSpeed()));
  base::TimeDelta remaining; facts.remaining_seconds = item->TimeRemaining(&remaining) ? std::max<int64_t>(0, remaining.InSeconds()) : -1;
  facts.host = std::string(item->GetURL().host());
  return sidebar::download_row(facts);
}
}  // namespace
// Design: Downloads — a 32 px extension tile, the name in `body`, the state as a
// symbol and word in its tone beside the `url` facts, a 3 px bar, icon actions.
struct DownloadPanel::Row {
  std::string guid;
  raw_ptr<views::View> tile, title, state, meta;
  raw_ptr<views::View> progress;
  raw_ptr<views::View> pause, cancel, reveal;
};
DownloadPanel::DownloadPanel() = default;
DownloadPanel::~DownloadPanel() { Disconnect(); }
void DownloadPanel::Disconnect() {
  CloseUI();
  for (auto& [guid, item] : items_) item->RemoveObserver(this);
  items_.clear(); in_progress_.clear(); initialized_ = false;
  if (manager_) manager_->RemoveObserver(this);
  manager_ = nullptr; profile_ = nullptr;
}
void DownloadPanel::CloseUI(){
  weak_factory_.InvalidateWeakPtrs();timer_.Stop();refresh_pending_=false;
  scene_.reset();rows_.clear();list_=nullptr;count_=nullptr;scroll_=nullptr;widget_.reset();delegate_.reset();
}
views::View* DownloadPanel::MakeLabel(views::View* parent,std::u16string text){return parent->AddChildView(scene_->MakeText(std::move(text),true,false,0));}
views::View* DownloadPanel::MakeLine(views::View* parent,uint32_t style,int tone){auto* line=parent->AddChildView(scene_->MakeText(u"",false,false,0));scene_->SetTextStyle(line,style,static_cast<PanelScene::Tone>(tone));return line;}
void DownloadPanel::SetText(views::View* view,std::u16string text){scene_->SetText(view,std::move(text));}
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
  initialized_ = manager_->IsManagerInitialized();
  ReportProgress();
}
void DownloadPanel::Show(Profile* profile, views::Widget* parent, gfx::Rect anchor) {
  SetProfile(profile);
  if (!profile_ || !parent) return;
  if(widget_&&widget_->IsClosed())CloseUI();
  // Above the ring: its left edge 16 px before the ring, 8 px over it.
  auto place = [&](views::Widget* widget) {
    const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
    gfx::Rect bounds(0, 0, std::min(anchor.IsEmpty() ? 600 : 420, area.width()), std::min(anchor.IsEmpty() ? 600 : 460, area.height()));
    if (anchor.IsEmpty()) bounds.set_origin(gfx::Point(parent->GetWindowBoundsInScreen().CenterPoint().x() - bounds.width()/2,
                                                      parent->GetWindowBoundsInScreen().CenterPoint().y() - bounds.height()/2));
    else bounds.set_origin(gfx::Point(anchor.x() - 16, anchor.y() - 8 - bounds.height()));
    bounds.AdjustToFit(area); widget->SetBounds(bounds);
  };
  if (widget_) { place(widget_.get()); widget_->Show(); widget_->Activate(); Refresh(); return; }
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->SetTitle(TF(u"Pobrania · {}", {BrowsingProfileName(profile)}));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetCanResize(true);
  delegate->set_use_custom_frame(false);
  delegate->set_draggable(true);
  auto content = std::make_unique<views::View>();
  scene_=CreatePanelScene(content.get());
  // `zt-pagetitle`: italic „Pobrane” and the count in `url`.
  auto* heading=content->AddChildView(std::make_unique<views::View>());
  auto* title=MakeLine(heading,2,static_cast<int>(PanelScene::Tone::kInk));scene_->SetText(title,T(u"Pobrane"));
  count_=MakeLine(heading,8,static_cast<int>(PanelScene::Tone::kMuted));
  SetSidebarPanelLayout(heading,false,gfx::Insets(),16,title);
  PanelViewport* viewport=nullptr;
  auto scroll_host=CreatePanelViewport(&viewport,scene_->ViewportChangedCallback(),{100,480,false,true,true});scroll_=viewport;
  auto* scroll=content->AddChildView(std::move(scroll_host));
  SetSidebarPanelLayout(content.get(),true,gfx::Insets(16),12,scroll);
  auto list = std::make_unique<DownloadListView>();
  SetSidebarPanelLayout(list.get(),true,gfx::Insets(),4);
  list_=list.get();viewport->SetPanelContents(std::move(list));
  content->SetPreferredSize(gfx::Size(560, 500));
  delegate->SetContentsView(std::move(content));
  delegate_=std::move(delegate);auto* widget=views::DialogDelegate::CreateDialogWidget(delegate_.get(),parent->GetNativeWindow(),parent->GetNativeView());widget_.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<DownloadPanel> panel,views::Widget::ClosedReason){if(panel)panel->CloseUI();},weak_factory_.GetWeakPtr()));
  widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget,true);
#endif
  place(widget);
  Refresh(); widget->Show();scene_->Schedule();
  timer_.Start(FROM_HERE, base::Seconds(1), base::BindRepeating(&DownloadPanel::Refresh, weak_factory_.GetWeakPtr()));
}
void DownloadPanel::ReportProgress() {
  if (!progress_) return;
  int active = 0; int64_t received = 0, total = 0; bool unknown = false;
  Item* newest = nullptr;
  for (const auto& [guid, item] : items_) {
    if (item->GetState() != Item::IN_PROGRESS || item->IsTransient()) continue;
    ++active; received += std::max<int64_t>(0, item->GetReceivedBytes());
    if (item->GetTotalBytes() > 0) total += item->GetTotalBytes(); else unknown = true;
    if (!newest || item->GetStartTime() > newest->GetStartTime()) newest = item;
  }
  const double fraction = active == 0 ? 1.0 : unknown || total <= 0 ? -1.0 : std::min(1.0, static_cast<double>(received) / total);
  progress_.Run(active, fraction, newest ? newest->GetFileNameToReportUser().LossyDisplayName() : std::u16string());
}
void DownloadPanel::Observe(Item* item) {
  if (!items_.emplace(item->GetGuid(), item).second) return;
  item->AddObserver(this);
  if (item->GetState() == Item::IN_PROGRESS) in_progress_.insert(item->GetGuid());
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
    if (ids.empty()) MakeLabel(list_, T(u"Nie ma jeszcze pobrań w tym profilu."));
    for (const auto& guid : ids) {
      auto* box = list_->AddChildView(std::make_unique<views::View>());
      Row row; row.guid = guid;
      row.tile=box->AddChildView(scene_->MakeFileTile());
      auto* column=box->AddChildView(std::make_unique<views::View>());
      row.title=MakeLine(column,5,static_cast<int>(PanelScene::Tone::kInk));
      // Static copies skip event targeting by default; the filename keeps its
      // original full-path tooltip through the native hit-testing adapter.
      row.title->SetCanProcessEventsWithinSubtree(true);
      auto* facts=column->AddChildView(std::make_unique<views::View>());
      row.state=MakeLine(facts,6,static_cast<int>(PanelScene::Tone::kMuted));row.meta=MakeLine(facts,8,static_cast<int>(PanelScene::Tone::kMuted));
      SetSidebarPanelLayout(facts,false,gfx::Insets(),8,row.meta);
      row.progress=column->AddChildView(scene_->MakeProgress());
      SetSidebarPanelLayout(column,true,gfx::Insets(),4);
      auto* buttons=box->AddChildView(std::make_unique<views::View>());
      auto button=[&](std::u16string text,const gfx::VectorIcon& icon,Action action)->views::View*{
        auto callback=base::BindRepeating([](base::WeakPtr<DownloadPanel> panel,std::string guid,Action action){if(panel)panel->Act(std::move(guid),action);},weak_factory_.GetWeakPtr(),guid,action);
        return scene_->AddIconAction(buttons,std::move(callback),std::move(text),&icon);
      };
      row.pause = button(T(u"Wstrzymaj"), vector_icons::kPauseIcon, Action::kPauseResume);
      row.cancel = button(T(u"Anuluj"), vector_icons::kCloseSmallIcon, Action::kCancel);
      row.reveal = button(T(u"Pokaż w Finderze"), vector_icons::kFolderOpenIcon, Action::kReveal);
      SetSidebarPanelLayout(buttons,false,gfx::Insets(),4);
      SetSidebarPanelLayout(box,false,gfx::Insets::VH(8,12),12,column);
      rows_.push_back(row);
    }
  }
  int64_t bytes = 0;
  for (auto& row : rows_) {
    auto* item = items_.at(row.guid).get();
    bytes += std::max<int64_t>(0, item->GetTotalBytes() > 0 ? item->GetTotalBytes() : item->GetReceivedBytes());
    const auto file = item->GetFileNameToReportUser();
    auto name = file.LossyDisplayName();
    std::u16string extension = file.Extension().empty() ? std::u16string() : base::FilePath(file.Extension()).AsUTF16Unsafe().substr(1);
    scene_->SetFileTile(row.tile, extension.empty() ? u"—" : extension);
    SetText(row.title,name.empty()?T(u"Przygotowywanie pliku…"):name);
    row.title->SetTooltipText(item->GetTargetFilePath().LossyDisplayName());
    const auto rust = RustRow(item);
    const auto tone = rust.tone == 1 ? PanelScene::Tone::kOk : rust.tone == 2 ? PanelScene::Tone::kWarn : rust.tone == 3 ? PanelScene::Tone::kDanger : PanelScene::Tone::kMuted;
    scene_->SetTextStyle(row.state, 6, tone);
    SetText(row.state, base::UTF8ToUTF16(std::string(rust.state)));
    row.state->SetVisible(!rust.state.empty());
    SetText(row.meta, base::UTF8ToUTF16(std::string(rust.meta)));
    const bool active = item->GetState() == Item::IN_PROGRESS;
    const bool safe = !Unsafe(item);
    row.progress->GetViewAccessibility().SetName(TF(u"Postęp pobierania: {}", {name}));
    row.progress->SetVisible(active);
    scene_->SetProgressPaused(row.progress, item->IsPaused());
    const double value=item->PercentComplete()<0?-1.0:item->PercentComplete()/100.0;scene_->SetProgress(row.progress,value);
    row.pause->SetVisible(safe && (active || item->CanResume()));
    const bool resume = item->IsPaused() || item->GetState() == Item::INTERRUPTED;
    scene_->SetIconAction(row.pause, resume ? T(u"Wznów") : T(u"Wstrzymaj"), resume ? &vector_icons::kPlayArrowIcon : &vector_icons::kPauseIcon);
    row.pause->SetEnabled((active && !item->IsPaused()) || item->CanResume());
    row.cancel->SetVisible(active || item->GetState() == Item::INTERRUPTED);
    row.reveal->SetVisible(safe && item->GetState() == Item::COMPLETE && !item->GetFileExternallyRemoved());
  }
  // „4 pliki · 1,2 GB”: Polish picks the plural by the count.
  const size_t n = rows_.size(), tens = n % 100, ones = n % 10;
  const auto count = base::NumberToString16(n);
  const auto files = n == 1 ? TF(u"{} plik", {count}) : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? TF(u"{} pliki", {count}) : TF(u"{} plików", {count});
  SetText(count_, n ? files + u" · " + Bytes(bytes) : std::u16string());
  static_cast<DownloadListView*>(list_.get())->ContentsChanged();scene_->Schedule();
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
void DownloadPanel::OnDownloadCreated(content::DownloadManager*, Item* item) {
  Observe(item);
  if (initialized_ && started_ && item->GetState() == Item::IN_PROGRESS && !item->IsTransient())
    started_.Run(item->GetFileNameToReportUser().LossyDisplayName());
  ReportProgress(); ScheduleRefresh();
}
void DownloadPanel::OnManagerInitialized() { initialized_ = true; ReportProgress(); ScheduleRefresh(); }
void DownloadPanel::ManagerGoingDown(content::DownloadManager*) { Disconnect(); }
void DownloadPanel::OnDownloadUpdated(Item* item) {
  const auto guid = item->GetGuid();
  bool finished = false;
  if (item->GetState() == Item::IN_PROGRESS) in_progress_.insert(guid);
  else finished = in_progress_.erase(guid) && item->GetState() == Item::COMPLETE && !Unsafe(item);
  ReportProgress();
  if (finished && completed_) completed_.Run(guid, item->GetFileNameToReportUser().LossyDisplayName());
  ScheduleRefresh();
}
void DownloadPanel::OnDownloadRemoved(Item* item) { item->RemoveObserver(this); items_.erase(item->GetGuid()); in_progress_.erase(item->GetGuid()); ReportProgress(); ScheduleRefresh(); }
void DownloadPanel::OnDownloadDestroyed(Item* item) { OnDownloadRemoved(item); }
}  // namespace zitter::prototype
