// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/assistant_visuals.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {
constexpr std::array<const char*, 4> kProviders = {"openai", "codex", "claude", "ollama"};
constexpr std::array<const char*, 4> kModels = {"gpt-5.4", "gpt-5.4", "sonnet", "qwen3:8b"};
std::unique_ptr<ui::SimpleComboboxModel> Combo(std::vector<std::u16string> labels) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  for (auto& label : labels) items.emplace_back(std::move(label));
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}
}
base::DictValue AssistantPermissions::ToValue() const {
  return base::DictValue().Set("tabs", tabs).Set("pages", pages).Set("actions", actions).Set("history", history).Set("web", web).Set("confirm_clicks", confirm_clicks);
}

AssistantPanel::AssistantPanel(WorkspaceWindow* window) : window_(window) {
  GetViewAccessibility().SetName(T(u"Asystent AI"));
  SetBackground(CreateAssistantBackground());
  SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
  scene_ = CreatePanelScene(this,true);
  auto* header=AddChildView(std::make_unique<views::View>());
  auto* identity=header->AddChildView(CreateAssistantIdentity());
  header->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::NewSession,base::Unretained(this)),AssistantGlyph::kPlus,T(u"Nowa sesja")));
  header->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::ToggleDetails,base::Unretained(this),false),AssistantGlyph::kHistory,T(u"Sesje i model")));
  pin_action_=header->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){self->SetPinned(!self->pinned());},base::Unretained(this)),AssistantGlyph::kPin,T(u"Przypnij")));
  header->AddChildView(CreateAssistantIcon(base::BindRepeating([](AssistantPanel* self){self->Stop();self->SetVisible(false);self->window_->shell()->LayoutAssistant(false);},base::Unretained(this)),AssistantGlyph::kClose,T(u"Zamknij")));
  SetSidebarPanelLayout(header,false,gfx::Insets(),4,identity,true);

  configuration_=AddChildView(CreateAssistantSurface());
  SetSidebarPanelLayout(configuration_,true,gfx::Insets(12),8);
  auto* caption=configuration_->AddChildView(scene_->MakeText(T(u"Twoja sesja"),false,false,0));
  scene_->SetTextStyle(caption,6,PanelScene::Tone::kMuted);
  sessions_control_=configuration_->AddChildView(std::make_unique<views::Combobox>(Combo({T(u"Sesja 1")})));
  sessions_control_->GetViewAccessibility().SetName(T(u"Sesja asystenta"));
  sessions_control_->SetCallback(base::BindRepeating(&AssistantPanel::SwitchSession,base::Unretained(this)));
  provider_=configuration_->AddChildView(std::make_unique<views::Combobox>(Combo({u"OpenAI API",u"Codex SDK",u"Claude CLI",u"Ollama"})));
  provider_->GetViewAccessibility().SetName(T(u"Backend AI"));
  provider_->SetCallback(base::BindRepeating([](AssistantPanel* self){self->model_->SetText(base::UTF8ToUTF16(kModels[self->provider_->GetSelectedIndex().value_or(0)]));self->UpdateControls();},base::Unretained(this)));
  model_=AddEditor(configuration_);model_->SetText(base::UTF8ToUTF16(kModels[0]));
  model_->SetPlaceholderText(T(u"ID modelu"));model_->GetViewAccessibility().SetName(T(u"ID modelu dla tej sesji"));
  model_->SetCallbacks(base::BindRepeating(&AssistantPanel::UpdateControls,base::Unretained(this)),{});
  auto* utilities=configuration_->AddChildView(scene_->MakeActions(8));
  auto* copy=scene_->AddAction(utilities,base::BindRepeating([](AssistantPanel* self){ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(base::UTF8ToUTF16(self->sessions_[self->selected_].transcript));},base::Unretained(this)),T(u"Kopiuj rozmowę"));
  auto* clear=scene_->AddAction(utilities,base::BindRepeating([](AssistantPanel* self){
    self->Stop();if(self->process_.IsValid()){self->process_.Terminate(0,false);self->process_.Close();}
#if BUILDFLAG(IS_WIN)
    self->process_job_.Close();
#endif
    self->sessions_.clear();self->NewSession();
  },base::Unretained(this)),T(u"Wyczyść rozmowy"));
  scene_->SetActionKind(copy,3);scene_->SetActionKind(clear,3);
  configuration_->SetVisible(false);

  PanelViewport* permissions_viewport=nullptr;
  permissions_view_=AddChildView(CreatePanelViewport(&permissions_viewport,scene_->ViewportChangedCallback()));
  permissions_viewport->SetHeightRange(180,180);
  auto permission_body=CreateAssistantSurface();
  SetSidebarPanelLayout(permission_body.get(),true,gfx::Insets(12),8);
  auto check=[&](std::u16string label,bool checked){auto* box=permission_body->AddChildView(std::make_unique<views::Checkbox>(std::move(label),base::BindRepeating(&AssistantPanel::UpdateControls,base::Unretained(this))));box->SetChecked(checked);return box;};
  tabs_=check(T(u"Otwarte i przypięte karty tego profilu"),false);
  pages_=check(T(u"Czytanie treści i elementów strony"),false);
  actions_=check(T(u"Klikanie, pisanie i zarządzanie kartami"),false);
  history_=check(T(u"Przeszukiwanie historii tego profilu"),false);
  web_=check(T(u"Wyszukiwanie i czytanie publicznych stron"),true);
  confirm_clicks_=check(T(u"Potwierdzaj każde kliknięcie na stronie"),true);
  auto* privacy=permission_body->AddChildView(scene_->MakeText(T(u"Pola wrażliwe i ciasteczka są wykluczone. Treść strony może zawierać dane osobowe. Nic nie wysyłamy przed kliknięciem Wyślij."),true,false,0));
  scene_->SetTextStyle(privacy,6,PanelScene::Tone::kMuted);
  permissions_viewport->SetPanelContents(std::move(permission_body));permissions_view_->SetVisible(false);
  research_=AddChildView(std::make_unique<views::Checkbox>(T(u"Deep research · równoległe źródła")));research_->SetVisible(false);

  PanelViewport* transcript_viewport=nullptr;
  auto* viewport=AddChildView(CreatePanelViewport(&transcript_viewport,scene_->ViewportChangedCallback()));viewport_=transcript_viewport;
  auto content=std::make_unique<views::View>();conversation_=content.get();
  content->SetLayoutManager(CreateSidebarColumnLayout(8));viewport_->SetPanelContents(std::move(content));

  auto* choices=AddChildView(std::make_unique<views::View>());
  model_chip_=choices->AddChildView(CreateAssistantChip(base::BindRepeating(&AssistantPanel::ToggleDetails,base::Unretained(this),false),AssistantGlyph::kSpark,base::UTF8ToUTF16(kModels[0])));
  access_chip_=choices->AddChildView(CreateAssistantChip(base::BindRepeating(&AssistantPanel::ToggleDetails,base::Unretained(this),true),AssistantGlyph::kShield,T(u"Dostęp")));
  SetSidebarPanelLayout(choices,false,gfx::Insets(),4,model_chip_,true);

  mentions_=AddChildView(std::make_unique<views::Combobox>(Combo({T(u"Wybierz otwartą lub przypiętą kartę")})));
  mentions_->GetViewAccessibility().SetName(T(u"Wskaż otwartą lub przypiętą kartę"));mentions_->SetVisible(false);
  mentions_->SetCallback(base::BindRepeating([](AssistantPanel* self){const auto index=self->mentions_->GetSelectedIndex().value_or(0);if(index<self->references_.size()){
    auto text=std::u16string(self->prompt_->GetText());if(text.ends_with(u"@"))text.pop_back();self->prompt_->SetText(text+u" "+base::UTF8ToUTF16(self->references_[index]));
    self->mentions_->SetVisible(false);self->prompt_->RequestFocus();
  }},base::Unretained(this)));

  confirm_=AddChildView(scene_->MakeActions(6));
  scene_->AddAction(confirm_,base::BindRepeating([](AssistantPanel* self){
    if(!self->confirmation_||!self->busy_)return;
    if(self->sessions_[self->selected_].profile!=self->window_->AssistantProfileId()){self->Stop();return;}
    auto action=std::move(*self->confirmation_);self->confirmation_.reset();self->confirm_->SetVisible(false);
    auto* args=action.FindDict("args");args->Set("confirmed",true);
    self->window_->AssistantTool(*action.FindString("tool"),args->Clone(),self->sessions_[self->selected_].permissions,
      base::BindOnce(&AssistantPanel::ToolFinished,self->weak_factory_.GetWeakPtr()),true);
  },base::Unretained(this)),T(u"Zezwól raz"));confirm_->SetVisible(false);

  auto* composer=AddChildView(CreateAssistantSurface(1));
  SetSidebarPanelLayout(composer,true,gfx::Insets(12),12);
  prompt_=AddEditor(composer);prompt_->SetPlaceholderText(T(u"Zapytaj Zittera…"));
  prompt_->SetPresentation(14,3,false,SK_ColorTRANSPARENT);
  prompt_->GetViewAccessibility().SetName(T(u"Wiadomość do asystenta"));
  prompt_->SetCallbacks(base::BindRepeating([](AssistantPanel* self){if(self->prompt_->GetText().ends_with(u"@"))self->PickReference();},base::Unretained(this)),
    base::BindRepeating([](AssistantPanel* self,const ui::KeyEvent& key){
      if(key.type()==ui::EventType::kKeyPressed&&key.key_code()==ui::VKEY_ESCAPE&&self->busy_){self->Stop();return true;}
      if(key.type()==ui::EventType::kKeyPressed&&key.key_code()==ui::VKEY_RETURN&&!key.IsShiftDown()){self->Send();return true;}return false;
    },base::Unretained(this)));
  auto* tools=composer->AddChildView(std::make_unique<views::View>());
  tools->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::PickReference,base::Unretained(this)),AssistantGlyph::kAt,T(u"@ Karty")));
  mode_chip_=tools->AddChildView(CreateAssistantChip(base::BindRepeating([](AssistantPanel* self){self->research_->SetChecked(!self->research_->GetChecked());self->UpdateControls();},base::Unretained(this)),AssistantGlyph::kSearch,T(u"Rozmowa")));
  auto* spacer=tools->AddChildView(std::make_unique<views::View>());
  send_=tools->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::Send,base::Unretained(this)),AssistantGlyph::kArrow,T(u"Wyślij"),true));
  stop_=tools->AddChildView(CreateAssistantIcon(base::BindRepeating(&AssistantPanel::Stop,base::Unretained(this)),AssistantGlyph::kStop,T(u"Stop"),true));
  SetSidebarPanelLayout(tools,false,gfx::Insets(),4,spacer,true);
  SetSidebarPanelLayout(this,true,gfx::Insets(16),10,viewport);
  NewSession();SetVisible(false);
}

