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
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "ui/gfx/font_list.h"
#include "base/strings/string_util.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chromium/prototype/editor_control.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
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
// Polish picks the plural by the count: „1 plik”, „3 pliki”, „5 plików”.
std::u16string Count(size_t n, std::u16string_view one, std::u16string_view few, std::u16string_view many) {
  const size_t tens = n % 100, ones = n % 10;
  const auto word = n == 1 ? one : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? few : many;
  return base::NumberToString16(n) + u" " + std::u16string(word);
}
std::u16string Files(size_t n) { return Count(n, T(u"plik"), T(u"pliki"), T(u"plików")); }
std::string Utf8Extension(Item* item) { return base::FilePath(item->GetFileNameToReportUser().Extension()).AsUTF8Unsafe(); }
bool Active(Item* item) { return item->GetState() == Item::IN_PROGRESS; }
bool Failed(Item* item) { return item->GetState() == Item::INTERRUPTED || item->GetState() == Item::CANCELLED; }
bool OnDisk(Item* item) { return item->GetState() == Item::COMPLETE && !item->GetFileExternallyRemoved(); }
// The day group a file starts in: „Dziś”, „Wczoraj”, „28 października”.
std::u16string DayLabel(base::Time start) {
  const auto now = base::Time::Now();
  const int days = std::max(0, static_cast<int>(std::lround((now.LocalMidnight() - start.LocalMidnight()).InSecondsF() / 86400)));
  base::Time::Exploded day, today; start.LocalExplode(&day); now.LocalExplode(&today);
  return base::UTF8ToUTF16(std::string(sidebar::download_day_label(days, day.day_of_month, day.month, day.year, day.year == today.year)));
}
// „Wyczyść”: clearing the whole list stands out in `body-strong`.
class ClearMenuModel final : public ui::SimpleMenuModel {
 public:
  using ui::SimpleMenuModel::SimpleMenuModel;
  void SetStrong(int command_id) { strong_ = command_id; }
  const gfx::FontList* GetLabelFontListAt(size_t index) const override {
    static const base::NoDestructor<gfx::FontList> strong(std::vector<std::string>{std::string(sidebar::design_font_family(2))}, gfx::Font::NORMAL, 13, gfx::Font::Weight::SEMIBOLD);
    return GetCommandIdAt(index) == strong_ ? strong.get() : ui::SimpleMenuModel::GetLabelFontListAt(index);
  }
 private:
  int strong_ = -1;
};
// Menu commands: a file's menu, the type menu (100 + filter) and „Wyczyść” (200 + Clear).
enum : int { kRowOpen = 1, kRowReveal, kRowCopy, kRowRetry, kRowRemove, kRowDelete, kTypeBase = 100, kClearBase = 200 };
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
// Design: Okno Pobrane — a 32 px extension tile (a checkbox while choosing),
// the name in `body`, the state as a symbol and word in its tone beside the
// `url` facts, a 3 px bar, icon actions and the file's „⋯” menu.
struct DownloadPanel::Row {
  std::string guid;
  raw_ptr<views::View> tile, title, state, meta;
  raw_ptr<views::View> progress;
  raw_ptr<views::View> pause, cancel, reveal, retry, more;
};
DownloadPanel::DownloadPanel() = default;
DownloadPanel::~DownloadPanel() { Disconnect(); }
void DownloadPanel::Disconnect() {
  CloseUI();
  CommitRemoval();
  for (auto& [guid, item] : items_) item->RemoveObserver(this);
  items_.clear(); in_progress_.clear(); initialized_ = false; chosen_.clear(); choosing_ = false;
  if (manager_) manager_->RemoveObserver(this);
  manager_ = nullptr; profile_ = nullptr;
}
void DownloadPanel::CloseUI(){
  weak_factory_.InvalidateWeakPtrs();timer_.Stop();refresh_pending_=false;menu_runner_.reset();menu_model_.reset();
  scene_.reset();rows_.clear();layout_.clear();list_=nullptr;count_=nullptr;scroll_=nullptr;choose_=nullptr;folder_=nullptr;choosing_actions_=nullptr;choose_all_=nullptr;
  search_=nullptr;chips_={};type_=nullptr;footer_=nullptr;clear_=nullptr;path_=nullptr;bulk_=nullptr;bulk_size_=nullptr;bulk_delete_=nullptr;
  widget_.reset();delegate_.reset();
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
    gfx::Rect bounds(0, 0, std::min(460, area.width()), std::min(660, area.height()));
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
  auto weak = weak_factory_.GetWeakPtr();
  auto icon = [&](views::View* parent, std::u16string name, const gfx::VectorIcon& vector, base::RepeatingClosure pressed) {
    return scene_->AddIconAction(parent, std::move(pressed), std::move(name), &vector);
  };
  // `zt-pagetitle`: italic „Pobrane”, the count in `url`, then choosing and the folder.
  auto* heading=content->AddChildView(std::make_unique<views::View>());
  auto* title=MakeLine(heading,2,static_cast<int>(PanelScene::Tone::kInk));scene_->SetText(title,T(u"Pobrane"));
  count_=MakeLine(heading,8,static_cast<int>(PanelScene::Tone::kMuted));
  choose_=icon(heading,T(u"Zaznacz pliki"),vector_icons::kChecklistIcon,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel)panel->SetChoosing(true);},weak));
  folder_=icon(heading,T(u"Otwórz folder Pobrane"),vector_icons::kFolderOpenIcon,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel)panel->OpenFolder();},weak));
  choosing_actions_=heading->AddChildView(scene_->MakeActions(4));
  choose_all_=scene_->AddAction(choosing_actions_,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){
    if(!panel)return;const auto ids=panel->visible_guids();const bool all=std::all_of(ids.begin(),ids.end(),[&](const auto& id){return panel->chosen_.contains(id);});
    for(const auto& id:ids)panel->Choose(id,!all);},weak),T(u"Zaznacz wszystkie"));
  scene_->SetActionKind(choose_all_,3);
  auto* done=scene_->AddAction(choosing_actions_,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel)panel->SetChoosing(false);},weak),T(u"Gotowe"));
  scene_->SetActionKind(done,2);
  SetSidebarPanelLayout(heading,false,gfx::Insets(),8,count_,true);
  // Search by name or site, then the state chips and the type chip.
  search_=scene_->AddSearch(content.get(),T(u"Szukaj po nazwie lub stronie"),T(u"Szukaj w pobranych"));
  search_->SetCallbacks(base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel&&panel->search_&&!panel->search_->IsIMEComposing())panel->SetQuery(std::u16string(panel->search_->GetText()));},weak),
      base::BindRepeating([](base::WeakPtr<DownloadPanel> panel,const ui::KeyEvent& event){
        if(!panel||event.type()!=ui::EventType::kKeyPressed||event.key_code()!=ui::VKEY_ESCAPE||panel->query_.empty())return false;
        panel->search_->SetText(u"");panel->SetQuery(u"");return true;},weak));
  search_->SetText(query_);
  auto* filters=content->AddChildView(std::make_unique<views::View>());
  auto* states=filters->AddChildView(scene_->MakeActions(6));
  for(size_t i=0;i<chips_.size();++i){
    chips_[i]=scene_->AddAction(states,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel,StateFilter filter){if(panel)panel->SetStateFilter(filter);},weak,static_cast<StateFilter>(i)),u"");
    chips_[i]->GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  }
  auto* kinds=filters->AddChildView(scene_->MakeActions(0));
  type_=scene_->AddAction(kinds,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel&&panel->type_)panel->ShowTypeMenu(panel->type_);},weak),T(u"Typ"));
  SetSidebarPanelLayout(filters,false,gfx::Insets(),6,states);
  // The list scrolls between the filters and the footer.
  PanelViewport* viewport=nullptr;
  auto scroll_host=CreatePanelViewport(&viewport,scene_->ViewportChangedCallback(),{100,480,false,true,true});scroll_=viewport;
  auto* scroll=content->AddChildView(std::move(scroll_host));
  auto list = std::make_unique<DownloadListView>();
  SetSidebarPanelLayout(list.get(),true,gfx::Insets(),2);
  list_=list.get();viewport->SetPanelContents(std::move(list));
  // Footer: „Wyczyść” and the downloads folder; while choosing, the chosen
  // files' size and what to do with them.
  footer_=content->AddChildView(std::make_unique<views::View>());
  auto* clear_group=footer_->AddChildView(scene_->MakeActions(0));
  clear_=scene_->AddAction(clear_group,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel&&panel->clear_)panel->ShowClearMenu(panel->clear_);},weak),T(u"Wyczyść…"));
  scene_->SetActionKind(clear_,3);
  path_=MakeLine(footer_,8,static_cast<int>(PanelScene::Tone::kMuted));
  auto* change_group=footer_->AddChildView(scene_->MakeActions(0));
  auto* change=scene_->AddAction(change_group,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel&&panel->change_folder_)panel->change_folder_.Run();},weak),T(u"Zmień"));
  scene_->SetActionKind(change,3);change->GetViewAccessibility().SetName(T(u"Zmień folder pobierania"));
  SetSidebarPanelLayout(footer_,false,gfx::Insets(),8,path_,true);
  bulk_=content->AddChildView(std::make_unique<views::View>());
  bulk_size_=MakeLine(bulk_,8,static_cast<int>(PanelScene::Tone::kMuted));
  auto* bulk_actions=bulk_->AddChildView(scene_->MakeActions(8));
  auto* remove=scene_->AddAction(bulk_actions,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel)panel->RemoveChosen();},weak),T(u"Usuń z listy"));
  scene_->SetActionKind(remove,2);
  bulk_delete_=scene_->AddAction(bulk_actions,base::BindRepeating([](base::WeakPtr<DownloadPanel> panel){if(panel)panel->DeleteChosen();},weak),T(u"Usuń z dysku…"));
  scene_->SetActionKind(bulk_delete_,5);
  SetSidebarPanelLayout(bulk_,false,gfx::Insets(),8,bulk_size_,true);
  SetSidebarPanelLayout(content.get(),true,gfx::Insets::TLBR(16,16,12,16),10,scroll);
  content->SetPreferredSize(gfx::Size(440, 640));
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
    // A file removed from the list leaves the footer ring at once.
    if (item->GetState() != Item::IN_PROGRESS || item->IsTransient() || hidden_.contains(guid)) continue;
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
bool DownloadPanel::Passes(Item* item, bool state, bool category) const {
  if (item->IsTransient() || hidden_.contains(item->GetGuid())) return false;
  if (state && state_ != StateFilter::kAll &&
      !(state_ == StateFilter::kActive ? Active(item) : state_ == StateFilter::kDone ? item->GetState() == Item::COMPLETE : Failed(item))) return false;
  if (category && category_ && sidebar::download_category(Utf8Extension(item)) + 1 != category_) return false;
  if (query_.empty()) return true;
  return sidebar::download_matches(item->GetFileNameToReportUser().BaseName().AsUTF8Unsafe(), std::string(item->GetURL().host()), base::UTF16ToUTF8(query_));
}
// Newest first; history beyond the 200 newest finished files stays out.
std::vector<Item*> DownloadPanel::Listed() const {
  std::vector<Item*> items;
  for (const auto& [guid,item] : items_) if (Passes(item, true, true)) items.push_back(item);
  std::sort(items.begin(), items.end(), [](auto* a, auto* b) {
    return a->GetStartTime() != b->GetStartTime() ? a->GetStartTime() > b->GetStartTime() : a->GetGuid() < b->GetGuid();
  });
  std::vector<Item*> result;size_t history = 0;
  for (auto* item : items) if (Active(item) || history++ < 200) result.push_back(item);
  return result;
}
std::vector<std::string> DownloadPanel::visible_guids() const {
  std::vector<std::string> result;
  for (auto* item : Listed()) result.push_back(item->GetGuid());
  return result;
}
int DownloadPanel::count_for_testing(StateFilter filter) const {
  int n = 0;
  for (const auto& [guid, item] : items_) if (Passes(item, false, true) && (filter == StateFilter::kAll || (filter == StateFilter::kActive ? Active(item) : filter == StateFilter::kDone ? item->GetState() == Item::COMPLETE : Failed(item)))) ++n;
  return n;
}
std::u16string DownloadPanel::folder_for_testing() const { return path_ ? static_cast<std::u16string>(path_->GetViewAccessibility().GetCachedName()) : std::u16string(); }
void DownloadPanel::SetQuery(std::u16string query) {
  query = std::u16string(base::TrimWhitespace(query, base::TRIM_ALL));
  if (query_ == query) return;
  query_ = std::move(query); ScheduleRefresh();
}
void DownloadPanel::SetStateFilter(StateFilter filter) { if (state_ == filter) return; state_ = filter; ScheduleRefresh(); }
void DownloadPanel::SetCategory(uint32_t category) { if (category_ == category) return; category_ = category; ScheduleRefresh(); }
void DownloadPanel::SetChoosing(bool choosing) {
  if (choosing_ == choosing) return;
  choosing_ = choosing; chosen_.clear(); layout_.clear(); ScheduleRefresh();
}
void DownloadPanel::Choose(const std::string& guid, bool chosen) {
  if (!items_.contains(guid)) return;
  if (chosen) chosen_.insert(guid); else chosen_.erase(guid);
  ScheduleRefresh();
}
void DownloadPanel::Refresh() {
  refresh_pending_ = false;
  if (!widget_ || widget_->IsClosed()) { timer_.Stop(); rows_.clear(); layout_.clear(); list_ = nullptr; return; }
  const auto listed = Listed();
  // Rows sit under their day: rebuilt only when the groups or files change.
  std::vector<std::string> layout;std::u16string day;
  for (auto* item : listed) { const auto label = DayLabel(item->GetStartTime()); if (label != day) { day = label; layout.push_back("#" + base::UTF16ToUTF8(label)); } layout.push_back(item->GetGuid()); }
  if (layout.empty()) layout.push_back(items_.empty() ? "~none" : "~filtered");
  if (layout != layout_ || list_->children().empty()) {
    layout_ = layout; rows_.clear(); list_->RemoveAllChildViews();
    auto weak = weak_factory_.GetWeakPtr();
    if (listed.empty()) {
      // Design: Brak wyników — „Nic tu nie ma” and a way back to every file.
      auto* empty = list_->AddChildView(std::make_unique<views::View>());
      if (items_.empty()) MakeLabel(empty, T(u"Nie ma jeszcze pobrań w tym profilu."));
      else {
        auto* title = MakeLine(empty, 2, static_cast<int>(PanelScene::Tone::kInk)); SetText(title, T(u"Nic tu nie ma"));
        MakeLabel(empty, query_.empty() ? T(u"Żaden plik nie pasuje do tych filtrów.") : TF(u"Żaden plik nie pasuje do „{}” w tych filtrach.", {query_}));
        auto* actions = empty->AddChildView(scene_->MakeActions(8));
        if (state_ != StateFilter::kAll || category_) {
          auto* all = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<DownloadPanel> panel) { if (panel) { panel->state_ = StateFilter::kAll; panel->category_ = 0; panel->ScheduleRefresh(); } }, weak), T(u"Pokaż we wszystkich"));
          scene_->SetActionKind(all, 0);
        }
        auto* reset = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<DownloadPanel> panel) {
          if (!panel) return; panel->state_ = StateFilter::kAll; panel->category_ = 0; panel->query_.clear(); if (panel->search_) panel->search_->SetText(u""); panel->ScheduleRefresh(); }, weak), T(u"Wyczyść filtry"));
        scene_->SetActionKind(reset, 2);
      }
      SetSidebarPanelLayout(empty, true, gfx::Insets::VH(48, 24), 12);
    }
    for (const auto& key : layout) {
      if (key.starts_with("~")) continue;
      if (key.starts_with("#")) { auto* label = MakeLine(list_, 7, static_cast<int>(PanelScene::Tone::kMuted)); SetText(label, base::UTF8ToUTF16(key.substr(1))); label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(12, 12, 4, 12))); continue; }
      const auto& guid = key;
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
        auto callback=base::BindRepeating([](base::WeakPtr<DownloadPanel> panel,std::string guid,Action action){if(panel)panel->Act(std::move(guid),action);},weak,guid,action);
        return scene_->AddIconAction(buttons,std::move(callback),std::move(text),&icon);
      };
      row.pause = button(T(u"Wstrzymaj"), vector_icons::kPauseIcon, Action::kPauseResume);
      row.cancel = button(T(u"Anuluj"), vector_icons::kCloseSmallIcon, Action::kCancel);
      row.retry = button(T(u"Pobierz ponownie"), vector_icons::kReplayIcon, Action::kRetry);
      row.reveal = button(T(u"Pokaż w Finderze"), vector_icons::kFolderOpenIcon, Action::kReveal);
      row.more = scene_->AddMoreAction(buttons, base::BindRepeating([](base::WeakPtr<DownloadPanel> panel, std::string guid, views::View* anchor) {
        if (panel) panel->ShowRowMenu(std::move(guid), anchor); }, weak, guid, nullptr), T(u"Więcej"));
      SetSidebarPanelLayout(buttons,false,gfx::Insets(),2);
      SetSidebarPanelLayout(box,false,gfx::Insets::VH(8,12),12,column);
      rows_.push_back(row);
    }
  }
  auto weak = weak_factory_.GetWeakPtr();
  for (auto& row : rows_) {
    auto* item = items_.at(row.guid).get();
    const auto file = item->GetFileNameToReportUser();
    auto name = file.LossyDisplayName();
    std::u16string extension = file.Extension().empty() ? std::u16string() : base::FilePath(file.Extension()).AsUTF16Unsafe().substr(1);
    scene_->SetFileTile(row.tile, extension.empty() ? u"—" : extension);
    scene_->SetFileTileChoice(row.tile, choosing_, chosen_.contains(row.guid), base::BindRepeating([](base::WeakPtr<DownloadPanel> panel, std::string guid) {
      if (panel) panel->Choose(guid, !panel->chosen_.contains(guid)); }, weak, row.guid));
    SetText(row.title,name.empty()?T(u"Przygotowywanie pliku…"):name);
    row.title->SetTooltipText(item->GetTargetFilePath().LossyDisplayName());
    const auto rust = RustRow(item);
    const auto tone = rust.tone == 1 ? PanelScene::Tone::kOk : rust.tone == 2 ? PanelScene::Tone::kWarn : rust.tone == 3 ? PanelScene::Tone::kDanger : PanelScene::Tone::kMuted;
    scene_->SetTextStyle(row.state, 6, tone);
    SetText(row.state, base::UTF8ToUTF16(std::string(rust.state)));
    row.state->SetVisible(!rust.state.empty());
    SetText(row.meta, base::UTF8ToUTF16(std::string(rust.meta)));
    const bool active = Active(item);
    const bool safe = !Unsafe(item);
    row.progress->GetViewAccessibility().SetName(TF(u"Postęp pobierania: {}", {name}));
    row.progress->SetVisible(active);
    scene_->SetProgressPaused(row.progress, item->IsPaused());
    const double value=item->PercentComplete()<0?-1.0:item->PercentComplete()/100.0;scene_->SetProgress(row.progress,value);
    // While choosing, the checkbox is the row's only control.
    row.pause->SetVisible(!choosing_ && safe && (active || item->CanResume()));
    const bool resume = item->IsPaused() || item->GetState() == Item::INTERRUPTED;
    scene_->SetIconAction(row.pause, resume ? T(u"Wznów") : T(u"Wstrzymaj"), resume ? &vector_icons::kPlayArrowIcon : &vector_icons::kPauseIcon);
    row.pause->SetEnabled((active && !item->IsPaused()) || item->CanResume());
    row.cancel->SetVisible(!choosing_ && (active || item->GetState() == Item::INTERRUPTED));
    row.reveal->SetVisible(!choosing_ && safe && OnDisk(item));
    row.retry->SetVisible(!choosing_ && safe && !active && !item->CanResume() && (Failed(item) || item->GetFileExternallyRemoved()));
    row.more->SetVisible(!choosing_);
  }
  RefreshChrome(listed.size(), items_.size());
  static_cast<DownloadListView*>(list_.get())->ContentsChanged();scene_->Schedule();
}
void DownloadPanel::RefreshChrome(size_t shown, size_t total) {
  int64_t bytes = 0, chosen_bytes = 0; size_t all = 0;
  for (const auto& [guid, item] : items_) {
    if (item->IsTransient() || hidden_.contains(guid)) continue;
    ++all; const int64_t size = std::max<int64_t>(0, item->GetTotalBytes() > 0 ? item->GetTotalBytes() : item->GetReceivedBytes());
    bytes += size; if (chosen_.contains(guid)) chosen_bytes += size;
  }
  const bool filtered = !query_.empty() || state_ != StateFilter::kAll || category_;
  // „24 pliki · 3,1 GB”, „4 z 24” while filtered, „3 zaznaczone” while choosing.
  SetText(count_, choosing_ ? Count(chosen_.size(), T(u"zaznaczony"), T(u"zaznaczone"), T(u"zaznaczonych"))
                            : filtered ? TF(u"{} z {}", {base::NumberToString16(shown), base::NumberToString16(all)})
                            : all ? Files(all) + u" · " + Bytes(bytes) : std::u16string());
  scene_->SetTextStyle(count_, 8, choosing_ ? PanelScene::Tone::kInk : PanelScene::Tone::kMuted);
  choose_->SetVisible(!choosing_); folder_->SetVisible(!choosing_); choosing_actions_->SetVisible(choosing_);
  const std::array<std::u16string, 4> states = {T(u"Wszystkie"), T(u"W toku"), T(u"Gotowe"), T(u"Przerwane")};
  for (size_t i = 0; i < chips_.size(); ++i) {
    const auto filter = static_cast<StateFilter>(i);
    scene_->SetActionText(chips_[i], states[i] + u" " + base::NumberToString16(count_for_testing(filter)));
    scene_->SetActionKind(chips_[i], state_ == filter ? 7 : 6);
    chips_[i]->GetViewAccessibility().SetIsSelected(state_ == filter);
  }
  const std::array<std::u16string, 6> kinds = {T(u"Typ"), T(u"Inne"), T(u"Dokumenty"), T(u"Obrazy i wideo"), T(u"Archiwa"), T(u"Aplikacje")};
  const auto& kind = kinds[std::min<size_t>(category_, 5)];
  scene_->SetActionText(type_, kind);
  scene_->SetActionKind(type_, category_ ? 7 : 6);
  type_->GetViewAccessibility().SetName(TF(u"Typ pliku: {}", {category_ ? kind : T(u"wszystkie")}));
  scene_->RefreshSearch(search_);
  footer_->SetVisible(!choosing_); bulk_->SetVisible(choosing_);
  SetText(bulk_size_, chosen_.empty() ? T(u"Zaznacz pliki na liście") : Bytes(chosen_bytes));
  bulk_delete_->SetEnabled(!chosen_.empty());
  // The folder with the home directory as „~”.
  if (profile_) {
    auto folder = DownloadPrefs::FromBrowserContext(profile_)->DownloadPath();
    base::FilePath relative;
    const auto home = base::GetHomeDir();
    std::u16string text = home.AppendRelativePath(folder, &relative) ? u"~/" + relative.LossyDisplayName() : folder.LossyDisplayName();
    SetText(path_, text); path_->GetViewAccessibility().SetName(text);
  }
  (void)total;
}
void DownloadPanel::ShowMenu(std::unique_ptr<ui::SimpleMenuModel> model, views::View* anchor) {
  if (!widget_ || !anchor) return;
  menu_model_ = std::move(model);
  menu_runner_ = std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::NO_FLAGS);
  menu_runner_->RunMenuAt(widget_.get(), nullptr, anchor->GetBoundsInScreen(), views::MenuAnchorPosition::kTopRight,
                          ui::mojom::MenuSourceType::kMouse, gfx::NativeView(), gfx::RoundedCornersF(12));
}
// Design: Okno Pobrane, menu wiersza — what a file allows, then the two removals.
void DownloadPanel::ShowRowMenu(std::string guid, views::View* anchor) {
  if (!items_.contains(guid)) return;
  menu_guid_ = guid;
  if (!anchor) for (const auto& row : rows_) if (row.guid == guid) anchor = row.more;
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  model->AddItem(kRowOpen, T(u"Otwórz"));
  model->AddItem(kRowReveal, T(u"Pokaż w Finderze"));
  model->AddItem(kRowCopy, T(u"Kopiuj adres pliku"));
  model->AddItem(kRowRetry, T(u"Pobierz ponownie"));
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItem(kRowRemove, T(u"Usuń z listy"));
  model->AddItem(kRowDelete, T(u"Usuń z dysku…"));
  ShowMenu(std::move(model), anchor);
}
void DownloadPanel::ShowTypeMenu(views::View* anchor) {
  // The kinds with how many files each has under the search and state filter.
  std::array<int, 6> counts = {};
  for (const auto& [guid, item] : items_) if (Passes(item, true, false)) { ++counts[0]; ++counts[sidebar::download_category(Utf8Extension(item)) + 1]; }
  auto model = std::make_unique<ui::SimpleMenuModel>(this);
  const std::array<std::pair<uint32_t, std::u16string>, 6> kinds = {{{0, T(u"Wszystkie typy")}, {2, T(u"Dokumenty")}, {3, T(u"Obrazy i wideo")}, {4, T(u"Archiwa")}, {5, T(u"Aplikacje")}, {1, T(u"Inne")}}};
  for (const auto& [kind, label] : kinds) model->AddCheckItem(kTypeBase + static_cast<int>(kind), label + u" · " + base::NumberToString16(counts[kind]));
  ShowMenu(std::move(model), anchor);
}
void DownloadPanel::ShowClearMenu(views::View* anchor) {
  const auto cutoff = base::Time::Now() - base::Days(30);
  std::array<int, 5> counts = {};
  for (const auto& [guid, item] : items_) {
    if (item->IsTransient() || hidden_.contains(guid) || Active(item)) continue;
    counts[0] += item->GetState() == Item::COMPLETE; counts[1] += Failed(item); counts[2] += item->GetStartTime() < cutoff;
    counts[3] += item->GetState() == Item::COMPLETE && item->GetFileExternallyRemoved(); ++counts[4];
  }
  auto model = std::make_unique<ClearMenuModel>(this);
  auto add = [&](Clear what, std::u16string label) { const int n = counts[static_cast<size_t>(what)]; model->AddItem(kClearBase + static_cast<int>(what), label + u" · " + base::NumberToString16(n)); };
  add(Clear::kDone, T(u"Wyczyść wszystkie gotowe"));
  add(Clear::kFailed, T(u"Usuń przerwane i anulowane"));
  add(Clear::kOlderThan30Days, T(u"Usuń starsze niż 30 dni"));
  add(Clear::kMissing, T(u"Usuń te, których nie ma na dysku"));
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  add(Clear::kAll, T(u"Wyczyść całą listę"));
  model->SetStrong(kClearBase + static_cast<int>(Clear::kAll));
  ShowMenu(std::move(model), anchor);
}
bool DownloadPanel::IsCommandIdChecked(int id) const { return id >= kTypeBase && id < kClearBase && static_cast<uint32_t>(id - kTypeBase) == category_; }
bool DownloadPanel::IsCommandIdEnabled(int id) const {
  if (id >= kClearBase || id >= kTypeBase) return true;
  auto found = items_.find(menu_guid_);
  if (found == items_.end()) return false;
  Item* item = found->second;
  const bool safe = !Unsafe(item);
  switch (id) {
    case kRowOpen: case kRowReveal: return safe && OnDisk(item);
    case kRowCopy: return item->GetURL().is_valid();
    case kRowRetry: return safe && !Active(item) && (item->CanResume() || Failed(item) || item->GetFileExternallyRemoved());
    case kRowRemove: return true;
    case kRowDelete: return true;
  }
  return false;
}
void DownloadPanel::ExecuteCommand(int id, int) {
  if (id >= kClearBase) { ClearList(static_cast<Clear>(id - kClearBase)); return; }
  if (id >= kTypeBase) { SetCategory(static_cast<uint32_t>(id - kTypeBase)); return; }
  static constexpr std::array<Action, 6> kActions = {Action::kOpen, Action::kReveal, Action::kCopyLink, Action::kRetry, Action::kRemove, Action::kDelete};
  if (id >= kRowOpen && id <= kRowDelete) Act(menu_guid_, kActions[id - kRowOpen]);
}
void DownloadPanel::OpenFolder() {
  if (!profile_) return;
  platform_util::OpenItem(profile_, DownloadPrefs::FromBrowserContext(profile_)->DownloadPath(), platform_util::OPEN_FOLDER, base::DoNothing());
}
void DownloadPanel::RemoveChosen() {
  std::vector<std::string> guids;
  for (const auto& guid : chosen_) if (items_.contains(guid)) guids.push_back(guid);
  if (guids.empty()) return;
  SetChoosing(false); RemoveLater(std::move(guids));
}
void DownloadPanel::DeleteChosen() {
  std::vector<std::string> guids(chosen_.begin(), chosen_.end());
  std::erase_if(guids, [&](const auto& guid) { return !items_.contains(guid); });
  if (guids.empty() || !confirm_) return;
  // Design: Potwierdzenie — no way back is offered, so the warning says so.
  std::vector<std::u16string> names;
  for (const auto& guid : guids) names.push_back(items_.at(guid)->GetFileNameToReportUser().LossyDisplayName());
  std::u16string detail = names.front();
  if (names.size() == 2) detail = TF(u"{} i {}", {names[0], names[1]});
  else if (names.size() > 2) detail = TF(u"{}, {} i {}", {names[0], names[1], Count(names.size() - 2, T(u"inny"), T(u"inne"), T(u"innych"))});
  const bool one = guids.size() == 1;
  confirm_.Run(widget_.get(), one ? T(u"Usunąć plik z dysku?") : TF(u"Usunąć {} z dysku?", {Files(guids.size())}), std::move(detail),
      one ? T(u"Ten plik zniknie z dysku i z tej listy.") : T(u"Te pliki znikną z dysku i z tej listy."), T(u"Usuń z dysku"),
      base::BindOnce([](base::WeakPtr<DownloadPanel> panel, std::vector<std::string> guids, bool yes) {
        if (!panel || !yes) return; panel->SetChoosing(false); panel->Delete(std::move(guids)); }, model_weak_factory_.GetWeakPtr(), std::move(guids)));
}
void DownloadPanel::ClearList(Clear what) {
  const auto cutoff = base::Time::Now() - base::Days(30);
  std::vector<std::string> guids;
  for (const auto& [guid, item] : items_) {
    if (item->IsTransient() || hidden_.contains(guid) || Active(item)) continue;
    const bool take = what == Clear::kAll || (what == Clear::kDone && item->GetState() == Item::COMPLETE) || (what == Clear::kFailed && Failed(item)) ||
        (what == Clear::kOlderThan30Days && item->GetStartTime() < cutoff) || (what == Clear::kMissing && item->GetState() == Item::COMPLETE && item->GetFileExternallyRemoved());
    if (take) guids.push_back(guid);
  }
  if (!guids.empty()) RemoveLater(std::move(guids));
}
// Removal from the list is undoable for as long as its toast stays (6 s). A
// file still arriving, or cut off midway, then goes from the disk too:
// Chromium's Remove() discards the partial file with the transfer.
void DownloadPanel::RemoveLater(std::vector<std::string> guids) {
  CommitRemoval();
  hidden_.insert(guids.begin(), guids.end());
  for (const auto& guid : guids) chosen_.erase(guid);
  const uint64_t batch = ++removal_batch_;
  removal_timer_.Start(FROM_HERE, base::Seconds(7), base::BindOnce(&DownloadPanel::CommitRemoval, model_weak_factory_.GetWeakPtr()));
  if (removed_) removed_.Run(static_cast<int>(guids.size()), base::BindOnce(&DownloadPanel::UndoRemoval, model_weak_factory_.GetWeakPtr(), batch));
  ScheduleRefresh(); ReportProgress();
}
void DownloadPanel::CommitRemoval() {
  removal_timer_.Stop();
  const auto hidden = std::move(hidden_); hidden_.clear();
  for (const auto& guid : hidden) if (auto found = items_.find(guid); found != items_.end()) found->second->Remove();
}
void DownloadPanel::UndoRemoval(uint64_t batch) {
  if (batch != removal_batch_ || hidden_.empty()) return;
  removal_timer_.Stop(); hidden_.clear(); layout_.clear(); ScheduleRefresh();
}
void DownloadPanel::Delete(std::vector<std::string> guids) {
  for (const auto& guid : guids) {
    auto found = items_.find(guid);
    if (found == items_.end()) continue;
    Item* item = found->second;
    if (OnDisk(item) && !Unsafe(item)) { item->DeleteFile(base::BindOnce(&DownloadPanel::Deleted, model_weak_factory_.GetWeakPtr(), guid)); continue; }
    // A partial file goes with its transfer; one already gone only leaves the list.
    if (Active(item) || item->GetState() == Item::INTERRUPTED) item->Cancel(true);
    if (items_.contains(guid)) items_.at(guid)->Remove();
  }
}
void DownloadPanel::Deleted(std::string guid, bool) {
  if (auto found = items_.find(guid); found != items_.end()) found->second->Remove();
}
bool DownloadPanel::Act(std::string guid, Action action) {
  auto found = items_.find(guid);
  if (found == items_.end()) return false;
  auto* item = found->second.get();
  switch (action) {
    case Action::kCancel:
      if (!Active(item) && item->GetState() != Item::INTERRUPTED) return false;
      item->Cancel(true); break;
    case Action::kPauseResume:
      if (Unsafe(item)) return false;
      if (item->IsPaused() || item->GetState() == Item::INTERRUPTED) {
        if (!item->CanResume()) return false;
        item->Resume(true);
      } else if (Active(item)) item->Pause();
      else return false;
      break;
    case Action::kReveal: case Action::kOpen:
      if (Unsafe(item) || !OnDisk(item)) return false;
      if (action == Action::kReveal) item->ShowDownloadInShell(); else item->OpenDownload();
      break;
    case Action::kCopyLink:
      if (!item->GetURL().is_valid()) return false;
      ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(base::UTF8ToUTF16(item->GetURL().spec()));
      break;
    case Action::kRetry:
      if (Unsafe(item) || Active(item)) return false;
      if (item->CanResume()) { item->Resume(true); break; }
      if (!redownload_ || !(Failed(item) || item->GetFileExternallyRemoved())) return false;
      redownload_.Run(item->GetURL()); break;
    case Action::kRemove:
      RemoveLater({guid}); break;
    case Action::kDelete: {
      if (!confirm_) return false;
      chosen_ = {guid}; DeleteChosen(); chosen_.clear(); break;
    }
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
void DownloadPanel::OnDownloadRemoved(Item* item) { item->RemoveObserver(this); items_.erase(item->GetGuid()); in_progress_.erase(item->GetGuid()); chosen_.erase(item->GetGuid()); hidden_.erase(item->GetGuid()); ReportProgress(); ScheduleRefresh(); }
void DownloadPanel::OnDownloadDestroyed(Item* item) { OnDownloadRemoved(item); }
}  // namespace zitter::prototype
