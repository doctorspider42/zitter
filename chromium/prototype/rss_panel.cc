// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/rss_reader.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/i18n/time_formatting.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/base/cursor/cursor.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_action_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/background.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "ui/views/widget/widget.h"
#include "ui/views/view_class_properties.h"
namespace zitter::prototype {
namespace {
// From this tab width the list and the reading pane sit side by side.
constexpr int kRssWide = 760, kRssListWidth = 320, kRssMeasure = 680;
// Native focus/AX targets, painted by the shared compositor/Metal panel scene.
class RssRoot final : public views::View {
  METADATA_HEADER(RssRoot, views::View)
 public:
  explicit RssRoot(base::RepeatingClosure resized) : resized_(std::move(resized)) {}
  void OnBoundsChanged(const gfx::Rect& previous) override {
    views::View::OnBoundsChanged(previous);
    if ((previous.width() < 420) != (width() < 420) || (previous.width() < kRssWide) != (width() < kRssWide)) resized_.Run();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(sidebar::design_palette(
        GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark, 0).bg_raised, 10));
  }
 private:
  base::RepeatingClosure resized_;
};
BEGIN_METADATA(RssRoot)
END_METADATA
class RssTitle final : public views::View {
  METADATA_HEADER(RssTitle, views::View)
 public:
  RssTitle(std::u16string name, base::RepeatingClosure read) : read_(std::move(read)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(std::move(name));
    GetViewAccessibility().SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);
    UpdatePresentation();
  }
  ui::Cursor GetCursor(const ui::MouseEvent&) override { return ui::Cursor(ui::mojom::CursorType::kHand); }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    pressed_ = event.IsOnlyLeftMouseButton();
    if (pressed_) RequestFocus();
    return pressed_;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    const bool read = pressed_ && HitTestPoint(event.location()); pressed_ = false;
    if (read) read_.Run();
  }
  void OnMouseCaptureLost() override { pressed_ = false; }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if ((event.flags() & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN)) || (event.key_code() != ui::VKEY_RETURN && event.key_code() != ui::VKEY_SPACE)) return false;
    read_.Run(); return true;
  }
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if (action.action != ax::mojom::Action::kDoDefault) return views::View::HandleAccessibleAction(action);
    read_.Run(); return true;
  }
  void OnMouseEntered(const ui::MouseEvent&) override { hovered_ = true; UpdatePresentation(); }
  void OnMouseExited(const ui::MouseEvent&) override { hovered_ = false; UpdatePresentation(); }
  void OnFocus() override { views::View::OnFocus(); UpdatePresentation(); }
  void OnBlur() override { views::View::OnBlur(); UpdatePresentation(); }
  void OnThemeChanged() override { views::View::OnThemeChanged(); UpdatePresentation(); }
 private:
  void UpdatePresentation() {
    const auto palette = sidebar::design_palette(GetWidget() &&
        GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark, WorkspaceColorForView(this));
    const auto color = HasFocus() ? palette.focus : SK_ColorTRANSPARENT;
    SetBackground(hovered_ ? views::CreateRoundedRectBackground(palette.bg_hover, 4) : nullptr);
    SetBorder(views::CreateRoundedRectBorder(2, 4, color));
    SchedulePaint();
  }
  bool pressed_ = false, hovered_ = false;
  base::RepeatingClosure read_;
};
BEGIN_METADATA(RssTitle)
END_METADATA
class RssBody final : public views::View {
  METADATA_HEADER(RssBody, views::View)
 public:
  void Changed() { InvalidateLayout(); PreferredSizeChanged(); }
};
BEGIN_METADATA(RssBody)
END_METADATA
// The list column: a fixed width beside the reading pane, free in one column.
class RssPane final : public views::View {
  METADATA_HEADER(RssPane, views::View)
 public:
  void SetFixedWidth(int width) { if (width_ == width) return; width_ = width; PreferredSizeChanged(); }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override {
    auto size = views::View::CalculatePreferredSize(width_ ? views::SizeBounds(width_, bounds.height()) : bounds);
    if (width_) size.set_width(width_);
    return size;
  }
  gfx::Size GetMinimumSize() const override {
    auto size = views::View::GetMinimumSize(); if (width_) size.set_width(width_); return size;
  }
 private:
  int width_ = 0;
};
BEGIN_METADATA(RssPane)
END_METADATA
// The reading pane's column: a comfortable measure, centred in the pane.
class RssReading final : public views::View {
  METADATA_HEADER(RssReading, views::View)
 public:
  RssReading() { Apply(24); }
  void Changed() { InvalidateLayout(); PreferredSizeChanged(); }
  void OnBoundsChanged(const gfx::Rect& previous) override {
    views::View::OnBoundsChanged(previous);
    const int side = std::max(24, (width() - kRssMeasure) / 2);
    if (side != side_) { Apply(side); Changed(); }
  }
 private:
  void Apply(int side) { side_ = side; SetSidebarPanelLayout(this, true, gfx::Insets::VH(28, side), 16); }
  int side_ = -1;
};
BEGIN_METADATA(RssReading)
END_METADATA
std::u16string U(std::string_view s) { return base::UTF8ToUTF16(s); }
// The design palette tinted by the window's workspace, like the panel's own controls.
auto Palette(views::Widget* widget, const views::View* view) {
  return sidebar::design_palette(widget && widget->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark, WorkspaceColorForView(view));
}
std::string TimeLabel(base::Time time) {
  if (time.is_null()) return base::UTF16ToUTF8(T(u"Bez daty"));
  base::Time::Exploded local; time.LocalExplode(&local);
  return base::StringPrintf("%02d:%02d", local.hour, local.minute);
}
// The list's day groups: today, yesterday, then the date.
std::string DayLabel(base::Time time) {
  if (time.is_null()) return base::UTF16ToUTF8(T(u"Bez daty"));
  const auto today = base::Time::Now().LocalMidnight();
  if (time >= today) return base::UTF16ToUTF8(T(u"Dziś"));
  if (time >= (today - base::Hours(12)).LocalMidnight()) return base::UTF16ToUTF8(T(u"Wczoraj"));
  base::Time::Exploded local; time.LocalExplode(&local);
  return base::StringPrintf("%04d-%02d-%02d", local.year, local.month, local.day_of_month);
}
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
  ApplyColors(); scene_->RefreshSearch(address_); ScheduleUpdate(); scene_->Schedule();
}
void RssPanel::ApplyColors() {
  if (!widget_ || !viewport_) return;
  const auto p = Palette(widget_, content_);
  viewport_->SetViewportBackground(wide_ ? p.bg_base : p.bg_raised);
  reader_viewport_->SetViewportBackground(p.bg_raised);
  list_pane_->SetBackground(wide_ ? views::CreateRoundedRectBackground(p.bg_base, 12) : nullptr);
}
bool RssPanel::CanHandleAccelerators() const { return content_ && content_->IsDrawn(); }
bool RssPanel::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (!CanHandleAccelerators() || accelerator.key_code() != ui::VKEY_RETURN) return false;
  auto* focused=widget_->GetFocusManager()->GetFocusedView(); if (!focused || !content_->Contains(focused)) return false;
  ui::AXNodeData data; focused->GetViewAccessibility().GetAccessibleNodeData(&data);
  if (data.role != ax::mojom::Role::kButton) return false;
  ui::AXActionData action; action.action=ax::mojom::Action::kDoDefault;
  return focused->HandleAccessibleAction(action);
}
void RssPanel::Close() {
  if (widget_) widget_->GetFocusManager()->UnregisterAccelerators(this);
  parent_observation_.Reset(); ++discovery_generation_; update_pending_ = false; observation_.Reset(); subscription_ = {}; weak_.InvalidateWeakPtrs(); scene_.reset(); header_ = nullptr; title_ = nullptr; scroll_ = nullptr; tools_ = nullptr; settings_button_ = nullptr; back_button_ = nullptr; refresh_button_ = nullptr; body_ = nullptr; add_row_ = nullptr; add_button_ = nullptr; address_ = nullptr; viewport_ = nullptr; count_ = nullptr; panes_ = nullptr; list_pane_ = nullptr; reader_pane_ = nullptr; reader_body_ = nullptr; reader_viewport_ = nullptr;
  auto* content = content_.get(); content_ = nullptr;
  if (content && content->parent()) content->parent()->RemoveChildViewT(content); widget_ = nullptr; reader_ = nullptr; profile_ = nullptr;
}
views::View* RssPanel::Text(views::View* parent, std::string text, int style) {
  auto* view = parent->AddChildView(scene_->MakeText(U(text), true, false, 0));
  scene_->SetTextStyle(view, style, style == 6 || style == 7 || style == 8 ? PanelScene::Tone::kMuted : PanelScene::Tone::kInk); return view;
}
views::View* RssPanel::Action(views::View* parent, std::u16string text, base::RepeatingClosure callback, int kind) {
  auto* button = scene_->AddAction(parent, std::move(callback), std::move(text)); scene_->SetActionKind(button, kind); button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS); return button;
}
void RssPanel::Show(Profile* profile, views::View* parent, base::RepeatingCallback<void(GURL)> open) {
  CHECK(!content_);
  widget_ = parent->GetWidget();
  profile_ = profile; parent_observation_.Observe(widget_); observation_.Observe(profile);
  reader_ = RssReader::For(profile); open_ = std::move(open);
  auto weak = weak_.GetWeakPtr();
  auto content = std::make_unique<RssRoot>(base::BindRepeating(&RssPanel::ScheduleUpdate, weak)); scene_ = CreatePanelScene(content.get());
  auto* header = content->AddChildView(std::make_unique<views::View>());
  header_ = header;
  auto* title = Text(header, base::UTF16ToUTF8(T(u"Czytnik RSS")), 2);
  title_ = title;
  count_ = Text(header, "", 6);
  auto* tools = header->AddChildView(std::make_unique<views::View>());
  tools_ = tools;
  auto* add_actions = tools->AddChildView(scene_->MakeActions(0));
  add_button_ = Action(add_actions, T(u"Dodaj źródło"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->adding_ = !p->adding_; p->ScheduleUpdate(); } },weak));
  auto* back_actions = tools->AddChildView(scene_->MakeActions(0));
  back_button_ = Action(back_actions, T(u"Wróć do czytnika"), base::BindRepeating(&RssPanel::BackToReader, weak));
  settings_button_ = scene_->AddIconAction(tools, base::BindRepeating(&RssPanel::OpenSettings, weak, false), T(u"Ustawienia RSS"), &vector_icons::kSettingsIcon);
  settings_button_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  refresh_button_ = scene_->AddIconAction(tools,base::BindRepeating([](base::WeakPtr<RssPanel> p){if(p)p->reader_->Refresh();},weak),T(u"Odśwież źródła"),&vector_icons::kRefreshIcon);
  refresh_button_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetSidebarPanelLayout(tools,false,{},4);
  title->SetTooltipText(profile->IsOffTheRecord() ? T(u"Tryb prywatny · tylko w pamięci tego okna") : T(u"Źródła i odczyty są wspólne dla przestrzeni tego profilu."));
  SetSidebarPanelLayout(header, false, {}, 8, count_, true);
  auto* add = content->AddChildView(std::make_unique<views::View>()); add_row_ = add;
  address_ = scene_->AddSearch(add, T(u"Adres kanału RSS lub Atom"), T(u"Adres kanału RSS lub Atom"));
  address_->SetCallbacks(base::DoNothing(), base::BindRepeating([](base::WeakPtr<RssPanel> p, const ui::KeyEvent& e) {
    if (!p || e.type() != ui::EventType::kKeyPressed || e.key_code() != ui::VKEY_RETURN) return false;
    p->Subscribe(); return true;
  }, weak));
  auto* buttons = add->AddChildView(scene_->MakeActions(0));
  Action(buttons, T(u"Subskrybuj"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) p->Subscribe(); }, weak), 0);
  SetSidebarPanelLayout(add, false, {}, 8, address_->view(), true);
  auto* panes = content->AddChildView(std::make_unique<views::View>()); panes_ = panes;
  auto* list_pane = panes->AddChildView(std::make_unique<RssPane>()); list_pane_ = list_pane;
  PanelViewport* adapter = nullptr;
  auto viewport = CreatePanelViewport(&adapter, scene_->ViewportChangedCallback(), {100, 1200, false, true, false});
  viewport_ = adapter;
  auto* scroll = list_pane->AddChildView(std::move(viewport)); scroll_ = scroll;
  SetSidebarPanelLayout(list_pane, true, {}, 0, scroll);
  auto body = std::make_unique<RssBody>(); body_ = body.get(); SetSidebarPanelLayout(body_, true, {}, 8); viewport_->SetPanelContents(std::move(body));
  PanelViewport* reader_adapter = nullptr;
  auto reader_viewport = CreatePanelViewport(&reader_adapter, scene_->ViewportChangedCallback(), {100, 4000, false, true, false});
  reader_viewport_ = reader_adapter;
  reader_pane_ = panes->AddChildView(std::move(reader_viewport));
  auto reading = std::make_unique<RssReading>(); reader_body_ = reading.get(); reader_viewport_->SetPanelContents(std::move(reading));
  reader_pane_->SetVisible(false);
  SetSidebarPanelLayout(panes, false, {}, 16, list_pane);
  SetSidebarPanelLayout(content.get(), true, gfx::Insets(20), 16, panes);
  content_ = parent->AddChildView(std::move(content));
  content_->SetBoundsRect(parent->GetLocalBounds());
  widget_->GetFocusManager()->RegisterAccelerator(ui::Accelerator(ui::VKEY_RETURN,ui::EF_NONE),ui::AcceleratorManager::kNormalPriority,this);
  subscription_ = reader_->Observe(base::BindRepeating(&RssPanel::ScheduleUpdate, weak));
  OnWidgetThemeChanged(widget_); Update(); scene_->Schedule();

}
void RssPanel::RequestFocus() {
  if (!widget_ || !content_ || !content_->IsDrawn()) return;
  if (add_row_->GetVisible()) { address_->RequestFocus(); return; }
  auto* first = widget_->GetFocusManager()->GetNextFocusableView(content_, widget_, false, false);
  if (first && content_->Contains(first)) first->RequestFocus();
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
  for (const auto& f : reader_->feeds()) if (f.url == feed) for (const auto& a : f.articles) if (a.id == id) {
    return_focus_name_ = TF(u"Czytaj: {}", {U(a.title)}); focus_reader_pending_ = true;
    (wide_ ? reader_viewport_ : viewport_)->ScrollTo(0); reader_->Mark(feed, id, true, a.saved); break;
  }
  ScheduleUpdate();
}
void RssPanel::OpenSettings(bool add) {
  settings_ = true; adding_ = add; screen_focus_pending_ = true;
  viewport_->ScrollTo(0); ScheduleUpdate();
}
void RssPanel::BackToReader() {
  settings_ = false; adding_ = false; screen_focus_pending_ = true;
  viewport_->ScrollTo(0); ScheduleUpdate();
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
void RssPanel::RenderArticle(views::View* parent, const RssFeed& f, const RssArticle& a, bool wide) {
  auto weak = weak_.GetWeakPtr();
  // Beside the list it closes the preview and returns focus to the entry.
  auto* nav_row = parent->AddChildView(std::make_unique<views::View>());
  auto* nav = nav_row->AddChildView(scene_->MakeActions(6));
  Action(nav, T(u"Wróć do wpisów"), base::BindRepeating([](base::WeakPtr<RssPanel> p, bool wide) { if (p) { p->selected_.clear(); p->ScheduleUpdate(); if (!wide) p->viewport_->ScrollTo(0); } }, weak, wide));
  std::string meta = f.title + " · " + DateLabel(a.published);
  const auto words = base::SplitStringPiece(a.text, " \t\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY).size();
  if (words) meta += " · " + base::UTF16ToUTF8(TF(u"{} min czytania", {base::NumberToString16(std::max<size_t>(1, (words + 199) / 200))}));
  Text(parent, meta, 6);
  Text(parent, a.title, wide ? 2 : 3);
  auto* note = Text(parent, base::UTF16ToUTF8(T(u"Podgląd tekstowy · bez obrazów, skryptów i śledzenia")), 6);
  scene_->SetTextStyle(note, 6, PanelScene::Tone::kOk);
  auto* actions = parent->AddChildView(scene_->MakeActions(6));
  if (!wide) SetSidebarPanelLayout(actions, true, {}, 4);
  Action(actions, T(u"Otwórz artykuł"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p && p->open_) p->open_.Run(url); }, weak, a.url), 0);
  Action(actions, a.saved ? T(u"Usuń zapis") : T(u"Zapisz artykuł"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL feed, std::string id, bool saved) { if (p) p->reader_->Mark(feed, id, true, !saved); }, weak, f.url, a.id, a.saved));
  // Beside the list „Nieprzeczytany” also leaves the preview, so it sits with „Wróć”.
  auto* spacer = nav_row->AddChildView(std::make_unique<views::View>());
  auto* unread_group = wide ? nav_row->AddChildView(scene_->MakeActions(6)) : actions;
  Action(unread_group, T(u"Nieprzeczytany"), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL feed, std::string id, bool saved) { if (p) { p->selected_.clear(); p->reader_->Mark(feed, id, false, saved); } }, weak, f.url, a.id, a.saved));
  SetSidebarPanelLayout(nav_row, false, {}, 8, spacer, true);
  Text(parent, a.text.empty() ? base::UTF16ToUTF8(T(u"To źródło udostępnia tylko tytuł. Otwórz artykuł, aby przeczytać całość.")) : a.text);
}
void RssPanel::Update() {
  update_pending_ = false;
  if (!body_ || !widget_) return;
  const bool narrow = content_->width() < 420;
  const bool empty = reader_->feeds().empty();
  const bool was_wide = wide_;
  wide_ = !settings_ && !empty && content_->width() >= kRssWide;
  SetSidebarPanelLayout(content_, true, gfx::Insets(narrow ? 12 : 20), narrow ? 12 : 16, panes_);
  SetSidebarPanelLayout(add_row_, narrow, {}, 8, narrow ? nullptr : address_->view(), !narrow);
  static_cast<RssPane*>(list_pane_.get())->SetFixedWidth(wide_ ? kRssListWidth : 0);
  reader_pane_->SetVisible(wide_);
  SetSidebarPanelLayout(panes_, false, {}, 16, wide_ ? reader_pane_.get() : list_pane_.get());
  SetSidebarPanelLayout(body_, true, wide_ ? gfx::Insets::VH(8, 8) : gfx::Insets(), wide_ ? 2 : 8);
  if (was_wide != wide_) ApplyColors();
  scene_->SetText(title_, settings_ ? T(u"Ustawienia RSS") : T(u"Czytnik RSS"));
  size_t unread = 0;
  for (const auto& f : reader_->feeds()) for (const auto& a : f.articles) unread += !a.read;
  scene_->SetText(count_, unread ? TF(u"Nieprzeczytane: {}", {base::NumberToString16(unread)}) : std::u16string());
  count_->SetVisible(!settings_ && !narrow && unread);
  SetSidebarPanelLayout(header_, narrow, {}, 8, narrow ? nullptr : count_->GetVisible() ? count_.get() : title_.get(), !narrow);
  SetSidebarPanelLayout(tools_, narrow && settings_, {}, 4);
  settings_button_->SetVisible(!settings_); back_button_->SetVisible(settings_); refresh_button_->SetVisible(!settings_);
  const bool was_adding = add_row_->GetVisible(); add_row_->SetVisible(settings_ && (empty || adding_));
  add_button_->SetVisible(settings_ && !empty);
  scene_->SetActionText(add_button_, adding_ ? T(u"Gotowe") : T(u"Dodaj źródło"));
  if (screen_focus_pending_) {
    screen_focus_pending_ = false;
    if (settings_ && add_row_->GetVisible()) address_->RequestFocus();
    else (settings_ ? back_button_ : settings_button_)->RequestFocus();
  } else if (!was_adding && add_row_->GetVisible()) address_->RequestFocus();
  std::u16string focus_name;
  auto* focused = widget_->GetFocusManager()->GetFocusedView();
  if (focused && (body_->Contains(focused) || reader_body_->Contains(focused))) {
    ui::AXNodeData data; focused->GetViewAccessibility().GetAccessibleNodeData(&data);
    focus_name = data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  }
  // Activating an entry moves focus to its preview, as in one column.
  if (focus_reader_pending_ && !focus_name.empty()) focus_name = T(u"Wróć do wpisów");
  focus_reader_pending_ = false;
  base::ScopedClosureRunner restore_focus(base::BindOnce([](base::WeakPtr<RssPanel> p, std::u16string name) {
    if (!p || name.empty()) return;
    if (name == T(u"Wróć do wpisów") && p->selected_.empty()) name = p->return_focus_name_;
    if (name == T(u"Zapisz artykuł")) name = T(u"Usuń zapis"); else if (name == T(u"Usuń zapis")) name = T(u"Zapisz artykuł");
    auto find = [&](auto&& self, views::View* view) -> views::View* {
      ui::AXNodeData data; view->GetViewAccessibility().GetAccessibleNodeData(&data);
      if (view->GetFocusBehavior() == views::View::FocusBehavior::ALWAYS && view->IsDrawn() && data.GetString16Attribute(ax::mojom::StringAttribute::kName) == name) return view;
      for (const auto& child : view->children()) if (auto* match = self(self, child.get())) return match;
      return nullptr;
    };
    auto search = [&]() -> views::View* { if (auto* v = find(find, p->body_)) return v; return find(find, p->reader_body_); };
    if (auto* target = search()) target->RequestFocus();
    else if (!p->selected_.empty()) { name = T(u"Wróć do wpisów"); if (auto* back = search()) back->RequestFocus(); }
    else if (!p->return_focus_name_.empty()) { name = p->return_focus_name_; if (auto* entry = search()) entry->RequestFocus(); }
  }, weak_.GetWeakPtr(), focus_name));
  body_->RemoveAllChildViews(); reader_body_->RemoveAllChildViews(); auto weak = weak_.GetWeakPtr();
  if (!source_.is_empty() && std::none_of(reader_->feeds().begin(), reader_->feeds().end(), [&](const auto& f) { return f.url == source_; })) source_ = {};
  const RssFeed* selected_feed = nullptr; const RssArticle* selected_article = nullptr;
  if (!selected_.empty()) {
    for (const auto& f : reader_->feeds()) if (f.url == article_feed_) for (const auto& a : f.articles) if (a.id == selected_) { selected_feed = &f; selected_article = &a; }
    if (!selected_article) selected_.clear();
  }
  auto finish = [&] {
    static_cast<RssBody*>(body_.get())->Changed(); static_cast<RssReading*>(reader_body_.get())->Changed(); scene_->Schedule();
  };
  // Shared profile data, but all explicit navigation is bound to this panel's
  // original Profile by the window callback (even after a workspace switch).
  if (empty || settings_) Text(body_, base::UTF16ToUTF8(profile_->IsOffTheRecord() ? T(u"Tryb prywatny · tylko w pamięci tego okna") : T(u"Źródła i odczyty są wspólne dla przestrzeni tego profilu.")), 6);
  if (!reader_->ready()) Text(body_, base::UTF16ToUTF8(T(u"Wczytywanie źródeł…")), 6);
  if (reader_->busy()) Text(body_, base::UTF16ToUTF8(T(u"Odświeżanie źródeł…")), 6);
  if (!reader_->status().empty()) Text(body_, base::UTF16ToUTF8(T(U(reader_->status()))), 6);
  if (settings_) {
    for (const auto& feed : reader_->feeds()) {
      auto* row = body_->AddChildView(std::make_unique<views::View>());
      auto* details = row->AddChildView(std::make_unique<views::View>());
      Text(details, feed.title, 4); Text(details, feed.url.spec(), 8);
      SetSidebarPanelLayout(details, true, {}, 4);
      auto* actions = row->AddChildView(scene_->MakeActions(0));
      Action(actions, T(u"Usuń"), base::BindRepeating([](base::WeakPtr<RssPanel> panel, GURL url) {
        if (panel) panel->reader_->Remove(url);
      }, weak, feed.url), 4);
      SetSidebarPanelLayout(row, narrow, gfx::Insets::VH(8, 0), 8, narrow ? nullptr : details, !narrow);
      if (!feed.error.empty()) Text(body_, base::UTF16ToUTF8(T(u"Nie udało się odświeżyć źródła. Sprawdź adres lub spróbuj ponownie.")), 6);
    }
    if (empty) {
      Text(body_, base::UTF16ToUTF8(T(u"Twoje źródła, w jednym miejscu")), 3);
      Text(body_, base::UTF16ToUTF8(T(u"Wklej adres RSS lub Atom powyżej. Nowe wpisy pojawią się tutaj, bez reklam i automatycznie ładowanych obrazów.")));
    }
    finish(); return;
  }
  if (selected_article && !wide_) { RenderArticle(body_, *selected_feed, *selected_article, false); finish(); return; }
  if (wide_) {
    if (selected_article) RenderArticle(reader_body_, *selected_feed, *selected_article, true);
    else {
      Text(reader_body_, base::UTF16ToUTF8(T(u"Wybierz wpis")), 2);
      Text(reader_body_, base::UTF16ToUTF8(T(u"Podgląd otworzy się tutaj. Pełny artykuł otwierasz w zwykłej karcie.")), 6);
    }
  }
  if (!empty) {
  auto* filters = body_->AddChildView(scene_->MakeActions(4));
  if (narrow) SetSidebarPanelLayout(filters, true, {}, 4);
  Action(filters, T(u"Wszystkie"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->unread_ = false; p->saved_ = false; p->source_ = {}; p->ScheduleUpdate(); } }, weak), !unread_ && !saved_ && source_.is_empty() ? 7 : 6);
  Action(filters, T(u"Nieprzeczytane"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->unread_ = !p->unread_; p->ScheduleUpdate(); } }, weak), unread_ ? 7 : 6);
  Action(filters, T(u"Zapisane"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->saved_ = !p->saved_; p->ScheduleUpdate(); } }, weak), saved_ ? 7 : 6);
  }
  for (const auto& url : discovered_) {
    if (std::any_of(reader_->feeds().begin(), reader_->feeds().end(), [&](const auto& f) { return f.url == url; })) continue;
    auto* row = body_->AddChildView(scene_->MakeActions(4));
    Action(row, TF(u"Subskrybuj kanał: {}", {U(url.host())}), base::BindRepeating([](base::WeakPtr<RssPanel> p, GURL url) { if (p) { p->address_->SetText(base::UTF8ToUTF16(url.spec())); p->OpenSettings(true); } }, weak, url));
    Text(body_, url.spec(), 8);
  }
  if (empty) {
    Text(body_, base::UTF16ToUTF8(T(u"Twoje źródła, w jednym miejscu")), 3);
    Text(body_, base::UTF16ToUTF8(T(u"Dodaj źródło RSS lub Atom. Nowe wpisy pojawią się tutaj, bez reklam i automatycznie ładowanych obrazów.")));
    auto* actions = body_->AddChildView(scene_->MakeActions(0));
    Action(actions, T(u"Dodaj źródło"), base::BindRepeating(&RssPanel::OpenSettings, weak, true), 0);
    finish(); return;
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
  auto* source_tools = source_row->AddChildView(std::make_unique<views::View>());
  scene_->AddIconAction(source_tools,base::BindRepeating([](base::WeakPtr<RssPanel> p){if(p)p->reader_->MarkAll(p->source_);},weak),T(u"Oznacz jako przeczytane"),&vector_icons::kChecklistIcon)->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  SetSidebarPanelLayout(source_tools, false, {}, 8, nullptr, true);
  SetSidebarPanelLayout(source_row, false, {}, 8, picker, true);
  for (const auto& feed : reader_->feeds()) if (!feed.error.empty())
    Text(body_, feed.title + ": " + base::UTF16ToUTF8(T(u"Nie udało się odświeżyć źródła. Sprawdź adres lub spróbuj ponownie.")), 6);
  size_t count = 0;
  struct Item { raw_ptr<const RssFeed> feed; raw_ptr<const RssArticle> article; };
  std::vector<Item> items;
  for (const auto& f : reader_->feeds()) for (const auto& a : f.articles)
    if ((source_.is_empty() || f.url == source_) && (!unread_ || !a.read) && (!saved_ || a.saved)) items.push_back({&f, &a});
  std::stable_sort(items.begin(), items.end(), [](const auto& x, const auto& y) { return x.article->published > y.article->published; });
  const auto palette = Palette(widget_, content_);
  std::string day;
  for (const auto& item : items) {
    const auto& f = *item.feed; const auto& a = *item.article;
    if (count >= visible_limit_) break;
    if (const auto label = DayLabel(a.published); label != day) {
      day = label;
      auto* eyebrow = Text(body_, day, 7);
      eyebrow->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(count ? 12 : 6, 10, 2, 10));
    }
    auto* row = body_->AddChildView(std::make_unique<views::View>());
    if (wide_ && selected_article == &a) row->SetBackground(views::CreateRoundedRectBackground(palette.accent_soft, 8));
    // An unread entry carries an accent dot beside its source line.
    auto* marker = row->AddChildView(std::make_unique<views::View>());
    auto* dot = marker->AddChildView(std::make_unique<views::View>());
    dot->SetPreferredSize(gfx::Size(7, 7));
    if (!a.read) dot->SetBackground(views::CreateRoundedRectBackground(palette.accent, 4));
    SetSidebarPanelLayout(marker, true, gfx::Insets::TLBR(5, 0, 0, 0), 0);
    auto* column = row->AddChildView(std::make_unique<views::View>());
    Text(column, f.title + " · " + TimeLabel(a.published) + (a.saved ? " · " + base::UTF16ToUTF8(T(u"Zapisane")) : ""), 6);
    auto* title = column->AddChildView(std::make_unique<RssTitle>(TF(u"Czytaj: {}", {U(a.title)}),
        base::BindRepeating([](base::WeakPtr<RssPanel> panel, GURL feed, std::string id) {
          if (panel) panel->Select(feed, id);
        }, weak, f.url, a.id)));
    auto* label = Text(title, a.title, a.read ? 5 : 4);
    if (a.read) scene_->SetTextStyle(label, 5, PanelScene::Tone::kMuted);
    label->GetViewAccessibility().SetIsIgnored(true);
    SetSidebarPanelLayout(title, true, {}, 0);
    auto preview = base::CollapseWhitespace(base::UTF8ToUTF16(a.text),false); const size_t limit = wide_ ? 120 : 180;
    if (preview.size() > limit) { preview.resize(limit); if (preview.back() >= 0xd800 && preview.back() <= 0xdbff) preview.pop_back(); preview += u"…"; }
    if (!preview.empty()) {
      auto* excerpt=column->AddChildView(scene_->MakeText(std::move(preview),false,false,0));
      scene_->SetTextStyle(excerpt,6,PanelScene::Tone::kMuted);
    }
    SetSidebarPanelLayout(column, true, {}, 4);
    SetSidebarPanelLayout(row, false, gfx::Insets::VH(10, 10), 10, column, false); ++count;
  }
  if (items.size() > visible_limit_) {
    auto* more = body_->AddChildView(scene_->MakeActions(0));
    Action(more, T(u"Pokaż kolejne wpisy"), base::BindRepeating([](base::WeakPtr<RssPanel> p) { if (p) { p->visible_limit_ += 100; p->ScheduleUpdate(); } }, weak));
  }
  if (!count && !reader_->busy()) Text(body_, base::UTF16ToUTF8(T(u"Brak wpisów w tym widoku.")), 6);
  finish();
}
}
