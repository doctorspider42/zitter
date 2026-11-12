// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/assistant_visuals.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
#include <algorithm>
#include "base/functional/callback_helpers.h"
#include "chromium/prototype/assistant_host.h"
#include "chromium/prototype/workspace_shell.h"
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
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/weak_document_ptr.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/models/dialog_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/events/event.h"
#include "ui/display/screen.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/window/dialog_delegate.h"
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
struct ShortcutAccess {
  bool AssistantPermissions::*permission;
  const char16_t* label;
};
// Positions are persisted in the shortcut permission mask.
constexpr auto kShortcutAccess = std::to_array<ShortcutAccess>({
    {&AssistantPermissions::pages, u"Odczyt stron"},
    {&AssistantPermissions::web, u"Wyszukiwanie w sieci"},
    {&AssistantPermissions::actions, u"Działania w przeglądarce"},
    {&AssistantPermissions::tabs, u"Odczyt otwartych kart"},
    {&AssistantPermissions::history, u"Odczyt historii przeglądania"},
});
static_assert(kAssistantShortcutAccessMask == (1 << kShortcutAccess.size()) - 1);
// Letter tiles for pages take a stable color from their host.
constexpr std::array<uint32_t, 6> kPageColors = {0xffff8a66, 0xff5cc8ff, 0xffffc35c, 0xffff7eb6, 0xff5fe0b8, 0xffb4a6ff};
std::u16string Fold(std::u16string_view text) { return base::i18n::ToLower(text); }
bool IsPage(const AssistantChip& chip) { return chip.kind == "tab" || chip.kind == "pin"; }
// "18:00" → minutes after midnight.
std::optional<int> ParseClock(std::string_view text) {
  const auto parts = base::SplitStringPiece(text, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  int h = 0, m = 0;
  if (parts.size() != 2 || !base::StringToInt(parts[0], &h) || !base::StringToInt(parts[1], &m) || h < 0 || h > 23 || m < 0 || m > 59) return std::nullopt;
  return h * 60 + m;
}
// "dziś 8:00", "jutro 8:00", "wczoraj 18:02", "pt 7.11 8:00".
std::u16string WhenText(base::Time time) {
  if (time.is_null()) return u"—";
  base::Time::Exploded e; time.LocalExplode(&e);
  const auto clock = base::UTF8ToUTF16(base::StringPrintf("%d:%02d", e.hour, e.minute));
  const auto day = time.LocalMidnight(), today = base::Time::Now().LocalMidnight();
  if (day == today) return TF(u"dziś {}", {clock});
  if (day == (today + base::Hours(36)).LocalMidnight()) return TF(u"jutro {}", {clock});
  if (day == (today - base::Hours(12)).LocalMidnight()) return TF(u"wczoraj {}", {clock});
  static constexpr std::array<const char16_t*, 7> kDays = {u"nd", u"pn", u"wt", u"śr", u"cz", u"pt", u"so"};
  return T(kDays.at(e.day_of_week)) + base::UTF8ToUTF16(base::StringPrintf(" %d.%02d ", e.day_of_month, e.month)) + clock;
}
// A compact record of one tool call: its arguments and the gist of its result
// (errors, outcome fields, sizes), never screenshots or whole pages.
std::string TraceOf(const std::string& tool, const base::DictValue& args, const base::Value& value) {
  auto clip = [](std::string text, size_t size) { if (text.size() > size) { base::TruncateUTF8ToByteSize(text, size, &text); text += "…"; } return text; };
  base::DictValue result;
  if (const auto* dict = value.GetIfDict()) {
    for (const auto [key, item] : *dict) {
      if (key == "debug_document" || key == "page_confirmation") continue;
      if (tool == "adblock_diagnose" && item.is_list()) {
        result.Set(key, static_cast<int>(item.GetList().size())); continue;
      }
      if (IsAssistantDebugTool(tool) && (key == "entries" || key == "matches") && item.is_list()) {
        result.Set(key, static_cast<int>(item.GetList().size())); continue;
      }
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
  shown.Remove("_debug_document");
  shown.Remove("_page_confirmation");
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
  if (tool == "debug_console") return T(u"Czytam konsolę karty…");
  if (tool == "debug_dom") return T(u"Sprawdzam DOM karty…");
  if (tool == "debug_edit_dom") return T(u"Zmieniam DOM karty…");
  if (tool == "debug_hard_reload") return T(u"Przeładowuję kartę bez pamięci podręcznej…");
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
  if (tool == "adblock_diagnose") return T(u"Sprawdzam, jak strona wykrywa blokowanie…");
  return T(u"Pracuję w przeglądarce…");
}
// Runs after the current click has finished: the work may rebuild the clicked view.
base::RepeatingClosure Later(base::RepeatingClosure work) {
  return base::BindRepeating([](base::RepeatingClosure work) { base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, work); }, std::move(work));
}
bool PageTool(const std::string& tool) {
  return IsAssistantDebugTool(tool) || tool == "read_page" || tool == "click" || tool == "type" || tool == "scroll" || tool == "navigate" ||
         tool == "insert_text" || tool == "press_key" || tool == "select" || tool == "mouse" || tool == "screenshot" ||
         tool == "wait" || tool == "dialog" || tool == "adblock_inspect" || tool == "adblock_add_rules" || tool == "adblock_diagnose";
}
}
AssistantTask::AssistantTask() = default;
AssistantTask::AssistantTask(const AssistantTask&) = default;
AssistantTask& AssistantTask::operator=(const AssistantTask&) = default;
AssistantTask::AssistantTask(AssistantTask&&) = default;
AssistantTask& AssistantTask::operator=(AssistantTask&&) = default;
AssistantTask::~AssistantTask() = default;
base::DictValue AssistantTask::ToValue() const {
  auto time = [](base::Time t) { return t.is_null() ? 0.0 : t.InMillisecondsFSinceUnixEpoch(); };
  return base::DictValue().Set("id", id).Set("name", name).Set("instruction", instruction).Set("provider", provider).Set("model", model)
      .Set("profile", profile).Set("permissions", permissions.ToValue()).Set("kind", kind).Set("days", days).Set("minute", minute)
      .Set("active", active).Set("catch_up", catch_up)
      .Set("last_run", time(last_run)).Set("next_run", time(next_run)).Set("last_state", last_state);
}
std::optional<AssistantTask> AssistantTask::FromValue(const base::DictValue& value) {
  AssistantTask task;
  auto text = [&](const char* key) { const auto* v = value.FindString(key); return v ? *v : std::string(); };
  task.id = text("id"); task.name = text("name"); task.instruction = text("instruction");
  task.provider = text("provider"); task.model = text("model"); task.profile = text("profile");
  if (task.id.empty() || task.instruction.empty()) return std::nullopt;
  if (const auto* p = value.FindDict("permissions")) {
    task.permissions.tabs = p->FindBool("tabs").value_or(false); task.permissions.pages = p->FindBool("pages").value_or(false);
    task.permissions.actions = p->FindBool("actions").value_or(false); task.permissions.history = p->FindBool("history").value_or(false);
    task.permissions.web = p->FindBool("web").value_or(true); task.permissions.confirm_clicks = p->FindBool("confirm_clicks").value_or(false);
  }
  task.kind = std::clamp(value.FindInt("kind").value_or(0), 0, 3);
  task.days = value.FindInt("days").value_or(0) & 0x7f;
  task.minute = std::clamp(value.FindInt("minute").value_or(8 * 60), 0, 24 * 60 - 1);
  task.active = value.FindBool("active").value_or(true);
  task.catch_up = value.FindBool("catch_up").value_or(true);
  auto time = [&](const char* key) { const double ms = value.FindDouble(key).value_or(0); return ms > 0 ? base::Time::FromMillisecondsSinceUnixEpoch(ms) : base::Time(); };
  task.last_run = time("last_run"); task.next_run = time("next_run");
  task.last_state = std::clamp(value.FindInt("last_state").value_or(0), 0, 3);
  return task;
}
namespace {
// `day` at `minute` past local midnight.
base::Time AtMinute(base::Time day, int minute) {
  base::Time::Exploded e; day.LocalExplode(&e);
  e.hour = minute / 60; e.minute = minute % 60; e.second = 0; e.millisecond = 0;
  base::Time result;
  return base::Time::FromLocalExploded(e, &result) ? result : base::Time();
}
}
base::Time AssistantTaskNextRun(const AssistantTask& task, base::Time after) {
  if (task.kind == 2) {
    // Every hour at `minute % 60`.
    base::Time::Exploded e; after.LocalExplode(&e);
    e.minute = task.minute % 60; e.second = 0; e.millisecond = 0;
    base::Time t;
    if (!base::Time::FromLocalExploded(e, &t)) return base::Time();
    while (t <= after) t += base::Hours(1);
    return t;
  }
  if (task.kind == 1 && !task.days) return base::Time();
  // Noon keeps day arithmetic clear of DST changes.
  const base::Time noon = after.LocalMidnight() + base::Hours(12);
  for (int offset = 0; offset < 9; ++offset) {
    const base::Time day = noon + base::Days(offset);
    const base::Time candidate = AtMinute(day, task.minute);
    if (candidate.is_null() || candidate <= after) continue;
    if (task.kind == 1) {
      base::Time::Exploded e; day.LocalExplode(&e);
      if (!(task.days & (1 << ((e.day_of_week + 6) % 7)))) continue;
    }
    if (task.kind == 3 && !task.last_run.is_null()) return base::Time();
    return candidate;
  }
  return base::Time();
}
std::u16string AssistantTaskSchedule(const AssistantTask& task) {
  const auto clock = base::UTF8ToUTF16(base::StringPrintf("%d:%02d", task.minute / 60, task.minute % 60));
  if (task.kind == 2) return T(u"Co godzinę");
  if (task.kind == 3) return TF(u"Raz · {}", {clock});
  if (task.kind == 0) return TF(u"Codziennie · {}", {clock});
  static constexpr std::array<const char16_t*, 7> kDays = {u"Pn", u"Wt", u"Śr", u"Cz", u"Pt", u"So", u"Nd"};
  std::u16string days;
  for (int i = 0; i < 7; ++i) if (task.days & (1 << i)) days += (days.empty() ? u"" : u", ") + T(kDays.at(i));
  if (task.days == 0x1f) days = T(u"Dni robocze");
  if (task.days == 0x7f) days = T(u"Codziennie");
  return days + u" · " + clock;
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

AssistantPanel::AssistantPanel(AssistantHost* host)
    : host_(host),
      observed_default_permissions_(host->assistant_settings().defaults) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kComplementary);
  GetViewAccessibility().SetName(T(u"Asystent AI"));
  SetBackground(CreateAssistantBackground());
  SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
  SetNotifyEnterExitOnChild(true);
  scene_ = CreatePanelScene(this,true);
  auto weak=weak_factory_.GetWeakPtr();

  header_=AddChildView(std::make_unique<views::View>());
  auto* title_row=header_->AddChildView(std::make_unique<views::View>());
  auto* identity=title_row->AddChildView(CreateAssistantIdentity());
  title_row->AddChildView(CreateAssistantIcon(
      Later(base::BindRepeating(&AssistantPanel::NewSession, weak)),
      AssistantGlyph::kPlus, T(u"Nowa sesja")));
  access_action_ = title_row->AddChildView(CreateAssistantIcon(
      Later(base::BindRepeating(&AssistantPanel::ToggleMenu, weak, Menu::kAccess)),
      AssistantGlyph::kShield, T(u"Dostęp tej sesji")));
  title_row->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kSettings)),AssistantGlyph::kGear,T(u"Ustawienia asystenta")));
  title_row->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::Hide,weak)),AssistantGlyph::kClose,T(u"Zamknij")));
  SetSidebarPanelLayout(title_row,false,gfx::Insets(),4,identity,true);
  auto* navigation=header_->AddChildView(std::make_unique<views::View>());
  sessions_action_ = navigation->AddChildView(CreateAssistantIcon(
      Later(base::BindRepeating(&AssistantPanel::ToggleMenu, weak, Menu::kSessions)),
      AssistantGlyph::kHistory, T(u"Historia")));
  tasks_action_=navigation->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kTasks)),AssistantGlyph::kCalendar,T(u"Zadania zaplanowane")));
  SetSidebarPanelLayout(navigation,false,gfx::Insets(),4);
  SetSidebarPanelLayout(header_,true,gfx::Insets(),6);

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
  background_chip_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::ToggleTool,base::Unretained(this),std::string("background")),AssistantGlyph::kLayers,T(u"W tle")));
  model_chip_=tools_->AddChildView(CreateAssistantChip(Later(base::BindRepeating(&AssistantPanel::ToggleMenu,weak,Menu::kModel)),AssistantGlyph::kSpark,u""));
  send_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){self->Send(false);},base::Unretained(this)),AssistantGlyph::kArrow,T(u"Wyślij"),true));
  stop_=tools_->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::Stop,base::Unretained(this)),AssistantGlyph::kStop,T(u"Stop"),true));
  SetSidebarPanelLayout(tools_,false,gfx::Insets(),4,model_chip_,true);
  SetSidebarPanelLayout(this,true,gfx::Insets(16),10,viewport);
  resize_handle_=AddChildView(CreateAssistantResizeGrip(this));
  resize_handle_->SetProperty(views::kViewIgnoredByLayoutKey,true);
  NewSession();SetVisible(false);
  host_->LoadAssistantSessions(base::BindOnce(&AssistantPanel::SessionsLoaded,weak));
}

