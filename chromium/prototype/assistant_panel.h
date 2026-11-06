// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PANEL_H_
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_handle.h"
#endif
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/view.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/controls/textfield/textfield_controller.h"
namespace network { class SimpleURLLoader; }
namespace views { class EventMonitor; class ScrollView; class Textarea; }
namespace zitter::prototype {
class WorkspaceWindow;
class EditorControl;
class PanelScene;
class PanelViewport;
class AssistantMenuPresses;
struct AssistantPermissions {
  bool tabs = false, pages = false, actions = false, history = false, web = true;
  bool confirm_clicks = true;
  base::DictValue ToValue() const;
  bool operator==(const AssistantPermissions&) const = default;
};
// Backends in their stable order: openai, codex, claude, ollama.
inline constexpr std::array<const char*, 4> kAssistantProviders = {"openai", "codex", "claude", "ollama"};
std::u16string AssistantProviderLabel(std::string_view provider);
struct AssistantModel { std::string provider, id; bool visible = true; };
// App-wide assistant settings (Settings → Asystent AI). Sessions copy the
// permission defaults when they start; narrowing applies to running sessions.
struct AssistantSettings {
  AssistantSettings();
  AssistantSettings(const AssistantSettings&);
  AssistantSettings& operator=(const AssistantSettings&);
  ~AssistantSettings();
  bool enabled = true;
  std::array<bool, 4> backends{true, true, true, true};
  std::vector<AssistantModel> models;
  std::string default_model;  // "<provider>/<id>"
  AssistantPermissions defaults;
  bool background = true, confirm_background = true;
  int concurrency = 12;
  bool BackendEnabled(std::string_view provider) const;
  // Visible models of enabled backends, in settings order.
  std::vector<AssistantModel> PanelModels() const;
  base::DictValue ToValue() const;
  static AssistantSettings FromValue(const base::DictValue* value);
};
// A page or tool inserted into a message (after @, or from the composer's buttons).
struct AssistantChip {
  std::string kind;  // "tab", "pin", "research" or "background".
  std::string ref;   // @current, @tab:<id> or @pin:<id> for pages.
  std::u16string title;
  uint32_t color = 0;
  bool operator==(const AssistantChip&) const = default;
};
// A scheduled task: an instruction the assistant runs by itself, on a schedule,
// with its own model and access.
struct AssistantTask {
  AssistantTask();
  AssistantTask(const AssistantTask&);
  AssistantTask& operator=(const AssistantTask&);
  AssistantTask(AssistantTask&&);
  AssistantTask& operator=(AssistantTask&&);
  ~AssistantTask();
  std::string id, name, instruction, provider, model, profile;
  AssistantPermissions permissions;
  int kind = 0;         // 0 every day, 1 on chosen weekdays, 2 every hour, 3 once
  int days = 0;         // kind 1: bit 0 Monday … bit 6 Sunday
  int minute = 8 * 60;  // local time of day (kind 2: minutes past the hour)
  bool active = true;
  base::Time last_run, next_run;
  int last_state = 0;   // 0 never ran, 1 done, 2 needed the user, 3 failed
  base::DictValue ToValue() const;
  static std::optional<AssistantTask> FromValue(const base::DictValue& value);
};
// The first run strictly after `after`; a null time when none is left.
base::Time AssistantTaskNextRun(const AssistantTask& task, base::Time after);
// "Codziennie · 8:00", "Pn, Pt · 18:00", "Co godzinę", "Raz · 18:00".
std::u16string AssistantTaskSchedule(const AssistantTask& task);
class AssistantPanel final : public views::View, public views::ResizeAreaDelegate, public views::TextfieldController {
 public:
  enum class Menu { kNone, kSessions, kAccess, kModel, kMention, kTasks, kTaskEditor };
  explicit AssistantPanel(WorkspaceWindow* window);
  ~AssistantPanel() override;
  // An unpinned panel follows pointer departure regardless of how it opens.
  // Keyboard opening stays visible until the mouse moves; edge opening arms immediately.
  void Show(bool from_edge = false);
  // Stops the selected conversation; background tasks keep running.
  void Stop();
  void StopAll();
  void RevokeAccess();
  void Collapse();
  // Escape skips a pending click on the visible page, otherwise stops it.
  bool HandleEscape();
  void SettingsChanged();
  void OnResize(int resize_amount, bool done_resizing) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  // The composer field: Enter sends, Shift+Enter starts a new line.
  void ContentsChanged(views::Textfield* sender, const std::u16string& new_contents) override;
  // A press outside the open menu closes it.
  void MenuPressed();
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override;
  views::View* resize_handle_for_testing() const { return resize_handle_; }
  bool pinned() const { return pinned_; }
  bool busy() const;
  void SetPinned(bool pinned);
  // Sends `text` as the user would, in the selected session; a session that
  // is busy or waiting for an answer keeps its work and only shows.
  void Ask(std::u16string text);
  // Prepares a fresh page-scoped draft without changing permission grants.
  void PreparePageAction(std::u16string text, bool needs_actions, bool needs_web);
  void SendForTesting(std::string text, bool background = false);
  void ConfigureForTesting(std::string provider, std::string model, AssistantPermissions permissions);
  bool ContainsForTesting(std::string_view text) const;
  AssistantPermissions permissions_for_testing() const;
  void ToggleMenuForTesting(Menu menu) { ToggleMenu(menu); }
  Menu open_menu_for_testing() const { return menu_; }
  gfx::Rect menu_bounds_for_testing() const;
  views::View* menu_for_testing() const { return menu_view_; }
  gfx::Rect composer_bounds_for_testing() const;
  void TypeForTesting(std::u16string text);
  std::u16string draft_for_testing() const;
  void SelectSessionForTesting(size_t index) { SelectSession(index); }
  bool KeyForTesting(ui::KeyboardCode key, int flags = 0);
  std::vector<AssistantChip> chips_for_testing() const { return chips_; }
  size_t background_tasks_for_testing() const;
  bool waiting_for_consent_for_testing() const;
  void ConsentForTesting(bool allow);
  void TakeOverForTesting();
  // Adds a running background task and one waiting for consent, for renders.
  void SeedBackgroundForTesting();
  // Adds a finished exchange to the selected session, as a real turn would.
  void SeedExchangeForTesting(std::string prompt, std::string answer);
  size_t sessions_for_testing() const { return sessions_.size(); }
  // For the toolbar button: any conversation or task working, results not seen yet.
  bool working() const;
  int unread() const { return unread_; }
  const std::vector<AssistantTask>& tasks() const { return tasks_; }
  void AddTaskForTesting(AssistantTask task);
  void RunTaskForTesting(const std::string& id) { RunTask(id); }
  void CheckTasksForTesting(base::Time now) { CheckTasks(now); }
  void OpenTaskEditorForTesting(const std::string& id) { OpenTaskEditor(id); }
  bool SaveDraftForTesting(std::string instruction);
  bool asking_for_testing() const { return selected().asking; }
  void AnswerForTesting(std::string answer) { AnswerQuestion(selected(), std::move(answer)); }
  // Runs a model's tool request as if the runtime had sent it.
  void RunActionsForTesting(base::ListValue actions);
  bool sessions_loaded_for_testing() const { return loaded_; }
  void NewSessionForTesting() { NewSession(); }
  void DeleteSessionForTesting(size_t index) { DeleteSession(sessions_[index]->key); }
  void SaveNowForTesting() { SaveSessions(); }
 private:
  // kind: 0 answer, 1 the user's message, 2 a step (state 1) or an issue (state 2),
  // 3 a note, 4 a hidden trace of a tool call (saved, for inspection),
  // 5 a question to the user with one-click options (state 1: several may be picked).
  struct Message {
    Message();
    Message(const Message&);
    Message& operator=(const Message&);
    Message(Message&&);
    Message& operator=(Message&&);
    ~Message();
    int kind = 0; std::string text; std::vector<AssistantChip> chips; int state = 0;
    std::vector<std::string> options;
    std::string answer;  // Once answered: the choice, or the user's own words.
  };
  struct Session {
    Session();
    ~Session();
    std::string key, id, provider, model, profile, transcript;
    std::u16string title;
    AssistantPermissions permissions;
    bool confirm_background = true;
    std::vector<Message> messages;
    std::u16string composer_draft;
    std::vector<AssistantChip> composer_chips;
    // Runtime of the current turn.
    bool busy = false, background = false, finished = false, auto_confirm = false, page_control = false;
    uint64_t epoch = 0;
    std::unique_ptr<network::SimpleURLLoader> loader;
    std::optional<base::DictValue> request;
    base::ListValue queue, results;
    size_t index = 0;
    std::optional<base::DictValue> confirmation;
    std::u16string confirm_label, status;
    bool confirm_key = false;  // The pending consent is a key press, not a click.
    // The tool's own question, when the consent is not for a click or key
    // (adblock_add_rules: "Zablokować reklamy na example.com?…").
    std::u16string confirm_prompt;
    std::string target;  // The tab its page actions use.
    base::Time updated;
    // The scheduled task this conversation runs, and whether it needed the user.
    std::string task_id;
    bool needed_user = false;
    std::optional<AssistantTask> proposal;  // schedule_task waiting for an answer
    // Waiting for the model, with its answer so far; a browser tool is running.
    bool thinking = false, tool_running = false;
    // A question of the model waits for the user's answer (ask_user).
    bool asking = false;
    std::vector<bool> picks;
    std::string partial;
  };
  // Has the user written anything here yet?
  static bool Started(const Session& session);
  void Layout(PassKey) override;
  Session& selected() { return *sessions_[selected_]; }
  const Session& selected() const { return *sessions_[selected_]; }
  Session* Find(const std::string& key, uint64_t epoch);
  void StopSession(Session& session);
  void ResetRuntime();
  void RenderConversation();
  void RenderBackground();
  void ToggleMenu(Menu menu);
  void CloseMenu();
  void BuildMenu();
  // A scrolling part of a menu, up to `max_height` tall.
  views::View* ScrollList(views::View* body, int max_height);
  void BuildSessionsMenu(views::View* body);
  void BuildAccessMenu(views::View* body);
  // Access of the selected session: narrowing applies at once; widening only
  // before its first message.
  void TogglePermission(int index);
  void BuildModelMenu(views::View* body);
  void BuildMentionMenu(views::View* body);
  void PromptChanged();
  bool PromptKey(const ui::KeyEvent& key);
  void MoveMention(int delta);
  void InsertChip(AssistantChip chip, bool from_mention);
  void RemoveChip(size_t index);
  void RenderChips();
  void ToggleTool(const std::string& kind);
  void PickModel(std::string provider, std::string model);
  void NewSession();
  void CreateSession(bool preserve_previous);
  void SelectSession(size_t index);
  void DeleteSession(std::string key);
  // Conversations are saved shortly after they change, and read back once.
  void ScheduleSave();
  void SaveSessions();
  void SessionsLoaded(std::optional<base::DictValue> saved);
  // The answer being written, polled from the runtime while the model works.
  void PollProgress();
  // Scheduled tasks.
  void BuildTasksMenu(views::View* body);
  void BuildTaskEditor(views::View* body);
  void OpenTaskEditor(const std::string& id);
  void CaptureDraft();
  bool SaveDraft();
  void RunTask(const std::string& id);
  void CheckTasks(base::Time now);
  void TaskFinished(Session& session, bool ok);
  AssistantTask* FindTask(const std::string& id);
  void NotifyBar();
  // Answers the pending question and lets the turn go on.
  void AnswerQuestion(Session& session, std::string answer);
  Message* PendingQuestion(Session& session);
  void ShowPartial();
  void Send(bool background);
  void StartTurn(Session& session);
  void PollRuntime(std::string key, uint64_t epoch, int attempts);
  void Request(Session& session, std::string path, base::DictValue body, base::OnceCallback<void(base::DictValue)> done);
  void OnTurn(std::string key, uint64_t epoch, base::DictValue reply);
  void RunNext(Session& session);
  void ToolFinished(std::string key, uint64_t epoch, base::Value value);
  void Consent(Session& session, bool allow);
  void MoveToBackground();
  void TakeOver(std::string key);
  void Dismiss(std::string key);
  void Append(Session& session, std::string text, std::vector<AssistantChip> chips = {});
  void UpdateControls();
  void UpdatePageControl();
  void OpenSettings();
  raw_ptr<WorkspaceWindow> window_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> viewport_ = nullptr;
  raw_ptr<views::View> header_ = nullptr, conversation_ = nullptr, background_ = nullptr;
  raw_ptr<views::View> subtitle_ = nullptr, sessions_action_ = nullptr, tasks_action_ = nullptr, access_action_ = nullptr, pin_action_ = nullptr;
  raw_ptr<views::View> composer_ = nullptr, chips_view_ = nullptr, tools_ = nullptr;
  raw_ptr<views::View> research_chip_ = nullptr, background_chip_ = nullptr, model_chip_ = nullptr, send_ = nullptr, stop_ = nullptr;
  raw_ptr<views::Textarea> prompt_ = nullptr;
  raw_ptr<EditorControl> model_search_ = nullptr;
  raw_ptr<views::View> menu_view_ = nullptr, menu_footer_ = nullptr;
  raw_ptr<views::ScrollView> menu_scroll_ = nullptr;
  std::unique_ptr<AssistantMenuPresses> menu_observer_;
  std::unique_ptr<views::EventMonitor> menu_monitor_;
  raw_ptr<views::View> resize_handle_ = nullptr;
  Menu menu_ = Menu::kNone;
  // @ suggestions: the query after the @, its position, the listed choices.
  std::optional<size_t> mention_at_;
  std::u16string mention_query_;
  std::vector<AssistantChip> mention_items_;
  std::vector<std::u16string> mention_notes_;
  std::vector<raw_ptr<views::View>> mention_rows_;
  size_t mention_active_ = 0;
  std::optional<size_t> mention_dismissed_;
  std::u16string model_query_;
  std::vector<AssistantChip> chips_;
  int preferred_width_ = 420;
  std::optional<int> resize_start_width_;
  base::OneShotTimer hide_timer_;
  base::RepeatingTimer hover_check_;
  gfx::Point last_pointer_position_;
  bool pointer_exit_armed_ = false;
  // The panel plus the edge strip beside it: leaving it hides an unpinned panel.
  gfx::Rect HoverBounds() const;
  void WatchPointer();
  std::vector<std::unique_ptr<Session>> sessions_;
  size_t selected_ = 0;
  bool loaded_ = false;
  base::OneShotTimer save_timer_;
  // A second press of "delete all" within the open history confirms it.
  bool confirm_clear_ = false;
  base::RepeatingTimer progress_timer_;
  std::vector<AssistantTask> tasks_;
  std::optional<AssistantTask> draft_;  // the task in the editor
  raw_ptr<views::Textarea> draft_instruction_ = nullptr;
  raw_ptr<views::Textfield> draft_time_ = nullptr;
  base::RepeatingTimer task_timer_;
  int unread_ = 0;
  std::unique_ptr<network::SimpleURLLoader> progress_loader_;
  raw_ptr<views::View> live_answer_ = nullptr;
  bool pinned_ = false;
  int port_ = 0;
  std::string token_;
  base::Process process_;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle process_job_;
#endif
  base::WeakPtrFactory<AssistantPanel> weak_factory_{this};
};
}
#endif