AssistantPanel::~AssistantPanel() {
  loader_.reset(); startup_timer_.Stop();
  if (process_.IsValid()) process_.Terminate(0, false);
  // Scene nodes must be released before the scene registry.
  RemoveAllChildViews();
}
void AssistantPanel::Show() { SetVisible(true); window_->shell()->LayoutAssistant(pinned_); prompt_->RequestFocus(); }
void AssistantPanel::PickReference() {
  if (!tabs_->GetChecked()) { Append("Enable tab discovery under Access before sending the first message, or start a new session."); return; }
  if (sessions_[selected_].profile != window_->AssistantProfileId()) { Append("Start a session in this profile to pick references."); return; }
  std::vector<std::u16string> labels{T(u"Bieżąca karta")}; references_ = {"@current"};
  auto context = window_->AssistantContext({true, false, false, false, false});
  for (const auto* key : {"tabs", "pins"}) if (const auto* rows = context.FindList(key)) for (const auto& row : *rows) {
    const auto& entry = row.GetDict();
    labels.push_back((std::string_view(key) == "pins" ? u"Pin · " : u"") + base::UTF8ToUTF16(*entry.FindString("title")));
    references_.push_back(*entry.FindString("mention"));
  }
  mentions_->SetOwnedModel(Combo(std::move(labels))); mentions_->SetSelectedIndex(0); mentions_->SetVisible(true);
  mentions_->RequestFocus(); InvalidateLayout();
}
void AssistantPanel::SetPinned(bool pinned) {
  pinned_ = pinned; UpdateAssistantButton(pin_action_, pinned ? T(u"Odepnij") : T(u"Przypnij"),pinned);
  window_->shell()->LayoutAssistant(pinned_);
}
void AssistantPanel::NewSession() {
  Stop();
  if (sessions_.size() >= 32) { Append("Session limit reached. Close and reopen Zitter to clear conversations."); return; }
  sessions_.push_back({base::Uuid::GenerateRandomV4().AsLowercaseString(), {}, {}, window_->AssistantProfileId(), {}, {}});
  selected_ = sessions_.size() - 1;
  std::vector<std::u16string> labels;
  for (size_t i = 0; i < sessions_.size(); ++i) labels.push_back(TF(u"Sesja {}", {base::NumberToString16(i + 1)}));
  sessions_control_->SetOwnedModel(Combo(std::move(labels))); sessions_control_->SetSelectedIndex(selected_);
  tabs_->SetChecked(false); pages_->SetChecked(false); actions_->SetChecked(false); history_->SetChecked(false); web_->SetChecked(true);
  confirm_clicks_->SetChecked(true);
  configuration_->SetVisible(false);permissions_view_->SetVisible(false);mentions_->SetVisible(false);
  RenderConversation();UpdateControls();
}
void AssistantPanel::SwitchSession() {
  Stop(); selected_ = sessions_control_->GetSelectedIndex().value_or(0);
  auto& session = sessions_[selected_];
  if (!session.provider.empty()) {
    for (size_t i = 0; i < std::size(kProviders); ++i) if (session.provider == kProviders[i]) provider_->SetSelectedIndex(i);
    model_->SetText(base::UTF8ToUTF16(session.model));
  }
  tabs_->SetChecked(session.permissions.tabs); pages_->SetChecked(session.permissions.pages); actions_->SetChecked(session.permissions.actions);
  history_->SetChecked(session.permissions.history); web_->SetChecked(session.permissions.web);
  confirm_clicks_->SetChecked(session.permissions.confirm_clicks);
  RenderConversation();UpdateControls();
}
void AssistantPanel::ToggleDetails(bool privacy) {
  auto* target=privacy?permissions_view_.get():configuration_.get();
  const bool visible=!target->GetVisible();permissions_view_->SetVisible(false);configuration_->SetVisible(false);
  target->SetVisible(visible);InvalidateLayout();scene_->Schedule();
}
void AssistantPanel::RenderConversation() {
  output_=nullptr;
  conversation_->RemoveAllChildViews();
  const auto& messages=sessions_[selected_].messages;
  if(messages.empty()) {
    conversation_->AddChildView(CreateAssistantWelcome());
    auto suggestion=[&](AssistantGlyph glyph,std::u16string title,std::u16string detail,std::u16string prompt,bool research){
      conversation_->AddChildView(CreateAssistantSuggestion(base::BindRepeating([](AssistantPanel* self,std::u16string prompt,bool research){
        self->prompt_->SetText(prompt);self->research_->SetChecked(research);self->UpdateControls();self->prompt_->RequestFocus();
      },base::Unretained(this),std::move(prompt),research),glyph,std::move(title),std::move(detail)));
    };
    suggestion(AssistantGlyph::kPage,T(u"Zrozum tę stronę"),T(u"Najważniejsze informacje, bez szukania"),T(u"Podsumuj najważniejsze informacje z @current."),false);
    suggestion(AssistantGlyph::kSearch,T(u"Znajdź coś w internecie"),T(u"Odpowiedzi z konkretnych źródeł"),T(u"Pomóż mi znaleźć informacje o "),false);
    suggestion(AssistantGlyph::kSpark,T(u"Zbadaj temat głębiej"),T(u"Wiele perspektyw. Jeden dobry raport."),T(u"Zbadaj dokładnie i porównaj "),true);
  } else for(const auto& message:messages) {
    if(message.kind==2) {
      auto* status=conversation_->AddChildView(scene_->MakeText(base::UTF8ToUTF16(message.text),true,false,0));
      scene_->SetTextStyle(status,6,PanelScene::Tone::kMuted);continue;
    }
    auto* card=conversation_->AddChildView(CreateAssistantSurface(message.kind==1?2:3));
    SetSidebarPanelLayout(card,true,gfx::Insets(12),6);
    auto* role=card->AddChildView(scene_->MakeText(message.kind==1?T(u"Ty"):T(u"Asystent"),false,false,0));
    scene_->SetTextStyle(role,6,PanelScene::Tone::kMuted);
    auto* body=card->AddChildView(scene_->MakeText(base::UTF8ToUTF16(message.text),true,false,0));scene_->SetTextStyle(body,5,PanelScene::Tone::kInk);
  }
  if(busy_&&!messages.empty()) {
    auto* status=conversation_->AddChildView(scene_->MakeText(research_->GetChecked()?T(u"Sprawdzam źródła…"):T(u"Pracuję nad tym…"),false,false,0));output_=status;
    scene_->SetTextStyle(status,6,PanelScene::Tone::kMuted);
  }
  InvalidateLayout();scene_->Schedule();
}
void AssistantPanel::Append(std::string text) {
  auto& transcript = sessions_[selected_].transcript;
  if (transcript.size() + text.size() > 250000) transcript.erase(0, std::min(transcript.size(), text.size() + 10000));
  transcript += "\n\n" + text;
  Message message;
  if(text.starts_with("You: ")){message.kind=1;message.text=text.substr(5);}
  else if(text.starts_with("Assistant: ")){message.kind=0;message.text=text.substr(11);}
  else if(text.starts_with("Tool: ")) {
    message.kind=2;
    const auto tool=text.substr(6);
    message.text=base::UTF16ToUTF8(tool=="read_page"?T(u"Czytam stronę…"):tool=="list_tabs"?T(u"Sprawdzam otwarte karty…"):tool=="type"?T(u"Wpisuję tekst…"):T(u"Pracuję w przeglądarce…"));
  } else {message.kind=3;message.text=text;}
  auto& messages=sessions_[selected_].messages;messages.push_back(std::move(message));
  if(messages.size()>160)messages.erase(messages.begin(),messages.begin()+40);
  RenderConversation();
  if(GetWidget())GetWidget()->LayoutRootViewIfNecessary();
  viewport_->ScrollTo(viewport_->maximum());
}
void AssistantPanel::UpdateControls() {
  const bool editable = !busy_ && sessions_[selected_].provider.empty();
  provider_->SetEnabled(editable); model_->view()->SetEnabled(editable);
  for (auto* box : {tabs_.get(), pages_.get(), actions_.get(), history_.get(), web_.get(), confirm_clicks_.get()}) box->SetEnabled(editable);
  send_->SetEnabled(!busy_); stop_->SetEnabled(busy_); prompt_->view()->SetEnabled(!busy_);
  research_->SetEnabled(!busy_); sessions_control_->SetEnabled(!busy_);
  mode_chip_->SetEnabled(!busy_);send_->SetVisible(!busy_);stop_->SetVisible(busy_);
  UpdateAssistantButton(mode_chip_,research_->GetChecked()?T(u"Deep research"):T(u"Rozmowa"),research_->GetChecked());
  UpdateAssistantButton(model_chip_,std::u16string(model_->GetText()));
  model_chip_->SetTooltipText(T(u"Sesje i model"));
  model_chip_->GetViewAccessibility().SetName(T(u"Sesje i model")+u": "+std::u16string(model_->GetText()));
  const int shared=static_cast<int>(tabs_->GetChecked())+pages_->GetChecked()+actions_->GetChecked()+history_->GetChecked();
  UpdateAssistantButton(access_chip_,shared?TF(u"Dostęp · {}",{base::NumberToString16(shared)}):T(u"Dostęp"),shared>0);
  if(static_cast<bool>(output_)!=(busy_&&!sessions_[selected_].messages.empty()))RenderConversation();
}
void AssistantPanel::Stop() {
  startup_timer_.Stop(); loader_.reset(); weak_factory_.InvalidateWeakPtrs();
  // Disconnecting the RPC aborts its provider request. Pending native callbacks
  // are invalidated above, so cancellation cannot dispatch another mutation.
  busy_ = false; actions_queue_.clear(); results_.clear(); confirmation_.reset();
  if (confirm_) confirm_->SetVisible(false);
  if (!sessions_.empty() && send_) UpdateControls();
  window_->shell()->SetAssistantActivity(false);
}
void AssistantPanel::SendForTesting(std::string text) { prompt_->SetText(base::UTF8ToUTF16(text)); Send(); }
void AssistantPanel::ConfigureForTesting(std::string provider, std::string model, AssistantPermissions permissions) {
  for (size_t i = 0; i < kProviders.size(); ++i) if (provider == kProviders[i]) provider_->SetSelectedIndex(i);
  model_->SetText(base::UTF8ToUTF16(model));
  tabs_->SetChecked(permissions.tabs); pages_->SetChecked(permissions.pages); actions_->SetChecked(permissions.actions);
  history_->SetChecked(permissions.history); web_->SetChecked(permissions.web); confirm_clicks_->SetChecked(permissions.confirm_clicks);
}
bool AssistantPanel::ContainsForTesting(std::string_view text) const { return sessions_[selected_].transcript.find(text) != std::string::npos; }
void AssistantPanel::Send() {
  if (busy_ || prompt_->GetText().empty()) return;
  auto& session = sessions_[selected_];
  if (session.profile != window_->AssistantProfileId()) { Append("This session belongs to another profile. Start a new session."); return; }
  if (session.provider.empty()) {
    session.provider = kProviders[provider_->GetSelectedIndex().value_or(0)]; session.model = base::UTF16ToUTF8(model_->GetText());
    session.permissions = {tabs_->GetChecked(), pages_->GetChecked(), actions_->GetChecked(), history_->GetChecked(), web_->GetChecked(), confirm_clicks_->GetChecked()};
  }
  busy_ = true; UpdateControls();
  configuration_->SetVisible(false);permissions_view_->SetVisible(false);mentions_->SetVisible(false);
  if (!process_.IsValid()) StartRuntime();
  else PollRuntime(0);
}
void AssistantPanel::StartRuntime() {
  base::FilePath exe; base::PathService::Get(base::DIR_EXE, &exe);
  auto env = base::Environment::Create();
  auto node_path = env->GetVar("ZITTER_NODE_PATH");
  base::FilePath node = node_path ? base::FilePath::FromUTF8Unsafe(*node_path) : base::FilePath(FILE_PATH_LITERAL("node"));
  const auto script = exe.AppendASCII("assistant").AppendASCII("server.mjs");
  port_ = 49152 + static_cast<int>(base::RandGenerator(16384)); token_ = base::HexEncode(base::RandBytesAsVector(24));
  token_ = base::ToLowerASCII(token_);
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
  if (!process_.IsValid()) { busy_ = false; Append("Could not start Node.js. Install Node 22+ or set ZITTER_NODE_PATH, then restart Zitter."); UpdateControls(); return; }
  PollRuntime(0);
}
void AssistantPanel::PollRuntime(int attempts) {
  Request("/health", {}, base::BindOnce([](base::WeakPtr<AssistantPanel> self, int attempts, base::DictValue reply) {
    if (!self || !self->busy_) return;
    if (!reply.FindBool("ready").value_or(false)) {
      if (attempts >= 30) {
        self->Stop();
        if (self->process_.IsValid()) { self->process_.Terminate(0, false); self->process_.Close(); }
        self->Append("Assistant runtime did not start. Check Node 22+ and the assistant runtime beside the executable."); return;
      }
      self->startup_timer_.Start(FROM_HERE, base::Milliseconds(100), base::BindOnce(&AssistantPanel::PollRuntime, self, attempts + 1)); return;
    }
    auto& session = self->sessions_[self->selected_];
    const auto prompt = base::UTF16ToUTF8(self->prompt_->GetText()); self->prompt_->SetText(u""); self->Append("You: " + prompt);
    auto request = base::DictValue().Set("id", session.id).Set("prompt", prompt).Set("research", self->research_->GetChecked())
        .Set("date", base::DictValue().Set("unix_ms", static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()))
             .Set("utc_offset_minutes", TimelineUtcOffsetMinutes(base::Time::Now())))
        .Set("config", base::DictValue().Set("provider", session.provider).Set("model", session.model))
        .Set("permissions", session.permissions.ToValue()).Set("context", self->window_->AssistantContext(session.permissions));
    self->Request("/turn", std::move(request), base::BindOnce(&AssistantPanel::OnTurn, self));
  }, weak_factory_.GetWeakPtr(), attempts));
}
void AssistantPanel::Request(std::string path, base::DictValue body, base::OnceCallback<void(base::DictValue)> done) {
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
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  if (path != "/health") loader_->AttachStringForUpload(base::WriteJson(body).value_or("{}"), "application/json");
  loader_->SetTimeoutDuration(path == "/health" ? base::Seconds(1) : base::Minutes(11));
  loader_->SetAllowHttpErrorResults(true);
  auto factory = window_->AssistantProfile()->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  loader_->DownloadToString(factory.get(), base::BindOnce([](base::WeakPtr<AssistantPanel> self,
      base::OnceCallback<void(base::DictValue)> done, std::optional<std::string> response) {
    if (!self) return;
    self->loader_.reset();
    auto value = response ? base::JSONReader::ReadDict(*response, base::JSON_PARSE_RFC) : std::nullopt;
    std::move(done).Run(value ? std::move(*value) : base::DictValue().Set("error", "Assistant connection failed."));
  }, weak_factory_.GetWeakPtr(), std::move(done)), 2 * 1024 * 1024);
}
void AssistantPanel::OnTurn(base::DictValue reply) {
  if (const auto* error = reply.FindString("error")) { Append(*error); busy_ = false; window_->shell()->SetAssistantActivity(false); UpdateControls(); return; }
  if (const auto* answer = reply.FindString("answer"); answer && !answer->empty()) Append("Assistant: " + *answer);
  auto* actions = reply.FindList("actions");
  if (!actions || actions->empty()) { busy_ = false; window_->shell()->SetAssistantActivity(false); UpdateControls(); return; }
  actions_queue_ = actions->Clone(); results_.clear(); action_index_ = 0; RunNext();
}
void AssistantPanel::RunNext() {
  if (!busy_) return;
  auto& session = sessions_[selected_];
  if (session.profile != window_->AssistantProfileId()) { Stop(); Append("Stopped: active profile changed."); return; }
  if (action_index_ >= actions_queue_.size()) {
    Request("/continue", base::DictValue().Set("id", session.id).Set("results", std::move(results_)),
            base::BindOnce(&AssistantPanel::OnTurn, weak_factory_.GetWeakPtr())); return;
  }
  const auto& action = actions_queue_[action_index_];
  if (!action.is_dict() || !action.GetDict().FindString("tool") || !action.GetDict().FindDict("args")) {
    Stop(); Append("Invalid browser tool request."); return;
  }
  Append("Tool: " + *action.GetDict().FindString("tool"));
  const auto& tool = *action.GetDict().FindString("tool");
  if (tool == "read_page" || tool == "click" || tool == "type" || tool == "scroll" || tool == "navigate")
    window_->shell()->SetAssistantActivity(true);
  window_->AssistantTool(*action.GetDict().FindString("tool"), action.GetDict().FindDict("args")->Clone(), session.permissions,
      base::BindOnce(&AssistantPanel::ToolFinished, weak_factory_.GetWeakPtr()));
}
void AssistantPanel::ToolFinished(base::Value value) {
  if (!busy_) return;
  if (value.is_dict() && value.GetDict().FindBool("confirmation_required").value_or(false)) {
    confirmation_ = actions_queue_[action_index_].GetDict().Clone(); confirm_->SetVisible(true);
    Append("Allow this page action once? " + value.GetDict().FindString("label")->substr(0, 120)); return;
  }
  results_.Append(base::DictValue().Set("tool", *actions_queue_[action_index_].GetDict().FindString("tool")).Set("result", std::move(value)));
  ++action_index_; RunNext();
}
}
