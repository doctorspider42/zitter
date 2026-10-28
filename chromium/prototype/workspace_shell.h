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
#include "url/gurl.h"

namespace zitter::sidebar { struct Item; struct PreferenceCatalogue; struct WorkspaceMenuState; struct FolderMenuState; enum class FormCommand : uint32_t; struct FormCommandBinding; }
namespace ui { class AcceleratorTarget; }
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

// Top band above the sidebar; its first 28 px align with macOS's titlebar,
// where the native traffic lights sit.
inline constexpr int kShellHeaderHeight = 38;

enum class ThemeMode { kSystem, kLight, kDark };
enum class SearchEngine { kDuckDuckGo, kGoogle, kBing };

enum class PageCommand { kBack, kForward, kReload };

struct ShellTab {
  std::u16string title;
  GURL url;
  size_t index = 0;
  std::string pin_id;
  std::array<uint64_t,2> identity{};  // Opaque 128-bit row token, independent of lazy WebContents.
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
  bool split = false;          // A pair exists, even while another tab is shown.
  bool split_visible = false;  // The pair is on screen.
  bool split_vertical = false;
  bool right_active = false;
  std::array<std::optional<size_t>, 2> split_tabs;
  bool can_go_back = false;
  bool can_go_forward = false;
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
    base::RepeatingCallback<void(std::u16string, size_t)> query_history;
    base::RepeatingCallback<TimeMachineState()> time_machine;
    base::RepeatingCallback<bool()> operation_pending;
    base::RepeatingCallback<bool(std::string, bool)> save_snapshot;
    base::RepeatingCallback<bool(std::string)> restore_snapshot, delete_snapshot, reopen_closed, sleep_workspace;
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
    base::RepeatingCallback<void(bool)> focus_pane;
    base::RepeatingCallback<void(double, bool)> split_ratio;
    base::RepeatingCallback<bool(size_t)> split_tab;
    // Closes Split View, keeping the given tab of the pair on screen.
    base::RepeatingCallback<void(size_t)> unsplit;
    base::RepeatingClosure swap_split;
    base::RepeatingCallback<void(bool)> split_vertical;
    base::RepeatingCallback<void(std::string)> split_pin;
  };

  WorkspaceShell(base::FilePath settings_path, Callbacks callbacks);
  ~WorkspaceShell() override;
  // Client view that turns empty frame/header space into a window drag area.
  static views::WidgetDelegate::ClientViewFactory ClientViewFactory();
  std::unique_ptr<views::View> Build();
  views::View* content_host() const { return content_host_; }
  void Attach(views::Widget* widget, ui::AcceleratorTarget* target);
  void SetPanes(views::View* left, views::View* right, double ratio, bool vertical = false);
  // Header buttons above each half of a visible split.
  void ShowSplitPaneMenu(bool right, gfx::Point screen_point);
  void CloseSplitPane(bool right);
  views::View* split_pane_button(bool right, bool close) const;
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
  void SetSearchResults(std::u16string query, std::string profile_id, std::vector<PaletteResult> results);
  const std::vector<PaletteResult>& search_results() const { return search_results_; }
  const std::vector<PaletteResult>& suggestions() const { return suggestions_; }
  void ShowSidebar();
  void ToggleSidebar();
  void SetPinned(bool pinned);
  // Drag delta from the sidebar's right-edge handle; persisted when done.
  void ResizeSidebar(int amount, bool done);
  int sidebar_width() const { return sidebar_width_; }
  bool CloseOverlays(bool keep_sidebar = false);
  void OpenGeneralSettings();
  void OpenTools(int page);  // 0: Time Machine, 1: shortcuts, 2: search providers, 3: history.
  void RefreshHistory(bool more = false);
  void SetHistoryResults(std::u16string query, std::string profile_id, size_t limit,
      std::vector<BrowsingHistoryEntry> entries, bool more, bool error);
  bool history_visible() const;
  bool history_loaded() const;
  views::View* history_results_view() const { return history_list_; }
  EditorControl* history_search() const { return history_search_; }
  views::View* history_period() const { return history_period_; }
  const std::vector<BrowsingHistoryEntry>& history_entries() const { return history_entries_; }
  void SyncShortcuts() { RefreshKeyRegistrations(); }
  bool CaptureShortcut(const ui::Accelerator& accelerator);
  std::string ShortcutFor(const ui::Accelerator& accelerator) const;
  bool SetShortcut(std::string id, std::string chord);
  bool UpsertSearchProvider(SearchProvider provider, std::string previous = {});
  bool RemoveSearchProvider(std::string keyword);
  bool SetDefaultSearchProvider(std::string keyword);
  GURL ResolveInput(std::string text, std::u16string* provider_name = nullptr) const;
  const BrowserTools& browser_tools() const { return tools_; }
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
  bool workspace_animations() const { return workspace_animations_; }
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
  // 0 untyped/retired, 1 checkbox, 2 switch, 3 selector, 4 workspace, 5 history row, 6 palette row, 7 site-rule row, 8 form command, 9 tab row, 10 pin row; local graph model diagnostics.
  views::View* site_rule_results_for_testing() const{return site_rules_list_;}
  void RefreshSiteRulesForTesting(bool reload=true){RefreshSitePreferences(reload);}
  uint32_t control_model_kind_for_testing(views::View* view) const;
  uint64_t scene_parent_id_for_testing(views::View* view) const;
  views::View* header_action_for_testing(size_t index) const;
  views::View* sidebar_action_for_testing(size_t index) const{return index==0?url_pill_.get():index==1?new_tab_button_.get():nullptr;}
  views::View* footer_action_for_testing(size_t index) const{return index==0?pin_button_.get():index==1?workspace_settings_button_.get():nullptr;}
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
  void OpenImport();
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
  void SetProfileStatus(ProfileStatusEvent event);
  void SetProfileStatus(std::u16string message);
  views::Widget* profiles_widget() const { return profiles_widget_.get(); }
  EditorControl* profile_name_input() const { return profile_name_input_; }
  views::View* managed_profile_picker() const { return managed_profile_picker_; }
  std::optional<size_t> profile_picker_selection(bool managed) const;
  size_t profile_picker_count() const;
  void SetProfilePickerSelectionForTesting(bool managed,size_t index);
  void ActivateProfilePickerSelectionForTesting(bool managed,size_t index);
  bool profile_picker_metal_visible(bool managed) const;
  bool profile_panel_contains_scene_text(std::string_view text) const;
  bool workspace_panel_contains_scene_text(std::string_view text) const;
  views::View* workspace_panel_for_testing() const{return settings_root_;}
  views::View* profile_panel_for_testing() const{return profiles_root_;}
  views::Widget* remove_workspace_dialog() const { return remove_workspace_dialog_.get(); }
  views::View* workspace_notice_for_testing() const;
  bool workspace_notice_metal_visible() const;
  bool appearance_metal_visible(bool emoji) const;
  bool form_actions_contains_scene_text(bool profiles,std::string_view text) const;
  size_t form_action_canvases_visible_for_testing(bool profiles) const;
  bool appearance_contains_scene_text(std::string_view text) const;
  gfx::Rect appearance_scene_bounds_for_testing(bool emoji) const;
  bool workspace_notice_contains_scene_text(std::string_view text) const;
  std::u16string workspace_notice_error_for_testing() const;
  size_t workspace_notice_line_count_for_testing() const;
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
  views::Widget* address_widget() const { return address_widget_.get(); }
  views::View* tab_button(size_t index) const;
  uint64_t sidebar_node_id_for_testing(views::View* row) const;
  // Rust split pair node: {id, parent id, visible}; zeros without a Rust graph.
  std::array<uint64_t, 3> sidebar_pair_for_testing() const;
  views::Widget* sidebar_widget() const { return sidebar_widget_.get(); }
  EditorControl* workspace_input() const { return workspace_input_; }
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
  views::View* profile_picker() const { return profile_picker_; }
  EditorControl* color_input() const { return color_input_; }
  EditorControl* address_input() const { return address_; }
  // Application key policy; the platform input method owns composing keys.
  bool HandleAddressKey(const ui::KeyEvent& event);
  bool HandlePreferenceEditorKey(EditorControl* sender,const ui::KeyEvent& event);
  bool HandleToolsEditorKey(EditorControl* sender,const ui::KeyEvent& event);
  EditorControl* snapshot_input() const { return snapshot_name_; }
  EditorControl* timeline_filter_input() const { return timeline_filter_; }
  EditorControl* provider_input(size_t index) const {
    return std::array<EditorControl*,3>{provider_name_,provider_keyword_,provider_url_}.at(index);
  }

 private:
  void ManagedProfileSelectionChanged();
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
  std::array<std::unique_ptr<SidebarMetal>,3> metal_split_;
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
  std::u16string ToolsStatusText(int status, std::u16string native) const;
  std::u16string ToolsHistoryQuery() const;
  std::u16string ToolsTimelineQuery() const;
  void ResetToolsForm();
  void RefreshTools();
  void BuildRustToolsForm();
  void BuildRustPreferenceRows(const sidebar::PreferenceCatalogue& catalogue);
  void RefreshTimelineItems();
  void RefreshToolsViewport();
  void SetHistoryMoreVisible(bool visible);
  void RefreshKeyRegistrations();
  void SaveTools();
  void ToolsAction(int action,std::optional<std::string> choice_key=std::nullopt);
  BrowserTools tools_;
  bool tools_ready_ = false;
  bool keys_dirty_ = false, engines_dirty_ = false;
  raw_ptr<ui::AcceleratorTarget> accelerator_target_ = nullptr;
  raw_ptr<views::View> tools_root_ = nullptr;
  raw_ptr<views::View> tools_body_ = nullptr;
  raw_ptr<views::View> tools_close_ = nullptr;
  raw_ptr<views::View> tools_title_ = nullptr;
  raw_ptr<views::View> tools_status_ = nullptr;
  raw_ptr<EditorControl> snapshot_name_ = nullptr;
  raw_ptr<EditorControl> timeline_filter_ = nullptr;
  raw_ptr<EditorControl> shortcut_chord_ = nullptr;
  raw_ptr<EditorControl> provider_name_ = nullptr;
  raw_ptr<EditorControl> provider_keyword_ = nullptr;
  raw_ptr<EditorControl> provider_url_ = nullptr;
  raw_ptr<views::View> tools_picker_ = nullptr;
  raw_ptr<views::View> closed_picker_ = nullptr;
  raw_ptr<PanelViewport> tools_scroll_ = nullptr;
  std::vector<std::string> snapshot_ids_, closed_ids_;
  raw_ptr<EditorControl> history_search_ = nullptr;
  raw_ptr<views::View> history_period_ = nullptr;
  raw_ptr<views::View> history_list_ = nullptr;
  raw_ptr<views::View> history_more_ = nullptr;
  raw_ptr<views::View> history_clear_ = nullptr;
  std::shared_ptr<SidebarNodeGraphState> history_node_graph_;
  std::vector<BrowsingHistoryEntry> history_entries_;
  // Views-only UI state; the opt-in backend reads/writes ToolsForm instead.
  size_t history_limit_ = 200;
  bool history_loaded_ = false;
  bool browsing_data_busy_ = false;
  base::OneShotTimer history_timer_;
  int tools_page_ = 0;
  bool recording_shortcut_ = false;
  uint64_t tools_generation_ = 0;
  std::u16string history_query_;
  std::u16string timeline_query_;
  void ToolsEditorEdited(EditorControl* sender);
  void PostToolsAction(int action);
  void RefreshToolsChoiceFields();
  void SetShortcutRecording(bool recording);
  std::unique_ptr<views::WidgetDelegate> tools_delegate_;
  std::unique_ptr<views::Widget> tools_widget_;
  void RequestFolderName(std::string workspace, std::string parent, std::string renamed);
  void ApplyTheme();
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
  void SaveGeneralPatch(std::optional<ThemeMode> theme, std::optional<SearchEngine> search, std::optional<bool> animations);
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
    std::array<std::optional<size_t>, 2> split_tabs;
    bool split = false;
    std::map<std::string, raw_ptr<views::View>> pin_buttons;
    std::map<size_t, raw_ptr<views::View>> tab_buttons;
    raw_ptr<views::View> pin_pair = nullptr;  // A split placed among the pins.
    raw_ptr<views::View> root_pair = nullptr;  // A split placed among the tabs.
    uint64_t pair_id = 0;  // Rust sidebar.pair node the native pair adapter projects.
    std::string active_pin;
  };
  void ReconcileSidebarNodes(TabListCache& cache, std::string_view workspace, const std::vector<PinnedItem>& pins, const std::vector<ShellTab>& tabs,
                             std::array<std::optional<size_t>, 2> split_tabs = {}, bool split = false);
  void AppendPinnedItems(views::View* root, const std::vector<PinnedItem>& pins,
                         TabListCache& cache, const std::string& workspace,
                         std::map<std::string,std::unique_ptr<views::View>>* retained=nullptr);
  void ReconcileChromeNodes();
  void RebuildWorkspaceDots();
  void ShowContextMenuForViewImpl(views::View*, const gfx::Point&, ui::mojom::MenuSourceType) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void RenameSelectedProfile();
