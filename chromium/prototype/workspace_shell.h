#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_

#include <array>
#include <cstdint>
#include <memory>
#include <cstdint>
#include <map>
#include <set>
#include "zitter/core/workspace_model.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/sidebar_tree.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/browser_tools.h"
#include "chromium/prototype/page_printing.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/timer/timer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/events/event_handler.h"
#include "ui/views/context_menu_controller.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view_tracker.h"
#include "url/gurl.h"

namespace zitter::sidebar { struct Item; struct Timeline; struct PreferenceCatalogue; struct WorkspaceMenuState; struct FolderMenuState; struct ProfileControls; enum class FormCommand : uint32_t; struct FormCommandBinding; }
namespace ui { class AcceleratorTarget; }
namespace gfx { class Canvas; }
namespace views {
class DialogDelegate;
class Button;
class Combobox;
class Checkbox;
class ToggleButton;
class MdTextButton;
class Label;
class MenuRunner;
class Textfield;
class View;
class Widget;
}

namespace zitter::prototype {
class WorkspaceSwipeState;
class PanelViewport;
class ProfileFormState;
class WorkspaceFormState;
class PreferencesFormState;
class ToolsFormState;
class PreferenceChromeState;
class ImportFormState;
class SidebarNodeGraphState;
class TimeMachineView;

// Top band above the sidebar; the native traffic lights and the page commands
// are centered on its middle line.
inline constexpr int kShellHeaderHeight = 38;
// Split View shows at most this many pages side by side.
inline constexpr size_t kMaxSplitPanes = 4;

enum class ThemeMode { kSystem, kLight, kDark };
enum class SearchEngine { kDuckDuckGo, kGoogle, kBing };

enum class PageCommand { kBack, kForward, kReload };

struct ShellTab {
  std::u16string title;
  GURL url;
  size_t index = 0;
  std::string pin_id;
  std::array<uint64_t,2> identity{};  // Opaque 128-bit row token, independent of lazy WebContents.
  bool audible = false;  // Playing sound: the row shows a speaker.
  bool operator==(const ShellTab&) const = default;
};

struct ShellState {
  std::string workspace_id;
  std::string profile_id;
  std::vector<Workspace> workspaces;
  bool configuration_ready = false;
  bool profiles_busy = false;
  std::vector<ProfileInfo> profiles;
  std::vector<LinkRule> link_rules;
  std::set<std::string> sleeping_workspaces;
  std::vector<PinnedItem> pins;
  std::string active_pin;
  std::u16string workspace_name;
  std::u16string profile_name;
  GURL url;
  std::vector<ShellTab> tabs;
  size_t active_tab = 0;
  bool split = false;          // A group exists, even while another tab is shown.
  bool split_visible = false;  // The group is on screen.
  bool split_vertical = false;
  size_t active_pane = 0;      // Focused pane of a visible group.
  std::vector<size_t> split_tabs;  // Group members in pane order.
  bool can_go_back = false;
  bool can_go_forward = false;
  bool loading = false;
};

class SidebarMetal;

// Native interaction tree with optional Rust/Metal card rendering.
// No Profile, BrowserContext or WebContents dependencies.
class WorkspaceShell final : public ui::EventHandler,
                             public views::WidgetObserver,
                             public views::ContextMenuController,
                             public ui::SimpleMenuModel::Delegate,
                             public ui::NativeThemeObserver {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();
    base::RepeatingCallback<void(std::string)> select_workspace;
    base::RepeatingCallback<bool(std::string, std::string)> add_workspace;
    base::RepeatingCallback<bool(std::string, std::string)> edit_workspace;
    base::RepeatingCallback<bool(std::string)> remove_workspace;
    base::RepeatingCallback<bool(std::string, std::string)> create_profile;
    base::RepeatingCallback<bool(std::string, std::string)> rename_profile;
    base::RepeatingCallback<bool(std::string, std::string)> assign_profile;
    base::RepeatingCallback<bool(std::string)> remove_profile;
    base::RepeatingCallback<void(BrowserImportOptions)> preview_import;
    base::RepeatingClosure invalidate_import;
    base::RepeatingCallback<bool()> apply_import;
    base::RepeatingClosure dismiss_import;
    base::RepeatingCallback<void(std::string)> open_pin;
    base::RepeatingCallback<bool(const GURL&, bool)> navigate;
    base::RepeatingCallback<void(size_t)> select_tab;
    base::RepeatingCallback<void(size_t)> close_tab;
    base::RepeatingCallback<void(PageCommand)> page_command;
    base::RepeatingCallback<void(std::u16string)> search;
    base::RepeatingCallback<void(PaletteResult)> activate_result;
    base::RepeatingCallback<std::vector<PaletteResult>(std::u16string)> commands;
    base::RepeatingClosure passwords;
    base::RepeatingClosure downloads;
    base::RepeatingCallback<bool(std::string, std::string, std::string)> create_folder, rename_folder;
    base::RepeatingCallback<bool(std::string, std::string, bool)> delete_folder;
    base::RepeatingCallback<bool(std::string, std::vector<PinnedItem>)> restore_pins;
    base::RepeatingCallback<bool(std::string)> restore_workspace;
    base::RepeatingCallback<bool()> operation_pending;
    // Time Machine (timeline.rs): the window owns the timeline and its file.
    base::RepeatingCallback<const sidebar::Timeline*()> timeline;
    base::RepeatingCallback<base::Time()> timeline_now;
    base::RepeatingClosure record_timeline;
    base::RepeatingCallback<bool(int64_t, int64_t)> restore_timeline;
    base::RepeatingCallback<void(bool, int)> timeline_settings;
    base::RepeatingCallback<bool()> clear_timeline;
    base::RepeatingCallback<bool(std::string)> run_shortcut;
    base::RepeatingCallback<BrowserPreferences()> read_preferences;
    base::RepeatingCallback<std::u16string()> download_directory;
    base::RepeatingClosure choose_download_directory;
    base::RepeatingCallback<bool(std::string, int)> write_preference;
    base::RepeatingCallback<bool(std::string, std::string)> write_text_preference;
    base::RepeatingCallback<std::vector<SitePreferenceRule>()> read_site_preferences;
    base::RepeatingCallback<bool(std::string, std::string, int)> write_site_preference;
    base::RepeatingCallback<bool(SitePreferenceRule)> remove_site_preference;
    base::RepeatingCallback<void(BrowsingDataSelection)> clear_browsing_data;
    base::RepeatingCallback<void(bool)> system_settings;
    base::RepeatingClosure close_peek;
    base::RepeatingCallback<bool(bool)> promote_peek;
    base::RepeatingCallback<bool(const GURL&, std::string)> peek;
    base::RepeatingCallback<bool(const GURL&, std::string)> set_link_rule;
    base::RepeatingCallback<bool(const GURL&, std::string, bool, bool)> open_link;
    base::RepeatingClosure toggle_split;
    base::RepeatingCallback<void(size_t)> focus_pane;
    // Divider index, ratio of its two neighbouring panes, drag finished.
    base::RepeatingCallback<void(size_t, double, bool)> split_ratio;
    base::RepeatingCallback<bool(size_t)> split_tab;
    // Separates the whole group, keeping the given tab on screen.
    base::RepeatingCallback<void(size_t)> unsplit;
    // Takes one tab out of the group.
    base::RepeatingCallback<void(size_t)> leave_split;
    // Closes every tab of the group the given tab belongs to.
    base::RepeatingCallback<void(size_t)> close_split_tabs;
    base::RepeatingCallback<void(size_t, bool)> move_split_pane;
    base::RepeatingClosure swap_split;
    base::RepeatingCallback<void(bool)> split_vertical;
    base::RepeatingCallback<void(std::string)> split_pin;
    // Sidebar drag and drop: reorder tabs and pins, pinning across sections.
    base::RepeatingCallback<bool(size_t, size_t)> move_tab;
    base::RepeatingCallback<bool(size_t, std::string, std::string)> pin_tab;
    base::RepeatingCallback<bool(std::string, size_t)> unpin;
    base::RepeatingCallback<bool(std::string, std::string, GURL)> update_pinned_url;
    base::RepeatingCallback<bool(std::string)> reset_pinned_url;
    base::RepeatingCallback<bool(std::string, std::string, std::string)> move_pin;
  };