AssistantPanel::~AssistantPanel() {
  if (shortcut_dialog_) shortcut_dialog_->Close();
  if (save_timer_.IsRunning()) SaveSessions();
  menu_monitor_.reset();
  // Overlays left in the shell call back only through weak pointers.
  for (auto& session : sessions_) session->loader.reset();
  if (process_.IsValid()) process_.Terminate(0, false);
  // Drop view references before deleting the children. Their scene nodes
  // must still be released before the scene registry.
  viewport_ = nullptr;
  header_ = conversation_ = background_ = nullptr;
  sessions_action_ = tasks_action_ = access_action_ = nullptr;
  composer_ = chips_view_ = tools_ = nullptr;
  research_chip_ = background_chip_ = model_chip_ = send_ = stop_ = nullptr;
  prompt_ = nullptr;
  model_search_ = nullptr;
  menu_view_ = menu_footer_ = nullptr;
  menu_scroll_ = nullptr;
  resize_handle_ = nullptr;
  mention_rows_.clear();
  draft_instruction_ = nullptr;
  draft_time_ = nullptr;
  live_answer_ = nullptr;
  RemoveAllChildViews();
}
void AssistantPanel::Show(bool from_edge) {
  if (!host_->assistant_settings().enabled) return;
  hide_timer_.Stop();SetVisible(true); host_->shell()->LayoutAssistant(pinned_); prompt_->RequestFocus();
  unread_ = 0; NotifyBar();
  // Opened from its button or a shortcut, the pointer is still elsewhere: an
  // unpinned panel hides once the pointer moves and stays outside it.
  hover_check_.Stop();
  last_pointer_position_ = display::Screen::Get()->GetCursorScreenPoint();
  pointer_exit_armed_ = HoverBounds().Contains(last_pointer_position_);
  WatchPointer();
}
void AssistantPanel::WatchPointer() {
  // Keep watching while visible: child views, popovers and native capture can
  // swallow exit events. A blocked collapse must be retried after capture ends.
  if (pinned_ || !GetVisible() || hover_check_.IsRunning()) return;
  hover_check_.Start(FROM_HERE, base::Milliseconds(100), base::BindRepeating([](AssistantPanel* self) {
    if (!self->GetVisible() || self->pinned_) {
      self->hide_timer_.Stop(); self->hover_check_.Stop(); return;
    }
    const auto point = display::Screen::Get()->GetCursorScreenPoint();
    const bool moved = point != self->last_pointer_position_;
    self->last_pointer_position_ = point;
    if (self->HoverBounds().Contains(point) || self->host_->shell()->AssistantConsentContains(point)) {
      self->pointer_exit_armed_ = true; self->hide_timer_.Stop(); return;
    }
    if (self->resize_start_width_) { self->hide_timer_.Stop(); return; }
    if (!self->pointer_exit_armed_ && !moved) return;
    self->pointer_exit_armed_ = true;
    if (!self->hide_timer_.IsRunning())
      self->hide_timer_.Start(FROM_HERE, base::Milliseconds(450), base::BindOnce([](base::WeakPtr<AssistantPanel> panel) {
        if (!panel || !panel->GetVisible()) return;
        const auto cursor = display::Screen::Get()->GetCursorScreenPoint();
        if (!panel->HoverBounds().Contains(cursor) && !panel->host_->shell()->AssistantConsentContains(cursor))
          panel->Collapse();
      }, self->weak_factory_.GetWeakPtr()));
  }, base::Unretained(this)));
}
gfx::Rect AssistantPanel::HoverBounds() const {
  auto bounds = GetBoundsInScreen();
  // The window's edge beside the panel and the gap up to its toolbar button.
  bounds.Inset(gfx::Insets::TLBR(-48, 0, 0, -24));
  return bounds;
}
void AssistantPanel::Hide() {
  // Hiding is presentation only. Drafts, open editors and pending questions
  // remain intact, and a running task keeps working in the background.
  hide_timer_.Stop(); hover_check_.Stop();
  if (!GetVisible()) return;
  CloseMenu();
  SetVisible(false); host_->shell()->LayoutAssistant(false); NotifyBar();
}
bool AssistantPanel::busy() const { return selected().busy && !selected().background; }
gfx::Size AssistantPanel::CalculatePreferredSize(const views::SizeBounds&) const {return {preferred_width_,560};}
void AssistantPanel::OnMouseEntered(const ui::MouseEvent&) {
  pointer_exit_armed_ = true; hide_timer_.Stop(); WatchPointer();
}
void AssistantPanel::OnMouseExited(const ui::MouseEvent&) { pointer_exit_armed_ = true; WatchPointer(); }
void AssistantPanel::Collapse() {
  if(pinned_||resize_start_width_)return;
  Hide();
}
void AssistantPanel::OnResize(int amount,bool done) {
  pointer_exit_armed_ = true; WatchPointer();
  hide_timer_.Stop();
  if(!resize_start_width_)resize_start_width_=width();
  preferred_width_=std::clamp(*resize_start_width_-(base::i18n::IsRTL()?-amount:amount),360,760);
  PreferredSizeChanged();host_->shell()->LayoutAssistant(pinned_);
  if(done)resize_start_width_.reset();
}
void AssistantPanel::SetPinned(bool pinned) {
  pinned_ = pinned;
  hide_timer_.Stop(); hover_check_.Stop();
  last_pointer_position_ = display::Screen::Get()->GetCursorScreenPoint();
  pointer_exit_armed_ = true;
  WatchPointer();
  host_->shell()->LayoutAssistant(pinned_);
}
AssistantPanel::Session* AssistantPanel::Find(const std::string& key, uint64_t epoch) {
  for (auto& session : sessions_) if (session->key == key) return session->epoch == epoch ? session.get() : nullptr;
  return nullptr;
}
void AssistantPanel::OpenSettings() {
  CloseMenu();
  host_->shell()->OpenAssistantSettings();
}

// Sessions ---------------------------------------------------------------
bool AssistantPanel::Started(const Session& session) {
  return session.busy || std::ranges::any_of(session.messages, [](const Message& m) { return m.kind == 1; });
}
void AssistantPanel::StartNewConversation() {
  if (!host_->assistant_settings().enabled) return;
  const auto previous = selected().key;
  selected().composer_draft = prompt_->GetText();
  selected().composer_chips = chips_;
  if (busy()) Stop();
  CreateSession(true);
  if (selected().key != previous) { prompt_->SetText(u""); viewport_->ScrollTo(0); }
  UpdatePageControl();
  prompt_->RequestFocus();
}
bool AssistantPanel::SwitchProfile() {
  StopAll();
  ResetRuntime();
  const auto profile = host_->AssistantProfileId();
  std::optional<size_t> newest;
  for (size_t i = 0; i < sessions_.size(); ++i) {
    if (sessions_[i]->profile == profile &&
        (!newest || sessions_[i]->updated > sessions_[*newest]->updated)) {
      newest = i;
    }
  }
  if (newest) {
    SelectSession(*newest);
  } else {
    StartNewConversation();
  }
  return selected().profile == profile;
}

