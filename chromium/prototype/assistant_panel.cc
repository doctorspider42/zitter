// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/assistant_visuals.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
#include <algorithm>
#include "base/functional/callback_helpers.h"
#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/hash/hash.h"
#include "base/i18n/time_formatting.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/events/event.h"
#include "ui/display/screen.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/event_monitor.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {
constexpr std::array<const char16_t*, 4> kProviderLabels = {u"OpenAI API", u"Codex SDK", u"Claude CLI", u"Ollama"};
// Letter tiles for pages take a stable color from their host.
constexpr std::array<uint32_t, 6> kPageColors = {0xffff8a66, 0xff5cc8ff, 0xffffc35c, 0xffff7eb6, 0xff5fe0b8, 0xffb4a6ff};
std::u16string Fold(std::u16string_view text) { return base::i18n::ToLower(text); }
bool IsPage(const AssistantChip& chip) { return chip.kind == "tab" || chip.kind == "pin"; }
// A compact record of one tool call: its arguments and the gist of its result
// (errors, outcome fields, sizes), never screenshots or whole pages.
std::string TraceOf(const std::string& tool, const base::DictValue& args, const base::Value& value) {
  auto clip = [](std::string text, size_t size) { if (text.size() > size) { base::TruncateUTF8ToByteSize(text, size, &text); text += "…"; } return text; };
  base::DictValue result;
  if (const auto* dict = value.GetIfDict()) {
    for (const auto [key, item] : *dict) {
      if (key == "image") { result.Set("image", base::StringPrintf("%d×%d", dict->FindInt("width").value_or(0), dict->FindInt("height").value_or(0))); continue; }
      if (key == "elements" && item.is_list()) { result.Set("elements", static_cast<int>(item.GetList().size())); continue; }
      if (key == "pages" && item.is_list()) { result.Set("pages", static_cast<int>(item.GetList().size())); continue; }
      if (item.is_string()) result.Set(key, clip(item.GetString(), key == "text" ? 400 : 300));
      else if (item.is_bool() || item.is_int() || item.is_double()) result.Set(key, item.Clone());
      else result.Set(key, clip(base::WriteJson(item).value_or(""), 300));
    }
  } else if (value.is_list()) result.Set("items", static_cast<int>(value.GetList().size()));
  else result.Set("value", clip(base::WriteJson(value).value_or(""), 300));
  auto shown = args.Clone();
  if (const auto* text = shown.FindString("text")) shown.Set("text", clip(*text, 300));
  return base::WriteJson(base::DictValue().Set("tool", tool).Set("args", std::move(shown)).Set("result", std::move(result))).value_or("{}");
}
AssistantMark MarkFor(const AssistantChip& chip) {
  if (chip.kind == "research") return {AssistantGlyph::kSpark, {}, SK_ColorTRANSPARENT};
  if (chip.kind == "background") return {AssistantGlyph::kLayers, {}, SK_ColorTRANSPARENT};
  return {std::nullopt, chip.title.empty() ? u"?" : base::i18n::ToUpper(chip.title.substr(0, 1)), chip.color};
}
std::u16string ToolStatus(const std::string& tool) {
  if (tool == "read_page") return T(u"Czytam stronę…");
  if (tool == "list_tabs") return T(u"Sprawdzam otwarte karty…");
  if (tool == "type" || tool == "insert_text") return T(u"Wpisuję tekst…");
  if (tool == "press_key") return T(u"Naciskam klawisz…");
  if (tool == "visit") return T(u"Czytam stronę w tle…");
  if (tool == "click" || tool == "mouse") return T(u"Klikam na stronie…");
  if (tool == "select") return T(u"Wybieram opcję…");
  if (tool == "screenshot") return T(u"Patrzę na stronę…");
  if (tool == "wait") return T(u"Czekam na stronę…");
  if (tool == "dialog") return T(u"Odpowiadam na okno strony…");
  if (tool == "navigate" || tool == "open_tab") return T(u"Otwieram stronę…");
  if (tool == "adblock_inspect") return T(u"Szukam reklam na stronie…");
  if (tool == "adblock_add_rules") return T(u"Przygotowuję reguły blokowania…");
  return T(u"Pracuję w przeglądarce…");
}
// Runs after the current click has finished: the work may rebuild the clicked view.
base::RepeatingClosure Later(base::RepeatingClosure work) {
  return base::BindRepeating([](base::RepeatingClosure work) { base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, work); }, std::move(work));
}
bool PageTool(const std::string& tool) {
  return tool == "read_page" || tool == "click" || tool == "type" || tool == "scroll" || tool == "navigate" ||
         tool == "insert_text" || tool == "press_key" || tool == "select" || tool == "mouse" || tool == "screenshot" ||
         tool == "wait" || tool == "dialog" || tool == "adblock_inspect" || tool == "adblock_add_rules";
}
}
base::DictValue AssistantPermissions::ToValue() const {
  return base::DictValue().Set("tabs", tabs).Set("pages", pages).Set("actions", actions).Set("history", history).Set("web", web).Set("confirm_clicks", confirm_clicks);
}
std::u16string AssistantProviderLabel(std::string_view provider) {
  for (size_t i = 0; i < kAssistantProviders.size(); ++i) if (provider == kAssistantProviders[i]) return kProviderLabels[i];
  return base::UTF8ToUTF16(provider);
}
AssistantSettings::AssistantSettings()
    : models({{"claude", "sonnet"}, {"claude", "opus"}, {"openai", "gpt-5.4"}, {"openai", "gpt-5.4-mini"}, {"codex", "gpt-5.4"}, {"ollama", "qwen3:8b"}}),
      default_model("claude/sonnet") {}
AssistantSettings::AssistantSettings(const AssistantSettings&) = default;
AssistantSettings& AssistantSettings::operator=(const AssistantSettings&) = default;
AssistantSettings::~AssistantSettings() = default;
bool AssistantSettings::BackendEnabled(std::string_view provider) const {
  for (size_t i = 0; i < kAssistantProviders.size(); ++i) if (provider == kAssistantProviders[i]) return backends[i];
  return false;
}
std::vector<AssistantModel> AssistantSettings::PanelModels() const {
  std::vector<AssistantModel> result;
  for (const auto& model : models) if (model.visible && BackendEnabled(model.provider)) result.push_back(model);
  return result;
}
base::DictValue AssistantSettings::ToValue() const {
  base::ListValue list, enabled_backends;
  for (const auto& model : models) list.Append(base::DictValue().Set("provider", model.provider).Set("id", model.id).Set("visible", model.visible));
  for (bool backend : backends) enabled_backends.Append(backend);
  return base::DictValue().Set("enabled", enabled).Set("backends", std::move(enabled_backends)).Set("models", std::move(list))
      .Set("default_model", default_model).Set("permissions", defaults.ToValue()).Set("background", background)
      .Set("confirm_background", confirm_background).Set("concurrency", concurrency);
}
AssistantSettings AssistantSettings::FromValue(const base::DictValue* value) {
  AssistantSettings result;
  if (!value) return result;
  result.enabled = value->FindBool("enabled").value_or(true);
  if (const auto* list = value->FindList("backends"); list && list->size() == result.backends.size())
    for (size_t i = 0; i < list->size(); ++i) result.backends[i] = (*list)[i].GetIfBool().value_or(true);
  if (const auto* list = value->FindList("models")) {
    std::vector<AssistantModel> models;
    for (const auto& entry : *list) {
      if (!entry.is_dict()) continue;
      const auto* provider = entry.GetDict().FindString("provider"); const auto* id = entry.GetDict().FindString("id");
      if (!provider || !id || id->empty() || id->size() > 120 ||
          std::ranges::find(kAssistantProviders, *provider) == kAssistantProviders.end()) continue;
      models.push_back({*provider, *id, entry.GetDict().FindBool("visible").value_or(true)});
      if (models.size() >= 32) break;
    }
    if (!models.empty()) result.models = std::move(models);
  }
  if (const auto* model = value->FindString("default_model")) result.default_model = *model;
  if (const auto* permissions = value->FindDict("permissions")) {
    auto flag = [&](const char* key, bool fallback) { return permissions->FindBool(key).value_or(fallback); };
    result.defaults = {flag("tabs", false), flag("pages", false), flag("actions", false), flag("history", false), flag("web", true), flag("confirm_clicks", true)};
  }
  result.background = value->FindBool("background").value_or(true);
  result.confirm_background = value->FindBool("confirm_background").value_or(true);
  result.concurrency = std::clamp(value->FindInt("concurrency").value_or(12), 1, 12);
  return result;
}

class AssistantMenuPresses final : public ui::EventObserver {
 public:
  explicit AssistantMenuPresses(base::RepeatingClosure pressed) : pressed_(std::move(pressed)) {}
  ~AssistantMenuPresses() override = default;
  void OnEvent(const ui::Event& event) override { if (event.type() == ui::EventType::kMousePressed) pressed_.Run(); }
 private:
  base::RepeatingClosure pressed_;
};

AssistantPanel::Message::Message() = default;
AssistantPanel::Message::Message(const Message&) = default;
AssistantPanel::Message& AssistantPanel::Message::operator=(const Message&) = default;
AssistantPanel::Message::Message(Message&&) = default;
AssistantPanel::Message& AssistantPanel::Message::operator=(Message&&) = default;
AssistantPanel::Message::~Message() = default;
AssistantPanel::Session::Session() = default;
AssistantPanel::Session::~Session() = default;

AssistantPanel::AssistantPanel(WorkspaceWindow* window) : window_(window) {
  GetViewAccessibility().SetName(T(u"Asystent AI"));
  SetBackground(CreateAssistantBackground());
  SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
  SetNotifyEnterExitOnChild(true);
  scene_ = CreatePanelScene(this,true);
  auto weak=weak_factory_.GetWeakPtr();

  header_=AddChildView(std::make_unique<views::View>());
  auto* identity=header_->AddChildView(std::make_unique<views::View>());
  identity->AddChildView(CreateAssistantIdentity());
  subtitle_=identity->AddChildView(scene_->MakeText(T(u"Nowa sesja"),false,false,0));
  scene_->SetTextStyle(subtitle_,6,PanelScene::Tone::kMuted);
  SetSidebarPanelLayout(identity,true,gfx::Insets::TLBR(0,0,0,0),0);
  sessions_action_=header_->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kSessions)),AssistantGlyph::kHistory,T(u"Sesje")));
  access_action_=header_->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kAccess)),AssistantGlyph::kShield,T(u"Dostęp tej sesji")));
  header_->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::NewSession,weak)),AssistantGlyph::kPlus,T(u"Nowa sesja")));
  pin_action_=header_->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){self->SetPinned(!self->pinned());},base::Unretained(this)),AssistantGlyph::kPin,T(u"Przypnij")));
  header_->AddChildView(CreateAssistantIcon(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self){
    if(!self)return;self->Stop();self->CloseMenu();self->SetVisible(false);self->window_->shell()->LayoutAssistant(false);},weak)),AssistantGlyph::kClose,T(u"Zamknij")));
  SetSidebarPanelLayout(header_,false,gfx::Insets(),4,identity,true);

  background_=AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(background_,true,gfx::Insets(),6);
  background_->SetVisible(false);

  PanelViewport* transcript_viewport=nullptr;
  auto* viewport=AddChildView(CreatePanelViewport(&transcript_viewport,scene_->ViewportChangedCallback()));viewport_=transcript_viewport;
  auto content=std::make_unique<views::View>();conversation_=content.get();
  content->SetLayoutManager(CreateSidebarColumnLayout(8));viewport_->SetPanelContents(std::move(content));

  composer_=AddChildView(CreateAssistantSurface(1));
  SetSidebarPanelLayout(composer_,true,gfx::Insets::TLBR(10,12,8,10),8);
  chips_view_=composer_->AddChildView(CreateAssistantFlow(4));chips_view_->SetVisible(false);
  prompt_=composer_->AddChildView(CreateAssistantPromptArea(base::BindRepeating(&AssistantPanel::PromptKey,base::Unretained(this))));prompt_->SetPlaceholderText(T(u"Zapytaj Zittera… @ wstawia kartę albo narzędzie"));
  prompt_->GetViewAccessibility().SetName(T(u"Wiadomość do asystenta"));
  prompt_->set_controller(this);
  tools_=composer_->AddChildView(std::make_unique<views::View>());
  tools_->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){
    auto text=std::u16string(self->prompt_->GetText());
    if(!text.empty()&&!base::IsUnicodeWhitespace(text.back()))text+=u" ";
    self->mention_dismissed_.reset();self->prompt_->SetText(text+u"@");self->prompt_->SetSelectedRange(gfx::Range(text.size()+1));
    self->prompt_->RequestFocus();self->PromptChanged();
  },base::Unretained(this)),AssistantGlyph::kAt,T(u"Wstaw kartę lub narzędzie")));
  // The tools are toggles: they insert or remove the same chips as @.
  research_chip_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::ToggleTool,base::Unretained(this),std::string("research")),AssistantGlyph::kSpark,T(u"Deep research")));
  background_chip_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::ToggleTool,base::Unretained(this),std::string("background")),AssistantGlyph::kLayers,T(u"W tle · Alt+Enter")));
  auto* spacer=tools_->AddChildView(std::make_unique<views::View>());
  model_chip_=tools_->AddChildView(CreateAssistantChip(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kModel)),AssistantGlyph::kSpark,u""));
  send_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){self->Send(false);},base::Unretained(this)),AssistantGlyph::kArrow,T(u"Wyślij"),true));
  stop_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::Stop,base::Unretained(this)),AssistantGlyph::kStop,T(u"Stop"),true));
  SetSidebarPanelLayout(tools_,false,gfx::Insets(),4,spacer,true);
  SetSidebarPanelLayout(this,true,gfx::Insets(16),10,viewport);
  resize_handle_=AddChildView(CreateAssistantResizeGrip(this));
  resize_handle_->SetProperty(views::kViewIgnoredByLayoutKey,true);
  NewSession();SetVisible(false);
  window_->LoadAssistantSessions(base::BindOnce(&AssistantPanel::SessionsLoaded,weak));
}