  WorkspaceShell(base::FilePath settings_path, Callbacks callbacks);
  ~WorkspaceShell() override;
  // Client view that turns empty frame/header space into a window drag area.
  static views::WidgetDelegate::ClientViewFactory ClientViewFactory();
  std::unique_ptr<views::View> Build();
  views::View* content_host() const { return content_host_; }
  // A view at the right end of the header band, owned by the window's root
  // (design: Extensions). Its preferred width is honored up to the page commands.
  views::View* SetHeaderTrailing(std::unique_ptr<views::View> view);
  // The color of the space on screen, which tints the window and its popovers.
  SkColor active_workspace_color() const { return workspace_color(state_.workspace_id); }
  void Attach(views::Widget* widget, ui::AcceleratorTarget* target);
  // Pages in pane order; fewer than two show the first page alone.
  void SetPanes(std::vector<views::View*> panes, std::vector<double> weights, bool vertical = false);
  // Header buttons above each pane of a visible split.
  void ShowSplitPaneMenu(size_t pane, gfx::Point screen_point);
  void CloseSplitPane(size_t pane);
  views::View* split_pane_button(size_t pane, bool close) const;
  views::View* peek_host() const { return peek_host_; }
  views::Widget* peek_widget() const { return peek_widget_.get(); }
  views::View* peek_toolbar_for_testing() const{return peek_toolbar_;}
  views::View* peek_title_for_testing() const{return peek_title_;}
  views::View* peek_action_for_testing(size_t index) const{constexpr std::array<size_t,5> positions={0,1,3,4,5};return peek_toolbar_&&index<positions.size()&&peek_toolbar_->children().size()==6?peek_toolbar_->children()[positions[index]].get():nullptr;}
  bool peek_toolbar_metal_visible_for_testing() const;
  bool peek_toolbar_contains_text_for_testing(std::string_view text) const;
  gfx::Rect peek_toolbar_scene_bounds_for_testing() const;
  void ShowPeek(std::u16string title);
  void SetPeekTitle(std::u16string title);
  void HidePeek();
  void BuildLinkMenu(GURL url, std::string source_workspace);
  void ShowLinkMenu(GURL url, std::string source_workspace, gfx::Point point);
  ui::SimpleMenuModel* link_menu() const { return link_menu_.get(); }
  void Detach();
  void Update(ShellState state);
  void OpenAddress(bool new_tab);
  void OpenCommandPalette();
  // `complete` is false for the quick first answer (tabs, pins); the loader
  // stays until the history answer arrives.
  void SetSearchResults(std::u16string query, std::string profile_id, std::vector<PaletteResult> results, bool complete = true);
  bool palette_loading_for_testing() const;
  const std::vector<PaletteResult>& search_results() const { return search_results_; }
  const std::vector<PaletteResult>& suggestions() const { return suggestions_; }
  void ShowSidebar();
  void SetContentFullscreen(views::View* page);
  void AnimateReload();
  bool reload_animating_for_testing() const { return reload_timer_.IsRunning(); }
  views::View* copy_url_button_for_testing() const { return copy_url_button_; }
  void ToggleSidebar();
  // The header's sidebar button pins and unpins the panel.
  void TogglePinned();
  // Asks before removing a space; accepting removes it with an undo toast.
  void RequestRemoveWorkspace(std::string id);
  // Closing the window would cancel |count| downloads; |answer| gets whether
  // to quit anyway. Outside macOS, where AppController asks.
  void ConfirmQuitWithDownloads(int count, base::OnceCallback<void(bool)> answer);
  // A destructive confirmation over `parent` (design: Dialog): `detail` muted,
  // `warning` in `danger`, `action` the danger button; waiting is the default.
  void ConfirmDanger(views::Widget* parent, std::u16string title, std::u16string detail, std::u16string warning,
                     std::u16string action, base::OnceCallback<void(bool)> answer);
  views::Widget* danger_dialog_for_testing() const { return danger_dialog_.get(); }
  // The |z⟩ menu's „O Zitter”: Zitter's and Chromium's versions.
  void ShowAbout();
  void ShowUpdateReady(std::string version, base::RepeatingClosure install = {});
  // Find in page (design: Find & Print): the bar over the active page's top
  // right. `find` gets the text, the direction and case matching; `closed`
  // runs when the bar hides.
  using FindRequest = base::RepeatingCallback<void(std::u16string, bool, bool)>;
  void ShowFind(base::RepeatingCallback<gfx::Rect()> page_bounds, FindRequest find, base::RepeatingClosure closed);
  void SetFindResult(int active, int count);
  void HideFind();
  // Hides the bar and forgets its text (another page became active).
  void ResetFind();
  bool find_visible() const;
  std::u16string find_count_for_testing() const;
  views::Textfield* find_field_for_testing() const;
  // The print sheet (design: Print sheet) on the dimmed window.
  struct PrintHandlers {
    base::RepeatingCallback<void(PrintOptions)> preview;
    base::RepeatingCallback<void(PrintOptions)> print;
    base::RepeatingClosure system_dialog;
    base::RepeatingClosure closed;
  };
  void ShowPrint(PrintHandlers handlers);
  void SetPrinters(std::vector<PrinterChoice> printers);
  void SetPrintPreview(std::vector<gfx::ImageSkia> pages, bool failed);
  // An empty error closes the sheet; otherwise it shows under the settings.
  void PrintFinished(std::u16string error);
  void HidePrint();
  bool print_visible() const;
  views::View* print_sheet_for_testing() const;
  std::u16string print_sheets_for_testing() const;
  size_t print_preview_pages_for_testing() const;
  bool print_ready_for_testing() const;
  // `printer` indexes the printers, then Save as PDF; `pages` is All, Current or Range.
  void PrintChooseForTesting(size_t printer, size_t pages, std::u16string range, bool landscape);
  void PrintNowForTesting();
  views::Widget* workspace_remove_dialog() const { return remove_dialog_.get(); }
  std::optional<size_t> split_drop_slot_for_testing() const { return split_drop_slot_; }
  bool drag_hover_split_for_testing() const { return drag_hover_split_; }
  std::optional<size_t> SplitDropSlotForTesting(gfx::Point point) const { return SplitDropSlot(point); }
  void DropIntoSplitForTesting(size_t tab, size_t slot) { QueueSplit(tab, {}, slot); }
  const std::vector<size_t>& split_tabs_for_testing() const { return state_.split_tabs; }
  size_t active_pane_for_testing() const { return state_.active_pane; }
  void PaneDragForTesting(size_t pane, std::optional<gfx::Point> point, bool drop) { HandlePaneDrag(pane, point, drop); }
  // The footer's „+”: a new tab, folder or space.
  void ShowAddMenu();
  void SetPinned(bool pinned);
  // Drag delta from the sidebar's right-edge handle; persisted when done.
  void ResizeSidebar(int amount, bool done);
  int sidebar_width() const { return sidebar_width_; }
  bool CloseOverlays(bool keep_sidebar = false);
  void OpenGeneralSettings();
  // The |z⟩ menu (design: Menu Zitter): the macOS menu bar's commands on
  // Windows, below |anchor|.
  void ShowAppMenu(views::View* anchor);
  bool app_menu_running_for_testing() const;
  // Preferences on the Privacy category (fingerprint protection).
  void OpenPrivacySettings();
  void OpenTools(int page);  // 0: Time Machine, 1: shortcuts, 2: search providers.
  void SyncShortcuts() { RefreshKeyRegistrations(); }
  bool CaptureShortcut(const ui::Accelerator& accelerator);
  std::string ShortcutFor(const ui::Accelerator& accelerator) const;
  bool SetShortcut(std::string id, std::string chord);
  bool UpsertSearchProvider(SearchProvider provider, std::string previous = {});
  bool RemoveSearchProvider(std::string keyword);
  bool SetDefaultSearchProvider(std::string keyword);
  GURL ResolveInput(std::string text, std::u16string* provider_name = nullptr) const;
  const BrowserTools& browser_tools() const { return tools_; }
  void RecordCommandUse(std::string id, std::string target);
  bool tools_ready() const { return tools_ready_; }
  views::Widget* tools_widget() const { return tools_widget_.get(); }
  PanelViewport* tools_viewport_for_testing() const{return tools_scroll_;}
  EditorControl* shortcut_input() const { return shortcut_chord_; }
  views::View* tools_selection() const { return tools_picker_; }
  views::View* tools_choice_for_testing(std::string_view key) const;
  size_t tools_choice_count_for_testing(std::string_view key) const;
  std::optional<size_t> tools_choice_selection_for_testing(std::string_view key) const;
  std::u16string tools_choice_label_for_testing(std::string_view key, size_t index) const;
  bool tools_choice_metal_visible_for_testing(std::string_view key) const;
  gfx::Rect tools_choice_metal_bounds_for_testing(std::string_view key) const;
  void ActivateToolsChoiceForTesting(std::string_view key, size_t index);
  bool tools_action_metal_visible_for_testing(views::View* button) const;
  gfx::Rect tools_action_metal_bounds_for_testing(views::View* button) const;
  bool tools_actions_contains_scene_text_for_testing(std::string_view text) const;
  size_t tools_action_groups_for_testing() const;
  views::View* tools_body_for_testing() const{return tools_body_;}
  views::View* tools_close_for_testing() const{return tools_close_;}
  bool tools_close_metal_visible_for_testing() const;
  bool tools_chrome_visible_for_testing() const{return ToolsChromeVisible();}
  bool tools_chrome_contains_text_for_testing(std::string_view text) const;
  bool tools_chrome_excludes_for_testing(views::View* view) const;
  bool tools_chrome_order_correct_for_testing() const;
  std::string tools_chrome_debug_for_testing() const;
  void SetThemeMode(ThemeMode mode);
  ThemeMode theme_mode() const { return theme_mode_; }
  void SetSearchEngine(SearchEngine engine);
  SearchEngine search_engine() const { return search_engine_; }
  void SetWorkspaceAnimations(bool enabled);
  // Time Machine recording (Preferences → Wygląd): on/off and 7, 30 or 90 days.
  void SetTimeMachine(bool enabled, int days);
  bool time_machine_enabled() const { return time_machine_enabled_; }
  // The full-window Time Machine view of the active workspace.
  void OpenTimeMachine();
  bool time_machine_visible() const;
  TimeMachineView* time_machine_for_testing() const { return time_machine_; }
  void SetWorkspaceNames(bool shown);
  void SetPinnedFoldersCollapsed(bool collapsed);
  void SetUiLanguageChoice(size_t index);
  // The same without saving the preference (tests that predate the default).
  void SetPinnedFoldersCollapsedForTesting(bool collapsed) { ApplyPinnedFoldersCollapsed(collapsed); }
  bool workspace_animations() const { return workspace_animations_; }
  bool workspace_names() const { return workspace_names_; }
  bool pinned_folders_collapsed() const { return folders_collapsed_; }
  void AnimateWorkspaceSwitch(std::string_view previous);
  bool workspace_animation_running() const;
  bool workspace_animation_pending() const { return workspace_animation_pending_; }
  bool workspace_switch_metal_visible() const;
  bool workspace_switch_contains_scene_text(std::string_view text) const;
  bool workspace_swipe_visible() const { return swipe_overlay_ != nullptr; }
  double workspace_swipe_position() const;
  size_t workspace_swipe_preview_count() const;
  bool workspace_swipe_metal_visible() const;
  size_t workspace_swipe_native_capture_count() const;
  size_t workspace_swipe_metal_frame_count() const;
  bool workspace_swipe_contains_scene_text(std::string_view text) const;
  views::Widget* general_widget() const { return general_widget_.get(); }
  void SetPreferencesStatus(std::u16string text, bool error = false);
  // Rust forms own outcome copy/tone; `native` is the Views fallback text.
  // outcome is sidebar::PreferenceOutcome; general updates only the settings status.
  void ReportPreferenceOutcome(int outcome, const std::string& key, bool ok, std::u16string native);
  void SetOperationStatus(std::u16string text);
  // Guarded page-lifecycle operations; wording comes from Rust in Rust mode.
  enum class OperationStatusEvent { kChecking, kCancelled, kDone, kStopCapture };
  void SetOperationStatus(OperationStatusEvent event);
  void SetBrowsingDataBusy(bool busy);
  EditorControl* preference_textfield(std::string key) const { return preference_textfields_.at(key); }
  views::View* preference_picker(std::string key) const { return preference_pickers_.at(key); }
  EditorControl* site_origin_input() const { return site_origin_; }
  EditorControl* preferences_search() const { return preferences_search_; }
  views::View* preference_toggle(std::string key) const { return preference_toggles_.at(key); }
  bool preference_chrome_contains_text_for_testing(std::string_view text) const;
  bool preference_chrome_order_correct_for_testing() const;
  std::string preference_chrome_debug_for_testing() const;
  bool preference_chrome_excludes_for_testing(views::View* view) const;
  bool preference_chrome_visible_for_testing() const{return PreferenceChromeVisible();}
  size_t preference_category_for_testing() const;
  views::View* preference_navigation_for_testing(size_t category) const;
  std::vector<views::View*> preference_rows_for_testing(size_t category) const{std::vector<views::View*> rows;for(const auto& row:preference_sections_.at(category).rows)rows.push_back(row.view);return rows;}
  PanelViewport* preference_viewport_for_testing() const{return general_scroll_;}
  views::View* preference_close_for_testing() const{return preferences_close_;}
  bool preference_navigation_metal_visible_for_testing() const;
  size_t preference_navigation_frame_count_for_testing() const;
  bool preference_navigation_selected_for_testing(size_t category) const;
  bool preference_navigation_contains_text_for_testing(std::string_view text) const;
  bool preference_close_metal_visible_for_testing() const;
  views::View* preference_save_button_for_testing(std::string key) const{return preference_save_buttons_.at(key);}
  bool preference_action_metal_visible_for_testing(views::View* button) const;
  bool preference_actions_contains_scene_text_for_testing(std::string_view text) const;
  views::View* preference_switch_for_testing(std::string_view key) const;
  bool preference_switch_checked_for_testing(std::string_view key) const;
  bool preference_switch_metal_visible_for_testing(std::string_view key) const;
  bool preference_switch_timer_running_for_testing(std::string_view key) const;
  views::View* clear_checkbox_for_testing(size_t index) const;
  bool clear_choices_metal_visible_for_testing() const;
  views::View* theme_picker() const { return theme_picker_; }
  views::View* search_engine_picker() const { return search_engine_picker_; }
  views::View* preference_choice_for_testing(std::string_view key) const;
  std::optional<size_t> preference_choice_selection_for_testing(std::string_view key) const;
  size_t preference_choice_count_for_testing(std::string_view key) const;
  std::u16string preference_choice_label_for_testing(std::string_view key,size_t index) const;
  bool preference_choice_metal_visible_for_testing(std::string_view key) const;
  gfx::Rect preference_choice_metal_bounds_for_testing(std::string_view key) const;
  void ActivatePreferenceChoiceForTesting(std::string_view key,size_t index);