void AssistantPanel::NewSession() { CreateSession(false); }
void AssistantPanel::CreateSession(bool preserve_previous) {
  CloseMenu();
  // An untouched conversation is reused instead of piling up empty ones.
  const auto profile = host_->AssistantProfileId();
  const auto& settings = host_->assistant_settings();
  for (size_t i = sessions_.size(); !preserve_previous && i-- > 0;) {
    const auto& session = *sessions_[i];
    if (Started(session) || session.night_shift || !session.composer_draft.empty() ||
        session.profile != profile || session.permissions != settings.defaults ||
        session.confirm_background != settings.confirm_background ||
        host_->AssistantHasBackground(session.key)) continue;
    if (i != selected_) { if (busy()) Stop(); selected_ = i; }
    chips_.clear(); RenderChips(); RenderConversation(); UpdateControls(); UpdatePageControl();
    if (prompt_) prompt_->RequestFocus();
    return;
  }
  // A conversation in the foreground stops; background tasks keep running.
  if (!preserve_previous && !sessions_.empty() && busy()) Stop();
  if (sessions_.size() >= 200) {
    // Forget the least recently used idle conversation without a background page.
    auto idle = sessions_.end();
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
      if (!(*it)->busy && (*it)->composer_draft.empty() && !host_->AssistantHasBackground((*it)->key) && (idle == sessions_.end() || (*it)->updated < (*idle)->updated)) idle = it;
    if (idle == sessions_.end()) { Append(selected(), "Session limit reached. Delete old conversations in the history."); return; }
    const size_t removed = idle - sessions_.begin();
    sessions_.erase(idle);
    if (selected_ >= removed && selected_ > 0) --selected_;
  }
  auto session = std::make_unique<Session>();
  session->key = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session->id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session->profile = host_->AssistantProfileId();
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
  if (index >= sessions_.size() || index == selected_ ||
      sessions_[index]->profile != host_->AssistantProfileId()) {
    return;
  }
  if (busy()) Stop();
  selected().composer_draft = prompt_->GetText();
  selected().composer_chips = chips_;
  // Leaving an untouched conversation without a draft drops it.
  const auto left = sessions_[selected_]->key;
  selected_ = index;
  if (auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s->key == left; });
      it != sessions_.end() && !Started(**it) && (*it)->composer_draft.empty() && !host_->AssistantHasBackground(left)) {
    const size_t removed = it - sessions_.begin();
    sessions_.erase(it);
    if (selected_ > removed) --selected_;
  }
  prompt_->SetText(selected().composer_draft);
  chips_ = selected().composer_chips; RenderChips();
  RenderConversation(); UpdateControls(); UpdatePageControl();
  if (GetWidget()) GetWidget()->LayoutRootViewIfNecessary();
  viewport_->ScrollTo(viewport_->maximum());
}
void AssistantPanel::DeleteSession(std::string key) {
  auto it = std::ranges::find_if(sessions_, [&](const auto& s) { return s->key == key; });
  if (it == sessions_.end()) return;
  StopSession(**it);
  host_->AssistantDiscardBackground(key);
  const size_t index = it - sessions_.begin();
  const bool was_selected = index == selected_;
  sessions_.erase(it);
  if (selected_ > index) --selected_;
  if (sessions_.empty()) { selected_ = 0; const auto keep = menu_; NewSession(); menu_ = keep; }
  else if (was_selected) {
    // The newest conversation of this profile takes its place, or a new one.
    selected_ = std::min(selected_, sessions_.size() - 1);
    const auto profile = host_->AssistantProfileId();
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
  base::ListValue tasks;
  for (const auto& task : tasks_) tasks.Append(task.ToValue());
  host_->SaveAssistantSessions(base::DictValue().Set("version", 1).Set("sessions", std::move(list)).Set("tasks", std::move(tasks)));
}
void AssistantPanel::SessionsLoaded(std::optional<base::DictValue> saved) {
  loaded_ = true;
  std::vector<std::unique_ptr<Session>> restored;
  const auto* list = saved ? saved->FindList("sessions") : nullptr;
  const auto& d = host_->assistant_settings().defaults;
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
  if (const auto* tasks = saved ? saved->FindList("tasks") : nullptr) {
    for (const auto& item : *tasks) {
      const auto* dict = item.GetIfDict();
      auto task = dict ? AssistantTask::FromValue(*dict) : std::nullopt;
      if (!task || FindTask(task->id)) continue;
      if (task->active && !task->catch_up && !task->next_run.is_null() &&
          task->next_run < scheduler_started_) {
        task->next_run = task->kind == 3 ? base::Time() : AssistantTaskNextRun(*task, scheduler_started_);
        ScheduleSave();
      }
      tasks_.push_back(std::move(*task));
    }
  }
  task_timer_.Start(FROM_HERE, base::Seconds(30), base::BindRepeating([](AssistantPanel* self) { self->CheckTasks(base::Time::Now()); }, base::Unretained(this)));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
    if (self) self->CheckTasks(base::Time::Now()); }, weak_factory_.GetWeakPtr()), base::Seconds(5));
  if (menu_ == Menu::kSessions || menu_ == Menu::kTasks) BuildMenu();
  NotifyBar();
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
  if (session.confirmation &&
      *session.confirmation->FindString("tool") == "adblock_add_rules") {
    host_->AssistantTool("adblock_clear_preview",
        session.confirmation->FindDict("args")->Clone(), session.permissions,
        base::DoNothing(), false, session.background ? session.key : std::string());
  }
  // Disconnecting the RPC aborts its provider request. Pending native callbacks
  // carry the old epoch, so cancellation cannot dispatch another mutation.
  session.busy = false; session.queue.clear(); session.results.clear(); session.confirmation.reset();
  session.request.reset(); session.page_control = false;
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
  for(auto& session:sessions_){session->id=base::Uuid::GenerateRandomV4().AsLowercaseString();session->auto_confirm=false;}
}
void AssistantPanel::RevokeAccess() {
  StopAll();
  selected().permissions={false,false,false,false,false,true};
  selected().auto_confirm=false;
  std::erase_if(chips_, [](const auto& chip) { return chip.kind == "background"; });
  RenderChips();
  ResetRuntime();UpdateControls();
  Append(selected(),base::UTF16ToUTF8(T(u"Dostęp odebrany. Zadania zatrzymane.")));
  if (menu_ == Menu::kAccess) BuildMenu();
}
void AssistantPanel::SettingsChanged() {
  const auto& settings = host_->assistant_settings();
  const auto previous = std::exchange(observed_default_permissions_, settings.defaults);
  if (!settings.enabled) {
    StopAll(); CloseMenu(); SetVisible(false); host_->shell()->LayoutAssistant(false);
    UpdatePageControl(); return;
  }
  // Narrowing applies at once, also to running sessions; wider access waits for a new session.
  bool narrowed = false;
  const auto& d = settings.defaults;
  for (auto& session : sessions_) {
    auto p = session->permissions;
    // Provider refreshes and unrelated settings writes must preserve explicit
    // shortcut consent. Revoke when the user actually narrows a default.
    p.tabs &= !previous.tabs || d.tabs;
    p.pages &= !previous.pages || d.pages;
    p.actions &= !previous.actions || d.actions;
    p.history &= !previous.history || d.history;
    p.web &= !previous.web || d.web;
    p.confirm_clicks |= !previous.confirm_clicks && d.confirm_clicks;
    const bool confirm_background = session->confirm_background || settings.confirm_background;
    if (p != session->permissions || confirm_background != session->confirm_background || (!settings.background && session->background)) {
      session->permissions = p; session->confirm_background = confirm_background; narrowed = true;
    }
  }
  if (narrowed) { StopAll(); ResetRuntime(); }
  if (!settings.background || !selected().permissions.actions) std::erase_if(chips_, [](const auto& chip) { return chip.kind == "background"; });
  SyncSelectedModel();
  RenderChips(); UpdateControls(); RenderBackground();
  if (menu_ != Menu::kNone) BuildMenu();
}
void AssistantPanel::BackendAvailabilityChanged() {
  SyncSelectedModel();
  UpdateControls();
  if (menu_ == Menu::kModel) BuildMenu();
}
void AssistantPanel::SyncSelectedModel() {
  const auto& settings = host_->assistant_settings();
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
}