AssistantPanel::~AssistantPanel() {
  if (save_timer_.IsRunning()) SaveSessions();
  menu_monitor_.reset();
  // Overlays left in the shell call back only through weak pointers.
  for (auto& session : sessions_) session->loader.reset();
  if (process_.IsValid()) process_.Terminate(0, false);
  // Scene nodes must be released before the scene registry.
  RemoveAllChildViews();
}
void AssistantPanel::Show(bool from_edge) {
  if (!window_->assistant_settings().enabled) return;
  hide_timer_.Stop();SetVisible(true); window_->shell()->LayoutAssistant(pinned_); prompt_->RequestFocus();
  hover_check_.Stop();
  if (from_edge) WatchPointer();
}
void AssistantPanel::WatchPointer() {
  // Once the pointer is outside the panel and its edge strip, an unpinned panel
  // hides; until then keep checking, wherever the pointer left from.
  if (pinned_ || hover_check_.IsRunning()) return;
  hover_check_.Start(FROM_HERE, base::Milliseconds(650), base::BindRepeating([](AssistantPanel* self) {
    if (!self->GetVisible() || self->pinned_) { self->hover_check_.Stop(); return; }
    if (self->HoverBounds().Contains(display::Screen::Get()->GetCursorScreenPoint())) return;
    self->hover_check_.Stop(); self->Collapse();
  }, base::Unretained(this)));
}
gfx::Rect AssistantPanel::HoverBounds() const {
  auto bounds = GetBoundsInScreen();
  bounds.Inset(gfx::Insets::TLBR(0, 0, 0, -24));
  return bounds;
}
bool AssistantPanel::busy() const { return selected().busy && !selected().background; }
gfx::Size AssistantPanel::CalculatePreferredSize(const views::SizeBounds&) const {return {preferred_width_,560};}
void AssistantPanel::OnMouseEntered(const ui::MouseEvent&) {hide_timer_.Stop();hover_check_.Stop();}
void AssistantPanel::OnMouseExited(const ui::MouseEvent& event) {
  if(event.IsLeftMouseButton())return;
  WatchPointer();
}
void AssistantPanel::Collapse() {
  // A running task keeps working while the panel is hidden.
  if(pinned_||resize_start_width_||menu_!=Menu::kNone)return;
  // The consent prompt sits beside the panel: reaching it must not slide the panel away.
  if(!sessions_.empty()&&(selected().confirmation||selected().asking)&&!selected().background)return;
  // Keep a keyboard-authored draft visible until the user finishes editing it.
  if((!prompt_->GetText().empty()||!chips_.empty())&&GetFocusManager()&&GetFocusManager()->GetFocusedView()&&prompt_->Contains(GetFocusManager()->GetFocusedView()))return;
  hide_timer_.Stop();SetVisible(false);window_->shell()->LayoutAssistant(false);
}
void AssistantPanel::OnResize(int amount,bool done) {
  hide_timer_.Stop();
  if(!resize_start_width_)resize_start_width_=width();
  preferred_width_=std::clamp(*resize_start_width_-(base::i18n::IsRTL()?-amount:amount),360,760);
  PreferredSizeChanged();window_->shell()->LayoutAssistant(pinned_);
  if(done)resize_start_width_.reset();
}
void AssistantPanel::SetPinned(bool pinned) {
  pinned_ = pinned; UpdateAssistantButton(pin_action_, pinned ? T(u"Odepnij") : T(u"Przypnij"),pinned);
  window_->shell()->LayoutAssistant(pinned_);
}
AssistantPanel::Session* AssistantPanel::Find(const std::string& key, uint64_t epoch) {
  for (auto& session : sessions_) if (session->key == key) return session->epoch == epoch ? session.get() : nullptr;
  return nullptr;
}
void AssistantPanel::OpenSettings() {
  CloseMenu();
  window_->shell()->OpenAssistantSettings();
}

// Sessions ---------------------------------------------------------------
bool AssistantPanel::Started(const Session& session) {
  return session.busy || std::ranges::any_of(session.messages, [](const Message& m) { return m.kind == 1; });
}
void AssistantPanel::NewSession() {
  CloseMenu();
  // An untouched conversation is reused instead of piling up empty ones.
  const auto profile = window_->AssistantProfileId();
  for (size_t i = sessions_.size(); i-- > 0;) {
    if (Started(*sessions_[i]) || sessions_[i]->profile != profile || window_->AssistantHasBackground(sessions_[i]->key)) continue;
    if (i != selected_) { if (busy()) Stop(); selected_ = i; }
    chips_.clear(); RenderChips(); RenderConversation(); UpdateControls(); UpdatePageControl();
    if (prompt_) prompt_->RequestFocus();
    return;
  }
  // A conversation in the foreground stops; background tasks keep running.
  if (!sessions_.empty() && busy()) Stop();
  if (sessions_.size() >= 200) {
    // Forget the least recently used idle conversation without a background page.
    auto idle = sessions_.end();
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
      if (!(*it)->busy && !window_->AssistantHasBackground((*it)->key) && (idle == sessions_.end() || (*it)->updated < (*idle)->updated)) idle = it;
    if (idle == sessions_.end()) { Append(selected(), "Session limit reached. Delete old conversations in the history."); return; }
    const size_t removed = idle - sessions_.begin();
    sessions_.erase(idle);
    if (selected_ >= removed && selected_ > 0) --selected_;
  }
  const auto& settings = window_->assistant_settings();
  auto session = std::make_unique<Session>();
  session->key = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session->id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session->profile = window_->AssistantProfileId();
  session->permissions = settings.defaults;
  session->confirm_background = settings.confirm_background;
  session->updated = base::Time::Now();
  const auto models = settings.PanelModels();
  for (const auto& model : models) if (model.provider + "/" + model.id == settings.default_model) { session->provider = model.provider; session->model = model.id; }
  if (session->provider.empty() && !models.empty()) { session->provider = models.front().provider; session->model = models.front().id; }
  sessions_.push_back(std::move(session));
  selected_ = sessions_.size() - 1;
  chips_.clear(); RenderChips();
  RenderConversation(); UpdateControls();
}
void AssistantPanel::SelectSession(size_t index) {
  CloseMenu();
  if (index >= sessions_.size() || index == selected_) return;
  if (busy()) Stop();
  // Leaving an untouched conversation drops it.
  const auto left = sessions_[selected_]->key;
  selected_ = index;
  if (auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s->key == left; });
      it != sessions_.end() && !Started(**it) && !window_->AssistantHasBackground(left)) {
    const size_t removed = it - sessions_.begin();
    sessions_.erase(it);
    if (selected_ > removed) --selected_;
  }
  chips_.clear(); RenderChips();
  RenderConversation(); UpdateControls(); UpdatePageControl();
  if (GetWidget()) GetWidget()->LayoutRootViewIfNecessary();
  viewport_->ScrollTo(viewport_->maximum());
}
void AssistantPanel::DeleteSession(std::string key) {
  auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s->key == key; });
  if (it == sessions_.end()) return;
  StopSession(**it);
  window_->AssistantDiscardBackground(key);
  const size_t index = it - sessions_.begin();
  const bool was_selected = index == selected_;
  sessions_.erase(it);
  if (selected_ > index) --selected_;
  if (sessions_.empty()) { selected_ = 0; const auto keep = menu_; NewSession(); menu_ = keep; }
  else if (was_selected) {
    // The newest conversation of this profile takes its place, or a new one.
    selected_ = std::min(selected_, sessions_.size() - 1);
    const auto profile = window_->AssistantProfileId();
    std::optional<size_t> newest;
    for (size_t i = 0; i < sessions_.size(); ++i)
      if (sessions_[i]->profile == profile && (!newest || sessions_[i]->updated > sessions_[*newest]->updated)) newest = i;
    if (newest) selected_ = *newest; else { const auto keep = menu_; NewSession(); menu_ = keep; }
  }
  chips_.clear(); RenderChips(); RenderConversation(); UpdateControls(); UpdatePageControl(); RenderBackground();
  ScheduleSave();
  if (menu_ != Menu::kNone) BuildMenu();
}
void AssistantPanel::ScheduleSave() {
  // Nothing is written before the saved conversations are back: that would lose them.
  if (!loaded_) return;
  save_timer_.Start(FROM_HERE, base::Seconds(1), base::BindOnce(&AssistantPanel::SaveSessions, base::Unretained(this)));
}
void AssistantPanel::SaveSessions() {
  if (!loaded_) return;
  save_timer_.Stop();
  base::ListValue list;
  for (const auto& s : sessions_) {
    if (!Started(*s)) continue;
    base::ListValue messages;
    for (const auto& m : s->messages) {
      base::ListValue chips;
      for (const auto& chip : m.chips)
        chips.Append(base::DictValue().Set("kind", chip.kind).Set("ref", chip.ref).Set("title", chip.title).Set("color", static_cast<double>(chip.color)));
      base::ListValue options;
      for (const auto& option : m.options) options.Append(option);
      messages.Append(base::DictValue().Set("kind", m.kind).Set("text", m.text).Set("chips", std::move(chips)).Set("state", m.state)
          .Set("options", std::move(options)).Set("answer", m.answer));
    }
    list.Append(base::DictValue().Set("key", s->key).Set("title", s->title).Set("provider", s->provider).Set("model", s->model)
        .Set("profile", s->profile).Set("permissions", s->permissions.ToValue()).Set("confirm_background", s->confirm_background)
        .Set("updated", s->updated.InMillisecondsFSinceUnixEpoch()).Set("messages", std::move(messages)));
  }
  window_->SaveAssistantSessions(base::DictValue().Set("version", 1).Set("sessions", std::move(list)));
}
void AssistantPanel::SessionsLoaded(std::optional<base::DictValue> saved) {
  loaded_ = true;
  std::vector<std::unique_ptr<Session>> restored;
  const auto* list = saved ? saved->FindList("sessions") : nullptr;
  const auto& d = window_->assistant_settings().defaults;
  if (list) for (const auto& item : *list) {
    const auto* entry = item.GetIfDict();
    if (!entry || restored.size() >= 200) continue;
    const auto* key = entry->FindString("key");
    if (!key || std::ranges::any_of(sessions_, [&](const auto& s) { return s->key == *key; })) continue;
    auto s = std::make_unique<Session>();
    s->key = *key;
    // The runtime forgot it; the next turn passes the earlier messages along.
    s->id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    auto text = [&](const char* name) { const auto* v = entry->FindString(name); return v ? *v : std::string(); };
    s->title = base::UTF8ToUTF16(text("title")); s->provider = text("provider"); s->model = text("model"); s->profile = text("profile");
    s->confirm_background = entry->FindBool("confirm_background").value_or(true);
    s->updated = base::Time::FromMillisecondsSinceUnixEpoch(entry->FindDouble("updated").value_or(0));
    if (const auto* p = entry->FindDict("permissions")) {
      // Saved access never exceeds today's defaults.
      s->permissions.tabs = p->FindBool("tabs").value_or(false) && d.tabs;
      s->permissions.pages = p->FindBool("pages").value_or(false) && d.pages;
      s->permissions.actions = p->FindBool("actions").value_or(false) && d.actions;
      s->permissions.history = p->FindBool("history").value_or(false) && d.history;
      s->permissions.web = p->FindBool("web").value_or(false) && d.web;
      s->permissions.confirm_clicks = p->FindBool("confirm_clicks").value_or(true) || d.confirm_clicks;
    }
    if (const auto* messages = entry->FindList("messages")) for (const auto& value : *messages) {
      const auto* m = value.GetIfDict();
      if (!m || !m->FindString("text")) continue;
      Message message; message.kind = std::clamp(m->FindInt("kind").value_or(3), 0, 5); message.text = *m->FindString("text");
      if (const auto* options = m->FindList("options")) for (const auto& option : *options) if (option.is_string()) message.options.push_back(option.GetString());
      if (const auto* answer = m->FindString("answer")) message.answer = *answer;
      // A question left open when Zitter closed can no longer be answered.
      if (message.kind == 5 && message.answer.empty()) message.answer = "—";
      message.state = m->FindInt("state").value_or(message.kind == 2 ? 1 : 0);
      if (const auto* chips = m->FindList("chips")) for (const auto& c : *chips) {
        const auto* chip = c.GetIfDict();
        if (!chip || !chip->FindString("kind")) continue;
        const auto* ref = chip->FindString("ref"); const auto* title = chip->FindString("title");
        message.chips.push_back({*chip->FindString("kind"), ref ? *ref : std::string(), base::UTF8ToUTF16(title ? *title : std::string()),
                                 static_cast<uint32_t>(chip->FindDouble("color").value_or(0))});
      }
      if (message.kind != 4 && message.kind != 5) s->transcript += std::string("\n\n") + (message.kind == 0 ? "Assistant: " : message.kind == 1 ? "You: " : "") + message.text;
      s->messages.push_back(std::move(message));
    }
    if (Started(*s)) restored.push_back(std::move(s));
  }
  // Saved conversations go before the ones opened since; the selection stays.
  if (!restored.empty()) {
    const size_t count = restored.size();
    sessions_.insert(sessions_.begin(), std::make_move_iterator(restored.begin()), std::make_move_iterator(restored.end()));
    selected_ += count;
  }
  if (std::ranges::any_of(sessions_, [](const auto& s) { return Started(*s); })) ScheduleSave();
  if (menu_ == Menu::kSessions) BuildMenu();
}
void AssistantPanel::PickModel(std::string provider, std::string model) {
  CloseMenu();
  auto& session = selected();
  if (session.provider == provider && session.model == model) return;
  // The model is fixed once a conversation has started: another model opens a new session.
  if (!session.messages.empty() || session.busy) NewSession();
  selected().provider = std::move(provider); selected().model = std::move(model);
  UpdateControls();
}
void AssistantPanel::Stop() { if (!sessions_.empty()) StopSession(selected()); }
void AssistantPanel::StopAll() { for (auto& session : sessions_) StopSession(*session); }
void AssistantPanel::StopSession(Session& session) {
  const bool was_busy = session.busy;
  ++session.epoch; session.loader.reset();
  // Disconnecting the RPC aborts its provider request. Pending native callbacks
  // carry the old epoch, so cancellation cannot dispatch another mutation.
  session.busy = false; session.queue.clear(); session.results.clear(); session.confirmation.reset();
  session.request.reset(); session.page_control = false; session.auto_confirm = false;
  session.thinking = false; session.tool_running = false; session.partial.clear();
  session.asking = false; session.picks.clear();
  if (was_busy) session.status = T(u"Zatrzymane");
  if (!send_) return;
  UpdateControls(); UpdatePageControl(); RenderBackground();
  if (&session == &selected()) RenderConversation();
}
void AssistantPanel::ResetRuntime() {
  if(process_.IsValid()){process_.Terminate(0,false);process_.Close();}
#if BUILDFLAG(IS_WIN)
  process_job_.Close();
#endif
  // Discard all backend contexts, including cached provider conversation IDs.
  for(auto& session:sessions_)session->id=base::Uuid::GenerateRandomV4().AsLowercaseString();
}
void AssistantPanel::RevokeAccess() {
  CloseMenu();
  StopAll();
  selected().permissions={false,false,false,false,false,true};
  ResetRuntime();UpdateControls();
  Append(selected(),base::UTF16ToUTF8(T(u"Dostęp odebrany. Zadania zatrzymane.")));
}
void AssistantPanel::SettingsChanged() {
  const auto& settings = window_->assistant_settings();
  if (!settings.enabled) {
    StopAll(); CloseMenu(); SetVisible(false); window_->shell()->LayoutAssistant(false);
    UpdatePageControl(); return;
  }
  // Narrowing applies at once, also to running sessions; wider access waits for a new session.
  bool narrowed = false;
  const auto& d = settings.defaults;
  for (auto& session : sessions_) {
    auto p = session->permissions;
    p.tabs &= d.tabs; p.pages &= d.pages; p.actions &= d.actions; p.history &= d.history; p.web &= d.web;
    p.confirm_clicks |= d.confirm_clicks;
    const bool confirm_background = session->confirm_background || settings.confirm_background;
    if (p != session->permissions || confirm_background != session->confirm_background || (!settings.background && session->background)) {
      session->permissions = p; session->confirm_background = confirm_background; narrowed = true;
    }
  }
  if (narrowed) { StopAll(); ResetRuntime(); }
  if (!settings.background || !selected().permissions.actions) std::erase_if(chips_, [](const auto& chip) { return chip.kind == "background"; });
  auto& session = selected();
  if (session.messages.empty() && !session.busy) {
    const auto models = settings.PanelModels();
    const bool available = std::ranges::any_of(models, [&](const auto& m) { return m.provider == session.provider && m.id == session.model; });
    if (!available) {
      session.provider.clear(); session.model.clear();
      for (const auto& model : models) if (model.provider + "/" + model.id == settings.default_model) { session.provider = model.provider; session.model = model.id; }
      if (session.provider.empty() && !models.empty()) { session.provider = models.front().provider; session.model = models.front().id; }
    }
  }
  RenderChips(); UpdateControls(); RenderBackground();
  if (menu_ != Menu::kNone) BuildMenu();
}