#if BUILDFLAG(IS_MAC)
  sidebar::WorkspaceMenuState RustWorkspaceMenuState() const;
  sidebar::FolderMenuState RustFolderMenuState() const;
#endif
  bool ShowRustFolderNotice(std::string workspace,std::string folder,std::string title,bool error_only);
  void QueueSplit(std::optional<size_t> tab, std::string pin);
  struct ResolvedRowTarget { bool valid; std::optional<size_t> tab; std::string pin; };
  ResolvedRowTarget ResolveSidebarTarget(std::string_view workspace,std::array<uint64_t,2> identity,std::string_view pin) const;
  void HandleSidebarDrop(std::string workspace,std::optional<size_t> tab,std::array<uint64_t,2> identity,std::string pin,gfx::Point point);
  void QueueTabCommand(int command);
  bool RustTabCommandEnabled(int command) const;
  // Menu for context_tab_/context_pin_, at a screen point.
  void ShowTabMenu(gfx::Point point, ui::mojom::MenuSourceType source_type);
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
  std::unique_ptr<views::DialogDelegate> remove_workspace_dialog_delegate_;
  std::unique_ptr<views::Widget> remove_workspace_dialog_;
  void RefreshWorkspaceSettings();
  void ToggleWorkspaceCreation();
  void RefreshRustWorkspacePresentation();
  void ShowWorkspaceAppearanceError(unsigned kind);
  bool ShowRustWorkspaceNotice(std::string workspace,std::string title);
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
  bool SubmitRustProfileAction(unsigned action);
  void SubmitProfile();
  void DeleteSelectedProfile();
  void RebuildSuggestions();
  void RequestSearch();
  void AddressEdited();
  void ApplyAppearance(bool refresh_theme = true);
  void ApplyHexColor();
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
  size_t preference_category_ = 0;
  std::map<std::string, raw_ptr<EditorControl>> preference_textfields_;
  std::map<std::string, raw_ptr<views::View>> preference_save_buttons_;
  std::set<std::string> preference_text_dirty_;
  std::string preferences_profile_id_;
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
  raw_ptr<views::View> pin_button_ = nullptr;
  std::array<raw_ptr<views::View>, 3> nav_buttons_ = {};
  raw_ptr<views::View> sidebar_footer_=nullptr;
  raw_ptr<PanelViewport> workspace_scroll_=nullptr;
  raw_ptr<views::View> workspace_settings_button_=nullptr;
  raw_ptr<views::View> workspace_dots_ = nullptr;
  std::map<std::string, raw_ptr<views::View>> workspace_buttons_;
  std::string context_workspace_id_;
  std::string new_profile_workspace_id_;
  std::vector<ProfileInfo> menu_profiles_;
  std::unique_ptr<ui::SimpleMenuModel> workspace_menu_;
  std::unique_ptr<ui::SimpleMenuModel> profile_menu_;
  std::unique_ptr<ui::SimpleMenuModel> color_menu_;
  std::unique_ptr<ui::SimpleMenuModel> emoji_menu_;
  std::unique_ptr<views::MenuRunner> workspace_menu_runner_;
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
  size_t pin_visibility_update_count_ = 0;
  raw_ptr<views::View> profile_picker_ = nullptr;
  raw_ptr<EditorControl> workspace_input_ = nullptr;
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
  raw_ptr<views::View> managed_profile_picker_ = nullptr;
  raw_ptr<EditorControl> profile_name_input_ = nullptr;
  raw_ptr<views::View> create_profile_ = nullptr;
  raw_ptr<views::View> rename_profile_ = nullptr;
  raw_ptr<views::View> delete_profile_ = nullptr;
  raw_ptr<views::View> profile_status_ = nullptr;
  std::unique_ptr<ProfileFormState> profile_form_;
  bool confirm_profile_deletion_ = false;
  raw_ptr<EditorControl> address_ = nullptr;
  raw_ptr<EditorControl> color_input_ = nullptr;
  raw_ptr<EditorControl> emoji_input_ = nullptr;
  raw_ptr<views::View> address_error_ = nullptr;
  raw_ptr<views::View> color_error_ = nullptr;
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
  std::map<std::string, SkColor> colors_ = {{"work", 0xFF9E91DC}, {"personal", 0xFF84B6A4}};
  std::set<std::string> color_dirty_;
  std::map<std::string, std::u16string> emoji_;
  std::set<std::string> emoji_dirty_;
  bool pin_dirty_ = false;
  int sidebar_width_ = 248;
  int sidebar_resize_start_ = 248;
  bool sidebar_resizing_ = false;
  bool sidebar_width_dirty_ = false;
  bool pinned_ = true;
  bool new_tab_ = false;
  bool appearance_ready_ = false;
  int pending_writes_ = 0;
  base::WeakPtrFactory<WorkspaceShell> weak_factory_{this};
};
}
#endif