// Menus ------------------------------------------------------------------
void AssistantPanel::ToggleMenu(Menu menu) {
  if (menu_ == menu || (menu == Menu::kTasks && menu_ == Menu::kTaskEditor)) {
    CloseMenu();
    if (menu == Menu::kSessions) prompt_->RequestFocus();
    return;
  }
  draft_instruction_ = nullptr; draft_time_ = nullptr;
  menu_ = menu; model_query_.clear(); confirm_clear_ = false;
  if (menu == Menu::kTasks) { unread_ = 0; NotifyBar(); }
  BuildMenu();
  if ((menu == Menu::kModel || menu == Menu::kSessions) && model_search_) model_search_->RequestFocus();
}
void AssistantPanel::CloseMenu() {
  if (menu_ == Menu::kMention && mention_at_) mention_dismissed_ = mention_at_;
  menu_ = Menu::kNone; mention_rows_.clear(); menu_footer_ = nullptr; model_search_ = nullptr; menu_scroll_ = nullptr;
  menu_monitor_.reset();
  draft_instruction_ = nullptr; draft_time_ = nullptr;
  if (menu_view_) {
    auto old_menu = RemoveChildViewT(menu_view_.get());
    menu_view_ = nullptr;
  }
  UpdateAssistantButton(sessions_action_, T(u"Sesje"), false);
  UpdateAssistantButton(tasks_action_, T(u"Zadania zaplanowane"), false);
  UpdateControls();
  InvalidateLayout(); scene_->Schedule();
}
void AssistantPanel::BuildMenu() {
  mention_rows_.clear(); menu_footer_ = nullptr; model_search_ = nullptr; menu_scroll_ = nullptr;
  draft_instruction_ = nullptr; draft_time_ = nullptr;
  if (menu_view_) {
    auto old_menu = RemoveChildViewT(menu_view_.get());
    menu_view_ = nullptr;
  }
  if (menu_ == Menu::kNone) return;
  auto surface = CreateAssistantSurface(4);
  SetSidebarPanelLayout(surface.get(), true, gfx::Insets(6), 2);
  surface->GetViewAccessibility().SetRole(menu_ == Menu::kMention ? ax::mojom::Role::kListBox : ax::mojom::Role::kDialog);
  switch (menu_) {
    case Menu::kSessions: surface->GetViewAccessibility().SetName(T(u"Historia")); BuildSessionsMenu(surface.get()); break;
    case Menu::kTasks: surface->GetViewAccessibility().SetName(T(u"Zadania zaplanowane")); BuildTasksMenu(surface.get()); break;
    case Menu::kTaskEditor: surface->GetViewAccessibility().SetName(T(u"Zadanie zaplanowane")); BuildTaskEditor(surface.get()); break;
    case Menu::kAccess: surface->GetViewAccessibility().SetName(T(u"Dostęp tej sesji")); BuildAccessMenu(surface.get()); break;
    case Menu::kSettings: surface->GetViewAccessibility().SetName(T(u"Ustawienia asystenta")); BuildSettingsMenu(surface.get()); break;
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
  UpdateAssistantButton(sessions_action_, T(u"Historia"), menu_ == Menu::kSessions);
  UpdateAssistantButton(tasks_action_, T(u"Zadania zaplanowane"), menu_ == Menu::kTasks || menu_ == Menu::kTaskEditor);
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
  const auto profile = host_->AssistantProfileId();
  // This profile's conversations with something written, newest first.
  std::vector<size_t> order;
  for (size_t i = 0; i < sessions_.size(); ++i) if (sessions_[i]->profile == profile && (Started(*sessions_[i]) || !sessions_[i]->composer_draft.empty())) order.push_back(i);
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
      const auto at = Fold(s.title.empty() ? s.composer_draft : s.title).find(query);
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
    row.title = s.title.empty() ? (s.composer_draft.empty() ? T(u"Nowa sesja") : s.composer_draft.substr(0,48)) : s.title;
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
    const auto profile = self->host_->AssistantProfileId();
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
  if (started && !granted) {
    AssistantRow fresh;
    fresh.title = T(u"Nowa sesja");
    fresh.detail = T(u"Zacznij z domyślnym dostępem");
    fresh.mark = {AssistantGlyph::kPlus, {}, SK_ColorTRANSPARENT};
    body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::StartNewConversation, weak)), std::move(fresh)));
  }
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
  if (host_->assistant_settings().background && p.actions) {
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
void AssistantPanel::BuildSettingsMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto& settings = host_->assistant_settings();
  const auto& d = settings.defaults;
  auto* head = body->AddChildView(std::make_unique<views::View>());
  head->AddChildView(CreateAssistantIcon(Later(base::BindRepeating(&AssistantPanel::CloseMenu, weak)), AssistantGlyph::kBack, T(u"Wróć")));
  auto* title = head->AddChildView(CreateAssistantEyebrow(T(u"Ustawienia asystenta"), {}));
  SetSidebarPanelLayout(head, false, gfx::Insets(), 4, title, true);
  AssistantRow pin;
  pin.title = T(u"Przypnij panel asystenta");
  pin.toggle = pinned_;
  pin.mark = {AssistantGlyph::kPin, {}, SK_ColorTRANSPARENT};
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
    if (!self) return;
    self->SetPinned(!self->pinned());
    self->BuildMenu();
  }, weak)), std::move(pin)));
  // The default model of every new conversation.
  AssistantRow model; model.title = T(u"Model");
  model.detail = settings.default_model.empty() ? T(u"Nie wybrano") : base::UTF8ToUTF16(settings.default_model);
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::ToggleMenu, weak, Menu::kModel)), std::move(model)));
  const std::array<std::tuple<bool, std::u16string, std::u16string>, 8> rows = {{
      {d.tabs, T(u"Otwarte i przypięte karty"), T(u"Podpowiedzi po @")},
      {d.pages, T(u"Czytanie bieżącej karty"), T(u"Treść strony trafia do rozmowy")},
      {d.web, T(u"Szukanie w internecie"), T(u"Bez ciasteczek i logowania")},
      {d.actions, T(u"Działania na stronach"), T(u"Klikanie i wypełnianie formularzy")},
      {d.history, T(u"Historia przeglądania"), T(u"Tego profilu")},
      {d.confirm_clicks, T(u"Pytaj przed kliknięciem"), T(u"Na widocznej karcie")},
      {settings.background, T(u"Zadania w tle"), T(u"Niewidoczna karta tego profilu")},
      {settings.confirm_background, T(u"Pytaj przed kliknięciem w tle"), T(u"Zadanie czeka w panelu")}}};
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& [on, label, detail] = rows[i];
    AssistantRow row; row.title = label; row.detail = detail; row.toggle = on;
    body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::ToggleDefault, weak, static_cast<int>(i))), std::move(row)));
  }
  body->AddChildView(CreateAssistantNote(T(u"Dotyczy nowych rozmów. Węższy dostęp działa od razu.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 8, 6, 8));
  auto* actions = body->AddChildView(std::make_unique<views::View>());
  auto* spacer = actions->AddChildView(std::make_unique<views::View>());
  actions->AddChildView(CreateAssistantLink(Later(base::BindRepeating(&AssistantPanel::OpenSettings, weak)), T(u"Połączenia AI i modele")));
  SetSidebarPanelLayout(actions, false, gfx::Insets::TLBR(2, 4, 2, 0), 8, spacer, true);
}
void AssistantPanel::ToggleDefault(int index) {
  auto settings = host_->assistant_settings();
  auto& d = settings.defaults;
  const std::array<bool*, 8> values = {&d.tabs, &d.pages, &d.web, &d.actions, &d.history, &d.confirm_clicks,
                                       &settings.background, &settings.confirm_background};
  if (index < 0 || index >= static_cast<int>(values.size())) return;
  *values[index] = !*values[index];
  host_->SetAssistantSettings(std::move(settings));
  if (menu_ == Menu::kSettings) BuildMenu();
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
  const auto& settings = host_->assistant_settings();
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
  const auto& settings = host_->assistant_settings();
  const auto& session = selected();
  const auto query = Fold(mention_query_);
  struct Item { AssistantChip chip; std::u16string detail, badge, note; bool mono = true, disabled = false; };
  std::vector<Item> tabs, pins, tools;
  auto matches = [&](const std::u16string& a, const std::u16string& b) { return query.empty() || Fold(a).find(query) != std::u16string::npos || Fold(b).find(query) != std::u16string::npos; };
  auto taken = [&](const AssistantChip& chip) { return std::ranges::any_of(chips_, [&](const auto& c) { return c.kind == chip.kind && c.ref == chip.ref; }); };
  const bool can_list = session.permissions.tabs && session.profile == host_->AssistantProfileId();
  if (can_list) {
    AssistantPermissions list; list.tabs = true;
    const auto context = host_->AssistantContext(list);
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
  if (!menu_view_ || !menu_monitor_) return;
  const auto point = menu_monitor_->GetLastMouseLocation();
  auto inside = [&](const views::View* view) { return view && view->GetVisible() && view->GetBoundsInScreen().Contains(point); };
  // The menu itself, its own toggle (which closes it) and, for @, the field it belongs to.
  if (inside(menu_view_)) return;
  if (inside(header_)) return;
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
  if (chip.kind == "background" && !(host_->assistant_settings().background && selected().permissions.actions)) return;
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
  if (kind == "background" && !selected().permissions.actions) {
    ToggleMenu(Menu::kAccess); return;
  }
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
std::u16string AssistantPanel::draft_for_testing() const { return std::u16string(prompt_->GetText()); }
void AssistantPanel::PreparePageAction(std::u16string text, AssistantShortcut shortcut,
                                      bool night_shift) {
  if (shortcut_dialog_ && !shortcut_dialog_->IsClosed()) {
    shortcut_dialog_->Activate();
    return;
  }
  auto* current = host_->AssistantCurrentPage();
  if (!current) return;
  auto document = current->GetPrimaryMainFrame()->GetWeakDocumentPtr();
  // Keep the previous conversation and composer intact, including running work.
  const auto previous = selected().key;
  selected().composer_draft = prompt_->GetText();
  selected().composer_chips = chips_;
  CreateSession(true);
  if (selected().key == previous) return;
  selected().night_shift = night_shift;
  if (night_shift) {
    const bool pages = selected().permissions.pages;
    selected().permissions = {.pages = pages, .web = false};
  }
  prompt_->SetText(text); prompt_->SetSelectedRange(gfx::Range(text.size()));
  if (!night_shift) InsertChip({"tab", "@current", T(u"Bieżąca karta"), 0}, false);
  PromptChanged();
  auto& access = selected().permissions;
  const auto& remembered = host_->assistant_settings().shortcut_access;
  const std::string key = selected().profile + "/" + shortcut.id;
  const auto grant = remembered.find(key);
  const int granted = grant == remembered.end() ? 0 : grant->second;
  int missing = 0;
  int requested = 0;
  std::vector<std::u16string> labels;
  for (size_t i = 0; i < kShortcutAccess.size(); ++i) {
    const auto [permission, label] = kShortcutAccess[i];
    if (!(shortcut.required.*permission)) continue;
    requested |= 1 << i;
    if (granted & (1 << i)) access.*permission = true;
    if (access.*permission) continue;
    missing |= 1 << i;
    labels.push_back(u"• " + T(label));
  }
  UpdateControls();
  if (!missing) {
    if (!selected().provider.empty()) Send(false);
    return;
  }
  auto accept = base::BindRepeating(
      [](base::WeakPtr<AssistantPanel> panel, std::string session_key, std::string grant_key,
         content::WeakDocumentPtr document, int missing, int apply, bool remember) {
        if (!panel || panel->selected().key != session_key ||
            panel->selected().profile != panel->host_->AssistantProfileId() ||
            !panel->host_->assistant_settings().enabled) return;
        auto* page = panel->host_->AssistantCurrentPage();
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!frame || !page || page->GetPrimaryMainFrame() != frame) return;
        if (remember) {
          auto settings = panel->host_->assistant_settings();
          settings.shortcut_access[grant_key] |= missing;
          panel->host_->SetAssistantSettings(std::move(settings));
        }
        for (size_t i = 0; i < kShortcutAccess.size(); ++i)
          if (apply & (1 << i)) panel->selected().permissions.*kShortcutAccess[i].permission = true;
        panel->UpdateControls();
        if (!panel->selected().provider.empty()) panel->Send(false);
      }, weak_factory_.GetWeakPtr(), selected().key, key, document, missing,
      missing | (requested & granted));
  auto cancel = base::BindRepeating([](base::WeakPtr<AssistantPanel> panel, const std::string& key) {
    if (!panel || panel->selected().key != key) return;
    panel->prompt_->SetText(u"");
    panel->chips_.clear();
    panel->RenderChips();
    panel->PromptChanged();
  }, weak_factory_.GetWeakPtr(), selected().key);
  auto remember = std::make_shared<bool>(false);
  auto dialog = ui::DialogModel::Builder()
      .SetTitle(T(u"Zezwolić na dostęp?"))
      .AddParagraph(ui::DialogModelLabel(base::JoinString(labels, u"\n")), shortcut.title)
      .AddOkButton(base::BindOnce([](base::RepeatingCallback<void(bool)> accept,
                                    std::shared_ptr<bool> remember) { accept.Run(*remember); },
                                  accept, remember),
                   ui::DialogModel::Button::Params().SetLabel(T(u"Zezwól")))
      .AddCancelButton(base::BindOnce(cancel),
                       ui::DialogModel::Button::Params().SetLabel(T(u"Anuluj")))
      .SetCloseActionCallback(base::BindOnce(cancel))
      .AddExtraButton(base::BindRepeating(
          [](base::WeakPtr<AssistantPanel> panel, std::shared_ptr<bool> remember,
             const ui::Event&) {
            if (!panel || !panel->shortcut_dialog_ || panel->shortcut_dialog_->IsClosed()) return;
            *remember = true;
            panel->shortcut_dialog_->widget_delegate()->AsDialogDelegate()->AcceptDialog();
          }, weak_factory_.GetWeakPtr(), remember),
          ui::DialogModel::Button::Params().SetLabel(T(u"Zawsze zezwalaj")))
      .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
      .Build();
  auto* parent = GetWidget();
  if (!parent) return;
  auto* prompt = views::DialogDelegate::CreateDialogWidget(
      views::BubbleDialogModelHost::CreateModal(std::move(dialog), ui::mojom::ModalType::kWindow),
      parent->GetNativeWindow(), parent->GetNativeView());
  prompt->SetColorModeOverride(parent->color_mode_override());
  shortcut_dialog_ = prompt->GetWeakPtr();
  prompt->Show();
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
bool AssistantPanel::SelectedConversationContainsForTesting(std::string_view text) const {
  return selected().transcript.find(text) != std::string::npos;
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
  return static_cast<size_t>(std::ranges::count_if(sessions_, [&](const auto& s) { return s->background && (s->busy || host_->AssistantHasBackground(s->key)); }));
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
    session->profile = host_->AssistantProfileId();
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
  if (session.busy || host_->AssistantProfileId().empty()) return;
  const auto text = std::u16string(base::TrimWhitespace(prompt_->GetText(), base::TRIM_ALL));
  if (text.empty() && chips_.empty()) return;
  CloseMenu();
  if (session.profile != host_->AssistantProfileId()) { Append(session, "This session belongs to another profile. Start a new session."); return; }
  if (session.provider.empty()) { Append(session, base::UTF16ToUTF8(T(u"Dodaj model w Ustawieniach → Asystent AI."))); return; }
  const auto& settings = host_->assistant_settings();
  if (!settings.BackendEnabled(session.provider)) {
    Append(session, "This Assistant provider is disabled.");
    return;
  }
  const bool research = !session.night_shift && std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "research"; });
  bool in_background = background || std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "background"; });
  if (in_background && (session.night_shift || !(settings.background && session.permissions.actions))) {
    Append(session, base::UTF16ToUTF8(T(u"Praca w tle wymaga klikania i włączonej pracy w tle w Ustawieniach.")));
    return;
  }
  // References travel as text the model already knows: @current, @tab:, @pin:.
  std::string prompt = base::UTF16ToUTF8(text);
  std::string references;
  if (!session.night_shift) for (const auto& chip : chips_) if (IsPage(chip)) references += (references.empty() ? "" : ", ") + chip.ref + " (" + base::UTF16ToUTF8(chip.title) + ")";
  if (!references.empty()) prompt += (prompt.empty() ? "" : "\n\n") + std::string("Referenced pages: ") + references;
  if (prompt.empty()) prompt = research ? "Research this." : "Continue.";
  if (in_background && !host_->AssistantHasBackground(session.key) && !host_->AssistantStartBackground(session.key, {}, GURL())) {
    Append(session, base::UTF16ToUTF8(T(u"Nie udało się otworzyć karty w tle.")));
    return;
  }
  in_background = in_background || (session.background && host_->AssistantHasBackground(session.key));
  auto chips = std::exchange(chips_, {});
  session.composer_draft.clear(); session.composer_chips.clear();
  prompt_->SetText(u""); RenderChips();
  if (session.title.empty()) session.title = text.empty() ? chips.front().title : text.substr(0, 48);
  Append(session, "You: " + base::UTF16ToUTF8(text), chips);
  session.background = in_background;
  const auto permissions = session.night_shift
      ? AssistantPermissions{.pages = session.permissions.pages, .web = false}
      : session.permissions;
  session.request = base::DictValue().Set("id", session.id).Set("prompt", prompt).Set("research", research).Set("background", in_background)
      .Set("concurrency", settings.concurrency)
      .Set("date", base::DictValue().Set("unix_ms", static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()))
           .Set("utc_offset_minutes", TimelineUtcOffsetMinutes(base::Time::Now())))
      .Set("config", base::DictValue().Set("provider", session.provider).Set("model", session.model))
      .Set("permissions", permissions.ToValue())
      .Set("context", session.night_shift ? base::DictValue() : host_->AssistantContext(permissions));
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
  if(process_.IsValid()){PollRuntime(session.key,session.epoch,0);return;}
  host_->WithAssistantApiKey(base::BindOnce(&AssistantPanel::StartRuntime,weak_factory_.GetWeakPtr(),session.key,session.epoch));
}
void AssistantPanel::StartRuntime(std::string key,uint64_t epoch,std::string api_key){
  auto* current=Find(key,epoch);if(!current||!current->busy)return;
  auto& session=*current;
  if (!process_.IsValid()) {
    base::FilePath exe; base::PathService::Get(base::DIR_EXE, &exe);
#if BUILDFLAG(IS_WIN)
    const auto runtime = exe.AppendASCII("zitter_assistant.exe");
#else
    const auto runtime = exe.AppendASCII("zitter_assistant");
#endif
    port_ = 49152 + static_cast<int>(base::RandGenerator(16384)); token_ = base::ToLowerASCII(base::HexEncode(base::RandBytesAsVector(24)));
    base::CommandLine command(runtime);
    command.AppendSwitchASCII("port", base::NumberToString(port_));
    command.AppendSwitchASCII("token", token_);
    command.AppendSwitchASCII("parent", base::NumberToString(base::GetCurrentProcId()));
    base::LaunchOptions options;
    host_->assistant_settings().ApplyCliPaths(options.environment);
#if BUILDFLAG(IS_WIN)
    if(!api_key.empty())options.environment[L"ZITTER_OPENAI_API_KEY"]=base::UTF8ToWide(api_key);
#else
    if(!api_key.empty())options.environment["ZITTER_OPENAI_API_KEY"]=api_key;
#endif
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
      Append(session, "Could not start the native assistant helper. Check the Zitter installation."); return;
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
      // A cold helper start (fresh files scanned by antivirus) can take a while.
      if (attempts >= 300) {
        self->StopSession(*session);
        if (self->process_.IsValid()) { self->process_.Terminate(0, false); self->process_.Close(); }
        self->Append(*session, "Assistant runtime did not start. Check the native helper beside the Zitter executable."); return;
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
  auto factory = host_->AssistantBrowserContext()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
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
  auto factory = host_->AssistantBrowserContext()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
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
    session->busy = false; session->finished = true; session->page_control = false;
    if (!session->task_id.empty()) TaskFinished(*session, status == T(u"Gotowe"));
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
  if (session.profile != host_->AssistantProfileId()) { StopSession(session); Append(session, "Stopped: active profile changed."); return; }
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
  const bool night_shift_tool = tool == "night_shift_inspect" ||
                                tool == "night_shift_propose";
  if (session.night_shift != night_shift_tool) {
    const char* error = session.night_shift
        ? "Only Night Shift tools are available in this turn."
        : "Start this request from Night Shift.";
    session.results.Append(base::DictValue().Set("tool", tool).Set(
        "result", base::DictValue().Set("error", error)));
    ++session.index;
    RunNext(session);
    return;
  }
  if (tool == "list_tasks") {
    base::ListValue list;
    for (const auto& task : tasks_) if (task.profile == session.profile)
      list.Append(base::DictValue().Set("name", task.name).Set("instruction", task.instruction).Set("schedule", base::UTF16ToUTF8(AssistantTaskSchedule(task)))
          .Set("active", task.active).Set("model", task.model));
    session.results.Append(base::DictValue().Set("tool", tool).Set("result", std::move(list)));
    ++session.index; RunNext(session); return;
  }
  if (tool == "schedule_task" || tool == "add_search_engine") {
    // Both wait for the user: a proposal card, or a question with the engine's details.
    const auto& args = *action.GetDict().FindDict("args");
    auto fail = [&](const char* error) {
      session.results.Append(base::DictValue().Set("tool", tool).Set("result", base::DictValue().Set("error", error)));
      ++session.index; RunNext(session);
    };
    Message message; message.kind = 5;
    if (tool == "schedule_task") {
      AssistantTask task;
      const auto* instruction = args.FindString("instruction");
      if (!instruction || instruction->empty()) { fail("An instruction is required"); return; }
      task.instruction = instruction->substr(0, 4000);
      const auto* name = args.FindString("name");
      task.name = name && !name->empty() ? name->substr(0, 80) : task.instruction.substr(0, 60);
      const auto* schedule = args.FindDict("schedule");
      const auto* kind = schedule ? schedule->FindString("kind") : nullptr;
      task.kind = !kind ? 0 : *kind == "days" ? 1 : *kind == "hourly" ? 2 : *kind == "once" ? 3 : 0;
      if (const auto* days = schedule ? schedule->FindList("days") : nullptr) {
        static constexpr std::array<std::string_view, 7> kNames = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
        for (const auto& day : *days)
          for (size_t i = 0; i < kNames.size(); ++i)
            if (day.is_string() && base::ToLowerASCII(day.GetString()).starts_with(kNames.at(i))) task.days |= 1 << i;
      }
      if (task.kind == 1 && !task.days) { fail("Choose at least one weekday for kind days"); return; }
      if (const auto* time = schedule ? schedule->FindString("time") : nullptr)
        if (const auto minute = ParseClock(*time)) task.minute = *minute;
      // The task's access never exceeds this conversation's.
      task.permissions = session.permissions;
      if (const auto* access = args.FindDict("permissions")) {
        task.permissions.tabs &= access->FindBool("tabs").value_or(false);
        task.permissions.pages &= access->FindBool("pages").value_or(task.permissions.pages);
        task.permissions.actions &= access->FindBool("actions").value_or(task.permissions.actions);
        task.permissions.history &= access->FindBool("history").value_or(false);
        task.permissions.web &= access->FindBool("web").value_or(task.permissions.web);
      }
      task.permissions.confirm_clicks = false;
      task.provider = session.provider; task.model = session.model; task.profile = session.profile;
      std::vector<std::u16string> access;
      if (task.permissions.pages) access.push_back(T(u"Strony"));
      if (task.permissions.actions) access.push_back(T(u"Działania"));
      if (task.permissions.web) access.push_back(T(u"Internet"));
      if (task.permissions.history) access.push_back(T(u"Historia"));
      if (task.permissions.tabs) access.push_back(T(u"Karty"));
      const auto next = AssistantTaskNextRun(task, base::Time::Now());
      message.text = base::UTF16ToUTF8(TF(u"Zaplanować zadanie **{}**?\n\n- Instrukcja: {}\n- Kiedy: {}\n- Model: `{}`\n- Uprawnienia: {}",
          {base::UTF8ToUTF16(task.name), base::UTF8ToUTF16(task.instruction), AssistantTaskSchedule(task) + (next.is_null() ? u"" : u" · " + TF(u"pierwszy raz {}", {WhenText(next)})),
           base::UTF8ToUTF16(task.model), access.empty() ? T(u"bez dostępu do przeglądarki") : base::JoinString(access, u" · ")}));
      message.options = {base::UTF16ToUTF8(T(u"Zaplanuj")), base::UTF16ToUTF8(T(u"Zmień ustawienia")), base::UTF16ToUTF8(T(u"Tylko teraz, raz"))};
      message.state = 2;
      session.proposal = std::move(task);
    } else {
      const auto* name = args.FindString("name"); const auto* keyword = args.FindString("keyword"); const auto* url = args.FindString("url");
      if (!name || !keyword || !url) { fail("name, keyword and url are required"); return; }
      std::string address = *url;
      if (const auto at = address.find("%s"); at != std::string::npos) address.replace(at, 2, "{query}");
      if (!ValidSearchProvider({*name, base::ToLowerASCII(*keyword), address})) { fail("Invalid search engine: keyword a-z0-9 up to 16, url with one {query} after the host"); return; }
      message.text = base::UTF16ToUTF8(TF(u"Dodać wyszukiwarkę **{}** do paska adresu?\n\n- Skrót: `{}`\n- Adres: `{}`",
          {base::UTF8ToUTF16(*name), base::UTF8ToUTF16(base::ToLowerASCII(*keyword)), base::UTF8ToUTF16(address)}));
      message.options = {base::UTF16ToUTF8(TF(u"Dodaj ze skrótem {}", {base::UTF8ToUTF16(base::ToLowerASCII(*keyword))})), base::UTF16ToUTF8(T(u"Nie dodawaj"))};
      session.proposal.reset();
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
  // Session consent lasts across turns, in memory only; background defaults also apply.
  const bool confirmed = !IsAssistantDebugTool(tool) && tool != "adblock_add_rules" &&
      tool != "adblock_diagnose" &&
      (session.auto_confirm || (session.background && !session.confirm_background));
  auto tool_args = action.GetDict().FindDict("args")->Clone();
  tool_args.Remove("_page_confirmation");
  host_->AssistantTool(tool, std::move(tool_args), session.permissions,
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
    if (const auto* document = value.GetDict().FindString("debug_document"))
      session->confirmation->FindDict("args")->Set("_debug_document", *document);
    if (const auto* confirmation = value.GetDict().FindString("page_confirmation"))
      session->confirmation->FindDict("args")->Set("_page_confirmation", *confirmation);
    const auto* label = value.GetDict().FindString("label");
    session->confirm_label = base::UTF8ToUTF16(label ? label->substr(0, 120) : "Click");
    session->confirm_key = value.GetDict().FindBool("key").value_or(false);
    const auto* prompt = value.GetDict().FindString("prompt");
    session->confirm_prompt = prompt ? base::UTF8ToUTF16(prompt->substr(0, 300)) : std::u16string();
    session->status = T(u"Czeka na zgodę");
    session->needed_user = true;
    if (session == &selected()) RenderConversation();
    if (session->background) { UpdatePageControl(); RenderBackground(); return; }
    UpdatePageControl();
    // A small prompt beside the element; without its bounds, above the capsule.
    const auto& args = *session->confirmation->FindDict("args");
    const auto* tab = args.FindString("tab"); const auto* element = args.FindString("element");
    host_->AssistantElementBounds(tab ? *tab : "@current", element ? *element : "", base::BindOnce(
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
          const bool debug = IsAssistantDebugTool(*session->confirmation->FindString("tool"));
          const bool one_time = debug ||
              *session->confirmation->FindString("tool") == "adblock_add_rules" ||
              *session->confirmation->FindString("tool") == "adblock_diagnose";
          auto bubble = CreateAssistantConsentBubble(!session->confirm_prompt.empty() ? session->confirm_prompt : session->confirm_key ? TF(u"Nacisnąć {}?", {session->confirm_label}) : TF(u"Kliknąć „{}”?", {session->confirm_label}),
              debug ? session->confirm_label : session->status == T(u"Czeka na zgodę") && !session->title.empty() ? session->title : T(u"Asystent prosi o zgodę"),
              base::BindRepeating(act, self, key, epoch, 1), base::BindRepeating(act, self, key, epoch, 0),
              one_time ? base::RepeatingClosure() : base::BindRepeating(act, self, key, epoch, 2));
          auto* allow = AssistantConsentAllowButton(bubble.get());
          if (*session->confirmation->FindString("tool") == "adblock_add_rules" &&
              session->confirmation->FindDict("args")->contains("_page_confirmation")) {
            self->host_->AssistantTool("adblock_show_preview",
                session->confirmation->FindDict("args")->Clone(), session->permissions,
                base::DoNothing(), false, {});
          }
          self->host_->shell()->SetAssistantConsent(std::move(bubble), anchor);
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
  const auto* pending = session.index < session.queue.size() ? session.queue[session.index].GetDict().FindString("tool") : nullptr;
  const std::string tool = pending ? *pending : "ask_user";
  base::DictValue result;
  result.Set("answer", answer);
  if (tool == "schedule_task" && session.proposal) {
    if (answer == base::UTF16ToUTF8(T(u"Zaplanuj"))) {
      auto task = std::move(*session.proposal);
      task.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      task.next_run = AssistantTaskNextRun(task, base::Time::Now());
      result.Set("scheduled", true);
      result.Set("next_run", base::UTF16ToUTF8(WhenText(task.next_run)));
      tasks_.push_back(std::move(task));
      ScheduleSave(); NotifyBar();
    } else if (answer == base::UTF16ToUTF8(T(u"Zmień ustawienia"))) {
      draft_ = std::move(*session.proposal);
      result.Set("scheduled", false);
      result.Set("detail", "The user opened the task editor to adjust and save the task.");
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
        if (self && self->draft_) { self->menu_ = Menu::kTaskEditor; self->BuildMenu(); } }, weak_factory_.GetWeakPtr()));
    } else {
      result.Set("scheduled", false);
      result.Set("detail", "Not scheduled. If the user asked to do it now, do it now.");
    }
    session.proposal.reset();
  } else if (tool == "add_search_engine") {
    const auto& args = *session.queue[session.index].GetDict().FindDict("args");
    std::string keyword = base::ToLowerASCII(*args.FindString("keyword"));
    std::string address = *args.FindString("url");
    if (const auto at = address.find("%s"); at != std::string::npos) address.replace(at, 2, "{query}");
    if (answer == base::UTF16ToUTF8(T(u"Nie dodawaj"))) {
      result.Set("added", false);
    } else {
      // Typed words are the user's own keyword.
      if (!std::ranges::contains(question->options, answer)) keyword = base::ToLowerASCII(std::string(base::TrimWhitespaceASCII(answer, base::TRIM_ALL)));
      const bool added = host_->shell()->UpsertSearchProvider({*args.FindString("name"), keyword, address});
      result.Set("added", added);
      result.Set("keyword", keyword);
      if (!added) result.Set("error", "The keyword is taken or invalid (a-z, 0-9, up to 16). Ask for another one.");
    }
  }
  Append(session, "Trace: " + base::WriteJson(base::DictValue().Set("tool", tool).Set("question", question->text).Set("answer", answer)).value_or("{}"));
  session.results.Append(base::DictValue().Set("tool", tool).Set("result", std::move(result)));
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
  if (session.profile != host_->AssistantProfileId()) { StopSession(session); return; }
  auto action = std::move(*session.confirmation); session.confirmation.reset();
  host_->shell()->SetAssistantConsent(nullptr, std::nullopt);
  const auto& tool = *action.FindString("tool");
  const bool debug = IsAssistantDebugTool(tool) || tool == "adblock_diagnose";
  const bool save_rules = tool == "adblock_add_rules";
  if (debug || save_rules) {
    session.status = allow ? ToolStatus(tool)
                           : save_rules ? T(u"Pominięto zapis reguł") : T(u"Pominięto debugowanie");
  } else {
    session.status = allow ? T(u"Klikam na stronie…") : T(u"Pominięto kliknięcie");
  }
  UpdatePageControl(); RenderBackground();
  if (!allow) {
    if (!pinned_) WatchPointer();
    // Declined rules leave nothing outlined on the page.
    if (save_rules)
      host_->AssistantTool("adblock_clear_preview", action.FindDict("args")->Clone(), session.permissions, base::DoNothing(),
                             false, session.background ? session.key : std::string());
    const char* detail = save_rules ? "The user declined saving these ad blocking rules. Do not retry it."
        : debug ? "The user declined this debugging action. Do not retry it."
                : "The user declined this click. Do not retry it; continue another way or ask.";
    session.results.Append(base::DictValue().Set("tool", tool).Set("result",
        base::DictValue().Set("skipped", true).Set("detail", detail)));
    ++session.index; RunNext(session); return;
  }
  session.tool_running = true;
  RenderConversation();
  if (!pinned_) WatchPointer();
  host_->AssistantTool(tool, action.FindDict("args")->Clone(), session.permissions,
      base::BindOnce(&AssistantPanel::ToolFinished, weak_factory_.GetWeakPtr(), session.key, session.epoch), true,
      session.background ? session.key : std::string());
}
bool AssistantPanel::HandleEscape() {
  if (sessions_.empty()) return false;
  auto& session = selected();
  if (session.confirmation && !session.background) { Consent(session, false); return true; }
  if (busy()) { Stop(); return true; }
  if (menu_ != Menu::kNone) { CloseMenu(); return true; }
  if (GetVisible() && !pinned_) { Hide(); return true; }
  return false;
}

// Scheduled tasks ----------------------------------------------------------
bool AssistantPanel::working() const {
  return std::ranges::any_of(sessions_, [](const auto& s) { return s->busy; });
}
void AssistantPanel::NotifyBar() { host_->AssistantStateChanged(); }
AssistantTask* AssistantPanel::FindTask(const std::string& id) {
  for (auto& task : tasks_) if (task.id == id) return &task;
  return nullptr;
}
void AssistantPanel::AddTaskForTesting(AssistantTask task) {
  if (task.next_run.is_null()) task.next_run = AssistantTaskNextRun(task, base::Time::Now());
  tasks_.push_back(std::move(task)); ScheduleSave(); NotifyBar();
}
bool AssistantPanel::SaveDraftForTesting(std::string instruction) {
  if (!draft_) return false;
  if (draft_instruction_) draft_instruction_->SetText(base::UTF8ToUTF16(instruction)); else draft_->instruction = std::move(instruction);
  return SaveDraft();
}
void AssistantPanel::BuildTasksMenu(views::View* body) {
  auto weak = weak_factory_.GetWeakPtr();
  const auto profile = host_->AssistantProfileId();
  const auto active = std::ranges::count_if(tasks_, [&](const auto& t) { return t.profile == profile && t.active; });
  body->AddChildView(CreateAssistantEyebrow(T(u"Zadania zaplanowane"), TF(u"{} aktywne", {base::NumberToString16(active)})));
  AssistantRow add; add.title = T(u"Nowe zadanie"); add.detail = T(u"albo napisz w rozmowie „codziennie…”");
  add.mark = {AssistantGlyph::kPlus, {}, SK_ColorTRANSPARENT};
  body->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(&AssistantPanel::OpenTaskEditor, weak, std::string())), std::move(add)));
  auto* list = ScrollList(body, 10000);
  SetSidebarPanelLayout(list, true, gfx::Insets::TLBR(4, 0, 0, 0), 6);
  size_t shown = 0;
  for (const auto& task : tasks_) {
    if (task.profile != profile) continue;
    AssistantTaskCard card;
    card.name = base::UTF8ToUTF16(task.name); card.instruction = base::UTF8ToUTF16(task.instruction);
    card.schedule = AssistantTaskSchedule(task); card.model = base::UTF8ToUTF16(task.model); card.active = task.active;
    std::vector<std::u16string> access;
    if (task.permissions.pages) access.push_back(T(u"Strony"));
    if (task.permissions.actions) access.push_back(T(u"Działania"));
    if (task.permissions.web) access.push_back(T(u"Internet"));
    card.access = access.empty() ? T(u"bez dostępu") : base::JoinString(access, u" · ");
    const bool running = std::ranges::any_of(sessions_, [&](const auto& s) { return s->task_id == task.id && s->busy; });
    std::u16string last = running ? T(u"Pracuje teraz")
        : task.last_run.is_null() ? T(u"Jeszcze nie uruchomione")
        : TF(task.last_state == 2 ? u"{} · czekało na Ciebie" : task.last_state == 3 ? u"{} · nie udało się" : u"{} · gotowe", {WhenText(task.last_run)});
    const auto next = task.active && !task.next_run.is_null() ? TF(u"następne {}", {WhenText(task.next_run)}) : task.active ? T(u"bez kolejnych uruchomień") : T(u"wstrzymane");
    card.status = last + u" · " + next;
    card.state = running ? 0 : task.active ? task.last_state : 4;
    const auto id = task.id;
    list->AddChildView(CreateAssistantTaskCard(std::move(card),
        Later(base::BindRepeating(&AssistantPanel::OpenTaskEditor, weak, id)),
        Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self, std::string id) {
          if (!self) return;
          if (auto* task = self->FindTask(id)) {
            task->active = !task->active;
            if (task->active) task->next_run = AssistantTaskNextRun(*task, base::Time::Now());
          }
          self->ScheduleSave(); self->BuildMenu(); self->NotifyBar();
        }, weak, id)),
        Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self, std::string id) { if (self) { self->RunTask(id); self->BuildMenu(); } }, weak, id)),
        Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self, std::string id) {
          if (!self) return;
          std::erase_if(self->tasks_, [&](const auto& t) { return t.id == id; });
          self->ScheduleSave(); self->BuildMenu(); self->NotifyBar();
        }, weak, id))));
    ++shown;
  }
  if (!shown) list->AddChildView(CreateAssistantNote(T(u"Nie ma zadań. Napisz w rozmowie na przykład „co piątek wpisz mi do OneNote nowe kody z loterii”.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::VH(8, 8));
  body->AddChildView(CreateAssistantNote(T(u"Zadania działają w tle, gdy Zitter jest otwarty, z modelem i uprawnieniami wybranymi przy zadaniu.")))
      ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 8, 4, 8));
  SetSidebarPanelLayout(body, true, gfx::Insets(6), 2, menu_scroll_);
}
void AssistantPanel::OpenTaskEditor(const std::string& id) {
  if (const auto* existing = id.empty() ? nullptr : FindTask(id)) draft_ = *existing;
  else {
    AssistantTask fresh;
    const auto& settings = host_->assistant_settings();
    fresh.profile = host_->AssistantProfileId();
    fresh.permissions = settings.defaults; fresh.permissions.confirm_clicks = false;
    const auto& session = selected();
    fresh.provider = session.provider; fresh.model = session.model;
    draft_ = std::move(fresh);
  }
  menu_ = Menu::kTaskEditor; model_query_.clear();
  BuildMenu();
  if (draft_instruction_) draft_instruction_->RequestFocus();
}
void AssistantPanel::CaptureDraft() {
  if (!draft_) return;
  if (draft_instruction_) draft_->instruction = base::UTF16ToUTF8(base::TrimWhitespace(draft_instruction_->GetText(), base::TRIM_ALL));
  if (draft_time_) {
    if (const auto minute = ParseClock(base::UTF16ToUTF8(draft_time_->GetText()))) draft_->minute = *minute;
  }
}
bool AssistantPanel::SaveDraft() {
  CaptureDraft();
  if (!draft_ || draft_->instruction.empty() || (draft_->kind == 1 && !draft_->days)) return false;
  auto task = std::move(*draft_); draft_.reset();
  if (task.name.empty()) {
    // The first words of the instruction name the task.
    const auto sentence = task.instruction.substr(0, task.instruction.find_first_of(".!?\n"));
    const auto words = base::SplitString(sentence, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    std::vector<std::string> first(words.begin(), words.begin() + std::min<size_t>(words.size(), 5));
    task.name = base::JoinString(first, " ") + (words.size() > 5 ? "…" : "");
    if (task.name.empty()) task.name = task.instruction.substr(0, 60);
  }
  if (task.id.empty()) task.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto* existing = FindTask(task.id);
  if (!existing || task.kind != existing->kind || task.days != existing->days || task.minute != existing->minute)
    task.next_run = task.active ? AssistantTaskNextRun(task, base::Time::Now()) : base::Time();
  else
    task.next_run = existing->next_run;
  if (existing) *existing = std::move(task); else tasks_.push_back(std::move(task));
  ScheduleSave(); NotifyBar();
  menu_ = Menu::kTasks; BuildMenu();
  return true;
}
void AssistantPanel::BuildTaskEditor(views::View* body) {
  if (!draft_) { menu_ = Menu::kTasks; BuildTasksMenu(body); return; }
  auto weak = weak_factory_.GetWeakPtr();
  auto& d = *draft_;
  auto rebuild = [](base::WeakPtr<AssistantPanel> self, base::RepeatingCallback<void(AssistantTask&)> change) {
    if (!self || !self->draft_) return;
    self->CaptureDraft(); change.Run(*self->draft_); self->BuildMenu();
  };
  auto* head = body->AddChildView(std::make_unique<views::View>());
  head->AddChildView(CreateAssistantIcon(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
    if (!self) return; self->draft_.reset(); self->menu_ = Menu::kTasks; self->BuildMenu(); }, weak)), AssistantGlyph::kBack, T(u"Wróć do zadań")));
  auto* title = head->AddChildView(CreateAssistantEyebrow(FindTask(d.id) ? T(u"Edytuj zadanie") : T(u"Nowe zadanie")));
  SetSidebarPanelLayout(head, false, gfx::Insets(), 6, title, true);
  auto* list = ScrollList(body, 10000);
  SetSidebarPanelLayout(list, true, gfx::Insets::TLBR(4, 4, 4, 4), 8);
  list->AddChildView(CreateAssistantEyebrow(T(u"Co ma zrobić")));
  auto* frame = list->AddChildView(CreateAssistantSurface(1));
  SetSidebarPanelLayout(frame, true, gfx::Insets::VH(8, 10), 0);
  draft_instruction_ = frame->AddChildView(CreateAssistantPromptArea(base::BindRepeating([](const ui::KeyEvent&) { return false; })));
  draft_instruction_->SetPlaceholderText(T(u"Na przykład: co piątek znajdź nowe kody z loterii i dopisz je w OneNote"));
  draft_instruction_->SetText(base::UTF8ToUTF16(d.instruction));
  draft_instruction_->GetViewAccessibility().SetName(T(u"Instrukcja zadania"));
  // Four lines, as in the design; longer instructions scroll inside.
  draft_instruction_->SetPreferredSize(gfx::Size(0, 4 * 19 + 4));
  list->AddChildView(CreateAssistantNote(T(u"Pisz jak do asystenta w rozmowie. Nazwa powstanie z pierwszego zdania.")));
  list->AddChildView(CreateAssistantEyebrow(T(u"Kiedy")));
  auto* kinds = list->AddChildView(CreateAssistantFlow(4));
  const std::array<std::u16string, 4> kind_names = {T(u"Codziennie"), T(u"W dni"), T(u"Co godzinę"), T(u"Raz")};
  for (int k = 0; k < 4; ++k)
    kinds->AddChildView(CreateAssistantPill(Later(base::BindRepeating(rebuild, weak, base::BindRepeating([](int k, AssistantTask& t) { t.kind = k; }, k))),
        kind_names.at(k), d.kind == k ? 0 : 2));
  if (d.kind == 1) {
    auto* days = list->AddChildView(CreateAssistantFlow(4));
    const std::array<std::u16string, 7> day_names = {T(u"Pn"), T(u"Wt"), T(u"Śr"), T(u"Cz"), T(u"Pt"), T(u"So"), T(u"Nd")};
    for (int i = 0; i < 7; ++i)
      days->AddChildView(CreateAssistantPill(Later(base::BindRepeating(rebuild, weak, base::BindRepeating([](int i, AssistantTask& t) { t.days ^= 1 << i; }, i))),
          day_names.at(i), d.days & (1 << i) ? 0 : 2));
  }
  if (d.kind != 2) {
    auto* time_row = list->AddChildView(std::make_unique<views::View>());
    SetSidebarPanelLayout(time_row, false, gfx::Insets(), 8);
    auto field = std::make_unique<views::Textfield>();
    field->SetText(base::UTF8ToUTF16(base::StringPrintf("%02d:%02d", d.minute / 60, d.minute % 60)));
    field->GetViewAccessibility().SetName(T(u"Godzina"));
    field->SetDefaultWidthInChars(6);
    draft_time_ = time_row->AddChildView(std::move(field));
  }
  const auto next = AssistantTaskNextRun(d, base::Time::Now());
  list->AddChildView(CreateAssistantNote(AssistantTaskSchedule(d) + u" · " + (next.is_null() ? T(u"wybierz co najmniej jeden dzień") : TF(u"następne {}", {WhenText(next)}))));
  AssistantRow catch_up;
  catch_up.title = T(u"Nadrabiaj po uruchomieniu");
  catch_up.detail = T(u"Uruchom raz, jeśli termin minął przy zamkniętym Zitterze.");
  catch_up.toggle = d.catch_up;
  list->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(rebuild, weak, base::BindRepeating([](AssistantTask& task) {
    task.catch_up = !task.catch_up;
  }))), std::move(catch_up)));
  list->AddChildView(CreateAssistantEyebrow(T(u"Model")));
  auto* model_row = list->AddChildView(CreateAssistantFlow(4));
  model_row->AddChildView(CreateAssistantChip(Later(base::BindRepeating(rebuild, weak, base::BindRepeating([](std::vector<AssistantModel> models, AssistantTask& t) {
    // Each click moves to the next model from Settings.
    if (models.empty()) return;
    size_t at = 0;
    for (size_t i = 0; i < models.size(); ++i) if (models[i].provider == t.provider && models[i].id == t.model) at = i + 1;
    t.provider = models[at % models.size()].provider; t.model = models[at % models.size()].id;
  }, host_->assistant_settings().PanelModels()))), AssistantGlyph::kSpark, base::UTF8ToUTF16(d.model.empty() ? "—" : d.model)));
  list->AddChildView(CreateAssistantEyebrow(T(u"Uprawnienia zadania")));
  const auto& defaults = host_->assistant_settings().defaults;
  const std::tuple<int, std::u16string, std::u16string, bool, bool> grants[] = {
      {0, T(u"Karty i przypięte"), T(u"Lista otwartych kart"), d.permissions.tabs, defaults.tabs},
      {1, T(u"Treść stron"), T(u"Czytanie stron, które otworzy"), d.permissions.pages, defaults.pages},
      {2, T(u"Działania w przeglądarce"), T(u"Klikanie, pisanie, otwieranie stron w tle"), d.permissions.actions, defaults.actions},
      {3, T(u"Historia"), T(u"Wyszukiwanie w historii"), d.permissions.history, defaults.history},
      {4, T(u"Internet"), T(u"Wyszukiwanie publicznych stron"), d.permissions.web, defaults.web},
      {5, T(u"Pytaj o każde kliknięcie"), T(u"Zadanie zatrzyma się i poczeka na Ciebie"), d.permissions.confirm_clicks, true}};
  for (const auto& [index, label, help, on, allowed] : grants) {
    AssistantRow row; row.title = label; row.detail = allowed ? help : T(u"Wyłączone w Ustawieniach"); row.toggle = on && allowed; row.disabled = !allowed;
    list->AddChildView(CreateAssistantMenuRow(Later(base::BindRepeating(rebuild, weak, base::BindRepeating([](int index, AssistantTask& t) {
      auto& p = t.permissions;
      const std::array<bool*, 6> flags = {&p.tabs, &p.pages, &p.actions, &p.history, &p.web, &p.confirm_clicks};
      *flags.at(index) = !*flags.at(index);
    }, index))), std::move(row)));
  }
  list->AddChildView(CreateAssistantNote(T(u"Zadanie pracuje w niewidocznej karcie z Twoimi logowaniami. Zakupy, wysyłki i usuwanie zawsze czekają na Twoje „tak”.")));
  auto* foot = body->AddChildView(std::make_unique<views::View>());
  auto* spacer = foot->AddChildView(std::make_unique<views::View>());
  if (FindTask(d.id)) foot->AddChildView(CreateAssistantPill(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
    if (!self || !self->draft_) return;
    const auto id = self->draft_->id; self->draft_.reset();
    std::erase_if(self->tasks_, [&](const auto& t) { return t.id == id; });
    self->ScheduleSave(); self->NotifyBar(); self->menu_ = Menu::kTasks; self->BuildMenu(); }, weak)), T(u"Usuń"), 3));
  foot->AddChildView(CreateAssistantPill(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
    if (!self) return; self->draft_.reset(); self->menu_ = Menu::kTasks; self->BuildMenu(); }, weak)), T(u"Anuluj"), 1));
  foot->AddChildView(CreateAssistantPill(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self) { if (self) self->SaveDraft(); }, weak)),
      T(u"Zapisz zadanie"), 0));
  SetSidebarPanelLayout(foot, false, gfx::Insets::TLBR(6, 4, 2, 4), 6, spacer, true);
  SetSidebarPanelLayout(body, true, gfx::Insets(6), 4, menu_scroll_);
}
void AssistantPanel::CheckTasks(base::Time now) {
  if (!loaded_ || !host_->assistant_settings().enabled) return;
  std::vector<std::string> due;
  for (const auto& task : tasks_)
    if (task.active && !task.next_run.is_null() && task.next_run <= now && task.profile == host_->AssistantProfileId()) due.push_back(task.id);
  for (const auto& id : due) RunTask(id);
}
void AssistantPanel::RunTask(const std::string& id) {
  auto* task = FindTask(id);
  const auto& settings = host_->assistant_settings();
  if (!task || !settings.enabled || task->profile != host_->AssistantProfileId()) return;
  if (std::ranges::any_of(sessions_, [&](const auto& s) { return s->task_id == id && s->busy; })) return;
  auto owned = std::make_unique<Session>();
  auto& session = *owned;
  session.key = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  session.profile = task->profile; session.task_id = id; session.updated = base::Time::Now();
  session.provider = task->provider; session.model = task->model;
  const auto models = settings.PanelModels();
  if (!std::ranges::any_of(models, [&](const auto& m) { return m.provider == session.provider && m.id == session.model; })) {
    for (const auto& model : models) if (model.provider + "/" + model.id == settings.default_model) { session.provider = model.provider; session.model = model.id; }
    if (session.provider.empty() && !models.empty()) { session.provider = models.front().provider; session.model = models.front().id; }
  }
  // Never wider than today's defaults; page actions only in an invisible tab.
  const auto& d = settings.defaults;
  session.permissions = task->permissions;
  session.permissions.tabs &= d.tabs; session.permissions.pages &= d.pages; session.permissions.actions &= d.actions;
  session.permissions.history &= d.history; session.permissions.web &= d.web;
  const bool background = session.permissions.actions && settings.background;
  if (!background) session.permissions.actions = false;
  session.confirm_background = task->permissions.confirm_clicks;
  session.title = base::UTF8ToUTF16(task->name);
  task->last_run = base::Time::Now();
  task->next_run = AssistantTaskNextRun(*task, task->last_run);
  sessions_.push_back(std::move(owned));
  Message banner; banner.kind = 3; banner.state = 7; banner.text = id;
  session.messages.push_back(std::move(banner));
  if (!settings.BackendEnabled(session.provider)) {
    Append(session, base::UTF16ToUTF8(T(u"Dodaj model w Ustawieniach → Asystent AI.")));
    TaskFinished(session, false);
    return;
  }
  if (background && !host_->AssistantStartBackground(session.key, {}, GURL())) { Append(session, base::UTF16ToUTF8(T(u"Nie udało się otworzyć karty w tle."))); TaskFinished(session, false); return; }
  session.background = background;
  Append(session, "You: " + task->instruction);
  session.request = base::DictValue().Set("id", session.id)
      .Set("prompt", task->instruction + "\n\n(Scheduled task \"" + task->name + "\", started automatically; the user is not watching. Do the work in the background and report the result in your answer.)")
      .Set("research", false).Set("background", background).Set("concurrency", settings.concurrency)
      .Set("date", base::DictValue().Set("unix_ms", static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()))
           .Set("utc_offset_minutes", TimelineUtcOffsetMinutes(base::Time::Now())))
      .Set("config", base::DictValue().Set("provider", session.provider).Set("model", session.model))
      .Set("permissions", session.permissions.ToValue()).Set("context", host_->AssistantContext(session.permissions))
      .Set("history", base::ListValue());
  StartTurn(session);
  ScheduleSave(); NotifyBar(); RenderBackground();
  if (menu_ == Menu::kTasks) BuildMenu();
}
void AssistantPanel::TaskFinished(Session& session, bool ok) {
  if (auto* task = FindTask(session.task_id)) task->last_state = !ok ? 3 : session.needed_user ? 2 : 1;
  if (!GetVisible() || &session != &selected()) ++unread_;
  ScheduleSave(); NotifyBar();
  if (menu_ == Menu::kTasks) BuildMenu();
}