// Menus ------------------------------------------------------------------
void AssistantPanel::ToggleMenu(Menu menu) {
  if (menu_ == menu) { CloseMenu(); return; }
  menu_ = menu; model_query_.clear(); confirm_clear_ = false;
  BuildMenu();
  if ((menu == Menu::kModel || menu == Menu::kSessions) && model_search_) model_search_->RequestFocus();
}
void AssistantPanel::CloseMenu() {
  if (menu_ == Menu::kMention && mention_at_) mention_dismissed_ = mention_at_;
  menu_ = Menu::kNone; mention_rows_.clear(); menu_footer_ = nullptr; model_search_ = nullptr; menu_scroll_ = nullptr;
  menu_monitor_.reset();
  if (menu_view_) RemoveChildViewT(std::exchange(menu_view_, nullptr).get());
  UpdateAssistantButton(sessions_action_, T(u"Sesje"), false);
  UpdateControls();
  InvalidateLayout(); scene_->Schedule();
}
void AssistantPanel::BuildMenu() {
  if (menu_view_) RemoveChildViewT(std::exchange(menu_view_, nullptr).get());
  mention_rows_.clear(); menu_footer_ = nullptr; model_search_ = nullptr; menu_scroll_ = nullptr;
  if (menu_ == Menu::kNone) return;
  auto surface = CreateAssistantSurface(4);
  SetSidebarPanelLayout(surface.get(), true, gfx::Insets(6), 2);
  surface->GetViewAccessibility().SetRole(menu_ == Menu::kMention ? ax::mojom::Role::kListBox : ax::mojom::Role::kDialog);
  switch (menu_) {
    case Menu::kSessions: surface->GetViewAccessibility().SetName(T(u"Sesje")); BuildSessionsMenu(surface.get()); break;
    case Menu::kAccess: surface->GetViewAccessibility().SetName(T(u"Dostęp tej sesji")); BuildAccessMenu(surface.get()); break;
    case Menu::kModel: surface->GetViewAccessibility().SetName(T(u"Wybierz model")); BuildModelMenu(surface.get()); break;
    case Menu::kMention: surface->GetViewAccessibility().SetName(T(u"Wstaw kartę lub narzędzie")); BuildMentionMenu(surface.get()); break;
    case Menu::kNone: break;
  }
  // Its own layer keeps it above the conversation, which scrolls on a layer.
  surface->SetPaintToLayer();
  surface->layer()->SetFillsBoundsOpaquely(false);
  menu_view_ = AddChildView(std::move(surface));
  menu_view_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  if (!menu_monitor_ && GetWidget()) {
    menu_observer_ = std::make_unique<AssistantMenuPresses>(base::BindRepeating(&AssistantPanel::MenuPressed, base::Unretained(this)));
    menu_monitor_ = views::EventMonitor::CreateWindowMonitor(menu_observer_.get(), GetWidget()->GetNativeWindow(), {ui::EventType::kMousePressed});
  }
  ReorderChildView(resize_handle_, children().size() - 1);
  UpdateAssistantButton(sessions_action_, T(u"Sesje"), menu_ == Menu::kSessions);
  UpdateControls();
  InvalidateLayout(); scene_->Schedule();
}
views::View* AssistantPanel::ScrollList(views::View* body, int max_height) {
  auto scroll = std::make_unique<views::ScrollView>(views::ScrollView::ScrollWithLayers::kDisabled);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetDrawOverflowIndicator(false);
  scroll->ClipHeightTo(0, max_height);
  scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(views::ScrollBar::Orientation::kVertical));
  auto list = std::make_unique<views::View>();
  SetSidebarPanelLayout(list.get(), true, gfx::Insets(), 2);
  auto* raw = scroll->SetContents(std::move(list));
  menu_scroll_ = body->AddChildView(std::move(scroll));
  return raw;
}
void AssistantPanel::BuildSessionsMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto profile = window_->AssistantProfileId();
  // This profile's conversations with something written, newest first.
  std::vector<size_t> order;
  for (size_t i = 0; i < sessions_.size(); ++i) if (sessions_[i]->profile == profile && Started(*sessions_[i])) order.push_back(i);
  std::ranges::sort(order, [&](size_t a, size_t b) { return sessions_[a]->updated > sessions_[b]->updated; });
  body->AddChildView(CreateAssistantEyebrow(T(u"Historia rozmów"), base::NumberToString16(order.size())));
  model_search_ = scene_->AddSearch(body, T(u"Szukaj w rozmowach"), T(u"Szukaj w rozmowach"));
  model_search_->SetText(model_query_);
  model_search_->SetCallbacks(base::BindRepeating([](AssistantPanel* self) {
    const std::u16string query(self->model_search_->GetText());
    if (query == self->model_query_) return;
    self->model_query_ = query;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
      if (!self || self->menu_ != Menu::kSessions) return;
      self->BuildMenu();
      if (self->model_search_) { self->model_search_->RequestFocus(); self->model_search_->SelectRange(gfx::Range(self->model_query_.size())); }
    }, self->weak_factory_.GetWeakPtr()));
  }, base::Unretained(this)), base::BindRepeating([](AssistantPanel* self, const ui::KeyEvent& key) {
    if (key.type() != ui::EventType::kKeyPressed || key.key_code() != ui::VKEY_ESCAPE) return false;
    self->CloseMenu(); self->prompt_->RequestFocus(); return true;
  }, base::Unretained(this)));
  AssistantRow add; add.title = T(u"Nowa sesja"); add.mark = {AssistantGlyph::kPlus, {}, SK_ColorTRANSPARENT};
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::NewSession, weak)), std::move(add)));
  const auto query = Fold(model_query_);
  // The list takes the height the foot leaves; the foot stays at the bottom.
  auto* list = ScrollList(body, 10000);
  // A line to recognise the conversation by: where the query matched, or the last reply.
  auto excerpt = [](std::string_view text) {
    std::u16string line;
    for (const auto& part : base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      std::string clean(base::TrimString(part, "#*>-` ", base::TRIM_LEADING));
      base::RemoveChars(clean, "*`", &clean);
      if (!clean.empty()) { line = base::UTF8ToUTF16(clean); break; }
    }
    return line.size() > 90 ? line.substr(0, 89) + u"…" : line;
  };
  const auto now = base::Time::Now();
  const auto today = now.LocalMidnight();
  int group = -1;
  size_t shown = 0;
  for (const size_t i : order) {
    const auto& s = *sessions_[i];
    std::u16string detail;
    gfx::Range match;
    if (!query.empty()) {
      const auto at = Fold(s.title).find(query);
      if (at != std::u16string::npos) match = gfx::Range(at, at + query.size());
      for (const auto& m : s.messages) {
        if (m.kind > 1 || !match.is_empty()) continue;
        const auto text = base::UTF8ToUTF16(m.text);
        const auto found = Fold(text).find(query);
        if (found == std::u16string::npos) continue;
        const size_t from = found > 30 ? found - 30 : 0;
        detail = (from ? u"…" : u"") + excerpt(base::UTF16ToUTF8(text.substr(from)));
        break;
      }
      if (match.is_empty() && detail.empty()) continue;
    }
    if (detail.empty()) for (auto m = s.messages.rbegin(); m != s.messages.rend(); ++m) if (m->kind == 0) { detail = excerpt(m->text); break; }
    if (detail.empty()) detail = base::UTF8ToUTF16(s.model);
    // Today, yesterday, this week, earlier.
    const int age = s.updated >= today ? 0 : s.updated >= today - base::Days(1) ? 1 : s.updated >= today - base::Days(7) ? 2 : 3;
    if (age != group) {
      group = age;
      list->AddChildView(CreateAssistantEyebrow(age == 0 ? T(u"Dziś") : age == 1 ? T(u"Wczoraj") : age == 2 ? T(u"Ostatnie 7 dni") : T(u"Wcześniej")))->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(shown ? 6 : 0, 0, 0, 0));
    }
    AssistantRow row;
    row.title = s.title.empty() ? T(u"Nowa sesja") : s.title;
    row.match = match;
    row.detail = detail;
    row.checked = i == selected_;
    row.badge = s.busy ? (s.background ? T(u"w tle") : T(u"pracuje"))
              : age == 0 ? base::TimeFormatTimeOfDay(s.updated) : [&] {
                  base::Time::Exploded day; s.updated.LocalExplode(&day);
                  return base::UTF8ToUTF16(base::StringPrintf("%d.%02d", day.day_of_month, day.month)); }();
    row.mark = {s.background ? AssistantGlyph::kLayers : AssistantGlyph::kSpark, {}, SK_ColorTRANSPARENT};
    row.muted_mark = !s.background;
    row.remove = Later(base::BindRepeating(&AssistantPanel::DeleteSession, weak, s.key));
    row.remove_name = T(u"Usuń rozmowę");
    list->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::SelectSession, weak, i)), std::move(row)));
    ++shown;
  }
  if (!shown) list->AddChildView(CreateAssistantNote(order.empty() ? T(u"Tu pojawią się Twoje rozmowy.") : T(u"Żadna rozmowa nie pasuje.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::VH(8, 8));
  AssistantRow clear;
  clear.title = confirm_clear_ ? TF(u"Kliknij jeszcze raz, aby usunąć {}", {base::NumberToString16(order.size())}) : T(u"Usuń wszystkie rozmowy");
  clear.mark = {AssistantGlyph::kTrash, {}, SK_ColorTRANSPARENT}; clear.danger = true; clear.disabled = order.empty();
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
    if (!self) return;
    if (!self->confirm_clear_) { self->confirm_clear_ = true; self->BuildMenu(); return; }
    self->confirm_clear_ = false;
    // This profile's conversations; other profiles keep theirs.
    const auto profile = self->window_->AssistantProfileId();
    std::vector<std::string> keys;
    for (const auto& s : self->sessions_) if (s->profile == profile) keys.push_back(s->key);
    for (const auto& key : keys) self->DeleteSession(key);
    self->ResetRuntime(); self->RenderBackground(); }, weak)), std::move(clear)));
  body->AddChildView(CreateAssistantNote(T(u"Rozmowy są zapisane na tym komputerze. Usuń te, których nie potrzebujesz.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 8, 4, 8));
  SetSidebarPanelLayout(body, true, gfx::Insets(6), 2, menu_scroll_);
}
void AssistantPanel::BuildAccessMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto& p = selected().permissions;
  const std::array<std::pair<bool, std::u16string>, 5> grants = {{
      {p.tabs, T(u"Otwarte i przypięte karty")}, {p.pages, T(u"Treść i kontrolki stron")}, {p.actions, T(u"Klikanie, pisanie, karty")},
      {p.history, T(u"Historia przeglądania")}, {p.web, T(u"Publiczne strony i wyszukiwanie")}}};
  const int granted = static_cast<int>(std::ranges::count_if(grants, [](const auto& g) { return g.first; }));
  const bool started = !selected().messages.empty() || selected().busy;
  body->AddChildView(CreateAssistantEyebrow(T(u"Dostęp tej sesji"), TF(u"{} z 5", {base::NumberToString16(granted)})));
  for (size_t i = 0; i < grants.size(); ++i) {
    const auto& [on, label] = grants[i];
    AssistantRow row; row.title = label; row.toggle = on;
    // Off after the first message: widening waits for a new session.
    row.disabled = !on && started;
    row.detail = row.disabled ? T(u"Włączysz w nowej sesji") : on ? T(u"Włączone") : T(u"Wyłączone");
    body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::TogglePermission, weak, static_cast<int>(i))), std::move(row)));
  }
  AssistantRow clicks; clicks.title = T(u"Potwierdzaj kliknięcia"); clicks.toggle = p.confirm_clicks;
  clicks.disabled = p.confirm_clicks && started;
  clicks.detail = clicks.disabled ? T(u"Wyłączysz w nowej sesji") : T(u"Na widocznej karcie");
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::TogglePermission, weak, 5)), std::move(clicks)));
  if (window_->assistant_settings().background && p.actions) {
    AssistantRow background; background.title = T(u"Potwierdzaj kliknięcia w tle"); background.toggle = selected().confirm_background;
    background.disabled = selected().confirm_background && started;
    background.detail = background.disabled ? T(u"Wyłączysz w nowej sesji") : T(u"Zadanie czeka w panelu na zgodę");
    body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::TogglePermission, weak, 6)), std::move(background)));
  }
  body->AddChildView(CreateAssistantNote(started ? T(u"Ograniczenie działa od razu i zatrzymuje zadanie. Szerszy dostęp działa od nowej sesji.")
                                                 : T(u"Przed pierwszą wiadomością możesz dowolnie zmienić dostęp tej sesji.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 8, 6, 8));
  auto* actions = body->AddChildView(std::make_unique<views::View>());
  actions->AddChildView(CreateAssistantPill(Later(base::BindRepeating(&AssistantPanel::RevokeAccess, weak)), T(u"Odbierz cały dostęp"), 3, AssistantGlyph::kRevoke));
  auto* spacer = actions->AddChildView(std::make_unique<views::View>());
  actions->AddChildView(CreateAssistantLink(Later(base::BindRepeating(&AssistantPanel::OpenSettings, weak)), T(u"Ustawienia")));
  SetSidebarPanelLayout(actions, false, gfx::Insets::TLBR(2, 4, 2, 0), 8, spacer, true);
}
void AssistantPanel::TogglePermission(int index) {
  auto& session = selected();
  const bool started = !session.messages.empty() || session.busy;
  auto p = session.permissions; bool confirm_background = session.confirm_background;
  std::array<bool*, 5> grants = {&p.tabs, &p.pages, &p.actions, &p.history, &p.web};
  bool narrowing = false;
  if (index >= 0 && index < 5) { narrowing = *grants[index]; *grants[index] = !*grants[index]; }
  else if (index == 5) { narrowing = !p.confirm_clicks; p.confirm_clicks = !p.confirm_clicks; }
  else if (index == 6) { narrowing = !confirm_background; confirm_background = !confirm_background; }
  else return;
  if (started && !narrowing) return;
  session.permissions = p; session.confirm_background = confirm_background;
  if (!p.actions) std::erase_if(chips_, [](const auto& chip) { return chip.kind == "background"; });
  if (started) {
    // The backend context was built under the old access: stop and start it again.
    StopSession(session);
    session.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  RenderChips(); UpdateControls();
  if (menu_ == Menu::kAccess) BuildMenu();
}
void AssistantPanel::BuildModelMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto& settings = window_->assistant_settings();
  model_search_ = scene_->AddSearch(body, T(u"Szukaj modelu"), T(u"Szukaj modelu"));
  model_search_->SetText(model_query_);
  model_search_->SetCallbacks(base::BindRepeating([](AssistantPanel* self) {
    const std::u16string query(self->model_search_->GetText());
    if (query == self->model_query_) return;
    self->model_query_ = query;
    // Rebuild after typing; keep focus and caret in the search field.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
      if (!self || self->menu_ != Menu::kModel) return;
      self->BuildMenu();
      if (self->model_search_) { self->model_search_->RequestFocus(); self->model_search_->SelectRange(gfx::Range(self->model_query_.size())); }
    }, self->weak_factory_.GetWeakPtr()));
  }, base::Unretained(this)), base::BindRepeating([](AssistantPanel* self, const ui::KeyEvent& key) {
    if (key.type() != ui::EventType::kKeyPressed || key.key_code() != ui::VKEY_ESCAPE) return false;
    self->CloseMenu(); self->prompt_->RequestFocus(); return true;
  }, base::Unretained(this)));
  if (!selected().messages.empty() || selected().busy)
    body->AddChildView(CreateAssistantNote(T(u"Inny model zacznie nową rozmowę. Obecna zostanie w historii sesji."), AssistantGlyph::kLock, true))
        ->SetProperty(views::kMarginsKey, gfx::Insets::VH(4, 2));
  const auto query = Fold(model_query_);
  const auto models = settings.PanelModels();
  size_t shown = 0;
  auto* list = ScrollList(body, 280);
  // Backends follow the order of their models in Settings.
  std::vector<std::string> providers;
  for (const auto& model : models) if (std::ranges::find(providers, model.provider) == providers.end()) providers.push_back(model.provider);
  for (const auto& provider : providers) {
    bool heading = false;
    for (const auto& model : models) {
      if (model.provider != provider) continue;
      if (!query.empty() && Fold(base::UTF8ToUTF16(model.id)).find(query) == std::u16string::npos &&
          Fold(AssistantProviderLabel(provider)).find(query) == std::u16string::npos) continue;
      if (!heading) { list->AddChildView(CreateAssistantEyebrow(AssistantProviderLabel(provider))); heading = true; }
      AssistantRow row; row.title = base::UTF8ToUTF16(model.id); row.mono_title = true;
      row.checked = selected().provider == model.provider && selected().model == model.id;
      if (settings.default_model == model.provider + "/" + model.id) row.badge = T(u"domyślny");
      list->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::PickModel, weak, model.provider, model.id)), std::move(row)));
      ++shown;
    }
  }
  if (!shown) body->AddChildView(CreateAssistantNote(models.empty() ? T(u"Brak widocznych modeli. Dodaj je w Ustawieniach.") : T(u"Żaden model nie pasuje.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::VH(8, 8));
  body->AddChildView(CreateAssistantLink(Later(base::BindRepeating(&AssistantPanel::OpenSettings, weak)), T(u"Backendy i modele w Ustawieniach"), AssistantGlyph::kGear));
}
void AssistantPanel::BuildMentionMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto& settings = window_->assistant_settings();
  const auto& session = selected();
  const auto query = Fold(mention_query_);
  struct Item { AssistantChip chip; std::u16string detail, badge, note; bool mono = true, disabled = false; };
  std::vector<Item> tabs, pins, tools;
  auto matches = [&](const std::u16string& a, const std::u16string& b) { return query.empty() || Fold(a).find(query) != std::u16string::npos || Fold(b).find(query) != std::u16string::npos; };
  auto taken = [&](const AssistantChip& chip) { return std::ranges::any_of(chips_, [&](const auto& c) { return c.kind == chip.kind && c.ref == chip.ref; }); };
  const bool can_list = session.permissions.tabs && session.profile == window_->AssistantProfileId();
  if (can_list) {
    AssistantPermissions list; list.tabs = true;
    const auto context = window_->AssistantContext(list);
    const auto read_note = session.permissions.pages ? T(u"Asystent przeczyta treść i kontrolki tej karty.") : T(u"Bez uprawnienia do treści stron asystent zobaczy tylko tytuł i adres.");
    for (const auto* key : {"tabs", "pins"}) if (const auto* rows = context.FindList(key)) for (const auto& entry : *rows) {
      const auto& e = entry.GetDict();
      const auto title = base::UTF8ToUTF16(*e.FindString("title"));
      const GURL url(*e.FindString("url"));
      const auto host = base::UTF8ToUTF16(url.host());
      auto address = host + base::UTF8ToUTF16(url.path() == "/" ? "" : url.path());
      if (base::StartsWith(address, u"www.")) address = address.substr(4);
      const bool current = e.FindBool("current").value_or(false);
      AssistantChip chip{std::string(key) == "pins" ? "pin" : "tab", current ? "@current" : *e.FindString("mention"),
                         title.empty() ? host : title, kPageColors[base::PersistentHash(url.host()) % kPageColors.size()]};
      if (taken(chip) || !matches(chip.title, address)) continue;
      Item item{chip, address, current ? T(u"Bieżąca") : u"", chip.kind == "pin" ? T(u"Przypięta karta. Otwarcie uśpionej wymaga zgody na akcje.") : read_note};
      (chip.kind == "pin" ? pins : tabs).push_back(std::move(item));
    }
  }
  {
    AssistantChip research{"research", "", T(u"Deep research"), 0};
    if (!taken(research) && matches(research.title, u"research badanie raport"))
      tools.push_back({research, T(u"Równoległe źródła, raport z cytatami"), u"", T(u"Wiele wywołań modelu. Bez historii i akcji w przeglądarce."), false,
                       !session.permissions.web});
    AssistantChip background{"background", "", T(u"W tle"), 0};
    const bool allowed = settings.background && session.permissions.actions;
    if (!taken(background) && matches(background.title, u"background tle"))
      tools.push_back({background, allowed ? T(u"Niewidoczna karta tego profilu, te same logowania") : T(u"Wymaga klikania i pracy w tle w Ustawieniach"), u"",
                       T(u"Nie klika po Twoim widoku. Postęp i prośby o zgodę są w panelu."), false, !allowed});
  }
  // The current tab leads the list.
  std::ranges::stable_partition(tabs, [](const Item& item) { return !item.badge.empty(); });
  for (auto& item : tools) if (item.chip.kind == "research" && item.disabled) item.detail = T(u"Wymaga publicznych stron w Ustawieniach");
  const size_t count = tabs.size() + pins.size() + tools.size();
  body->AddChildView(CreateAssistantEyebrow(u"@" + mention_query_, count == 1 ? T(u"1 wynik") : count >= 2 && count <= 4 ? TF(u"{} wyniki", {base::NumberToString16(count)}) : TF(u"{} wyników", {base::NumberToString16(count)})));
  mention_items_.clear(); mention_notes_.clear(); mention_rows_.clear();
  auto* list = ScrollList(body, 300);
  auto section = [&](std::u16string label, std::vector<Item>& items, size_t limit) {
    if (items.empty()) return;
    list->AddChildView(CreateAssistantEyebrow(std::move(label)));
    for (size_t i = 0; i < items.size() && i < limit; ++i) {
      auto& item = items[i];
      AssistantRow row; row.mark = MarkFor(item.chip); row.title = item.chip.title; row.detail = item.detail;
      row.mono_detail = item.mono; row.badge = item.badge; row.disabled = item.disabled;
      if (!query.empty()) if (const auto at = Fold(item.chip.title).find(query); at != std::u16string::npos) row.match = gfx::Range(at, at + query.size());
      const size_t index = mention_items_.size();
      mention_rows_.push_back(list->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self, size_t index) {
        if (self && index < self->mention_items_.size()) self->InsertChip(self->mention_items_[index], true); }, weak, index)), std::move(row))));
      mention_items_.push_back(item.disabled ? AssistantChip{} : item.chip);
      mention_notes_.push_back(item.note);
    }
  };
  section(T(u"Otwarte karty"), tabs, 24);
  section(T(u"Przypięte"), pins, 24);
  section(T(u"Narzędzia"), tools, 2);
  if (!can_list)
    body->AddChildView(CreateAssistantNote(T(u"Karty pojawią się tu po włączeniu „Otwarte i przypięte karty” w Ustawieniach.")))
        ->SetProperty(views::kMarginsKey, gfx::Insets::VH(6, 8));
  if (!count) body->AddChildView(CreateAssistantNote(TF(u"Nic nie pasuje do „{}”.", {mention_query_})))->SetProperty(views::kMarginsKey, gfx::Insets::VH(8, 8));
  // Start on the first choice that can be inserted.
  mention_active_ = 0;
  while (mention_active_ < mention_items_.size() && mention_items_[mention_active_].kind.empty()) ++mention_active_;
  if (mention_active_ < mention_rows_.size()) SetAssistantMenuRowActive(mention_rows_[mention_active_], true);
  menu_footer_ = body->AddChildView(CreateAssistantFooter(mention_active_ < mention_notes_.size() ? mention_notes_[mention_active_] : T(u"Spróbuj innej nazwy albo adresu."), u"↑↓ Tab Esc"));
}