  bool address_visible() const;
  bool sidebar_visible() const;
  bool metal_palette_chrome_visible(size_t index) const;
  size_t metal_palette_chrome_frame_count_for_testing(size_t index) const;
  bool metal_palette_chrome_contains_text_for_testing(size_t index,std::string_view text) const;
  gfx::Rect metal_palette_chrome_screen_bounds_for_testing(size_t index) const;
  views::View* palette_chrome_view_for_testing(size_t index) const{return index<palette_chrome_views_.size()?palette_chrome_views_[index].get():nullptr;}
  bool metal_palette_visible() const;
  bool metal_palette_contains_text_for_testing(std::string_view text) const;
  gfx::Rect metal_palette_screen_bounds_for_testing() const;
  size_t palette_selection_for_testing() const;
  views::View* palette_results_view() const{return suggestion_list_;}
  PanelViewport* palette_viewport() const{return suggestion_scroll_;}
  PanelViewport* workspace_viewport() const{return workspace_scroll_;}
  bool metal_sidebar_visible() const;
  bool metal_sidebar_grip_visible() const;
  // 0 untyped/retired, 1 checkbox, 2 switch, 3 selector, 4 workspace, 6 palette row, 7 site-rule row, 8 form command, 9 tab row, 10 pin row; local graph model diagnostics.
  views::View* site_rule_results_for_testing() const{return site_rules_list_;}
  void RefreshSiteRulesForTesting(bool reload=true){RefreshSitePreferences(reload);}
  uint32_t control_model_kind_for_testing(views::View* view) const;
  uint64_t scene_parent_id_for_testing(views::View* view) const;
  views::View* header_action_for_testing(size_t index) const;
  views::View* sidebar_action_for_testing(size_t index) const{return index==0?url_pill_.get():index==1?new_tab_button_.get():nullptr;}
  views::View* footer_action_for_testing(size_t index) const{return index==0?add_button_.get():index==1?downloads_button_.get():nullptr;}
  ui::SimpleMenuModel* add_menu_for_testing() const{return add_menu_.get();}
  std::unique_ptr<views::View> CreateWorkspaceActionForTesting(base::RepeatingClosure callback) const;
  views::View* sidebar_grip_for_testing() const;
  views::View* split_divider_for_testing() const;
  bool metal_split_visible(size_t index) const;
  bool metal_split_contains_text_for_testing(size_t index,std::string_view text) const;
  gfx::Rect metal_split_screen_bounds_for_testing(size_t index) const;
  bool metal_header_visible() const;
  size_t metal_header_frame_count() const;
  gfx::Rect metal_header_screen_bounds_for_testing() const;
  // GPU submissions are separate from native subtree paint passes skipped/run.
  size_t metal_sidebar_frame_count() const;
  bool metal_sidebar_contains_text_for_testing(std::string_view text) const;
  gfx::Rect metal_sidebar_screen_bounds_for_testing() const;
  bool metal_sidebar_contains_rect_for_testing(gfx::RectF bounds, gfx::RectF clip) const;
  size_t native_sidebar_paint_count() const;
  size_t metal_sidebar_paint_count() const;
  size_t CycleSidebarTab(size_t current, size_t count, bool backwards) const;