// Background --------------------------------------------------------------
void AssistantPanel::MoveToBackground() {
  auto& session = selected();
  if (!session.busy || session.background || !host_->assistant_settings().background || !session.permissions.actions) return;
  // Without page actions yet, the task gets a fresh invisible tab; otherwise
  // the tab it works in moves (or, as the space's only tab, is copied).
  const bool touched = !session.target.empty();
  if (!host_->AssistantStartBackground(session.key, touched ? session.target : std::string(), GURL())) {
    Append(session, base::UTF16ToUTF8(T(u"Nie udało się otworzyć karty w tle.")));
    return;
  }
  session.background = true; session.page_control = false;
  host_->shell()->SetAssistantConsent(nullptr, std::nullopt);
  Append(session, base::UTF16ToUTF8(touched ? T(u"Kończę w tle. Ta karta jest znowu Twoja.") : T(u"Kończę w tle, w osobnej niewidocznej karcie.")));
  UpdatePageControl(); RenderBackground(); UpdateControls();
}
void AssistantPanel::TakeOver(std::string key) {
  for (auto& s : sessions_) if (s->key == key) {
    if (s->busy) { StopSession(*s); Append(*s, base::UTF16ToUTF8(T(u"Przejęto kartę. Zadanie zatrzymane."))); }
    host_->AssistantTakeOver(key);
    s->background = false;
    RenderBackground(); UpdatePageControl();
    return;
  }
}
void AssistantPanel::Dismiss(std::string key) {
  for (auto& s : sessions_) if (s->key == key) { if (s->busy) StopSession(*s); s->background = false; }
  host_->AssistantDiscardBackground(key);
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
    if (!s->background || (!s->busy && !host_->AssistantHasBackground(s->key))) continue;
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
    const bool page = host_->AssistantHasBackground(s->key);
    const int state = s->busy ? 0 : s->finished ? 2 : 3;
    const auto status = s->busy ? s->status : s->finished ? T(u"Gotowe · przejmij kartę, żeby zobaczyć") : T(u"Zatrzymane");
    background_->AddChildView(CreateAssistantTaskRow(mark, title, status, state,
        page ? later(weak, s->key, 3) : base::RepeatingClosure(), s->busy ? later(weak, s->key, 5) : later(weak, s->key, 4)));
  }
  background_->SetVisible(shown > 0);
  host_->shell()->SetAssistantAttention(std::ranges::any_of(sessions_, [](const auto& s) { return s->background && s->confirmation.has_value(); }));
  InvalidateLayout(); scene_->Schedule();
}