// Composer: @ suggestions and chips --------------------------------------
void AssistantPanel::MenuPressed() {
  if (!menu_view_) return;
  const auto point = display::Screen::Get()->GetCursorScreenPoint();
  auto inside = [&](const views::View* view) { return view && view->GetVisible() && view->GetBoundsInScreen().Contains(point); };
  // The menu itself, its own toggle (which closes it) and, for @, the field it belongs to.
  if (inside(menu_view_)) return;
  if (menu_ == Menu::kSessions && inside(sessions_action_)) return;
  if (menu_ == Menu::kAccess && inside(access_action_)) return;
  if (menu_ == Menu::kModel && inside(model_chip_)) return;
  if (menu_ == Menu::kMention && inside(composer_)) return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
    if (self) self->CloseMenu(); }, weak_factory_.GetWeakPtr()));
}
void AssistantPanel::ContentsChanged(views::Textfield*, const std::u16string&) {
  // The field grows with its lines.
  prompt_->InvalidateLayout(); composer_->InvalidateLayout();
  PromptChanged();
}
void AssistantPanel::PromptChanged() {
  const std::u16string text(prompt_->GetText());
  const size_t caret = std::min(text.size(), prompt_->GetSelectedRange().end());
  std::optional<size_t> at;
  for (size_t i = caret; i-- > 0;) {
    if (text[i] == u'@') { if (i == 0 || base::IsUnicodeWhitespace(text[i - 1])) at = i; break; }
    if (base::IsUnicodeWhitespace(text[i])) break;
  }
  if (mention_dismissed_ && (!at || *at != *mention_dismissed_)) mention_dismissed_.reset();
  if (!at || at == mention_dismissed_) {
    mention_at_.reset();
    if (menu_ == Menu::kMention) { menu_ = Menu::kNone; BuildMenu(); UpdateControls(); InvalidateLayout(); }
    UpdateControls();
    return;
  }
  mention_at_ = at; mention_query_ = text.substr(*at + 1, caret - *at - 1);
  menu_ = Menu::kMention; BuildMenu();
}
void AssistantPanel::MoveMention(int delta) {
  if (mention_items_.empty()) return;
  const size_t n = mention_items_.size();
  size_t next = mention_active_;
  for (size_t step = 0; step < n; ++step) {
    next = (next + n + delta) % n;
    if (!mention_items_[next].kind.empty()) break;
  }
  if (mention_active_ < mention_rows_.size()) SetAssistantMenuRowActive(mention_rows_[mention_active_], false);
  mention_active_ = next;
  SetAssistantMenuRowActive(mention_rows_[mention_active_], true);
  if (menu_footer_) SetAssistantFooterText(menu_footer_, mention_notes_[mention_active_]);
  mention_rows_[mention_active_]->NotifyAccessibilityEventDeprecated(ax::mojom::Event::kSelection, true);
  // Keep the highlighted row in view.
  mention_rows_[mention_active_]->ScrollViewToVisible();
}
bool AssistantPanel::PromptKey(const ui::KeyEvent& key) {
  if (key.type() != ui::EventType::kKeyPressed) return false;
  const auto code = key.key_code();
  if (menu_ == Menu::kMention) {
    if (code == ui::VKEY_DOWN || code == ui::VKEY_UP) { MoveMention(code == ui::VKEY_DOWN ? 1 : -1); return true; }
    if (code == ui::VKEY_RETURN || code == ui::VKEY_TAB) {
      if (mention_active_ < mention_items_.size() && !mention_items_[mention_active_].kind.empty()) InsertChip(mention_items_[mention_active_], true);
      return true;
    }
    if (code == ui::VKEY_ESCAPE) { CloseMenu(); return true; }
  }
  if (code == ui::VKEY_ESCAPE) {
    if (menu_ != Menu::kNone) { CloseMenu(); return true; }
    if (busy()) { Stop(); return true; }
    return false;
  }
  if (!sessions_.empty() && selected().asking && prompt_->GetText().empty() && code >= ui::VKEY_1 && code <= ui::VKEY_8 && !key.IsControlDown() && !key.IsAltDown()) {
    auto& session = selected();
    const size_t index = code - ui::VKEY_1;
    auto* question = PendingQuestion(session);
    if (question && index < question->options.size()) {
      if (question->state == 1) { session.picks[index] = !session.picks[index]; RenderConversation(); }
      else AnswerQuestion(session, question->options[index]);
      return true;
    }
  }
  if (code == ui::VKEY_RETURN && !key.IsShiftDown()) { Send(key.IsAltDown()); return true; }
  if (code == ui::VKEY_BACK && prompt_->GetText().empty() && !chips_.empty()) { RemoveChip(chips_.size() - 1); return true; }
  return false;
}
void AssistantPanel::InsertChip(AssistantChip chip, bool from_mention) {
  if (chip.kind.empty()) return;
  if (from_mention && mention_at_) {
    std::u16string text(prompt_->GetText());
    const size_t caret = std::min(text.size(), std::max(*mention_at_, prompt_->GetSelectedRange().end()));
    text.erase(*mention_at_, caret - *mention_at_);
    const size_t at = *mention_at_;
    mention_at_.reset(); mention_dismissed_.reset();
    prompt_->SetText(text); prompt_->SetSelectedRange(gfx::Range(at));
  }
  if (chip.kind == "background" && !(window_->assistant_settings().background && selected().permissions.actions)) return;
  if (std::ranges::none_of(chips_, [&](const auto& c) { return c.kind == chip.kind && c.ref == chip.ref; })) chips_.push_back(std::move(chip));
  if (menu_ == Menu::kMention) { menu_ = Menu::kNone; BuildMenu(); }
  RenderChips(); UpdateControls();
  prompt_->RequestFocus();
}
void AssistantPanel::RemoveChip(size_t index) {
  if (index >= chips_.size()) return;
  chips_.erase(chips_.begin() + index);
  RenderChips(); UpdateControls(); prompt_->RequestFocus();
}
void AssistantPanel::RenderChips() {
  chips_view_->RemoveAllChildViews();
  for (size_t i = 0; i < chips_.size(); ++i) {
    const auto& chip = chips_[i];
    chips_view_->AddChildView(CreateAssistantToken(MarkFor(chip), chip.title, !IsPage(chip), base::BindRepeating([](base::WeakPtr<AssistantPanel> self, size_t index) {
      // The × removes its own token: act after the click.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self, size_t index) {
        if (self) self->RemoveChip(index); }, self, index));
    }, weak_factory_.GetWeakPtr(), i)));
  }
  chips_view_->SetVisible(!chips_.empty());
  InvalidateLayout(); scene_->Schedule();
}
void AssistantPanel::ToggleTool(const std::string& kind) {
  // During a task the background button moves that task out of view.
  if (kind == "background" && busy()) { MoveToBackground(); return; }
  const auto found = std::ranges::find_if(chips_, [&](const auto& chip) { return chip.kind == kind; });
  if (found != chips_.end()) { RemoveChip(found - chips_.begin()); return; }
  InsertChip({kind, "", kind == "research" ? T(u"Deep research") : T(u"W tle"), 0}, false);
}

