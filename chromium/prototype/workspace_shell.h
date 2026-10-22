#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_

#include <array>
#include <memory>
#include <map>
#include <set>
#include "zitter/core/workspace_model.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/sidebar_tree.h"
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
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/events/event_handler.h"
#include "ui/views/context_menu_controller.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/native_theme/native_theme_observer.h"
#include "url/gurl.h"

namespace ui { class AcceleratorTarget; }
namespace views {
class Button;
class Combobox;
class Checkbox;
class ToggleButton;
class MdTextButton;
class Label;
class MenuRunner;
class ScrollView;
class Textfield;
class View;
class Widget;
}

namespace zitter::prototype {

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
                             public views::TextfieldController,
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
  views::Button* split_pane_button(bool right, bool close) const;
  views::View* peek_host() const { return peek_host_; }
  views::Widget* peek_widget() const { return peek_widget_.get(); }
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
  bool history_loaded() const { return history_loaded_; }
  views::View* history_results_view() const { return history_list_; }
  views::Textfield* history_search() const { return history_search_; }
  views::Combobox* history_period() const { return history_period_; }
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
  views::Textfield* shortcut_input() const { return shortcut_chord_; }
  views::Combobox* tools_selection() const { return tools_picker_; }
  void SetThemeMode(ThemeMode mode);
  ThemeMode theme_mode() const { return theme_mode_; }
  void SetSearchEngine(SearchEngine engine);
  SearchEngine search_engine() const { return search_engine_; }
  void SetWorkspaceAnimations(bool enabled);
  bool workspace_animations() const { return workspace_animations_; }
  void AnimateWorkspaceSwitch(std::string_view previous);
  bool workspace_animation_running() const;
  bool workspace_animation_pending() const { return workspace_animation_pending_; }
  bool workspace_swipe_visible() const { return swipe_overlay_ != nullptr; }
  double workspace_swipe_position() const { return swipe_position_; }
  size_t workspace_swipe_preview_count() const;
  views::Widget* general_widget() const { return general_widget_.get(); }
  void SetPreferencesStatus(std::u16string text, bool error = false);
  void SetOperationStatus(std::u16string text);
  void SetBrowsingDataBusy(bool busy);
  views::Textfield* preference_textfield(std::string key) const { return preference_textfields_.at(key); }
  views::Combobox* preference_picker(std::string key) const { return preference_pickers_.at(key); }
  views::Textfield* site_origin_input() const { return site_origin_; }
  views::Textfield* preferences_search() const { return preferences_search_; }
  views::ToggleButton* preference_toggle(std::string key) const { return preference_toggles_.at(key); }
  views::Combobox* theme_picker() const { return theme_picker_; }
  views::Combobox* search_engine_picker() const { return search_engine_picker_; }
  bool address_visible() const;
  bool sidebar_visible() const;
  bool metal_palette_visible() const;
  bool metal_palette_contains_text_for_testing(std::string_view text) const;
  gfx::Rect metal_palette_screen_bounds_for_testing() const;
  size_t palette_selection_for_testing() const;
  views::View* palette_results_view() const{return suggestion_list_;}
  views::ScrollView* palette_scroll_view() const{return suggestion_scroll_;}
  bool metal_sidebar_visible() const;
  bool metal_sidebar_grip_visible() const;
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
  bool import_visible() const;
  BrowserImportOptions import_options() const;
  views::Combobox* import_browser_picker() const { return import_browser_picker_; }
  views::Checkbox* import_bookmarks() const { return import_bookmarks_; }
  views::Button* import_close() const { return dismiss_import_; }
  views::Checkbox* import_history() const { return import_history_; }
  views::Checkbox* import_cookies() const { return import_cookies_; }
  views::Checkbox* import_profiles() const { return import_profiles_; }
  views::Widget* import_widget() const { return import_widget_.get(); }
  void SetProfileStatus(std::u16string message);
  views::Widget* profiles_widget() const { return profiles_widget_.get(); }
  views::Textfield* profile_name_input() const { return profile_name_input_; }
  views::Combobox* managed_profile_picker() const { return managed_profile_picker_; }
  views::Widget* remove_workspace_dialog() const { return remove_workspace_dialog_.get(); }
  views::Widget* settings_widget() const { return settings_widget_.get(); }
  bool pinned() const { return pinned_; }
  void SetWorkspaceColor(std::string_view id, SkColor color);
  SkColor workspace_color(std::string_view id) const;
  // One emoji grapheme shown instead of the workspace dot; empty clears it.
  void SetWorkspaceEmoji(std::string_view id, std::u16string emoji);
  std::u16string workspace_emoji(std::string_view id) const;
  views::Textfield* emoji_input() const { return emoji_input_; }
  bool appearance_ready() const { return appearance_ready_; }
  bool appearance_saved() const { return pending_writes_ == 0; }
  views::Widget* address_widget() const { return address_widget_.get(); }
  views::Button* tab_button(size_t index) const;
  views::Widget* sidebar_widget() const { return sidebar_widget_.get(); }
  views::Textfield* workspace_input() const { return workspace_input_; }
  views::Button* workspace_button(std::string_view id) const;
  size_t pinned_list_build_count() const { return pinned_list_build_count_; }
  size_t sidebar_context_visit_count() const { return sidebar_context_visit_count_; }
  size_t pin_visibility_update_count() const { return pin_visibility_update_count_; }
  views::ScrollView* tab_scroll_view() const { return tab_scroll_; }
  void BuildWorkspaceMenu(std::string id);
  void BuildFolderMenu(std::string parent = {});
  void RequestNewFolder(std::string workspace, std::string parent = {});
  void RequestRenameFolder(std::string workspace, std::string folder);
  void RequestDeleteFolder(std::string workspace, std::string folder);
  void ShowFolderSaveError();
  void WarmWorkspaceSidebars(std::map<std::string, std::vector<PinnedItem>> pins);
  bool sidebars_warmed() const { return sidebar_warm_queue_.empty() && !sidebar_warm_timer_.IsRunning(); }
  void OnScrollEvent(ui::ScrollEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
  views::Widget* folder_dialog() const { return folder_dialog_.get(); }
  views::View* sidebar_view() const { return sidebar_root_; }
  views::View* tab_list_view() const { return tab_list_; }
  ui::SimpleMenuModel* workspace_menu() const { return workspace_menu_.get(); }
  views::Combobox* profile_picker() const { return profile_picker_; }
  views::Textfield* color_input() const { return color_input_; }
  views::Textfield* address_input() const { return address_; }

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
  std::unique_ptr<SidebarMetal> metal_palette_;
  bool metal_palette_pending_=false;
  base::CallbackListSubscription metal_palette_scroll_subscription_;
  std::unique_ptr<views::ScrollView::ScopedScrollSynchronizer> metal_palette_scroll_synchronizer_;
  void ScheduleMetalSidebar();
  void UpdateMetalSidebar();
  std::array<std::unique_ptr<SidebarMetal>,3> metal_split_;
  bool metal_split_pending_=false;
  std::unique_ptr<SidebarMetal> metal_sidebar_grip_;
  std::unique_ptr<SidebarMetal> metal_header_;
  bool metal_header_pending_=false;
  raw_ptr<views::View> header_view_=nullptr;
  raw_ptr<views::Button> header_sidebar_button_=nullptr;
  std::unique_ptr<SidebarMetal> metal_sidebar_;
  bool metal_sidebar_pending_ = false;
  base::CallbackListSubscription metal_workspace_scroll_subscription_;
  std::unique_ptr<views::ScrollView::ScopedScrollSynchronizer> metal_workspace_scroll_synchronizer_;
  base::CallbackListSubscription metal_scroll_subscription_;
  std::unique_ptr<views::ScrollView::ScopedScrollSynchronizer> metal_scroll_synchronizer_;
  void RefreshTools();
  void RefreshTimelineItems();
  void RefreshKeyRegistrations();
  void SaveTools();
  void ToolsAction(int action);
  BrowserTools tools_;
  bool tools_ready_ = false;
  bool keys_dirty_ = false, engines_dirty_ = false;
  raw_ptr<ui::AcceleratorTarget> accelerator_target_ = nullptr;
  raw_ptr<views::View> tools_root_ = nullptr;
  raw_ptr<views::View> tools_body_ = nullptr;
  raw_ptr<views::Label> tools_title_ = nullptr;
  raw_ptr<views::Label> tools_status_ = nullptr;
  raw_ptr<views::Textfield> snapshot_name_ = nullptr;
  raw_ptr<views::Textfield> timeline_filter_ = nullptr;
  raw_ptr<views::Textfield> shortcut_chord_ = nullptr;
  raw_ptr<views::Textfield> provider_name_ = nullptr;
  raw_ptr<views::Textfield> provider_keyword_ = nullptr;
  raw_ptr<views::Textfield> provider_url_ = nullptr;
  raw_ptr<views::Combobox> tools_picker_ = nullptr;
  raw_ptr<views::Combobox> closed_picker_ = nullptr;
  raw_ptr<views::ScrollView> tools_scroll_ = nullptr;
  std::vector<std::string> snapshot_ids_, closed_ids_;
  raw_ptr<views::Textfield> history_search_ = nullptr;
  raw_ptr<views::Combobox> history_period_ = nullptr;
  raw_ptr<views::View> history_list_ = nullptr;
  raw_ptr<views::Button> history_more_ = nullptr;
  raw_ptr<views::Button> history_clear_ = nullptr;
  std::vector<BrowsingHistoryEntry> history_entries_;
  size_t history_limit_ = 200;
  bool history_loaded_ = false;
  bool browsing_data_busy_ = false;
  base::OneShotTimer history_timer_;
  int tools_page_ = 0;
  bool recording_shortcut_ = false;
  std::unique_ptr<views::WidgetDelegate> tools_delegate_;
  std::unique_ptr<views::Widget> tools_widget_;
  void RequestFolderName(std::string workspace, std::string parent, std::string renamed);
  void ApplyTheme();
  void RefreshGeneralControls();
  void FilterPreferences();
  void SelectPreferenceCategory(size_t category);
  void WritePreference(std::string key, int value);
  void SaveTextPreference(std::string key);
  void RefreshSitePreferences(bool reload = true);
  void SaveSitePreference();
  void SaveGeneralPatch(std::optional<ThemeMode> theme, std::optional<SearchEngine> search, std::optional<bool> animations);
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;
  void RebuildTabs(std::string_view previous, bool catalogue_changed);
  void StopWorkspaceAnimation();
  void ConfigureSidebarContextMenus(views::View* root);
  struct TabListCache {
    raw_ptr<views::View> root = nullptr;
    raw_ptr<views::View> pins_root = nullptr;
    raw_ptr<views::View> tabs_root = nullptr;
    std::vector<PinnedItem> pins;
    std::vector<ShellTab> tabs;
    SidebarTabPlan tab_plan;
    std::array<std::optional<size_t>, 2> split_tabs;
    std::map<std::string, raw_ptr<views::Button>> pin_buttons;
    std::map<size_t, raw_ptr<views::Button>> tab_buttons;
    raw_ptr<views::View> pin_pair = nullptr;  // A split placed among the pins.
    std::string active_pin;
  };
  void AppendPinnedItems(views::View* root, const std::vector<PinnedItem>& pins,
                         TabListCache& cache, const std::string& workspace);
  void RebuildWorkspaceDots();
  void ShowContextMenuForViewImpl(views::View*, const gfx::Point&, ui::mojom::MenuSourceType) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void RenameSelectedProfile();
  void QueueSplit(std::optional<size_t> tab, std::string pin);
  // Menu for context_tab_/context_pin_, at a screen point.
  void ShowTabMenu(gfx::Point point, ui::mojom::MenuSourceType source_type);
  std::optional<size_t> context_tab_;
  std::string context_pin_;
  void SubmitWorkspace(bool add);
  void ImportSelectionChanged();
  base::OneShotTimer import_preview_timer_;
  void ConfirmRemoveWorkspace(std::string id);
  bool creating_workspace_ = false;
  raw_ptr<views::View> workspace_appearance_ = nullptr;
  base::WeakPtr<views::Widget> remove_workspace_dialog_;
  void RefreshWorkspaceSettings();
  void RefreshProfileControls();
  void SubmitProfile();
  void DeleteSelectedProfile();
  void RebuildSuggestions();
  void RequestSearch();
  void ApplyAppearance(bool refresh_theme = true);
  void ApplyHexColor();
  void RefreshWorkspaceHeading();
  void RestorePageFocus();
  void RunSuggestion(size_t index);
  void ShowAddressError();
  bool HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) override;
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
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
  raw_ptr<views::ScrollView> general_scroll_ = nullptr;
  raw_ptr<views::Combobox> theme_picker_ = nullptr;
  raw_ptr<views::Combobox> search_engine_picker_ = nullptr;
  raw_ptr<views::ToggleButton> sidebar_check_ = nullptr;
  raw_ptr<views::ToggleButton> animations_check_ = nullptr;
  raw_ptr<views::Textfield> preferences_search_ = nullptr;
  raw_ptr<views::Label> preferences_profile_ = nullptr;
  raw_ptr<views::Label> download_directory_label_ = nullptr;
  raw_ptr<views::Button> download_directory_button_ = nullptr;
  raw_ptr<views::Label> preferences_empty_ = nullptr;
  raw_ptr<views::View> preferences_body_ = nullptr;
  raw_ptr<views::View> preferences_navigation_ = nullptr;
  raw_ptr<views::Combobox> preferences_category_picker_ = nullptr;
  struct PreferenceRow {
    raw_ptr<views::View> view;
    std::u16string keywords;
  };
  struct PreferenceSection {
    raw_ptr<views::View> view;
    raw_ptr<views::Button> navigation;
    std::vector<PreferenceRow> rows;
  };
  std::vector<PreferenceSection> preference_sections_;
  std::map<std::string, raw_ptr<views::ToggleButton>> preference_toggles_;
  std::map<std::string, raw_ptr<views::Combobox>> preference_pickers_;
  size_t preference_category_ = 0;
  std::map<std::string, raw_ptr<views::Textfield>> preference_textfields_;
  std::map<std::string, raw_ptr<views::Button>> preference_save_buttons_;
  std::set<std::string> preference_text_dirty_;
  std::string preferences_profile_id_;
  raw_ptr<views::Textfield> site_origin_ = nullptr;
  raw_ptr<views::Combobox> site_type_ = nullptr;
  raw_ptr<views::Combobox> site_policy_ = nullptr;
  raw_ptr<views::View> site_rules_list_ = nullptr;
  raw_ptr<views::View> site_rules_card_ = nullptr;
  std::vector<SitePreferenceRule> site_preference_rules_;
  size_t site_rules_limit_ = 50;
  raw_ptr<views::Combobox> clear_data_period_ = nullptr;
  raw_ptr<views::Checkbox> clear_history_ = nullptr;
  raw_ptr<views::Checkbox> clear_cookies_ = nullptr;
  raw_ptr<views::Checkbox> clear_cache_ = nullptr;
  raw_ptr<views::MdTextButton> clear_data_button_ = nullptr;
  raw_ptr<views::Label> general_status_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> general_delegate_;
  std::unique_ptr<views::Widget> general_widget_;
  ThemeMode theme_mode_ = ThemeMode::kSystem;
  SearchEngine search_engine_ = SearchEngine::kDuckDuckGo;
  bool workspace_animations_ = true;
  std::set<std::string> general_dirty_;
  bool applying_theme_ = false;
  raw_ptr<views::View> peek_host_ = nullptr;
  raw_ptr<views::Label> peek_title_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> peek_delegate_;
  std::unique_ptr<views::Widget> peek_widget_;
  std::unique_ptr<ui::SimpleMenuModel> link_menu_;
  std::unique_ptr<ui::SimpleMenuModel> route_menu_;
  std::unique_ptr<views::MenuRunner> link_menu_runner_;
  GURL context_url_;
  std::string link_source_workspace_;
  std::vector<Workspace> route_workspaces_;
  raw_ptr<views::Button> new_tab_button_ = nullptr;
  base::WeakPtr<views::Widget> folder_dialog_;
  std::string folder_parent_id_;
  raw_ptr<views::View> sidebar_root_ = nullptr;
  raw_ptr<views::View> palette_root_ = nullptr;
  raw_ptr<views::View> settings_root_ = nullptr;
  raw_ptr<views::Label> settings_title_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<views::ScrollView> tab_scroll_ = nullptr;
  raw_ptr<views::View> suggestion_list_ = nullptr;
  raw_ptr<views::ScrollView> suggestion_scroll_ = nullptr;
  raw_ptr<views::Button> url_pill_ = nullptr;
  raw_ptr<views::View> workspace_name_ = nullptr;
  raw_ptr<views::Label> profile_name_ = nullptr;
  raw_ptr<views::Button> pin_button_ = nullptr;
  std::array<raw_ptr<views::Button>, 3> nav_buttons_ = {};
  raw_ptr<views::View> sidebar_footer_=nullptr;
  raw_ptr<views::ScrollView> workspace_scroll_=nullptr;
  raw_ptr<views::Button> workspace_settings_button_=nullptr;
  raw_ptr<views::View> workspace_dots_ = nullptr;
  std::map<std::string, raw_ptr<views::Button>> workspace_buttons_;
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
  float swipe_x_ = 0, swipe_y_ = 0;
  int swipe_axis_ = 0;
  bool swipe_tracking_ = false;
  bool swipe_closed_ = false;
  bool swipe_phase_less_ = false;
  bool swipe_committing_ = false;
  int swipe_start_index_ = 0;
  double swipe_position_ = 0;
  std::string swipe_source_, swipe_commit_target_;
  std::vector<std::string> swipe_workspace_ids_;
  raw_ptr<views::View> swipe_overlay_ = nullptr;
  base::OneShotTimer swipe_idle_timer_, swipe_settle_timer_;
  void UpdateWorkspaceSwipePreview();
  void FinishWorkspaceSwipe();
  void CancelWorkspaceSwipe();
  size_t workspace_animation_generation_ = 0;
  bool workspace_animation_pending_ = false;
  base::TimeTicks swipe_last_time_;
  base::TimeTicks last_native_scroll_time_;
  size_t pinned_list_build_count_ = 0;
  size_t sidebar_context_visit_count_ = 0;
  size_t pin_visibility_update_count_ = 0;
  raw_ptr<views::Combobox> profile_picker_ = nullptr;
  raw_ptr<views::Textfield> workspace_input_ = nullptr;
  raw_ptr<views::Button> add_workspace_ = nullptr;
  raw_ptr<views::Button> edit_workspace_ = nullptr;
  raw_ptr<views::Button> remove_workspace_ = nullptr;
  raw_ptr<views::Label> configuration_error_ = nullptr;
  raw_ptr<views::View> import_root_ = nullptr;
  raw_ptr<views::Label> import_status_ = nullptr;
  raw_ptr<views::Combobox> import_browser_picker_ = nullptr;
  raw_ptr<views::Checkbox> import_bookmarks_ = nullptr;
  raw_ptr<views::Checkbox> import_profiles_ = nullptr;
  raw_ptr<views::Checkbox> import_history_ = nullptr;
  raw_ptr<views::Checkbox> import_cookies_ = nullptr;
  raw_ptr<views::Button> apply_import_ = nullptr;
  raw_ptr<views::Button> dismiss_import_ = nullptr;
  bool import_can_apply_ = false;
  bool import_busy_ = false;
  bool import_applying_ = false;
  SidebarTree sidebar_tree_;
  raw_ptr<views::View> profiles_root_ = nullptr;
  raw_ptr<views::Combobox> managed_profile_picker_ = nullptr;
  raw_ptr<views::Textfield> profile_name_input_ = nullptr;
  raw_ptr<views::Button> create_profile_ = nullptr;
  raw_ptr<views::Button> rename_profile_ = nullptr;
  raw_ptr<views::Button> delete_profile_ = nullptr;
  raw_ptr<views::Label> profile_status_ = nullptr;
  bool confirm_profile_deletion_ = false;
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::Textfield> color_input_ = nullptr;
  raw_ptr<views::Textfield> emoji_input_ = nullptr;
  raw_ptr<views::Label> address_error_ = nullptr;
  raw_ptr<views::Label> color_error_ = nullptr;
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