  void OpenWorkspaceSettings();
  bool settings_visible() const;
  void SetConfigurationError(bool error);
  void OpenProfiles(std::string profile_id = {}, std::string workspace_id = {});
  void OpenImport(std::optional<ImportBrowser> browser = std::nullopt);
  // The first-run wizard (design: Pierwsze uruchomienie, Windows): welcome,
  // default browser, import and the first space, in a tool window.
  struct WelcomeSetup {
    // Browsers found on this computer, in the import step.
    std::vector<ImportBrowser> sources;
    std::u16string space_name;
    SkColor space_color = 0;
    // Passwords from a CSV export; reports the line to show under the source.
    base::RepeatingCallback<void(base::OnceCallback<void(std::u16string)>)> import_passwords;
    base::RepeatingCallback<void(ImportBrowser, base::OnceCallback<void(bool, std::u16string)>)> import_browser;
  };
  struct WelcomeResult {
    std::u16string space_name;
    std::optional<SkColor> space_color;
  };
  void ShowWelcome(WelcomeSetup setup, base::OnceCallback<void(WelcomeResult)> done);
  views::Widget* welcome_widget_for_testing() const { return welcome_widget_.get(); }
  // Import service stage/counts; wording comes from Rust (or the Views formatter).
  struct ImportStatusReport {
    enum class Stage { kReading, kPreview, kImporting, kSavingBrowsingData, kFinished };
    Stage stage = Stage::kPreview;
    std::u16string browser;
    std::string error;
    size_t profiles = 0, spaces = 0, folders = 0, tabs = 0, skipped = 0;
    bool ok = false;
    size_t added_profiles = 0, added_spaces = 0;
    bool browsing_data = false;
    size_t history = 0, cookies = 0, data_skipped = 0;
    bool data_failed = false;
  };
  void SetImportStatus(const ImportStatusReport& report, bool can_import, bool busy = false, bool applying = false);
  void SetImportStatus(std::u16string message, bool can_import, bool busy = false, bool applying = false);
  void BeginImportPreview(const BrowserImportOptions& options);
  bool import_panel_contains_scene_text(std::string_view text) const;
  bool import_choices_metal_visible() const;
  void SelectImportBrowserForTesting(size_t index);
  bool import_source_metal_visible() const;
  void SetImportOptionCheckedForTesting(size_t option,bool checked);
  views::View* import_panel_for_testing() const{return import_root_;}
  bool import_visible() const;
  BrowserImportOptions import_options() const;
  views::View* import_browser_picker() const { return import_browser_picker_; }
  views::View* import_bookmarks() const { return import_bookmarks_; }
  views::View* import_close() const { return dismiss_import_; }
  bool import_close_metal_visible_for_testing() const;
  views::View* import_history() const { return import_history_; }
  views::View* import_cookies() const { return import_cookies_; }
  views::View* import_profiles() const { return import_profiles_; }
  views::Widget* import_widget() const { return import_widget_.get(); }
  // Profile service outcomes; wording comes from Rust (or the Views table).
  enum class ProfileStatusEvent { kDuplicateName, kCreating, kCreateFailed, kCreated, kCreateNotSaved, kRenamed, kRenameFailed, kDeleting, kDeleted, kDeleteNotSaved, kLoadFailed };
  // Browsing-data removal outcomes and confirmation copy (Rust in Rust mode).
  enum class ClearDataEvent { kBusy, kNothingSelected, kHistoryForbidden, kClearing, kCleared, kFailed };
  void ReportClearData(ClearDataEvent event, const std::u16string& profile);
  struct ClearDataCopy { bool valid = false; std::u16string title, description, confirm, cancel; };
  ClearDataCopy ClearDataConfirmCopy(const std::u16string& profile, int period, bool history, bool cookies, bool cache) const;
  void SetProfileStatus(ProfileStatusEvent event);
  void SetProfileStatus(std::u16string message);
  views::Widget* profiles_widget() const { return profiles_widget_.get(); }
  EditorControl* profile_name_input() const { return profile_name_input_; }
  views::View* managed_profile_picker() const { return profile_list_; }
  EditorControl* profile_rename_input() const { return profile_rename_input_; }
  std::optional<size_t> profile_picker_selection(bool managed) const;
  size_t profile_picker_count() const;
  void SetProfilePickerSelectionForTesting(bool managed,size_t index);
  void ActivateProfilePickerSelectionForTesting(bool managed,size_t index);
  bool profile_picker_metal_visible(bool managed) const;
  bool profile_panel_contains_scene_text(std::string_view text) const;
  bool workspace_panel_contains_scene_text(std::string_view text) const;
  views::View* workspace_panel_for_testing() const{return settings_root_;}
  views::View* profile_panel_for_testing() const{return profiles_root_;}
  // An undoable event: "lead" + bold "noun", with its button running `undo`.
  // `action` names the button („Cofnij” when empty).
  void ShowToast(std::u16string lead, std::u16string noun, base::OnceClosure undo, std::u16string action = {});
  void HideToast();
  // The address pill trembles once (a copied address).
  void TremorAddress();
  // Downloads (design: Downloads). The footer's Downloads icon turns into a ring
  // showing how much of the files in progress has arrived (`fraction` < 0: an
  // unknown size) and how many there are. `newest` names the latest.
  void SetDownloadProgress(int active, double fraction, std::u16string newest);
  // A transfer began: an arrow flies from the pointer (or the page) to the ring.
  // With the sidebar slid away it lands in a pill in the page's lower left
  // corner instead, which stays a moment and opens the list when clicked.
  void DownloadStarted(std::u16string name);
  // A transfer finished: the ring trembles once and shows ✓ for a moment.
  void DownloadFinished();
  // Where the downloads list opens from: the ring or the pill (empty: neither).
  gfx::Rect downloads_anchor_in_screen() const;
  views::Widget* download_cue_widget_for_testing() const { return download_cue_widget_.get(); }
  bool download_cue_is_pill_for_testing() const;
  void UndoToast();
  views::Widget* toast_widget() const { return toast_widget_.get(); }
  std::u16string toast_text_for_testing() const;
  bool appearance_metal_visible(bool emoji) const;
  bool form_actions_contains_scene_text(bool profiles,std::string_view text) const;
  size_t form_action_canvases_visible_for_testing(bool profiles) const;
  bool appearance_contains_scene_text(std::string_view text) const;
  gfx::Rect appearance_scene_bounds_for_testing(bool emoji) const;
  views::Widget* settings_widget() const { return settings_widget_.get(); }
  PanelViewport* workspace_viewport_for_testing() const{return settings_scroll_;}
  PanelViewport* import_viewport_for_testing() const{return import_scroll_;}
  bool pinned() const { return pinned_; }
  void SetWorkspaceColor(std::string_view id, SkColor color);
  SkColor workspace_color(std::string_view id) const;
  // One emoji grapheme shown instead of the workspace dot; empty clears it.
  void SetWorkspaceEmoji(std::string_view id, std::u16string emoji);
  std::u16string workspace_emoji(std::string_view id) const;
  EditorControl* emoji_input() const { return emoji_input_; }
  bool appearance_ready() const { return appearance_ready_; }
  bool appearance_saved() const { return pending_writes_ == 0; }
  bool browsing_data_busy_for_testing() const { return browsing_data_busy_; }
  views::Widget* address_widget() const { return address_widget_.get(); }
  views::Widget* scrim_widget_for_testing() const { return scrim_widget_.get(); }
  views::View* tab_button(size_t index) const;
  views::View* pin_button(std::string_view id) const;
  uint64_t sidebar_node_id_for_testing(views::View* row) const;
  // Rust split pair node: {id, parent id, visible}; zeros without a Rust graph.
  std::array<uint64_t, 3> sidebar_pair_for_testing() const;
  views::Widget* sidebar_widget() const { return sidebar_widget_.get(); }
  EditorControl* workspace_input() const { return workspace_input_; }
  EditorControl* new_profile_input() const { return new_profile_input_; }
  views::View* workspace_profile_row(size_t index) const { return index < workspace_profile_rows_.size() ? workspace_profile_rows_[index].get() : nullptr; }
  size_t workspace_profile_row_count() const { return workspace_profile_rows_.size(); }
  bool workspace_saved_visible() const;
  void SaveWorkspaceForTesting(){SubmitWorkspace(false);}
  void StartWorkspaceCreationForTesting(){if(!creating_workspace_)ToggleWorkspaceCreation();}
  bool creating_workspace_for_testing() const{return creating_workspace_;}
  views::View* new_profile_import_for_testing() const{return new_profile_import_;}
  views::View* workspace_button(std::string_view id) const;
  size_t pinned_list_build_count() const { return pinned_list_build_count_; }
  size_t sidebar_context_visit_count() const { return sidebar_context_visit_count_; }
  size_t pin_visibility_update_count() const { return pin_visibility_update_count_; }
  views::View* tab_scroll_view() const { return tab_scroll_?tab_scroll_->view():nullptr; }
  PanelViewport* tab_viewport() const { return tab_scroll_; }
  void BuildWorkspaceMenu(std::string id);
  void BuildFolderMenu(std::string parent = {});
  void BuildTabMenu(std::optional<size_t> tab,std::string pin = {});
  ui::SimpleMenuModel* tab_context_menu_for_testing() const {return workspace_menu_.get();}
  void RequestNewFolder(std::string workspace, std::string parent = {});
  void RequestRenameFolder(std::string workspace, std::string folder);
  void RequestEditPinnedUrl(std::string workspace, std::string pin);
  views::Widget* pin_url_dialog() const { return pin_url_dialog_.get(); }
  EditorControl* pin_url_editor_for_testing() const { return pin_url_editor_; }
  void RequestDeleteFolder(std::string workspace, std::string folder);
  void ShowFolderSaveError();
  views::View* folder_form_for_testing() const;
  EditorControl* folder_editor_for_testing() const;
  bool folder_form_metal_visible() const;
  bool folder_form_contains_scene_text(std::string_view text) const;
  std::u16string folder_form_error_for_testing() const;
  views::View* folder_notice_for_testing() const;
  bool folder_notice_metal_visible() const;
  bool folder_notice_contains_scene_text(std::string_view text) const;
  std::u16string folder_notice_error_for_testing() const;
  size_t folder_notice_line_count_for_testing() const;
  void WarmWorkspaceSidebars(std::map<std::string, std::vector<PinnedItem>> pins);
  bool sidebars_warmed() const { return sidebar_warm_queue_.empty() && !sidebar_warm_timer_.IsRunning(); }
  void OnScrollEvent(ui::ScrollEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
  views::Widget* folder_dialog() const { return folder_dialog_.get(); }
  views::View* sidebar_view() const { return sidebar_root_; }
  views::View* tab_list_view() const { return tab_list_; }
  ui::SimpleMenuModel* workspace_menu() const { return workspace_menu_.get(); }
  bool workspace_context_menu_running_for_testing() const;
  void CancelContextMenuForTesting();
  bool add_menu_running_for_testing() const;
  views::View* profile_picker() const { return profile_picker_; }
  views::View* color_presets() const { return color_presets_; }
  views::View* color_wheel_for_testing(size_t index) const { return color_wheel_[index]; }
  EditorControl* address_input() const { return address_; }
  // Application key policy; the platform input method owns composing keys.
  bool HandleAddressKey(const ui::KeyEvent& event);
  bool HandlePreferenceEditorKey(EditorControl* sender,const ui::KeyEvent& event);
  bool HandleToolsEditorKey(EditorControl* sender,const ui::KeyEvent& event);
  EditorControl* provider_input(size_t index) const {
    return std::array<EditorControl*,3>{provider_name_,provider_keyword_,provider_url_}.at(index);
  }

 private:
  using Suggestion = PaletteResult;

  void PositionPanels();
  void ApplySidebarWidth(int width,bool done);
  void HideMetalSidebarGrip();
  void ScheduleMetalSplit();
  void UpdateMetalSplit();
  void ScheduleMetalHeader();
  void UpdateMetalHeader();
  void ScheduleMetalPalette();
  void UpdateMetalPalette();
  void UpdateMetalPaletteChrome();
  void SetPaletteErrorText(std::u16string text);
  std::array<std::unique_ptr<SidebarMetal>,4> metal_palette_chrome_;
  std::array<raw_ptr<views::View>,4> palette_chrome_views_={};
  std::unique_ptr<SidebarMetal> metal_palette_;
  bool metal_palette_pending_=false;
  void SyncSceneRowInput();
  void WatchSceneRowInput(views::View* row,base::RepeatingClosure sync);
  std::map<views::View*,base::RepeatingClosure> scene_row_input_;
  void ScheduleMetalSidebar();
  void UpdateMetalSidebar();
  std::array<std::unique_ptr<SidebarMetal>,2*kMaxSplitPanes-1> metal_split_;
  bool metal_split_pending_=false;
  std::unique_ptr<SidebarMetal> metal_sidebar_grip_;
  std::unique_ptr<SidebarMetal> metal_header_;
  bool metal_header_pending_=false;
  raw_ptr<views::View> header_view_=nullptr;
  raw_ptr<views::View> header_sidebar_button_=nullptr;
  std::unique_ptr<SidebarMetal> metal_sidebar_;
  bool metal_sidebar_pending_ = false;
  int ToolsPage() const;
  bool ToolsRecording() const;
  // status is sidebar::ToolsStatus; Rust owns the copy when the tools form exists.
  std::u16string ToolsStatusText(int status) const;
  void ResetToolsForm();
  void RefreshTools();
  void BuildRustToolsForm();
  void BuildRustPreferenceRows(const sidebar::PreferenceCatalogue& catalogue);
  void RefreshToolsViewport();
  void RefreshKeyRegistrations();
  void SaveTools();
  void ToolsAction(int action,std::optional<std::string> choice_key=std::nullopt);
  BrowserTools tools_;
  bool tools_ready_ = false;
  bool command_usage_dirty_ = false;
  std::vector<std::pair<std::pair<std::string, std::string>, base::Time>> pending_command_uses_;
  bool keys_dirty_ = false, engines_dirty_ = false;
  raw_ptr<ui::AcceleratorTarget> accelerator_target_ = nullptr;
  raw_ptr<views::View> tools_root_ = nullptr;
  raw_ptr<views::View> tools_body_ = nullptr;
  raw_ptr<views::View> tools_close_ = nullptr;
  raw_ptr<views::View> tools_title_ = nullptr;
  raw_ptr<views::View> tools_status_ = nullptr;
  raw_ptr<EditorControl> shortcut_chord_ = nullptr;
  raw_ptr<EditorControl> provider_name_ = nullptr;
  raw_ptr<EditorControl> provider_keyword_ = nullptr;
  raw_ptr<EditorControl> provider_url_ = nullptr;
  raw_ptr<views::View> tools_picker_ = nullptr;
  raw_ptr<PanelViewport> tools_scroll_ = nullptr;
  bool browsing_data_busy_ = false;
  uint64_t tools_generation_ = 0;
  void PostToolsAction(int action);
  void RefreshToolsChoiceFields();
  void SetShortcutRecording(bool recording);
  std::unique_ptr<views::WidgetDelegate> tools_delegate_;
  std::unique_ptr<views::WidgetDelegate> toast_delegate_;
  std::unique_ptr<views::Widget> toast_widget_;
  std::unique_ptr<views::WidgetDelegate> scrim_delegate_;
  std::unique_ptr<views::Widget> scrim_widget_;
  raw_ptr<views::View> toast_ = nullptr;  // ToastView
  base::OnceClosure toast_undo_;
  base::OneShotTimer toast_timer_;
  base::RepeatingTimer toast_tremor_timer_;
  int toast_tremor_step_ = 0;
  std::unique_ptr<views::Widget> tools_widget_;
  void RequestFolderName(std::string workspace, std::string parent, std::string renamed);
  // `refresh_controls` false only repaints: a new workspace color must not rebind
  // or cancel the general settings controls.
  void ApplyTheme(bool refresh_controls = true);
  void RefreshGeneralControls();
  void RefreshPreferenceViewport();
  void RefreshFormViewports();
  void FilterPreferences();
  void SelectPreferenceCategory(size_t category);
  void WritePreference(std::string key, int value);
  void SaveTextPreference(std::string key);
  void PreferenceTextEdited(const std::string& key);
  void PreferenceTextSaved(const std::string& key, bool ok);
  bool SyncPreferenceText(const std::string& key, bool present, bool editable, bool composing, bool profile_changed);
  void ResetPreferenceRuleLimit();
  void MorePreferenceRules();
  size_t PreferenceRuleLimit() const;
  void PostRemoveSiteRule(SitePreferenceRule rule,std::string profile_id);
  void RefreshSitePreferences(bool reload = true);
  void SaveSitePreference();
  void SaveGeneralPatch(std::optional<ThemeMode> theme, std::optional<SearchEngine> search, std::optional<bool> animations, std::optional<bool> workspace_names = std::nullopt, std::optional<bool> folders_collapsed = std::nullopt, std::optional<std::pair<bool, int>> time_machine = std::nullopt);
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;
  void RebuildTabs(std::string_view previous, bool catalogue_changed);
  void StopWorkspaceAnimation();
  std::vector<sidebar::Item> CaptureWorkspaceScene(std::string_view id,gfx::Size size,int offset,bool source);
  void ConfigureSidebarContextMenus(views::View* root);
  struct TabListCache {
    std::shared_ptr<SidebarNodeGraphState> node_graph;
    raw_ptr<views::View> root = nullptr;
    raw_ptr<views::View> pins_root = nullptr;
    raw_ptr<views::View> tabs_root = nullptr;
    std::vector<PinnedItem> pins;
    std::vector<ShellTab> tabs;
    SidebarTabPlan tab_plan;
    std::vector<size_t> split_tabs;
    bool split = false;
    std::map<std::string, raw_ptr<views::View>> pin_buttons;
    std::map<size_t, raw_ptr<views::View>> tab_buttons;
    raw_ptr<views::View> pin_pair = nullptr;  // A split placed among the pins.
    raw_ptr<views::View> root_pair = nullptr;  // A split placed among the tabs.
    uint64_t pair_id = 0;  // Rust sidebar.pair node the native pair adapter projects.
    std::string active_pin;
  };
  void ReconcileSidebarNodes(TabListCache& cache, std::string_view workspace, const std::vector<PinnedItem>& pins, const std::vector<ShellTab>& tabs,
                             const std::vector<size_t>& split_tabs = {}, bool split = false);
  void ApplyPinnedFoldersCollapsed(bool collapsed);
  void PositionToast();
  void PositionFind();
  void PositionPrint();
  void TremorToast();
  void SetPaletteLoading(bool loading);
  void ShowWorkspaceRemovedToast(std::string id, std::u16string name);
  std::optional<std::pair<std::string, std::u16string>> pending_removal_;  // Waiting on a page's unload answer.
  void RefreshFolderPeek(std::string workspace, const std::string& folder, views::View* group, bool collapsed);
  void AppendPinnedItems(views::View* root, const std::vector<PinnedItem>& pins,
                         TabListCache& cache, const std::string& workspace,
                         std::map<std::string,std::unique_ptr<views::View>>* retained=nullptr,
                         std::map<uint64_t,std::unique_ptr<views::View>>* retained_groups=nullptr,
                         std::map<std::string,std::unique_ptr<views::View>>* retained_holders=nullptr);
  void ReconcileChromeNodes();
  void RebuildWorkspaceDots();
  void ShowContextMenuForViewImpl(views::View*, const gfx::Point&, ui::mojom::MenuSourceType) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  bool GetAcceleratorForCommandId(int command_id, ui::Accelerator* accelerator) const override;
  void RenameSelectedProfile();
  sidebar::WorkspaceMenuState RustWorkspaceMenuState() const;
  sidebar::FolderMenuState RustFolderMenuState() const;
  bool ShowRustFolderNotice(std::string workspace,std::string folder,std::string title,bool error_only);
  void QueueSplit(std::optional<size_t> tab, std::string pin, std::optional<size_t> slot = std::nullopt);
  std::optional<size_t> SplitDropSlot(gfx::Point point) const;
  // `others`: panes besides the dropped one (default: the shown split).
  void ShowSplitDrop(std::optional<size_t> slot, std::u16string title, std::optional<size_t> others = std::nullopt);
  // A split pane's bar dragged: within the page it moves, onto the sidebar it leaves.
  void HandlePaneDrag(size_t pane, std::optional<gfx::Point> point, bool drop);
  void CreateSplitDrop();
  struct ResolvedRowTarget { bool valid; std::optional<size_t> tab; std::string pin; };
  ResolvedRowTarget ResolveSidebarTarget(std::string_view workspace,std::array<uint64_t,2> identity,std::string_view pin) const;
  void HandleSidebarDrop(std::string workspace,std::optional<size_t> tab,std::array<uint64_t,2> identity,std::string pin,gfx::Point point);
  // Where a row dragged within the sidebar would land, in screen coordinates.
  struct SidebarReorder {
    gfx::Rect indicator;
    bool into_folder = false;
    bool pinned = false;
    std::string parent, before_pin;  // Pinned target: folder and next sibling ("" appends).
    size_t before_tab = 0;           // Tab target: host tab index (or count to append).
  };
  std::optional<SidebarReorder> SidebarReorderAt(gfx::Point screen_point) const;
  // Live drag feedback for `row`, grabbed at `grab` (row-local); nullopt ends the drag.
  void HandleSidebarDrag(views::View* row, gfx::Point grab, std::optional<gfx::Point> screen_point);
  std::optional<SidebarReorder> sidebar_reorder_;
  // The dragged row follows the pointer as a lifted copy; the original dims.
  views::ViewTracker drag_row_;
  gfx::Point drag_grab_;
  std::optional<gfx::Point> drag_point_;
  // Compositor raster: the native rows paint themselves, so an overlay above
  // the sidebar draws the lifted copy, the faded slot and the drop target.
  raw_ptr<views::View> drag_overlay_ = nullptr;
  void PaintSidebarDrag(gfx::Canvas* canvas, views::View* overlay);
 public:
  const views::View* sidebar_drag_row_for_testing() const { return drag_row_.view(); }
  bool sidebar_drop_target_for_testing() const { return sidebar_reorder_.has_value(); }
  views::View* sidebar_drag_overlay_for_testing() const { return drag_overlay_; }
 private:
  void OpenPalette(bool commands);
  void QueueTabCommand(int command);
  bool RustTabCommandEnabled(int command) const;
  // Menu for context_tab_/context_pin_, at a screen point.
  void ShowTabMenu(gfx::Point point, ui::mojom::MenuSourceType source_type);
  std::string ContextPinId() const;
  std::unique_ptr<views::DialogDelegate> pin_url_dialog_delegate_;
  std::unique_ptr<views::Widget> pin_url_dialog_;
  raw_ptr<EditorControl> pin_url_editor_ = nullptr;
  std::optional<size_t> context_tab_;
  std::array<uint64_t,2> context_tab_identity_{};
  std::string context_pin_;
  void BindFormCommand(views::View* view,sidebar::FormCommand command,std::string key={});
  void BindAppearancePreset(views::View* view,bool emoji,size_t index);
  void BindPreferenceCategory(views::View* view,size_t category);
  void DispatchFormCommand(views::View* source,const sidebar::FormCommandBinding& command);
  void SubmitWorkspace(bool add);
  void ImportSelectionChanged();
  void RefreshRustImportControls();
  void DispatchImportPreview(uint64_t epoch);
  void SubmitImport();
  base::OneShotTimer import_preview_timer_;
  void ConfirmRemoveWorkspace(std::string id);
  bool creating_workspace_ = false;
  raw_ptr<views::View> workspace_appearance_ = nullptr;
  raw_ptr<views::View> workspace_more_ = nullptr;
  void RefreshWorkspaceSettings();
  void ToggleWorkspaceCreation();
  // The space whose color and icon the panel edits: the draft while creating one.
  std::string EditedWorkspaceId() const;
  void RebuildWorkspaceProfileRows();
  void PickWorkspaceProfile(std::string profile_id);
  void SaveWorkspaceName();
  void RefreshWorkspaceDanger();
  void RefreshRustWorkspacePresentation();
  void ShowWorkspaceAppearanceError(unsigned kind);
  std::unique_ptr<WorkspaceFormState> workspace_form_;
  std::unique_ptr<PreferencesFormState> preferences_form_;
  std::unique_ptr<ToolsFormState> tools_form_;
  std::unique_ptr<PreferenceChromeState> preferences_chrome_;
  std::unique_ptr<PreferenceChromeState> tools_chrome_;
  void ScheduleToolsChrome();
  bool ToolsChromeVisible() const;
  views::View* AddToolsCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline);
  void SchedulePreferenceChrome();
  bool PreferenceChromeVisible() const;
  views::View* AddPreferenceCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline);
  void SetPreferenceCopyTone(views::View* view,SkColor tone);
  void RefreshProfileControls();
  sidebar::ProfileControls CurrentProfileControls();
  void RebuildProfileRows();
  void LayoutProfileStrips();
  void SelectManagedProfile(std::string profile_id);
  void StartProfileRename(std::string profile_id);
  void RequestProfileDelete(std::string profile_id);
  void CancelProfileRow();
  bool HandleProfileEditorKey(bool rename,const ui::KeyEvent& event);
  void OpenEmojiPicker();
  bool SubmitRustProfileAction(unsigned action);
  void SubmitProfile();
  void DeleteSelectedProfile();
  void RebuildSuggestions();
  void RequestSearch();
  void AddressEdited();
  void ApplyAppearance(bool refresh_theme = true);
  bool HandleAppearanceEditorKey(bool emoji,const ui::KeyEvent& event);
  void RefreshWorkspaceHeading();
  void RestorePageFocus();
  void RunSuggestion(size_t index);
  void ShowAddressError();
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;

  base::FilePath settings_path_;
  Callbacks callbacks_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;
  raw_ptr<views::Widget> widget_ = nullptr;
  raw_ptr<views::View> root_ = nullptr;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<views::View> edge_ = nullptr;
  raw_ptr<views::View> general_root_ = nullptr;
  raw_ptr<PanelViewport> general_scroll_ = nullptr;
  raw_ptr<views::View> theme_picker_ = nullptr;
  raw_ptr<views::View> search_engine_picker_ = nullptr;
  raw_ptr<views::View> sidebar_check_ = nullptr;
  raw_ptr<views::View> workspace_names_check_ = nullptr;
  raw_ptr<views::View> folders_collapsed_check_ = nullptr;
  raw_ptr<views::View> language_picker_ = nullptr;
  raw_ptr<views::View> animations_check_ = nullptr;
  raw_ptr<EditorControl> preferences_search_ = nullptr;
  raw_ptr<views::View> preferences_profile_ = nullptr;
  raw_ptr<views::View> download_directory_label_ = nullptr;
  raw_ptr<views::View> download_directory_button_ = nullptr;
  raw_ptr<views::View> preferences_empty_ = nullptr;
  raw_ptr<views::View> preferences_body_ = nullptr;
  raw_ptr<views::View> preferences_navigation_ = nullptr;
  raw_ptr<views::View> preferences_close_ = nullptr;
  raw_ptr<views::View> preferences_category_picker_ = nullptr;
  struct PreferenceRow {
    raw_ptr<views::View> view;
    std::u16string keywords;
  };
  struct PreferenceSection {
    raw_ptr<views::View> view;
    raw_ptr<views::View> navigation;
    std::vector<PreferenceRow> rows;
  };
  std::vector<PreferenceSection> preference_sections_;
  std::map<std::string, raw_ptr<views::View>> preference_toggles_;
  std::map<std::string, raw_ptr<views::View>> preference_pickers_;
  std::map<std::string, raw_ptr<EditorControl>> preference_textfields_;
  std::map<std::string, raw_ptr<views::View>> preference_save_buttons_;
  std::set<std::string> preference_text_dirty_;
  raw_ptr<EditorControl> site_origin_ = nullptr;
  raw_ptr<views::View> site_type_ = nullptr;
  raw_ptr<views::View> site_policy_ = nullptr;
  std::shared_ptr<SidebarNodeGraphState> site_rule_node_graph_;
  raw_ptr<views::View> site_rules_list_ = nullptr;
  raw_ptr<views::View> site_rules_card_ = nullptr;
  std::vector<SitePreferenceRule> site_preference_rules_;
  size_t site_rules_limit_ = 50;
  raw_ptr<views::View> clear_data_period_ = nullptr;
  raw_ptr<views::View> clear_history_ = nullptr;
  raw_ptr<views::View> clear_cookies_ = nullptr;
  raw_ptr<views::View> clear_cache_ = nullptr;
  raw_ptr<views::View> clear_data_button_ = nullptr;
  raw_ptr<views::View> general_status_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> general_delegate_;
  std::unique_ptr<views::Widget> general_widget_;
  ThemeMode theme_mode_ = ThemeMode::kSystem;
  SearchEngine search_engine_ = SearchEngine::kDuckDuckGo;
  bool workspace_animations_ = true;
  bool time_machine_enabled_ = true;
  int time_machine_days_ = 30;
  raw_ptr<views::View> time_machine_check_ = nullptr;
  raw_ptr<views::View> time_machine_days_picker_ = nullptr;
  raw_ptr<TimeMachineView> time_machine_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> time_machine_delegate_;
  std::unique_ptr<views::Widget> time_machine_widget_;
  bool workspace_names_ = true;
  bool folders_collapsed_ = true;
  std::set<std::string> general_dirty_;
  bool applying_theme_ = false;
  raw_ptr<views::View> peek_host_ = nullptr;
  raw_ptr<views::View> peek_title_ = nullptr;
  raw_ptr<views::View> peek_toolbar_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> peek_delegate_;
  std::unique_ptr<views::Widget> peek_widget_;
  std::unique_ptr<ui::SimpleMenuModel> link_menu_;
  std::unique_ptr<ui::SimpleMenuModel> route_menu_;
  std::unique_ptr<views::MenuRunner> link_menu_runner_;
  GURL context_url_;
  std::string link_source_workspace_;
  std::vector<Workspace> route_workspaces_;
  raw_ptr<views::View> new_tab_button_ = nullptr;
  std::unique_ptr<views::DialogDelegate> rust_folder_dialog_delegate_;
  // Removing a space asks first (design: Dialog), then removes with an undo toast.
  std::unique_ptr<views::DialogDelegate> remove_dialog_delegate_;
  std::unique_ptr<views::Widget> remove_dialog_;
  // The quit-while-downloading prompt or the About dialog, one at a time.
  std::unique_ptr<views::DialogDelegate> quit_dialog_delegate_;
  std::unique_ptr<views::Widget> quit_dialog_;
  std::unique_ptr<views::DialogDelegate> danger_dialog_delegate_;
  std::unique_ptr<views::Widget> danger_dialog_;
  std::unique_ptr<views::WidgetDelegate> welcome_delegate_;
  std::unique_ptr<views::Widget> welcome_widget_;
  std::unique_ptr<views::WidgetDelegate> find_delegate_;
  std::unique_ptr<views::Widget> find_widget_;
  raw_ptr<views::View> find_view_ = nullptr;
  base::RepeatingCallback<gfx::Rect()> find_page_bounds_;
  FindRequest find_request_;
  base::RepeatingClosure find_closed_;
  std::unique_ptr<views::WidgetDelegate> print_delegate_;
  std::unique_ptr<views::Widget> print_widget_;
  raw_ptr<views::View> print_view_ = nullptr;
  base::RepeatingClosure print_closed_;
  // A tab dragged over the page: where it lands in the split, shown on a panel.
  std::unique_ptr<views::WidgetDelegate> drop_delegate_;
  std::unique_ptr<views::Widget> drop_widget_;
  raw_ptr<views::View> drop_view_ = nullptr;
  std::optional<size_t> split_drop_slot_;
  // Dragging a tab over another tab's row: after a moment that tab takes focus
  // and the drop joins its split, on the right.
  base::OneShotTimer drag_hover_timer_;
  std::optional<std::array<uint64_t,2>> drag_hover_tab_;
  bool drag_hover_split_ = false;
  void DragHoverSettled();
  std::unique_ptr<views::Widget> rust_folder_dialog_;
  base::WeakPtr<views::Widget> folder_dialog_;
  std::string folder_parent_id_;
  raw_ptr<views::View> sidebar_root_ = nullptr;
  raw_ptr<views::View> palette_root_ = nullptr;
  raw_ptr<PanelViewport> settings_scroll_ = nullptr;
  raw_ptr<views::View> settings_root_ = nullptr;
  raw_ptr<views::View> settings_title_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<PanelViewport> tab_scroll_ = nullptr;
  raw_ptr<views::View> suggestion_list_ = nullptr;
  raw_ptr<PanelViewport> suggestion_scroll_ = nullptr;
  raw_ptr<views::View> url_pill_ = nullptr;
  raw_ptr<views::View> workspace_name_ = nullptr;
  raw_ptr<views::View> profile_name_ = nullptr;
  std::shared_ptr<SidebarNodeGraphState> chrome_node_graph_;
  std::array<raw_ptr<views::View>, 3> nav_buttons_ = {};
  raw_ptr<views::View> copy_url_button_ = nullptr;
  raw_ptr<views::View> sidebar_footer_=nullptr;
  raw_ptr<PanelViewport> workspace_scroll_=nullptr;
  raw_ptr<views::View> workspace_dots_ = nullptr;
  raw_ptr<views::View> add_button_ = nullptr;  // The footer's „+”: tab, folder or space.
  raw_ptr<views::View> downloads_button_ = nullptr;  // The footer's Downloads ring.
  int downloads_active_ = 0;
  double downloads_fraction_ = 0;
  std::u16string downloads_newest_;
  bool downloads_done_ = false;  // ✓ after a finish, until the timer clears it.
  base::OneShotTimer downloads_done_timer_;
  base::RepeatingTimer downloads_tremor_timer_;
  int downloads_tremor_step_ = 0;
  base::RepeatingTimer downloads_spin_timer_;  // An unknown size spins the arc.
  int downloads_spin_ = 0;
  // The start cue: a flying arrow, then (sidebar away) a pill in the corner.
  std::unique_ptr<views::WidgetDelegate> download_cue_delegate_;
  std::unique_ptr<views::Widget> download_cue_widget_;
  raw_ptr<views::View> download_cue_ = nullptr;  // DownloadCueView
  base::RepeatingTimer download_cue_timer_;
  base::OneShotTimer download_cue_hide_timer_;
  base::TimeTicks download_cue_start_;
  gfx::Point download_cue_from_;
  bool download_cue_to_pill_ = false;
  void UpdateDownloadsButton();
  void StepDownloadCue();
  void LandDownloadCue();
  void HideDownloadCue();
  void TremorDownloads();
  gfx::Rect DownloadPillBounds() const;
  gfx::Point DownloadCueTarget() const;
  std::unique_ptr<ui::SimpleMenuModel> add_menu_;
  std::unique_ptr<views::MenuRunner> add_menu_runner_;
  std::map<std::string, raw_ptr<views::View>> workspace_buttons_;
  std::string context_workspace_id_;
  std::string new_profile_workspace_id_;
  std::vector<ProfileInfo> menu_profiles_;
  std::unique_ptr<ui::SimpleMenuModel> workspace_menu_;
  std::unique_ptr<ui::SimpleMenuModel> profile_menu_;
  std::unique_ptr<ui::SimpleMenuModel> color_menu_;
  std::unique_ptr<ui::SimpleMenuModel> emoji_menu_;
  std::unique_ptr<views::MenuRunner> workspace_menu_runner_;
  std::unique_ptr<ui::SimpleMenuModel::Delegate> app_menu_delegate_;
  std::unique_ptr<ui::SimpleMenuModel> app_menu_;
  std::unique_ptr<views::MenuRunner> app_menu_runner_;
  std::map<std::string, TabListCache> tab_lists_;
  void WarmNextSidebar();
  std::vector<std::pair<std::string, std::vector<PinnedItem>>> sidebar_warm_queue_;
  base::OneShotTimer sidebar_warm_timer_;
  bool HandleWorkspaceScroll(float x, float y, bool began, bool ended, bool momentum, base::TimeTicks time, bool phase_less = false);
  std::unique_ptr<WorkspaceSwipeState> workspace_swipe_;
  bool swipe_committing_ = false;
  std::string swipe_source_, swipe_commit_target_;
  std::vector<std::string> swipe_workspace_ids_;
  raw_ptr<views::View> swipe_overlay_ = nullptr;
  base::OneShotTimer swipe_idle_timer_, swipe_settle_timer_;
  void UpdateWorkspaceSwipePreview();
  void FinishWorkspaceSwipe();
  void CancelWorkspaceSwipe(bool reset_state = true);
  size_t workspace_animation_generation_ = 0;
  bool workspace_animation_pending_ = false;
  raw_ptr<views::View> workspace_switch_overlay_ = nullptr;
  base::TimeTicks swipe_last_time_;
  base::TimeTicks last_native_scroll_time_;
  size_t pinned_list_build_count_ = 0;
  size_t sidebar_context_visit_count_ = 0;
  std::string update_version_;
  base::RepeatingClosure install_update_;
  size_t pin_visibility_update_count_ = 0;
  raw_ptr<views::View> profile_picker_ = nullptr;
  raw_ptr<EditorControl> workspace_input_ = nullptr;
  // Space panel (design: space settings): the name saves itself, profiles are a
  // list of rows, a new space may make its profile on the spot.
  raw_ptr<views::View> workspace_saved_ = nullptr;
  raw_ptr<views::View> workspace_profile_list_ = nullptr;
  std::vector<raw_ptr<views::View>> workspace_profile_rows_;
  std::vector<std::string> workspace_profile_row_ids_;  // "" is „Nowy profil”.
  raw_ptr<views::View> new_profile_strip_ = nullptr;
  raw_ptr<EditorControl> new_profile_input_ = nullptr;
  raw_ptr<views::View> new_profile_import_ = nullptr;
  raw_ptr<views::View> workspace_actions_ = nullptr;
  raw_ptr<views::View> workspace_danger_text_ = nullptr;
  bool new_profile_choice_ = false;
  base::OneShotTimer workspace_name_timer_;
  raw_ptr<views::View> add_workspace_ = nullptr;
  raw_ptr<views::View> edit_workspace_ = nullptr;
  raw_ptr<views::View> remove_workspace_ = nullptr;
  raw_ptr<views::View> configuration_error_ = nullptr;
  raw_ptr<PanelViewport> import_scroll_ = nullptr;
  raw_ptr<views::View> import_root_ = nullptr;
  raw_ptr<views::View> import_status_ = nullptr;
  std::unique_ptr<ImportFormState> import_form_;
  raw_ptr<views::View> import_browser_picker_ = nullptr;
  raw_ptr<views::View> import_bookmarks_ = nullptr;
  raw_ptr<views::View> import_profiles_ = nullptr;
  raw_ptr<views::View> import_history_ = nullptr;
  raw_ptr<views::View> import_cookies_ = nullptr;
  raw_ptr<views::View> apply_import_ = nullptr;
  raw_ptr<views::View> dismiss_import_ = nullptr;
  bool import_can_apply_ = false;
  bool import_busy_ = false;
  bool import_applying_ = false;
  SidebarTree sidebar_tree_;
  raw_ptr<views::View> profiles_root_ = nullptr;
  raw_ptr<views::View> profile_list_ = nullptr;
  std::vector<raw_ptr<views::View>> profile_rows_;
  std::vector<std::string> profile_row_ids_;
  raw_ptr<views::View> profile_rename_strip_ = nullptr;
  raw_ptr<EditorControl> profile_rename_input_ = nullptr;
  raw_ptr<views::View> profile_confirm_strip_ = nullptr;
  raw_ptr<views::View> profile_confirm_text_ = nullptr;
  raw_ptr<views::View> profile_create_status_ = nullptr;
  std::string renaming_profile_id_;
  // 0: the last profile action came from the new-profile field; else a row.
  unsigned last_profile_action_ = 1;
  raw_ptr<EditorControl> profile_name_input_ = nullptr;
  raw_ptr<views::View> create_profile_ = nullptr;
  raw_ptr<views::View> rename_profile_ = nullptr;
  raw_ptr<views::View> delete_profile_ = nullptr;
  raw_ptr<views::View> profile_status_ = nullptr;
  std::unique_ptr<ProfileFormState> profile_form_;
  raw_ptr<EditorControl> address_ = nullptr;
  raw_ptr<views::View> color_presets_ = nullptr;
  // The panel header's icon tile.
  raw_ptr<views::View> settings_icon_ = nullptr;
  raw_ptr<EditorControl> emoji_input_ = nullptr;
  raw_ptr<views::View> address_error_ = nullptr;
  raw_ptr<views::View> color_error_ = nullptr;
  // Workspace color wheel: the hue ring, then the saturation x tone field.
  std::array<raw_ptr<views::View>, 2> color_wheel_{};
  std::unique_ptr<views::WidgetDelegate> import_delegate_;
  std::unique_ptr<views::Widget> import_widget_;
  std::unique_ptr<views::WidgetDelegate> profiles_delegate_;
  std::unique_ptr<views::Widget> profiles_widget_;
  std::unique_ptr<views::WidgetDelegate> sidebar_delegate_;
  std::unique_ptr<views::WidgetDelegate> address_delegate_;
  std::unique_ptr<views::WidgetDelegate> settings_delegate_;
  std::unique_ptr<views::Widget> sidebar_widget_;
  std::unique_ptr<views::Widget> address_widget_;
  std::unique_ptr<views::Widget> settings_widget_;
  ShellState state_;
  std::vector<Suggestion> suggestions_;
  std::shared_ptr<SidebarNodeGraphState> palette_node_graph_;
  std::vector<PaletteResult> search_results_;
  std::u16string search_query_;
  base::OneShotTimer search_timer_;
  bool command_palette_ = false;
  std::u16string suggestions_text_;
  // Violet and mint (design.rs WorkspaceColor::PRESETS); any stored hex is kept.
  std::map<std::string, SkColor> colors_ = {{"work", 0xFF928FD4}, {"personal", 0xFF4FAC8F}};
  std::set<std::string> color_dirty_;
  std::map<std::string, std::u16string> emoji_;
  std::set<std::string> emoji_dirty_;
  bool pin_dirty_ = false;
  int sidebar_width_ = 248;
  int sidebar_resize_start_ = 248;
  bool sidebar_resizing_ = false;
  void HideSidebar();
  void PollSidebar();
  void AnimateSidebar(bool show);
  void StepSidebarAnimation();
  base::RepeatingTimer sidebar_hover_timer_;
  base::RepeatingTimer sidebar_slide_timer_;
  base::TimeTicks sidebar_left_at_;
  base::TimeTicks sidebar_slide_started_;
  double sidebar_slide_from_ = 0;
  double sidebar_slide_value_ = 0;
  bool sidebar_slide_show_ = false;
  bool sidebar_hover_armed_ = false;
  bool content_fullscreen_ = false;
  base::RepeatingTimer reload_timer_;
  base::TimeTicks reload_started_;
  void StepReloadAnimation();
  bool sidebar_width_dirty_ = false;
  bool pinned_ = true;
  bool new_tab_ = false;
  bool appearance_ready_ = false;
  int pending_writes_ = 0;
  base::WeakPtrFactory<WorkspaceShell> weak_factory_{this};
};
}
#endif