// Turns -------------------------------------------------------------------
void AssistantPanel::Ask(std::u16string text) {
  auto& session = selected();
  if (session.busy || session.asking) return;
  prompt_->SetText(text); prompt_->SetSelectedRange(gfx::Range(text.size())); PromptChanged();
  Send(false);
}
void AssistantPanel::SendForTesting(std::string text, bool background) {
  prompt_->SetText(base::UTF8ToUTF16(text)); Send(background);
}
void AssistantPanel::ConfigureForTesting(std::string provider, std::string model, AssistantPermissions permissions) {
  auto& session = selected();
  session.provider = std::move(provider); session.model = std::move(model); session.permissions = permissions;
  UpdateControls();
}
bool AssistantPanel::ContainsForTesting(std::string_view text) const {
  return std::ranges::any_of(sessions_, [&](const auto& s) { return s->transcript.find(text) != std::string::npos; });
}
AssistantPermissions AssistantPanel::permissions_for_testing() const { return selected().permissions; }
gfx::Rect AssistantPanel::menu_bounds_for_testing() const { return menu_view_ ? menu_view_->bounds() : gfx::Rect(); }
gfx::Rect AssistantPanel::composer_bounds_for_testing() const { return composer_->bounds(); }
void AssistantPanel::TypeForTesting(std::u16string text) {
  prompt_->SetText(text); prompt_->SetSelectedRange(gfx::Range(text.size())); PromptChanged();
}
bool AssistantPanel::KeyForTesting(ui::KeyboardCode code, int flags) {
  ui::KeyEvent key(ui::EventType::kKeyPressed, code, flags);
  return PromptKey(key);
}
size_t AssistantPanel::background_tasks_for_testing() const {
  return static_cast<size_t>(std::ranges::count_if(sessions_, [&](const auto& s) { return s->background && (s->busy || window_->AssistantHasBackground(s->key)); }));
}
bool AssistantPanel::waiting_for_consent_for_testing() const {
  return std::ranges::any_of(sessions_, [](const auto& s) { return s->confirmation.has_value(); });
}
void AssistantPanel::ConsentForTesting(bool allow) {
  for (auto& s : sessions_) if (s->confirmation) { Consent(*s, allow); return; }
}
void AssistantPanel::SeedBackgroundForTesting() {
  auto add = [&](std::u16string title, bool waiting) {
    auto session = std::make_unique<Session>();
    session->key = base::Uuid::GenerateRandomV4().AsLowercaseString();
    session->id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    session->profile = window_->AssistantProfileId();
    session->title = std::move(title);
    session->busy = session->background = true;
    session->status = T(u"Czytam stronę…");
    if (waiting) {
      session->confirmation = base::DictValue().Set("tool", "click").Set("args", base::DictValue().Set("tab", "@current").Set("element", "demo"));
      session->confirm_label = u"Przejdź do płatności";
    }
    sessions_.insert(sessions_.begin(), std::move(session));
    ++selected_;
  };
  add(u"Porównuję ceny laptopów", false);
  add(u"Zamawiam ładowarkę", true);
  RenderBackground();
}
void AssistantPanel::SeedExchangeForTesting(std::string prompt, std::string answer) {
  auto& session = selected();
  if (session.title.empty()) session.title = base::UTF8ToUTF16(prompt.substr(0, 48));
  Append(session, "You: " + prompt);
  Append(session, "Assistant: " + answer);
}
void AssistantPanel::TakeOverForTesting() {
  for (auto& s : sessions_) if (s->background) { TakeOver(s->key); return; }
}
void AssistantPanel::Append(Session& session, std::string text, std::vector<AssistantChip> chips) {
  auto& transcript = session.transcript;
  if (!text.starts_with("Trace: ")) {
    if (transcript.size() + text.size() > 250000) transcript.erase(0, std::min(transcript.size(), text.size() + 10000));
    transcript += "\n\n" + text;
  }
  Message message;
  if(text.starts_with("You: ")){message.kind=1;message.text=text.substr(5);message.chips=std::move(chips);}
  else if(text.starts_with("Assistant: ")){message.kind=0;message.text=text.substr(11);}
  else if(text.starts_with("Tool: ")){message.kind=2;message.state=1;message.text=base::UTF16ToUTF8(ToolStatus(text.substr(6)));}
  else if(text.starts_with("Issue: ")){message.kind=2;message.state=2;message.text=text.substr(7);}
  else if(text.starts_with("Trace: ")){message.kind=4;message.text=text.substr(7);}
  else {message.kind=3;message.text=text;}
  auto& messages=session.messages;messages.push_back(std::move(message));
  if(messages.size()>600)messages.erase(messages.begin(),messages.begin()+100);
  session.updated=base::Time::Now();
  ScheduleSave();
  if (&session != &selected() || session.messages.back().kind == 4) return;
  RenderConversation();
  if(GetWidget())GetWidget()->LayoutRootViewIfNecessary();
  viewport_->ScrollTo(viewport_->maximum());
}
void AssistantPanel::Send(bool background) {
  auto& session = selected();
  if (session.asking) {
    const auto text = std::u16string(base::TrimWhitespace(prompt_->GetText(), base::TRIM_ALL));
    if (text.empty()) return;
    prompt_->SetText(u"");
    AnswerQuestion(session, base::UTF16ToUTF8(text));
    return;
  }
  if (session.busy || window_->AssistantProfileId().empty()) return;
  const auto text = std::u16string(base::TrimWhitespace(prompt_->GetText(), base::TRIM_ALL));
  if (text.empty() && chips_.empty()) return;
  CloseMenu();
  if (session.profile != window_->AssistantProfileId()) { Append(session, "This session belongs to another profile. Start a new session."); return; }
  if (session.provider.empty()) { Append(session, base::UTF16ToUTF8(T(u"Dodaj model w Ustawieniach → Asystent AI."))); return; }
  const auto& settings = window_->assistant_settings();
  const bool research = std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "research"; });
  bool in_background = background || std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "background"; });
  if (in_background && !(settings.background && session.permissions.actions)) {
    Append(session, base::UTF16ToUTF8(T(u"Praca w tle wymaga klikania i włączonej pracy w tle w Ustawieniach.")));
    return;
  }
  // References travel as text the model already knows: @current, @tab:, @pin:.
  std::string prompt = base::UTF16ToUTF8(text);
  std::string references;
  for (const auto& chip : chips_) if (IsPage(chip)) references += (references.empty() ? "" : ", ") + chip.ref + " (" + base::UTF16ToUTF8(chip.title) + ")";
  if (!references.empty()) prompt += (prompt.empty() ? "" : "\n\n") + std::string("Referenced pages: ") + references;
  if (prompt.empty()) prompt = research ? "Research this." : "Continue.";
  if (in_background && !window_->AssistantHasBackground(session.key) && !window_->AssistantStartBackground(session.key, {}, GURL())) {
    Append(session, base::UTF16ToUTF8(T(u"Nie udało się otworzyć karty w tle.")));
    return;
  }
  in_background = in_background || (session.background && window_->AssistantHasBackground(session.key));
  auto chips = std::exchange(chips_, {});
  prompt_->SetText(u""); RenderChips();
  if (session.title.empty()) session.title = text.empty() ? chips.front().title : text.substr(0, 48);
  Append(session, "You: " + base::UTF16ToUTF8(text), chips);
  session.background = in_background;
  session.request = base::DictValue().Set("id", session.id).Set("prompt", prompt).Set("research", research).Set("background", in_background)
      .Set("concurrency", settings.concurrency)
      .Set("date", base::DictValue().Set("unix_ms", static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()))
           .Set("utc_offset_minutes", TimelineUtcOffsetMinutes(base::Time::Now())))
      .Set("config", base::DictValue().Set("provider", session.provider).Set("model", session.model))
      .Set("permissions", session.permissions.ToValue()).Set("context", window_->AssistantContext(session.permissions));
  base::ListValue history;
  for (size_t i = 0; i + 1 < session.messages.size(); ++i) {
    const auto& m = session.messages[i];
    if (m.kind == 0 || m.kind == 1) history.Append(base::DictValue().Set("role", m.kind == 1 ? "user" : "assistant").Set("text", m.text.substr(0, 8000)));
  }
  while (history.size() > 30) history.erase(history.begin());
  session.request->Set("history", std::move(history));
  StartTurn(session);
}
void AssistantPanel::StartTurn(Session& session) {
  ++session.epoch; session.busy = true; session.finished = false;
  session.status = session.background ? T(u"Pracuję w tle…") : T(u"Pracuję nad tym…");
  UpdateControls(); RenderBackground(); RenderConversation();
  if (!process_.IsValid()) {
    base::FilePath exe; base::PathService::Get(base::DIR_EXE, &exe);
    auto env = base::Environment::Create();
    auto node_path = env->GetVar("ZITTER_NODE_PATH");
    base::FilePath node = node_path ? base::FilePath::FromUTF8Unsafe(*node_path) : base::FilePath(FILE_PATH_LITERAL("node"));
    const auto script = exe.AppendASCII("assistant").AppendASCII("server.mjs");
    port_ = 49152 + static_cast<int>(base::RandGenerator(16384)); token_ = base::ToLowerASCII(base::HexEncode(base::RandBytesAsVector(24)));
    base::CommandLine command(node); command.AppendArgPath(script); command.AppendArg(base::NumberToString(port_)); command.AppendArg(token_);
    base::LaunchOptions options;
#if BUILDFLAG(IS_WIN)
    options.start_hidden = true;
    process_job_.Set(::CreateJobObject(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (process_job_.is_valid() && ::SetInformationJobObject(process_job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
      options.job_handle = process_job_.get();
#endif
    process_ = base::LaunchProcess(command, options);
    if (!process_.IsValid()) {
      StopSession(session);
      Append(session, "Could not start Node.js. Install Node 22+ or set ZITTER_NODE_PATH, then restart Zitter."); return;
    }
  }
  PollRuntime(session.key, session.epoch, 0);
}
void AssistantPanel::PollRuntime(std::string key, uint64_t epoch, int attempts) {
  auto* session = Find(key, epoch);
  if (!session || !session->busy) return;
  Request(*session, "/health", {}, base::BindOnce([](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch, int attempts, base::DictValue reply) {
    if (!self) return;
    auto* session = self->Find(key, epoch);
    if (!session || !session->busy) return;
    if (!reply.FindBool("ready").value_or(false)) {
      // A cold Node start (fresh files scanned by antivirus) can take a while.
      if (attempts >= 300) {
        self->StopSession(*session);
        if (self->process_.IsValid()) { self->process_.Terminate(0, false); self->process_.Close(); }
        self->Append(*session, "Assistant runtime did not start. Check Node 22+ and the assistant runtime beside the executable."); return;
      }
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
          base::BindOnce(&AssistantPanel::PollRuntime, self, key, epoch, attempts + 1), base::Milliseconds(100));
      return;
    }
    auto request = session->request ? session->request->Clone() : base::DictValue();
    self->Request(*session, "/turn", std::move(request), base::BindOnce(&AssistantPanel::OnTurn, self, key, epoch));
  }, weak_factory_.GetWeakPtr(), key, epoch, attempts));
}
void AssistantPanel::PollProgress() {
  if (sessions_.empty() || progress_loader_) return;
  auto& session = selected();
  if (!session.busy || !session.thinking || session.background || !port_) {
    if (!std::ranges::any_of(sessions_, [](const auto& s) { return s->thinking; })) progress_timer_.Stop();
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("http://127.0.0.1:" + base::NumberToString(port_) + "/progress");
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Authorization", "Bearer " + token_);
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_optional_assistant_progress", R"(
    semantics { sender: "Zitter AI assistant" description: "Reads the answer the local assistant runtime is writing."
    trigger: "An assistant turn is waiting for its model." data: "A session ID." destination: LOCAL }
    policy { cookies_allowed: NO setting: "Only while the optional assistant answers a message."
    policy_exception_justification: "Not implemented." })");
  progress_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  progress_loader_->AttachStringForUpload(base::WriteJson(base::DictValue().Set("id", session.id)).value_or("{}"), "application/json");
  progress_loader_->SetTimeoutDuration(base::Seconds(2));
  auto factory = window_->AssistantProfile()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  progress_loader_->DownloadToString(factory.get(), base::BindOnce([](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch,
      std::optional<std::string> response) {
    if (!self) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(self->progress_loader_));
    auto* session = self->Find(key, epoch);
    const auto value = response ? base::JSONReader::ReadDict(*response, base::JSON_PARSE_RFC) : std::nullopt;
    const auto* partial = value ? value->FindString("partial") : nullptr;
    if (!session || !session->thinking || !partial || *partial == session->partial) return;
    session->partial = *partial;
    if (session == &self->selected()) self->ShowPartial();
  }, weak_factory_.GetWeakPtr(), session.key, session.epoch), 1024 * 1024);
}
void AssistantPanel::ShowPartial() {
  // Only the live card changes; the rest of the conversation stays put.
  auto& session = selected();
  const bool at_end = viewport_->offset() >= viewport_->maximum() - 24;
  if (!live_answer_) { RenderConversation(); }
  else {
    live_answer_->RemoveAllChildViews();
    live_answer_->AddChildView(CreateAssistantMarkdown(base::UTF8ToUTF16(session.partial), base::BindRepeating([](const GURL&) {})));
    live_answer_->InvalidateLayout(); conversation_->InvalidateLayout(); InvalidateLayout(); scene_->Schedule();
  }
  if (GetWidget()) GetWidget()->LayoutRootViewIfNecessary();
  if (at_end) viewport_->ScrollTo(viewport_->maximum());
}
void AssistantPanel::Request(Session& session, std::string path, base::DictValue body, base::OnceCallback<void(base::DictValue)> done) {
  if (path == "/turn" || path == "/continue") {
    session.thinking = true; session.partial.clear();
    if (!progress_timer_.IsRunning())
      progress_timer_.Start(FROM_HERE, base::Milliseconds(250), base::BindRepeating(&AssistantPanel::PollProgress, base::Unretained(this)));
    if (&session == &selected()) RenderConversation();
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("http://127.0.0.1:" + base::NumberToString(port_) + path);
  request->method = path == "/health" ? "GET" : "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Authorization", "Bearer " + token_);
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_optional_assistant", R"(
    semantics { sender: "Zitter AI assistant" description: "User initiated optional local assistant RPC."
    trigger: "User sends an assistant message." data: "User message and explicitly allowed browser context."
    destination: LOCAL }
    policy { cookies_allowed: NO setting: "Only created when the user sends a message in the optional assistant."
    policy_exception_justification: "Not implemented." })");
  session.loader = network::SimpleURLLoader::Create(std::move(request), annotation);
  if (path != "/health") session.loader->AttachStringForUpload(base::WriteJson(body).value_or("{}"), "application/json");
  session.loader->SetTimeoutDuration(path == "/health" ? base::Seconds(1) : base::Minutes(11));
  session.loader->SetAllowHttpErrorResults(true);
  auto factory = window_->AssistantProfile()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  session.loader->DownloadToString(factory.get(), base::BindOnce([](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch,
      base::OnceCallback<void(base::DictValue)> done, std::optional<std::string> response) {
    if (!self) return;
    auto* session = self->Find(key, epoch);
    if (!session) return;
    // The loader runs this callback; release it only after returning.
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(session->loader));
    auto value = response ? base::JSONReader::ReadDict(*response, base::JSON_PARSE_RFC) : std::nullopt;
    std::move(done).Run(value ? std::move(*value) : base::DictValue().Set("error", "Assistant connection failed."));
  }, weak_factory_.GetWeakPtr(), session.key, session.epoch, std::move(done)), 2 * 1024 * 1024);
}
void AssistantPanel::OnTurn(std::string key, uint64_t epoch, base::DictValue reply) {
  auto* session = Find(key, epoch);
  if (!session) return;
  session->thinking = false; session->partial.clear();
  auto finish = [&](std::u16string status) {
    session->busy = false; session->finished = true; session->auto_confirm = false; session->page_control = false;
    session->status = std::move(status);
    UpdateControls(); UpdatePageControl(); RenderBackground();
    if (session == &selected()) {
      RenderConversation();
      // The field was disabled during the turn; give the keyboard back to it.
      if (auto* focus = GetFocusManager(); focus && (!focus->GetFocusedView() || Contains(focus->GetFocusedView()))) prompt_->RequestFocus();
    }
  };
  if (const auto* error = reply.FindString("error")) { Append(*session, *error); finish(T(u"Błąd")); return; }
  if (const auto* answer = reply.FindString("answer"); answer && !answer->empty()) Append(*session, "Assistant: " + *answer);
  auto* actions = reply.FindList("actions");
  if (!actions || actions->empty()) { finish(T(u"Gotowe")); return; }
  session->queue = actions->Clone(); session->results.clear(); session->index = 0;
  RunNext(*session);
}
void AssistantPanel::RunNext(Session& session) {
  if (!session.busy) return;
  if (session.profile != window_->AssistantProfileId()) { StopSession(session); Append(session, "Stopped: active profile changed."); return; }
  if (session.index >= session.queue.size()) {
    Request(session, "/continue", base::DictValue().Set("id", session.id).Set("results", std::move(session.results)),
            base::BindOnce(&AssistantPanel::OnTurn, weak_factory_.GetWeakPtr(), session.key, session.epoch));
    session.results = base::ListValue();
    return;
  }
  const auto& action = session.queue[session.index];
  if (!action.is_dict() || !action.GetDict().FindString("tool") || !action.GetDict().FindDict("args")) {
    StopSession(session); Append(session, "Invalid browser tool request."); return;
  }
  const auto tool = *action.GetDict().FindString("tool");
  if (tool == "ask_user") {
    // Shown in the conversation; the turn waits for a click or typed words.
    const auto& args = *action.GetDict().FindDict("args");
    const auto* question = args.FindString("question");
    Message message; message.kind = 5;
    message.text = question ? question->substr(0, 1000) : std::string();
    if (const auto* options = args.FindList("options"))
      for (const auto& option : *options) if (option.is_string() && !option.GetString().empty() && message.options.size() < 8) message.options.push_back(option.GetString().substr(0, 200));
    message.state = args.FindBool("multiple").value_or(false) && message.options.size() > 1 ? 1 : 0;
    if (message.text.empty()) {
      session.results.Append(base::DictValue().Set("tool", tool).Set("result", base::DictValue().Set("error", "A question is required")));
      ++session.index; RunNext(session); return;
    }
    session.picks.assign(message.options.size(), false);
    session.messages.push_back(std::move(message));
    session.updated = base::Time::Now(); ScheduleSave();
    session.asking = true; session.status = T(u"Czeka na Twoją odpowiedź");
    UpdateControls(); UpdatePageControl(); RenderBackground();
    if (&session == &selected()) {
      RenderConversation();
      if (GetWidget()) GetWidget()->LayoutRootViewIfNecessary();
      viewport_->ScrollTo(viewport_->maximum());
      prompt_->RequestFocus();
    }
    return;
  }
  Append(session, "Tool: " + tool);
  session.status = ToolStatus(tool);
  if (PageTool(tool)) {
    const auto* tab = action.GetDict().FindDict("args")->FindString("tab");
    session.target = tab ? *tab : "@current";
    if (!session.background) session.page_control = true;
  }
  session.tool_running = true;
  if (&session == &selected()) RenderConversation();
  UpdatePageControl(); RenderBackground();
  // Native consent: a whole-task approval, or a background session that clicks without asking.
  const bool confirmed = session.auto_confirm || (session.background && !session.confirm_background);
  window_->AssistantTool(tool, action.GetDict().FindDict("args")->Clone(), session.permissions,
      base::BindOnce(&AssistantPanel::ToolFinished, weak_factory_.GetWeakPtr(), session.key, session.epoch),
      confirmed, session.background ? session.key : std::string());
}
void AssistantPanel::ToolFinished(std::string key, uint64_t epoch, base::Value value) {
  auto* session = Find(key, epoch);
  if (!session || !session->busy) return;
  session->tool_running = false;
  // What the model asked for and what came back, saved with the conversation.
  if (session->index < session->queue.size()) {
    const auto& action = session->queue[session->index].GetDict();
    Append(*session, "Trace: " + TraceOf(*action.FindString("tool"), *action.FindDict("args"), value));
  }
  if (value.is_dict() && value.GetDict().FindBool("confirmation_required").value_or(false)) {
    session->confirmation = session->queue[session->index].GetDict().Clone();
    const auto* label = value.GetDict().FindString("label");
    session->confirm_label = base::UTF8ToUTF16(label ? label->substr(0, 120) : "Click");
    session->confirm_key = value.GetDict().FindBool("key").value_or(false);
    const auto* prompt = value.GetDict().FindString("prompt");
    session->confirm_prompt = prompt ? base::UTF8ToUTF16(prompt->substr(0, 300)) : std::u16string();
    session->status = T(u"Czeka na zgodę");
    if (session == &selected()) RenderConversation();
    if (session->background) { UpdatePageControl(); RenderBackground(); return; }
    UpdatePageControl();
    // A small prompt beside the element; without its bounds, above the capsule.
    const auto& args = *session->confirmation->FindDict("args");
    const auto* tab = args.FindString("tab"); const auto* element = args.FindString("element");
    window_->AssistantElementBounds(tab ? *tab : "@current", element ? *element : "", base::BindOnce(
        [](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch, std::optional<gfx::Rect> anchor) {
          if (!self) return;
          auto* session = self->Find(key, epoch);
          if (!session || !session->confirmation || session->background) return;
          auto act = [](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch, int choice) {
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self, std::string key, uint64_t epoch, int choice) {
              if (!self) return;
              auto* session = self->Find(key, epoch);
              if (!session) return;
              if (choice == 2) session->auto_confirm = true;
              self->Consent(*session, choice != 0);
            }, self, key, epoch, choice));
          };
          auto bubble = CreateAssistantConsentBubble(!session->confirm_prompt.empty() ? session->confirm_prompt : session->confirm_key ? TF(u"Nacisnąć {}?", {session->confirm_label}) : TF(u"Kliknąć „{}”?", {session->confirm_label}), session->status == T(u"Czeka na zgodę") && !session->title.empty() ? session->title : T(u"Asystent prosi o zgodę"),
              base::BindRepeating(act, self, key, epoch, 1), base::BindRepeating(act, self, key, epoch, 0), base::BindRepeating(act, self, key, epoch, 2));
          auto* allow = AssistantConsentAllowButton(bubble.get());
          self->window_->shell()->SetAssistantConsent(std::move(bubble), anchor);
          if (allow) allow->RequestFocus();
        }, weak_factory_.GetWeakPtr(), key, epoch));
    return;
  }
  // A failed browser action shows as a quiet status line, not only in the model's words.
  if (const auto* error = value.is_dict() ? value.GetDict().FindString("error") : nullptr)
    Append(*session, "Issue: " + base::UTF16ToUTF8(TF(u"Nie udało się ({}): {}", {base::UTF8ToUTF16(*session->queue[session->index].GetDict().FindString("tool")), base::UTF8ToUTF16(*error)})));
  session->results.Append(base::DictValue().Set("tool", *session->queue[session->index].GetDict().FindString("tool")).Set("result", std::move(value)));
  ++session->index; RunNext(*session);
}
AssistantPanel::Message* AssistantPanel::PendingQuestion(Session& session) {
  for (auto it = session.messages.rbegin(); it != session.messages.rend(); ++it)
    if (it->kind == 5) return it->answer.empty() ? &*it : nullptr;
  return nullptr;
}
void AssistantPanel::AnswerQuestion(Session& session, std::string answer) {
  auto* question = PendingQuestion(session);
  if (!session.asking || !question || answer.empty()) return;
  question->answer = answer;
  session.asking = false; session.picks.clear();
  Append(session, "Trace: " + base::WriteJson(base::DictValue().Set("tool", "ask_user").Set("question", question->text).Set("answer", answer)).value_or("{}"));
  session.results.Append(base::DictValue().Set("tool", "ask_user").Set("result", base::DictValue().Set("answer", answer)));
  ++session.index;
  RenderConversation(); UpdateControls();
  RunNext(session);
}
void AssistantPanel::RunActionsForTesting(base::ListValue actions) {
  auto& session = selected();
  session.busy = true; ++session.epoch;
  session.queue = std::move(actions); session.results.clear(); session.index = 0;
  RunNext(session);
}
void AssistantPanel::Consent(Session& session, bool allow) {
  if (!session.confirmation || !session.busy) return;
  if (session.profile != window_->AssistantProfileId()) { StopSession(session); return; }
  auto action = std::move(*session.confirmation); session.confirmation.reset();
  window_->shell()->SetAssistantConsent(nullptr, std::nullopt);
  session.status = allow ? T(u"Klikam na stronie…") : T(u"Pominięto kliknięcie");
  UpdatePageControl(); RenderBackground();
  if (!allow) {
    if (!pinned_) WatchPointer();
    // Declined rules leave nothing outlined on the page.
    if (*action.FindString("tool") == "adblock_add_rules")
      window_->AssistantTool("adblock_clear_preview", action.FindDict("args")->Clone(), session.permissions, base::DoNothing(),
                             false, session.background ? session.key : std::string());
    session.results.Append(base::DictValue().Set("tool", *action.FindString("tool")).Set("result",
        base::DictValue().Set("skipped", true).Set("detail", "The user declined this click. Do not retry it; continue another way or ask.")));
    ++session.index; RunNext(session); return;
  }
  session.tool_running = true;
  RenderConversation();
  if (!pinned_) WatchPointer();
  window_->AssistantTool(*action.FindString("tool"), action.FindDict("args")->Clone(), session.permissions,
      base::BindOnce(&AssistantPanel::ToolFinished, weak_factory_.GetWeakPtr(), session.key, session.epoch), true,
      session.background ? session.key : std::string());
}
bool AssistantPanel::HandleEscape() {
  if (sessions_.empty()) return false;
  auto& session = selected();
  if (session.confirmation && !session.background) { Consent(session, false); return true; }
  if (busy()) { Stop(); return true; }
  if (menu_ != Menu::kNone) { CloseMenu(); return true; }
  return false;
}