// Page control on the visible tab: spectral edge, capsule, consent bubble.
void AssistantPanel::UpdatePageControl() {
  auto* shell = host_->shell();
  const auto& session = selected();
  const bool active = session.busy && !session.background && session.page_control;
  shell->SetAssistantActivity(active);
  if (!active) { shell->SetAssistantCapsule(nullptr); shell->SetAssistantConsent(nullptr, std::nullopt); return; }
  const bool movable = host_->assistant_settings().background && session.permissions.actions;
  shell->SetAssistantCapsule(CreateAssistantCapsule(session.confirmation ? T(u"Czeka na Twoją zgodę") : T(u"Asystent działa"),
      movable ? base::BindRepeating([](base::WeakPtr<AssistantPanel> self) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<AssistantPanel> self) {
          if (self) self->MoveToBackground(); }, self));
      }, weak_factory_.GetWeakPtr()) : base::RepeatingClosure(), !gfx::Animation::PrefersReducedMotion()));
  if (!session.confirmation) shell->SetAssistantConsent(nullptr, std::nullopt);
}

// Rendering ---------------------------------------------------------------
void AssistantPanel::RenderConversation() {
  live_answer_ = nullptr;
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
            const auto context=self->host_->AssistantContext(list);
            if(const auto* tabs=context.FindList("tabs"))for(const auto& entry:*tabs)if(entry.GetDict().FindBool("current").value_or(false)){
              const auto title=base::UTF8ToUTF16(*entry.GetDict().FindString("title"));const GURL url(*entry.GetDict().FindString("url"));
              self->InsertChip({"tab","@current",title.empty()?base::UTF8ToUTF16(url.host()):title,kPageColors[base::PersistentHash(url.host())%kPageColors.size()]},false);
            }
          } else self->prompt_->SetText(prompt+u" @current");
        } else if(!tool.empty())self->InsertChip({tool,"",T(u"Deep research"),0},false);
        self->UpdateControls();self->prompt_->RequestFocus();
      },base::Unretained(this),std::move(prompt),std::move(tool)),glyph,std::move(title),std::move(detail)));
    };
    // The page quick actions live here, where the conversation starts.
    if(host_->AssistantPageActionsAvailable()){
      auto action=[&](AssistantGlyph glyph,std::u16string title,std::u16string detail,int command){
        conversation_->AddChildView(CreateAssistantSuggestion(Later(base::BindRepeating(
            [](base::WeakPtr<AssistantPanel> self,int command){if(self)self->host_->RunAssistantPageAction(command);},
            weak_factory_.GetWeakPtr(),command)),glyph,std::move(title),std::move(detail)));
      };
      action(AssistantGlyph::kPage,T(u"Podsumuj stronę"),T(u"Najważniejsze informacje"),0);
      action(AssistantGlyph::kCheck,T(u"Sprawdź fakty"),T(u"Z odnośnikami do źródeł"),3);
      action(AssistantGlyph::kLayers,T(u"Wyciągnij dane"),T(u"Tabela i porównanie"),4);
      action(AssistantGlyph::kSearch,T(u"Dodaj wyszukiwarkę"),T(u"Szukaj na tej stronie z paska"),1);
      action(AssistantGlyph::kShield,T(u"Zablokuj reklamy"),T(u"Reguła dla tej witryny"),2);
    }
    suggestion(AssistantGlyph::kGlobe,T(u"Znajdź coś w internecie"),T(u"Odpowiedzi z konkretnych źródeł"),T(u"Pomóż mi znaleźć informacje o "),"");
    suggestion(AssistantGlyph::kSpark,T(u"Zbadaj temat głębiej"),T(u"Wstawia narzędzie Deep research"),T(u"Zbadaj dokładnie i porównaj "),"research");
  } else for(size_t i=0;i<messages.size();++i) {
    const auto& message=messages[i];
    if(message.kind==4)continue;
    if(message.kind==3&&message.state==7){
      // A scheduled task's run: which task, and a way to change it.
      auto* card=conversation_->AddChildView(CreateAssistantSurface(1));
      SetSidebarPanelLayout(card,true,gfx::Insets(10),4);
      const auto* task=FindTask(message.text);
      card->AddChildView(CreateAssistantActivity(task?TF(u"Uruchomione automatycznie · {}",{base::UTF8ToUTF16(task->name)}):T(u"Uruchomione automatycznie"),1));
      if(task){
        card->AddChildView(CreateAssistantNote(AssistantTaskSchedule(*task)+u" · "+base::UTF8ToUTF16(task->model)));
        card->AddChildView(CreateAssistantLink(Later(base::BindRepeating(&AssistantPanel::OpenTaskEditor,weak_factory_.GetWeakPtr(),task->id)),T(u"Edytuj zadanie"),AssistantGlyph::kCalendar));
      }
      continue;
    }
    if(message.kind==5) {
      // The model's question: one click answers; several may be picked when allowed.
      auto* card=conversation_->AddChildView(CreateAssistantSurface(3));
      SetSidebarPanelLayout(card,true,gfx::Insets(12),6);
      const bool open=message.answer.empty()&&session.asking;
      card->AddChildView(CreateAssistantActivity(open?T(u"Asystent pyta"):T(u"Asystent zapytał"),open?3:1));
      card->AddChildView(CreateAssistantMarkdown(base::UTF8ToUTF16(message.text),base::BindRepeating([](const GURL&){})));
      const auto weak=weak_factory_.GetWeakPtr();
      if(message.state==2){
        // A proposal: its answers are buttons, the first one primary.
        if(open){
          auto* flow=card->AddChildView(CreateAssistantFlow(6));
          for(size_t o=0;o<message.options.size();++o)
            flow->AddChildView(CreateAssistantPill(Later(base::BindRepeating([](base::WeakPtr<AssistantPanel> self,std::string key,size_t o){
              if(!self)return;
              for(auto& s:self->sessions_)if(s->key==key&&s->asking){auto* q=self->PendingQuestion(*s);if(q&&o<q->options.size())self->AnswerQuestion(*s,q->options[o]);}
            },weak,session.key,o)),base::UTF8ToUTF16(message.options[o]),o==0?0:o==1?1:2));
        }else if(!message.answer.empty()&&message.answer!="—"){
          card->AddChildView(CreateAssistantActivity(base::UTF8ToUTF16(message.answer),1));
        }
        continue;
      }
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
        if(self&&url.SchemeIsHTTPOrHTTPS())self->host_->NavigateFromBar(url,true);},weak_factory_.GetWeakPtr())));
    } else if(!message.text.empty()){card->AddChildView(CreateAssistantPlainText(base::UTF8ToUTF16(message.text)));}
  }
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
  InvalidateLayout();scene_->Schedule();
}
void AssistantPanel::UpdateControls() {
  NotifyBar();
  if (sessions_.empty() || !send_) return;
  const auto& session = selected();
  const auto& settings = host_->assistant_settings();
  const bool idle = !session.busy;
  send_->SetVisible(idle); stop_->SetVisible(!idle); send_->SetEnabled(idle && !session.provider.empty());
  stop_->SetEnabled(!idle);
  prompt_->SetEnabled(idle || session.asking);
  // A disabled text field can retain its caret timer until focus changes.
  prompt_->SetCursorEnabled(idle || session.asking);
  if (session.asking) { send_->SetVisible(true); send_->SetEnabled(true); stop_->SetVisible(false); }
  prompt_->SetPlaceholderText(session.asking ? T(u"Wybierz odpowiedź albo napisz własną…") : T(u"Zapytaj Zittera… @ wstawia kartę albo narzędzie"));
  const bool research = std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "research"; });
  const bool background = std::ranges::any_of(chips_, [](const auto& c) { return c.kind == "background"; });
  UpdateAssistantButton(research_chip_, T(u"Deep research"), research);
  const bool movable = session.busy && !session.background;
  UpdateAssistantButton(background_chip_, movable ? T(u"Dokończ w tle") : T(u"W tle"), background || session.background);
  research_chip_->SetEnabled(idle && session.permissions.web);
  background_chip_->SetVisible(true);
  background_chip_->SetEnabled(settings.background && (idle || movable));
  background_chip_->SetTooltipText(!settings.background ? T(u"Włącz pracę w tle w Ustawieniach → Asystent AI") :
      !session.permissions.actions ? T(u"Nadaj tej sesji dostęp do działań, aby uruchomić zadanie w tle") :
      movable ? T(u"Dokończ w tle") : T(u"W tle · Alt+Enter"));
  UpdateAssistantButton(model_chip_, session.model.empty() ? T(u"Wybierz model") : base::UTF8ToUTF16(session.model), menu_ == Menu::kModel);
  if (!session.model.empty()) {
    const auto label = TF(u"Model: {}. Zmień model", {base::UTF8ToUTF16(session.model)});
    model_chip_->SetTooltipText(label);
    model_chip_->GetViewAccessibility().SetName(label);
  }
  const int shared = static_cast<int>(session.permissions.tabs) + session.permissions.pages + session.permissions.actions + session.permissions.history + session.permissions.web;
  UpdateAssistantButton(access_action_, TF(u"Dostęp · {}", {base::NumberToString16(shared)}),
                        menu_ == Menu::kAccess);
  access_action_->GetViewAccessibility().SetName(T(u"Dostęp tej sesji"));
  InvalidateLayout();
}

void AssistantPanel::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  resize_handle_->SetBounds(0,16,8,std::max(0,height()-32));
  if (!menu_view_) return;
  const int inner = std::max(0, width() - 32);
  int w = inner, x = 16, top = 0, bottom = 0;
  if (menu_ == Menu::kSessions || menu_ == Menu::kTasks || menu_ == Menu::kTaskEditor || menu_ == Menu::kSettings) {
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
