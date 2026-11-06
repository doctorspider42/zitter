// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/rss_reader.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/i18n/time_formatting.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_action_data.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#include "chromium/prototype/workspace_frame_mac.h"
#elif BUILDFLAG(IS_WIN)
#include "chromium/prototype/workspace_frame_win.h"
#endif
#include "ui/base/metadata/metadata_header_macros.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
namespace zitter::prototype {
namespace {
// Native focus/AX targets, painted by the shared compositor/Metal panel scene.
class RssBody final : public views::View {
  METADATA_HEADER(RssBody, views::View)
 public:
  void Changed() { InvalidateLayout(); PreferredSizeChanged(); }
};
BEGIN_METADATA(RssBody)
END_METADATA
std::u16string U(std::string_view s) { return base::UTF8ToUTF16(s); }
std::string DateLabel(base::Time time) {
  if (time.is_null()) return base::UTF16ToUTF8(T(u"Bez daty"));
  base::Time::Exploded local; time.LocalExplode(&local);
  return base::StringPrintf("%04d-%02d-%02d %02d:%02d",local.year,local.month,local.day_of_month,local.hour,local.minute);
}
}
RssPanel::RssPanel() = default;
RssPanel::~RssPanel() { Close(); }
void RssPanel::OnProfileWillBeDestroyed(Profile*) { Close(); }
void RssPanel::OnWidgetDestroying(views::Widget*) { Close(); }
void RssPanel::OnWidgetThemeChanged(views::Widget* parent) {
  if (!widget_) return;
  widget_->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  SetWorkspaceNativeAppearance(widget_.get(), parent->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark);
#endif
  scene_->RefreshSearch(address_); scene_->Schedule();
}
void RssPanel::SynchronizeTheme() {
  if (widget_ && parent_observation_.IsObserving()) OnWidgetThemeChanged(parent_observation_.GetSource());
}
bool RssPanel::CanHandleAccelerators() const { return widget_ && widget_->IsVisible(); }
bool RssPanel::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (!widget_ || accelerator.key_code() != ui::VKEY_RETURN) return false;
  auto* focused=widget_->GetFocusManager()->GetFocusedView(); if (!focused) return false;
  ui::AXNodeData data; focused->GetViewAccessibility().GetAccessibleNodeData(&data);
  if (data.role != ax::mojom::Role::kButton) return false;
  ui::AXActionData action; action.action=ax::mojom::Action::kDoDefault;
  return focused->HandleAccessibleAction(action);
}
void RssPanel::Close() {
  if (widget_) widget_->GetFocusManager()->UnregisterAccelerators(this);
  parent_observation_.Reset(); ++discovery_generation_; update_pending_ = false; observation_.Reset(); subscription_ = {}; weak_.InvalidateWeakPtrs(); scene_.reset(); body_ = nullptr; add_row_ = nullptr; add_button_ = nullptr; address_ = nullptr; viewport_ = nullptr;
  widget_.reset(); delegate_.reset(); reader_ = nullptr; profile_ = nullptr;
}
views::View* RssPanel::Text(views::View* parent, std::string text, int style) {
  auto* view = parent->AddChildView(scene_->MakeText(U(text), true, false, 0));
  scene_->SetTextStyle(view, style, style == 6 || style == 8 ? PanelScene::Tone::kMuted : PanelScene::Tone::kInk); return view;
}
views::View* RssPanel::Action(views::View* parent, std::u16string text, base::RepeatingClosure callback, int kind) {
  auto* button = scene_->AddAction(parent, std::move(callback), std::move(text)); scene_->SetActionKind(button, kind); button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS); return button;
}
void RssPanel::Show(Profile* profile, views::Widget* parent, GURL current, base::RepeatingCallback<void(GURL)> open) {
  if (widget_ && (widget_->IsClosed() || profile_ != profile)) Close();
  if (widget_) {
    OnWidgetThemeChanged(parent); widget_->Show(); widget_->Activate();
    // Parent theme notifications may coalesce an intermediate override away.
    // Re-read its final state after this event, including hidden-panel reopen.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&RssPanel::SynchronizeTheme,weak_.GetWeakPtr()));
    return;
  }
  profile_ = profile; parent_observation_.Observe(parent); observation_.Observe(profile); reader_ = RssReader::For(profile); open_ = std::move(open); current_ = current; source_ = {}; adding_ = false; selected_.clear(); discovered_.clear();
  auto weak = weak_.GetWeakPtr();
  delegate_ = std::make_unique<views::DialogDelegate>();
  delegate_->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate_->SetTitle(T(u"Czytnik RSS") + u" · " + (profile->IsOffTheRecord() ? T(u"Okno prywatne") : BrowsingProfileName(profile))); delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate_->SetCanResize(true); delegate_->set_use_custom_frame(false); delegate_->set_draggable(true);
  auto content = std::make_unique<views::View>(); scene_ = CreatePanelScene(content.get());
  auto* header = content->AddChildView(std::make_unique<views::View>());
  auto* title = Text(header, base::UTF16ToUTF8(T(u"Czytnik RSS")), 2);
  auto* tools = header->AddChildView(std::make_unique<views::View>());
  auto* add_actions = tools->AddChildView(scene_->MakeActions(0));
  add_button_ = Action(add_actions, T(u"Dodaj źródło"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->adding_ = !p->adding_; p->ScheduleUpdate(); } },weak));
  scene_->AddIconAction(tools,base::BindRepeating([](base::WeakPtr<RssPanel> p){if(p)p->reader_->Refresh();},weak),T(u"Odśwież źródła"),&vector_icons::kRefreshIcon)->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetSidebarPanelLayout(tools,false,{},4);
  title->SetTooltipText(profile->IsOffTheRecord() ? T(u"Tryb prywatny · tylko w pamięci tego okna") : T(u"Źródła i odczyty są wspólne dla przestrzeni tego profilu."));
  SetSidebarPanelLayout(header, false, {}, 8, title, true);
  auto* add = content->AddChildView(std::make_unique<views::View>()); add_row_ = add;
  address_ = scene_->AddSearch(add, T(u"Adres kanału RSS lub Atom"), T(u"Adres kanału RSS lub Atom"));
  address_->SetCallbacks(base::DoNothing(), base::BindRepeating([](base::WeakPtr<RssPanel> p, const ui::KeyEvent& e) {
    if (!p || e.type() != ui::EventType::kKeyPressed || e.key_code() != ui::VKEY_RETURN) return false;
    p->Subscribe(); return true;
  }, weak));
  auto* buttons = add->AddChildView(scene_->MakeActions(0));
  Action(buttons, T(u"Subskrybuj"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) p->Subscribe(); }, weak), 0);
  SetSidebarPanelLayout(add, false, {}, 8, address_->view(), true);
  PanelViewport* adapter = nullptr;
  auto viewport = CreatePanelViewport(&adapter, scene_->ViewportChangedCallback(), {100, 1200, false, true, false});
  viewport_ = adapter;
  auto* scroll = content->AddChildView(std::move(viewport));
  auto body = std::make_unique<RssBody>(); body_ = body.get(); SetSidebarPanelLayout(body_, true, {}, 8); viewport_->SetPanelContents(std::move(body));
  SetSidebarPanelLayout(content.get(), true, gfx::Insets(20), 16, scroll);
  content->SetPreferredSize(gfx::Size(780, 720)); delegate_->SetContentsView(std::move(content));
  widget_.reset(views::DialogDelegate::CreateDialogWidget(delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView()));
  widget_->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<RssPanel> p, views::Widget::ClosedReason) { if (p) p->Close(); }, weak));
  widget_->GetFocusManager()->RegisterAccelerator(ui::Accelerator(ui::VKEY_RETURN,ui::EF_NONE),ui::AcceleratorManager::kNormalPriority,this);
  OnWidgetThemeChanged(parent);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&RssPanel::SynchronizeTheme,weak));
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget_.get(), true);
#endif
  auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(820, area.width()), std::min(760, area.height()));
  bounds.set_origin(parent->GetWindowBoundsInScreen().CenterPoint() - gfx::Vector2d(bounds.width()/2, bounds.height()/2)); bounds.AdjustToFit(area); widget_->SetBounds(bounds);
  subscription_ = reader_->Observe(base::BindRepeating(&RssPanel::ScheduleUpdate, weak));
  Update(); widget_->Show(); scene_->Schedule(); if (add_row_->GetVisible()) address_->RequestFocus();
}
void RssPanel::BeginDiscovery() { ++discovery_generation_; discovered_.clear(); ScheduleUpdate(); }
void RssPanel::DiscoverFromPage(base::Value value) {
  if (!value.is_list()) return;
  std::vector<GURL> urls;
  for (const auto& item : value.GetList()) if (item.is_string()) {
    GURL url(item.GetString());
    if (url.SchemeIsHTTPOrHTTPS() && url.spec().size() <= 2048 && !url.has_username() && !url.has_password() && std::find(urls.begin(), urls.end(), url) == urls.end()) urls.push_back(url);
  }
  Discover(std::move(urls));
}
void RssPanel::Discover(std::vector<GURL> urls) { discovered_ = std::move(urls); ScheduleUpdate(); }
bool RssPanel::contains_for_testing(std::string_view text) const { return scene_ && scene_->contains_text(text); }
void RssPanel::Select(GURL feed, std::string id) {
  article_feed_ = feed; selected_ = id;
  for (const auto& f : reader_->feeds()) if (f.url == feed) for (const auto& a : f.articles) if (a.id == id) { return_focus_name_ = TF(u"Czytaj: {}", {U(a.title)}); reader_->Mark(feed, id, true, a.saved); viewport_->ScrollTo(0); return; }
  ScheduleUpdate();
}
void RssPanel::Subscribe() {
  const auto count=reader_->feeds().size(); reader_->Add(GURL(address_->GetText()));
  if (reader_->feeds().size()>count) adding_=false;
  ScheduleUpdate();
}
void RssPanel::ScheduleUpdate() {
  if (update_pending_ || !body_) return;
  update_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&RssPanel::Update, weak_.GetWeakPtr()));
}
void RssPanel::Update() {
  update_pending_ = false;
  if (!body_) return;
  const bool empty=reader_->feeds().empty();
  const bool was_adding=add_row_->GetVisible(); add_row_->SetVisible(empty||adding_);
  add_button_->SetVisible(!empty||adding_); scene_->SetActionText(add_button_,adding_?T(u"Gotowe"):T(u"Dodaj źródło"));
  if (!was_adding && add_row_->GetVisible()) address_->RequestFocus();
  std::u16string focus_name;
  auto* focused = widget_->GetFocusManager()->GetFocusedView();
  if (focused && body_->Contains(focused)) {
    ui::AXNodeData data; focused->GetViewAccessibility().GetAccessibleNodeData(&data);
    focus_name = data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  }
  base::ScopedClosureRunner restore_focus(base::BindOnce([](base::WeakPtr<RssPanel> p, std::u16string name) {
    if (!p || name.empty()) return;
    if (name == T(u"Wróć do wpisów") && p->selected_.empty()) name = p->return_focus_name_;
    if (name == T(u"Zapisz artykuł")) name = T(u"Usuń zapis"); else if (name == T(u"Usuń zapis")) name = T(u"Zapisz artykuł");
    auto find = [&](auto&& self, views::View* view) -> views::View* {
      ui::AXNodeData data; view->GetViewAccessibility().GetAccessibleNodeData(&data);
      if (view->GetFocusBehavior() == views::View::FocusBehavior::ALWAYS && data.GetString16Attribute(ax::mojom::StringAttribute::kName) == name) return view;
      for (const auto& child : view->children()) if (auto* match = self(self, child.get())) return match;
      return nullptr;
    };
    if (auto* target = find(find, p->body_)) target->RequestFocus();
    else if (!p->selected_.empty()) { name = T(u"Wróć do wpisów"); if (auto* back = find(find, p->body_)) back->RequestFocus(); }
  }, weak_.GetWeakPtr(), focus_name));
  body_->RemoveAllChildViews(); auto weak = weak_.GetWeakPtr();
  if (!source_.is_empty() && std::none_of(reader_->feeds().begin(), reader_->feeds().end(), [&](const auto& f) { return f.url == source_; })) source_ = {};
  // Shared profile data, but all explicit navigation is bound to this panel's
  // original Profile by the window callback (even after a workspace switch).
  if (empty) Text(body_, base::UTF16ToUTF8(profile_->IsOffTheRecord() ? T(u"Tryb prywatny · tylko w pamięci tego okna") : T(u"Źródła i odczyty są wspólne dla przestrzeni tego profilu.")), 6);
  if (!reader_->ready()) Text(body_, base::UTF16ToUTF8(T(u"Wczytywanie źródeł…")), 6);
  if (reader_->busy()) Text(body_, base::UTF16ToUTF8(T(u"Odświeżanie źródeł…")), 6);
  if (!reader_->status().empty()) Text(body_, base::UTF16ToUTF8(T(U(reader_->status()))), 6);
  if (!selected_.empty()) {
    for (const auto& f : reader_->feeds()) if (f.url == article_feed_) for (const auto& a : f.articles) if (a.id == selected_) {
      auto* nav = body_->AddChildView(scene_->MakeActions(6));
      Action(nav, T(u"Wróć do wpisów"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->selected_.clear(); p->ScheduleUpdate(); p->viewport_->ScrollTo(0); } }, weak));
      Text(body_, a.title, 3); Text(body_, f.title + " · " + DateLabel(a.published), 8);
      auto* actions = body_->AddChildView(scene_->MakeActions(6));
      Action(actions, T(u"Otwórz artykuł"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p && p->open_) p->open_.Run(url); }, weak, a.url), 0);
      Action(actions, a.saved ? T(u"Usuń zapis") : T(u"Zapisz artykuł"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL feed, std::string id, bool saved) { if (p) p->reader_->Mark(feed, id, true, !saved); }, weak, f.url, a.id, a.saved));
      Action(actions, T(u"Nieprzeczytany"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL feed, std::string id, bool saved) { if (p) { p->selected_.clear(); p->reader_->Mark(feed, id, false, saved); } }, weak, f.url, a.id, a.saved));
      Text(body_, a.text.empty() ? base::UTF16ToUTF8(T(u"To źródło udostępnia tylko tytuł. Otwórz artykuł, aby przeczytać całość.")) : a.text);
      static_cast<RssBody*>(body_.get())->Changed(); scene_->Schedule(); return;
    }
    selected_.clear();
  }
  if (!empty) {
  auto* filters = body_->AddChildView(scene_->MakeActions(4));
  Action(filters, T(u"Wszystkie"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->unread_ = false; p->saved_ = false; p->source_ = {}; p->ScheduleUpdate(); } }, weak), !unread_ && !saved_ && source_.is_empty() ? 7 : 6);
  Action(filters, T(u"Nieprzeczytane"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->unread_ = !p->unread_; p->ScheduleUpdate(); } }, weak), unread_ ? 7 : 6);
  Action(filters, T(u"Zapisane"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->saved_ = !p->saved_; p->ScheduleUpdate(); } }, weak), saved_ ? 7 : 6);
  }
  for (const auto& url : discovered_) {
    if (std::any_of(reader_->feeds().begin(), reader_->feeds().end(), [&](const auto& f) { return f.url == url; })) continue;
    auto* row = body_->AddChildView(scene_->MakeActions(4));
    Action(row, TF(u"Subskrybuj kanał: {}", {U(url.host())}), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p) p->reader_->Add(url); }, weak, url));
    Text(body_, url.spec(), 8);
  }
  if (reader_->feeds().empty()) {
    Text(body_, base::UTF16ToUTF8(T(u"Twoje źródła, w jednym miejscu")), 3);
    Text(body_, base::UTF16ToUTF8(T(u"Wklej adres RSS lub Atom powyżej. Nowe wpisy pojawią się tutaj, bez reklam i automatycznie ładowanych obrazów.")));
    static_cast<RssBody*>(body_.get())->Changed(); scene_->Schedule(); return;
  }
  auto* source_row = body_->AddChildView(std::make_unique<views::View>());
  std::vector<std::u16string> labels{T(u"Wszystkie źródła")};
  std::vector<GURL> feed_urls{GURL()}; size_t selected_source = 0;
  for (const auto& f : reader_->feeds()) { labels.push_back(U(f.title)); feed_urls.push_back(f.url); if (source_ == f.url) selected_source = labels.size() - 1; }
  auto* picker = source_row->AddChildView(scene_->MakeSelect(source_row, labels, T(u"Źródło wpisów")));
  SetSceneSelectIndex(picker, selected_source);
  SetSceneSelectCallback(picker, base::BindRepeating([](base::WeakPtr<RssPanel> p, views::View* picker, std::vector<GURL> urls) {
    if (!p) return; const auto index = SceneSelectIndex(picker).value_or(0);
    if (index < urls.size()) { p->source_ = urls[index]; p->ScheduleUpdate(); }
  }, weak, picker, feed_urls));
  auto* manage = source_row->AddChildView(scene_->MakeActions(0));
  Action(manage, T(u"Zarządzaj źródłami"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->managing_ = !p->managing_; p->ScheduleUpdate(); } }, weak));
  scene_->AddIconAction(source_row,base::BindRepeating([](base::WeakPtr<RssPanel> p){if(p)p->reader_->MarkAll(p->source_);},weak),T(u"Oznacz jako przeczytane"),&vector_icons::kChecklistIcon)->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetSidebarPanelLayout(source_row, false, {}, 8, picker, true);
  // Compact source rows double as source management. Lists and previews share
  // one viewport, keeping narrow windows usable and long titles fully visible.
  if (managing_) for (const auto& feed : reader_->feeds()) {
    auto* row = body_->AddChildView(std::make_unique<views::View>());
    auto* label = Text(row, feed.title, 5);
    auto* actions = row->AddChildView(scene_->MakeActions(4));
    Action(actions, source_ == feed.url ? T(u"Wybrane źródło") : T(u"Wybierz"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p) { p->source_ = p->source_ == url ? GURL() : url; p->ScheduleUpdate(); } }, weak, feed.url), source_ == feed.url ? 7 : 3);
    Action(actions, T(u"Usuń"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p) { if (p->source_ == url) p->source_ = {}; p->reader_->Remove(url); } }, weak, feed.url), 4);
    SetSidebarPanelLayout(row, false, {}, 8, label, true);
    if (!feed.error.empty()) Text(body_, base::UTF16ToUTF8(T(u"Nie udało się odświeżyć źródła. Sprawdź adres lub spróbuj ponownie.")), 6);
  }
  if (!managing_) for (const auto& f : reader_->feeds()) if (!f.error.empty()) Text(body_, f.title + ": " + base::UTF16ToUTF8(T(u"Nie udało się odświeżyć źródła. Sprawdź adres lub spróbuj ponownie.")), 6);
  size_t count = 0;
  struct Item { raw_ptr<const RssFeed> feed; raw_ptr<const RssArticle> article; };
  std::vector<Item> items;
  for (const auto& f : reader_->feeds()) for (const auto& a : f.articles)
    if ((source_.is_empty() || f.url == source_) && (!unread_ || !a.read) && (!saved_ || a.saved)) items.push_back({&f, &a});
  std::stable_sort(items.begin(), items.end(), [](const auto& x, const auto& y) { return x.article->published > y.article->published; });
  for (const auto& item : items) {
    const auto& f = *item.feed; const auto& a = *item.article;
    if (count >= visible_limit_) break;
    {

      auto* row = body_->AddChildView(std::make_unique<views::View>());
      auto* column = row->AddChildView(std::make_unique<views::View>());
      auto* read_actions = row->AddChildView(scene_->MakeActions(0));
      auto* read_button = Action(read_actions, T(u"Czytaj"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL feed, std::string id) { if (p) p->Select(feed,id); },weak,f.url,a.id));
      read_button->GetViewAccessibility().SetName(TF(u"Czytaj: {}",{U(a.title)}));
      Text(column, a.title, a.read ? 5 : 4); Text(column, f.title + " · " + DateLabel(a.published) + (a.saved ? " · " + base::UTF16ToUTF8(T(u"Zapisane")) : ""), 8);
      auto preview = base::CollapseWhitespace(base::UTF8ToUTF16(a.text),false); if (preview.size() > 180) { preview.resize(180); if (preview.back() >= 0xd800 && preview.back() <= 0xdbff) preview.pop_back(); preview += u"…"; }
      auto* excerpt=column->AddChildView(scene_->MakeText(std::move(preview),false,false,0));
      scene_->SetTextStyle(excerpt,6,PanelScene::Tone::kMuted);
      SetSidebarPanelLayout(column, true, {}, 6);
      SetSidebarPanelLayout(row, false, gfx::Insets::VH(12, 8), 8, column, true); ++count;
    }
  }
  if (items.size() > visible_limit_) {
    auto* more = body_->AddChildView(scene_->MakeActions(0));
    Action(more, T(u"Pokaż kolejne wpisy"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->visible_limit_ += 100; p->ScheduleUpdate(); } }, weak));
  }
  if (!count && !reader_->feeds().empty() && !reader_->busy()) Text(body_, base::UTF16ToUTF8(T(u"Brak wpisów w tym widoku.")), 6);
  static_cast<RssBody*>(body_.get())->Changed(); scene_->Schedule();
}
}