// Background --------------------------------------------------------------
void AssistantPanel::MoveToBackground() {
  auto& session = selected();
  if (!session.busy || session.background || !window_->assistant_settings().background || !session.permissions.actions) return;
  // Without page actions yet, the task gets a fresh invisible tab; otherwise
  // the tab it works in moves (or, as the space's only tab, is copied).
  const bool touched = !session.target.empty();
  if (!window_->AssistantStartBackground(session.key, touched ? session.target : std::string(), GURL())) {
    Append(session, base::UTF16ToUTF8(T(u"Nie udało się otworzyć karty w tle.")));
    return;
  }
  session.background = true; session.page_control = false;
  window_->shell()->SetAssistantConsent(nullptr, std::nullopt);
  Append(session, base::UTF16ToUTF8(touched ? T(u"Kończę w tle. Ta karta jest znowu Twoja.") : T(u"Kończę w tle, w osobnej niewidocznej karcie.")));
  UpdatePageControl(); RenderBackground(); UpdateControls();
}
void AssistantPanel::TakeOver(std::string key) {
  for (auto& s : sessions_) if (s->key == key) {
    if (s->busy) { StopSession(*s); Append(*s, base::UTF16ToUTF8(T(u"Przejęto kartę. Zadanie zatrzymane."))); }
    window_->AssistantTakeOver(key);
    s->background = false;
    RenderBackground(); UpdatePageControl();
    return;
  }
}
void AssistantPanel::Dismiss(std::string key) {
  for (auto& s : sessions_) if (s->key == key) { if (s->busy) StopSession(*s); s->background = false; }
  window_->AssistantDiscardBackground(key);
  RenderBackground();
}
void AssistantPanel::RenderBackground() {
  if (!background_) return;
  background_->RemoveAllChildViews();
  auto weak = weak_factory_.GetWeakPtr();
  auto later = [](base::WeakPtr<AssistantPanel> self, std::string key, int choice) {
    return base::BindRepeating([](base::WeakPtr<AssistantPanel> self, std::string key, int choice) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self, std::string key, int choice) {
        if (!self) return;
        if (choice == 3) { self->TakeOver(key); return; }
        if (choice == 4) { self->Dismiss(key); return; }
        for (auto& s : self->sessions_) if (s->key == key) {
          if (choice == 5) { self->StopSession(*s); return; }
          self->Consent(*s, choice == 1);
        }
      }, self, key, choice));
    }, self, std::move(key), choice);
  };
  size_t shown = 0, running = 0;
  for (const auto& s : sessions_) if (s->background && s->busy) ++running;
  for (const auto& s : sessions_) {
    if (!s->background || (!s->busy && !window_->AssistantHasBackground(s->key))) continue;
    if (shown++ == 0) background_->AddChildView(CreateAssistantEyebrow(T(u"W tle"), running == 1 ? T(u"1 zadanie") : TF(u"{} zadania", {base::NumberToString16(running)})));
    if (shown > 4) continue;
    const auto title = s->title.empty() ? T(u"Zadanie w tle") : s->title;
    const AssistantMark mark{AssistantGlyph::kLayers, {}, SK_ColorTRANSPARENT};
    if (s->confirmation) {
      auto* card = background_->AddChildView(CreateAssistantSurface(0));
      SetSidebarPanelLayout(card, true, gfx::Insets(8), 8);
      card->AddChildView(CreateAssistantTaskRow(mark, title, T(u"Czeka na zgodę"), 1, {}, {}));
      card->AddChildView(CreateAssistantElementPreview(s->confirm_label));
      card->AddChildView(CreateAssistantNote(!s->confirm_prompt.empty() ? s->confirm_prompt : s->confirm_key ? TF(u"Nacisnąć {}?", {s->confirm_label}) : TF(u"Kliknąć „{}”?", {s->confirm_label})));
      auto* actions = card->AddChildView(std::make_unique<views::View>());
      actions->AddChildView(CreateAssistantPill(later(weak, s->key, 1), T(u"Zezwól"), 0));
      actions->AddChildView(CreateAssistantPill(later(weak, s->key, 0), T(u"Pomiń"), 1));
      auto* spacer = actions->AddChildView(std::make_unique<views::View>());
      actions->AddChildView(CreateAssistantPill(later(weak, s->key, 3), T(u"Przejmij kartę"), 2, AssistantGlyph::kExpand));
      SetSidebarPanelLayout(actions, false, gfx::Insets(), 6, spacer, true);
      continue;
    }
    const bool page = window_->AssistantHasBackground(s->key);
    const int state = s->busy ? 0 : s->finished ? 2 : 3;
    const auto status = s->busy ? s->status : s->finished ? T(u"Gotowe · przejmij kartę, żeby zobaczyć") : T(u"Zatrzymane");
    background_->AddChildView(CreateAssistantTaskRow(mark, title, status, state,
        page ? later(weak, s->key, 3) : base::RepeatingClosure(), s->busy ? later(weak, s->key, 5) : later(weak, s->key, 4)));
  }
  background_->SetVisible(shown > 0);
  window_->shell()->SetAssistantAttention(std::ranges::any_of(sessions_, [](const auto& s) { return s->background && s->confirmation.has_value(); }));
  InvalidateLayout(); scene_->Schedule();
}

// Page control on the visible tab: spectral edge, capsule, consent bubble.
void AssistantPanel::UpdatePageControl() {
  auto* shell = window_->shell();
  const auto& session = selected();
  const bool active = session.busy && !session.background && session.page_control;
  shell->SetAssistantActivity(active);
  if (!active) { shell->SetAssistantCapsule(nullptr); shell->SetAssistantConsent(nullptr, std::nullopt); return; }
  const bool movable = window_->assistant_settings().background && session.permissions.actions;
  shell->SetAssistantCapsule(CreateAssistantCapsule(session.confirmation ? T(u"Czeka na Twoją zgodę") : T(u"Asystent działa"),
      movable ? base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
          if (self) self->MoveToBackground(); }, self));
      }, weak_factory_.GetWeakPtr()) : base::RepeatingClosure(), !gfx::Animation::PrefersReducedMotion()));
  if (!session.confirmation) shell->SetAssistantConsent(nullptr, std::nullopt);
}

// Rendering ---------------------------------------------------------------
void AssistantPanel::RenderConversation() {
  conversation_->RemoveAllChildViews();
  const auto& session=selected();
  const auto& messages=session.messages;
  if(messages.empty()) {
    conversation_->AddChildView(CreateAssistantWelcome());
    auto suggestion=[&](AssistantGlyph glyph,std::u16string title,std::u16string detail,std::u16string prompt,std::string tool){
      conversation_->AddChildView(CreateAssistantSuggestion(base::BindRepeating([](AssistantPanel* self,std::u16string prompt,std::string tool){
        self->prompt_->SetText(prompt);
        if(tool=="current"){
          // The page itself goes in as a chip when tabs may be listed.
          if(self->selected().permissions.tabs){
            AssistantPermissions list;list.tabs=true;
            const auto context=self->window_->AssistantContext(list);
            if(const auto* tabs=context.FindList("tabs"))for(const auto& entry:*tabs)if(entry.GetDict().FindBool("current").value_or(false)){
              const auto title=base::UTF8ToUTF16(*entry.GetDict().FindString("title"));const GURL url(*entry.GetDict().FindString("url"));
              self->InsertChip({"tab","@current",title.empty()?base::UTF8ToUTF16(url.host()):title,kPageColors[base::PersistentHash(url.host())%kPageColors.size()]},false);
            }
          } else self->prompt_->SetText(prompt+u" @current");
        } else if(!tool.empty())self->InsertChip({tool,"",T(u"Deep research"),0},false);
        self->UpdateControls();self->prompt_->RequestFocus();
      },base::Unretained(this),std::move(prompt),std::move(tool)),glyph,std::move(title),std::move(detail)));
    };
    suggestion(AssistantGlyph::kPage,T(u"Zrozum tę stronę"),T(u"Wstawia bieżącą kartę"),T(u"Podsumuj najważniejsze informacje z tej strony."),"current");
    suggestion(AssistantGlyph::kSearch,T(u"Znajdź coś w internecie"),T(u"Odpowiedzi z konkretnych źródeł"),T(u"Pomóż mi znaleźć informacje o "),"");
    suggestion(AssistantGlyph::kSpark,T(u"Zbadaj temat głębiej"),T(u"Wstawia narzędzie Deep research"),T(u"Zbadaj dokładnie i porównaj "),"research");
  } else for(size_t i=0;i<messages.size();++i) {
    const auto& message=messages[i];
    if(message.kind==4)continue;
    if(message.kind==5) {
      // The model's question: one click answers; several may be picked when allowed.
      auto* card=conversation_->AddChildView(CreateAssistantSurface(3));
      SetSidebarPanelLayout(card,true,gfx::Insets(12),6);
      const bool open=message.answer.empty()&&session.asking;
      card->AddChildView(CreateAssistantActivity(open?T(u"Asystent pyta"):T(u"Asystent zapytał"),open?3:1));
      card->AddChildView(CreateAssistantMarkdown(base::UTF8ToUTF16(message.text),base::BindRepeating([](const GURL&){})));
      const auto weak=weak_factory_.GetWeakPtr();
      for(size_t o=0;o<message.options.size();++o){
        AssistantRow row;row.title=base::UTF8ToUTF16(message.options[o]);
        row.mark={std::nullopt,base::NumberToString16(o+1),SK_ColorTRANSPARENT};
        if(message.state==1&&open)row.toggle=o<session.picks.size()&&session.picks[o];
        const bool chosen=!message.answer.empty()&&(message.answer==message.options[o]||
            (message.state==1&&std::ranges::contains(base::SplitString(message.answer,"\n",base::TRIM_WHITESPACE,base::SPLIT_WANT_NONEMPTY),message.options[o])));
        row.checked=chosen;row.disabled=!open&&!chosen;
        card->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self,std::string key,size_t o){
          if(!self)return;
          for(auto& s:self->sessions_)if(s->key==key&&s->asking){
            auto* question=self->PendingQuestion(*s);
            if(!question||o>=question->options.size())return;
            if(question->state==1){s->picks[o]=!s->picks[o];self->RenderConversation();}
            else self->AnswerQuestion(*s,question->options[o]);
          }},weak,session.key,o)),std::move(row)));
      }
      if(open&&message.state==1){
        card->AddChildView(CreateAssistantPill(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self,std::string key){
          if(!self)return;
          for(auto& s:self->sessions_)if(s->key==key&&s->asking){
            auto* question=self->PendingQuestion(*s);
            std::string answer;
            for(size_t i=0;question&&i<question->options.size();++i)if(s->picks[i])answer+=(answer.empty()?"":"\n")+question->options[i];
            if(!answer.empty())self->AnswerQuestion(*s,answer);
          }},weak,session.key)),T(u"Wyślij odpowiedź"),0));
      }
      if(open)card->AddChildView(CreateAssistantNote(message.options.empty()?T(u"Odpowiedz w polu poniżej."):T(u"Kliknij odpowiedź, naciśnij jej numer albo napisz własną poniżej.")));
      else if(!message.answer.empty()&&message.answer!="—"&&!std::ranges::contains(message.options,message.answer)&&message.state!=1)
        card->AddChildView(CreateAssistantPlainText(TF(u"Twoja odpowiedź: {}",{base::UTF8ToUTF16(message.answer)})));
      continue;
    }
    if(message.kind==2) {
      // The step in progress turns; finished steps get a check, failures a warning.
      size_t last=messages.size();
      while(last>0&&messages[last-1].kind==4)--last;
      const bool running=session.busy&&session.tool_running&&i+1==last&&message.state!=2;
      conversation_->AddChildView(CreateAssistantActivity(base::UTF8ToUTF16(message.text),running?0:message.state==2?2:1));
      continue;
    }
    auto* card=conversation_->AddChildView(CreateAssistantSurface(message.kind==1?2:3));
    SetSidebarPanelLayout(card,true,gfx::Insets(12),6);
    auto* head=card->AddChildView(std::make_unique<views::View>());
    auto* role=head->AddChildView(scene_->MakeText(message.kind==1?T(u"Ty"):message.kind==0?TF(u"Asystent · {}",{base::UTF8ToUTF16(session.model)}):T(u"Asystent"),false,false,0));
    scene_->SetTextStyle(role,6,PanelScene::Tone::kMuted);
    // Copies the message as written (Markdown for replies).
    if(!message.text.empty())head->AddChildView(CreateAssistantIcon(base::BindRepeating([](std::u16string text){
      ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(text);},base::UTF8ToUTF16(message.text)),AssistantGlyph::kCopy,T(u"Kopiuj wiadomość")));
    SetSidebarPanelLayout(head,false,gfx::Insets(),4,role,true);
    if(!message.chips.empty()){
      auto* flow=card->AddChildView(CreateAssistantFlow(4));
      for(const auto& chip:message.chips)flow->AddChildView(CreateAssistantToken(MarkFor(chip),chip.title,!IsPage(chip),{}));
    }
    if(message.kind==0&&!message.text.empty()){
      card->AddChildView(CreateAssistantMarkdown(base::UTF8ToUTF16(message.text),base::BindRepeating([](base::WeakPtr<AssistantPanel> self,const GURL& url){
        if(self&&url.SchemeIsHTTPOrHTTPS())self->window_->NavigateFromBar(url,true);},weak_factory_.GetWeakPtr())));
    } else if(!message.text.empty()){card->AddChildView(CreateAssistantPlainText(base::UTF8ToUTF16(message.text)));}
  }
  live_answer_=nullptr;
  if(session.busy&&!messages.empty()) {
    if(session.background){
      conversation_->AddChildView(CreateAssistantActivity(T(u"Pracuję w tle. Postęp i prośby o zgodę są wyżej, w sekcji W tle."),0));
    }else if(session.asking){
    }else if(session.confirmation){
      conversation_->AddChildView(CreateAssistantActivity(T(u"Czeka na Twoją zgodę"),3));
    }else if(session.thinking&&!session.partial.empty()){
      // The answer as the model writes it.
      auto* card=conversation_->AddChildView(CreateAssistantSurface(3));
      SetSidebarPanelLayout(card,true,gfx::Insets(12),6);
      card->AddChildView(CreateAssistantActivity(TF(u"Asystent · {} pisze…",{base::UTF8ToUTF16(session.model)}),0));
      live_answer_=card->AddChildView(std::make_unique<views::View>());
      live_answer_->SetLayoutManager(std::make_unique<views::FillLayout>());
      live_answer_->AddChildView(CreateAssistantMarkdown(base::UTF8ToUTF16(session.partial),base::BindRepeating([](const GURL&){})));
    }else if(session.thinking){
      conversation_->AddChildView(CreateAssistantActivity(T(u"Myślę…"),0));
    }else if(!session.tool_running){
      conversation_->AddChildView(CreateAssistantActivity(session.status,0));
    }
  }
  if(subtitle_)scene_->SetText(subtitle_,session.title.empty()?T(u"Nowa sesja"):session.title);
  InvalidateLayout();scene_->Schedule();
}
void AssistantPanel::UpdateControls() {
  if (sessions_.empty() || !send_) return;
  const auto& session = selected();
  const auto& settings = window_->assistant_settings();
  const bool idle = !session.busy;
  send_->SetVisible(idle); stop_->SetVisible(!idle); send_->SetEnabled(idle && !session.provider.empty());
  stop_->SetEnabled(!idle);
  prompt_->SetEnabled(idle || session.asking);
  if (session.asking) { send_->SetVisible(true); send_->SetEnabled(true); stop_->SetVisible(false); }
  prompt_->SetPlaceholderText(session.asking ? T(u"Wybierz odpowiedź albo napisz własną…") : T(u"Zapytaj Zittera… @ wstawia kartę albo narzędzie"));
  const bool research = std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "research"; });
  const bool background = std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "background"; });
  UpdateAssistantButton(research_chip_, T(u"Deep research"), research);
  const bool movable = session.busy && !session.background;
  UpdateAssistantButton(background_chip_, movable ? T(u"Przenieś zadanie do tła") : T(u"W tle · Alt+Enter"), background || session.background);
  research_chip_->SetEnabled(idle && session.permissions.web);
  background_chip_->SetVisible(settings.background && session.permissions.actions);
  background_chip_->SetEnabled(idle || movable);
  UpdateAssistantButton(model_chip_, session.model.empty() ? T(u"Wybierz model") : base::UTF8ToUTF16(session.model), menu_ == Menu::kModel);
  model_chip_->SetTooltipText(T(u"Zmień model"));
  model_chip_->GetViewAccessibility().SetName(TF(u"Model: {}. Zmień model", {base::UTF8ToUTF16(session.model)}));
  const int shared = static_cast<int>(session.permissions.tabs) + session.permissions.pages + session.permissions.actions + session.permissions.history;
  UpdateAssistantButton(access_action_, shared ? TF(u"Dostęp tej sesji · {}", {base::NumberToString16(shared)}) : T(u"Dostęp tej sesji"), menu_ == Menu::kAccess || shared > 0);
  InvalidateLayout();
}

void AssistantPanel::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  resize_handle_->SetBounds(0,16,8,std::max(0,height()-32));
  if (!menu_view_) return;
  const int inner = std::max(0, width() - 32);
  int w = inner, x = 16, top = 0, bottom = 0;
  if (menu_ == Menu::kSessions) {
    menu_view_->SetBounds(16, header_->bounds().bottom() + 6, inner, std::max(0, height() - header_->bounds().bottom() - 22));
    return;
  }
  if (menu_ == Menu::kAccess) { w = std::min(320, inner); x = width() - 16 - w; top = header_->bounds().bottom() + 6; }
  else if (menu_ == Menu::kModel) { w = std::min(300, inner); x = width() - 16 - w; bottom = composer_->y() - 8; }
  else { w = composer_->width(); x = composer_->x(); bottom = composer_->y() - 8; }
  const int limit = bottom ? std::max(0, bottom - header_->bounds().bottom() - 6) : std::max(0, height() - top - 16);
  const int h = std::min(limit, menu_view_->GetHeightForWidth(w));
  menu_view_->SetBounds(x, bottom ? bottom - h : top, w, h);
}
}
