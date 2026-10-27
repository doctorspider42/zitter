// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Control layout, presentation and input policy; browser state and command ownership
// remain in WorkspaceShell. Only opaque IDs, display text and metrics cross the ABI.
// Display text may include URLs; browser URL objects, profile objects/directories
// and WebContents remain native, and projected text never authorizes an operation.
mod sidebar_editor;
use sidebar_editor::EditorState;

#[cxx::bridge(namespace = "zitter::sidebar")]
mod ffi {
    enum EditorSelectionPolicy { None, Retain, Extend, Caret }
    struct EditorGeometry { clip: FloatRect, origin: f32, baseline: f32, caret: FloatRect }
    struct EditorSpan { x: f32, width: f32, thickness: f32, color: u32 }
    struct EditorStyle { width: f32, height: f32, font_size: f32, ink: u32, fill: u32, edge: u32, selection_fill: u32, selection_ink: u32, placeholder_ink: u32, text_direction: u32, placeholder_width: f32, right_align: bool, framed: bool, focused: bool, caret_visible: bool }

    struct EditorInfo { revision: u64, anchor: usize, focus: usize, secondary: bool, directed: bool, enabled: bool, read_only: bool, composing: bool, composition_start: usize, composition_end: usize, composition_token: u64, can_undo: bool, can_redo: bool, boundaries_ready: bool }
    struct EditorKill { update: EditorUpdate, start: usize, end: usize, before_revision: u64 }
    struct EditorUpdate { admitted: bool, text_changed: bool, selection_changed: bool, composition_changed: bool, committed: bool, info: EditorInfo }
    struct SelectLayout { width: i32, height: i32, label: Rect, arrow: Rect }
    struct SelectOption { key: String, label: String, search: String }
    struct ToolsCopy { title: String, intro: String, tail: String, initial_status: String, fields: Vec<String>, pickers: Vec<String>, headings: Vec<String>, actions: Vec<String>, choices: Vec<String> }
    struct PageDialogCopy { title: String, identity: String, message: Vec<u16>, accept: String, cancel: String, show_cancel: bool, default_cancel: bool, prompt: bool }
    struct ToolsActionContext { snapshot_field: bool, shortcut_field: bool, provider_field: bool, provider_selected: bool, snapshot_composing: bool, shortcut_composing: bool, provider_composing: bool, snapshot_selected: bool, closed_selected: bool, shortcut_selected: bool }
    struct ToolsDispatch { admitted: bool, action: i32, recording: bool, sync_recording: bool, record_status: String }
    struct ToolsOutcome { refresh: bool, status: String }
    enum ToolsNodeKind { Hint, Heading, Editor, Choice, Action, BeginRow, EndRow, Rows, BeginActions, EndActions }
    enum ToolsNodeRole { None, SnapshotName, TimelineFilter, SnapshotChoice, ClosedChoice, ShortcutChoice, ShortcutChord, ProviderChoice, ProviderName, ProviderKeyword, ProviderUrl, HistoryQuery, HistoryPeriod, HistoryClear, HistoryRows, HistoryMore }
    struct ToolsSleepingCopy { title: Vec<u16>, detail: Vec<u16> }
    struct ToolsFormNode { kind: ToolsNodeKind, role: ToolsNodeRole, text: Vec<u16>, action: i32, choices: Vec<String>, enabled: bool, visible: bool, flex: bool, spacing: i32 }
    struct ToolsFormPlan { title: Vec<u16>, status: Vec<u16>, nodes: Vec<ToolsFormNode> }
    struct ToolsShortcutItem { id: String, title: Vec<u16>, chord: Vec<u16> }
    struct ToolsProviderItem { id: String, name: Vec<u16>, keyword: Vec<u16>, url: Vec<u16>, default_provider: bool }
    struct ToolsFormChoice { key: String, label: Vec<u16> }
    struct ToolsFormChoices { choices: Vec<ToolsFormChoice>, enabled: bool }
    struct ToolsChoiceDraft { valid: bool, page: usize, create: bool, target: String, chord: Vec<u16>, name: Vec<u16>, keyword: Vec<u16>, url: Vec<u16>, stop_recording: bool }
    struct ToolsTimelineItem { id: String, title: Vec<u16>, detail: Vec<u16>, search: Vec<u16> }
    struct ToolsTimelineChoice { key: String, label: Vec<u16> }
    struct ToolsTimelineList { choices: Vec<ToolsTimelineChoice>, enabled: bool }
    struct ToolsHistoryView { empty: String, status: String, more_visible: bool, more_enabled: bool }
    struct ToolsClearControls { label: String, enabled: bool }
    enum PreferenceRowKind { Theme, Pinned, Animations, Search, Toggle, Text, Picker, Action, Directory, SiteEditor, SiteRules, ClearData, ShortcutHint }
    enum PreferenceActionKind { None, History, Workspace, Passwords, Profiles, Import, Proxy, Certificates, Providers, Shortcuts, TimeMachine }
    struct PreferenceSpec { category: usize, kind: PreferenceRowKind, key: String, title: String, detail: String, choices: Vec<String>, placeholder: String, action: PreferenceActionKind, button: String, button_ax: String, tooltip: String }
    struct PreferenceSiteCopy { origin_ax: String, placeholder: String, type_ax: String, policy_ax: String, save: String, types: Vec<String> }
    struct PreferenceClearCopy { period_ax: String, periods: Vec<String>, checks: Vec<String>, action: String }
    struct PreferenceCatalogue { categories: Vec<String>, descriptions: Vec<String>, rows: Vec<PreferenceSpec>, site: PreferenceSiteCopy, clear: PreferenceClearCopy }
    struct ScrollViewportEdges { top: Rect, bottom: Rect, show_top: bool, show_bottom: bool }
    struct ScrollViewportLayout { viewport: Rect, content: Rect, bar: Rect, show_bar: bool, offset: f64, max_offset: i32 }
    struct PreferenceRow { category: usize, keywords: Vec<u16> }
    struct PreferenceVisibility { rows: Vec<bool>, sections: Vec<bool>, navigation: Vec<bool>, any: bool }
    struct ToggleStyle { width: f32, height: f32, progress: f32, hover: f32, checked: bool, pressed: bool, focused: bool, rtl: bool, track_on: u32, track_off: u32, thumb_on: u32, thumb_off: u32, thumb_hover: u32, halo: u32, focus: u32, border: u32, clip: FloatRect }
    struct CheckboxLayout { width: i32, height: i32, mark: Rect, label: Rect, lines: Vec<Rect> }
    #[derive(Clone, PartialEq, Eq)]
    struct ImportSelection { browser: u32, separate_profiles: bool, bookmarks: bool, history: bool, cookies: bool }
    struct ImportCopy { title: String, dismiss: String, intro: String, source_label: String, source_ax: String, heading: String, profiles: String, history: String, cookies: String, apply_label: String, browsers: Vec<String> }
    enum ImportAction { None, Preview, Rescan, Apply }
    struct ImportDispatch { action: ImportAction, epoch: u64, selection: ImportSelection }
    struct ImportView { message: String, bookmarks_label: String, options_enabled: bool, close_enabled: bool, apply_enabled: bool, applying: bool, busy: bool, can_apply: bool }
    struct ActionButtonLayout { width: i32, height: i32, text: Rect }
    struct AppearancePreset { color: u32, emoji: String, label: String }
    struct WorkspaceCopy { name_label: String, placeholder: String, profile_label: String, profile_ax: String, profile_hint: String, save_hint: String, close: String, appearance: String, color: String, color_ax: String, emoji: String, emoji_ax: String, emoji_placeholder: String, more: String, manage: String, import_label: String, remove_label: String }
    enum WorkspaceFormAction { None, Create, Edit }
    struct WorkspaceFormRequest { action: WorkspaceFormAction, workspace: String, name: String, profile: String }
    struct WorkspaceFormView { creating: bool, title: String, description: String, save_label: String, secondary_label: String, appearance: bool, enabled: bool, remove_enabled: bool, error: String }
    struct ProfileOption { id: String, name: String }
    struct ProfileUse { workspace: String, name: String, profile: String }
    struct ProfileCopy { title: String, hint: String, name_label: String, name_ax: String, placeholder: String, existing: String, picker_ax: String, create: String, rename: String, remove_label: String, confirm_delete: String, back: String }
    enum ProfileAction { None, Create, Rename, Delete, ConfirmDelete }
    struct ProfileRequest { action: ProfileAction, id: String, name: String, workspace: String }
    struct ProfileControls { create: bool, rename: bool, remove: bool, picker: bool, delete_label: String, status: String, selected: usize, has_selected: bool }
    enum WorkspaceNoticeAction { None, Delete }
    struct WorkspaceNoticeRequest { action: WorkspaceNoticeAction, workspace: String }
    struct NoticeCopy { title: String, body: String, accept: String, cancel: String, default_cancel: bool }
    enum FolderNoticeAction { None, Delete, Acknowledge }
    struct FolderNoticeRequest { action: FolderNoticeAction, workspace: String, folder: String }
    struct NoticeLayout { width: i32, height: i32, body: Rect, error: Rect, body_lines: Vec<Rect>, error_lines: Vec<Rect> }
    struct FolderCopy { title: String, label: String, accept: String, cancel: String }
    enum FolderAction { None, Create, Rename }
    struct FolderRequest { action: FolderAction, workspace: String, parent: String, folder: String, name: String, expand_parent: bool }
    struct FolderLayout { width: i32, height: i32, label: Rect, input: Rect, error: Rect }
    struct MenuTab { index: usize, high: u64, low: u64 }
    struct RowTarget { valid: bool, has_tab: bool, tab: usize, pin: String }
    struct TabMenuState {
        ready: bool, busy: bool, valid: bool, tab: usize, has_tab: bool,
        split: bool, first: usize, has_first: bool, second: usize, has_second: bool,
        vertical: bool, tab_count: usize, http: bool,
    }
    struct TabMenuEntry { command: u32, label: String }
    enum SidebarAction { None, OpenSplit, CloseSplit, MoveFirst, MoveSecond, ToggleOrientation, CloseTab }
    // kind: 0 row, 1 section label, 2 split pair (three consecutive items), 3 gap.
    struct PreviewRow { index: usize, kind: u32, depth: i32, height: i32 }
    struct PreviewPlacement { index: usize, bounds: Rect, clip: Rect }
    struct WorkspaceMotion { enabled: bool, offset: f64, duration_ms: u32 }
    struct SwipeUpdate { started: bool, preview: bool, finish: bool, handled: bool }
    struct SwipePage { index: i32, x: f64 }
    struct SwipeLayout { center: i32, pages: Vec<SwipePage> }
    struct PaletteInputLayout { icon: Rect, input: Rect, hint: Rect }
    struct WidthChange { width: i32, changed: bool }
    struct ShellLayout { content: Rect, edge: Rect, sidebar: Rect }
    struct PaneHostLayout { first: Rect, second: Rect, first_bar: Rect, second_bar: Rect, divider: Rect }
    struct PaneBarLayout { title: Rect, menu: Rect, close: Rect }
    struct DragRatio { ratio: f64, changed: bool }
    struct ScrollMetrics { position: i32, length: i32, offset: i32, max_offset: i32 }
    #[derive(Clone, Copy)]
    struct FloatRect { x: f32, y: f32, width: f32, height: f32 }
    struct ViewportNode { parent: usize, bounds: FloatRect, visible: bool }
    struct ProjectedNode { index: usize, bounds: FloatRect, clip: FloatRect }
    struct ViewportProjection { x: f32, y: f32, nodes: Vec<ProjectedNode> }
    struct Item {
        x: f32, y: f32, width: f32, height: f32,
        // 0: row, 1: section label, 2: split group background, 3: New Tab action, 4: address, 5: workspace heading, 6: scroll thumb, 7: control glyph, 8: pane title, 9: split grip, 10: rounded backing, 11: sidebar grip, 12: palette result, 13: result icon, 14: separator, 15: palette static text, 16: palette frame, 17: form static text, 18: appearance color swatch, 19: form text action, 20: checkbox mark, 21: closed selector, 22: preference category, 23: preferences dismiss, 24: preference surface, 25: history result row, 26: progress indicator.
        kind: u32, glyph: u32,
        title: String, monogram: String, detail: String,
        title_width: f32, detail_ink: u32, detail_font_size: f32,
        ink: u32, fill: u32, badge: u32,
        active: bool, hovered: bool, tile: bool, close: bool, close_visible: bool, focused: bool,
        font_size: f32, font_height: f32, font_weight: i32,
        clip: FloatRect,
    }
    struct TreeNode { id: String, parent: String, folder: bool }
    struct TreeEntry { index: usize, parent: usize, depth: i32, collapsed: bool, visible: bool }
    struct TabNode { index: usize, pin: String }
    struct TabSlot { first: usize, second: usize, pin: usize, pair: bool }
    struct PinVisibility { index: usize, visible: bool }
    struct TabPlan { slots: Vec<TabSlot>, pins: Vec<PinVisibility> }
    enum PointerPhase { Enter, Exit, Move, Press, Drag, Release }
    enum RowVisual { Normal, Hovered, Pressed, Disabled }
    enum RowAction { None, Activate, Drop, Cancel }
    struct RowInputResult { visual: RowVisual, action: RowAction, handled: bool }
    struct ActionNodeKey { kind: u32, high: u64, low: u64, name: String }
    struct ActionNodePlan { admitted: bool, ids: Vec<u64>, created: usize, retired: usize }
    struct ActionSnapshot { enabled: bool, focused: bool, on: bool, is_default: bool, visual: RowVisual }
    struct Rect { x: i32, y: i32, width: i32, height: i32 }
    struct RowLayout { title: Rect, detail: Rect, close: Rect }
    struct ColumnChild { width: i32, height: i32, top: i32, left: i32, bottom: i32, right: i32 }
    struct ColumnLayout { width: i32, height: i32, children: Vec<Rect> }
    struct PanelStyle { width: i32, height: i32, vertical: bool, spacing: i32, top: i32, left: i32, bottom: i32, right: i32, flex: usize }
    #[derive(Clone)]
    struct Primitive {
        x: f32, y: f32, width: f32, height: f32,
        radius: f32, color: u32, stroke: f32, shape: u32,
        text: String, font_size: f32, font_weight: i32, bold: bool, middle: bool, center: bool, clip: FloatRect,
        editor: bool, utf16: Vec<u16>, text_x: f32, baseline: f32, text_direction: u32,
    }
    extern "Rust" {

        fn editor_viewport(state: &mut EditorState, width: f32, height: f32, line_width: f32, caret_x: f32, ascent: f32, descent: f32, rtl: bool, inset: f32) -> EditorGeometry;
        fn editor_scene(state: &EditorState, placeholder: &[u16], geometry: &EditorGeometry, style: &EditorStyle, selection: &[EditorSpan], composition: &[EditorSpan]) -> Vec<Primitive>;
        fn editor_drop(state: &mut EditorState, destination: usize, copy: bool) -> EditorUpdate;
        fn editor_orient(state: &mut EditorState, anchor: usize, focus: usize, secondary: bool) -> EditorUpdate;
        fn editor_move_policy(state: &mut EditorState, index: usize, policy: EditorSelectionPolicy, secondary: bool) -> EditorUpdate;
        fn editor_navigate(state: &mut EditorState, backward: bool, word: bool, policy: EditorSelectionPolicy) -> EditorUpdate;
        fn editor_kill(state: &mut EditorState, end: bool) -> EditorKill;
        fn editor_yank(state: &mut EditorState, text: &[u16]) -> EditorUpdate;
        fn editor_transpose(state: &mut EditorState) -> EditorUpdate;
        type EditorState;
        fn new_editor_state() -> Box<EditorState>;
        fn editor_text(state: &EditorState) -> &[u16];
        fn editor_info(state: &EditorState) -> EditorInfo;
        fn editor_install_boundaries(state: &mut EditorState, revision: u64, stops: &[usize], words: &[usize]) -> bool;
        fn editor_begin_composition(state: &mut EditorState) -> u64;
        fn editor_reset(state: &mut EditorState, text: &[u16]) -> EditorUpdate;
        fn editor_availability(state: &mut EditorState, enabled: bool, read_only: bool) -> EditorUpdate;
        fn editor_select(state: &mut EditorState, anchor: usize, focus: usize, secondary: bool) -> EditorUpdate;
        fn editor_move_to(state: &mut EditorState, index: usize, extend: bool, secondary: bool) -> EditorUpdate;
        fn editor_select_word(state: &mut EditorState) -> EditorUpdate;
        fn editor_pointer_begin(state: &mut EditorState, index: usize, secondary: bool, extend: bool, clicks: u8) -> EditorUpdate;
        fn editor_pointer_drag(state: &mut EditorState, index: usize, secondary: bool) -> EditorUpdate;
        fn editor_pointer_end(state: &mut EditorState);
        fn editor_pointer_active(state: &EditorState) -> bool;
        fn editor_move_logical(state: &mut EditorState, backward: bool, word: bool, extend: bool) -> EditorUpdate;
        fn editor_insert(state: &mut EditorState, text: &[u16], typing: bool) -> EditorUpdate;
        fn editor_erase(state: &mut EditorState, backward: bool, word: bool) -> EditorUpdate;
        fn editor_compose(state: &mut EditorState, token: u64, text: &[u16], anchor: usize, focus: usize) -> EditorUpdate;
        fn editor_confirm(state: &mut EditorState, token: u64) -> EditorUpdate;
        fn editor_cancel(state: &mut EditorState, token: u64) -> EditorUpdate;
        fn editor_undo(state: &mut EditorState) -> EditorUpdate;
        fn editor_redo(state: &mut EditorState) -> EditorUpdate;

        fn tools_copy(page: usize, context: &str) -> ToolsCopy;
        fn page_dialog_copy(kind: u32, unload: bool, origin: &str, profile: &str, message: &[u16]) -> PageDialogCopy;
        fn tools_clear_label(busy: bool) -> String;
        fn tools_clear_controls(busy: bool) -> ToolsClearControls;
        type ToolsForm;
        fn new_tools_form() -> Box<ToolsForm>;
        fn tools_select(self: &mut ToolsForm, page: usize);
        fn tools_page(self: &ToolsForm) -> usize;
        fn tools_reset(self: &mut ToolsForm);
        fn tools_record(self: &mut ToolsForm, recording: bool);
        fn tools_recording(self: &ToolsForm) -> bool;
        fn tools_dispatch(self: &mut ToolsForm, action: i32, context: &ToolsActionContext) -> ToolsDispatch;
        fn tools_outcome(self: &ToolsForm, ok: bool, pending: bool) -> ToolsOutcome;
        fn tools_history_view(self: &ToolsForm, count: usize, more: bool, error: bool) -> ToolsHistoryView;
        fn tools_history_edit(self: &mut ToolsForm, query: Vec<u16>);
        fn tools_history_request(self: &mut ToolsForm, more: bool) -> usize;
        fn tools_history_query(self: &ToolsForm) -> Vec<u16>;
        fn tools_history_accept(self: &mut ToolsForm, query: &[u16], limit: usize) -> bool;
        fn tools_history_loaded(self: &ToolsForm) -> bool;
        fn tools_timeline_edit(self: &mut ToolsForm, query: Vec<u16>);
        fn tools_timeline_query(self: &ToolsForm) -> Vec<u16>;
        fn tools_form_plan(self: &ToolsForm, context: &str, busy: bool, sleeping: Vec<ToolsSleepingCopy>) -> ToolsFormPlan;
        fn tools_shortcut_choices(self: &mut ToolsForm, items: Vec<ToolsShortcutItem>) -> ToolsFormChoices;
        fn tools_provider_choices(self: &mut ToolsForm, items: Vec<ToolsProviderItem>) -> ToolsFormChoices;
        fn tools_choice_select(self: &mut ToolsForm, key: &str) -> ToolsChoiceDraft;
        fn tools_choice_draft(self: &ToolsForm, key: &str) -> ToolsChoiceDraft;
        fn tools_timeline_project(self: &mut ToolsForm, closed: bool, items: Vec<ToolsTimelineItem>) -> ToolsTimelineList;
        fn tools_timeline_target(self: &ToolsForm, closed: bool, key: &str) -> String;
        fn preference_catalogue(system_certificates: bool) -> PreferenceCatalogue;
        fn preference_site_policies(index: usize) -> Vec<String>;
        type PreferencesForm;
        fn new_preferences_form(categories: usize) -> Box<PreferencesForm>;
        fn preference_select(self: &mut PreferencesForm, category: usize) -> bool;
        fn preference_current(self: &PreferencesForm) -> usize;
        fn preference_profile(self: &mut PreferencesForm, profile: &str) -> bool;
        fn preference_detach(self: &mut PreferencesForm);
        fn preference_edited(self: &mut PreferencesForm, key: &str);
        fn preference_saved(self: &mut PreferencesForm, key: &str, ok: bool);
        fn preference_sync_text(self: &mut PreferencesForm, key: &str, present: bool, editable: bool, composing: bool, profile_changed: bool) -> bool;
        fn preference_can_save(self: &PreferencesForm, enabled: bool, composing: bool) -> bool;
        fn preference_reset_rules(self: &mut PreferencesForm);
        fn preference_more_rules(self: &mut PreferencesForm);
        fn preference_rule_limit(self: &PreferencesForm) -> usize;
        fn preference_filter(self: &mut PreferencesForm, query: Vec<u16>, rows: &[PreferenceRow]) -> PreferenceVisibility;
        type SelectState;
        fn new_select_state(options: Vec<SelectOption>) -> Box<SelectState>;
        fn selected(self: &SelectState) -> usize;
        fn has_selection(self: &SelectState) -> bool;
        fn selected_key(self: &SelectState) -> &str;
        fn option_key(self: &SelectState, index: usize) -> &str;
        fn replace_options(self: &mut SelectState, options: Vec<SelectOption>, fallback_last: bool, select_last: bool) -> bool;
        fn can_choose(self: &SelectState, token: u64, key: &str, enabled: bool) -> bool;
        fn choose_key(self: &mut SelectState, token: u64, key: &str, enabled: bool) -> bool;
        fn close_menu(self: &mut SelectState, token: u64) -> bool;
        fn close_menu_at(self: &mut SelectState, token: u64, now_us: u64) -> bool;
        fn dismiss_popup(self: &mut SelectState, now_us: u64, popup_present: bool);
        fn open_pointer(self: &mut SelectState, enabled: bool, now_us: u64, guard_us: u64) -> u64;
        fn select_index(self: &mut SelectState, index: usize) -> bool;
        fn clear_selection(self: &mut SelectState);
        fn open(self: &mut SelectState, enabled: bool) -> u64;
        fn expanded(self: &SelectState) -> bool;
        fn choose(self: &mut SelectState, token: u64, index: usize, enabled: bool) -> bool;
        fn dismiss(self: &mut SelectState);
        fn reset_prefix(self: &mut SelectState);
        fn input(self: &mut SelectState, lower_text: &str, now_ms: u64, enabled: bool) -> bool;
        fn select_menu_key(key: u32) -> bool;
        fn select_layout(width: i32, height: i32, text_width: i32, control_height: i32, rtl: bool) -> SelectLayout;
        type ToggleState;
        fn new_toggle_state() -> Box<ToggleState>;
        fn toggle_checked(self: &ToggleState) -> bool;
        fn toggle_set(self: &mut ToggleState, checked: bool);
        fn toggle_flip(self: &mut ToggleState, enabled: bool, now: u64, animate: bool) -> bool;
        fn toggle_hover(self: &mut ToggleState, hovered: bool, now: u64, animate: bool);
        fn toggle_progress(self: &ToggleState, now: u64) -> f32;
        fn toggle_hover_progress(self: &ToggleState, now: u64) -> f32;
        fn toggle_animating(self: &ToggleState, now: u64) -> bool;
        fn toggle_finish(self: &mut ToggleState);
        fn toggle_scene(style: &ToggleStyle) -> Vec<Primitive>;
        type CheckboxState;
        fn new_checkbox_state(checked: bool) -> Box<CheckboxState>;
        fn checked(self: &CheckboxState) -> bool;
        fn set_checked(self: &mut CheckboxState, checked: bool) -> bool;
        fn toggle(self: &mut CheckboxState, enabled: bool) -> bool;
        fn checkbox_layout(width: i32, line_height: i32, line_count: usize, left: i32, top: i32, right: i32, bottom: i32, rtl: bool) -> CheckboxLayout;
        type ImportForm;
        fn new_import_form() -> Box<ImportForm>;
        fn import_defaults() -> ImportSelection;
        fn import_copy() -> ImportCopy;
        fn select(self: &mut ImportForm, selection: ImportSelection) -> ImportDispatch;
        fn cancel_scan(self: &mut ImportForm);
        fn take_scan(self: &mut ImportForm, epoch: u64, visible: bool, current: ImportSelection, ready: bool, profiles_busy: bool) -> ImportDispatch;
        fn should_resume(self: &ImportForm, ready: bool, profiles_busy: bool) -> bool;
        fn begin_preview(self: &mut ImportForm, selection: ImportSelection);
        fn set_status(self: &mut ImportForm, message: &str, can_apply: bool, busy: bool, applying: bool);
        fn view(self: &ImportForm, ready: bool, profiles_busy: bool) -> ImportView;
        fn request_apply(self: &mut ImportForm, current: ImportSelection, ready: bool, profiles_busy: bool, visible: bool) -> ImportDispatch;
        fn finish_apply(self: &mut ImportForm, accepted: bool);
        fn action_button_layout(width: i32, height: i32, text_width: i32, control_height: i32, padding: i32, minimum_width: i32) -> ActionButtonLayout;
        fn dialog_actions_layout(width: i32, height: i32, spacing: i32, link_limit: i32, top: i32, left: i32, bottom: i32, right: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn action_group_layout(width: i32, height: i32, vertical: bool, spacing: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn appearance_presets(emoji: bool) -> Vec<AppearancePreset>;
        fn appearance_selected(emoji: bool, index: usize, color: u32, current_emoji: &str) -> bool;
        fn appearance_row_layout(width: i32, height: i32, editor_width: i32, editor_height: i32, emoji: bool) -> ColumnLayout;
        type WorkspaceNotice;
        fn new_workspace_notice(workspace: &str, title: &str) -> Box<WorkspaceNotice>;
        fn copy(self: &WorkspaceNotice) -> NoticeCopy;
        fn error(self: &WorkspaceNotice) -> &str;
        fn submit(self: &mut WorkspaceNotice, ready: bool, busy: bool, workspaces: &[ProfileUse]) -> WorkspaceNoticeRequest;
        fn finish_attempt(self: &mut WorkspaceNotice, accepted: bool);
        fn cancel(self: &mut WorkspaceNotice);
        type WorkspaceForm;
        fn new_workspace_form() -> Box<WorkspaceForm>;
        fn workspace_copy() -> WorkspaceCopy;
        fn workspace_appearance_error(kind: u32) -> String;
        fn reset(self: &mut WorkspaceForm, current: &str);
        fn begin_create(self: &mut WorkspaceForm, current: &str);
        fn creating(self: &WorkspaceForm) -> bool;
        fn view(self: &WorkspaceForm, name: &str, profile_name: &str, ready: bool, busy: bool, count: usize) -> WorkspaceFormView;
        fn set_configuration_error(self: &mut WorkspaceForm, error: bool);
        fn submit(self: &mut WorkspaceForm, name: &str, profile: &str, current: &str, ready: bool, busy: bool, composing: bool, profiles: &[ProfileOption], workspaces: &[ProfileUse]) -> WorkspaceFormRequest;
        fn finish_attempt(self: &mut WorkspaceForm, accepted: bool);
        type ProfileForm;
        fn new_profile_form() -> Box<ProfileForm>;
        fn profile_copy() -> ProfileCopy;
        fn open(self: &mut ProfileForm, selected: &str, workspace: &str);
        fn select(self: &mut ProfileForm, selected: &str);
        fn reconcile(self: &mut ProfileForm, profiles: &[ProfileOption], prefer_new: bool);
        fn set_status(self: &mut ProfileForm, status: &str);
        fn status(self: &ProfileForm) -> &str;
        fn selection(self: &ProfileForm) -> &str;
        fn controls(self: &mut ProfileForm, ready: bool, busy: bool, profiles: &[ProfileOption], uses: &[ProfileUse]) -> ProfileControls;
        fn request(self: &mut ProfileForm, action: u32, name: &str, ready: bool, busy: bool, profiles: &[ProfileOption], uses: &[ProfileUse]) -> ProfileRequest;
        fn finish_attempt(self: &mut ProfileForm, accepted: bool);
        fn preference_chrome_scene(items: &[Item], holes: &[FloatRect]) -> Vec<Primitive>;
        fn paragraph_layout(width: i32, line_height: i32, count: usize) -> ColumnLayout;
        type FolderNotice;
        fn new_folder_notice(workspace: &str, folder: &str, title: &str, error_only: bool) -> Box<FolderNotice>;
        fn copy(self: &FolderNotice) -> NoticeCopy;
        fn error(self: &FolderNotice) -> &str;
        fn submit(self: &mut FolderNotice, current: &str, ready: bool, busy: bool, nodes: &[TreeNode]) -> FolderNoticeRequest;
        fn finish_attempt(self: &mut FolderNotice, accepted: bool);
        fn cancel(self: &mut FolderNotice);
        fn notice_layout(width: i32, body_height: i32, body_count: usize, error_height: i32, error_count: usize) -> NoticeLayout;
        type FolderForm;
        fn new_folder_form(workspace: &str, parent: &str, folder: &str, initial: &str) -> Box<FolderForm>;
        fn copy(self: &FolderForm) -> FolderCopy;
        fn edit(self: &mut FolderForm, name: &str);
        fn error(self: &FolderForm) -> &str;
        fn submit(self: &mut FolderForm, current: &str, ready: bool, busy: bool, nodes: &[TreeNode], composing: bool) -> FolderRequest;
        fn finish_attempt(self: &mut FolderForm, accepted: bool);
        fn cancel(self: &mut FolderForm);
        fn folder_form_layout(width: i32, label_height: i32, input_height: i32, error_height: i32, error: bool) -> FolderLayout;
        fn resolve_row_target(source: &str, current: &str, high: u64, low: u64, pin: &str, tabs: &[MenuTab], pins: &[TreeNode]) -> RowTarget;
        fn sidebar_drop_accepts(bounds: Rect, x: i32, y: i32, allowed: bool) -> bool;
        fn tab_context_menu(state: &TabMenuState) -> Vec<TabMenuEntry>;
        fn tab_menu_enabled(command: u32, state: &TabMenuState) -> bool;
        fn tab_menu_action(command: u32, state: &TabMenuState) -> SidebarAction;
        type WorkspaceSwipe;
        fn new_workspace_swipe() -> Box<WorkspaceSwipe>;
        fn event(self: &mut WorkspaceSwipe, x: f32, y: f32, width: i32, index: i32, count: i32,
                 began: bool, ended: bool, momentum: bool, phase_less: bool) -> SwipeUpdate;
        fn finish(self: &mut WorkspaceSwipe);
        fn cancel(self: &mut WorkspaceSwipe);
        fn tracking(self: &WorkspaceSwipe) -> bool;
        fn phase_less(self: &WorkspaceSwipe) -> bool;
        fn position(self: &WorkspaceSwipe) -> f64;
        fn target(self: &WorkspaceSwipe, count: i32) -> i32;
        fn swipe_layout(position: f64, count: i32, width: i32) -> SwipeLayout;
        fn workspace_switch_motion(previous: i32, current: i32, visible: bool, enabled: bool, reduced: bool) -> WorkspaceMotion;
        fn workspace_preview_layout(width: i32, height: i32, offset: i32, rows: &[PreviewRow]) -> Vec<PreviewPlacement>;
        type PaletteSelection;
        fn new_palette_selection() -> Box<PaletteSelection>;
        fn reset(self: &mut PaletteSelection);
        fn selected(self: &PaletteSelection) -> usize;
        fn moved(self: &PaletteSelection) -> bool;
        fn step(self: &mut PaletteSelection, count: usize, backwards: bool) -> bool;
        fn reconcile(self: &mut PaletteSelection, count: usize, preserved: usize, found: bool);
        fn palette_text_layout(kind: u32, width: i32, height: i32, text_width: i32, font_height: i32) -> ColumnLayout;
        fn palette_input_layout(width: i32, height: i32, icon: Rect, field_height: i32, hint: Rect) -> PaletteInputLayout;
        fn palette_width(window_width: i32) -> i32;
        fn palette_results_height(content_height: i32) -> i32;
        fn palette_frame(content: Rect, width: i32, height: i32) -> Rect;
        type SidebarDrag;
        fn new_sidebar_drag() -> Box<SidebarDrag>;
        fn begin(self: &mut SidebarDrag, width: i32, pointer: f64);
        fn update(self: &mut SidebarDrag, pointer: f64, reverse: bool) -> WidthChange;
        fn delta(self: &mut SidebarDrag, delta: f64, reverse: bool) -> WidthChange;
        fn end(self: &mut SidebarDrag) -> WidthChange;
        fn cancel(self: &mut SidebarDrag);
        fn active(self: &SidebarDrag) -> bool;
        fn sidebar_resize_width(initial: i32, amount: i32) -> i32;
        fn shell_layout(width: i32, height: i32, preferred: i32, pinned: bool) -> ShellLayout;
        fn sidebar_grip_layout(width: i32, height: i32) -> Rect;
        fn sidebar_auto_hide(pinned: bool, blocked: bool, inside: bool, dragging: bool) -> bool;
        type SplitDrag;
        fn new_split_drag() -> Box<SplitDrag>;
        fn begin(self: &mut SplitDrag, ratio: f64, pointer: f64);
        fn update(self: &mut SplitDrag, pointer: f64, extent: i32, reverse: bool) -> DragRatio;
        fn delta(self: &mut SplitDrag, delta: f64, extent: i32, reverse: bool) -> DragRatio;
        fn end(self: &mut SplitDrag) -> DragRatio;
        fn cancel(self: &mut SplitDrag);
        fn active(self: &SplitDrag) -> bool;
        fn moved(self: &SplitDrag) -> bool;
        fn pane_host_layout(width: i32, height: i32, ratio: f64, vertical: bool, split: bool) -> PaneHostLayout;
        fn pane_bar_layout(width: i32, height: i32) -> PaneBarLayout;
        type ScrollViewportState;
        fn new_scroll_viewport() -> Box<ScrollViewportState>;
        fn viewport_preferred_size(width: i32, height: i32, minimum: i32, maximum: i32) -> Rect;
        fn viewport_measure_width(width: i32, height: i32, full_height: i32, thickness: i32, overlay: bool) -> i32;
        fn viewport_configure(self: &mut ScrollViewportState, width: i32, height: i32, preferred_width: i32, preferred_height: i32, thickness: i32, overlay: bool) -> ScrollViewportLayout;
        fn viewport_configure_horizontal(self: &mut ScrollViewportState, width: i32, height: i32, preferred_width: i32, preferred_height: i32, thickness: i32, overlay: bool) -> ScrollViewportLayout;
        fn viewport_scroll_to(self: &mut ScrollViewportState, offset: f64) -> bool;
        fn viewport_scroll_delta(self: &mut ScrollViewportState, delta: f64) -> bool;
        fn viewport_reveal(self: &mut ScrollViewportState, y: i32, height: i32) -> bool;
        fn viewport_edges(self: &ScrollViewportState, width: i32) -> ScrollViewportEdges;
        fn viewport_axis_edges(self: &ScrollViewportState, cross: i32, horizontal: bool) -> ScrollViewportEdges;
        fn viewport_offset(self: &ScrollViewportState) -> f64;
        fn viewport_maximum(self: &ScrollViewportState) -> i32;
        type ScrollThumb;
        fn new_scroll_thumb() -> Box<ScrollThumb>;
        fn begin(self: &mut ScrollThumb, track: i32, viewport: i32, content: i32, offset: i32, pointer: f32);
        fn drag(self: &ScrollThumb, track: i32, viewport: i32, content: i32, offset: i32,
                pointer: f32, cross: f32, thickness: i32, snap: bool) -> i32;
        fn end(self: &mut ScrollThumb);
        fn scroll_metrics(track: i32, viewport: i32, content: i32, offset: i32) -> ScrollMetrics;
        fn scroll_offset(track: i32, viewport: i32, content: i32, position: f32) -> i32;
        type Viewport;
        fn new_viewport() -> Box<Viewport>;
        fn project(self: &mut Viewport, content_width: f32, content_height: f32,
                   width: f32, height: f32, x: f32, y: f32,
                   nodes: &[ViewportNode]) -> ViewportProjection;
        type SidebarTree;
        fn new_sidebar_tree() -> Box<SidebarTree>;
        fn tree_plan(self: &SidebarTree, workspace: &str, nodes: &[TreeNode]) -> Vec<TreeEntry>;
        fn tab_plan(self: &SidebarTree, workspace: &str, nodes: &[TreeNode], tabs: &[TabNode],
                    first: usize, second: usize, split: bool) -> TabPlan;
        fn collapsed(self: &SidebarTree, workspace: &str, id: &str) -> bool;
        fn set_collapsed(self: &mut SidebarTree, workspace: &str, id: &str, value: bool);
        fn toggle(self: &mut SidebarTree, workspace: &str, id: &str) -> bool;
        fn forget_workspace(self: &mut SidebarTree, workspace: &str);
        type ActionGraph;
        fn new_action_graph() -> Box<ActionGraph>;
        fn reconcile(self: &mut ActionGraph, keys: Vec<ActionNodeKey>) -> ActionNodePlan;
        fn acquire(self: &mut ActionGraph, key: &ActionNodeKey) -> Box<ActionControl>;
        type ActionControl;
        fn action_node_id(self: &ActionControl) -> u64;
        fn new_action_control() -> Box<ActionControl>;
        fn action_preferred_size(dot: bool, label: bool) -> Rect;
        fn action_enabled(self: &mut ActionControl, enabled: bool) -> bool;
        fn action_focus(self: &mut ActionControl, focused: bool);
        fn action_on(self: &mut ActionControl, on: bool) -> bool;
        fn action_cancel(self: &mut ActionControl) -> bool;
        fn action_snapshot(self: &ActionControl) -> ActionSnapshot;
        fn action_pointer(self: &mut ActionControl, phase: PointerPhase, x: i32, y: i32, inside: bool, trigger: bool, visible: bool) -> RowInputResult;
        fn action_default(self: &mut ActionControl, value: bool) -> bool;
        fn action_on_press(self: &mut ActionControl, value: bool) -> bool;
        fn action_accelerator(self: &mut ActionControl, key: u32, modified: bool, visible: bool) -> RowInputResult;
        fn action_pending(self: &ActionControl) -> bool;
        fn action_row_pointer(self: &mut ActionControl, phase: PointerPhase, x: i32, y: i32, inside: bool, trigger: bool, visible: bool, drop: bool) -> RowInputResult;
        fn action_key(self: &mut ActionControl, pressed: bool, key: u32, mac: bool, visible: bool) -> RowInputResult;
        fn action_gesture(self: &mut ActionControl, kind: u32, visible: bool) -> RowInputResult;
        type RowPointer;
        fn new_row_pointer() -> Box<RowPointer>;
        fn press(self: &mut RowPointer, x: i32, y: i32);
        fn drag(self: &mut RowPointer, x: i32, y: i32, enabled: bool);
        fn release(self: &mut RowPointer) -> bool;
        fn cancel(self: &mut RowPointer);
        fn pointer_event(self: &mut RowPointer, phase: PointerPhase, x: i32, y: i32,
                         inside: bool, enabled: bool, trigger: bool, drop: bool) -> RowInputResult;
        fn key_policy(key: u32, mac: bool) -> u32;
        // Platform policy: 0 none, 1 activate on key press, 2 on release.
        fn key_event(self: &mut RowPointer, pressed: bool, policy: u32,
                     enabled: bool) -> RowInputResult;
        fn panel_layout(style: &PanelStyle, initial: &[ColumnChild], measured: &[ColumnChild]) -> ColumnLayout;
        fn panel_center_layout(style: &PanelStyle, initial: &[ColumnChild], measured: &[ColumnChild]) -> ColumnLayout;
        fn column_layout(width: i32, spacing: i32, top: i32, left: i32,
                         bottom: i32, right: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn split_layout(width: i32, height: i32, count: usize) -> ColumnLayout;
        fn folder_indent(depth: i32) -> i32;
        fn close_visible(active: bool, tile: bool, hovered: bool) -> bool;
        fn footer_layout(width: i32, content_width: i32) -> ColumnLayout;
        fn strip_layout(width: i32, height: i32, spacing: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn action_frame(row: Rect, viewport: Rect) -> Rect;
        fn address_layout(width: i32, height: i32, host_width: i32) -> RowLayout;
        fn row_layout(width: i32, height: i32, title_width: i32,
                      detail: bool, chip: bool, close: bool) -> RowLayout;
        fn layout(items: &[Item]) -> Vec<Primitive>;
        fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize;
    }
}
#[derive(Default)]
struct PaletteSelection { index:usize, moved:bool }
fn new_palette_selection()->Box<PaletteSelection>{Box::new(PaletteSelection::default())}
impl PaletteSelection {
    fn reset(&mut self){self.index=0;self.moved=false;}
    fn selected(&self)->usize{self.index}
    fn moved(&self)->bool{self.moved}
    fn step(&mut self,count:usize,backwards:bool)->bool {
        if count==0{return false;}self.index=self.index.min(count-1);
        self.index=if backwards{if self.index==0{count-1}else{self.index-1}}else if self.index+1==count{0}else{self.index+1};
        self.moved=true;true
    }
    fn reconcile(&mut self,count:usize,preserved:usize,found:bool){if found{self.index=preserved;}self.index=self.index.min(count.saturating_sub(1));}
}
// Gesture state is independent of window visibility, timers and workspace IDs.
// Only the native adapter commits the selected workspace after settling begins.
pub struct WorkspaceSwipe {
    x:f32, y:f32, axis:i32, tracking:bool, closed:bool, phase_less:bool,
    start:i32, count:i32, position:f64,
}
fn new_workspace_swipe()->Box<WorkspaceSwipe>{Box::new(WorkspaceSwipe{x:0.0,y:0.0,axis:0,tracking:false,closed:false,phase_less:false,start:0,count:0,position:0.0})}
impl WorkspaceSwipe {
    fn event(&mut self,x:f32,y:f32,width:i32,index:i32,count:i32,began:bool,ended:bool,momentum:bool,phase_less:bool)->ffi::SwipeUpdate {
        let started=count>=2 && (began || (!self.tracking && !momentum && !ended && (!self.closed || phase_less)));
        if started {
            self.cancel();self.x=0.0;self.y=0.0;self.axis=0;self.tracking=true;self.closed=false;
            self.phase_less=phase_less;self.count=count;self.start=index.clamp(0,count-1);self.position=self.start as f64;
        }
        let mut out=ffi::SwipeUpdate{started,preview:false,finish:false,handled:self.axis==1};
        if momentum || !self.tracking {
            if momentum && self.tracking {self.finish();out.finish=true;}
            return out;
        }
        // Malformed platform input must not poison later transforms/targets.
        if self.axis!=-1 && x.is_finite() && y.is_finite() {
            let nx=self.x+x;let ny=self.y+y;
            if nx.is_finite() && ny.is_finite(){self.x=nx;self.y=ny;}
            if self.axis==0 {
                if self.y.abs()>=12.0 && self.y.abs()>self.x.abs(){self.axis=-1;}
                else if self.x.abs()>=12.0 && self.x.abs()>=self.y.abs()*1.5{self.axis=1;}
            }
            if self.axis==1 {
                let position=self.start as f64-self.x as f64/width.max(1) as f64;let last=(self.count-1) as f64;
                self.position=if position<0.0{-(-position*0.2).min(0.22)}else if position>last{last+((position-last)*0.2).min(0.22)}else{position};
                out.preview=true;
            }
        }
        out.handled=self.axis==1;
        if ended{self.finish();out.finish=true;}
        out
    }
    fn finish(&mut self){self.tracking=false;self.closed=true;}
    fn cancel(&mut self){self.finish();}
    fn tracking(&self)->bool{self.tracking}
    fn phase_less(&self)->bool{self.phase_less}
    fn position(&self)->f64{self.position}
    fn target(&self,count:i32)->i32{swipe_center(self.position,count)}
}
fn swipe_center(position:f64,count:i32)->i32 {
    if count<=0{return -1;}
    let position=if position.is_finite(){position}else{0.0};
    (position.round() as i32).clamp(0,count-1)
}
fn swipe_layout(position:f64,count:i32,width:i32)->ffi::SwipeLayout {
    let center=swipe_center(position,count);
    let mut pages=Vec::new();
    if center>=0 {
        let position=if position.is_finite(){position}else{0.0};
        for index in (center-1).max(0)..=(center+1).min(count-1){pages.push(ffi::SwipePage{index,x:(index as f64-position)*width.max(0) as f64});}
    }
    ffi::SwipeLayout{center,pages}
}

// Workspace names retain the original ASCII-only trimming policy. Unicode
// whitespace, duplicate workspace names and long names were accepted previously.
pub struct WorkspaceForm {
    workspace: String,
    creating: bool,
    submitting: bool,
    error: String,
}
fn new_workspace_form() -> Box<WorkspaceForm> {
    Box::new(WorkspaceForm { workspace: String::new(), creating: false, submitting: false, error: String::new() })
}
fn workspace_copy() -> ffi::WorkspaceCopy {
    ffi::WorkspaceCopy {
        name_label: "Nazwa przestrzeni".into(), placeholder: "Np. Projekt lub Klient".into(),
        profile_label: "Profil przeglądania".into(), profile_ax: "Profil przestrzeni".into(),
        profile_hint: "Wspólny profil = wspólne logowanie i cookies.\nZmiana profilu zamknie karty tej przestrzeni.".into(),
        save_hint: "Zapisz poniżej nazwę i profil. Wygląd zapisuje się od razu.".into(),
        close: "Zamknij ustawienia przestrzeni".into(), appearance: "Wygląd · zapis automatyczny".into(),
        color: "Kolor przestrzeni".into(), color_ax: "Kolor przestrzeni #RRGGBB".into(),
        emoji: "Emoji przestrzeni".into(), emoji_ax: "Emoji przestrzeni".into(), emoji_placeholder: "Inne… (⌃⌘Spacja)".into(),
        more: "Więcej opcji".into(), manage: "Zarządzaj profilami".into(), import_label: "Importuj z przeglądarki…".into(), remove_label: "Usuń przestrzeń…".into(),
    }
}
fn workspace_appearance_error(kind: u32) -> String {
    match kind { 0 => "Nie udało się zapisać ustawień", 1 => "Nie udało się zapisać koloru", 2 => "Nie udało się zapisać emoji", _ => "Podaj kolor #RRGGBB" }.into()
}
const WORKSPACE_CONFIGURATION_ERROR: &str = "Nie udało się odczytać lub zapisać przestrzeni.";
impl WorkspaceForm {
    fn reset(&mut self, current: &str) { self.workspace=current.into(); self.creating=false; self.submitting=false; }
    fn begin_create(&mut self, current: &str) { self.workspace=current.into(); self.creating=true; self.submitting=false; self.error.clear(); }
    fn creating(&self) -> bool { self.creating }
    fn view(&self, name: &str, profile_name: &str, ready: bool, busy: bool, count: usize) -> ffi::WorkspaceFormView {
        let enabled=ready&&!busy&&!self.submitting;
        ffi::WorkspaceFormView { creating:self.creating, title:if self.creating { "Nowa przestrzeń".into() } else { name.into() }, description:format!("Chromium: {}",profile_name),
            save_label:if self.creating { "Utwórz przestrzeń" } else { "Zapisz nazwę i profil" }.into(),
            secondary_label:if self.creating { "Anuluj" } else { "Nowa przestrzeń…" }.into(),
            appearance:!self.creating, enabled, remove_enabled:enabled&&!self.creating&&count>1, error:self.error.clone() }
    }
    fn set_configuration_error(&mut self, error: bool) { self.error=if error { WORKSPACE_CONFIGURATION_ERROR.into() } else { String::new() }; }
    fn submit(&mut self, raw_name: &str, profile: &str, current: &str, ready: bool, busy: bool, composing: bool, profiles: &[ffi::ProfileOption], workspaces: &[ffi::ProfileUse]) -> ffi::WorkspaceFormRequest {
        let none=||ffi::WorkspaceFormRequest { action:ffi::WorkspaceFormAction::None, workspace:String::new(), name:String::new(), profile:String::new() };
        if composing||self.submitting { return none(); }
        if !ready||busy { self.error="Poczekaj na zakończenie zapisywania.".into(); return none(); }
        if self.workspace.is_empty()||self.workspace!=current||!workspaces.iter().any(|w|w.workspace==self.workspace) { self.error="Przestrzeń zmieniła się. Otwórz ustawienia ponownie.".into(); return none(); }
        let name=raw_name.trim_matches(|c|matches!(c,' '|'\t'|'\n'|'\r'|'\x0b'|'\x0c'));
        if name.is_empty() { self.error="Podaj nazwę przestrzeni.".into(); return none(); }
        if profile.is_empty()||!profiles.iter().any(|p|p.id==profile) { self.error="Wybierz istniejący profil.".into(); return none(); }
        self.error.clear(); self.submitting=true;
        ffi::WorkspaceFormRequest { action:if self.creating { ffi::WorkspaceFormAction::Create } else { ffi::WorkspaceFormAction::Edit }, workspace:self.workspace.clone(), name:name.into(), profile:profile.into() }
    }
    fn finish_attempt(&mut self, accepted: bool) {
        self.submitting=false;
        if accepted { self.creating=false; self.error.clear(); }
        else if self.error.is_empty() { self.error=WORKSPACE_CONFIGURATION_ERROR.into(); }
    }
}

pub struct ProfileForm { selected:String, workspace:String, confirmation:String, status:String, submitting:bool }
fn new_profile_form()->Box<ProfileForm>{Box::new(ProfileForm{selected:String::new(),workspace:String::new(),confirmation:String::new(),status:String::new(),submitting:false})}
fn profile_copy()->ffi::ProfileCopy {ffi::ProfileCopy{title:"Profile przeglądania".into(),hint:"Każdy profil ma osobne logowania i dane stron.\nMożesz przypisać go do kilku przestrzeni.".into(),name_label:"Nazwa profilu".into(),name_ax:"Nazwa nowego profilu".into(),placeholder:"Np. Klient, Projekt, Konto testowe".into(),existing:"Istniejące profile".into(),picker_ax:"Zarządzany profil".into(),create:"Utwórz profil".into(),rename:"Zapisz nazwę profilu".into(),remove_label:"Usuń profil i jego dane".into(),confirm_delete:"Potwierdź usunięcie profilu i danych".into(),back:"Wróć do przestrzeni".into()}}
impl ProfileForm {
    fn open(&mut self,selected:&str,workspace:&str){self.selected=selected.into();self.workspace=workspace.into();self.confirmation.clear();self.status.clear();self.submitting=false;}
    fn select(&mut self,selected:&str){self.selected=selected.into();self.workspace.clear();self.confirmation.clear();self.status.clear();}
    fn reconcile(&mut self,profiles:&[ffi::ProfileOption],prefer_new:bool){if prefer_new||!profiles.iter().any(|p|p.id==self.selected){self.selected=profiles.last().map(|p|p.id.clone()).unwrap_or_default();}self.confirmation.clear();}
    fn set_status(&mut self,status:&str){self.status=status.into();}
    fn status(&self)->&str{&self.status}
    fn selection(&self)->&str{&self.selected}
    fn used_names(&self,uses:&[ffi::ProfileUse])->String {uses.iter().filter(|w|w.profile==self.selected).map(|w|w.name.as_str()).collect::<Vec<_>>().join(", ")}
    fn controls(&mut self,ready:bool,busy:bool,profiles:&[ffi::ProfileOption],uses:&[ffi::ProfileUse])->ffi::ProfileControls {
        let selected=profiles.iter().position(|p|p.id==self.selected);let used=uses.iter().any(|w|w.profile==self.selected);let enabled=ready&&!busy&&!self.submitting;
        if !enabled||used||selected.is_none()||profiles.len()<=1{self.confirmation.clear();}
        if used&&!busy{self.status=format!("Profil jest używany przez: {}. Najpierw zmień profil tych przestrzeni.",self.used_names(uses));}
        let copy=profile_copy();ffi::ProfileControls{create:enabled,rename:enabled&&selected.is_some()&&self.workspace.is_empty(),remove:enabled&&selected.is_some()&&!used&&profiles.len()>1,picker:enabled,delete_label:if self.confirmation==self.selected&&!self.confirmation.is_empty(){copy.confirm_delete}else{copy.remove_label},status:self.status.clone(),selected:selected.unwrap_or(0),has_selected:selected.is_some()}
    }
    fn request(&mut self,action:u32,raw_name:&str,ready:bool,busy:bool,profiles:&[ffi::ProfileOption],uses:&[ffi::ProfileUse])->ffi::ProfileRequest {
        let none=||ffi::ProfileRequest{action:ffi::ProfileAction::None,id:String::new(),name:String::new(),workspace:String::new()};
        if !ready||busy||self.submitting{self.confirmation.clear();self.status="Poczekaj na zakończenie operacji na profilach.".into();return none();}
        if action>2{return none();}
        let selected=profiles.iter().find(|p|p.id==self.selected);
        if action==2 {
            if selected.is_none(){self.confirmation.clear();self.status="Wybierz istniejący profil.".into();return none();}
            if uses.iter().any(|w|w.profile==self.selected){self.confirmation.clear();self.status=format!("Profil jest używany przez: {}. Najpierw zmień profil tych przestrzeni.",self.used_names(uses));return none();}
            if profiles.len()<=1{self.confirmation.clear();self.status="Nie można usunąć ostatniego profilu.".into();return none();}
            if self.confirmation!=self.selected {self.confirmation=self.selected.clone();self.status="Usunięte zostaną logowania, cookies i dane stron tego profilu. Tej operacji nie można cofnąć.".into();return ffi::ProfileRequest{action:ffi::ProfileAction::ConfirmDelete,id:self.selected.clone(),name:String::new(),workspace:String::new()};}
            self.confirmation.clear();self.status.clear();self.submitting=true;return ffi::ProfileRequest{action:ffi::ProfileAction::Delete,id:self.selected.clone(),name:String::new(),workspace:String::new()};
        }
        self.confirmation.clear();let name=raw_name.trim();
        if name.is_empty(){self.status="Podaj nazwę profilu.".into();return none();}
        if action==1&&(selected.is_none()||!self.workspace.is_empty()){self.status="Wybierz profil w trybie zarządzania.".into();return none();}
        if action==0&&!self.workspace.is_empty()&&!uses.iter().any(|w|w.workspace==self.workspace){self.status="Przestrzeń nie jest już dostępna.".into();return none();}
        if profiles.iter().any(|p|(action==0||p.id!=self.selected)&&p.name.eq_ignore_ascii_case(name)){self.status="Profil o tej nazwie już istnieje.".into();return none();}
        self.status.clear();self.submitting=true;ffi::ProfileRequest{action:if action==0{ffi::ProfileAction::Create}else{ffi::ProfileAction::Rename},id:if action==0{String::new()}else{self.selected.clone()},name:name.into(),workspace:if action==0{self.workspace.clone()}else{String::new()}}
    }
    fn finish_attempt(&mut self,accepted:bool){self.submitting=false;if !accepted&&self.status.is_empty(){self.status="Nie udało się wykonać operacji na profilu. Spróbuj ponownie.".into();}}
}
fn paragraph_layout(width:i32,line_height:i32,count:usize)->ffi::ColumnLayout{let g=notice_layout(width,line_height,count,0,0);ffi::ColumnLayout{width:g.width,height:g.height,children:g.body_lines}}

// Stable UI keys retain selection through model changes. Popup generations and
// captured keys reject stale native callbacks. No browser data crosses.
pub struct SelectState { options: Vec<ffi::SelectOption>, selected: usize, epoch: u64, expanded: bool, prefix: String, last_input: Option<u64>, last_close: Option<u64> }
fn new_select_state(options: Vec<ffi::SelectOption>) -> Box<SelectState> {let mut state=Box::new(SelectState {options:vec![],selected:0,epoch:0,expanded:false,prefix:String::new(),last_input:None,last_close:None});state.replace_options(options,false,false);state}
impl SelectState {
    fn selected(&self)->usize {self.selected}
    fn has_selection(&self)->bool {self.selected<self.options.len()}
    fn option_key(&self,index:usize)->&str {self.options.get(index).map_or("",|option|option.key.as_str())}
    fn selected_key(&self)->&str {self.options.get(self.selected).map_or("",|option|option.key.as_str())}
    fn replace_options(&mut self,options:Vec<ffi::SelectOption>,fallback_last:bool,select_last:bool)->bool {
        let mut keys=BTreeSet::new();if options.iter().any(|option|option.key.is_empty()||!keys.insert(option.key.as_str())){return false;}
        let selected=self.selected_key().to_owned();self.dismiss();self.reset_prefix();
        let last=options.len().saturating_sub(1);
        self.selected=if select_last{last}else{options.iter().position(|option|option.key==selected).unwrap_or(if fallback_last{last}else{0})};
        self.options=options;true
    }
    fn can_choose(&self,token:u64,key:&str,enabled:bool)->bool {enabled&&self.expanded&&token!=0&&token==self.epoch&&self.options.iter().any(|option|option.key==key)}
    fn choose_key(&mut self,token:u64,key:&str,enabled:bool)->bool {if !self.can_choose(token,key,enabled){return false;}self.selected=self.options.iter().position(|option|option.key==key).unwrap();self.expanded=false;true}
    fn close_menu(&mut self,token:u64)->bool {if token==0||token!=self.epoch{return false;}self.dismiss();true}
    fn close_menu_at(&mut self,token:u64,now_us:u64)->bool {if !self.close_menu(token){return false;}self.last_close=Some(now_us);true}
    fn dismiss_popup(&mut self,now_us:u64,popup_present:bool){if self.expanded||popup_present{self.last_close=Some(now_us);}self.dismiss();}
    fn open_pointer(&mut self,enabled:bool,now_us:u64,guard_us:u64)->u64 {
        if self.last_close.is_some_and(|closed|now_us.saturating_sub(closed)<=guard_us){return 0;}
        self.open(enabled)
    }
    fn clear_selection(&mut self){self.dismiss();self.reset_prefix();self.selected=usize::MAX;}
    fn select_index(&mut self,index:usize)->bool {if index>=self.options.len(){return false;}self.dismiss();self.reset_prefix();self.selected=index;true}
    fn open(&mut self,enabled:bool)->u64 {if !enabled||self.options.is_empty(){return 0;}self.dismiss();self.reset_prefix();self.expanded=true;self.epoch}
    fn expanded(&self)->bool {self.expanded}
    fn choose(&mut self,token:u64,index:usize,enabled:bool)->bool {if !enabled||!self.expanded||token==0||token!=self.epoch||index>=self.options.len(){return false;}self.expanded=false;self.selected=index;true}
    fn dismiss(&mut self){self.expanded=false;self.epoch=self.epoch.wrapping_add(1).max(1);}
    fn reset_prefix(&mut self){self.prefix.clear();self.last_input=None;}
    fn input(&mut self,text:&str,now:u64,enabled:bool)->bool {
        if !enabled||self.options.is_empty()||text.is_empty()||text.chars().any(char::is_control){return false;}
        let continuing=self.last_input.is_some_and(|last|now>=last&&now-last<1000);
        let start=if !self.has_selection(){0}else if continuing{self.selected}else{(self.selected+1)%self.options.len()};
        if !continuing{self.prefix.clear();}self.prefix.push_str(text);self.last_input=Some(now);
        for offset in 0..self.options.len(){let i=(start+offset)%self.options.len();if self.options[i].search.starts_with(&self.prefix){let changed=i!=self.selected;self.selected=i;return changed;}}
        false
    }
}
// macOS Combobox opens a menu for these keys rather than changing selection.
fn select_menu_key(key:u32)->bool {matches!(key,32|35|36|38|40)}
fn select_layout(width:i32,height:i32,text_width:i32,control_height:i32,rtl:bool)->ffi::SelectLayout {
    let width=if width<0{text_width.max(0).saturating_add(48)}else{width};let height=if height<0{control_height.max(28)}else{height.max(0)};
    let pad=12.min(width/2);let room=width-pad*2;let arrow_width=16.min(room);let gap=8.min(room-arrow_width);let label_width=room-arrow_width-gap;
    ffi::SelectLayout{width,height,label:ffi::Rect{x:if rtl{pad+arrow_width+gap}else{pad},y:0,width:label_width,height},arrow:ffi::Rect{x:if rtl{pad}else{width-pad-arrow_width},y:0,width:arrow_width,height}}
}

// Display data only. The native manager retains frame/origin/profile authority,
// modal/default-key policy, generation checks and renderer callbacks.
fn page_dialog_copy(kind:u32,unload:bool,origin:&str,profile:&str,message:&[u16])->ffi::PageDialogCopy {
    ffi::PageDialogCopy{title:format!("{}{}",if unload{"Opuścić stronę? · "}else{"Strona: "},origin),identity:format!("Profil: {}",profile),message:message.iter().copied().take(8192).collect(),accept:if unload{"Opuść stronę"}else{"OK"}.into(),cancel:if unload{"Zostań"}else{"Anuluj"}.into(),show_cancel:kind!=0,default_cancel:unload,prompt:kind==2}
}

// Page copy is UI data only; context is a display name, never store authority.
fn tools_copy(page:usize,context:&str)->ffi::ToolsCopy{
    match page.min(3){
        0=>ffi::ToolsCopy{title:if context.is_empty(){"Time Machine".into()}else{format!("Time Machine · {}",context)},intro:"Snapshot zapisuje karty, foldery i Split View w tej przestrzeni. Przywrócenie przeładuje strony; formularze i historia Wstecz nie są częścią snapshotu. Poprzedni układ otrzyma punkt przywracania.".into(),tail:"Uśpienie zwalnia strony tej przestrzeni z pamięci i przełącza do innej. Kliknięcie jej kropki wznowi układ. Niezapisane formularze zostaną utracone.".into(),initial_status:"Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.".into(),fields:vec!["Nazwa snapshotu, np. Research przed deployem".into(),"Szukaj snapshotów i zamkniętych kart".into()],pickers:vec!["Zapisane snapshoty".into(),"Zamknięte karty".into()],headings:vec!["Snapshoty · 30 własnych + 30 automatycznych".into(),"Ostatnio zamknięte · do 100 kart".into()],actions:vec!["Zapisz snapshot".into(),"Przywróć wybrany układ".into(),"Usuń wybrany snapshot".into(),"Przywróć wybraną kartę".into(),"Uśpij tę przestrzeń".into()],choices:vec![]},
        1=>ffi::ToolsCopy{title:"Skróty klawiszowe".into(),intro:"Schemat inspirowany Arc. Wybierz akcję, nagraj kombinację lub wpisz np. Cmd+Shift+J. Puste pole wyłącza skrót. Escape, skróty edycji tekstu i systemowe pozostają zarezerwowane.".into(),tail:"".into(),initial_status:"Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.".into(),fields:vec!["Kombinacja, np. Cmd+Shift+J".into()],pickers:vec!["Akcja skrótu".into()],headings:vec![],actions:vec!["Nagraj kombinację…".into(),"Zapisz skrót".into(),"Wyłącz wybrany skrót".into(),"Przywróć schemat Arc".into()],choices:vec![]},
        2=>ffi::ToolsCopy{title:"Wyszukiwarki i prefiksy".into(),intro:"W pasku adresu wpisz prefiks i zapytanie, np. g koty lub w Warszawa. Szablon musi być adresem HTTP(S) z jednym znacznikiem {query}. Zapytania są kodowane automatycznie.".into(),tail:"".into(),initial_status:"Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.".into(),fields:vec!["Nazwa wyszukiwarki".into(),"Prefiks (a–z, 0–9)".into(),"Adres z {query}".into()],pickers:vec!["Wyszukiwarka do edycji".into()],headings:vec![],actions:vec!["Zapisz wyszukiwarkę".into(),"Ustaw jako domyślną".into(),"Usuń wyszukiwarkę".into()],choices:vec![TOOLS_NEW_PROVIDER.into()]},
        3=>ffi::ToolsCopy{title:"Historia przeglądania".into(),intro:format!("Profil: {}",context),tail:"".into(),initial_status:"Odczytywanie historii…".into(),fields:vec!["Szukaj w historii".into()],pickers:vec!["Zakres czyszczenia historii".into()],headings:vec![],actions:vec!["Wyczyść historię…".into(),"Pokaż więcej".into()],choices:vec!["Ostatnia godzina".into(),"Ostatnie 24 godziny".into(),"Ostatnie 7 dni".into(),"Cały okres".into()]},
        _=>unreachable!(),
    }
}
fn tools_clear_label(busy:bool)->String{if busy{"Czyszczenie…"}else{"Wyczyść historię…"}.into()}
fn tools_clear_controls(busy:bool)->ffi::ToolsClearControls{ffi::ToolsClearControls{label:tools_clear_label(busy),enabled:!busy}}

// UI state only. Native code supplies committed, normalized UTF-16 and retains
// visibility/profile authorization, timers, IME and all browser-service calls.
// Reconstructing a page clears its transient query/recording state. The history
// limit is reset by a fresh request, as in Views, not by page reconstruction.
struct ToolShortcutChoice { key:String, id:String, chord:Vec<u16> }
struct ToolProviderChoice { key:String, item:Option<ffi::ToolsProviderItem> }
fn tools_invalid_choice()->ffi::ToolsChoiceDraft {ffi::ToolsChoiceDraft{valid:false,page:0,create:false,target:String::new(),chord:vec![],name:vec![],keyword:vec![],url:vec![],stop_recording:false}}
const TOOLS_NEW_PROVIDER:&str="Dodaj nową wyszukiwarkę…";
pub struct ToolsForm {
    page: usize,
    recording: bool,
    history_query: Vec<u16>,
    timeline_query: Vec<u16>,
    timeline_epoch: u64,
    choice_epoch: u64,
    active_choice: String,
    shortcut_choices: Vec<ToolShortcutChoice>,
    provider_choices: Vec<ToolProviderChoice>,
    timeline_snapshots: Vec<(String,String)>,
    timeline_closed: Vec<(String,String)>,
    history_limit: usize,
    history_loaded: bool,
}
fn new_tools_form() -> Box<ToolsForm> {
    Box::new(ToolsForm { page: 0, recording: false, history_query: vec![],
        timeline_query: vec![], choice_epoch: 0, active_choice:String::new(), shortcut_choices:vec![], provider_choices:vec![], timeline_epoch: 0, timeline_snapshots: vec![], timeline_closed: vec![], history_limit: 200, history_loaded: false })
}
fn tools_form_node(kind:ffi::ToolsNodeKind,role:ffi::ToolsNodeRole,text:&str,action:i32)->ffi::ToolsFormNode {ffi::ToolsFormNode{kind,role,text:text.encode_utf16().collect(),action,choices:vec![],enabled:true,visible:true,flex:false,spacing:0}}
fn tools_group_actions(nodes:Vec<ffi::ToolsFormNode>)->Vec<ffi::ToolsFormNode>{
    let mut out=vec![];let mut group=false;
    for node in nodes{let action=node.kind==ffi::ToolsNodeKind::Action&&node.role==ffi::ToolsNodeRole::None;
        if group&&!action{out.push(tools_form_node(ffi::ToolsNodeKind::EndActions,ffi::ToolsNodeRole::None,"",0));group=false;}
        if action&&!group{let mut begin=tools_form_node(ffi::ToolsNodeKind::BeginActions,ffi::ToolsNodeRole::None,"",0);begin.spacing=10;out.push(begin);group=true;}
        out.push(node);
    }
    if group{out.push(tools_form_node(ffi::ToolsNodeKind::EndActions,ffi::ToolsNodeRole::None,"",0));}out
}
impl ToolsForm {
    fn tools_select(&mut self, page: usize) {
        let page=page.min(3);
        if self.page!=page{self.timeline_snapshots.clear();self.timeline_closed.clear();self.shortcut_choices.clear();self.provider_choices.clear();self.active_choice.clear();}
        self.page=page;
    }
    fn tools_page(&self) -> usize { self.page }
    fn tools_reset(&mut self) {
        self.recording = false;
        self.history_query.clear(); self.timeline_query.clear();
        self.timeline_snapshots.clear(); self.timeline_closed.clear();
        self.shortcut_choices.clear();self.provider_choices.clear();self.active_choice.clear();
        self.history_loaded = false;
    }
    fn tools_record(&mut self, recording: bool) { self.recording = recording; }
    fn tools_recording(&self) -> bool { self.recording }
    // These are UI command IDs and field/selection facts, never domain handles.
    // Native visibility/profile/workspace/generation checks and bounds checks
    // remain mandatory before applying a command to real browser services.
    fn tools_dispatch(&mut self, action:i32, c:&ffi::ToolsActionContext) -> ffi::ToolsDispatch {
        let composing=match action {1=>c.snapshot_composing,11|12=>c.shortcut_composing,20..=22=>c.provider_composing,_=>false};
        if composing {return ffi::ToolsDispatch{admitted:false,action:0,recording:self.recording,sync_recording:false,record_status:String::new()};}
        let allowed=match action {
            1=>c.snapshot_field,2|3=>c.snapshot_selected,4=>c.closed_selected,
            5|13=>true,10=>c.shortcut_field,11|12=>c.shortcut_field&&c.shortcut_selected,
            20..=22=>c.provider_field&&c.provider_selected,_=>false,
        };
        let command=if allowed{action}else{0};
        let sync_recording=(action!=10&&self.recording)||command==10;
        if action!=10 {self.recording=false;} else if command==10 {self.recording=true;}
        ffi::ToolsDispatch{admitted:true,action:command,recording:self.recording,sync_recording,
            record_status:if command==10{"Naciśnij kombinację. Escape anuluje nagrywanie.".into()}else{String::new()}}
    }
    fn tools_outcome(&self,ok:bool,pending:bool)->ffi::ToolsOutcome {
        let status=if ok {if pending{"Oczekiwanie na zgodę na opuszczenie strony…"}else{"Gotowe. Zmiany zostały zastosowane."}}
            else {match self.page {1=>"Nieprawidłowa lub zajęta kombinacja. Zmień ją albo wyłącz skrót drugiej akcji.",2=>"Sprawdź unikalny prefiks, nazwę i adres z {query}. Musi pozostać co najmniej jedna wyszukiwarka.",_=>"Nie można wykonać operacji. Podaj nazwę, wybierz pozycję zgodną z profilem lub zostaw drugą przestrzeń do przełączenia."}};
        ffi::ToolsOutcome{refresh:ok,status:status.into()}
    }
    fn tools_history_view(&self,count:usize,more:bool,error:bool)->ffi::ToolsHistoryView {
        let empty=if count!=0 {""}else if error{"Nie udało się odczytać historii."}else if self.history_query.is_empty(){"Brak historii przeglądania w tym profilu."}else{"Brak wyników dla tego wyszukiwania."};
        ffi::ToolsHistoryView{empty:empty.into(),status:if error{"Spróbuj ponownie otworzyć historię.".into()}else{format!("Wyświetlono wpisów: {}",count)},more_visible:more&&!error,more_enabled:!error}
    }

    fn tools_history_edit(&mut self, query: Vec<u16>) {
        self.history_query = query; self.history_loaded = false;
    }
    fn tools_history_request(&mut self, more: bool) -> usize {
        self.history_limit = if more { self.history_limit.saturating_add(200) } else { 200 };
        self.history_loaded = false; self.history_limit
    }
    fn tools_history_query(&self) -> Vec<u16> { self.history_query.clone() }
    fn tools_history_accept(&mut self, query: &[u16], limit: usize) -> bool {
        if query != self.history_query || limit != self.history_limit { return false; }
        self.history_loaded = true; true
    }
    fn tools_history_loaded(&self) -> bool { self.history_loaded }
    fn tools_form_plan(&self,context:&str,busy:bool,sleeping:Vec<ffi::ToolsSleepingCopy>)->ffi::ToolsFormPlan {
        use ffi::ToolsNodeKind as K;use ffi::ToolsNodeRole as R;
        let copy=tools_copy(self.page,context);let mut nodes=vec![tools_form_node(K::Hint,R::None,&copy.intro,0)];
        match self.page {
            0=>{
                nodes.push(tools_form_node(K::Editor,R::SnapshotName,&copy.fields[0],0));nodes.push(tools_form_node(K::Action,R::None,&copy.actions[0],1));nodes.push(tools_form_node(K::Editor,R::TimelineFilter,&copy.fields[1],0));
                nodes.push(tools_form_node(K::Heading,R::None,&copy.headings[0],0));let mut picker=tools_form_node(K::Choice,R::SnapshotChoice,&copy.pickers[0],0);picker.choices.push("Brak pozycji".into());nodes.push(picker);
                nodes.push(tools_form_node(K::Action,R::None,&copy.actions[1],2));nodes.push(tools_form_node(K::Action,R::None,&copy.actions[2],3));nodes.push(tools_form_node(K::Heading,R::None,&copy.headings[1],0));
                let mut picker=tools_form_node(K::Choice,R::ClosedChoice,&copy.pickers[1],0);picker.choices.push("Brak pozycji".into());nodes.push(picker);nodes.push(tools_form_node(K::Action,R::None,&copy.actions[3],4));nodes.push(tools_form_node(K::Hint,R::None,&copy.tail,0));nodes.push(tools_form_node(K::Action,R::None,&copy.actions[4],5));
                for item in sleeping{let mut text=item.title;text.extend(" · ".encode_utf16());text.extend(item.detail);let mut node=tools_form_node(K::Hint,R::None,"",0);node.text=text;nodes.push(node);}
            }
            1=>{nodes.push(tools_form_node(K::Choice,R::ShortcutChoice,&copy.pickers[0],0));nodes.push(tools_form_node(K::Editor,R::ShortcutChord,&copy.fields[0],0));for (i,text) in copy.actions.iter().enumerate(){nodes.push(tools_form_node(K::Action,R::None,text,10+i as i32));}}
            2=>{nodes.push(tools_form_node(K::Choice,R::ProviderChoice,&copy.pickers[0],0));for (i,role) in [R::ProviderName,R::ProviderKeyword,R::ProviderUrl].into_iter().enumerate(){nodes.push(tools_form_node(K::Editor,role,&copy.fields[i],0));}for (i,text) in copy.actions.iter().enumerate(){nodes.push(tools_form_node(K::Action,R::None,text,20+i as i32));}}
            _=>{
                nodes.push(tools_form_node(K::Editor,R::HistoryQuery,&copy.fields[0],0));let mut begin=tools_form_node(K::BeginRow,R::None,"",0);begin.spacing=8;nodes.push(begin);
                let mut period=tools_form_node(K::Choice,R::HistoryPeriod,&copy.pickers[0],0);period.choices=copy.choices;period.flex=true;nodes.push(period);
                let clear=tools_clear_controls(busy);let mut action=tools_form_node(K::Action,R::HistoryClear,&clear.label,30);action.enabled=clear.enabled;nodes.push(action);nodes.push(tools_form_node(K::EndRow,R::None,"",0));
                let mut rows=tools_form_node(K::Rows,R::HistoryRows,"",0);rows.spacing=4;nodes.push(rows);let mut more=tools_form_node(K::Action,R::HistoryMore,&copy.actions[1],31);more.visible=false;nodes.push(more);
            }
        }
        ffi::ToolsFormPlan{title:copy.title.encode_utf16().collect(),status:copy.initial_status.encode_utf16().collect(),nodes:tools_group_actions(nodes)}
    }
    fn tools_shortcut_choices(&mut self,items:Vec<ffi::ToolsShortcutItem>)->ffi::ToolsFormChoices {
        self.shortcut_choices.clear();self.active_choice.clear();let mut choices=vec![];
        if let Some(epoch)=self.choice_epoch.checked_add(1){self.choice_epoch=epoch;
            for item in items {let key=format!("form:{}:1:{}",epoch,choices.len());let mut label=item.title;label.extend(" — ".encode_utf16());label.extend_from_slice(&item.chord);self.shortcut_choices.push(ToolShortcutChoice{key:key.clone(),id:item.id,chord:item.chord});choices.push(ffi::ToolsFormChoice{key,label});}
        }
        let enabled=!choices.is_empty();if enabled{self.active_choice=choices[0].key.clone();}else{choices.push(ffi::ToolsFormChoice{key:"empty".into(),label:"Brak pozycji".encode_utf16().collect()});}
        ffi::ToolsFormChoices{choices,enabled}
    }
    fn tools_provider_choices(&mut self,items:Vec<ffi::ToolsProviderItem>)->ffi::ToolsFormChoices {
        self.provider_choices.clear();self.active_choice.clear();let mut choices=vec![];
        if let Some(epoch)=self.choice_epoch.checked_add(1){self.choice_epoch=epoch;
            let key=format!("form:{}:2:0",epoch);self.provider_choices.push(ToolProviderChoice{key:key.clone(),item:None});choices.push(ffi::ToolsFormChoice{key,label:TOOLS_NEW_PROVIDER.encode_utf16().collect()});
            for item in items {let key=format!("form:{}:2:{}",epoch,choices.len());let mut label=item.keyword.clone();label.extend(" · ".encode_utf16());label.extend_from_slice(&item.name);if item.default_provider{label.extend(" · domyślna".encode_utf16());}self.provider_choices.push(ToolProviderChoice{key:key.clone(),item:Some(item)});choices.push(ffi::ToolsFormChoice{key,label});}
        }
        let enabled=!choices.is_empty();if enabled{self.active_choice=choices[0].key.clone();}else{choices.push(ffi::ToolsFormChoice{key:"empty".into(),label:"Brak pozycji".encode_utf16().collect()});}
        ffi::ToolsFormChoices{choices,enabled}
    }
    fn tools_choice_select(&mut self,key:&str)->ffi::ToolsChoiceDraft {
        let known=match self.page{1=>self.shortcut_choices.iter().any(|row|row.key==key),2=>self.provider_choices.iter().any(|row|row.key==key),_=>false};
        if !known{return tools_invalid_choice();}self.active_choice=key.into();
        let draft=self.tools_choice_draft(key);if draft.stop_recording{self.recording=false;}draft
    }
    fn tools_choice_draft(&self,key:&str)->ffi::ToolsChoiceDraft {
        if key.is_empty()||self.active_choice!=key{return tools_invalid_choice();}
        if self.page==1 {if let Some(row)=self.shortcut_choices.iter().find(|row|row.key==key&&!row.id.is_empty()) {return ffi::ToolsChoiceDraft{valid:true,page:1,create:false,target:row.id.clone(),chord:row.chord.clone(),name:vec![],keyword:vec![],url:vec![],stop_recording:true};}}
        if self.page==2 {if let Some(row)=self.provider_choices.iter().find(|row|row.key==key) {return match &row.item{None=>ffi::ToolsChoiceDraft{valid:true,page:2,create:true,..tools_invalid_choice()},Some(item) if !item.id.is_empty()=>ffi::ToolsChoiceDraft{valid:true,page:2,create:false,target:item.id.clone(),name:item.name.clone(),keyword:item.keyword.clone(),url:item.url.clone(),chord:vec![],stop_recording:false},_=>tools_invalid_choice()};}}
        tools_invalid_choice()
    }
    fn tools_timeline_edit(&mut self, query: Vec<u16>) {
        self.timeline_query=query;
        self.timeline_snapshots.clear();self.timeline_closed.clear();
    }
    fn tools_timeline_query(&self) -> Vec<u16> { self.timeline_query.clone() }
    // Only opaque IDs and presentation metadata cross the bridge. Unicode
    // normalization stays in the native service; matching preserves UTF-16
    // units, including partial surrogates, exactly like native find().
    fn tools_timeline_project(&mut self, closed:bool, items:Vec<ffi::ToolsTimelineItem>)->ffi::ToolsTimelineList {
        let targets=if closed{&mut self.timeline_closed}else{&mut self.timeline_snapshots};
        targets.clear();let mut choices=vec![];
        if let Some(epoch)=self.timeline_epoch.checked_add(1){
            self.timeline_epoch=epoch;
            for item in items {
                if !self.timeline_query.is_empty()&&!item.search.windows(self.timeline_query.len()).any(|text|text==self.timeline_query.as_slice()){continue;}
                let key=format!("timeline:{}:{}:{}",epoch,u8::from(closed),choices.len());
                let mut label=item.title;label.extend(" — ".encode_utf16());label.extend(item.detail);
                targets.push((key.clone(),item.id));choices.push(ffi::ToolsTimelineChoice{key,label});
            }
        }
        let enabled=!choices.is_empty();
        if !enabled{choices.push(ffi::ToolsTimelineChoice{key:"empty".into(),label:"Brak zapisanych pozycji".encode_utf16().collect()});}
        ffi::ToolsTimelineList{choices,enabled}
    }
    fn tools_timeline_target(&self,closed:bool,key:&str)->String {
        // A UI key is valid only in its current list/projection. Native callers
        // must still validate visibility, workspace/profile and the domain ID.
        if self.page!=0{return String::new();}
        let targets=if closed{&self.timeline_closed}else{&self.timeline_snapshots};
        targets.iter().find(|(candidate,_)|candidate==key).map_or_else(String::new,|(_,id)|id.clone())
    }

}

// Query and keywords arrive normalized by the native Unicode/whitespace service.
// Preserve UTF-16 units, including partial surrogates, exactly as native find().
// Opaque profile identity scopes UI drafts only. The native adapter retains all
// store access, policy/authorization and actual mutations. Text itself lives in
// the shared Rust editor; this controller owns whether refresh may replace it.
// Static UI catalogue only. Browser preference keys are still admitted by the
// native store; a descriptor never authorizes a write or a platform operation.
fn preference_spec(category:usize,kind:ffi::PreferenceRowKind,key:&str,title:&str,detail:&str,choices:Vec<String>,placeholder:&str,action:ffi::PreferenceActionKind)->ffi::PreferenceSpec {
    use ffi::PreferenceRowKind as K;
    let (button,button_ax)=match kind {K::Text=>("Zapisz",format!("Zapisz: {}",title)),K::Action=>("Otwórz →",title.into()),K::Directory=>("Zmień…","Zmień folder pobierania".into()),_=>("",String::new())};
    ffi::PreferenceSpec{category,kind,key:key.into(),title:title.into(),detail:detail.into(),choices,placeholder:placeholder.into(),action,button:button.into(),button_ax,tooltip:if kind==K::ShortcutHint{"Skróty klawiaturowe Zitter".into()}else{String::new()}}
}
fn preference_site_policies(index:usize)->Vec<String> {
    let mut choices=vec!["Użyj domyślnej".into(),"Zezwalaj".into(),"Blokuj".into()];if index>=4{choices.push("Pytaj".into());}choices
}
fn preference_catalogue(system_certificates:bool)->ffi::PreferenceCatalogue {
    use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;
    let mut rows=vec![];
    rows.push(preference_spec(0,K::Theme,"","Motyw aplikacji","Kolory interfejsu i stron obsługujących motyw systemowy.",vec!["Systemowy".into(),"Jasny".into(),"Ciemny".into()],"",A::None));
    rows.push(preference_spec(0,K::Pinned,"","Zawsze widoczny panel kart","Przypnij karty i przestrzenie przy lewej krawędzi.",vec![],"",A::None));
    rows.push(preference_spec(0,K::Animations,"","Animacje przestrzeni","Płynne przejścia. Systemowe ograniczenie ruchu ma pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(1,K::Search,"","Domyślna wyszukiwarka","Używana, gdy wpiszesz zapytanie w pasku adresu.",vec!["DuckDuckGo".into(),"Google".into(),"Bing".into()],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"referrers","Przekazuj adres strony odsyłającej","Nagłówek Referer pomaga witrynie rozpoznać, skąd pochodzi wejście. Wyłączenie może zmienić działanie niektórych serwisów.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"https_only","Zawsze używaj HTTPS","Wymagaj bezpiecznego połączenia. Witryny bez HTTPS mogą wyświetlić ostrzeżenie.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"third_party_cookies","Blokuj cookies innych firm","Ogranicz śledzenie między witrynami. Niektóre logowania mogą wymagać cookies.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"do_not_track","Wysyłaj Do Not Track","Poproś witryny o nieśledzenie. Respektowanie prośby zależy od witryny.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"preconnect","Wstępne połączenia ze stronami","Przygotowuj połączenie podczas wpisywania adresu. Może użyć sieci, nawet jeśli nie otworzysz strony. Ustawienie dotyczy tego profilu.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"link_auditing","Raporty kliknięć w linki","Zezwalaj na wysyłanie sygnałów ping po kliknięciu odnośnika.",vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"camera","Kamera","Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"microphone","Mikrofon","Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"location","Lokalizacja","Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"notifications","Powiadomienia","Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"clipboard","Odczyt schowka","Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.",vec![],"",A::None));
    rows.push(preference_spec(4,K::Picker,"zoom","Domyślne powiększenie","Skala stron bez własnego, zapisanego powiększenia.",vec!["75%".into(),"90%".into(),"100%".into(),"110%".into(),"125%".into(),"150%".into(),"175%".into(),"200%".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"font_size","Rozmiar tekstu","Domyślna wielkość czcionki na stronach.",vec!["12 px".into(),"14 px".into(),"16 px".into(),"18 px".into(),"20 px".into(),"24 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"minimum_font_size","Minimalny rozmiar tekstu","Zapobiegaj wyświetlaniu zbyt drobnych liter.",vec!["Bez limitu".into(),"10 px".into(),"12 px".into(),"14 px".into(),"16 px".into(),"18 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"fixed_font_size","Rozmiar tekstu o stałej szerokości","Domyślna wielkość tekstu w blokach kodu i elementach monospace.",vec!["12 px".into(),"14 px".into(),"16 px".into(),"18 px".into(),"20 px".into(),"24 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Text,"font_standard","Czcionka standardowa","Nazwa rodziny czcionki używanej, gdy witryna nie wybierze własnej.",vec![],"Np. Times New Roman",A::None));
    rows.push(preference_spec(4,K::Text,"font_serif","Czcionka szeryfowa","Rodzina dla tekstu serif. Musi być zainstalowana w systemie.",vec![],"Np. Georgia",A::None));
    rows.push(preference_spec(4,K::Text,"font_sans","Czcionka bezszeryfowa","Rodzina dla tekstu sans-serif. Musi być zainstalowana w systemie.",vec![],"Np. Arial",A::None));
    rows.push(preference_spec(4,K::Text,"font_fixed","Czcionka o stałej szerokości","Rodzina monospace, używana m.in. w blokach kodu.",vec![],"Np. Menlo",A::None));
    rows.push(preference_spec(4,K::Toggle,"javascript","JavaScript","Skrypty potrzebne do działania większości aplikacji internetowych.",vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"images","Wyświetlaj obrazy","Pozwól witrynom ładować zdjęcia i grafiki.",vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"popups","Wyskakujące okna","Pozwól stronom otwierać dodatkowe okna. Domyślnie zablokowane.",vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"sound","Dźwięk na stronach","Domyślna zgoda na odtwarzanie dźwięku.",vec![],"",A::None));
    rows.push(preference_spec(5,K::Toggle,"save_passwords","Proponuj zapisywanie haseł","Zapisuj dane logowania w menedżerze haseł tego profilu.",vec![],"",A::None));
    rows.push(preference_spec(5,K::Toggle,"auto_signin","Automatyczne logowanie","Korzystaj z zapisanych danych logowania, gdy witryna na to pozwala.",vec![],"",A::None));
    rows.push(preference_spec(2,K::Action,"","Historia przeglądania","Wyszukuj odwiedzone strony i czyść historię tego profilu.",vec![],"",A::History));
    rows.push(preference_spec(0,K::Action,"","Kolor i nazwa przestrzeni","Własny akcent kolorystyczny i przypisanie profilu.",vec![],"",A::Workspace));
    rows.push(preference_spec(5,K::Action,"","Zapisane hasła","Przeglądaj i usuwaj loginy aktywnego profilu.",vec![],"",A::Passwords));
    rows.push(preference_spec(5,K::Action,"","Profile przeglądania","Oddziel cookies, historię i dane logowania między profilami.",vec![],"",A::Profiles));
    rows.push(preference_spec(5,K::Action,"","Import z przeglądarki","Przenieś profile, przestrzenie, foldery i zakładki.",vec![],"",A::Import));
    rows.push(preference_spec(6,K::Toggle,"download_prompt","Pytaj, gdzie zapisać każdy plik","Wybieraj miejsce zapisu przy każdym pobieraniu.",vec![],"",A::None));
    rows.push(preference_spec(6,K::Directory,"","Folder pobierania","Domyślne miejsce zapisu plików w tym profilu.",vec![],"",A::None));
    rows.push(preference_spec(7,K::Text,"accept_languages","Preferowane języki witryn","Kody języków w kolejności preferencji, oddzielone przecinkami. Np. pl, en-US, en. Zmiana dotyczy treści witryn; interfejs Zitter pozostaje po polsku.",vec![],"pl, en-US, en",A::None));
    rows.push(preference_spec(7,K::Toggle,"spellcheck","Sprawdzaj pisownię","Podkreślaj błędy w polach tekstowych na stronach. Na macOS słowniki wybiera system.",vec![],"",A::None));
    rows.push(preference_spec(7,K::Toggle,"online_spellcheck","Rozszerzone sprawdzanie pisowni","Wysyłaj wpisywany tekst do usługi Google w celu sprawdzania pisowni. Wymaga włączonego sprawdzania pisowni.",vec![],"",A::None));
    rows.push(preference_spec(9,K::Toggle,"caret_browsing","Nawigacja kursorem tekstowym","Poruszaj się po tekstach witryn za pomocą klawiatury i zaznaczaj je bez myszy.",vec![],"",A::None));
    rows.push(preference_spec(9,K::Toggle,"hardware_acceleration","Akceleracja sprzętowa","Używaj procesora graficznego do renderowania. Ustawienie wspólne dla całej aplikacji; działa po ponownym uruchomieniu Zitter.",vec![],"",A::None));
    rows.push(preference_spec(9,K::Picker,"dns_mode","Bezpieczny DNS","Wspólne dla całej aplikacji. Tryb automatyczny może używać DNS systemowego. Tryb ścisły wymaga wcześniej zapisanego adresu DNS-over-HTTPS.",vec!["Systemowy".into(),"Automatyczny".into(),"Ścisły (DoH)".into()],"",A::None));
    rows.push(preference_spec(9,K::Text,"dns_templates","Adres serwera DNS-over-HTTPS","Wspólny dla całej aplikacji. Wpisz szablon HTTPS, np. https://dns.google/dns-query{?dns}. Najpierw zapisz adres, następnie włącz tryb ścisły.",vec![],"https://…/dns-query{?dns}",A::None));
    rows.push(preference_spec(9,K::Action,"","Ustawienia proxy systemowego","Zitter korzysta z konfiguracji proxy systemu operacyjnego.",vec![],"",A::Proxy));
    if system_certificates{rows.push(preference_spec(9,K::Action,"","Certyfikaty systemowe","Otwórz systemowy magazyn certyfikatów i ustawienia zaufania.",vec![],"",A::Certificates));}
    rows.push(preference_spec(3,K::SiteEditor,"","Wyjątek dla witryny","Wpisz pełny adres witryny bez ścieżki, np. https://example.com. Wyjątek zastępuje politykę domyślną w tym profilu.",vec![],"",A::None));
    rows.push(preference_spec(3,K::SiteRules,"","Zapisane wyjątki","Własne reguły możesz usunąć, aby wrócić do ustawień domyślnych. Reguły zarządzane pozostają tylko do odczytu.",vec![],"",A::None));
    rows.push(preference_spec(8,K::ClearData,"","Wyczyść dane przeglądania","Dotyczy wyłącznie aktywnego profilu. Pobrane pliki, zapisane hasła i przypięte karty pozostaną zachowane. Przed usunięciem zobaczysz potwierdzenie.",vec![],"",A::None));
    rows.push(preference_spec(1,K::Action,"","Wyszukiwarki i prefiksy","Własne wyszukiwarki: g zapytanie, w hasło, yt film…",vec![],"",A::Providers));
    rows.push(preference_spec(1,K::Action,"","Skróty klawiszowe","Schemat Arc, własne kombinacje i wykrywanie konfliktów.",vec![],"",A::Shortcuts));
    rows.push(preference_spec(0,K::Action,"","Time Machine przestrzeni","Snapshoty, zamknięte karty i usypianie całych projektów.",vec![],"",A::TimeMachine));
    rows.push(preference_spec(1,K::ShortcutHint,"","Szybciej z klawiaturą","⌘ L  adres i wyszukiwanie\n⌘ K  karty, przestrzenie i historia\n⌘ T  nowa karta     ⌘ ,  preferencje",vec![],"",A::None));
    ffi::PreferenceCatalogue{
        categories:vec!["Wygląd".into(),"Wyszukiwanie".into(),"Prywatność".into(),"Uprawnienia stron".into(),"Treści i czytelność".into(),"Hasła i profile".into(),"Pobieranie".into(),"Języki i pisownia".into(),"Dane przeglądania".into(),"Zaawansowane".into()],descriptions:vec!["Dopasuj Zitter do swojego rytmu.".into(),"Wybierz, gdzie zaczyna się każde wyszukiwanie.".into(),"Zdecyduj, jakie ślady zostawiasz w sieci.".into(),"Ustal domyślny dostęp witryn do urządzenia.".into(),"Wygodne czytanie i kontrola zawartości stron.".into(),"Oddzielne logowania, wspólne możliwości.".into(),"Wybierz miejsce dla pobieranych plików.".into(),"Wybierz języki witryn i pomoc podczas pisania.".into(),"Kontroluj dane zapisane w bieżącym profilu.".into(),"Dostępność, połączenia i ustawienia silnika Chromium.".into()],rows,
        site:ffi::PreferenceSiteCopy{origin_ax:"Adres witryny dla wyjątku".into(),placeholder:"https://example.com".into(),type_ax:"Rodzaj uprawnienia dla witryny".into(),policy_ax:"Polityka dla witryny".into(),save:"Zapisz wyjątek".into(),types:vec!["JavaScript".into(),"Obrazy".into(),"Wyskakujące okna".into(),"Dźwięk".into(),"Powiadomienia".into(),"Lokalizacja".into(),"Kamera".into(),"Mikrofon".into(),"Schowek".into()]},
        clear:ffi::PreferenceClearCopy{period_ax:"Zakres czasu czyszczenia danych".into(),periods:vec!["Ostatnia godzina".into(),"Ostatnie 24 godziny".into(),"Ostatnie 7 dni".into(),"Cały okres".into()],checks:vec!["Historia przeglądania".into(),"Cookies i dane witryn (może wylogować)".into(),"Pamięć podręczna".into()],action:"Wyczyść…".into()}
    }
}
pub struct PreferencesForm { categories: usize, selected: usize, query: Vec<u16>, profile: String, dirty: std::collections::BTreeSet<String>, rule_limit: usize }
fn new_preferences_form(categories:usize)->Box<PreferencesForm>{Box::new(PreferencesForm{categories,selected:0,query:vec![],profile:String::new(),dirty:Default::default(),rule_limit:50})}
impl PreferencesForm {
    fn preference_select(&mut self,category:usize)->bool{if category>=self.categories{return false;}self.selected=category;self.query.clear();true}
    fn preference_current(&self)->usize{self.selected}
    fn preference_profile(&mut self,profile:&str)->bool {
        if self.profile==profile{return false;}self.profile=profile.into();self.dirty.clear();self.preference_reset_rules();true
    }
    fn preference_detach(&mut self){self.profile.clear();self.dirty.clear();}
    fn preference_edited(&mut self,key:&str){self.dirty.insert(key.into());}
    fn preference_saved(&mut self,key:&str,ok:bool){if ok{self.dirty.remove(key);}}
    fn preference_sync_text(&mut self,key:&str,present:bool,editable:bool,composing:bool,profile_changed:bool)->bool{
        let replace=present&&((!self.dirty.contains(key)&&!composing)||!editable||profile_changed);
        if replace{self.dirty.remove(key);}replace
    }
    fn preference_can_save(&self,enabled:bool,composing:bool)->bool{enabled&&!composing}
    fn preference_reset_rules(&mut self){self.rule_limit=50;}
    fn preference_more_rules(&mut self){self.rule_limit=self.rule_limit.saturating_add(50);}
    fn preference_rule_limit(&self)->usize{self.rule_limit}
    fn preference_filter(&mut self,query:Vec<u16>,rows:&[ffi::PreferenceRow])->ffi::PreferenceVisibility{
        self.query=query;let searching=!self.query.is_empty();let mut sections=vec![false;self.categories];
        let visible=rows.iter().map(|row|{let found=row.category<self.categories&&if searching{row.keywords.windows(self.query.len()).any(|text|text==self.query.as_slice())}else{row.category==self.selected};if found{sections[row.category]=true;}found}).collect::<Vec<_>>();
        let navigation=(0..self.categories).map(|category|!searching&&category==self.selected).collect();let any=sections.iter().any(|value|*value);
        ffi::PreferenceVisibility{rows:visible,sections,navigation,any}
    }
}

// Targets are authoritative immediately; only visual interpolation is timed.
struct ToggleTransition { from:f32, target:bool, started:Option<u64>, duration:u64 }
impl ToggleTransition {
 fn progress(&self,now:u64)->f32 {let to=if self.target{1.0}else{0.0};self.started.map_or(to,|start|{let t=(now.saturating_sub(start) as f32/self.duration as f32).clamp(0.0,1.0);self.from+(to-self.from)*t})}
 fn set(&mut self,target:bool){self.target=target;self.from=if target{1.0}else{0.0};self.started=None;}
 fn animate(&mut self,target:bool,now:u64,animate:bool){if target==self.target{return;}self.from=self.progress(now);self.target=target;self.started=if animate{Some(now)}else{None};}
 fn animating(&self,now:u64)->bool {self.started.is_some_and(|start|now.saturating_sub(start)<self.duration)}
}
pub struct ToggleState { slide:ToggleTransition, hover:ToggleTransition }
fn new_toggle_state()->Box<ToggleState>{Box::new(ToggleState{slide:ToggleTransition{from:0.0,target:false,started:None,duration:80},hover:ToggleTransition{from:0.0,target:false,started:None,duration:250}})}
impl ToggleState {
 fn toggle_checked(&self)->bool{self.slide.target}
 fn toggle_set(&mut self,checked:bool){self.slide.set(checked);}
 fn toggle_flip(&mut self,enabled:bool,now:u64,animate:bool)->bool{if !enabled{return false;}self.slide.animate(!self.slide.target,now,animate);true}
 fn toggle_hover(&mut self,hovered:bool,now:u64,animate:bool){self.hover.animate(hovered,now,animate);}
 fn toggle_progress(&self,now:u64)->f32{self.slide.progress(now)}
 fn toggle_hover_progress(&self,now:u64)->f32{self.hover.progress(now)}
 fn toggle_animating(&self,now:u64)->bool{self.slide.animating(now)||self.hover.animating(now)}
 fn toggle_finish(&mut self){self.slide.set(self.slide.target);self.hover.set(false);}
}
fn toggle_scene(s:&ffi::ToggleStyle)->Vec<ffi::Primitive>{
 if ![s.width,s.height,s.progress,s.hover,s.clip.x,s.clip.y,s.clip.width,s.clip.height].iter().all(|v|v.is_finite())||s.width<=0.0||s.height<=0.0{return vec![];}
 let p=s.progress.clamp(0.0,1.0);let hover=s.hover.clamp(0.0,1.0);
 let blend=|on:u32,off:u32,t:f32|{let mut color=0;for shift in [0,8,16,24]{let a=((on>>shift)&255) as f32;let b=((off>>shift)&255) as f32;color|=((b+(a-b)*t).round() as u32)<<shift;}color};
 let make=|x,y,width,height,radius,color,stroke|ffi::Primitive{x,y,width,height,radius,color,stroke,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,clip:ffi::FloatRect{x:s.clip.x,y:s.clip.y,width:s.clip.width,height:s.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0};
 let w=s.width.min(26.0);let h=s.height.min(16.0);let x=(s.width-w)/2.0;let y=(s.height-h)/2.0;
 let inset=if s.checked{2.0}else{4.0};let diameter=(h-2.0*inset).max(0.0);let pos=if s.rtl{1.0-p}else{p};let tx=x+inset+pos*(w-h).max(0.0);let ty=y+inset;let out=if s.pressed{1.0}else{0.0};
 let mut ops=vec![make(x,y,w,h,h/2.0,blend(s.track_on,s.track_off,p),0.0)];
 if !s.checked{ops.push(make(x+0.5,y+0.5,(w-1.0).max(0.0),(h-1.0).max(0.0),(h-1.0).max(0.0)/2.0,s.border,1.0));}
 if hover>0.0{let cx=tx+diameter/2.0;let cy=ty+diameter/2.0;ops.push(make(cx-10.0,cy-10.0,20.0,20.0,10.0,blend(s.halo,s.halo&0xffffff,hover),0.0));}
 let mut thumb=blend(s.thumb_on,s.thumb_off,p);if s.checked{thumb=blend(s.thumb_hover,thumb,hover);}
 ops.push(make(tx-out,ty-out,diameter+out*2.0,diameter+out*2.0,(diameter+out*2.0)/2.0,thumb,0.0));
 if s.focused{ops.push(make(x-3.0,y-3.0,w+6.0,h+6.0,h/2.0+3.0,s.focus,2.0));}
 ops
}

pub struct CheckboxState { checked: bool }
fn new_checkbox_state(checked: bool) -> Box<CheckboxState> {Box::new(CheckboxState {checked})}
impl CheckboxState {
    fn checked(&self) -> bool {self.checked}
    fn set_checked(&mut self, checked: bool) -> bool {let changed=self.checked!=checked;self.checked=checked;changed}
    fn toggle(&mut self, enabled: bool) -> bool {if !enabled {return false;}self.checked=!self.checked;true}
}
fn checkbox_layout(width: i32, line_height: i32, line_count: usize, left: i32, top: i32, right: i32, bottom: i32, rtl: bool) -> ffi::CheckboxLayout {
    let width=width.max(0);let left=left.max(0).min(width/2);let right=right.max(0).min(width-left);let top=top.max(0);let bottom=bottom.max(0);let line_height=line_height.max(0);
    let inside=width-left-right;let mark_width=inside.min(16);let gap=(inside-mark_width).min(8);let label_width=inside-mark_width-gap;
    let label_height=line_height.saturating_mul(line_count.min(i32::MAX as usize) as i32);let content=label_height.max(16);
    let height=content.saturating_add(top).saturating_add(bottom).saturating_add(4);let content_y=top.min(height);let room=height-content_y;
    let mark_height=16.min(room);let mark_y=content_y+(room.min(content.saturating_add(4))-mark_height).max(0)/2;
    let label_y=content_y+2.min((height-content_y).max(0));let label_x=if rtl {left} else {left+mark_width+gap};
    let mut lines=Vec::with_capacity(line_count);let mut y=label_y;
    for _ in 0..line_count {let h=line_height.min((height-y).max(0));lines.push(ffi::Rect{x:label_x,y,width:label_width,height:h});y=y.saturating_add(h);}
    ffi::CheckboxLayout {width,height,mark:ffi::Rect{x:if rtl {width-right-mark_width} else {left},y:mark_y,width:mark_width,height:mark_height},label:ffi::Rect{x:label_x,y:label_y,width:label_width,height:(y-label_y).max(0)},lines}
}

// UI option snapshots contain no source paths, imported data or credentials.
pub struct ImportForm { selection: ffi::ImportSelection, preview: ffi::ImportSelection, epoch: u64, scheduled: bool, waiting: bool, can_apply: bool, busy: bool, applying: bool, submitted: bool, message: String }
fn import_defaults() -> ffi::ImportSelection { ffi::ImportSelection { browser:0, separate_profiles:true, bookmarks:true, history:false, cookies:false } }
fn import_copy() -> ffi::ImportCopy {
    ffi::ImportCopy { title:"Przenieś się do Zittera".into(), dismiss:"Zamknij import".into(), intro:"Wybierz przeglądarkę i dane, które chcesz przenieść.".into(), source_label:"Przeglądarka źródłowa".into(), source_ax:"Przeglądarka do importu".into(), heading:"Co chcesz przenieść?".into(), profiles:"Oddzielne profile przeglądania".into(), history:"Historia przeglądania".into(), cookies:"Ciasteczka i sesje witryn".into(), apply_label:"Importuj".into(), browsers:vec!["Arc".into(),"Google Chrome".into(),"Microsoft Edge".into(),"Brave".into()] }
}
fn new_import_form() -> Box<ImportForm> { Box::new(ImportForm { selection:import_defaults(), preview:import_defaults(), epoch:0, scheduled:false, waiting:false, can_apply:false, busy:false, applying:false, submitted:false, message:String::new() }) }
impl ImportForm {
    fn dispatch(&self, action: ffi::ImportAction) -> ffi::ImportDispatch {ffi::ImportDispatch {action, epoch:self.epoch, selection:self.selection.clone()}}
    fn next_epoch(&mut self) {self.epoch=self.epoch.wrapping_add(1);if self.epoch==0 {self.epoch=1;}}
    fn select(&mut self, selection: ffi::ImportSelection) -> ffi::ImportDispatch {
        if self.applying||self.submitted {return self.dispatch(ffi::ImportAction::None);}
        self.next_epoch();self.can_apply=false;self.waiting=false;
        if selection.browser>3 {self.scheduled=false;self.busy=false;self.message="Wybierz przeglądarkę źródłową.".into();return self.dispatch(ffi::ImportAction::None);}
        self.selection=selection;self.scheduled=true;self.busy=true;self.message="Odczytywanie danych…".into();self.dispatch(ffi::ImportAction::Preview)
    }
    fn cancel_scan(&mut self) {self.next_epoch();self.scheduled=false;self.waiting=false;}
    fn take_scan(&mut self, epoch: u64, visible: bool, current: ffi::ImportSelection, ready: bool, profiles_busy: bool) -> ffi::ImportDispatch {
        if !self.scheduled||epoch!=self.epoch||self.applying||self.submitted {return self.dispatch(ffi::ImportAction::None);}
        self.scheduled=false;if !visible{return self.dispatch(ffi::ImportAction::None);}
        if current!=self.selection {self.can_apply=false;return self.dispatch(ffi::ImportAction::Rescan);}
        if !ready||profiles_busy {self.waiting=true;self.busy=false;self.message="Poczekaj na zakończenie zapisywania.".into();return self.dispatch(ffi::ImportAction::None);}
        self.busy=true;self.dispatch(ffi::ImportAction::Preview)
    }
    fn should_resume(&self, ready: bool, profiles_busy: bool) -> bool {self.waiting&&ready&&!profiles_busy&&!self.applying&&!self.submitted}
    fn begin_preview(&mut self, selection: ffi::ImportSelection) {self.next_epoch();self.scheduled=false;self.waiting=false;self.preview=selection;self.can_apply=false;self.busy=true;}
    fn set_status(&mut self, message: &str, can_apply: bool, busy: bool, applying: bool) {self.scheduled=false;self.waiting=false;self.submitted=false;self.message=message.into();self.can_apply=can_apply;self.busy=busy;self.applying=applying;}
    fn view(&self, ready: bool, profiles_busy: bool) -> ffi::ImportView {
        let locked=self.applying||self.submitted;ffi::ImportView {message:self.message.clone(),bookmarks_label:if self.selection.browser==0 {"Przypięte karty, foldery i Ulubione"} else {"Zakładki i foldery"}.into(),options_enabled:!locked,close_enabled:!locked,apply_enabled:self.can_apply&&!self.busy&&!locked&&ready&&!profiles_busy&&self.preview==self.selection,applying:locked,busy:self.busy||self.submitted,can_apply:self.can_apply}
    }
    fn request_apply(&mut self, current: ffi::ImportSelection, ready: bool, profiles_busy: bool, visible: bool) -> ffi::ImportDispatch {
        if !visible||self.applying||self.submitted {return self.dispatch(ffi::ImportAction::None);}
        if current!=self.selection||current!=self.preview {self.can_apply=false;return self.dispatch(ffi::ImportAction::Rescan);}
        if !self.view(ready,profiles_busy).apply_enabled {return self.dispatch(ffi::ImportAction::None);}
        self.submitted=true;self.dispatch(ffi::ImportAction::Apply)
    }
    fn finish_apply(&mut self, accepted: bool) {
        // A synchronous service callback may already have supplied progress or
        // completion. Do not overwrite its authoritative state on return.
        if !self.submitted {return;}self.submitted=false;
        if accepted {self.applying=true;self.busy=true;self.can_apply=false;self.message="Importowanie wybranych danych…".into();}
        else {self.message="Nie udało się rozpocząć importu. Spróbuj ponownie.".into();}
    }
}

fn action_button_layout(width: i32, height: i32, text_width: i32, control_height: i32, padding: i32, minimum_width: i32) -> ffi::ActionButtonLayout {
    let padding=padding.max(0);let width=if width<0 {text_width.max(0).saturating_add(padding.saturating_mul(2)).max(minimum_width.max(0))} else {width};
    let height=if height<0 {control_height.max(0)} else {height};let left=padding.min(width/2);
    ffi::ActionButtonLayout {width,height,text:ffi::Rect{x:left,y:0,width:width-left*2,height}}
}
// Dialog action order comes from the platform adapter; linking, trailing
// alignment and placement belong to the Rust scene. Insets/font metrics are
// native theme inputs, not hidden native controls.
fn dialog_actions_layout(width: i32, height: i32, spacing: i32, link_limit: i32, top: i32, left: i32, bottom: i32, right: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    if children.is_empty(){return ffi::ColumnLayout{width:width.max(0),height:height.max(0),children:Vec::new()};}
    let linked=children.iter().map(|c|c.width.max(0)).filter(|&w|w<=link_limit).max().unwrap_or(0);
    let measured:Vec<_>=children.iter().map(|c|ffi::ColumnChild{width:c.width.max(linked),height:c.height.max(0),top:0,left:0,bottom:0,right:0}).collect();
    let natural=action_group_layout(-1,-1,false,spacing,&measured);
    let top=top.max(0);let left=left.max(0);let bottom=bottom.max(0);let right=right.max(0);
    let width=if width<0 {natural.width.saturating_add(left).saturating_add(right)} else {width};
    let height=if height<0 {natural.height.saturating_add(top).saturating_add(bottom)} else {height};
    let inner_width=width.saturating_sub(left).saturating_sub(right).max(0);
    let inner_height=height.saturating_sub(top).saturating_sub(bottom).max(0);
    let mut row=action_group_layout(inner_width.min(natural.width),inner_height,false,spacing,&measured);
    let x=left.min(width).saturating_add(inner_width.saturating_sub(natural.width).max(0));
    for r in &mut row.children {r.x=r.x.saturating_add(x);r.y=top.min(height).saturating_add(inner_height.saturating_sub(r.height));}
    ffi::ColumnLayout{width,height,children:row.children}
}

fn action_group_layout(width: i32, height: i32, vertical: bool, spacing: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    let spacing=spacing.max(0);let gaps=children.len().saturating_sub(1).min(i32::MAX as usize) as i32;
    let sum_width=children.iter().fold(0i64,|s,c|s.saturating_add(i64::from(c.width.max(0))));
    let sum_height=children.iter().fold(0i32,|s,c|s.saturating_add(c.height.max(0)));
    let natural_width=if vertical {children.iter().map(|c|c.width.max(0)).max().unwrap_or(0)} else {sum_width.saturating_add(i64::from(spacing)*i64::from(gaps)).min(i64::from(i32::MAX)) as i32};
    let natural_height=if vertical {sum_height.saturating_add(spacing.saturating_mul(gaps))} else {children.iter().map(|c|c.height.max(0)).max().unwrap_or(0)};
    let width=if width<0 {natural_width} else {width};let height=if height<0 {natural_height} else {height};
    let gap=if !vertical&&gaps>0 {spacing.min(width/gaps)} else {spacing};let available=width-gap.saturating_mul(if vertical {0} else {gaps});
    let mut offset=0i32;let mut prefix=0i128;let mut previous=0i32;let mut rects=Vec::with_capacity(children.len());
    for (index,c) in children.iter().enumerate() {
        if vertical {let h=c.height.max(0).min((height-offset).max(0));rects.push(ffi::Rect{x:0,y:offset,width,height:h});offset=offset.saturating_add(h);if index+1<children.len(){offset=offset.saturating_add(spacing.min(height-offset));}}
        else {prefix+=i128::from(c.width.max(0));let edge=if sum_width>i64::from(available) {((prefix*i128::from(available))/i128::from(sum_width)) as i32} else {prefix.min(i128::from(i32::MAX)) as i32};let w=edge-previous;previous=edge;let h=c.height.max(0).min(height);rects.push(ffi::Rect{x:offset,y:(height-h)/2,width:w,height:h});offset=offset.saturating_add(w);if index+1<children.len(){offset=offset.saturating_add(gap);}}
    }
    ffi::ColumnLayout {width,height,children:rects}
}

const APPEARANCE_COLORS: [u32;4] = [0xff9e91dc,0xff84b6a4,0xffd6a07b,0xff7faacb];
const APPEARANCE_EMOJIS: [&str;6] = ["💼","🏠","🚀","🎨","📚","🎮"];
fn appearance_presets(emoji: bool) -> Vec<ffi::AppearancePreset> {
    if emoji { APPEARANCE_EMOJIS.iter().map(|e|ffi::AppearancePreset { color:0, emoji:(*e).into(), label:format!("Emoji {}",e) }).collect() }
    else { APPEARANCE_COLORS.iter().map(|&c|ffi::AppearancePreset { color:c, emoji:String::new(), label:format!("#{:06X}",c&0xffffff) }).collect() }
}
fn appearance_selected(emoji: bool, index: usize, color: u32, current_emoji: &str) -> bool {
    if emoji { APPEARANCE_EMOJIS.get(index).map(|e|*e==current_emoji).unwrap_or(false) }
    else { APPEARANCE_COLORS.get(index).map(|c|c&0xffffff==color&0xffffff).unwrap_or(false) }
}
fn appearance_row_layout(width: i32, height: i32, editor_width: i32, editor_height: i32, emoji: bool) -> ffi::ColumnLayout {
    let count=if emoji {6} else {4};let button_width=if emoji {30} else {32};let button_height=if emoji {28} else {32};
    let editor_height=editor_height.max(0);let width=if width<0 {(count*button_width+8i32).saturating_add(editor_width.max(0))} else {width};
    let height=if height<0 {button_height.max(editor_height)} else {height};let control_width=width.min(count*button_width);
    let mut children=Vec::with_capacity(count as usize+2);let mut x=0;
    for index in 0..count { let width=control_width/count+if index<control_width%count {1} else {0};let h=button_height.min(height);children.push(ffi::Rect {x,y:(height-h)/2,width,height:h});x+=width; }
    let gap=(width-x).min(8);children.push(ffi::Rect{x,y:0,width:gap,height});x+=gap;
    let h=editor_height.min(height);children.push(ffi::Rect{x,y:(height-h)/2,width:width-x,height:h});
    ffi::ColumnLayout {width,height,children}
}

// A workspace notice targets the captured ID, independently of the currently
// selected workspace. Removing a workspace never emits a profile deletion.
pub struct WorkspaceNotice { workspace: String, title: String, error: String, submitting: bool, closed: bool }
fn new_workspace_notice(workspace: &str, title: &str) -> Box<WorkspaceNotice> {
    Box::new(WorkspaceNotice { workspace: workspace.into(), title: title.into(), error: String::new(), submitting: false, closed: false })
}
impl WorkspaceNotice {
    fn copy(&self) -> ffi::NoticeCopy {
        ffi::NoticeCopy { title: format!("Usunąć przestrzeń „{}”?", self.title),
            body: "Przestrzeń, jej karty i przypięte elementy zostaną usunięte. Tej operacji nie można cofnąć.\n\nProfil przeglądania, hasła, historia i cookies pozostaną zachowane.".into(),
            accept: "Usuń przestrzeń".into(), cancel: "Anuluj".into(), default_cancel: true }
    }
    fn error(&self) -> &str { &self.error }
    fn submit(&mut self, ready: bool, busy: bool, workspaces: &[ffi::ProfileUse]) -> ffi::WorkspaceNoticeRequest {
        let none=||ffi::WorkspaceNoticeRequest { action: ffi::WorkspaceNoticeAction::None, workspace: String::new() };
        if self.closed||self.submitting { return none(); }
        if !ready||busy { self.error="Poczekaj na zakończenie zapisywania.".into(); return none(); }
        if self.workspace.is_empty()||!workspaces.iter().any(|w|w.workspace==self.workspace) { self.error="Przestrzeń nie jest już dostępna.".into(); return none(); }
        if workspaces.len()<=1 { self.error="Nie można usunąć ostatniej przestrzeni.".into(); return none(); }
        self.error.clear();self.submitting=true;
        ffi::WorkspaceNoticeRequest { action: ffi::WorkspaceNoticeAction::Delete, workspace: self.workspace.clone() }
    }
    fn finish_attempt(&mut self, accepted: bool) {
        if !self.submitting { return; }
        self.submitting=false;self.closed=accepted;
        if !accepted { self.error="Nie udało się usunąć przestrzeni. Spróbuj ponownie.".into(); }
    }
    fn cancel(&mut self) { self.closed=true;self.submitting=false; }
}

pub struct FolderNotice { workspace:String, folder:String, title:String, error_only:bool, error:String, submitting:bool, closed:bool }
fn new_folder_notice(workspace:&str,folder:&str,title:&str,error_only:bool)->Box<FolderNotice>{Box::new(FolderNotice{workspace:workspace.into(),folder:folder.into(),title:title.into(),error_only,error:String::new(),submitting:false,closed:false})}
impl FolderNotice {
    fn copy(&self)->ffi::NoticeCopy {
        if self.error_only{return ffi::NoticeCopy{title:"Nie udało się zapisać folderu".into(),body:"Sprawdź wolne miejsce na dysku i spróbuj ponownie.".into(),accept:"OK".into(),cancel:String::new(),default_cancel:false};}
        ffi::NoticeCopy{title:format!("Usunąć folder „{}”?",self.title),body:"Folder i cała jego przypięta zawartość zostaną usunięte. Tej operacji nie można cofnąć. Strony już otwarte w kartach pozostaną otwarte.".into(),accept:"Usuń folder".into(),cancel:"Anuluj".into(),default_cancel:true}
    }
    fn error(&self)->&str{&self.error}
    fn submit(&mut self,current:&str,ready:bool,busy:bool,nodes:&[ffi::TreeNode])->ffi::FolderNoticeRequest {
        let none=||ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::None,workspace:String::new(),folder:String::new()};
        if self.closed||self.submitting{return none();}
        if self.error_only{self.submitting=true;return ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::Acknowledge,workspace:String::new(),folder:String::new()};}
        if self.workspace.is_empty()||self.workspace!=current{self.error="Przestrzeń zmieniła się. Otwórz potwierdzenie ponownie.".into();return none();}
        if !ready||busy{self.error="Poczekaj na zakończenie zapisywania.".into();return none();}
        if self.folder.is_empty()||!nodes.iter().any(|n|n.id==self.folder&&n.folder){self.error="Folder nie jest już dostępny.".into();return none();}
        self.error.clear();self.submitting=true;ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::Delete,workspace:self.workspace.clone(),folder:self.folder.clone()}
    }
    fn finish_attempt(&mut self,accepted:bool){if !self.submitting{return;}self.submitting=false;self.closed=accepted;if !accepted{self.error="Nie udało się usunąć folderu. Spróbuj ponownie.".into();}}
    fn cancel(&mut self){self.closed=true;self.submitting=false;}
}
fn notice_layout(width:i32,body_height:i32,body_count:usize,error_height:i32,error_count:usize)->ffi::NoticeLayout {
    let width=if width<0{400}else{width};let body_height=body_height.max(0);let error_height=error_height.max(0);
    let mut y:i32=0;let mut body_lines=Vec::with_capacity(body_count);
    for _ in 0..body_count{body_lines.push(ffi::Rect{x:0,y,width,height:body_height.min(i32::MAX-y)});y=y.saturating_add(body_height);}
    let body=ffi::Rect{x:0,y:0,width,height:y};if body_count>0&&error_count>0{y=y.saturating_add(8);}
    let top=y;let mut error_lines=Vec::with_capacity(error_count);
    for _ in 0..error_count{error_lines.push(ffi::Rect{x:0,y,width,height:error_height.min(i32::MAX-y)});y=y.saturating_add(error_height);}
    ffi::NoticeLayout{width,height:y,body,error:ffi::Rect{x:0,y:top,width,height:y-top},body_lines,error_lines}
}

pub struct FolderForm { workspace:String, parent:String, folder:String, name:String, error:String, submitting:bool, closed:bool }
fn new_folder_form(workspace:&str,parent:&str,folder:&str,initial:&str)->Box<FolderForm>{Box::new(FolderForm{workspace:workspace.into(),parent:parent.into(),folder:folder.into(),name:initial.into(),error:String::new(),submitting:false,closed:false})}
impl FolderForm {
    fn copy(&self)->ffi::FolderCopy{ffi::FolderCopy{title:if !self.folder.is_empty(){"Zmień nazwę folderu"}else if self.parent.is_empty(){"Nowy folder"}else{"Nowy podfolder"}.into(),label:"Nazwa folderu".into(),accept:if self.folder.is_empty(){"Dodaj folder"}else{"Zapisz nazwę"}.into(),cancel:"Anuluj".into()}}
    fn edit(&mut self,name:&str){if !self.closed&&!self.submitting&&self.name!=name{self.name=name.into();self.error.clear();}}
    fn error(&self)->&str{&self.error}
    fn submit(&mut self,current:&str,ready:bool,busy:bool,nodes:&[ffi::TreeNode],composing:bool)->ffi::FolderRequest {
        let none=||ffi::FolderRequest{action:ffi::FolderAction::None,workspace:String::new(),parent:String::new(),folder:String::new(),name:String::new(),expand_parent:false};
        if self.closed||self.submitting||composing{return none();}
        if self.workspace.is_empty()||self.workspace!=current{self.error="Przestrzeń zmieniła się. Otwórz formularz ponownie.".into();return none();}
        if !ready||busy{self.error="Poczekaj na zakończenie zapisywania.".into();return none();}
        let target=if self.folder.is_empty(){&self.parent}else{&self.folder};
        if (!self.folder.is_empty()&&!self.parent.is_empty())||(!target.is_empty()&&!nodes.iter().any(|n|n.id==*target&&n.folder)){self.error="Folder nie jest już dostępny.".into();return none();}
        // Unicode White_Space matches Chromium's kWhitespaceUTF16 set. The
        // 256-byte UTF-8 limit is the existing application persistence contract.
        let name=self.name.trim();
        if name.is_empty(){self.error="Wpisz nazwę folderu.".into();return none();}
        if name.len()>256{self.error="Nazwa folderu jest za długa.".into();return none();}
        self.error.clear();self.submitting=true;
        ffi::FolderRequest{action:if self.folder.is_empty(){ffi::FolderAction::Create}else{ffi::FolderAction::Rename},workspace:self.workspace.clone(),parent:self.parent.clone(),folder:self.folder.clone(),name:name.into(),expand_parent:!self.parent.is_empty()}
    }
    // This acknowledges application admission, not completion of async storage.
    fn finish_attempt(&mut self,accepted:bool){if !self.submitting{return;}self.submitting=false;self.closed=accepted;if !accepted{self.error="Nie udało się zapisać folderu. Spróbuj ponownie.".into();}}
    fn cancel(&mut self){self.closed=true;self.submitting=false;}
}
fn folder_form_layout(width:i32,label_height:i32,input_height:i32,error_height:i32,error:bool)->ffi::FolderLayout {
    let width=if width<0{400}else{width};let label_height=label_height.max(0);let input_height=input_height.max(32);
    let input_y=label_height.saturating_add(8);let bottom=input_y.saturating_add(input_height);let error_y=bottom.saturating_add(8);
    let error_height=if error{error_height.max(0)}else{0};let height=if error{error_y.saturating_add(error_height)}else{bottom};
    ffi::FolderLayout{width,height,label:ffi::Rect{x:0,y:0,width,height:label_height},input:ffi::Rect{x:0,y:input_y,width,height:input_height.min(i32::MAX-input_y)},error:ffi::Rect{x:0,y:error_y,width,height:error_height.min(i32::MAX-error_y)}}
}

// Stable opaque WebContents UI identities, never browser objects or URLs.
// A surviving tab can move to another index; a reused index is a different target.
fn resolve_row_target(source:&str,current:&str,high:u64,low:u64,pin:&str,tabs:&[ffi::MenuTab],pins:&[ffi::TreeNode])->ffi::RowTarget {
    let none=||ffi::RowTarget{valid:false,has_tab:false,tab:0,pin:String::new()};
    if source.is_empty()||source!=current||((high==0&&low==0)==pin.is_empty()){return none();}
    if high!=0||low!=0{
        if let Some(tab)=tabs.iter().find(|t|t.high==high&&t.low==low){return ffi::RowTarget{valid:true,has_tab:true,tab:tab.index,pin:String::new()};}
    } else if pins.iter().any(|p|p.id==pin&&!p.folder){return ffi::RowTarget{valid:true,has_tab:false,tab:0,pin:pin.into()};}
    none()
}
fn sidebar_drop_accepts(bounds:ffi::Rect,x:i32,y:i32,allowed:bool)->bool {
    if !allowed||bounds.width<=0||bounds.height<=0{return false;}
    let left=bounds.x as i64;let top=bounds.y as i64;let x=x as i64;let y=y as i64;
    x>=left+bounds.width as i64/2 && x<left+bounds.width as i64 && y>=top && y<top+bounds.height as i64
}
fn tab_menu_paired(s:&ffi::TabMenuState)->bool{s.valid&&s.has_tab&&s.split&&((s.has_first&&s.first==s.tab)||(s.has_second&&s.second==s.tab))}
fn tab_menu_enabled(command:u32,s:&ffi::TabMenuState)->bool {
    if !s.valid||!s.ready||s.busy{return false;}
    let paired=tab_menu_paired(s);
    match command {5=>!paired,6|9=>paired,7=>paired&&s.has_second&&s.second==s.tab,8=>paired&&s.has_first&&s.first==s.tab,10=>s.has_tab&&s.tab_count>1,_=>false}
}
fn tab_menu_action(command:u32,s:&ffi::TabMenuState)->ffi::SidebarAction {
    use ffi::SidebarAction as A;if !tab_menu_enabled(command,s){return A::None;}
    match command{5=>A::OpenSplit,6=>A::CloseSplit,7=>A::MoveFirst,8=>A::MoveSecond,9=>A::ToggleOrientation,10=>A::CloseTab,_=>A::None}
}
fn tab_context_menu(s:&ffi::TabMenuState)->Vec<ffi::TabMenuEntry> {
    let mut entries=Vec::new();let mut add=|command,label:&str|entries.push(ffi::TabMenuEntry{command,label:label.into()});
    add(36,"Nowy folder…");add(0,"");add(20,"Otwórz w Peek");
    if tab_menu_paired(s){
        add(7,if s.vertical{"Przesuń w górę"}else{"Przesuń w lewo"});add(8,if s.vertical{"Przesuń w dół"}else{"Przesuń w prawo"});add(0,"");
        add(9,if s.vertical{"Podział poziomy (obok siebie)"}else{"Podział pionowy (jedna nad drugą)"});add(0,"");add(6,"Wyjmij kartę ze splitu");add(10,"Zamknij kartę");
    } else {add(5,"Otwórz obok w Split View");}
    if s.http{add(0,"");add(23,"Zawsze otwieraj tę domenę tutaj");add(24,"Usuń regułę dla tej domeny");}
    entries
}

fn workspace_switch_motion(previous:i32,current:i32,visible:bool,enabled:bool,reduced:bool)->ffi::WorkspaceMotion {
    let enabled=previous>=0 && current>=0 && previous!=current && visible && enabled && !reduced;
    ffi::WorkspaceMotion{enabled,offset:if !enabled{0.0}else if current>previous{32.0}else{-32.0},duration_ms:if enabled{180}else{0}}
}

// Preview data carries identities and font metrics, never native View bounds.
// Reuse live row indentation/split geometry and clip before preparing GPU text.
fn workspace_preview_layout(width:i32,height:i32,offset:i32,rows:&[ffi::PreviewRow])->Vec<ffi::PreviewPlacement> {
    let width=width.max(0);let height=height.max(0);let mut y=-offset.max(0);let mut out=Vec::new();
    let mut emit=|index,b:ffi::Rect| {
        let x=b.x.max(0);let top=b.y.max(0);let right=b.x.saturating_add(b.width).min(width);let bottom=b.y.saturating_add(b.height).min(height);
        if right>x && bottom>top{out.push(ffi::PreviewPlacement{index,bounds:b,clip:ffi::Rect{x,y:top,width:right-x,height:bottom-top}});}
    };
    for row in rows {
        if row.kind==3{y=y.saturating_add(row.height.max(0));continue;}
        let x=folder_indent(row.depth).min(width);let w=width-x;
        let h=match row.kind{1=>row.height.max(0),2=>46,_=>34};
        emit(row.index,ffi::Rect{x,y,width:w,height:h});
        if row.kind==2 {
            let g=split_layout(w,h,2);
            for (i,r) in g.children.into_iter().enumerate(){emit(row.index.saturating_add(i+1),ffi::Rect{x:x.saturating_add(r.x),y:y.saturating_add(r.y),width:r.width,height:r.height});}
        }
        y=y.saturating_add(h).saturating_add(2);
    }
    out
}


fn palette_text_layout(kind:u32,width:i32,height:i32,text_width:i32,font_height:i32)->ffi::ColumnLayout {
    let (top,left,bottom,right):(i32,i32,i32,i32)=match kind{0=>(1,1,1,1),1=>(11,18,10,18),_=>(8,18,8,18)};
    let tw=text_width.max(0);let fh=font_height.max(0);
    let width=if width<0{tw.saturating_add(left+right)}else{width};
    let height=if height<0{fh.saturating_add(top+bottom)}else{height};
    let inner_width=width.saturating_sub(left+right).max(0);let inner_height=height.saturating_sub(top+bottom).max(0);
    ffi::ColumnLayout{width,height,children:vec![ffi::Rect{x:left.min(width),y:top.min(height)+(inner_height-fh.min(inner_height))/2,width:tw.min(inner_width),height:fh.min(inner_height)}]}
}

fn palette_width(window_width:i32)->i32{window_width.saturating_sub(48).clamp(120,640)}
fn palette_results_height(content_height:i32)->i32{content_height.saturating_sub(230).clamp(90,300)}
fn palette_frame(content:ffi::Rect,width:i32,height:i32)->ffi::Rect {
    ffi::Rect{x:content.x+content.width/2-width/2,y:content.y+132.min(content.height/6),width,height}
}
fn palette_input_layout(width:i32,height:i32,icon:ffi::Rect,field_height:i32,hint:ffi::Rect)->ffi::PaletteInputLayout {
    let width=width.max(0);let height=height.max(0);let inner=(width-36).max(0);
    let icon_width=icon.width.clamp(0,inner);let hint_width=hint.width.clamp(0,(inner-icon_width-24).max(0));
    let rect=|x:i32,w:i32,h:i32|ffi::Rect{x,y:(height-h).max(0)/2,width:w,height:h};
    ffi::PaletteInputLayout{icon:rect(18.min(width),icon_width,icon.height.clamp(0,height)),
        input:rect(18+icon_width+12,(inner-icon_width-hint_width-24).max(0),field_height.clamp(0,height)),
        hint:rect((width-18-hint_width).max(0),hint_width,hint.height.clamp(0,height))}
}

fn sidebar_resize_width(initial:i32,amount:i32)->i32 {initial.saturating_add(amount).clamp(200,480)}
fn sidebar_grip_layout(width:i32,height:i32)->ffi::Rect{let width=width.max(0);ffi::Rect{x:(width-6).max(0),y:0,width:width.min(6),height:height.max(0)}}
fn sidebar_auto_hide(pinned:bool,blocked:bool,inside:bool,dragging:bool)->bool {!pinned&&!blocked&&!inside&&!dragging}
fn shell_layout(width:i32,height:i32,preferred:i32,pinned:bool)->ffi::ShellLayout {
    let width=width.max(0);let height=height.max(0);
    let sidebar_width=preferred.clamp(200,480).min(120.max(width/2));
    let column=if pinned{sidebar_width+8}else{0};
    ffi::ShellLayout {
        content:ffi::Rect{x:column+8,y:38,width:(width-column-16).max(0),height:(height-46).max(0)},
        edge:ffi::Rect{x:0,y:38,width:8,height:(height-38).max(0)},
        sidebar:ffi::Rect{x:8,y:38,width:sidebar_width,height:200.max(height-46)},
    }
}
#[derive(Default)]
struct SidebarDrag {anchor:Option<f64>,pointer:f64,initial:i32,width:i32,moved:bool}
fn new_sidebar_drag()->Box<SidebarDrag>{Box::new(SidebarDrag::default())}
impl SidebarDrag {
    fn begin(&mut self,width:i32,pointer:f64){self.initial=width.clamp(200,480);self.width=self.initial;self.pointer=pointer;self.anchor=pointer.is_finite().then_some(pointer);self.moved=false;}
    fn update(&mut self,pointer:f64,reverse:bool)->ffi::WidthChange {
        let Some(anchor)=self.anchor else{return ffi::WidthChange{width:self.width,changed:false};};
        if !pointer.is_finite(){return ffi::WidthChange{width:self.width,changed:false};}
        self.pointer=pointer;self.moved|=pointer!=anchor;
        let amount=((pointer-anchor)*if reverse{-1.0}else{1.0}) as i32;
        self.width=sidebar_resize_width(self.initial,amount);
        ffi::WidthChange{width:self.width,changed:self.moved}
    }
    fn delta(&mut self,delta:f64,reverse:bool)->ffi::WidthChange{self.update(self.pointer+delta,reverse)}
    fn end(&mut self)->ffi::WidthChange{let changed=self.anchor.is_some()&&self.moved;self.cancel();ffi::WidthChange{width:self.width,changed}}
    fn cancel(&mut self){self.anchor=None;self.moved=false;}
    fn active(&self)->bool{self.anchor.is_some()}
}

fn pane_bar_layout(width: i32, height: i32) -> ffi::PaneBarLayout {
    let width=width.max(0);let height=height.max(0);
    let close_width=32.min((width-4).max(0));let close_x=(width-4-close_width).max(0);
    let menu_width=32.min(close_x);let menu_x=close_x-menu_width;
    ffi::PaneBarLayout {
        title:ffi::Rect{x:12.min(width),y:0,width:(menu_x-12).max(0),height},
        menu:ffi::Rect{x:menu_x,y:(height-32).max(0)/2,width:menu_width,height:32.min(height)},
        close:ffi::Rect{x:close_x,y:(height-32).max(0)/2,width:close_width,height:32.min(height)},
    }
}
fn pane_host_layout(width: i32, height: i32, ratio: f64, vertical: bool, split: bool) -> ffi::PaneHostLayout {
    let width=width.max(0);let height=height.max(0);
    let empty=||ffi::Rect{x:0,y:0,width:0,height:0};
    if !split {return ffi::PaneHostLayout{first:ffi::Rect{x:0,y:0,width,height},second:empty(),first_bar:empty(),second_bar:empty(),divider:empty()};}
    let ratio=if ratio.is_finite(){ratio.clamp(0.0,1.0)}else{0.5};
    let extent=if vertical{height}else{width};let gap=8.min(extent);let available=extent-gap;
    let first=(f64::from(available)*ratio) as i32;let second=available-first;
    let (a,b,d)=if vertical {
        (ffi::Rect{x:0,y:0,width,height:first},ffi::Rect{x:0,y:first+gap,width,height:second},ffi::Rect{x:0,y:first,width,height:gap})
    } else {
        (ffi::Rect{x:0,y:0,width:first,height},ffi::Rect{x:first+gap,y:0,width:second,height},ffi::Rect{x:first,y:0,width:gap,height})
    };
    let bar=|r:&ffi::Rect|ffi::Rect{x:r.x,y:r.y,width:r.width,height:30.min(r.height)};
    let page=|r:&ffi::Rect|ffi::Rect{x:r.x,y:r.y+30.min(r.height),width:r.width,height:(r.height-30).max(0)};
    ffi::PaneHostLayout{first:page(&a),second:page(&b),first_bar:bar(&a),second_bar:bar(&b),divider:d}
}
#[derive(Default)]
struct SplitDrag { anchor: Option<f64>, initial: f64, pointer: f64, ratio: f64, moved: bool }
fn new_split_drag()->Box<SplitDrag>{Box::new(SplitDrag::default())}
impl SplitDrag {
    fn begin(&mut self, ratio:f64, pointer:f64) {
        self.initial=if ratio.is_finite(){ratio.clamp(0.2,0.8)}else{0.5};
        self.ratio=self.initial;self.pointer=pointer;self.anchor=pointer.is_finite().then_some(pointer);self.moved=false;
    }
    fn update(&mut self,pointer:f64,extent:i32,reverse:bool)->ffi::DragRatio {
        let Some(anchor)=self.anchor else{return ffi::DragRatio{ratio:self.ratio,changed:false};};
        if !pointer.is_finite(){return ffi::DragRatio{ratio:self.ratio,changed:false};}
        self.pointer=pointer;self.moved=true;
        self.ratio=(self.initial+(pointer-anchor)*if reverse{-1.0}else{1.0}/f64::from(extent.saturating_sub(8).max(1))).clamp(0.2,0.8);
        ffi::DragRatio{ratio:self.ratio,changed:true}
    }
    fn delta(&mut self,delta:f64,extent:i32,reverse:bool)->ffi::DragRatio {
        self.update(self.pointer+delta,extent,reverse)
    }
    fn end(&mut self)->ffi::DragRatio {
        let changed=self.anchor.is_some()&&self.moved;self.cancel();ffi::DragRatio{ratio:self.ratio,changed}
    }
    fn cancel(&mut self){self.anchor=None;self.moved=false;}
    fn active(&self)->bool{self.anchor.is_some()}
    fn moved(&self)->bool{self.moved}
}

// Owns the vertical viewport, extent, offset and reveal policy. Native code
// supplies preferred metrics and platform event/scrollbar requests, never state.
// Native adapters supply text measurements; Rust owns clipping and reflow policy.
fn viewport_preferred_size(width:i32,height:i32,minimum:i32,maximum:i32)->ffi::Rect {
    let height=height.max(0);let height=if minimum>=0&&maximum>=minimum{height.clamp(minimum,maximum)}else{height};
    ffi::Rect{x:0,y:0,width:width.max(0),height}
}
fn viewport_measure_width(width:i32,height:i32,full_height:i32,thickness:i32,overlay:bool)->i32 {
    let width=width.max(0);width-if full_height>height.max(0)&&!overlay{thickness.clamp(0,width)}else{0}
}
#[derive(Default)]
pub struct ScrollViewportState { height:i32, content:i32, maximum:i32, offset:f64 }
fn new_scroll_viewport()->Box<ScrollViewportState>{Box::new(ScrollViewportState::default())}
impl ScrollViewportState {
    fn viewport_configure(&mut self,width:i32,height:i32,preferred_width:i32,preferred_height:i32,thickness:i32,overlay:bool)->ffi::ScrollViewportLayout {
        let width=width.max(0);let height=height.max(0);let ready=width>0&&height>0;
        let show_bar=ready&&preferred_height>height;let thickness=thickness.clamp(0,width);
        let viewport_width=width-if show_bar&&!overlay{thickness}else{0};
        self.height=height;self.content=preferred_height.max(height);self.maximum=if ready{self.content.saturating_sub(height).max(0)}else{0};self.offset=self.offset.clamp(0.0,f64::from(self.maximum));
        ffi::ScrollViewportLayout{viewport:ffi::Rect{x:0,y:0,width:viewport_width,height},content:ffi::Rect{x:0,y:0,width:preferred_width.max(viewport_width),height:self.content},bar:ffi::Rect{x:width-thickness,y:0,width:thickness,height},show_bar,offset:self.offset,max_offset:self.maximum}
    }
    fn viewport_configure_horizontal(&mut self,width:i32,height:i32,preferred_width:i32,preferred_height:i32,thickness:i32,overlay:bool)->ffi::ScrollViewportLayout {
        let mut plan=self.viewport_configure(height,width,preferred_height,preferred_width,thickness,overlay);
        fn transpose(r:ffi::Rect)->ffi::Rect{ffi::Rect{x:r.y,y:r.x,width:r.height,height:r.width}}
        plan.viewport=transpose(plan.viewport);plan.content=transpose(plan.content);plan.bar=transpose(plan.bar);plan
    }
    fn viewport_scroll_to(&mut self,offset:f64)->bool {
        if !offset.is_finite(){return false;}let next=offset.clamp(0.0,f64::from(self.maximum));let changed=next!=self.offset;self.offset=next;changed
    }
    fn viewport_scroll_delta(&mut self,delta:f64)->bool {
        if !delta.is_finite(){return false;}self.viewport_scroll_to(self.offset-delta)
    }
    fn viewport_reveal(&mut self,y:i32,height:i32)->bool {
        if self.maximum==0||self.height==0{return false;}let start=y.clamp(0,self.content);let end=(i64::from(start)+i64::from(height.max(0).min(self.height))).min(i64::from(self.content)) as i32;
        let visible=self.offset as i32;if start>=visible&&end<=visible.saturating_add(self.height){return false;}
        self.viewport_scroll_to(f64::from(if visible>start{start}else{end.saturating_sub(self.height).max(0)}))
    }
    fn viewport_edges(&self,width:i32)->ffi::ScrollViewportEdges {
        let width=width.max(0);let thickness=self.height.clamp(0,1);let ready=width>0&&self.height>0&&self.maximum>0;
        ffi::ScrollViewportEdges{top:ffi::Rect{x:0,y:0,width,height:thickness},bottom:ffi::Rect{x:0,y:self.height.saturating_sub(thickness),width,height:thickness},show_top:ready&&self.offset>0.0,show_bottom:ready&&self.offset<f64::from(self.maximum)}
    }
    fn viewport_axis_edges(&self,cross:i32,horizontal:bool)->ffi::ScrollViewportEdges {
        let mut edges=self.viewport_edges(cross);if horizontal{for r in [&mut edges.top,&mut edges.bottom]{std::mem::swap(&mut r.x,&mut r.y);std::mem::swap(&mut r.width,&mut r.height);}}edges
    }
    fn viewport_offset(&self)->f64{self.offset}
    fn viewport_maximum(&self)->i32{self.maximum}
}

// Scroll geometry and thumb drag state use content offsets, never View IDs.
// The native adapter delivers track-local coordinates and applies the result to
// Chromium's scroll service; capture, keyboard/menu and AX delivery remain native.
fn scroll_metrics(track: i32, viewport: i32, content: i32, offset: i32) -> ffi::ScrollMetrics {
    let track = track.max(0); let viewport = viewport.max(0); let content = content.max(0);
    let max_offset = content.saturating_sub(viewport).max(0);
    let offset = offset.clamp(0, max_offset);
    let length = if track == 0 || viewport == 0 || content == 0 {0} else {
        (f64::from(viewport) / f64::from(content) * f64::from(track)).round()
            .clamp(f64::from(20.min(track)), f64::from(track)) as i32
    };
    let position = if max_offset == 0 {0} else {
        (f64::from(offset) / f64::from(max_offset) * f64::from(track - length)).round() as i32
    };
    ffi::ScrollMetrics {position, length, offset, max_offset}
}
fn scroll_offset(track: i32, viewport: i32, content: i32, position: f32) -> i32 {
    let g = scroll_metrics(track, viewport, content, 0);
    let travel = track.max(0) - g.length;
    if !position.is_finite() || travel <= 0 {return 0;}
    (f64::from(position).clamp(0.0, f64::from(travel)) / f64::from(travel)
        * f64::from(g.max_offset)).round() as i32
}
#[derive(Default)]
struct ScrollThumb { grip: Option<f32>, initial_offset: i32 }
fn new_scroll_thumb() -> Box<ScrollThumb> {Box::new(ScrollThumb::default())}
impl ScrollThumb {
    fn begin(&mut self, track: i32, viewport: i32, content: i32, offset: i32, pointer: f32) {
        let g = scroll_metrics(track, viewport, content, offset);
        self.grip = pointer.is_finite().then_some(pointer - g.position as f32);
        self.initial_offset = g.offset;
    }
    fn drag(&self, track: i32, viewport: i32, content: i32, offset: i32,
            pointer: f32, cross: f32, thickness: i32, snap: bool) -> i32 {
        let current = scroll_metrics(track, viewport, content, offset).offset;
        let Some(grip) = self.grip else {return current;};
        if !pointer.is_finite() || !cross.is_finite() {return current;}
        if snap && (cross < -128.0 || cross > thickness.max(0) as f32 + 128.0) {
            return scroll_metrics(track, viewport, content, self.initial_offset).offset;
        }
        scroll_offset(track, viewport, content, pointer - grip)
    }
    fn end(&mut self) {self.grip = None;}
}

// The platform delivers scroll offsets. Rust owns normalization, parent-local
// coordinate projection and inherited clipping; no native View pointers enter.
#[derive(Clone, Copy)]
struct ViewportWorld { x: f64, y: f64, left: f64, top: f64, right: f64, bottom: f64, visible: bool }
#[derive(Default)]
struct Viewport { width: f32, height: f32, x: f32, y: f32, world: Vec<ViewportWorld> }
fn new_viewport() -> Box<Viewport> { Box::new(Viewport::default()) }
impl Viewport {
    fn project(&mut self, content_width: f32, content_height: f32, width: f32, height: f32,
               x: f32, y: f32, nodes: &[ffi::ViewportNode]) -> ffi::ViewportProjection {
        let positive = |v: f32| if v.is_finite() {v.max(0.0)} else {0.0};
        let offset = |v: f32, max: f32| if v.is_finite() {v.clamp(0.0, max)} else {0.0};
        self.width = positive(width); self.height = positive(height);
        self.x = offset(x, (positive(content_width) - self.width).max(0.0));
        self.y = offset(y, (positive(content_height) - self.height).max(0.0));
        // Keep intermediate ancestry sums in f64 so large or deep native
        // rectangles cannot overflow before viewport rejection.
        let mut world = std::mem::take(&mut self.world);
        world.clear(); world.reserve(nodes.len());
        let mut out = Vec::new();
        for (index, node) in nodes.iter().enumerate() {
            let b = &node.bounds;
            let valid = node.visible && [b.x, b.y, b.width, b.height].iter().all(|v| v.is_finite())
                && b.width > 0.0 && b.height > 0.0;
            let parent = if node.parent == usize::MAX {
                ViewportWorld {x: 0.0, y: 0.0, left: 0.0, top: 0.0,
                    right: f64::from(self.width), bottom: f64::from(self.height), visible: true}
            } else if node.parent < index {world[node.parent]} else {
                ViewportWorld {x: 0.0, y: 0.0, left: 0.0, top: 0.0, right: 0.0, bottom: 0.0, visible: false}
            };
            let x = parent.x + f64::from(b.x); let y = parent.y + f64::from(b.y);
            let px = x - f64::from(self.x); let py = y - f64::from(self.y);
            let left = px.max(parent.left); let top = py.max(parent.top);
            let right = (px + f64::from(b.width)).min(parent.right);
            let bottom = (py + f64::from(b.height)).min(parent.bottom);
            let visible = valid && parent.visible && right > left && bottom > top &&
                [px as f32, py as f32].iter().all(|v| v.is_finite());
            world.push(ViewportWorld {x, y, left, top, right, bottom, visible});
            if visible {
                out.push(ffi::ProjectedNode {index,
                    bounds: ffi::FloatRect {x: px as f32, y: py as f32, width: b.width, height: b.height},
                    clip: ffi::FloatRect {x: left as f32, y: top as f32,
                        width: (right - left) as f32, height: (bottom - top) as f32}});
            }
        }
        self.world = world;
        ffi::ViewportProjection {x: self.x, y: self.y, nodes: out}
    }
}

// UI identity and expansion state only. Browser URLs, titles, profiles and
// commands stay in the application. State survives reconstruction and rename
// because it is keyed by workspace and item IDs, never by native View pointers.
use std::collections::{BTreeMap, BTreeSet};
use std::cell::RefCell;
use std::rc::Rc;
#[derive(Default)]
struct SidebarTree { collapsed: BTreeMap<String, BTreeSet<String>> }
fn new_sidebar_tree() -> Box<SidebarTree> { Box::new(SidebarTree::default()) }
impl SidebarTree {
    fn collapsed(&self, workspace: &str, id: &str) -> bool {
        self.collapsed.get(workspace).is_some_and(|ids| ids.contains(id))
    }
    fn set_collapsed(&mut self, workspace: &str, id: &str, value: bool) {
        if value {self.collapsed.entry(workspace.into()).or_default().insert(id.into());}
        else if let Some(ids) = self.collapsed.get_mut(workspace) {
            ids.remove(id);
            if ids.is_empty() {self.collapsed.remove(workspace);}
        }
    }
    fn toggle(&mut self, workspace: &str, id: &str) -> bool {
        let collapsed = !self.collapsed(workspace, id);
        self.set_collapsed(workspace, id, collapsed); collapsed
    }
    fn forget_workspace(&mut self, workspace: &str) {self.collapsed.remove(workspace);}
    fn tab_plan(&self, workspace: &str, nodes: &[ffi::TreeNode], tabs: &[ffi::TabNode],
                first: usize, second: usize, split: bool) -> ffi::TabPlan {
        let tree = self.tree_plan(workspace, nodes);
        let reachable: BTreeMap<&str, usize> = tree.iter().map(|e| (nodes[e.index].id.as_str(), e.index)).collect();
        let mut positions = BTreeMap::new(); let mut order = Vec::new();
        for (index, tab) in tabs.iter().enumerate() {
            if let std::collections::btree_map::Entry::Vacant(entry) = positions.entry(tab.index) {
                entry.insert(index); order.push(index);
            }
        }
        let referenced: BTreeSet<&str> = order.iter().map(|&i| tabs[i].pin.as_str()).filter(|s| !s.is_empty()).collect();
        let pins = tree.iter().map(|e| ffi::PinVisibility {
            index: e.index, visible: !referenced.contains(nodes[e.index].id.as_str())
        }).collect();
        let pair = if split && first != second {
            positions.get(&first).zip(positions.get(&second)).map(|(&a, &b)| (a, b))
        } else {None};
        let mut slots = Vec::with_capacity(order.len()); let mut emitted = false;
        for index in order {
            if let Some((a, b)) = pair {
                if index == a || index == b {
                    if !emitted {
                        let pin = [a, b].iter().find_map(|&i| reachable.get(tabs[i].pin.as_str()).copied()).unwrap_or(usize::MAX);
                        slots.push(ffi::TabSlot {first: a, second: b, pin, pair: true});
                        emitted = true;
                    }
                    continue;
                }
            }
            slots.push(ffi::TabSlot {first: index, second: usize::MAX, pin: usize::MAX, pair: false});
        }
        ffi::TabPlan {slots, pins}
    }
    fn tree_plan(&self, workspace: &str, nodes: &[ffi::TreeNode]) -> Vec<ffi::TreeEntry> {
        let mut children: BTreeMap<&str, Vec<usize>> = BTreeMap::new();
        let mut ids = BTreeSet::new();
        for (index, node) in nodes.iter().enumerate() {
            // First occurrence owns an ID. Orphans, cycles and children of a
            // leaf are unreachable; they never create native interaction nodes.
            if !node.id.is_empty() && ids.insert(node.id.as_str()) {
                children.entry(node.parent.as_str()).or_default().push(index);
            }
        }
        let mut pending = Vec::new();
        if let Some(roots) = children.get("") {
            for &index in roots.iter().rev() {pending.push((index, usize::MAX, 0, true));}
        }
        let mut seen = BTreeSet::new(); let mut out = Vec::with_capacity(nodes.len());
        while let Some((index, parent, depth, visible)) = pending.pop() {
            if depth > 64 || !seen.insert(index) {continue;}
            let node = &nodes[index];
            let collapsed = node.folder && self.collapsed(workspace, &node.id);
            out.push(ffi::TreeEntry {index, parent, depth, collapsed, visible});
            if node.folder {
                if let Some(descendants) = children.get(node.id.as_str()) {
                    for &child in descendants.iter().rev() {
                        pending.push((child, index, depth + 1, visible && !collapsed));
                    }
                }
            }
        }
        out
    }
}

// Semantic keys supplied by the event adapter: 1 Space, 2 Return, 0 other.
// Mac Space acts on key-down; Return remains available to the default action.
fn key_policy(key: u32, mac: bool) -> u32 {
    match key {1 => if mac {1} else {2}, 2 if !mac => 1, _ => 0}
}

// A row's gesture state is independent of its platform event adapter. Once the
// threshold is crossed, returning to the press point still completes a drag.
#[derive(Default)]
struct RowPointer { origin: Option<(i32, i32)>, dragging: bool, hovered: bool, key_armed: bool }
fn new_row_pointer() -> Box<RowPointer> { Box::new(RowPointer::default()) }
impl RowPointer {
    fn press(&mut self, x: i32, y: i32) {
        self.origin = Some((x, y)); self.dragging = false;
    }
    fn drag(&mut self, x: i32, y: i32, enabled: bool) {
        if let Some((sx, sy)) = self.origin {
            let dx = i128::from(x) - i128::from(sx);
            let dy = i128::from(y) - i128::from(sy);
            self.dragging |= enabled && dx * dx + dy * dy > 64;
        }
    }
    fn release(&mut self) -> bool {
        let result = self.origin.is_some() && self.dragging;
        self.cancel(); result
    }
    fn cancel(&mut self) {
        self.origin = None; self.dragging = false; self.hovered = false; self.key_armed = false;
    }
    fn pointer_event(&mut self, phase: ffi::PointerPhase, x: i32, y: i32,
                     inside: bool, enabled: bool, trigger: bool, drop: bool) -> ffi::RowInputResult {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        if !enabled { self.cancel(); return ffi::RowInputResult {visual: V::Disabled, action: A::None, handled: true}; }
        self.hovered = inside;
        let mut action = A::None;
        match phase {
            P::Press => { self.key_armed = false; if trigger && inside {self.press(x, y);} }
            P::Drag => self.drag(x, y, drop),
            P::Release => {
                let armed = self.origin.is_some();
                let dragged = self.release(); self.hovered = inside;
                action = if dragged && drop {A::Drop}
                    else if armed && inside && trigger {A::Activate}
                    else {A::Cancel};
            }
            P::Enter | P::Exit | P::Move => {self.key_armed = false;}
            _ => {self.cancel();}
        }
        let visual = if !inside {V::Normal}
            else if self.origin.is_some() && trigger {V::Pressed} else {V::Hovered};
        ffi::RowInputResult {visual, action, handled: true}
    }
    fn key_event(&mut self, pressed: bool, policy: u32, enabled: bool) -> ffi::RowInputResult {
        use ffi::{RowAction as A, RowVisual as V};
        if !enabled {self.cancel(); return ffi::RowInputResult {visual: V::Disabled, action: A::None, handled: false};}
        let mut action = A::None;
        let mut handled = false;
        if pressed && policy == 1 {self.cancel(); action = A::Activate; handled = true;}
        else if pressed && policy == 2 {self.key_armed = true; handled = true;}
        else if !pressed && policy == 2 && self.key_armed {self.cancel(); action = A::Activate; handled = true;}
        let visual = if self.key_armed {V::Pressed} else if self.hovered {V::Hovered} else {V::Normal};
        ffi::RowInputResult {visual, action, handled}
    }
}
// An action's state outlives individual native events; no Button state is read.
struct ActionState { pointer:RowPointer, enabled:bool, focused:bool, on:bool, is_default:bool, on_press:bool, visual:ffi::RowVisual }
fn initial_action_state()->ActionState{ActionState{pointer:RowPointer::default(),enabled:true,focused:false,on:false,is_default:false,on_press:false,visual:ffi::RowVisual::Normal}}
fn new_action_control()->Box<ActionControl>{Box::new(ActionControl{storage:ActionStorage::Standalone(initial_action_state())})}
fn action_preferred_size(dot:bool,label:bool)->ffi::Rect{ffi::Rect{x:0,y:0,width:if dot{if label{30}else{24}}else{32},height:if dot{28}else{32}}}
impl ActionState {
    fn action_cancel(&mut self)->bool{let visual=if self.enabled{ffi::RowVisual::Normal}else{ffi::RowVisual::Disabled};let changed=self.pointer.origin.is_some()||self.pointer.hovered||self.pointer.key_armed||self.visual!=visual;self.pointer.cancel();self.visual=visual;changed}
    fn action_enabled(&mut self,enabled:bool)->bool{if self.enabled==enabled{return false;}self.enabled=enabled;self.action_cancel();true}
    fn action_focus(&mut self,focused:bool){self.focused=focused;if !focused{self.action_cancel();}}
    fn action_on(&mut self,on:bool)->bool{let changed=self.on!=on;self.on=on;changed}
    fn action_snapshot(&self)->ffi::ActionSnapshot{ffi::ActionSnapshot{enabled:self.enabled,focused:self.focused,on:self.on,is_default:self.is_default,visual:self.visual}}
    fn reject(&mut self)->ffi::RowInputResult{self.action_cancel();ffi::RowInputResult{visual:self.visual,action:ffi::RowAction::None,handled:false}}
    fn action_default(&mut self,value:bool)->bool{let changed=self.is_default!=value;self.is_default=value;changed}
    fn action_on_press(&mut self,value:bool)->bool{if self.on_press==value{return false;}self.action_cancel();self.on_press=value;true}
    fn action_accelerator(&mut self,key:u32,modified:bool,visible:bool)->ffi::RowInputResult{
        if key!=2||modified||!self.is_default||!self.enabled||!visible{return self.reject();}
        self.action_cancel();ffi::RowInputResult{visual:self.visual,action:ffi::RowAction::Activate,handled:true}
    }
    fn action_pending(&self)->bool{self.pointer.origin.is_some()||self.pointer.key_armed}
    fn action_pointer(&mut self,phase:ffi::PointerPhase,x:i32,y:i32,inside:bool,trigger:bool,visible:bool)->ffi::RowInputResult{
        let result=self.action_row_pointer(phase,x,y,inside,trigger,visible,false);
        if self.on_press&&phase==ffi::PointerPhase::Press&&self.enabled&&visible&&inside&&trigger {
            self.action_cancel();return ffi::RowInputResult{visual:self.visual,action:ffi::RowAction::Activate,handled:true};
        }
        result
    }
    fn action_row_pointer(&mut self,phase:ffi::PointerPhase,x:i32,y:i32,inside:bool,trigger:bool,visible:bool,drop:bool)->ffi::RowInputResult{
        if !visible{return self.reject();}let r=self.pointer.pointer_event(phase,x,y,inside,self.enabled,trigger,drop);self.visual=r.visual;r
    }
    fn action_key(&mut self,pressed:bool,key:u32,mac:bool,visible:bool)->ffi::RowInputResult{
        if !visible{return self.reject();}let r=self.pointer.key_event(pressed,key_policy(key,mac),self.enabled);self.visual=r.visual;r
    }
    // 0 tap-down, 1 tap, 2 cancel/end; activation is independent of tap-down.
    fn action_gesture(&mut self,kind:u32,visible:bool)->ffi::RowInputResult{
        use ffi::{RowVisual as V,RowAction as A};if !visible||!self.enabled{return self.reject();}
        let (visual,action,handled)=match kind{0=>{self.pointer.press(0,0);(V::Pressed,A::None,true)},1=>{self.pointer.cancel();(V::Hovered,A::Activate,true)},2=>{self.pointer.cancel();(V::Normal,A::None,false)},_=>(self.visual,A::None,false)};self.visual=visual;ffi::RowInputResult{visual,action,handled}
    }
}

// Logical action nodes belong to a Rust scope, not the lifetime of a native View.
// Domain keys are opaque projections; they never authorize a browser operation.
type ActionNodeIdentity=(u32,u64,u64,String);
fn action_node_key(key:&ffi::ActionNodeKey)->Option<ActionNodeIdentity>{
    match key.kind {
        kind @ (1|10) if (key.high!=0||key.low!=0)&&key.name.is_empty()=>Some((kind,key.high,key.low,String::new())),
        kind @ (2|3|4|5) if key.high==0&&key.low==0&&!key.name.is_empty()=>Some((kind,0,0,key.name.clone())),
        _=>None,
    }
}
struct ActionNodeRecord { id:u64, lease:u64, attached:bool, alive:bool, state:ActionState }
pub struct ActionGraph { nodes:BTreeMap<ActionNodeIdentity,Rc<RefCell<ActionNodeRecord>>>, order:Vec<u64>, next_id:u64 }
fn new_action_graph()->Box<ActionGraph>{Box::new(ActionGraph{nodes:BTreeMap::new(),order:vec![],next_id:1})}
impl ActionGraph {
    fn rejected(&self)->ffi::ActionNodePlan{ffi::ActionNodePlan{admitted:false,ids:self.order.clone(),created:0,retired:0}}
    fn reconcile(&mut self,keys:Vec<ffi::ActionNodeKey>)->ffi::ActionNodePlan{
        let Some(identities)=keys.iter().map(action_node_key).collect::<Option<Vec<_>>>() else{return self.rejected();};
        let unique=identities.iter().cloned().collect::<BTreeSet<_>>();
        if unique.len()!=identities.len(){return self.rejected();}
        if unique.iter().any(|key|key.0==10&&!unique.contains(&(1,key.1,key.2,String::new()))){return self.rejected();}
        let created=identities.iter().filter(|key|!self.nodes.contains_key(*key)).count();
        if self.next_id.checked_add(created as u64).is_none(){return self.rejected();}
        let mut retired=0;
        self.nodes.retain(|key,node|{if unique.contains(key){return true;}let mut node=node.borrow_mut();node.alive=false;node.attached=false;node.state.action_focus(false);node.state.action_cancel();retired+=1;false});
        let mut order=Vec::with_capacity(identities.len());
        for key in identities {
            let node=self.nodes.entry(key).or_insert_with(||{let id=self.next_id;self.next_id+=1;Rc::new(RefCell::new(ActionNodeRecord{id,lease:0,attached:false,alive:true,state:initial_action_state()}))});
            order.push(node.borrow().id);
        }
        self.order=order.clone();ffi::ActionNodePlan{admitted:true,ids:order,created,retired}
    }
    fn acquire(&mut self,key:&ffi::ActionNodeKey)->Box<ActionControl>{
        let node=action_node_key(key).and_then(|key|self.nodes.get(&key)).cloned();
        let Some(node)=node else{return Box::new(ActionControl{storage:ActionStorage::Retired});};
        let lease={let mut record=node.borrow_mut();let Some(lease)=record.lease.checked_add(1) else{return Box::new(ActionControl{storage:ActionStorage::Retired});};record.lease=lease;record.attached=true;record.state.action_enabled(true);record.state.action_focus(false);record.state.action_cancel();lease};
        Box::new(ActionControl{storage:ActionStorage::Shared{node,lease}})
    }
}
impl Drop for ActionGraph {fn drop(&mut self){for node in self.nodes.values(){let mut record=node.borrow_mut();record.alive=false;record.attached=false;record.state.action_focus(false);record.state.action_cancel();}}}
enum ActionStorage { Standalone(ActionState), Shared{node:Rc<RefCell<ActionNodeRecord>>,lease:u64}, Retired }
struct ActionControl { storage:ActionStorage }
impl ActionControl {
    fn read<R>(&self,f:impl FnOnce(&ActionState)->R)->Option<R>{match &self.storage{ActionStorage::Standalone(state)=>Some(f(state)),ActionStorage::Shared{node,lease}=>{let record=node.borrow();(record.alive&&record.attached&&record.lease==*lease).then(||f(&record.state))},ActionStorage::Retired=>None}}
    fn write<R>(&mut self,f:impl FnOnce(&mut ActionState)->R)->Option<R>{match &mut self.storage{ActionStorage::Standalone(state)=>Some(f(state)),ActionStorage::Shared{node,lease}=>{let mut record=node.borrow_mut();if !record.alive||!record.attached||record.lease!=*lease{return None;}Some(f(&mut record.state))},ActionStorage::Retired=>None}}
    fn retired_input()->ffi::RowInputResult{ffi::RowInputResult{visual:ffi::RowVisual::Disabled,action:ffi::RowAction::None,handled:false}}
    fn action_node_id(&self)->u64{match &self.storage{ActionStorage::Shared{node,lease}=>{let record=node.borrow();if record.alive&&record.attached&&record.lease==*lease{record.id}else{0}},_=>0}}
    fn action_enabled(&mut self,value:bool)->bool{self.write(|s|s.action_enabled(value)).unwrap_or(false)}
    fn action_focus(&mut self,value:bool){self.write(|s|s.action_focus(value));}
    fn action_on(&mut self,value:bool)->bool{self.write(|s|s.action_on(value)).unwrap_or(false)}
    fn action_cancel(&mut self)->bool{self.write(ActionState::action_cancel).unwrap_or(false)}
    fn action_snapshot(&self)->ffi::ActionSnapshot{self.read(ActionState::action_snapshot).unwrap_or(ffi::ActionSnapshot{enabled:false,focused:false,on:false,is_default:false,visual:ffi::RowVisual::Disabled})}
    fn action_default(&mut self,value:bool)->bool{self.write(|s|s.action_default(value)).unwrap_or(false)}
    fn action_on_press(&mut self,value:bool)->bool{self.write(|s|s.action_on_press(value)).unwrap_or(false)}
    fn action_pending(&self)->bool{self.read(ActionState::action_pending).unwrap_or(false)}
    fn action_accelerator(&mut self,key:u32,modified:bool,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_accelerator(key,modified,visible)).unwrap_or_else(Self::retired_input)}
    fn action_pointer(&mut self,phase:ffi::PointerPhase,x:i32,y:i32,inside:bool,trigger:bool,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_pointer(phase,x,y,inside,trigger,visible)).unwrap_or_else(Self::retired_input)}
    fn action_row_pointer(&mut self,phase:ffi::PointerPhase,x:i32,y:i32,inside:bool,trigger:bool,visible:bool,drop:bool)->ffi::RowInputResult{self.write(|s|s.action_row_pointer(phase,x,y,inside,trigger,visible,drop)).unwrap_or_else(Self::retired_input)}
    fn action_key(&mut self,pressed:bool,key:u32,mac:bool,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_key(pressed,key,mac,visible)).unwrap_or_else(Self::retired_input)}
    fn action_gesture(&mut self,kind:u32,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_gesture(kind,visible)).unwrap_or_else(Self::retired_input)}
}
impl Drop for ActionControl {fn drop(&mut self){if let ActionStorage::Shared{node,lease}=&self.storage{let mut record=node.borrow_mut();if record.alive&&record.lease==*lease{record.attached=false;record.state.action_focus(false);record.state.action_cancel();}}}}

// Shared by the native input/accessibility adapter and the GPU display list.
// Widths are logical pixels; CoreText still owns font shaping and measurement.
// Geometry for an ordered visible list. Hidden/collapsed children are omitted
// by the adapter; no gap is created for them. -1 width requests natural size.
// A start-aligned, stretched panel with zero or one flexible child. Native
// font/control services measure; Rust allocates, clips and reports geometry.
// Optional remeasurement preserves the initial flex budget, matching Views'
// one-pass shrink policy instead of redistributing space after a child shrinks.
fn panel_layout(s:&ffi::PanelStyle, initial:&[ffi::ColumnChild], measured:&[ffi::ColumnChild])->ffi::ColumnLayout {
    panel_layout_aligned(s,initial,measured,false)
}
fn panel_center_layout(s:&ffi::PanelStyle, initial:&[ffi::ColumnChild], measured:&[ffi::ColumnChild])->ffi::ColumnLayout {
    panel_layout_aligned(s,initial,measured,true)
}
fn panel_layout_aligned(s:&ffi::PanelStyle, initial:&[ffi::ColumnChild], measured:&[ffi::ColumnChild],center:bool)->ffi::ColumnLayout {
    let pos=|v:i32|i64::from(v.max(0));let bound=|v:i64|v.clamp(0,i64::from(i32::MAX)) as i32;
    let (main_limit,cross_limit,lead,trail,cross_lead,cross_trail)=if s.vertical {(s.height,s.width,pos(s.top),pos(s.bottom),pos(s.left),pos(s.right))}else{(s.width,s.height,pos(s.left),pos(s.right),pos(s.top),pos(s.bottom))};
    let main=if main_limit<0{None}else{Some((i64::from(main_limit)-lead-trail).max(0))};
    let cross=if cross_limit<0{None}else{Some((i64::from(cross_limit)-cross_lead-cross_trail).max(0))};
    let parts=|c:&ffi::ColumnChild|if s.vertical{(pos(c.height),pos(c.width),pos(c.top),pos(c.bottom),pos(c.left),pos(c.right))}else{(pos(c.width),pos(c.height),pos(c.left),pos(c.right),pos(c.top),pos(c.bottom))};
    let mut preferred_main=0i64;let mut preferred_cross=0i64;let mut preferred_leading=0i64;let mut preferred_trailing=0i64;
    for (i,c) in initial.iter().enumerate(){let (m,x,ml,mt,xl,xt)=parts(c);if m+ml+mt==0&&i!=s.flex{continue;}preferred_main=preferred_main.saturating_add(m+ml+mt);if i+1<initial.len(){preferred_main=preferred_main.saturating_add(pos(s.spacing));}preferred_cross=preferred_cross.max(x+xl+xt);preferred_leading=preferred_leading.max(x/2+xl);preferred_trailing=preferred_trailing.max(x-x/2+xt);}
    if center{preferred_cross=preferred_leading+preferred_trailing;}
    if main==Some(0)&&cross==Some(0){return ffi::ColumnLayout{width:if s.vertical{bound(preferred_cross+cross_lead+cross_trail)}else{bound(preferred_main+lead+trail)},height:if s.vertical{bound(preferred_main+lead+trail)}else{bound(preferred_cross+cross_lead+cross_trail)},children:initial.iter().map(|_|ffi::Rect{x:s.left.max(0),y:s.top.max(0),width:0,height:0}).collect()};}
    let free=main.map_or(0,|m|m-preferred_main);let updated=if measured.len()==initial.len(){measured}else{initial};
    let mut offset=0i64;let mut natural_cross=0i64;let mut center_leading=0i64;let mut center_trailing=0i64;let mut allocated=Vec::with_capacity(initial.len());
    for (i,(original,c)) in initial.iter().zip(updated).enumerate(){
        let (original_main,_,_,_,_,_)=parts(original);let (new_main,x,ml,mt,xl,xt)=parts(c);offset=offset.saturating_add(ml);
        let padding=if i==s.flex{free}else{0};let remaining=main.map_or(i64::MAX,|m|(m-offset).max(0));
        let shrinking=padding<0||remaining<original_main;
        let size=if i==s.flex&&main.is_some(){if shrinking{(original_main+padding).max(0).min(remaining)}else{(original_main+padding).max(0)}}else if shrinking{new_main}else{original_main};
        allocated.push((offset,size,x,xl,xt));natural_cross=natural_cross.max(x+xl+xt);center_leading=center_leading.max((x+1)/2+xl);center_trailing=center_trailing.max(x/2+xt);
        if size>0||i==s.flex{offset=offset.saturating_add(size);if i+1<initial.len(){offset=offset.saturating_add(pos(s.spacing));}offset=offset.saturating_add(mt);}
    }
    if center{natural_cross=center_leading+center_trailing;}
    let cross_size=cross.filter(|x|*x>0).unwrap_or(natural_cross);
    // Match the platform contract's common center and odd-size pixel bias.
    // Keep signed cross extents until positioning; only emitted sizes clamp.
    let rects=allocated.into_iter().map(|(at,size,preferred,xl,xt)|{let size=main.map_or(size,|m|size.min((m-at).max(0)));let raw=(cross_size-xl-xt).min(preferred);let x=if center{cross_lead+(center_leading-raw/2-(raw&1)+(cross_size-natural_cross).div_euclid(2)).max(0)}else{cross_lead+xl};let y=lead+at;let w=if center{raw}else{(cross_size-xl-xt).max(0)};if s.vertical{ffi::Rect{x:bound(x),y:bound(y),width:bound(w),height:bound(size)}}else{ffi::Rect{x:bound(y),y:bound(x),width:bound(size),height:bound(w)}}}).collect();
    let main_size=bound(offset.saturating_add(lead).saturating_add(trail));let cross_size=bound(natural_cross.saturating_add(cross_lead).saturating_add(cross_trail));
    ffi::ColumnLayout{width:if s.vertical{cross_size}else{main_size},height:if s.vertical{main_size}else{cross_size},children:rects}
}

fn column_layout(width: i32, spacing: i32, top: i32, left: i32,
                 bottom: i32, right: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    let positive = |v: i32| i64::from(v.max(0));
    let bounded = |v: i64| v.clamp(0, i64::from(i32::MAX)) as i32;
    let left = positive(left); let right = positive(right);
    let natural = children.iter().map(|c| positive(c.width) + positive(c.left) + positive(c.right))
        .max().unwrap_or(0).saturating_add(left).saturating_add(right);
    let width = if width < 0 {bounded(natural)} else {width};
    let mut y = positive(top);
    let mut rects = Vec::with_capacity(children.len());
    for (index, child) in children.iter().enumerate() {
        if index > 0 {y = y.saturating_add(positive(spacing));}
        y = y.saturating_add(positive(child.top));
        rects.push(ffi::Rect { x: bounded(left + positive(child.left)), y: bounded(y),
            width: bounded(i64::from(width) - left - right - positive(child.left) - positive(child.right)),
            height: bounded(positive(child.height).min(i64::from(i32::MAX) - i64::from(bounded(y)))) });
        y = y.saturating_add(positive(child.height)).saturating_add(positive(child.bottom));
    }
    ffi::ColumnLayout { width, height: bounded(y.saturating_add(positive(bottom))), children: rects }
}

// Equal flex tiles with a four-pixel frame and gap. Distribute the remainder
// deterministically so odd widths fill the frame without a cumulative gap.
// Negative extents request the compact pair's natural size.
fn split_layout(width: i32, height: i32, count: usize) -> ffi::ColumnLayout {
    let width = if width < 0 {200} else {width};
    let height = if height < 0 {46} else {height};
    let left = 4.min(width); let top = 4.min(height);
    let inner = (width - 8).max(0);
    let inner_height = (height - 8).max(0);
    let gaps = (count.saturating_sub(1) as u128 * 4).min(inner as u128) as i32;
    let available = inner - gaps;
    let mut children = Vec::with_capacity(count);
    let mut x = left;
    for index in 0..count {
        let before = (available as u128 * index as u128 / count as u128) as i32;
        let after = (available as u128 * (index + 1) as u128 / count as u128) as i32;
        let tile_width = after - before;
        children.push(ffi::Rect {x, y: top, width: tile_width, height: inner_height});
        x = x.saturating_add(tile_width).saturating_add(4).min(width);
    }
    ffi::ColumnLayout {width, height, children}
}

fn folder_indent(depth: i32) -> i32 { depth.clamp(0, 8) * 12 }

fn close_visible(active: bool, tile: bool, hovered: bool) -> bool {
    (active && !tile) || hovered
}

// Chrome action and list share one opaque Metal surface, leaving the native
// scrollbar gutter outside it. Adapter mirroring happens outside this policy.
fn action_frame(row: ffi::Rect, viewport: ffi::Rect) -> ffi::Rect {
    ffi::Rect {x: viewport.x, y: row.y, width: viewport.width.max(0), height: row.height.max(0)}
}

fn row_layout(width: i32, height: i32, title_width: i32,
              detail: bool, chip: bool, close: bool) -> ffi::RowLayout {
    let width = width.max(0); let height = height.max(0);
    let lead = if chip {52} else {36};
    let trail = if close {32} else {10};
    let available = (width - lead - trail).max(0);
    let title = if detail {title_width.max(0).min((i64::from(available) * 3 / 5) as i32)} else {available};
    ffi::RowLayout {
        title: ffi::Rect { x: lead, y: 0, width: title, height },
        detail: ffi::Rect { x: lead + title + 10, y: 0,
            width: if detail {(available - title - 10).max(0)} else {0}, height },
        close: ffi::Rect { x: (width - 30).max(0), y: (height - 24).max(0) / 2,
            width: if close {24.min(width)} else {0}, height: if close {24.min(height)} else {0} },
    }
}

fn footer_layout(width: i32, content_width: i32) -> ffi::ColumnLayout {
    let width=width.max(0); let middle=(width-66).max(0);
    let height=if content_width>middle {47} else {32};
    ffi::ColumnLayout {width,height,children:vec![
        ffi::Rect{x:0,y:0,width:32.min(width),height:32},
        ffi::Rect{x:32,y:0,width:1,height:1},
        ffi::Rect{x:33,y:0,width:middle,height},
        ffi::Rect{x:(width-33).max(0),y:0,width:1,height:1},
        ffi::Rect{x:(width-32).max(0),y:0,width:32.min(width),height:32},
    ]}
}
fn strip_layout(width: i32, height: i32, spacing: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    let spacing=spacing.max(0);
    let natural=children.iter().fold(0i32,|sum,c|sum.saturating_add(c.width.max(0)))
        .saturating_add(spacing.saturating_mul(children.len().saturating_sub(1).min(i32::MAX as usize) as i32));
    let natural_height=children.iter().map(|c|c.height.max(0)).max().unwrap_or(0);
    let height=if height<0 {natural_height} else {height}; let mut x=0i32;
    let rects=children.iter().map(|c| {
        let r=ffi::Rect{x,y:(height-c.height.max(0)).max(0)/2,width:c.width.max(0),height:c.height.max(0)};
        x=x.saturating_add(r.width).saturating_add(spacing); r
    }).collect();
    ffi::ColumnLayout {width:if width<0 {natural} else {width},height,children:rects}
}

fn address_layout(width: i32, height: i32, host_width: i32) -> ffi::RowLayout {
    let width = width.max(0); let height = height.max(0);
    let available = (width - 46).max(0);
    let host = host_width.clamp(0, available);
    ffi::RowLayout {
        title: ffi::Rect {x: 34, y: 0, width: host, height},
        detail: ffi::Rect {x: 34 + host, y: 0, width: available - host, height},
        close: ffi::Rect {x: 0, y: 0, width: 0, height: 0},
    }
}


fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize {
    if count == 0 { return 0; }
    let current = current.min(count - 1);
    if backwards { if current == 0 { count - 1 } else { current - 1 } }
    else if current + 1 == count { 0 } else { current + 1 }
}
// Navigation and dismiss icons use only clipped analytic rectangles/circles,
// so Metal and the CPU recipe do not depend on native vector-icon painting.
fn preference_control_scene(i:&ffi::Item)->Vec<ffi::Primitive>{
    let mut ops=Vec::new();
    let mut rect=|x:f32,y:f32,w:f32,h:f32,r:f32,color:u32,stroke:f32|{if w>0.0&&h>0.0{ops.push(ffi::Primitive{x,y,width:w,height:h,radius:r,color,stroke,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,clip:ffi::FloatRect{x:i.clip.x,y:i.clip.y,width:i.clip.width,height:i.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0});}};
    if i.fill>>24!=0{rect(i.x,i.y,i.width,i.height,8.0,i.fill,0.0);}
    if i.active&&i.kind==22{rect(i.x+0.5,i.y+0.5,(i.width-1.0).max(0.0),(i.height-1.0).max(0.0),8.0,i.detail_ink,1.0);}
    let cx=if i.kind==23{i.x+i.width/2.0}else if i.tile{i.x+i.width-18.0}else{i.x+18.0};let cy=i.y+i.height/2.0;
    if i.kind==23{
        for n in -5..=5{let d=n as f32;rect(cx+d-0.8,cy+d-0.8,1.6,1.6,0.8,i.ink,0.0);rect(cx+d-0.8,cy-d-0.8,1.6,1.6,0.8,i.ink,0.0);}
    }else{
        match i.glyph{
            0=>{rect(cx-5.5,cy-5.5,11.0,11.0,5.5,i.ink,1.5);rect(cx-2.0,cy-2.0,4.0,4.0,2.0,i.ink,1.25);for (x,y,w,h) in [(-1.0,-8.0,2.0,3.0),(-1.0,5.0,2.0,3.0),(-8.0,-1.0,3.0,2.0),(5.0,-1.0,3.0,2.0)]{rect(cx+x,cy+y,w,h,0.5,i.ink,0.0);}},
            1=>{rect(cx-6.0,cy-6.0,10.0,10.0,5.0,i.ink,1.5);for d in 0..5{rect(cx+2.0+d as f32,cy+2.0+d as f32,1.8,1.8,0.8,i.ink,0.0);}},
            2|8=>{rect(cx-6.0,cy-7.0,12.0,14.0,4.0,i.ink,1.5);rect(cx-0.75,cy-4.0,1.5,6.0,0.5,i.ink,0.0);rect(cx-0.75,cy+3.0,1.5,1.5,0.75,i.ink,0.0);},
            3|9=>{for (y,x) in [(-5.0,-3.0),(0.0,3.0),(5.0,-1.0)]{rect(cx-7.0,cy+y-0.6,14.0,1.2,0.6,i.ink,0.0);rect(cx+x-1.5,cy+y-1.5,3.0,3.0,1.5,i.ink,0.0);}},
            4|7=>{rect(cx-7.0,cy-7.0,14.0,14.0,7.0,i.ink,1.25);rect(cx-3.0,cy-7.0,6.0,14.0,3.0,i.ink,1.0);rect(cx-6.0,cy-0.6,12.0,1.2,0.6,i.ink,0.0);},
            5=>{rect(cx-4.0,cy-7.0,8.0,10.0,4.0,i.ink,1.5);rect(cx-6.0,cy-1.0,12.0,9.0,2.0,i.ink,0.0);},
            6=>{rect(cx-0.75,cy-7.0,1.5,10.0,0.75,i.ink,0.0);for d in 0..5{let d=d as f32;rect(cx-d-0.75,cy+4.0-d-0.75,1.5,1.5,0.5,i.ink,0.0);rect(cx+d-0.75,cy+4.0-d-0.75,1.5,1.5,0.5,i.ink,0.0);}rect(cx-6.0,cy+7.0,12.0,1.5,0.75,i.ink,0.0);},
            _=>{}
        }
    }
    if i.focused{rect(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,0.0);}
    if i.kind==22&&!i.title.is_empty(){let x=if i.tile{i.x+10.0}else{i.x+36.0};let w=(i.width-46.0).max(0.0);if w>0.0{ops.push(ffi::Primitive{x,y:i.y,width:w,height:i.height,radius:0.0,color:i.ink,stroke:0.0,shape:0,text:i.title.clone(),font_size:i.font_size,font_weight:0,bold:i.active,middle:false,center:false,clip:ffi::FloatRect{x:i.clip.x,y:i.clip.y,width:i.clip.width,height:i.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0});}}
    ops
}

// Keep interactive rectangles transparent, including during a per-control
// drawable failure. Only display geometry crosses this clipping boundary.
fn preference_chrome_scene(items:&[ffi::Item],holes:&[ffi::FloatRect])->Vec<ffi::Primitive>{
    let mut result=Vec::new();
    for op in layout(items){
        let mut clips=vec![ffi::FloatRect{x:op.clip.x,y:op.clip.y,width:op.clip.width,height:op.clip.height}];
        for hole in holes {
            if ![hole.x,hole.y,hole.width,hole.height].iter().all(|x|x.is_finite())||hole.width<=0.0||hole.height<=0.0{continue;}
            let mut next=Vec::new();for c in clips {
                let x=c.x.max(hole.x);let y=c.y.max(hole.y);let right=(c.x+c.width).min(hole.x+hole.width);let bottom=(c.y+c.height).min(hole.y+hole.height);
                if right<=x||bottom<=y{next.push(c);continue;}
                for (px,py,w,h) in [(c.x,c.y,c.width,y-c.y),(c.x,bottom,c.width,c.y+c.height-bottom),(c.x,y,x-c.x,bottom-y),(right,y,c.x+c.width-right,bottom-y)]{if w>0.0&&h>0.0{next.push(ffi::FloatRect{x:px,y:py,width:w,height:h});}}
            }clips=next;if clips.is_empty(){break;}
        }
        for clip in clips{let mut piece=op.clone();piece.clip=clip;result.push(piece);}
    }result
}

fn layout(items: &[ffi::Item]) -> Vec<ffi::Primitive> {
    let mut out = Vec::with_capacity(items.len() * 5);
    for i in items {
        if ![i.x, i.y, i.width, i.height].iter().all(|v| v.is_finite())
            || i.width <= 0.0 || i.height <= 0.0
            || ![i.clip.x, i.clip.y, i.clip.width, i.clip.height].iter().all(|v| v.is_finite())
            || i.clip.width <= 0.0 || i.clip.height <= 0.0 { continue; }
        if i.kind==22||i.kind==23{out.extend(preference_control_scene(i));continue;}
        let first_primitive = out.len();
        let mut emit = |x, y, w: f32, h: f32, radius, color, text: &str, size, bold| {
            if w > 0.0 && h > 0.0 {
                out.push(ffi::Primitive { x, y, width: w, height: h, radius,
                    color, stroke: 0.0, shape: 0, text: text.into(), font_size: size, font_weight:0, bold, middle: false, center: false, editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,
                    clip: ffi::FloatRect {x: i.clip.x, y: i.clip.y, width: i.clip.width, height: i.clip.height} });
            }
        };
        if i.kind==26 {
            if !i.title_width.is_finite()||!i.detail_font_size.is_finite(){continue;}
            emit(i.x,i.y,i.width,i.height,0.0,i.fill,"",0.0,false);
            let mut slice=|left:f32,right:f32|{let left=(i.width*left).round();let right=(i.width*right.min(1.0)).round();let x=if i.tile{i.x+i.width-right}else{i.x+left};emit(x,i.y,(right-left).max(0.0),i.height,0.0,i.ink,"",0.0,false);};
            if i.title_width>=0.0{slice(0.0,i.title_width.min(1.0));}
            else{let t=i.detail_font_size.clamp(0.0,1.0);if t<0.5{slice(t/2.0,t*2.0);}else if t<0.75{slice(t*3.0-1.25,1.0);slice(0.0,t-0.5);}else{let left=(t-0.75)*4.0;slice(left,left+0.25-(t-0.75));}}
            continue;
        }
        if i.kind == 17 {
            if !i.title.is_empty(){emit(i.x,i.y,i.width,i.height,0.0,i.ink,&i.title,i.font_size,i.active);let last=out.last_mut().unwrap();last.font_weight=i.font_weight;last.center=i.glyph==1;}continue;
        }
        if i.kind==24 {
            emit(i.x,i.y,i.width,i.height,12.0,i.fill,"",0.0,false);
            if !i.active&&i.width>1.0&&i.height>1.0 {emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,12.0,i.ink,"",0.0,false);out.last_mut().unwrap().stroke=1.0;}
            continue;
        }
        if i.kind == 16 {
            emit(i.x,i.y,i.width,i.height,14.0,i.fill,"",0.0,false);
            emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,13.5,i.badge,"",0.0,false);
            if let Some(border)=out.get_mut(first_primitive+1){border.stroke=1.0;}continue;
        }
        if i.kind == 15 {
            if i.glyph==0{emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,4.5,i.badge,"",0.0,false);}
            let g=palette_text_layout(i.glyph,i.width as i32,i.height as i32,i.title_width as i32,i.font_height as i32);
            let r=&g.children[0];
            if !i.title.is_empty(){emit(i.x+r.x as f32,i.y+r.y as f32,r.width as f32,r.height as f32,0.0,i.ink,&i.title,i.font_size,false);}
            if i.glyph==0{if let Some(border)=out.get_mut(first_primitive){border.stroke=1.0;}}continue;
        }
        if i.kind == 14 {emit(i.x,i.y,i.width,i.height,0.0,i.fill,"",0.0,false);continue;}
        if i.kind == 13 {
            let start=first_primitive;
            emit(i.x+18.0,i.y+(i.height-16.0)/2.0,16.0,16.0,0.0,i.ink,"",0.0,false);
            for p in out[start..].iter_mut(){p.shape=if i.glyph==1{3}else if i.glyph==2{6}else{7};}continue;
        }
        if i.kind == 12 {
            emit(i.x,i.y,i.width,i.height,if i.active{9.0}else{8.0},i.fill,"",0.0,false);
            emit(i.x+12.0,i.y+(i.height-28.0)/2.0,28.0,28.0,7.0,i.badge,"",0.0,false);
            let g=row_layout(i.width as i32,i.height as i32,i.title_width as i32,!i.detail.is_empty(),true,false);
            if i.focused{emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            if !i.title.is_empty(){emit(i.x+g.title.x as f32,i.y,(g.title.width as f32).min(i.title_width.max(0.0)),i.height,0.0,i.ink,&i.title,i.font_size,true);}
            if !i.detail.is_empty(){emit(i.x+g.detail.x as f32,i.y,g.detail.width as f32,i.height,0.0,i.detail_ink,&i.detail,i.detail_font_size,false);}
            for p in out[first_primitive..].iter_mut(){if !p.text.is_empty()&&!p.bold{p.middle=true;}}continue;
        }
        if i.kind == 11 {
            if i.active||i.hovered {emit(i.x+i.width-3.0,i.y+8.0,2.0,(i.height-16.0).max(0.0),1.0,i.ink,"",0.0,false);}continue;
        }
        if i.kind == 10 {
            emit(i.x,i.y,i.width,i.height,10.0,i.fill,"",0.0,false);continue;
        }
        if i.kind == 8 {
            let g=pane_bar_layout(i.width as i32,i.height as i32);
            if !i.title.is_empty() {emit(i.x+g.title.x as f32,i.y,(g.title.width as f32).min(i.title_width.max(0.0)),i.height,0.0,i.ink,&i.title,i.font_size,false);}continue;
        }
        if i.kind == 9 {
            let across=i.width>i.height;let extent=if across{i.width}else{i.height};let grip=40.0f32.min(extent/4.0);
            emit(i.x+(i.width-if across{grip}else{4.0})/2.0,i.y+(i.height-if across{4.0}else{grip})/2.0,
                if across{grip}else{4.0},if across{4.0}else{grip},2.0,i.ink,"",0.0,false);continue;
        }
        if i.kind == 21 {
            if !(i.width>0.0&&i.height>0.0){continue;}
            emit(i.x,i.y,i.width,i.height,6.0,i.fill,"",0.0,false);
            if i.hovered {emit(i.x,i.y,i.width,i.height,6.0,i.detail_ink,"",0.0,false);}
            let g=select_layout(i.width as i32,i.height as i32,0,0,i.tile);
            if !i.title.is_empty()&&g.label.width>0 {emit(i.x+g.label.x as f32,i.y,g.label.width as f32,i.height,0.0,i.ink,&i.title,i.font_size,i.active);}
            if g.arrow.width>0 {emit(i.x+g.arrow.x as f32,i.y,g.arrow.width as f32,i.height,0.0,i.ink,"▾",i.font_size,false);}
            emit(i.x,i.y,i.width,i.height,6.0,i.badge,"",0.0,false);if let Some(p)=out.last_mut(){p.stroke=if i.focused{2.0}else{1.0};}for p in out[first_primitive..].iter_mut(){if p.text=="▾"{p.center=true;}}
            continue;
        }
        if i.kind == 20 {
            if !(i.width>0.0&&i.height>0.0){continue;}
            if i.hovered {emit(i.x-3.0,i.y-3.0,i.width+6.0,i.height+6.0,12.0,(i.badge&0xffffff)|0x14000000,"",0.0,false);}
            if i.active {emit(i.x,i.y,i.width,i.height,2.0,i.fill,"",0.0,false);emit(i.x,i.y,i.width,i.height,0.0,i.ink,"✓",i.font_size,false);}
            else {emit(i.x,i.y,i.width,i.height,2.0,i.badge,"",0.0,false);}
            if i.focused {emit(i.x-2.0,i.y-2.0,i.width+4.0,i.height+4.0,4.0,i.detail_ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut() {
                if !p.text.is_empty(){p.center=true;}
                else if !i.active&&p.x==i.x&&p.y==i.y&&p.width==i.width&&p.height==i.height {p.stroke=1.5;}
                else if i.focused&&p.x==i.x-2.0&&p.y==i.y-2.0 {p.stroke=2.0;}
            }
            continue;
        }
        if i.kind == 19 {
            if !(i.width>0.0&&i.height>0.0){continue;}
            let radius=i.width.min(i.height).max(0.0)/2.0;
            if i.fill>>24!=0 {emit(i.x,i.y,i.width,i.height,radius,i.fill,"",0.0,false);}
            if i.hovered&&i.detail_ink>>24!=0 {emit(i.x,i.y,i.width,i.height,radius,i.detail_ink,"",0.0,false);}
            let padding=i.title_width.max(0.0).min(i.width.max(0.0)/2.0);
            if !i.title.is_empty(){emit(i.x+padding,i.y,(i.width-padding*2.0).max(0.0),i.height,0.0,i.ink,&i.title,i.font_size,i.active);}
            if i.focused {emit(i.x+8.0,i.y+i.height-5.0,(i.width-16.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            if i.badge>>24!=0 {emit(i.x,i.y,i.width,i.height,radius,i.badge,"",0.0,false);}
            if i.badge>>24!=0 {if let Some(border)=out.last_mut(){border.stroke=1.0;}}
            for p in out[first_primitive..].iter_mut(){if !p.text.is_empty(){p.center=true;}}
            continue;
        }
        if i.kind == 18 {
            let cx=i.x+i.width/2.0;let cy=i.y+i.height/2.0;
            if i.active||i.hovered {emit(cx-10.0,cy-10.0,20.0,20.0,10.0,i.ink,"",0.0,false);}
            emit(cx-7.0,cy-7.0,14.0,14.0,7.0,i.badge,"",0.0,false);
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut() {if p.width==20.0&&p.height==20.0 {p.stroke=1.0;}}
            continue;
        }
        if i.kind == 7 {
            let cx=i.x+i.width/2.0; let cy=i.y+i.height/2.0;
            if i.glyph==1 {
                if i.monogram.is_empty() {
                    emit(cx-4.0,cy-4.0,8.0,8.0,4.0,i.ink,"",0.0,false);
                } else {
                    if i.fill>>24!=0 {emit(cx-13.0,cy-13.0,26.0,26.0,7.0,i.fill,"",0.0,false);}
                    emit(cx-13.0,cy-13.0,26.0,26.0,0.0,i.ink,&i.monogram,i.font_size,false);
                }
            } else {
                if i.fill>>24!=0 {emit(i.x,i.y,i.width,i.height,8.0,i.fill,"",0.0,false);}
                if i.glyph==0 {
                    emit(cx-8.0,cy-7.0,16.0,14.0,3.0,i.ink,"",0.0,false);
                    emit(cx-3.0,cy-7.0,1.0,14.0,0.0,i.ink,"",0.0,false);
                    if i.active {emit(cx-7.0,cy-6.0,4.0,12.0,0.0,(i.ink&0xffffff)|0x55000000,"",0.0,false);}
                } else if i.glyph==2 {
                    for (dy,knob) in [(-5.0,-3.0),(0.0,3.0),(5.0,-1.0)] {
                        emit(cx-7.0,cy+dy-0.5,14.0,1.0,0.5,i.ink,"",0.0,false);
                        emit(cx+knob-1.5,cy+dy-1.5,3.0,3.0,1.5,i.ink,"",0.0,false);
                    }
                } else if i.glyph==6 {
                    for dx in [-5.0,0.0,5.0] {emit(cx+dx-1.0,cy-1.0,3.0,3.0,1.5,i.ink,"",0.0,false);}
                } else {
                    emit(cx-8.0,cy-8.0,16.0,16.0,0.0,i.ink,"",0.0,false);
                }
            }
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut() {
                if !p.text.is_empty() {p.center=true;}
                if (3..=5).contains(&i.glyph) && p.width==16.0 && p.height==16.0 {p.shape=i.glyph-1;}
                if i.glyph==7 && p.width==16.0 && p.height==16.0 {p.shape=5;}
                if i.glyph==8 && p.width==16.0 && p.height==16.0 {p.shape=7;}
                if i.glyph==0 && p.width==16.0 && p.height==14.0 {p.stroke=1.0;}
                if i.glyph==1 && i.monogram.is_empty() && p.width==8.0 && p.height==8.0 && !i.active {p.stroke=1.0;}
            }
            continue;
        }
        if i.kind == 6 {
            emit(i.x + if i.tile {2.0} else {4.0}, i.y + if i.tile {4.0} else {2.0},
                (i.width-if i.tile {4.0} else {8.0}).max(0.0), (i.height-if i.tile {8.0} else {4.0}).max(0.0),
                3.5, i.fill, "", 0.0, false);
            continue;
        }
        if i.kind == 1 {
            emit(i.x, i.y, i.width, i.height, 0.0, i.ink, &i.title, i.font_size, false);
            continue;
        }
        if i.kind == 5 {
            emit(i.x + 8.0, i.y, (i.width - 16.0).max(0.0), i.height,
                0.0, i.ink, &i.title, i.font_size, true);
            continue;
        }
        if i.kind == 4 {
            // A filled rounded pill and inset outline. Ring and lock strokes
            // remain analytic GPU geometry; they need no icon/font dependency.
            emit(i.x, i.y, i.width, i.height, 10.0, i.fill, "", 0.0, false);
            let g = address_layout(i.width as i32, i.height as i32, i.title_width as i32);
            let cy = i.y + i.height / 2.0;
            let rings = [(i.x + 0.5, 1.0), (i.x + if i.active {14.0} else {12.0}, 1.5)];
            emit(i.x + 0.5, i.y + 0.5, i.width - 1.0, i.height - 1.0,
                9.5, (i.ink & 0x00ffffff) | 0x0d000000, "", 0.0, false);
            if i.active {
                emit(i.x + 14.0, cy - 7.0, 8.0, 10.0, 4.0, i.badge, "", 0.0, false);
                emit(i.x + 12.0, cy - 1.0, 12.0, 8.0, 1.5, i.badge, "", 0.0, false);
            } else {
                emit(i.x + 12.0, cy - 6.0, 10.0, 10.0, 5.0, i.badge, "", 0.0, false);
                for n in 0..4 {
                    emit(i.x + 20.0 + n as f32, cy + 2.0 + n as f32, 2.0, 2.0,
                        1.0, i.badge, "", 0.0, false);
                }
            }
            if i.focused {
                emit(i.x + 2.0, i.y + i.height - 3.0, (i.width - 4.0).max(0.0), 2.0,
                    1.0, i.ink, "", 0.0, false);
            }
            emit(i.x + g.title.x as f32, i.y, g.title.width as f32, i.height,
                0.0, i.ink, &i.title, i.font_size, true);
            if !i.detail.is_empty() {
                emit(i.x + g.detail.x as f32, i.y, g.detail.width as f32, i.height,
                    0.0, i.detail_ink, &i.detail, i.detail_font_size, false);
            }
            // No more emissions borrow out: attach analytic inset strokes.
            // Only the outline and icon ring are stroked; body/text stay filled.
            for p in out[first_primitive..].iter_mut() {
                if p.text.is_empty() {
                    if let Some((_, stroke)) = rings.iter().find(|(x, _)| *x == p.x) {
                        p.stroke = *stroke;
                    }
                }
            }
            continue;
        }
        if i.fill >> 24 != 0 {
            emit(i.x, i.y, i.width, i.height, if i.kind == 2 {12.0} else {8.0},
                 i.fill, "", 0.0, false);
        }
        if i.kind == 2 { continue; }
        if i.kind == 25 {
            // Match the original history RowButton's two single-line columns:
            // primary title elides at the tail; date/URL detail in the middle.
            let g=row_layout(i.width as i32,i.height as i32,i.title_width as i32,!i.detail.is_empty(),false,false);
            emit(i.x+10.0,i.y+(i.height-16.0)/2.0,16.0,16.0,4.0,i.badge,"",0.0,false);
            if !i.monogram.is_empty(){emit(i.x+10.0,i.y+(i.height-16.0)/2.0,16.0,16.0,0.0,0xffffffff,&i.monogram,10.0,true);}
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            emit(i.x+g.title.x as f32,i.y,g.title.width as f32,i.height,0.0,i.ink,&i.title,i.font_size,false);
            if !i.detail.is_empty() && g.detail.width>0 {
                emit(i.x+g.detail.x as f32,i.y,g.detail.width as f32,i.height,0.0,i.detail_ink,&i.detail,i.detail_font_size,false);
                out.last_mut().unwrap().middle=true;
            }
            continue;
        }
        if i.focused {
            // Inset focus indicator; event/focus ownership stays with the node.
            emit(i.x + 2.0, i.y + i.height - 3.0, (i.width - 4.0).max(0.0), 2.0,
                 1.0, i.ink, "", 0.0, false);
        }
        if i.kind == 3 {
            let g = row_layout(i.width as i32, i.height as i32, i.title_width as i32,
                !i.detail.is_empty(), false, false);
            let cy = i.y + i.height / 2.0;
            emit(i.x + 11.0, cy - 0.75, 14.0, 1.5, 0.75, i.badge, "", 0.0, false);
            emit(i.x + 17.25, cy - 7.0, 1.5, 14.0, 0.75, i.badge, "", 0.0, false);
            emit(i.x + g.title.x as f32, i.y, g.title.width as f32, i.height,
                0.0, i.ink, &i.title, i.font_size, false);
            if !i.detail.is_empty() && g.detail.width > 0 {
                emit(i.x + g.detail.x as f32, i.y, g.detail.width as f32, i.height,
                    0.0, i.detail_ink, &i.detail, i.detail_font_size, false);
                out.last_mut().unwrap().middle = true;
            }
            continue;
        }
        emit(i.x + 10.0, i.y + (i.height - 16.0) / 2.0, 16.0, 16.0,
             4.0, i.badge, "", 0.0, false);
        emit(i.x + 12.0, i.y + (i.height - 16.0) / 2.0, 14.0, 16.0,
             0.0, if i.badge >> 24 != 0 { 0xffffffff } else { i.ink },
             &i.monogram, 10.0, true);
        // Keep a close-button hit area reserved whenever it exists, just as the
        // native interaction node does (including half-width split cards).
        let geometry = row_layout(i.width as i32, i.height as i32, 0, false, false, i.close);
        emit(i.x + geometry.title.x as f32, i.y, geometry.title.width as f32, i.height,
             0.0, i.ink, &i.title, i.font_size, i.active);
        if i.close && i.close_visible {
            emit(i.x + geometry.close.x as f32 + 5.0, i.y,
                 (geometry.close.width - 10).max(0) as f32, i.height,
                 0.0, i.ink, "×", 18.0, false);
        }
    }
    out
}
#[cfg(test)]
mod tests {
    use super::*;
    fn row(width: f32) -> ffi::Item { ffi::Item {
        x:0.0, y:0.0, width, height:34.0, kind:0, glyph:0,
        title:"Zażółć 日本語 🦀".into(), monogram:"Z".into(), detail:String::new(),
        title_width:0.0, detail_ink:0, detail_font_size:12.0, ink:0xff101010,
        fill:0xffffffff, badge:0xff556677, active:true, hovered:false,
        tile:false, close:true, close_visible:true, focused:false, font_size:13.0, font_height:16.0, font_weight:0,
        clip:ffi::FloatRect {x:0.0, y:0.0, width:width.max(0.0), height:34.0},
    }}
    fn child(width: i32, height: i32) -> ffi::ColumnChild {
        ffi::ColumnChild { width, height, top: 0, left: 0, bottom: 0, right: 0 }
    }
    #[test]
    fn download_progress_rounding_direction_and_clipping() {
        let mut item=row(101.0);item.kind=26;item.height=5.0;item.fill=0xff112233;item.ink=0xff445566;item.title_width=0.5;
        let ltr=layout(std::slice::from_ref(&item));assert_eq!(ltr.len(),2);assert_eq!((ltr[0].width,ltr[1].width,ltr[1].x),(101.0,51.0,0.0));assert_eq!(ltr[0].radius,0.0);
        item.tile=true;let rtl=layout(std::slice::from_ref(&item));assert_eq!((rtl[1].x,rtl[1].width),(50.0,51.0));
        item.title_width=0.0;assert_eq!(layout(std::slice::from_ref(&item)).len(),1);item.title_width=1.0;assert_eq!(layout(std::slice::from_ref(&item))[1].width,101.0);
        item.title_width=f32::NAN;assert!(layout(std::slice::from_ref(&item)).is_empty());
    }
    #[test]
    fn download_progress_indeterminate_native_keyframes() {
        let mut item=row(100.0);item.kind=26;item.height=5.0;item.title_width=-1.0;
        for (time,segments) in [(0.0,vec![]),(0.25,vec![(13.0,37.0)]),(0.5,vec![(25.0,75.0)]),(0.625,vec![(63.0,37.0),(0.0,13.0)]),(0.75,vec![(0.0,25.0)]),(1.0,vec![])] {
            item.detail_font_size=time;let ops=layout(std::slice::from_ref(&item));assert_eq!(ops.iter().skip(1).map(|p|(p.x,p.width)).collect::<Vec<_>>(),segments);
            for p in ops{assert!(p.x>=0.0&&p.x+p.width<=100.0&&p.height==5.0);}
        }
    }
    #[test]
    fn dialog_actions_link_and_trailing_geometry() {
        let child=|width,height|ffi::ColumnChild{width,height,top:0,left:0,bottom:0,right:0};
        let children=[child(70,32),child(100,36)];
        let natural=dialog_actions_layout(-1,-1,8,200,12,20,16,20,&children);
        assert_eq!((natural.width,natural.height),(248,64));
        assert_eq!((natural.children[0].width,natural.children[1].width),(100,100));
        let wide=dialog_actions_layout(480,64,8,200,12,20,16,20,&children);
        assert_eq!((wide.children[0].x,wide.children[1].x,wide.children[0].y,wide.children[1].y),(252,360,16,12));
        let long=dialog_actions_layout(-1,-1,8,200,0,0,0,0,&[child(70,32),child(300,32)]);
        assert_eq!((long.children[0].width,long.children[1].width),(70,300));
    }
    #[test]
    fn dialog_actions_clip_empty_and_narrow_rows() {
        let empty=dialog_actions_layout(-1,-1,8,200,12,20,16,20,&[]);assert_eq!((empty.width,empty.height),(0,0));assert!(empty.children.is_empty());
        let children=[ffi::ColumnChild{width:90,height:32,top:0,left:0,bottom:0,right:0},ffi::ColumnChild{width:100,height:32,top:0,left:0,bottom:0,right:0}];
        for width in [0,1,20,40,90,480] {for height in [0,1,20,64] {
            let g=dialog_actions_layout(width,height,8,200,12,20,16,20,&children);
            for c in g.children {assert!(c.x>=0&&c.y>=0&&c.width>=0&&c.height>=0);assert!(c.x+c.width<=width&&c.y+c.height<=height);}
        }}
    }
    #[test] fn panel_flex_allocates_viewport_and_keeps_fixed_header_footer() {
        let s=ffi::PanelStyle{width:400,height:500,vertical:true,spacing:12,top:20,left:20,bottom:20,right:20,flex:1};
        let g=panel_layout(&s,&[child(300,32),child(300,1000),child(200,40)],&[]);
        assert_eq!((g.width,g.height),(340,500));assert_eq!((g.children[1].y,g.children[1].height),(64,364));assert_eq!(g.children[2].y,440);assert!(g.children.iter().all(|r|r.width==360));
    }
    #[test] fn panel_flex_shrink_preserves_initial_budget_and_clips_fixed_siblings() {
        let s=ffi::PanelStyle{width:80,height:40,vertical:false,spacing:8,top:0,left:0,bottom:0,right:0,flex:0};
        let g=panel_layout(&s,&[child(200,24),child(32,32)],&[child(40,24),child(32,32)]);
        assert_eq!((g.width,g.height),(80,32));assert_eq!((g.children[0].width,g.children[1].x,g.children[1].width),(40,48,32));
        let tiny=ffi::PanelStyle{width:20,..s};let g=panel_layout(&tiny,&[child(200,24),child(32,32)],&[]);assert_eq!((g.children[0].width,g.children[1].x,g.children[1].width),(0,8,12));assert_eq!(g.width,40);
    }
    #[test] fn panel_layout_empty_zero_children_margins_and_extreme_sizes() {
        let s=ffi::PanelStyle{width:100,height:-1,vertical:true,spacing:10,top:0,left:0,bottom:8,right:8,flex:usize::MAX};
        let g=panel_layout(&s,&[child(80,20),child(0,0),child(80,40)],&[]);assert_eq!(g.height,78);assert_eq!(g.children[2].y,30);
        let empty=panel_layout(&s,&[],&[]);assert_eq!((empty.width,empty.height),(8,8));
        let mut margin=child(80,20);margin.top=2;margin.left=3;margin.bottom=4;margin.right=5;let g=panel_layout(&s,&[margin],&[]);assert_eq!((g.width,g.height),(96,34));assert_eq!((g.children[0].x,g.children[0].y,g.children[0].width),(3,2,84));
        let huge=ffi::PanelStyle{width:i32::MAX,height:i32::MAX,spacing:i32::MAX,..s};let g=panel_layout(&huge,&[child(i32::MAX,i32::MAX),child(i32::MAX,i32::MAX)],&[]);assert_eq!(g.height,i32::MAX);assert!(g.children.iter().all(|r|r.width>=0&&r.height>=0));
    }
    #[test] fn viewport_scroll_delta_retains_fractional_pixels_and_clamps_both_edges() {
        let mut v=new_scroll_viewport();v.viewport_configure(300,200,300,900,15,true);
        for _ in 0..10{assert!(v.viewport_scroll_delta(-0.125));}assert_eq!(v.viewport_offset(),1.25);
        assert!(v.viewport_scroll_delta(0.5));assert_eq!(v.viewport_offset(),0.75);assert!(v.viewport_scroll_delta(-1e9));assert_eq!(v.viewport_offset(),700.0);assert!(!v.viewport_scroll_delta(-1.0));assert!(v.viewport_scroll_delta(1e9));assert_eq!(v.viewport_offset(),0.0);assert!(!v.viewport_scroll_delta(1.0));
    }
    #[test] fn viewport_scroll_delta_rejects_invalid_input_and_empty_extents() {
        let mut v=new_scroll_viewport();assert!(!v.viewport_scroll_delta(-50.0));v.viewport_configure(300,200,300,900,15,true);v.viewport_scroll_to(25.25);
        for delta in [f64::NAN,f64::INFINITY,f64::NEG_INFINITY,0.0]{assert!(!v.viewport_scroll_delta(delta));assert_eq!(v.viewport_offset(),25.25);}
        v.viewport_configure(300,200,300,100,15,true);assert!(!v.viewport_scroll_delta(-100.0));assert_eq!(v.viewport_offset(),0.0);
    }
    #[test] fn viewport_overflow_edges_follow_top_middle_bottom_and_content_shrink() {
        let mut v=new_scroll_viewport();v.viewport_configure(300,200,300,900,15,true);let e=v.viewport_edges(300);assert!(!e.show_top&&e.show_bottom);assert_eq!((e.top.y,e.bottom.y,e.bottom.height),(0,199,1));
        v.viewport_scroll_to(100.25);let e=v.viewport_edges(300);assert!(e.show_top&&e.show_bottom);v.viewport_scroll_to(700.0);let e=v.viewport_edges(300);assert!(e.show_top&&!e.show_bottom);
        v.viewport_configure(300,200,300,100,15,true);let e=v.viewport_edges(300);assert!(!e.show_top&&!e.show_bottom);
    }
    #[test] fn viewport_overflow_edges_reject_empty_and_fit_single_pixel_hosts() {
        let mut v=new_scroll_viewport();for (w,h) in [(0,0),(-1,-1),(0,100),(100,0)]{v.viewport_configure(w,h,100,900,15,true);let e=v.viewport_edges(w);assert!(!e.show_top&&!e.show_bottom);assert!(e.top.width>=0&&e.bottom.y>=0);}
        v.viewport_configure(1,1,1,900,15,true);let e=v.viewport_edges(1);assert_eq!((e.top.width,e.top.height,e.bottom.y),(1,1,0));assert!(e.show_bottom);
    }
    #[test] fn bounded_viewport_clips_preferred_height_without_changing_width() {
        for (height,expected) in [(0,100),(60,100),(240,240),(900,480)]{let size=viewport_preferred_size(560,height,100,480);assert_eq!((size.width,size.height),(560,expected));}
        for (min,max) in [(-1,-1),(200,100)]{assert_eq!(viewport_preferred_size(560,900,min,max).height,900);}
        assert_eq!(viewport_preferred_size(-1,-1,0,220).height,0);
    }
    #[test] fn bounded_viewport_remeasures_wrapping_only_when_scrollbar_reserves_width() {
        for overlay in [false,true]{for full_height in [100,200,201,900]{assert_eq!(viewport_measure_width(300,200,full_height,15,overlay),if full_height>200&&!overlay{285}else{300});}}
        assert_eq!(viewport_measure_width(1,1,900,15,false),0);assert_eq!(viewport_measure_width(-1,-1,900,15,false),0);
        let mut state=new_scroll_viewport();let width=viewport_measure_width(300,200,900,15,false);let g=state.viewport_configure(300,200,width,960,15,false);assert_eq!((g.viewport.width,g.content.width,g.max_offset),(285,285,760));
    }
    #[test] fn scroll_viewport_geometry_preserves_overlay_and_reserved_scrollbar_contract() {
        for overlay in [false,true]{let mut v=new_scroll_viewport();let g=v.viewport_configure(300,200,280,900,15,overlay);assert!(g.show_bar);assert_eq!(g.viewport.width,if overlay{300}else{285});assert_eq!((g.content.width,g.content.height,g.bar.x,g.bar.width),(if overlay{300}else{285},900,285,15));assert_eq!(g.max_offset,700);}
    }
    #[test] fn horizontal_viewport_reserves_bottom_bar_and_preserves_cross_extent() {
        for overlay in [false,true]{let mut v=new_scroll_viewport();let p=v.viewport_configure_horizontal(200,47,600,32,15,overlay);assert_eq!((p.viewport.width,p.viewport.height),(200,if overlay{47}else{32}));assert_eq!((p.content.width,p.content.height),(600,if overlay{47}else{32}));assert_eq!((p.bar.x,p.bar.y,p.bar.width,p.bar.height),(0,32,200,15));assert!(p.show_bar);assert_eq!(p.max_offset,400);let e=v.viewport_axis_edges(p.viewport.height,true);assert_eq!((e.top.x,e.top.y,e.top.width,e.top.height),(0,0,1,p.viewport.height));assert_eq!((e.bottom.x,e.bottom.width),(199,1));assert!(!e.show_top&&e.show_bottom);}
    }
    #[test] fn horizontal_viewport_fractional_reveal_resize_and_empty_extent() {
        let mut v=new_scroll_viewport();v.viewport_configure_horizontal(200,47,600,32,15,false);assert!(v.viewport_scroll_delta(-13.25));assert_eq!(v.viewport_offset(),13.25);assert!(v.viewport_reveal(560,28));assert_eq!(v.viewport_offset(),388.0);let p=v.viewport_configure_horizontal(500,47,600,32,15,false);assert_eq!((p.offset,p.max_offset),(100.0,100));let p=v.viewport_configure_horizontal(700,32,600,32,15,false);assert!(!p.show_bar);assert_eq!((p.offset,p.max_offset,p.content.width),(0.0,0,700));for (w,h) in [(0,47),(200,0),(-1,-1)]{let p=v.viewport_configure_horizontal(w,h,i32::MAX,32,15,false);assert!(!p.show_bar);assert_eq!(p.max_offset,0);assert!(p.viewport.width>=0&&p.viewport.height>=0&&p.bar.y>=0);}
    }
    #[test] fn scroll_viewport_resize_and_rebuild_clamp_authoritative_offset() {
        let mut v=new_scroll_viewport();v.viewport_configure(300,200,300,900,15,true);assert!(v.viewport_scroll_to(2000.0));assert_eq!(v.viewport_offset(),700.0);let g=v.viewport_configure(300,200,300,240,15,true);assert_eq!(g.offset,40.0);let g=v.viewport_configure(300,500,200,240,15,true);assert!(!g.show_bar);assert_eq!((g.offset,g.max_offset,g.content.height),(0.0,0,500));
    }
    #[test] fn scroll_viewport_reveal_uses_native_minimum_movement_and_oversized_target_rule() {
        let mut v=new_scroll_viewport();v.viewport_configure(300,200,300,900,15,true);assert!(!v.viewport_reveal(20,40));assert!(v.viewport_reveal(350,40));assert_eq!(v.viewport_offset(),190.0);assert!(!v.viewport_reveal(200,20));assert!(v.viewport_reveal(300,1000));assert_eq!(v.viewport_offset(),300.0);assert!(v.viewport_reveal(-100,40));assert_eq!(v.viewport_offset(),0.0);assert!(v.viewport_reveal(i32::MAX,i32::MAX));assert_eq!(v.viewport_offset(),700.0);
    }
    #[test] fn scroll_viewport_fractional_and_nonfinite_requests_never_poison_state() {
        let mut v=new_scroll_viewport();v.viewport_configure(300,200,300,900,15,true);for _ in 0..10{assert!(v.viewport_scroll_to(v.viewport_offset()+0.125));}assert_eq!(v.viewport_offset(),1.25);for bad in [f64::NAN,f64::INFINITY,f64::NEG_INFINITY]{assert!(!v.viewport_scroll_to(bad));assert_eq!(v.viewport_offset(),1.25);}assert!(v.viewport_scroll_to(-10.0));assert_eq!(v.viewport_offset(),0.0);
    }
    #[test] fn scroll_viewport_empty_tiny_and_extreme_metrics_have_valid_bounds() {
        let mut v=new_scroll_viewport();for (w,h,p) in [(0,0,900),(0,20,900),(20,0,900),(-1,-2,-3),(1,1,i32::MAX),(i32::MAX,1,i32::MAX)]{let g=v.viewport_configure(w,h,p,p,15,false);assert!(g.viewport.width>=0&&g.viewport.height>=0&&g.content.width>=0&&g.content.height>=0&&g.bar.x>=0&&g.bar.width>=0);if w<=0||h<=0{assert_eq!(g.max_offset,0);assert!(!g.show_bar);assert!(!v.viewport_reveal(100,20));}}
    }
    #[test] fn panel_center_uses_common_axis_odd_bias_and_asymmetric_margins() {
        let s=ffi::PanelStyle{width:400,height:101,vertical:false,spacing:20,top:0,left:0,bottom:0,right:0,flex:usize::MAX};let mut a=child(100,33);a.top=2;a.bottom=8;let mut b=child(80,20);b.top=9;b.bottom=1;let g=panel_center_layout(&s,&[a,b],&[]);assert_eq!((g.width,g.height),(200,43));assert_eq!((g.children[0].y,g.children[0].height),(31,33));assert_eq!((g.children[1].y,g.children[1].height),(38,20));
    }
    #[test] fn panel_center_flex_remeasurement_preserves_fixed_controls_and_new_cross_size() {
        let s=ffi::PanelStyle{width:180,height:150,vertical:false,spacing:20,top:16,left:16,bottom:16,right:16,flex:0};let g=panel_center_layout(&s,&[child(300,20),child(40,33)],&[child(88,81),child(40,33)]);assert_eq!((g.children[0].width,g.children[1].x,g.children[1].width),(88,124,40));assert_eq!((g.height,g.children[0].height,g.children[1].height),(113,81,33));assert_eq!(g.children[1].y-g.children[0].y,24);
    }
    #[test] fn panel_center_empty_zero_constraints_and_saturation_keep_nonnegative_rectangles() {
        let s=ffi::PanelStyle{width:0,height:0,vertical:false,spacing:20,top:16,left:16,bottom:16,right:16,flex:0};let g=panel_center_layout(&s,&[child(100,33),child(40,20)],&[]);assert_eq!((g.width,g.height),(192,65));assert!(g.children.iter().all(|r|r.width==0&&r.height==0));
        for width in [0,1,33,180,i32::MAX]{for height in [0,1,34,150,i32::MAX]{let bounded=ffi::PanelStyle{width,height,..s};let g=panel_center_layout(&bounded,&[child(100,33),child(40,20)],&[]);assert!(g.children.iter().all(|r|r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0));}}
        let empty=panel_center_layout(&s,&[],&[]);assert_eq!((empty.width,empty.height),(32,32));assert!(empty.children.is_empty());
    }
    #[test] fn column_measures_full_scroll_extent_and_stretches_rows() {
        let children: Vec<_> = (0..29).map(|_| child(200, 34)).collect();
        let natural = column_layout(-1, 2, 0, 0, 0, 0, &children);
        assert_eq!((natural.width, natural.height), (200, 1042));
        let actual = column_layout(300, 2, 0, 0, 0, 0, &children);
        assert_eq!(actual.children.last().unwrap().y, 1008);
        assert!(actual.children.iter().all(|r| r.width == 300 && r.height == 34));
        assert_eq!(actual.height, natural.height);
    }
    #[test] fn column_handles_folder_indents_empty_lists_and_size_limits() {
        let mut indented = child(200, 46); indented.left = 24;
        let result = column_layout(200, 2, 3, 8, 5, 6, &[indented, child(200, 34)]);
        assert_eq!((result.children[0].x, result.children[0].width), (32, 162));
        assert_eq!(result.children[1].y, 51);
        assert_eq!(result.height, 90);
        let empty = column_layout(-1, 20, 3, 8, 5, 6, &[]);
        assert_eq!((empty.width, empty.height), (14, 8));
        let huge = column_layout(10, i32::MAX, 0, 0, 0, 0,
            &[child(i32::MAX, i32::MAX), child(i32::MAX, i32::MAX)]);
        assert_eq!(huge.height, i32::MAX);
        assert_eq!(huge.children[1].y, i32::MAX);
        assert_eq!(huge.children[1].height, 0);
    }
    #[test] fn action_frame_aligns_with_viewport_without_changing_vertical_layout() {
        let r = action_frame(ffi::Rect {x: 4, y: 70, width: 240, height: 34},
            ffi::Rect {x: 4, y: 106, width: 225, height: 500});
        assert_eq!((r.x, r.y, r.width, r.height), (4, 70, 225, 34));
        let narrow = action_frame(r, ffi::Rect {x: 19, y: 106, width: -1, height: 0});
        assert_eq!((narrow.x, narrow.width, narrow.height), (19, 0, 34));
    }
    #[test] fn history_rows_keep_native_columns_elision_focus_and_clip() {
        let mut item=row(320.0);item.kind=25;item.height=60.0;item.active=false;
        item.title_width=120.0;item.detail="2026 · example.test/אבג/🦀".into();item.detail_ink=0xff778899;
        item.monogram.clear();item.close=false;item.focused=true;
        item.clip=ffi::FloatRect{x:0.0,y:12.0,width:320.0,height:24.0};
        for width in [20.0,80.0,160.0,320.0] {
            item.width=width;let ops=layout(std::slice::from_ref(&item));
            let g=row_layout(width as i32,60,120,true,false,false);
            let primary=ops.iter().find(|op|op.text==item.title);
            if g.title.width>0 {let p=primary.unwrap();assert_eq!((p.x,p.width,p.height),(36.0,g.title.width as f32,60.0));assert!(!p.middle);assert!(!p.bold);} else {assert!(primary.is_none());}
            let detail=ops.iter().find(|op|op.text==item.detail);
            if g.detail.width>0 {let p=detail.unwrap();assert_eq!((p.x,p.width,p.font_size),(g.detail.x as f32,g.detail.width as f32,12.0));assert!(p.middle);assert_eq!(p.color,item.detail_ink);} else {assert!(detail.is_none());}
            assert!(ops.iter().all(|p|p.clip.y==12.0&&p.clip.height==24.0));
            assert!(ops.iter().any(|p|p.text.is_empty()&&p.y==57.0&&p.height==2.0)||width<=4.0);
        }
        item.width=320.0;item.clip.height=0.0;assert!(layout(&[item]).is_empty());
    }
    #[test] fn new_tab_action_keeps_title_hint_icon_and_clip() {
        let mut item = row(177.0); item.kind = 3; item.close = false;
        item.title = "Nowa karta".into(); item.title_width = 75.0;
        item.detail = "Cmd+Shift+T".into(); item.detail_font_size = 12.0;
        let ops = layout(&[item]);
        let title = ops.iter().find(|p| p.text == "Nowa karta").unwrap();
        let detail = ops.iter().find(|p| p.text == "Cmd+Shift+T").unwrap();
        assert_eq!(title.x, 36.0); assert_eq!(title.width, 75.0);
        assert!(title.x + title.width < detail.x);
        assert!(detail.x + detail.width <= 167.0);
        assert!(detail.middle && !title.middle); assert_eq!(detail.font_size, 12.0);
        assert!(ops.iter().any(|p| p.width == 14.0 && p.height == 1.5));
        assert!(!ops.iter().any(|p| p.text == "×" || p.text == "Z"));
        assert!(ops.iter().all(|p| p.clip.width == 177.0));
    }
    #[test] fn new_tab_action_handles_empty_hint_and_narrow_widths() {
        for width in [10.0, 30.0, 100.0, 480.0] {
            let mut item = row(width); item.kind = 3; item.close = false;
            item.title = "Nowa karta".into(); item.detail = String::new();
            let ops = layout(&[item]);
            assert!(!ops.iter().any(|p| p.middle));
            assert!(ops.iter().all(|p| p.width > 0.0 && p.height > 0.0));
        }
    }
    #[test] fn address_preserves_host_before_path_and_clamps_narrow_widths() {
        for width in [0, 30, 46, 200, 480] {
            let g = address_layout(width, 40, 85);
            assert_eq!(g.title.x, 34);
            assert_eq!(g.detail.x, g.title.x + g.title.width);
            assert_eq!(g.title.width + g.detail.width, (width - 46).max(0));
            assert_eq!(g.close.width, 0);
        }
    }
    #[test] fn address_keeps_secure_and_search_icons_and_tail_elision() {
        for secure in [false, true] {
            let mut item = row(240.0); item.kind = 4; item.active = secure;
            item.title = "example.org".into(); item.detail = "/日本語/path".into();
            item.title_width = 85.0; item.focused = true;
            let ops = layout(&[item]);
            let host = ops.iter().find(|p| p.text == "example.org").unwrap();
            let path = ops.iter().find(|p| p.text == "/日本語/path").unwrap();
            assert_eq!(host.x + host.width, path.x);
            assert!(host.bold && !path.bold && !path.middle);
            assert!(ops.iter().filter(|p| p.stroke > 0.0).count() == 2);
            assert_eq!(ops.iter().any(|p| p.width == 12.0 && p.height == 8.0), secure);
            assert!(ops.iter().all(|p| p.clip.width == 240.0));
            assert!(!ops.iter().any(|p| p.text == "×" || p.text == "Z"));
        }
    }
    #[test] fn workspace_heading_uses_inset_bold_unicode_without_badge() {
        let mut item = row(200.0); item.kind = 5; item.title = "🚀 Praca 日本語".into();
        let ops = layout(&[item]); assert_eq!(ops.len(), 1);
        assert_eq!((ops[0].x, ops[0].width), (8.0, 184.0));
        assert!(ops[0].bold); assert_eq!(ops[0].text, "🚀 Praca 日本語");
    }
    #[test] fn scroll_thumb_reaches_both_endpoints_and_handles_tiny_tracks() {
        let start = scroll_metrics(300, 300, 3000, -10);
        assert_eq!((start.position, start.length, start.offset, start.max_offset), (0, 30, 0, 2700));
        let end = scroll_metrics(300, 300, 3000, i32::MAX);
        assert_eq!((end.position, end.offset), (270, 2700));
        assert_eq!(scroll_offset(300, 300, 3000, 270.0), 2700);
        assert_eq!(scroll_offset(300, 300, 3000, 135.0), 1350);
        assert_eq!(scroll_metrics(10, 100, 10000, 9900).length, 10);
        assert_eq!(scroll_offset(10, 100, 10000, 1.0), 0);
        assert_eq!(scroll_metrics(0, 0, -1, 20).length, 0);
        assert_eq!(scroll_metrics(300, 400, 300, 50).length, 300);
        assert_eq!(scroll_offset(300, 300, 3000, f32::NAN), 0);
    }
    #[test] fn scroll_drag_keeps_grip_snaps_back_and_cancels_without_late_scroll() {
        let mut drag = new_scroll_thumb();
        drag.begin(300, 300, 3000, 1000, 105.0); // Thumb starts at 100, grip is 5.
        assert_eq!(drag.drag(300, 300, 3000, 1000, 275.0, 7.0, 15, true), 2700);
        assert_eq!(drag.drag(300, 300, 3000, 2700, 275.0, -129.0, 15, true), 1000);
        assert_eq!(drag.drag(300, 300, 3000, 1000, 275.0, -129.0, 15, false), 2700);
        assert_eq!(drag.drag(300, 300, 3000, 1000, f32::NAN, 0.0, 15, true), 1000);
        // Content can shrink during capture; initial snap offset is normalized.
        assert_eq!(drag.drag(300, 300, 500, 0, 100.0, 999.0, 15, true), 200);
        drag.end();
        assert_eq!(drag.drag(300, 300, 3000, 400, 275.0, 0.0, 15, true), 400);
    }
    #[test] fn scrollbar_display_is_an_inset_clipped_thumb_without_text_or_cards() {
        let mut item = row(15.0); item.kind = 6; item.height = 80.0;
        let ops = layout(&[item]); assert_eq!(ops.len(), 1);
        assert_eq!((ops[0].x, ops[0].y, ops[0].width, ops[0].height), (4.0, 2.0, 7.0, 76.0));
        assert!(ops[0].text.is_empty()); assert_eq!(ops[0].clip.width, 15.0);
    }
    #[test] fn footer_reserves_a_horizontal_scroll_gutter_only_for_overflow() {
        let normal=footer_layout(240,60); assert_eq!(normal.height,32);
        let overflow=footer_layout(200,300); assert_eq!(overflow.height,47);
        assert_eq!(overflow.children[2].width,134); assert_eq!(overflow.children[2].height,47);
        assert_eq!(overflow.children[4].x,168); assert_eq!(overflow.children[0].y,0);
        let dots=strip_layout(-1,32,0,&[child(24,28),child(30,28),child(24,28)]);
        assert_eq!((dots.width,dots.children[1].x,dots.children[1].y),(78,24,2));
    }
    #[test] fn footer_scene_keeps_selected_plain_and_centered_emoji_variants() {
        let mut item=row(30.0);item.kind=7;item.glyph=1;item.monogram="🚀".into();
        let ops=layout(&[item]);let text=ops.iter().find(|p|p.text=="🚀").unwrap();assert!(text.center);
        let mut plain=row(24.0);plain.kind=7;plain.glyph=1;plain.monogram.clear();plain.active=false;
        let ops=layout(&[plain]);assert_eq!(ops.len(),1);assert_eq!(ops[0].stroke,1.0);
        let mut horizontal=row(80.0);horizontal.kind=6;horizontal.tile=true;horizontal.height=15.0;
        let ops=layout(&[horizontal]);assert_eq!((ops[0].width,ops[0].height),(76.0,7.0));
    }
    #[test] fn navigation_strip_preserves_gaps_and_arrow_reload_shapes() {
        let g=strip_layout(-1,32,2,&[child(32,32),child(32,32),child(32,32),child(32,32)]);
        assert_eq!(g.width,134);assert_eq!(g.children[3].x,102);
        for glyph in 3..=5 {
            let mut item=row(32.0);item.kind=7;item.glyph=glyph;item.height=32.0;item.focused=true;
            let ops=layout(&[item]);let icon=ops.iter().find(|p|p.shape>0).unwrap();
            assert_eq!(icon.shape,glyph-1);assert_eq!((icon.x,icon.y,icon.width,icon.height),(8.0,8.0,16.0,16.0));
            assert!(ops.iter().all(|p|p.text.is_empty()));
        }
    }
    fn menu_state()->ffi::TabMenuState{ffi::TabMenuState{ready:true,busy:false,valid:true,tab:7,has_tab:true,split:true,first:3,has_first:true,second:7,has_second:true,vertical:false,tab_count:3,http:true}}
    fn select_fixture()->Box<SelectState> {new_select_state(vec![ffi::SelectOption{key:"Arc".into(),label:"Arc".into(),search:"arc".into()},ffi::SelectOption{key:"Google Chrome".into(),label:"Google Chrome".into(),search:"google chrome".into()},ffi::SelectOption{key:"Microsoft Edge".into(),label:"Microsoft Edge".into(),search:"microsoft edge".into()},ffi::SelectOption{key:"Brave".into(),label:"Brave".into(),search:"brave".into()}])}
    fn keyed_options(values:&[(&str,&str)])->Vec<ffi::SelectOption>{values.iter().map(|(key,label)|ffi::SelectOption{key:(*key).into(),label:(*label).into(),search:label.to_lowercase()}).collect()}
    #[test] fn select_none_cancels_old_popup_and_allows_first_item_prefix() {
        let mut s=select_fixture();let old=s.open(true);s.clear_selection();
        assert!(!s.has_selection());assert_eq!(s.selected_key(),"");assert!(!s.expanded());
        assert!(!s.choose(old,1,true));assert!(!s.choose_key(old,"Arc",true));
        assert!(!s.select_index(99));assert!(!s.has_selection());
        assert!(s.input("arc",1,true));assert_eq!(s.selected_key(),"Arc");
        s.clear_selection();assert!(s.input("g",2,true));assert_eq!(s.selected_key(),"Google Chrome");
        let menu=s.open(true);assert!(s.choose_key(menu,"Brave",true));assert_eq!(s.selected(),3);
    }
    #[test] fn select_none_reconciles_empty_disabled_and_nonmatching_input() {
        let mut s=select_fixture();s.clear_selection();assert!(!s.input("z",1,true));assert!(!s.has_selection());
        assert!(s.input("a",1002,true));assert_eq!(s.selected_key(),"Arc");
        s.clear_selection();assert!(!s.input("b",3,false));assert!(!s.has_selection());
        assert!(s.replace_options(vec![],false,false));s.clear_selection();assert_eq!(s.open(true),0);assert!(!s.input("a",4,true));
        assert!(s.replace_options(keyed_options(&[("a","A"),("b","B")]),true,false));assert!(s.has_selection());assert_eq!(s.selected_key(),"b");
    }
    #[test] fn select_model_reorder_and_rename_preserve_identity_with_duplicate_labels() {
        let mut s=new_select_state(keyed_options(&[("a","Same"),("b","Same"),("c","Other")]));assert!(s.select_index(1));assert!(s.replace_options(keyed_options(&[("c","Other"),("a","Same"),("b","Renamed 🦀")]),false,false));assert_eq!((s.selected(),s.selected_key()),(2,"b"));assert!(s.replace_options(keyed_options(&[("b","Renamed again"),("c","Other")]),false,false));assert_eq!((s.selected(),s.selected_key()),(0,"b"));
    }
    #[test] fn select_model_new_profile_policy_selects_last_while_other_updates_preserve() {
        let mut s=new_select_state(keyed_options(&[("a","A"),("b","B")]));assert!(s.replace_options(keyed_options(&[("a","A"),("b","B"),("c","C")]),true,true));assert_eq!(s.selected_key(),"c");assert!(s.replace_options(keyed_options(&[("c","C"),("d","D"),("a","A")]),true,false));assert_eq!(s.selected_key(),"c");
    }
    #[test] fn select_model_removed_selection_uses_first_or_last_fallback_and_empty_clears() {
        for last in [false,true]{let mut s=new_select_state(keyed_options(&[("a","A")]));s.replace_options(keyed_options(&[("b","B"),("c","C")]),last,false);assert_eq!(s.selected_key(),if last{"c"}else{"b"});assert!(s.replace_options(vec![],last,false));assert!(!s.has_selection());assert_eq!(s.selected_key(),"");assert_eq!(s.open(true),0);assert!(s.replace_options(keyed_options(&[("d","D")]),last,false));assert!(s.has_selection());assert_eq!(s.selected_key(),"d");}
    }
    #[test] fn select_model_invalid_empty_or_duplicate_keys_are_rejected_transactionally() {
        let mut s=new_select_state(keyed_options(&[("a","A"),("b","B")]));s.select_index(1);let t=s.open(true);assert!(!s.replace_options(keyed_options(&[("","Empty")]),false,false));assert!(!s.replace_options(keyed_options(&[("x","First"),("x","Second")]),false,false));assert_eq!(s.selected_key(),"b");assert!(s.can_choose(t,"a",true));assert_eq!(s.option_key(1),"b");assert_eq!(s.option_key(99),"");
    }
    #[test] fn select_model_replacement_rejects_captured_key_and_numeric_commands() {
        let mut s=new_select_state(keyed_options(&[("a","A"),("b","B")]));let old=s.open(true);assert!(s.replace_options(keyed_options(&[("b","B"),("a","A")]),false,false));assert!(!s.can_choose(old,"b",true));assert!(!s.choose_key(old,"b",true));assert!(!s.choose(old,1,true));let current=s.open(true);assert!(!s.choose_key(current,"missing",true));assert!(!s.choose_key(current,"b",false));assert!(s.choose_key(current,"b",true));assert_eq!((s.selected(),s.selected_key()),(0,"b"));assert!(!s.choose_key(current,"a",true));
    }
    #[test] fn select_old_close_callback_cannot_collapse_new_popup_or_choose_after_close() {
        let mut s=select_fixture();let old=s.open(true);let current=s.open(true);assert!(!s.close_menu(old));assert!(s.expanded());assert!(s.choose_key(current,"Brave",true));assert!(s.close_menu(current));assert!(!s.expanded());let next=s.open(true);assert!(!s.close_menu(current));assert!(s.can_choose(next,"Arc",true));assert!(s.close_menu(next));assert!(!s.choose_key(next,"Arc",true));assert!(!s.close_menu(next));
    }
    #[test] fn select_model_update_resets_prefix_and_keyed_same_value_dispatch_is_once() {
        let mut s=new_select_state(keyed_options(&[("a","Alpha"),("b","Beta")]));assert!(s.input("b",10,true));s.replace_options(keyed_options(&[("a","Alpha"),("b","Beta renamed")]),true,false);assert!(s.input("a",11,true));assert_eq!(s.selected_key(),"a");let t=s.open(true);assert!(s.choose_key(t,"a",true));assert!(!s.choose_key(t,"a",true));assert!(s.select_index(1));assert!(!s.choose_key(t,"a",true));
    }
    #[test] fn select_unicode_normalized_search_survives_rename_and_distinguishes_ids() {
        let mut s=new_select_state(keyed_options(&[("a","Łódź 🦀"),("b","日本語"),("c","Łódź 🦀")]));assert!(s.input("łó",10,true));assert_eq!(s.selected_key(),"c");s.replace_options(keyed_options(&[("c","日本語 🦀"),("a","Łódź 🦀")]),false,false);assert_eq!(s.selected_key(),"c");assert!(!s.input("日本",11,true));assert_eq!(s.selected_key(),"c");assert!(s.input("ł",1011,true));assert_eq!(s.selected_key(),"a");
    }
    #[test] fn select_menu_tokens_reject_old_closed_duplicate_and_disabled_commands() {
        let mut s=select_fixture();let first=s.open(true);assert!(s.expanded());let second=s.open(true);assert_ne!(first,second);assert!(!s.choose(first,1,true));assert!(!s.choose(second,1,false));assert!(!s.choose(second,4,true));assert_eq!(s.selected(),0);assert!(s.choose(second,1,true));assert!(!s.choose(second,2,true));assert_eq!(s.selected(),1);let t=s.open(true);s.dismiss();assert!(!s.expanded());assert!(!s.choose(t,0,true));assert_eq!(s.selected(),1);
    }
    #[test] fn select_empty_disabled_and_programmatic_values_are_bounded() {
        let mut empty=new_select_state(vec![]);assert_eq!(empty.open(true),0);assert!(!empty.input("a",0,true));assert!(!empty.select_index(0));let mut s=select_fixture();assert_eq!(s.open(false),0);assert!(!s.expanded());let t=s.open(true);assert!(s.select_index(3));assert!(!s.choose(t,1,true));assert!(!s.select_index(99));assert_eq!(s.selected(),3);
    }
    #[test] fn select_prefix_extends_within_timeout_and_restarts_after_one_second() {
        let mut s=select_fixture();assert!(s.input("g",100,true));assert_eq!(s.selected(),1);assert!(!s.input("oogle",101,true));assert!(!s.input("b",999,true));assert_eq!(s.selected(),1);assert!(s.input("b",1999,true));assert_eq!(s.selected(),3);s.reset_prefix();assert!(s.input("arc",2000,true));assert_eq!(s.selected(),0);
    }
    #[test] fn select_prefix_cycles_after_timeout_and_handles_clock_reset() {
        let mut s=new_select_state(vec![ffi::SelectOption{key:"A".into(),label:"A".into(),search:"a".into()},ffi::SelectOption{key:"Ab".into(),label:"Ab".into(),search:"ab".into()}]);assert!(s.input("a",5000,true));assert_eq!(s.selected(),1);assert!(s.input("a",6000,true));assert_eq!(s.selected(),0);assert!(s.input("a",10,true));assert_eq!(s.selected(),1);assert!(!s.input("a",11,true));assert_eq!(s.selected(),1);
    }
    #[test] fn select_prefix_ignores_controls_disabled_and_empty_without_poisoning_search() {
        let mut s=select_fixture();assert!(!s.input("g",0,false));assert!(!s.input("\t",0,true));assert!(!s.input("\r",0,true));assert!(!s.input("",0,true));assert!(s.input("g",10,true));assert!(!s.input("\n",11,true));assert!(!s.input("oogle",12,true));assert_eq!(s.selected(),1);
    }
    #[test] fn select_mac_keyboard_policy_opens_only_original_combobox_keys() {
        for key in [32,35,36,38,40]{assert!(select_menu_key(key));}for key in [0,9,13,27,37,39,65,115]{assert!(!select_menu_key(key));}
    }
    #[test] fn select_layout_mirrors_and_bounds_text_arrow_and_saturated_sizes() {
        let ltr=select_layout(300,30,0,0,false);let rtl=select_layout(300,30,0,0,true);assert_eq!((ltr.label.x,ltr.label.width,ltr.arrow.x),(12,252,272));assert_eq!((rtl.label.x,rtl.label.width,rtl.arrow.x),(36,252,12));assert_eq!(select_layout(-1,-1,100,20,false).height,28);assert_eq!(select_layout(-1,-1,i32::MAX,20,false).width,i32::MAX);
        for width in [0,1,10,30,300,i32::MAX]{for rtl in [false,true]{let g=select_layout(width,30,0,0,rtl);for r in [&g.label,&g.arrow]{assert!(r.x>=0&&r.width>=0&&i64::from(r.x)+i64::from(r.width)<=i64::from(width));}}}
    }
    #[test] fn select_recipe_uses_separate_text_arrow_clipping_and_no_empty_label_ink() {
        let mut item=row(300.0);item.kind=21;item.title="Google Chrome".into();item.focused=true;let ops=layout(std::slice::from_ref(&item));assert_eq!(ops.iter().filter(|p|!p.text.is_empty()).count(),2);assert!(ops.iter().any(|p|p.text=="▾"&&p.center));assert!(ops.iter().any(|p|p.text=="Google Chrome"&&!p.center&&p.width==252.0));assert_eq!(ops.last().unwrap().stroke,2.0);assert!(ops.iter().all(|p|p.clip.width<=item.clip.width));item.title.clear();assert_eq!(layout(std::slice::from_ref(&item)).iter().filter(|p|!p.text.is_empty()).count(),1);item.width=0.0;assert!(layout(&[item]).is_empty());
    }
    #[test] fn checkbox_state_programmatic_sets_are_idempotent_and_user_toggle_is_explicit() {
        let mut c=new_checkbox_state(false);assert!(!c.checked());assert!(!c.set_checked(false));assert!(c.set_checked(true));assert!(c.checked());assert!(!c.set_checked(true));assert!(c.toggle(true));assert!(!c.checked());
    }
    #[test] fn checkbox_state_disabled_activation_does_not_mutate_value() {
        for value in [false,true] {let mut c=new_checkbox_state(value);assert!(!c.toggle(false));assert_eq!(c.checked(),value);assert!(c.set_checked(!value));assert_eq!(c.checked(),!value);}
    }
    #[test] fn preferences_form_filters_all_categories_and_preserves_selection() {
        let mut f=new_preferences_form(3);let rows=vec![ffi::PreferenceRow{category:0,keywords:"wygląd motyw".encode_utf16().collect()},ffi::PreferenceRow{category:1,keywords:"języki witryn".encode_utf16().collect()},ffi::PreferenceRow{category:1,keywords:"języki pisownia".encode_utf16().collect()},ffi::PreferenceRow{category:99,keywords:vec![]}];
        let v=f.preference_filter(vec![],&rows);assert_eq!(v.rows,vec![true,false,false,false]);assert_eq!(v.navigation,vec![true,false,false]);assert!(v.any);
        let v=f.preference_filter("języki".encode_utf16().collect(),&rows);assert_eq!(v.rows,vec![false,true,true,false]);assert_eq!(v.sections,vec![false,true,false]);assert_eq!(v.navigation,vec![false,false,false]);assert_eq!(f.preference_current(),0);
        assert!(!f.preference_select(usize::MAX));assert_eq!(f.preference_current(),0);assert!(f.preference_select(2));let v=f.preference_filter(vec![],&rows);assert!(!v.any);assert_eq!(v.navigation,vec![false,false,true]);
    }
    #[test] fn preferences_form_matches_utf16_units_and_updated_rule_keywords() {
        let mut f=new_preferences_form(1);let mut rows=vec![ffi::PreferenceRow{category:0,keywords:vec![0xd83e,0xdd80,0xfffd]}];
        assert!(f.preference_filter(vec![0xd83e],&rows).any);assert!(!f.preference_filter(vec![0xd800],&rows).any);assert!(!f.preference_filter(vec![1,2,3,4],&rows).any);rows[0].keywords="nowy wyjątek".encode_utf16().collect();assert!(f.preference_filter("nowy".encode_utf16().collect(),&rows).any);
        let mut empty=new_preferences_form(0);assert!(!empty.preference_select(0));assert!(!empty.preference_filter(vec![],&rows).any);
    }
    fn timeline_item(id:&str,title:&[u16],detail:&[u16],search:&[u16])->ffi::ToolsTimelineItem{ffi::ToolsTimelineItem{id:id.into(),title:title.to_vec(),detail:detail.to_vec(),search:search.to_vec()}}
    fn timeline_text(id:&str,title:&str,detail:&str)->ffi::ToolsTimelineItem{timeline_item(id,&title.encode_utf16().collect::<Vec<_>>(),&detail.encode_utf16().collect::<Vec<_>>(),&format!("{} {}",title,detail).to_lowercase().encode_utf16().collect::<Vec<_>>())}
    #[test] fn tools_timeline_projection_preserves_raw_utf16_and_native_search_join(){
        let mut f=new_tools_form();let title=[0x41,0xd83e,0xdd80,0xd800];let detail=[0x42,0xdfff];let search=[0x61,0xd83e,0xdd80,0xd800,0x20,0x62,0xdfff];
        f.tools_timeline_edit(vec![0xd83e]);let p=f.tools_timeline_project(false,vec![timeline_item("opaque",&title,&detail,&search)]);assert!(p.enabled);assert_eq!(p.choices.len(),1);let mut label=title.to_vec();label.extend(" — ".encode_utf16());label.extend(detail);assert_eq!(p.choices[0].label,label);assert_eq!(f.tools_timeline_target(false,&p.choices[0].key),"opaque");
        f.tools_timeline_edit(" — ".encode_utf16().collect());let p=f.tools_timeline_project(false,vec![timeline_text("a","Alpha","Beta")]);assert!(!p.enabled);assert_eq!(String::from_utf16(&p.choices[0].label).unwrap(),"Brak zapisanych pozycji");assert!(f.tools_timeline_target(false,&p.choices[0].key).is_empty());
        f.tools_timeline_edit("a b".encode_utf16().collect());let p=f.tools_timeline_project(false,vec![timeline_text("a","Alpha","Beta")]);assert!(p.enabled);
        // Search is already normalized by ICU: Rust must not fold, trim or
        // reconstruct it using the display em dash or another Unicode policy.
        f.tools_timeline_edit(vec![0x69,0x307]);let p=f.tools_timeline_project(false,vec![timeline_item("icu",&[0x130],&[],&[0x69,0x307,0x20])]);assert!(p.enabled);
    }
    #[test] fn tools_timeline_keys_reject_stale_reordered_reset_and_cross_group_targets(){
        let mut f=new_tools_form();let a=f.tools_timeline_project(false,vec![timeline_text("a","Same",""),timeline_text("b","Same","")]);let bkey=a.choices[1].key.clone();assert_ne!(a.choices[0].key,bkey);assert_eq!(f.tools_timeline_target(false,&bkey),"b");assert!(f.tools_timeline_target(true,&bkey).is_empty());
        let b=f.tools_timeline_project(false,vec![timeline_text("b","Renamed",""),timeline_text("a","Same","")]);assert!(f.tools_timeline_target(false,&bkey).is_empty());assert_eq!(f.tools_timeline_target(false,&b.choices[0].key),"b");
        let key=b.choices[0].key.clone();f.tools_timeline_edit(vec![]);assert!(f.tools_timeline_target(false,&key).is_empty());
        let c=f.tools_timeline_project(false,vec![timeline_text("b","Renamed","")]);let key=c.choices[0].key.clone();f.tools_select(3);f.tools_select(0);assert!(f.tools_timeline_target(false,&key).is_empty());
        let c=f.tools_timeline_project(false,vec![timeline_text("b","Renamed","")]);let key=c.choices[0].key.clone();f.tools_reset();assert!(f.tools_timeline_target(false,&key).is_empty());let d=f.tools_timeline_project(false,vec![timeline_text("b","Renamed","")]);assert_ne!(key,d.choices[0].key);
    }
    #[test] fn tools_timeline_lists_are_independent_and_duplicate_ids_have_distinct_ui_keys(){
        let mut f=new_tools_form();let closed=f.tools_timeline_project(true,vec![timeline_text("same-id","Same",""),timeline_text("same-id","Same","")]);assert_ne!(closed.choices[0].key,closed.choices[1].key);let key=closed.choices[1].key.clone();
        f.tools_timeline_project(false,vec![timeline_text("snapshot","Same","")]);assert_eq!(f.tools_timeline_target(true,&key),"same-id");
        let empty=f.tools_timeline_project(true,vec![]);assert!(!empty.enabled);assert_eq!(empty.choices.len(),1);assert!(f.tools_timeline_target(true,&key).is_empty());assert!(f.tools_timeline_target(false,"empty").is_empty());
        let mut f=new_tools_form();let p=f.tools_timeline_project(false,vec![timeline_text("","Visible but unusable","")]);assert!(p.enabled);assert!(f.tools_timeline_target(false,&p.choices[0].key).is_empty());
    }
    #[test] fn tools_timeline_epoch_exhaustion_fails_closed_without_reusing_keys(){
        let mut f=new_tools_form();let p=f.tools_timeline_project(false,vec![timeline_text("a","A","")]);let key=p.choices[0].key.clone();f.timeline_epoch=u64::MAX;let p=f.tools_timeline_project(false,vec![timeline_text("b","B","")]);assert!(!p.enabled);assert!(f.tools_timeline_target(false,&key).is_empty());f.tools_reset();let p=f.tools_timeline_project(false,vec![timeline_text("b","B","")]);assert!(!p.enabled);
    }
    fn form_shortcut(id:&str,title:&str,chord:&str)->ffi::ToolsShortcutItem{ffi::ToolsShortcutItem{id:id.into(),title:title.encode_utf16().collect(),chord:chord.encode_utf16().collect()}}
    fn form_provider(id:&str,name:&str,default_provider:bool)->ffi::ToolsProviderItem{ffi::ToolsProviderItem{id:id.into(),name:name.encode_utf16().collect(),keyword:id.encode_utf16().collect(),url:format!("https://{}.test/?q={{query}}",id).encode_utf16().collect(),default_provider}}
    #[test] fn tools_shortcut_projection_and_selection_own_labels_chords_and_record_cancellation(){
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![form_shortcut("a","Same 🦀","Cmd+J"),form_shortcut("b","Same 🦀","")]);assert!(p.enabled);assert_eq!(String::from_utf16(&p.choices[0].label).unwrap(),"Same 🦀 — Cmd+J");assert_ne!(p.choices[0].key,p.choices[1].key);
        let first=f.tools_choice_draft(&p.choices[0].key);assert!(first.valid&&first.stop_recording);assert_eq!(first.target,"a");assert_eq!(String::from_utf16(&first.chord).unwrap(),"Cmd+J");
        f.tools_record(true);let draft=f.tools_choice_select(&p.choices[1].key);assert!(draft.valid);assert_eq!(draft.target,"b");assert!(draft.chord.is_empty());assert!(!f.tools_recording());assert!(!f.tools_choice_draft(&p.choices[0].key).valid);
    }
    #[test] fn tools_provider_projection_create_and_default_markers_preserve_native_fields(){
        let mut f=new_tools_form();f.tools_select(2);let p=f.tools_provider_choices(vec![form_provider("a","Same 🦀",false),form_provider("b","Same 🦀",true)]);assert!(p.enabled);assert_eq!(p.choices.len(),3);assert_eq!(String::from_utf16(&p.choices[0].label).unwrap(),tools_copy(2,"").choices[0]);assert_eq!(String::from_utf16(&p.choices[2].label).unwrap(),"b · Same 🦀 · domyślna");
        let create=f.tools_choice_draft(&p.choices[0].key);assert!(create.valid&&create.create);assert!(create.target.is_empty()&&create.name.is_empty()&&create.keyword.is_empty()&&create.url.is_empty());
        let draft=f.tools_choice_select(&p.choices[2].key);assert!(draft.valid&&!draft.create&&!draft.stop_recording);assert_eq!(draft.target,"b");assert_eq!(String::from_utf16(&draft.name).unwrap(),"Same 🦀");assert_eq!(String::from_utf16(&draft.keyword).unwrap(),"b");assert_eq!(String::from_utf16(&draft.url).unwrap(),"https://b.test/?q={query}");assert!(!f.tools_choice_draft(&p.choices[0].key).valid);
    }
    #[test] fn tools_choice_reprojection_reset_page_and_wrong_role_invalidate_old_drafts(){
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![form_shortcut("a","A","Cmd+J")]);let key=p.choices[0].key.clone();let p=f.tools_shortcut_choices(vec![form_shortcut("a","Renamed","Cmd+K")]);assert!(!f.tools_choice_draft(&key).valid);assert!(f.tools_choice_draft(&p.choices[0].key).valid);let key=p.choices[0].key.clone();f.tools_reset();assert!(!f.tools_choice_draft(&key).valid);
        let p=f.tools_shortcut_choices(vec![form_shortcut("a","A","")]);let key=p.choices[0].key.clone();f.tools_select(2);let p=f.tools_provider_choices(vec![]);assert!(!f.tools_choice_select(&key).valid);assert!(f.tools_choice_draft(&p.choices[0].key).valid);f.tools_select(1);assert!(!f.tools_choice_select(&key).valid);
    }
    #[test] fn tools_choice_raw_utf16_empty_ids_and_duplicate_ids_are_not_control_keys(){
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![ffi::ToolsShortcutItem{id:"a".into(),title:vec![0xd800],chord:vec![0xdfff]}]);assert_eq!(p.choices[0].label,vec![0xd800,0x20,0x2014,0x20,0xdfff]);assert_eq!(f.tools_choice_draft(&p.choices[0].key).chord,vec![0xdfff]);
        let p=f.tools_shortcut_choices(vec![form_shortcut("same","Same",""),form_shortcut("same","Same","")]);assert_ne!(p.choices[0].key,p.choices[1].key);assert_eq!(f.tools_choice_select(&p.choices[1].key).target,"same");
        let p=f.tools_shortcut_choices(vec![form_shortcut("","Invalid","")]);assert!(!f.tools_choice_draft(&p.choices[0].key).valid);let p=f.tools_shortcut_choices(vec![]);assert!(!p.enabled);assert!(!f.tools_choice_select(&p.choices[0].key).valid);
    }
    #[test] fn tools_choice_epoch_exhaustion_does_not_resurrect_old_or_create_targets(){
        let mut f=new_tools_form();f.tools_select(2);let p=f.tools_provider_choices(vec![]);let key=p.choices[0].key.clone();f.choice_epoch=u64::MAX;let p=f.tools_provider_choices(vec![form_provider("a","A",false)]);assert!(!p.enabled);assert!(!f.tools_choice_draft(&key).valid);f.tools_reset();let p=f.tools_provider_choices(vec![]);assert!(!p.enabled);assert!(!f.tools_choice_select(&p.choices[0].key).valid);
    }
    #[test] fn tools_provider_dispatch_requires_current_choice_as_well_as_native_fields(){
        let mut f=new_tools_form();let mut c=tools_context();c.provider_selected=false;for action in [20,21,22]{assert_eq!(f.tools_dispatch(action,&c).action,0);}c.provider_selected=true;for action in [20,21,22]{assert_eq!(f.tools_dispatch(action,&c).action,action);}c.provider_composing=true;assert!(!f.tools_dispatch(20,&c).admitted);
    }
    fn tools_context()->ffi::ToolsActionContext {ffi::ToolsActionContext{snapshot_field:true,shortcut_field:true,provider_field:true,provider_selected:true,snapshot_composing:false,shortcut_composing:false,provider_composing:false,snapshot_selected:true,closed_selected:true,shortcut_selected:true}}
    #[test] fn tools_dispatch_preserves_command_and_composition_admission() {
        let mut form=new_tools_form();let mut c=tools_context();
        for action in [1,2,3,4,5,10,11,12,13,20,21,22] {let p=form.tools_dispatch(action,&c);assert!(p.admitted);assert_eq!(p.action,action);}
        form.tools_record(true);c.snapshot_composing=true;c.shortcut_composing=true;c.provider_composing=true;
        for action in [1,11,12,20,21,22] {let p=form.tools_dispatch(action,&c);assert!(!p.admitted);assert!(!p.sync_recording);assert!(form.tools_recording());}
        // Record cancels preedit natively; restore/reset remain admitted as in Views.
        for action in [2,3,4,5,10,13] {assert!(form.tools_dispatch(action,&c).admitted);}
        c=tools_context();c.snapshot_field=false;c.shortcut_field=false;c.provider_field=false;c.snapshot_selected=false;c.closed_selected=false;c.shortcut_selected=false;
        for action in [1,2,3,4,10,11,12,20,21,22,-1,99] {assert_eq!(form.tools_dispatch(action,&c).action,0);}
    }
    #[test] fn tools_dispatch_recording_and_outcomes_follow_native_flow() {
        let mut form=new_tools_form();let c=tools_context();
        for _ in 0..2 {let p=form.tools_dispatch(10,&c);assert!(p.recording&&p.sync_recording);assert!(!p.record_status.is_empty());}
        let p=form.tools_dispatch(99,&c);assert!(p.admitted&&p.sync_recording);assert!(!p.recording);assert_eq!(p.action,0);
        assert!(!form.tools_dispatch(11,&c).sync_recording);
        for page in 0..4 {form.tools_select(page);let failed=form.tools_outcome(false,true);assert!(!failed.refresh);assert!(!failed.status.is_empty());assert_eq!(form.tools_outcome(true,false).status,"Gotowe. Zmiany zostały zastosowane.");assert!(form.tools_outcome(true,true).refresh);assert!(form.tools_outcome(true,true).status.contains("Oczekiwanie"));}
        form.tools_select(2);assert!(form.tools_outcome(false,false).status.contains("{query}"));form.tools_select(1);assert!(form.tools_outcome(false,false).status.contains("kombinacja"));
    }
    #[test] fn tools_history_presentation_preserves_errors_counts_and_busy_controls() {
        let mut form=new_tools_form();assert!(form.tools_history_view(0,false,false).empty.contains("w tym profilu"));
        form.tools_history_edit(vec![0xd800]);assert!(form.tools_history_view(0,false,false).empty.contains("wyszukiwania"));
        for count in [0,1,200,usize::MAX] {for error in [false,true] {for more in [false,true] {let v=form.tools_history_view(count,more,error);assert_eq!(v.more_visible,more&&!error);assert_eq!(v.more_enabled,!error);assert_eq!(v.empty.is_empty(),count!=0);if error{assert!(v.status.contains("ponownie"));}else{assert_eq!(v.status,format!("Wyświetlono wpisów: {}",count));}}}}
        for busy in [false,true] {let c=tools_clear_controls(busy);assert_eq!(c.enabled,!busy);assert_eq!(c.label,tools_clear_label(busy));}
    }
    #[test] fn page_dialog_copy_preserves_roles_and_utf16_cap_without_authority() {
        for kind in 0..3 {for unload in [false,true] {let copy=page_dialog_copy(kind,unload,"https://example.test","Profil 🦀 אבג",&[0xd800,0x41]);assert_eq!(copy.show_cancel,kind!=0);assert_eq!(copy.prompt,kind==2);assert_eq!(copy.default_cancel,unload);assert_eq!(copy.identity,"Profil: Profil 🦀 אבג");assert!(copy.title.ends_with("https://example.test"));assert_eq!(copy.message,vec![0xd800,0x41]);assert_eq!(copy.accept,if unload{"Opuść stronę"}else{"OK"});assert_eq!(copy.cancel,if unload{"Zostań"}else{"Anuluj"});}}
        let mut long=vec![0x41;8191];long.extend([0xd83e,0xdd80]);let copy=page_dialog_copy(2,false,"null","",&long);assert_eq!(copy.message.len(),8192);assert_eq!(copy.message.last(),Some(&0xd83e));assert!(page_dialog_copy(0,false,"","",&[]).message.is_empty());
    }
    #[test] fn static_copy_centering_is_explicit_and_keeps_existing_left_alignment() {
        let mut item=row(440.0);item.kind=17;item.glyph=1;item.font_weight=400;
        let center=layout(std::slice::from_ref(&item));assert_eq!(center.len(),1);assert!(center[0].center);assert_eq!(center[0].font_weight,400);
        item.glyph=0;let left=layout(&[item]);assert!(!left[0].center);
    }
    #[test] fn tools_form_committed_queries_paging_and_stale_results() {
        let mut form = new_tools_form(); form.tools_select(3);
        let query = vec![0x41, 0xd83e, 0xdd80, 0xd800, 0x5d0];
        form.tools_history_edit(query.clone());
        assert_eq!(form.tools_history_query(), query);
        assert_eq!(form.tools_history_request(false), 200);
        assert!(!form.tools_history_accept(&[0x41], 200));
        assert!(!form.tools_history_accept(&query, 400));
        assert!(!form.tools_history_loaded());
        assert!(form.tools_history_accept(&query, 200));
        assert!(form.tools_history_loaded());
        assert_eq!(form.tools_history_request(true), 400);
        assert!(!form.tools_history_loaded());
        assert!(!form.tools_history_accept(&query, 200));
        assert!(form.tools_history_accept(&query, 400));
        form.tools_history_edit(vec![]);
        assert!(!form.tools_history_loaded());
        assert!(!form.tools_history_accept(&query, 400));
        assert_eq!(form.tools_history_request(false), 200);
        assert!(form.tools_history_accept(&[], 200));
    }
    #[test] fn tools_form_rebuild_clears_transient_state_and_preserves_page() {
        let mut form = new_tools_form(); form.tools_select(1); form.tools_record(true);
        form.tools_history_edit(vec![0xd800]); form.tools_timeline_edit(vec![0x5d0, 0xdfff]);
        assert_eq!(form.tools_timeline_query(), vec![0x5d0, 0xdfff]);
        assert_eq!(form.tools_history_request(true), 400);
        form.tools_reset();
        assert_eq!(form.tools_page(), 1); assert!(!form.tools_recording());
        assert!(form.tools_history_query().is_empty()); assert!(form.tools_timeline_query().is_empty());
        assert!(!form.tools_history_loaded());
        assert!(!form.tools_history_accept(&[], 200));
        assert_eq!(form.tools_history_request(false), 200);
        form.tools_select(usize::MAX); assert_eq!(form.tools_page(), 3);
        form.tools_select(0); assert_eq!(form.tools_page(), 0);
    }
    #[test] fn tools_form_pagination_saturates_instead_of_wrapping() {
        let mut form = new_tools_form(); form.history_limit = usize::MAX - 100;
        assert_eq!(form.tools_history_request(true), usize::MAX);
        assert_eq!(form.tools_history_request(true), usize::MAX);
        assert_eq!(form.tools_history_request(false), 200);
    }
    #[test] fn tools_form_order_preserves_native_control_and_command_sequence() {
        use ffi::ToolsNodeKind as K;use ffi::ToolsNodeRole as R;
        let roles=[vec![R::SnapshotName,R::TimelineFilter,R::SnapshotChoice,R::ClosedChoice],vec![R::ShortcutChoice,R::ShortcutChord],vec![R::ProviderChoice,R::ProviderName,R::ProviderKeyword,R::ProviderUrl],vec![R::HistoryQuery,R::HistoryPeriod]];
        let actions=[vec![1,2,3,4,5],vec![10,11,12,13],vec![20,21,22],vec![30,31]];
        for page in 0..4 {let mut f=new_tools_form();f.tools_select(page);let plan=f.tools_form_plan("Profil",false,vec![]);
            assert!(plan.nodes.iter().filter(|n|n.kind==K::Editor||n.kind==K::Choice).map(|n|n.role).eq(roles[page].iter().copied()));
            assert_eq!(plan.nodes.iter().filter(|n|n.kind==K::Action).map(|n|n.action).collect::<Vec<_>>(),actions[page]);
            assert!(plan.nodes.iter().filter(|n|n.kind==K::Editor||n.kind==K::Choice||n.kind==K::Action).all(|n|!n.text.is_empty()));
        }
    }
    #[test] fn tools_form_groups_preserve_native_gaps_and_history_flex() {
        use ffi::ToolsNodeKind as K;use ffi::ToolsNodeRole as R;
        for (page,expected) in [4,1,1,0].into_iter().enumerate(){let mut f=new_tools_form();f.tools_select(page);let plan=f.tools_form_plan("",false,vec![]);let mut stack=vec![];let mut groups=0;
            for n in &plan.nodes {match n.kind {K::BeginActions=>{assert_eq!(n.spacing,10);groups+=1;stack.push(K::EndActions);},K::BeginRow=>{assert_eq!(n.spacing,8);stack.push(K::EndRow);},K::EndActions|K::EndRow=>assert!(stack.pop()==Some(n.kind)),_=>{}}
                if n.role==R::HistoryPeriod{assert!(n.flex);assert!(stack.last()==Some(&K::EndRow));}else{assert!(!n.flex);}if n.role==R::HistoryRows{assert_eq!(n.spacing,4);assert!(stack.is_empty());}
            }assert!(stack.is_empty());assert_eq!(groups,expected);
        }
    }
    #[test] fn tools_form_history_reconstruction_respects_busy_and_hidden_more() {
        use ffi::ToolsNodeRole as R;let mut f=new_tools_form();f.tools_select(3);
        for busy in [false,true]{let p=f.tools_form_plan("Osobisty",busy,vec![]);let clear=p.nodes.iter().find(|n|n.role==R::HistoryClear).unwrap();assert_eq!(clear.enabled,!busy);assert_eq!(String::from_utf16(&clear.text).unwrap(),if busy{"Czyszczenie…"}else{"Wyczyść historię…"});assert!(!p.nodes.iter().find(|n|n.role==R::HistoryMore).unwrap().visible);assert_eq!(p.nodes.iter().find(|n|n.role==R::HistoryPeriod).unwrap().choices,["Ostatnia godzina","Ostatnie 24 godziny","Ostatnie 7 dni","Cały okres"]);}
    }
    #[test] fn tools_form_context_and_sleeping_copy_preserve_utf16() {
        use ffi::ToolsNodeKind as K;let f=new_tools_form();let p=f.tools_form_plan("Praca 🦀 אבג",false,vec![ffi::ToolsSleepingCopy{title:vec![0xd800],detail:vec![0xdfff]}]);assert_eq!(String::from_utf16(&p.title).unwrap(),"Time Machine · Praca 🦀 אבג");let last=p.nodes.last().unwrap();assert!(last.kind==K::Hint);assert_eq!(last.text,vec![0xd800,0x20,0xb7,0x20,0xdfff]);assert!(p.nodes[p.nodes.len()-2].kind==K::EndActions);
        let mut f=new_tools_form();f.tools_select(3);let p=f.tools_form_plan("Profil 🦀 אבג",false,vec![]);assert_eq!(String::from_utf16(&p.nodes[0].text).unwrap(),"Profil: Profil 🦀 אבג");
    }
    #[test] fn tools_form_description_does_not_reset_queries_recording_or_choice_keys() {
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![form_shortcut("a","A","Cmd+J")]);let key=p.choices[0].key.clone();f.tools_record(true);f.tools_timeline_edit(vec![0xd800]);f.tools_history_edit(vec![0xdfff]);let epoch=f.choice_epoch;
        for _ in 0..2 {let _=f.tools_form_plan("",false,vec![]);assert!(f.tools_recording());assert_eq!(f.tools_timeline_query(),vec![0xd800]);assert_eq!(f.history_query,vec![0xdfff]);assert_eq!(f.choice_epoch,epoch);let draft=f.tools_choice_draft(&key);assert!(draft.valid);assert_eq!(draft.target,"a");}
    }
    #[test] fn tools_copy_display_context_and_adapter_roles_are_complete() {
        let copy=tools_copy(0,"Praca 🦀 אבג");assert_eq!(copy.title,"Time Machine · Praca 🦀 אבג");assert_eq!(copy.fields.len(),2);assert_eq!(copy.pickers.len(),2);assert_eq!(copy.headings.len(),2);assert_eq!(copy.actions.len(),5);assert!(copy.tail.contains("Niezapisane formularze"));
        let shortcut=tools_copy(1,"");assert_eq!(shortcut.fields.len(),1);assert_eq!(shortcut.actions.len(),4);assert!(shortcut.intro.contains("Escape"));
        let provider=tools_copy(2,"");assert_eq!(provider.fields.len(),3);assert_eq!(provider.actions.len(),3);assert!(provider.intro.contains("{query}"));
        let history=tools_copy(usize::MAX,"Profil 🦀");assert_eq!(history.intro,"Profil: Profil 🦀");assert_eq!(history.fields.len(),1);assert_eq!(history.choices.len(),4);assert_eq!(history.actions[0],tools_clear_label(false));assert_eq!(tools_clear_label(true),"Czyszczenie…");
        for page in 0..4{let copy=tools_copy(page,"");assert!(!copy.title.is_empty());assert!(!copy.initial_status.is_empty());assert!(copy.fields.iter().chain(copy.actions.iter()).all(|s|!s.is_empty()));}
    }
    #[test] fn preference_catalogue_preserves_categories_counts_and_platform_gate() {
        use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;
        let c=preference_catalogue(true);assert_eq!(c.categories,["Wygląd","Wyszukiwanie","Prywatność","Uprawnienia stron","Treści i czytelność","Hasła i profile","Pobieranie","Języki i pisownia","Dane przeglądania","Zaawansowane"]);assert_eq!(c.descriptions.len(),10);assert_eq!(c.rows.len(),52);
        let mut counts=[0;10];for row in &c.rows{counts[row.category]+=1;assert!(!row.title.is_empty()&&!row.detail.is_empty());}assert_eq!(counts,[5,4,7,7,12,5,2,3,1,6]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Toggle||r.kind==K::Pinned||r.kind==K::Animations).count(),24);assert_eq!(c.rows.iter().filter(|r|r.kind==K::Text).count(),6);
        let no=preference_catalogue(false);assert_eq!(no.rows.len(),51);assert!(no.rows.iter().all(|r|r.action!=A::Certificates));assert!(c.rows.iter().filter(|r|r.action!=A::Certificates).map(|r|(&r.title,&r.detail,r.category)).eq(no.rows.iter().map(|r|(&r.title,&r.detail,r.category))));
    }
    #[test] fn preference_catalogue_registered_store_keys_keep_their_control_kinds() {
        use ffi::PreferenceRowKind as K;let c=preference_catalogue(true);let mut keys=std::collections::BTreeSet::new();
        for row in &c.rows{if row.kind==K::Text||row.kind==K::Toggle||row.kind==K::Picker{assert!(!row.key.is_empty());assert!(keys.insert(row.key.clone()));}else{assert!(row.key.is_empty());}}
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Toggle).map(|r|r.key.as_str()).collect::<Vec<_>>(),["referrers","https_only","third_party_cookies","do_not_track","preconnect","link_auditing","camera","microphone","location","notifications","clipboard","javascript","images","popups","sound","save_passwords","auto_signin","download_prompt","spellcheck","online_spellcheck","caret_browsing","hardware_acceleration"]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Text).map(|r|r.key.as_str()).collect::<Vec<_>>(),["font_standard","font_serif","font_sans","font_fixed","accept_languages","dns_templates"]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Picker).map(|r|r.key.as_str()).collect::<Vec<_>>(),["zoom","font_size","minimum_font_size","fixed_font_size","dns_mode"]);
        for key in ["hardware_acceleration","dns_mode","dns_templates"]{let row=c.rows.iter().find(|r|r.key==key).unwrap();assert!(row.detail.contains("całej aplikacji"));}
    }
    #[test] fn preference_catalogue_editor_and_action_accessibility_retains_native_copy() {
        use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;let c=preference_catalogue(true);let mut actions=vec![];
        for row in &c.rows{match row.kind{K::Text=>{assert!(!row.placeholder.is_empty());assert_eq!(row.button,"Zapisz");assert_eq!(row.button_ax,format!("Zapisz: {}",row.title));assert!(row.action==A::None);},K::Action=>{assert_eq!(row.button,"Otwórz →");assert_eq!(row.button_ax,row.title);assert!(row.action!=A::None);assert!(!actions.contains(&row.action));actions.push(row.action);},K::Directory=>{assert_eq!(row.button,"Zmień…");assert_eq!(row.button_ax,"Zmień folder pobierania");},_=>{assert!(row.button.is_empty());assert!(row.action==A::None);}}}assert_eq!(actions.len(),10);
        assert_eq!(c.site.origin_ax,"Adres witryny dla wyjątku");assert_eq!(c.site.placeholder,"https://example.com");assert_eq!(c.site.save,"Zapisz wyjątek");assert_eq!(c.clear.checks,["Historia przeglądania","Cookies i dane witryn (może wylogować)","Pamięć podręczna"]);assert_eq!(c.clear.action,"Wyczyść…");
    }
    #[test] fn preference_catalogue_choice_indices_match_native_value_and_permission_order() {
        let c=preference_catalogue(true);let choices=|key:&str|c.rows.iter().find(|r|r.key==key).unwrap().choices.clone();
        assert_eq!(choices("zoom"),["75%","90%","100%","110%","125%","150%","175%","200%"]);assert_eq!(choices("font_size"),["12 px","14 px","16 px","18 px","20 px","24 px"]);assert_eq!(choices("fixed_font_size"),choices("font_size"));assert_eq!(choices("minimum_font_size"),["Bez limitu","10 px","12 px","14 px","16 px","18 px"]);assert_eq!(choices("dns_mode"),["Systemowy","Automatyczny","Ścisły (DoH)"]);
        assert_eq!(c.site.types,["JavaScript","Obrazy","Wyskakujące okna","Dźwięk","Powiadomienia","Lokalizacja","Kamera","Mikrofon","Schowek"]);for i in 0..9{let p=preference_site_policies(i);assert_eq!(&p[..3],["Użyj domyślnej","Zezwalaj","Blokuj"]);assert_eq!(p.len(),if i<4{3}else{4});if i>=4{assert_eq!(p[3],"Pytaj");}}
        assert_eq!(c.clear.periods,["Ostatnia godzina","Ostatnie 24 godziny","Ostatnie 7 dni","Cały okres"]);
    }
    #[test] fn preference_draft_refresh_policy_truth_table() {
        for dirty in [false,true]{for present in [false,true]{for editable in [false,true]{for composing in [false,true]{for profile_changed in [false,true]{
            let mut f=new_preferences_form(10);if dirty{f.preference_edited("font_standard");}
            let expected=present&&((!dirty&&!composing)||!editable||profile_changed);
            assert_eq!(f.preference_sync_text("font_standard",present,editable,composing,profile_changed),expected);
            assert_eq!(f.dirty.contains("font_standard"),dirty&&!expected);
            assert_eq!(f.preference_can_save(present&&editable,composing),present&&editable&&!composing);
        }}}}}
    }
    #[test] fn preference_failed_save_and_profile_lifecycle_preserve_original_drafts() {
        let mut f=new_preferences_form(10);assert!(f.preference_profile("work-profile"));f.preference_select(7);
        f.preference_edited("accept_languages");f.preference_edited("font_sans");f.preference_saved("accept_languages",false);
        assert!(!f.preference_sync_text("accept_languages",true,true,false,false));
        f.preference_saved("font_sans",true);assert!(f.preference_sync_text("font_sans",true,true,false,false));
        f.preference_more_rules();assert_eq!(f.preference_rule_limit(),100);assert!(!f.preference_profile("work-profile"));
        assert!(f.dirty.contains("accept_languages"));assert_eq!(f.preference_rule_limit(),100);
        assert!(f.preference_profile("personal-profile"));assert!(f.dirty.is_empty());assert_eq!(f.preference_rule_limit(),50);assert_eq!(f.preference_current(),7);
        f.preference_edited("font_fixed");f.preference_detach();assert!(f.dirty.is_empty());assert!(f.preference_profile("personal-profile"));assert_eq!(f.preference_current(),7);
    }
    #[test] fn preference_rule_limit_resets_and_does_not_wrap() {
        let mut f=new_preferences_form(10);f.rule_limit=usize::MAX-20;f.preference_more_rules();assert_eq!(f.preference_rule_limit(),usize::MAX);f.preference_reset_rules();assert_eq!(f.preference_rule_limit(),50);
    }
    #[test] fn preference_chrome_holes_partition_and_preserve_text_weight() {
        let mut item=row(100.0);item.kind=24;item.height=100.0;item.clip.height=100.0;item.active=true;
        let hole=ffi::FloatRect{x:20.0,y:20.0,width:40.0,height:40.0};let ops=preference_chrome_scene(std::slice::from_ref(&item),&[hole]);assert_eq!(ops.len(),4);assert_eq!(ops.iter().map(|p|p.clip.width*p.clip.height).sum::<f32>(),8400.0);
        assert!(ops.iter().all(|p|p.clip.x+p.clip.width<=20.0||p.clip.x>=60.0||p.clip.y+p.clip.height<=20.0||p.clip.y>=60.0));
        item.kind=17;item.font_weight=600;let text=preference_chrome_scene(std::slice::from_ref(&item),&[]);assert_eq!(text[0].font_weight,600);assert_eq!(text[0].text,item.title);
    }
    #[test] fn preference_chrome_overlapping_invalid_and_covering_holes() {
        let mut item=row(100.0);item.kind=24;item.height=100.0;item.clip.height=100.0;item.active=true;
        let ops=preference_chrome_scene(std::slice::from_ref(&item),&[ffi::FloatRect{x:0.0,y:0.0,width:60.0,height:100.0},ffi::FloatRect{x:40.0,y:0.0,width:40.0,height:100.0},ffi::FloatRect{x:f32::NAN,y:0.0,width:10.0,height:10.0}]);assert_eq!(ops.iter().map(|p|p.clip.width*p.clip.height).sum::<f32>(),2000.0);
        assert!(preference_chrome_scene(std::slice::from_ref(&item),&[ffi::FloatRect{x:-1.0,y:-1.0,width:102.0,height:102.0}]).is_empty());
        item.active=false;let ops=layout(std::slice::from_ref(&item));assert_eq!(ops.len(),2);assert_eq!(ops[1].stroke,1.0);assert_eq!(ops[0].radius,12.0);
    }
    #[test] fn preference_category_and_dismiss_share_clipped_cpu_gpu_recipe() {
        for glyph in 0..10{let mut i=row(180.0);i.kind=22;i.glyph=glyph;i.title="Uprawnienia stron".into();i.active=true;i.focused=true;let ops=layout(std::slice::from_ref(&i));assert!(ops.iter().any(|p|p.text=="Uprawnienia stron"&&p.bold));assert!(ops.iter().any(|p|p.stroke>0.0));assert!(ops.iter().all(|p|p.shape==0&&p.clip.width==180.0&&p.width>0.0&&p.height>0.0));i.tile=true;assert_eq!(layout(std::slice::from_ref(&i)).iter().find(|p|!p.text.is_empty()).unwrap().x,i.x+10.0);}
        let mut close=row(32.0);close.kind=23;close.height=32.0;close.fill=0;close.title.clear();let ops=layout(std::slice::from_ref(&close));assert_eq!(ops.len(),22);assert!(ops.iter().all(|p|p.text.is_empty()&&p.shape==0));close.width=f32::NAN;assert!(layout(std::slice::from_ref(&close)).is_empty());
    }
    #[test] fn toggle_transition_reverses_and_programmatic_refresh_snaps() {
        let mut s=new_toggle_state();assert!(!s.toggle_flip(false,10,true));assert!(!s.toggle_checked());
        assert!(s.toggle_flip(true,10,true));assert!(s.toggle_checked());assert_eq!(s.toggle_progress(50),0.5);
        assert!(s.toggle_flip(true,50,true));assert!(!s.toggle_checked());assert_eq!(s.toggle_progress(90),0.25);assert_eq!(s.toggle_progress(130),0.0);assert!(!s.toggle_animating(130));
        s.toggle_flip(true,200,true);s.toggle_set(true);assert_eq!(s.toggle_progress(201),1.0);assert!(!s.toggle_animating(201));
    }
    #[test] fn toggle_hover_reduced_motion_and_hide_do_not_leave_idle_animation() {
        let mut s=new_toggle_state();s.toggle_hover(true,0,true);assert_eq!(s.toggle_hover_progress(125),0.5);assert!(s.toggle_animating(125));
        s.toggle_finish();assert_eq!(s.toggle_hover_progress(126),0.0);assert!(!s.toggle_animating(126));
        assert!(s.toggle_flip(true,130,false));s.toggle_hover(true,130,false);assert_eq!(s.toggle_progress(130),1.0);assert_eq!(s.toggle_hover_progress(130),1.0);assert!(!s.toggle_animating(130));
    }
    fn toggle_style()->ffi::ToggleStyle {ffi::ToggleStyle{width:38.0,height:28.0,progress:0.0,hover:0.0,checked:false,pressed:false,focused:false,rtl:false,track_on:0xff8040c0,track_off:0xffeeeeee,thumb_on:0xffffffff,thumb_off:0xff111111,thumb_hover:0xffcccccc,halo:0x22000000,focus:0xff0000ff,border:0xff888888,clip:ffi::FloatRect{x:0.0,y:0.0,width:38.0,height:28.0}}}
    #[test] fn toggle_scene_matches_native_track_thumb_and_rtl_endpoints() {
        let mut s=toggle_style();let off=toggle_scene(&s);assert_eq!((off[0].x,off[0].y,off[0].width,off[0].height),(6.0,6.0,26.0,16.0));assert_eq!((off[2].x,off[2].width),(10.0,8.0));
        s.checked=true;s.progress=1.0;let on=toggle_scene(&s);assert_eq!((on[1].x,on[1].width),(18.0,12.0));assert_eq!(on[0].color,s.track_on);
        s.rtl=true;let rtl=toggle_scene(&s);assert_eq!(rtl[1].x,8.0);s.focused=true;s.hover=1.0;s.pressed=true;let ops=toggle_scene(&s);assert_eq!(ops.len(),4);assert_eq!(ops[3].stroke,2.0);assert!(ops.iter().all(|p|p.text.is_empty()&&p.clip.width==38.0));
    }
    #[test] fn toggle_scene_rejects_nonfinite_and_empty_surfaces() {
        for value in [f32::NAN,f32::INFINITY,f32::NEG_INFINITY]{let mut s=toggle_style();s.progress=value;assert!(toggle_scene(&s).is_empty());}
        let mut s=toggle_style();s.width=0.0;assert!(toggle_scene(&s).is_empty());
        s.width=1.0;s.height=1.0;assert!(toggle_scene(&s).iter().all(|p|p.width>=0.0&&p.height>=0.0&&p.x.is_finite()));
    }
    #[test] fn checkbox_layout_places_mark_gap_and_full_label_extent() {
        let g=checkbox_layout(400,20,1,6,5,6,5,false);assert_eq!((g.mark.x,g.mark.width,g.label.x,g.label.width),(6,16,30,364));assert_eq!(g.height,34);assert_eq!(g.lines.len(),1);assert_eq!(g.label.height,20);
    }
    #[test] fn checkbox_layout_retains_wrapped_line_positions_and_empty_label_space() {
        let g=checkbox_layout(200,20,3,6,5,6,5,false);assert_eq!(g.height,74);assert_eq!(g.lines.iter().map(|r|r.y).collect::<Vec<_>>(),vec![7,27,47]);assert_eq!(g.label.height,60);assert_eq!(checkbox_layout(200,20,0,6,5,6,5,false).height,30);
    }
    #[test] fn checkbox_layout_mirrors_mark_and_label_for_rtl() {
        let ltr=checkbox_layout(300,20,2,6,5,6,5,false);let rtl=checkbox_layout(300,20,2,6,5,6,5,true);assert_eq!(ltr.height,rtl.height);assert_eq!(rtl.mark.x,278);assert_eq!(rtl.label.x,6);assert_eq!(rtl.label.width,ltr.label.width);assert!(rtl.label.x+rtl.label.width<rtl.mark.x);
    }
    #[test] fn checkbox_layout_is_bounded_for_zero_narrow_and_saturated_metrics() {
        for width in [0,1,6,15,50,i32::MAX] {for rtl in [false,true] {let g=checkbox_layout(width,i32::MAX,3,6,i32::MAX,6,i32::MAX,rtl);for r in g.lines.iter().chain([&g.mark,&g.label]) {assert!(r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0);assert!(i64::from(r.x)+i64::from(r.width)<=i64::from(width));assert!(i64::from(r.y)+i64::from(r.height)<=i64::from(g.height));}}}
    }
    #[test] fn checkbox_mark_scene_preserves_checked_unchecked_focus_hover_and_clip() {
        let mut c=row(16.0);c.kind=20;c.height=16.0;c.active=true;c.hovered=true;c.focused=true;c.detail_ink=0xff0000ff;let ops=layout(&[c]);assert_eq!(ops.len(),4);assert_eq!(ops[2].text,"✓");assert!(ops[2].center);assert_eq!(ops[3].stroke,2.0);
        let mut c=row(16.0);c.kind=20;c.height=16.0;c.active=false;let ops=layout(&[c]);assert_eq!(ops.len(),1);assert_eq!(ops[0].stroke,1.5);assert!(ops[0].text.is_empty());
    }
    fn import_ready_form() -> Box<ImportForm> {let mut f=new_import_form();let s=import_defaults();f.select(s.clone());f.begin_preview(s);f.set_status("Gotowe",true,false,false);f}
    #[test] fn import_form_preserves_defaults_sources_and_copy() {
        let s=import_defaults();assert_eq!(s.browser,0);assert!(s.separate_profiles&&s.bookmarks&&!s.history&&!s.cookies);let c=import_copy();assert_eq!(c.browsers,vec!["Arc","Google Chrome","Microsoft Edge","Brave"]);assert_eq!(c.apply_label,"Importuj");assert!(c.cookies.contains("sesje"));
    }
    #[test] fn import_form_selects_all_browsers_without_forcing_data_categories() {
        let mut f=new_import_form();for browser in 0..4 {let mut s=import_defaults();s.browser=browser;s.bookmarks=false;s.separate_profiles=false;let r=f.select(s);assert!(matches!(r.action,ffi::ImportAction::Preview));assert!(!r.selection.bookmarks&&!r.selection.history&&!r.selection.cookies);assert_eq!(f.view(true,false).bookmarks_label,if browser==0 {"Przypięte karty, foldery i Ulubione"} else {"Zakładki i foldery"});}
    }
    #[test] fn import_form_debounce_dispatches_only_latest_visible_selection_once() {
        let mut f=new_import_form();let a=f.select(import_defaults());let mut current=import_defaults();current.browser=1;let b=f.select(current.clone());assert_ne!(a.epoch,b.epoch);assert!(matches!(f.take_scan(a.epoch,true,current.clone(),true,false).action,ffi::ImportAction::None));
        let r=f.take_scan(b.epoch,true,current.clone(),true,false);assert!(matches!(r.action,ffi::ImportAction::Preview));assert_eq!(r.selection.browser,1);assert!(matches!(f.take_scan(b.epoch,true,current,true,false).action,ffi::ImportAction::None));
    }
    #[test] fn import_form_cancel_and_hidden_tick_cannot_restart_scan() {
        let mut f=new_import_form();let a=f.select(import_defaults());f.cancel_scan();assert!(matches!(f.take_scan(a.epoch,true,import_defaults(),true,false).action,ffi::ImportAction::None));let b=f.select(import_defaults());assert!(matches!(f.take_scan(b.epoch,false,import_defaults(),true,false).action,ffi::ImportAction::None));assert!(matches!(f.take_scan(b.epoch,true,import_defaults(),true,false).action,ffi::ImportAction::None));
    }
    #[test] fn import_form_blocked_preview_resumes_only_when_configuration_available() {
        for (ready,busy) in [(false,false),(true,true)] {let mut f=new_import_form();let r=f.select(import_defaults());assert!(matches!(f.take_scan(r.epoch,true,import_defaults(),ready,busy).action,ffi::ImportAction::None));assert!(!f.should_resume(false,false));assert!(!f.should_resume(true,true));assert!(f.should_resume(true,false));f.cancel_scan();assert!(!f.should_resume(true,false));}
    }
    #[test] fn import_form_rechecks_programmatic_option_changes_before_scan_and_apply() {
        let mut f=new_import_form();let r=f.select(import_defaults());let mut changed=import_defaults();changed.cookies=true;assert!(matches!(f.take_scan(r.epoch,true,changed.clone(),true,false).action,ffi::ImportAction::Rescan));
        let mut f=import_ready_form();assert!(matches!(f.request_apply(changed,true,false,true).action,ffi::ImportAction::Rescan));assert!(!f.view(true,false).apply_enabled);
    }
    #[test] fn import_form_scanning_stays_editable_applying_locks_controls_and_close() {
        let mut f=new_import_form();f.select(import_defaults());let v=f.view(true,false);assert!(v.options_enabled&&v.close_enabled&&!v.apply_enabled);f.set_status("Importowanie…",false,true,true);let v=f.view(true,false);assert!(!v.options_enabled&&!v.close_enabled&&!v.apply_enabled);assert!(v.applying);assert!(matches!(f.select(import_defaults()).action,ffi::ImportAction::None));
        f.set_status("Odmowa dostępu do źródła",true,false,false);let v=f.view(true,false);assert!(v.options_enabled&&v.close_enabled&&v.apply_enabled);assert_eq!(v.message,"Odmowa dostępu do źródła");
    }
    #[test] fn import_form_apply_admission_prevents_duplicates_and_can_retry_rejection() {
        let mut f=import_ready_form();assert!(matches!(f.request_apply(import_defaults(),true,false,true).action,ffi::ImportAction::Apply));assert!(!f.view(true,false).options_enabled);assert!(matches!(f.request_apply(import_defaults(),true,false,true).action,ffi::ImportAction::None));f.finish_apply(false);assert!(f.view(true,false).apply_enabled);assert!(f.view(true,false).message.contains("Spróbuj ponownie"));assert!(matches!(f.request_apply(import_defaults(),true,false,true).action,ffi::ImportAction::Apply));f.finish_apply(true);assert!(f.view(true,false).applying);
    }
    #[test] fn import_form_service_completion_is_not_overwritten_by_admission_return() {
        let mut f=import_ready_form();f.request_apply(import_defaults(),true,false,true);f.set_status("Zaimportowano dane",false,false,false);f.finish_apply(true);let v=f.view(true,false);assert!(!v.applying&&!v.apply_enabled);assert!(v.options_enabled);assert_eq!(v.message,"Zaimportowano dane");
        f.begin_preview(import_defaults());f.set_status("Odczytywanie…",true,true,false);assert!(!f.view(true,false).apply_enabled);
    }
    #[test] fn import_form_rejects_invalid_sources_hidden_apply_and_unready_service_state() {
        let mut f=new_import_form();let mut invalid=import_defaults();invalid.browser=99;assert!(matches!(f.select(invalid).action,ffi::ImportAction::None));assert!(!f.view(true,false).message.is_empty());
        for (ready,busy,visible) in [(false,false,true),(true,true,true),(true,false,false)] {let mut f=import_ready_form();assert!(matches!(f.request_apply(import_defaults(),ready,busy,visible).action,ffi::ImportAction::None));}
        let mut f=import_ready_form();let mut external=import_defaults();external.history=true;f.begin_preview(external);f.set_status("Inny podgląd",true,false,false);assert!(!f.view(true,false).apply_enabled);
    }
    #[test] fn action_button_metrics_preserve_platform_padding_minimum_and_height() {
        let g=action_button_layout(-1,-1,100,40,16,64);assert_eq!((g.width,g.height,g.text.x,g.text.width),(132,40,16,100));assert_eq!(action_button_layout(-1,-1,0,40,16,64).width,64);
    }
    #[test] fn action_button_metrics_bound_tiny_negative_and_saturated_inputs() {
        for width in [0,1,8,64,i32::MAX] {let g=action_button_layout(width,0,100,40,16,64);assert!(g.text.x>=0&&g.text.width>=0);assert!(g.text.x+g.text.width<=width);assert_eq!(g.text.height,0);}
        let g=action_button_layout(-1,-1,i32::MAX,40,i32::MAX,64);assert_eq!(g.width,i32::MAX);assert_eq!(action_button_layout(-1,-1,-5,-1,-8,-1).width,0);
    }
    #[test] fn action_group_vertical_preserves_full_width_spacing_and_extent() {
        let g=action_group_layout(378,-1,true,10,&[child(100,40),child(150,40)]);assert_eq!((g.width,g.height,g.children[1].y),(378,90,50));assert!(g.children.iter().all(|r|r.width==378));
        assert_eq!(action_group_layout(-1,-1,true,8,&[child(100,40),child(150,40),child(90,40)]).height,136);
    }
    #[test] fn action_group_horizontal_preserves_natural_width_and_shrinks_safely() {
        let g=action_group_layout(-1,-1,false,8,&[child(132,40),child(100,36)]);assert_eq!((g.width,g.height,g.children[1].x,g.children[1].y),(240,40,140,2));
        let g=action_group_layout(100,-1,false,8,&[child(132,40),child(100,40)]);assert_eq!(g.children[1].x+g.children[1].width,100);assert!(g.children[0].width>g.children[1].width);
    }
    #[test] fn action_groups_remain_bounded_with_empty_tiny_and_maximum_metrics() {
        for vertical in [false,true] {for width in [0,1,80,i32::MAX] {for height in [0,1,40,i32::MAX] {let g=action_group_layout(width,height,vertical,8,&[child(i32::MAX,i32::MAX),child(i32::MAX,i32::MAX),child(i32::MAX,i32::MAX)]);for r in g.children{assert!(r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0);assert!(i64::from(r.x)+i64::from(r.width)<=i64::from(width));assert!(i64::from(r.y)+i64::from(r.height)<=i64::from(height));}}}}
        assert!(action_group_layout(-1,-1,false,8,&[]).children.is_empty());
    }
    #[test] fn action_button_scene_keeps_hover_border_focus_and_centered_text() {
        let mut b=row(160.0);b.kind=19;b.height=40.0;b.title_width=16.0;b.hovered=true;b.focused=true;b.detail_ink=0x14101010;let ops=layout(&[b]);assert_eq!(ops.len(),5);assert_eq!(ops[0].radius,20.0);assert_eq!(ops[2].text,"Zażółć 日本語 🦀");assert!(ops[2].center);assert_eq!(ops[2].width,128.0);assert_eq!(ops[4].stroke,1.0);
    }
    #[test] fn action_button_text_style_omits_empty_background_and_border() {
        let mut b=row(160.0);b.kind=19;b.fill=0;b.badge=0;b.active=false;b.title_width=16.0;let ops=layout(&[b]);assert_eq!(ops.len(),1);assert!(ops[0].center);assert!(!ops[0].bold);assert_eq!(ops[0].text,"Zażółć 日本語 🦀");
        let mut empty=row(160.0);empty.kind=19;empty.fill=0;empty.badge=0;empty.title.clear();assert!(layout(&[empty]).is_empty());
    }
    #[test] fn appearance_presets_preserve_palette_order_labels_and_unicode() {
        let colors=appearance_presets(false);assert_eq!(colors.len(),4);assert_eq!(colors[0].color,0xff9e91dc);assert_eq!(colors[3].label,"#7FAACB");assert!(colors.iter().all(|p|p.emoji.is_empty()&&p.color>>24==255));
        let emojis=appearance_presets(true);assert_eq!(emojis.len(),6);assert_eq!(emojis[2].emoji,"🚀");assert_eq!(emojis[5].label,"Emoji 🎮");
    }
    #[test] fn appearance_selection_matches_custom_values_and_out_of_range_indices() {
        assert!(appearance_selected(false,0,0x9e91dc,""));assert!(!appearance_selected(false,0,0xff123456,""));assert!(!appearance_selected(false,4,0xff9e91dc,""));
        assert!(appearance_selected(true,2,0,"🚀"));assert!(!appearance_selected(true,2,0,"👩‍💻"));assert!(!appearance_selected(true,6,0,"🚀"));assert!(!appearance_selected(true,0,0,""));
    }
    #[test] fn appearance_row_preserves_control_sizes_and_excludes_editor() {
        for (emoji,count,bw,bh) in [(false,4,32,32),(true,6,30,28)] {
            let g=appearance_row_layout(398,-1,100,30,emoji);assert_eq!(g.children.len(),count+2);assert_eq!(g.height,bh.max(30));
            for r in &g.children[..count] {assert_eq!((r.width,r.height),(bw,bh));}
            let field=&g.children[count+1];assert_eq!(field.x,count as i32*bw+8);assert_eq!(field.x+field.width,398);
        }
    }
    #[test] fn appearance_row_bounds_remain_nonnegative_on_tiny_and_saturated_metrics() {
        for emoji in [false,true] {for width in [0,1,17,127,188,398,i32::MAX] {for height in [0,1,40] {
            let g=appearance_row_layout(width,height,100,30,emoji);for r in &g.children {assert!(r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0);assert!(i64::from(r.x)+i64::from(r.width)<=i64::from(width));assert!(r.y+r.height<=height);}
        }}}
        assert_eq!(appearance_row_layout(-1,-1,100,30,false).width,236);assert_eq!(appearance_row_layout(-1,-1,i32::MAX,i32::MAX,true).width,i32::MAX);
    }
    #[test] fn appearance_swatch_draws_raw_color_outline_focus_and_clip() {
        let mut swatch=row(32.0);swatch.kind=18;swatch.height=32.0;swatch.active=false;swatch.badge=0xff84b6a4;let ops=layout(std::slice::from_ref(&swatch));assert_eq!(ops.len(),1);assert_eq!(ops[0].color,0xff84b6a4);assert_eq!(ops[0].radius,7.0);
        swatch.active=true;swatch.focused=true;let ops=layout(&[swatch]);assert_eq!(ops.len(),3);assert_eq!(ops[0].stroke,1.0);assert_eq!(ops[1].width,14.0);assert_eq!(ops[2].height,2.0);assert_eq!(ops[0].clip.width,32.0);
    }
    #[test] fn appearance_emoji_scene_uses_centered_text_and_selection_wash() {
        let mut emoji=row(30.0);emoji.kind=7;emoji.glyph=1;emoji.monogram="🚀".into();emoji.fill=0x1f1b1a22;let ops=layout(&[emoji]);assert_eq!(ops.len(),2);assert_eq!(ops[0].radius,7.0);assert_eq!(ops[1].text,"🚀");assert!(ops[1].center);
    }
    #[test] fn action_geometry_preserves_workspace_emoji_and_chrome_metrics() {
        for (dot,label,w,h) in [(false,false,32,32),(false,true,32,32),(true,false,24,28),(true,true,30,28)]{let r=action_preferred_size(dot,label);assert_eq!((r.width,r.height),(w,h));}
    }
    fn node_tab(low:u64)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:1,high:7,low,name:String::new()}}
    fn node_pin(name:&str)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:2,high:0,low:0,name:name.into()}}
    fn node_close(low:u64)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:10,high:7,low,name:String::new()}}
    fn node_named(kind:u32,name:&str)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind,high:0,low:0,name:name.into()}}
    #[test] fn action_graph_history_records_keep_ids_across_refresh_and_retire_old_results(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_named(5,"profile-a/url/time/0"),node_named(5,"profile-a/url/time/1")]);assert!(p.admitted);assert_ne!(p.ids[0],p.ids[1]);
        let mut old=g.acquire(&node_named(5,"profile-a/url/time/0"));old.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        let q=g.reconcile(vec![node_named(5,"profile-a/url/time/1"),node_named(5,"profile-a/url/time/0")]);assert_eq!(q.ids,vec![p.ids[1],p.ids[0]]);
        let next=g.acquire(&node_named(5,"profile-a/url/time/0"));assert_eq!(next.action_node_id(),p.ids[0]);assert!(!next.action_pending());assert!(!old.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).handled);
        g.reconcile(vec![node_named(5,"profile-b/url/time/0")]);assert_eq!(next.action_node_id(),0);
    }
    #[test] fn action_graph_result_namespace_is_separate_and_empty_keys_are_invalid(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_named(5,"record"),node_named(3,"record"),node_pin("record")]);assert!(p.admitted);assert_eq!(p.ids.iter().copied().collect::<BTreeSet<_>>().len(),3);
        let invalid=g.reconcile(vec![node_named(5,"")]);assert!(!invalid.admitted);assert_eq!(invalid.ids,p.ids);
    }
    #[test] fn action_graph_footer_namespaces_do_not_alias_domain_ids(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_pin("pin"),node_named(3,"pin"),node_named(4,"pin"),node_named(4,"settings")]);
        assert!(p.admitted);assert_eq!(p.ids.iter().copied().collect::<BTreeSet<_>>().len(),4);
        let mut space=g.acquire(&node_named(3,"pin"));let chrome=g.acquire(&node_named(4,"pin"));space.action_on(true);assert!(!chrome.action_snapshot().on);
    }
    #[test] fn action_graph_footer_reorder_and_replacement_keep_identity_but_retire_removed_space(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_named(4,"pin"),node_named(4,"settings"),node_named(3,"work"),node_named(3,"personal")]);
        let mut old=g.acquire(&node_named(3,"personal"));old.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        let q=g.reconcile(vec![node_named(4,"pin"),node_named(4,"settings"),node_named(3,"personal"),node_named(3,"work")]);assert_eq!(q.ids,vec![p.ids[0],p.ids[1],p.ids[3],p.ids[2]]);
        let next=g.acquire(&node_named(3,"personal"));assert_eq!(next.action_node_id(),p.ids[3]);assert!(!next.action_pending());assert!(!old.action_key(true,1,true,true).handled);
        let removed=g.reconcile(vec![node_named(4,"pin"),node_named(4,"settings"),node_named(3,"work")]);assert_eq!(removed.retired,1);assert_eq!(next.action_node_id(),0);
        let reinsert=g.reconcile(vec![node_named(4,"pin"),node_named(4,"settings"),node_named(3,"work"),node_named(3,"personal")]);assert!(reinsert.ids[3]>p.ids[3]);
    }
    #[test] fn action_graph_footer_invalid_projection_cannot_retire_live_controls(){
        let mut g=new_action_graph();let ids=g.reconcile(vec![node_named(4,"pin"),node_named(3,"work")]).ids;
        for key in [node_named(3,""),node_named(4,""),ffi::ActionNodeKey{kind:3,high:1,low:0,name:"work".into()}]{let rejected=g.reconcile(vec![key]);assert!(!rejected.admitted);assert_eq!(rejected.ids,ids);}
    }
    #[test] fn action_graph_close_children_follow_parent_identity_and_retire_with_affordance(){let mut g=new_action_graph();let p=g.reconcile(vec![node_tab(1),node_close(1),node_tab(2),node_close(2)]);assert!(p.admitted);let old=g.acquire(&node_close(2));let q=g.reconcile(vec![node_tab(2),node_close(2),node_tab(1),node_close(1)]);assert_eq!(q.ids,vec![p.ids[2],p.ids[3],p.ids[0],p.ids[1]]);let one=g.reconcile(vec![node_tab(2)]);assert_eq!((one.created,one.retired),(0,3));assert_eq!(one.ids,vec![p.ids[2]]);assert_eq!(old.action_node_id(),0);}
    #[test] fn action_graph_orphan_close_is_rejected_without_retiring_live_nodes(){let mut g=new_action_graph();let p=g.reconcile(vec![node_tab(1),node_close(1)]);let node=g.acquire(&node_tab(1));let failed=g.reconcile(vec![node_tab(2),node_close(1)]);assert!(!failed.admitted);assert_eq!(failed.ids,p.ids);assert_ne!(0,node.action_node_id());}
    #[test] fn action_graph_reorders_without_replacing_logical_nodes(){let mut g=new_action_graph();let p=g.reconcile(vec![node_tab(1),node_tab(2),node_pin("folder")]);assert!(p.admitted);assert_eq!(p.created,3);let q=g.reconcile(vec![node_pin("folder"),node_tab(2),node_tab(1)]);assert!(q.admitted);assert_eq!(q.ids,vec![p.ids[2],p.ids[1],p.ids[0]]);assert_eq!((q.created,q.retired),(0,0));}
    #[test] fn action_graph_native_adapter_replacement_retains_id_and_value_but_not_pending_input(){let mut g=new_action_graph();let id=g.reconcile(vec![node_tab(1)]).ids[0];{let mut first=g.acquire(&node_tab(1));first.action_on(true);first.action_focus(true);first.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(first.action_pending());}let next=g.acquire(&node_tab(1));assert_eq!(next.action_node_id(),id);assert!(next.action_snapshot().on);assert!(!next.action_snapshot().focused);assert!(!next.action_pending());}
    #[test] fn action_graph_retirement_reinsertion_never_reuses_or_resurrects_old_handles(){let mut g=new_action_graph();let id=g.reconcile(vec![node_tab(1)]).ids[0];let mut old=g.acquire(&node_tab(1));assert_eq!(g.reconcile(vec![]).retired,1);assert_eq!(old.action_node_id(),0);assert!(!old.action_enabled(true));assert!(!old.action_key(true,1,true,true).handled);let next=g.reconcile(vec![node_tab(1)]);assert!(next.ids[0]>id);assert_eq!(old.action_node_id(),0);}
    #[test] fn action_graph_rebinding_invalidates_old_lease_and_old_drop_cannot_cancel_new_input(){let mut g=new_action_graph();g.reconcile(vec![node_tab(1)]);let old=g.acquire(&node_tab(1));let id=old.action_node_id();let mut next=g.acquire(&node_tab(1));assert_eq!(next.action_node_id(),id);assert_eq!(old.action_node_id(),0);next.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);drop(old);assert!(next.action_pending());assert!(next.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action==ffi::RowAction::Activate);}
    #[test] fn action_graph_scope_isolation_does_not_alias_identical_keys(){let mut a=new_action_graph();let mut b=new_action_graph();a.reconcile(vec![node_pin("same")]);b.reconcile(vec![node_pin("same")]);let mut x=a.acquire(&node_pin("same"));let y=b.acquire(&node_pin("same"));x.action_on(true);a.reconcile(vec![]);assert!(!y.action_snapshot().on);assert!(y.action_snapshot().enabled);assert_ne!(0,y.action_node_id());}
    #[test] fn action_graph_invalid_snapshots_are_transactional(){let mut g=new_action_graph();let ids=g.reconcile(vec![node_tab(1)]).ids;let mut node=g.acquire(&node_tab(1));node.action_on(true);for keys in [vec![node_tab(2),node_tab(2)],vec![node_pin("")],vec![ffi::ActionNodeKey{kind:1,high:0,low:0,name:String::new()}],vec![ffi::ActionNodeKey{kind:99,high:0,low:0,name:"x".into()}]]{let p=g.reconcile(keys);assert!(!p.admitted);assert_eq!(p.ids,ids);assert!(node.action_snapshot().on);assert!(node.action_node_id()!=0);}}
    #[test] fn action_graph_owner_drop_retires_retained_native_adapter(){let mut node={let mut g=new_action_graph();g.reconcile(vec![node_tab(1)]);g.acquire(&node_tab(1))};assert_eq!(node.action_node_id(),0);assert!(!node.action_gesture(1,true).handled);assert!(!node.action_snapshot().enabled);}
    #[test] fn action_graph_id_exhaustion_is_transactional_and_cleanup_still_works(){let mut g=new_action_graph();let ids=g.reconcile(vec![node_tab(1)]).ids;g.next_id=u64::MAX;let failed=g.reconcile(vec![node_tab(2)]);assert!(!failed.admitted);assert_eq!(failed.ids,ids);assert_eq!(g.reconcile(vec![]).retired,1);}
    #[test] fn action_graph_invalid_acquire_never_creates_an_authorized_node(){let mut g=new_action_graph();let mut node=g.acquire(&node_tab(1));assert_eq!(node.action_node_id(),0);assert!(!node.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true).handled);assert!(!node.action_on(true));assert_eq!(g.reconcile(vec![node_tab(1)]).created,1);}
    #[test] fn action_press_activation_consumes_release_and_keeps_default_release_policy() {
        use ffi::{PointerPhase as P,RowAction as A};let mut ordinary=new_action_control();assert!(ordinary.action_pointer(P::Press,2,2,true,true,true).action==A::None);assert!(ordinary.action_pointer(P::Release,2,2,true,true,true).action==A::Activate);
        let mut menu=new_action_control();assert!(menu.action_on_press(true));assert!(!menu.action_on_press(true));assert!(menu.action_pointer(P::Press,2,2,true,true,true).action==A::Activate);assert!(!menu.action_pending());assert!(menu.action_pointer(P::Release,2,2,true,true,true).action!=A::Activate);assert!(menu.action_pointer(P::Drag,3,3,true,true,true).action==A::None);
    }
    #[test] fn action_press_activation_obeys_admission_and_mode_change_cancels_pending_input() {
        use ffi::{PointerPhase as P,RowAction as A};let mut menu=new_action_control();menu.action_on_press(true);
        for (inside,trigger,visible,enabled) in [(false,true,true,true),(true,false,true,true),(true,true,false,true),(true,true,true,false)] {menu.action_enabled(enabled);assert!(menu.action_pointer(P::Press,1,1,inside,trigger,visible).action==A::None);assert!(!menu.action_pending());}
        menu.action_enabled(true);menu.action_on_press(false);menu.action_pointer(P::Press,1,1,true,true,true);assert!(menu.action_pending());menu.action_on_press(true);assert!(!menu.action_pending());assert!(menu.action_pointer(P::Release,1,1,true,true,true).action!=A::Activate);
    }
    #[test] fn select_pointer_reopen_guard_is_strict_monotonic_and_keyboard_bypasses_it() {
        let mut s=select_fixture();let first=s.open_pointer(true,1000,100000);assert_ne!(0,first);assert!(s.close_menu_at(first,2000));for now in [0,1999,2000,101999,102000]{assert_eq!(0,s.open_pointer(true,now,100000));}assert_ne!(0,s.open_pointer(true,102001,100000));let keyboard=s.open(true);assert_ne!(0,keyboard);assert!(s.can_choose(keyboard,"Arc",true));
    }
    #[test] fn select_stale_close_cannot_change_new_popup_or_its_pointer_guard() {
        let mut s=select_fixture();let old=s.open(true);s.dismiss_popup(1000,true);let next=s.open(true);assert!(!s.close_menu_at(old,1000000));assert!(s.can_choose(next,"Arc",true));assert!(s.close_menu_at(next,2000));assert_eq!(0,s.open_pointer(true,102000,100000));assert_ne!(0,s.open_pointer(true,102001,100000));
    }
    #[test] fn select_popup_dismiss_tracks_chosen_native_popup_without_blocking_initial_open() {
        let mut s=select_fixture();s.dismiss_popup(5000,false);assert_ne!(0,s.open_pointer(true,5000,100000));let token=s.open(true);assert!(s.choose_key(token,"Brave",true));s.dismiss_popup(6000,true);assert_eq!("Brave",s.selected_key());assert_eq!(0,s.open_pointer(true,106000,100000));assert_ne!(0,s.open_pointer(true,106001,100000));let token=s.open(true);assert!(s.close_menu_at(token,200000));s.dismiss_popup(999999,false);assert_ne!(0,s.open_pointer(true,300001,100000));
    }
    #[test] fn default_action_accelerator_requires_default_enabled_visible_unmodified_return() {
        use ffi::RowAction as A;let mut c=new_action_control();assert!(!c.action_snapshot().is_default);assert!(!c.action_accelerator(2,false,true).handled);assert!(c.action_default(true));assert!(!c.action_default(true));assert!(c.action_snapshot().is_default);
        assert!(!c.action_accelerator(1,false,true).handled);assert!(!c.action_accelerator(2,true,true).handled);assert!(!c.action_accelerator(2,false,false).handled);c.action_enabled(false);assert!(!c.action_accelerator(2,false,true).handled);c.action_enabled(true);assert!(c.action_accelerator(2,false,true).action==A::Activate);c.action_default(false);assert!(!c.action_accelerator(2,false,true).handled);
    }
    #[test] fn default_accelerator_retires_pointer_and_keeps_independent_selection() {
        use ffi::{PointerPhase as P,RowAction as A,RowVisual as V};let mut c=new_action_control();c.action_default(true);c.action_on(true);c.action_pointer(P::Press,2,2,true,true,true);assert!(c.action_pending());let r=c.action_accelerator(2,false,true);assert!(r.handled&&r.action==A::Activate&&r.visual==V::Normal);assert!(!c.action_pending());assert!(c.action_snapshot().on&&c.action_snapshot().is_default);assert!(c.action_pointer(P::Release,2,2,true,true,true).action!=A::Activate);
    }
    #[test] fn action_cancel_is_idle_safe_and_retires_capture_before_viewport_return() {
        use ffi::{PointerPhase as P,RowAction as A};let mut c=new_action_control();assert!(!c.action_cancel());c.action_pointer(P::Press,1,1,true,true,true);assert!(c.action_cancel());assert!(!c.action_cancel());assert!(!matches!(c.action_pointer(P::Release,1,1,true,true,true).action,A::Activate));c.action_cancel();c.action_key(true,1,false,true);assert!(c.action_cancel());assert!(!matches!(c.action_key(false,1,false,true).action,A::Activate));c.action_enabled(false);assert!(!c.action_cancel());
    }
    #[test] fn action_control_pointer_disabled_hidden_and_cancel_never_dispatch_stale_presses() {
        use ffi::{PointerPhase as P,RowAction as A,RowVisual as V};let mut c=new_action_control();c.action_pointer(P::Press,2,2,true,true,true);assert!(matches!(c.action_snapshot().visual,V::Pressed));assert!(matches!(c.action_pointer(P::Release,2,2,true,true,true).action,A::Activate));
        c.action_pointer(P::Press,2,2,true,true,true);c.action_enabled(false);assert!(matches!(c.action_snapshot().visual,V::Disabled));assert!(matches!(c.action_pointer(P::Release,2,2,true,true,true).action,A::None));c.action_enabled(true);
        c.action_pointer(P::Press,2,2,true,true,true);assert!(!c.action_pointer(P::Move,2,2,true,false,false).handled);assert!(!matches!(c.action_pointer(P::Release,2,2,true,true,true).action,A::Activate));
        c.action_pointer(P::Press,2,2,true,true,true);c.action_cancel();assert!(!matches!(c.action_pointer(P::Release,2,2,true,true,true).action,A::Activate));
    }
    #[test] fn action_control_platform_keys_gestures_focus_and_selection_are_authoritative() {
        use ffi::{RowAction as A,RowVisual as V};let mut c=new_action_control();c.action_focus(true);assert!(c.action_snapshot().focused);assert!(c.action_on(true));assert!(!c.action_on(true));assert!(c.action_snapshot().on);
        assert!(matches!(c.action_key(true,1,true,true).action,A::Activate));assert!(!c.action_key(false,1,true,true).handled);assert!(!c.action_key(true,2,true,true).handled);
        assert!(c.action_key(true,1,false,true).handled);assert!(matches!(c.action_snapshot().visual,V::Pressed));c.action_focus(false);assert!(!matches!(c.action_key(false,1,false,true).action,A::Activate));assert!(!c.action_snapshot().focused);
        c.action_key(true,1,false,true);assert!(matches!(c.action_key(false,1,false,true).action,A::Activate));assert!(matches!(c.action_gesture(1,true).action,A::Activate));c.action_gesture(0,true);assert!(!c.action_gesture(2,true).handled);assert!(matches!(c.action_snapshot().visual,V::Normal));c.action_enabled(false);assert!(!c.action_gesture(1,true).handled);c.action_enabled(true);assert!(!c.action_gesture(1,false).handled);assert!(!c.action_key(true,1,true,false).handled);assert!(!c.action_gesture(99,true).handled);
    }
    #[test] fn appearance_input_reuses_cancel_disabled_and_platform_key_policy() {
        let mut p=new_row_pointer();p.pointer_event(ffi::PointerPhase::Press,10,10,true,true,true,false);p.cancel();assert!(matches!(p.pointer_event(ffi::PointerPhase::Release,10,10,true,true,true,false).action,ffi::RowAction::Cancel));
        assert!(matches!(p.key_event(true,key_policy(1,true),true).action,ffi::RowAction::Activate));assert!(!p.key_event(true,key_policy(2,true),true).handled);assert!(matches!(p.key_event(true,key_policy(1,true),false).action,ffi::RowAction::None));
    }
    fn notice_workspaces() -> Vec<ffi::ProfileUse> { vec![ffi::ProfileUse { workspace:"work".into(), name:"Praca".into(), profile:"shared".into() },ffi::ProfileUse { workspace:"personal".into(), name:"Prywatne".into(), profile:"shared".into() }] }
    #[test] fn workspace_notice_preserves_irreversible_warning_profile_data_and_cancel() {
        let f=new_workspace_notice("personal","Żółć 日本語 🦀");let c=f.copy();assert_eq!(c.title,"Usunąć przestrzeń „Żółć 日本語 🦀”?");assert!(c.default_cancel);assert_eq!(c.cancel,"Anuluj");assert_eq!(c.accept,"Usuń przestrzeń");
        assert!(c.body.contains("karty i przypięte elementy zostaną usunięte"));assert!(c.body.contains("Tej operacji nie można cofnąć."));assert!(c.body.contains("\n\nProfil przeglądania, hasła, historia i cookies pozostaną zachowane."));
    }
    #[test] fn workspace_notice_revalidates_readiness_busy_last_and_missing_targets() {
        for (id,ready,busy) in [("personal",false,false),("personal",true,true),("missing",true,false),("",true,false)] {
            let mut f=new_workspace_notice(id,"W");let r=f.submit(ready,busy,&notice_workspaces());assert!(matches!(r.action,ffi::WorkspaceNoticeAction::None));assert!(r.workspace.is_empty());assert!(!f.error().is_empty());
        }
        let mut f=new_workspace_notice("work","W");let mut workspaces=notice_workspaces();workspaces.pop();assert!(matches!(f.submit(true,false,&workspaces).action,ffi::WorkspaceNoticeAction::None));assert!(f.error().contains("ostatniej"));
    }
    #[test] fn workspace_notice_keeps_captured_id_across_reorder_and_profile_assignment() {
        let mut f=new_workspace_notice("personal","Prywatne");let mut workspaces=notice_workspaces();workspaces.reverse();workspaces[0].profile="changed".into();workspaces[0].name="Renamed".into();
        let r=f.submit(true,false,&workspaces);assert!(matches!(r.action,ffi::WorkspaceNoticeAction::Delete));assert_eq!(r.workspace,"personal");
    }
    #[test] fn workspace_notice_admission_can_retry_but_pending_and_accepted_cannot_repeat() {
        let mut f=new_workspace_notice("personal","P");assert!(matches!(f.submit(true,false,&notice_workspaces()).action,ffi::WorkspaceNoticeAction::Delete));assert!(matches!(f.submit(true,false,&notice_workspaces()).action,ffi::WorkspaceNoticeAction::None));
        f.finish_attempt(false);assert!(f.error().contains("Spróbuj ponownie"));assert!(matches!(f.submit(true,false,&notice_workspaces()).action,ffi::WorkspaceNoticeAction::Delete));assert!(f.error().is_empty());f.finish_attempt(true);
        assert!(matches!(f.submit(true,false,&notice_workspaces()).action,ffi::WorkspaceNoticeAction::None));
    }
    #[test] fn workspace_notice_cancel_is_terminal_and_external_removal_never_redirects() {
        let mut f=new_workspace_notice("personal","P");f.cancel();assert!(matches!(f.submit(true,false,&notice_workspaces()).action,ffi::WorkspaceNoticeAction::None));
        let mut f=new_workspace_notice("personal","P");let mut workspaces=notice_workspaces();workspaces[1].workspace="replacement".into();let r=f.submit(true,false,&workspaces);assert!(matches!(r.action,ffi::WorkspaceNoticeAction::None));assert!(r.workspace.is_empty());assert_eq!(f.error(),"Przestrzeń nie jest już dostępna.");
    }
    #[test] fn workspace_notice_shared_geometry_retains_blank_paragraph_lines_and_errors() {
        let g=notice_layout(400,20,5,18,2);assert_eq!(g.body_lines.len(),5);assert_eq!((g.body.height,g.error.y,g.height),(100,108,144));assert_eq!(g.body_lines[3].y,60);
        let g=notice_layout(400,20,5,18,0);assert_eq!(g.height,100);assert!(g.error_lines.is_empty());
    }
    #[test] fn workspace_form_preserves_copy_and_profile_change_warning() {
        let c=workspace_copy();assert!(c.profile_hint.contains("Zmiana profilu zamknie karty"));assert!(c.profile_hint.contains("wspólne logowanie i cookies"));assert!(c.save_hint.contains("Wygląd zapisuje się od razu"));
        assert_eq!(workspace_appearance_error(2),"Nie udało się zapisać emoji");assert_eq!(workspace_appearance_error(3),"Podaj kolor #RRGGBB");
    }
    #[test] fn workspace_form_tracks_create_cancel_and_edit_presentation() {
        let mut f=new_workspace_form();f.reset("work");let v=f.view("Praca","Pracowy",true,false,2);assert!(!v.creating);assert!(v.appearance&&v.enabled&&v.remove_enabled);assert_eq!(v.description,"Chromium: Pracowy");
        f.set_configuration_error(true);f.begin_create("work");let v=f.view("Praca","Pracowy",true,false,2);assert!(v.creating);assert!(!v.appearance&&!v.remove_enabled);assert!(v.error.is_empty());assert_eq!(v.title,"Nowa przestrzeń");assert_eq!(v.secondary_label,"Anuluj");
        f.reset("work");assert!(!f.creating());assert_eq!(f.view("Praca","P",true,false,2).save_label,"Zapisz nazwę i profil");
    }
    #[test] fn workspace_form_preserves_ascii_trim_and_unicode_name_policy() {
        let mut f=new_workspace_form();f.begin_create("work");let r=f.submit(" \tŻółć 日本語 🦀\r\n","used","work",true,false,false,&profile_options(),&profile_uses());assert_eq!(r.name,"Żółć 日本語 🦀");assert!(matches!(r.action,ffi::WorkspaceFormAction::Create));f.finish_attempt(true);
        let raw="\u{3000}\u{00a0}";let r=f.submit(raw,"used","work",true,false,false,&profile_options(),&profile_uses());assert_eq!(r.name,raw);f.finish_attempt(true);
        // Existing model permits duplicate and arbitrarily long workspace names.
        let name="Praca".repeat(300);assert_eq!(f.submit(&name,"used","work",true,false,false,&profile_options(),&profile_uses()).name,name);
    }
    #[test] fn workspace_form_rejects_stale_context_missing_profile_and_unready_state() {
        let mut f=new_workspace_form();f.reset("work");
        for (current,ready,busy,profile) in [("other",true,false,"used"),("work",false,false,"used"),("work",true,true,"used"),("work",true,false,"missing"),("work",true,false,"")] {
            assert!(matches!(f.submit("Name",profile,current,ready,busy,false,&profile_options(),&profile_uses()).action,ffi::WorkspaceFormAction::None));assert!(!f.error.is_empty());
        }
        assert!(matches!(f.submit("Name","used","work",true,false,false,&profile_options(),&[]).action,ffi::WorkspaceFormAction::None));
    }
    #[test] fn workspace_form_keeps_composition_and_empty_validation_separate() {
        let mut f=new_workspace_form();f.reset("work");assert!(matches!(f.submit("Name","used","work",true,false,true,&profile_options(),&profile_uses()).action,ffi::WorkspaceFormAction::None));assert!(f.error.is_empty());
        assert!(matches!(f.submit(" \t\r\n\x0b\x0c","used","work",true,false,false,&profile_options(),&profile_uses()).action,ffi::WorkspaceFormAction::None));assert_eq!(f.error,"Podaj nazwę przestrzeni.");
        assert!(matches!(f.submit("Name","used","work",true,false,true,&profile_options(),&profile_uses()).action,ffi::WorkspaceFormAction::None));assert_eq!(f.error,"Podaj nazwę przestrzeni.");
    }
    #[test] fn workspace_form_latches_ids_and_admits_only_one_pending_action() {
        let mut f=new_workspace_form();f.reset("work");let mut profiles=profile_options();profiles.reverse();let r=f.submit("Renamed","spare","work",true,false,false,&profiles,&profile_uses());assert_eq!((r.workspace.as_str(),r.profile.as_str()),("work","spare"));assert!(matches!(r.action,ffi::WorkspaceFormAction::Edit));
        assert!(!f.view("Praca","P",true,false,2).enabled);assert!(matches!(f.submit("Other","used","work",true,false,false,&profiles,&profile_uses()).action,ffi::WorkspaceFormAction::None));
        f.finish_attempt(false);assert!(!f.error.is_empty());assert!(matches!(f.submit("Retry","used","work",true,false,false,&profiles,&profile_uses()).action,ffi::WorkspaceFormAction::Edit));
    }
    #[test] fn workspace_form_callback_refresh_does_not_restore_old_target() {
        let mut f=new_workspace_form();f.begin_create("work");f.submit("New","used","work",true,false,false,&profile_options(),&profile_uses());
        // Native AddWorkspace selects its allocated ID before returning admission.
        f.reset("allocated");f.finish_attempt(true);assert_eq!(f.workspace,"allocated");assert!(!f.creating());assert!(f.error.is_empty());
    }
    #[test] fn workspace_form_readiness_last_workspace_and_external_errors() {
        let mut f=new_workspace_form();f.reset("work");for (ready,busy) in [(false,false),(true,true)] { let v=f.view("W","P",ready,busy,2);assert!(!v.enabled&&!v.remove_enabled); }
        assert!(!f.view("W","P",true,false,1).remove_enabled);f.set_configuration_error(true);f.reset("work");assert_eq!(f.view("W","P",true,false,2).error,WORKSPACE_CONFIGURATION_ERROR);f.set_configuration_error(false);assert!(f.error.is_empty());
    }
    fn profile_options()->Vec<ffi::ProfileOption>{vec![ffi::ProfileOption{id:"used".into(),name:"Work".into()},ffi::ProfileOption{id:"spare".into(),name:"Spare".into()}]}
    fn profile_uses()->Vec<ffi::ProfileUse>{vec![ffi::ProfileUse{workspace:"work".into(),name:"Praca".into(),profile:"used".into()}]}
    #[test] fn profile_form_selects_by_id_across_reorder_and_catalogue_changes() {
        let mut f=new_profile_form();f.open("spare","");let mut profiles=profile_options();profiles.reverse();f.reconcile(&profiles,false);assert_eq!(f.controls(true,false,&profiles,&profile_uses()).selected,0);
        profiles.remove(0);f.reconcile(&profiles,false);assert_eq!(f.selected,"used");assert!(!f.controls(true,false,&profiles,&profile_uses()).remove);
        profiles.push(ffi::ProfileOption{id:"new".into(),name:"New".into()});f.reconcile(&profiles,true);assert_eq!(f.selected,"new");
    }
    #[test] fn profile_form_keeps_used_last_busy_and_unready_profiles_safe() {
        let mut f=new_profile_form();f.open("used","");let c=f.controls(true,false,&profile_options(),&profile_uses());assert!(!c.remove);assert!(c.status.contains("Praca"));assert!(c.rename);
        assert!(matches!(f.request(2,"",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::None));
        f.open("spare","");for (ready,busy) in [(false,false),(true,true)]{let c=f.controls(ready,busy,&profile_options(),&[]);assert!(!c.create&&!c.rename&&!c.remove&&!c.picker);}
        let mut profiles=profile_options();profiles.remove(0);assert!(matches!(f.request(2,"",true,false,&profiles,&[]).action,ffi::ProfileAction::None));
    }
    #[test] fn profile_form_requires_fresh_same_target_delete_confirmation() {
        let mut f=new_profile_form();f.open("spare","");assert!(matches!(f.request(2,"",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::ConfirmDelete));
        assert!(f.controls(true,false,&profile_options(),&profile_uses()).delete_label.starts_with("Potwierdź"));
        f.select("used");assert!(matches!(f.request(2,"",true,false,&profile_options(),&[]).action,ffi::ProfileAction::ConfirmDelete));
        let r=f.request(2,"",true,false,&profile_options(),&[]);assert!(matches!(r.action,ffi::ProfileAction::Delete));assert_eq!(r.id,"used");
        assert!(matches!(f.request(2,"",true,false,&profile_options(),&[]).action,ffi::ProfileAction::None));
    }
    #[test] fn profile_form_busy_and_usage_changes_disarm_confirmation() {
        let mut f=new_profile_form();f.open("spare","");f.request(2,"",true,false,&profile_options(),&[]);f.controls(true,true,&profile_options(),&[]);
        assert!(matches!(f.request(2,"",true,false,&profile_options(),&[]).action,ffi::ProfileAction::ConfirmDelete));
        let uses=vec![ffi::ProfileUse{workspace:"new".into(),name:"Nowa".into(),profile:"spare".into()}];f.controls(true,false,&profile_options(),&uses);
        assert!(matches!(f.request(2,"",true,false,&profile_options(),&[]).action,ffi::ProfileAction::ConfirmDelete));
        f.reconcile(&profile_options(),false);assert!(matches!(f.request(2,"",true,false,&profile_options(),&[]).action,ffi::ProfileAction::ConfirmDelete));
    }
    #[test] fn profile_form_normalizes_names_preserves_ascii_duplicate_policy_and_targets() {
        let mut f=new_profile_form();f.open("spare","");assert!(matches!(f.request(0," \u{3000} ",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::None));
        assert!(matches!(f.request(0,"work",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::None));
        let r=f.request(1,"  Spare  ",true,false,&profile_options(),&profile_uses());assert!(matches!(r.action,ffi::ProfileAction::Rename));assert_eq!(r.id,"spare");f.finish_attempt(true);
        let r=f.request(0," Żółć 日本語 🦀 ",true,false,&profile_options(),&profile_uses());assert_eq!(r.name,"Żółć 日本語 🦀");
    }
    #[test] fn profile_form_rechecks_attach_context_and_preserves_service_status() {
        let mut f=new_profile_form();f.open("spare","work");assert!(!f.controls(true,false,&profile_options(),&profile_uses()).rename);
        assert!(matches!(f.request(0,"Name",true,false,&profile_options(),&[]).action,ffi::ProfileAction::None));
        let r=f.request(0,"Name",true,false,&profile_options(),&profile_uses());assert_eq!(r.workspace,"work");f.set_status("Tworzenie profilu…");f.finish_attempt(true);assert_eq!(f.status(),"Tworzenie profilu…");
        f.request(0,"Other",true,false,&profile_options(),&profile_uses());f.set_status("Błąd usługi");f.finish_attempt(false);assert_eq!(f.status(),"Błąd usługi");
    }
    #[test] fn profile_paragraph_layout_retains_lines_and_bold_scene_text() {
        let g=paragraph_layout(300,18,3);assert_eq!((g.width,g.height,g.children[2].y),(300,54,36));
        let mut text=row(300.0);text.kind=17;text.active=true;let ops=layout(&[text]);assert!(ops[0].bold);assert_eq!(ops[0].text,"Zażółć 日本語 🦀");
    }
    #[test] fn folder_notice_preserves_destructive_copy_and_safe_default() {
        let f=new_folder_notice("work","folder","Zażółć 日本語 🦀",false);let copy=f.copy();assert_eq!(copy.title,"Usunąć folder „Zażółć 日本語 🦀”?");assert!(copy.default_cancel);assert_eq!(copy.cancel,"Anuluj");
        assert!(copy.body.contains("Tej operacji nie można cofnąć."));assert!(copy.body.contains("Strony już otwarte w kartach pozostaną otwarte."));
    }
    #[test] fn folder_notice_delete_revalidates_source_busy_and_live_folder() {
        let nodes=folder_nodes();let mut f=new_folder_notice("work","parent","Folder",false);let r=f.submit("work",true,false,&nodes);assert!(matches!(r.action,ffi::FolderNoticeAction::Delete));assert_eq!((r.workspace.as_str(),r.folder.as_str()),("work","parent"));
        for (current,ready,busy,id) in [("personal",true,false,"parent"),("work",false,false,"parent"),("work",true,true,"parent"),("work",true,false,"pin"),("work",true,false,"missing"),("work",true,false,"")] {
            let mut f=new_folder_notice("work",id,"Folder",false);assert!(matches!(f.submit(current,ready,busy,&nodes).action,ffi::FolderNoticeAction::None));assert!(!f.error().is_empty());
        }
    }
    #[test] fn folder_notice_admission_rejection_retry_cancel_and_duplicate() {
        let mut f=new_folder_notice("work","parent","Folder",false);f.submit("work",true,false,&folder_nodes());assert!(matches!(f.submit("work",true,false,&folder_nodes()).action,ffi::FolderNoticeAction::None));
        f.finish_attempt(false);assert!(!f.error().is_empty());assert!(matches!(f.submit("work",true,false,&folder_nodes()).action,ffi::FolderNoticeAction::Delete));f.finish_attempt(true);assert!(matches!(f.submit("work",true,false,&folder_nodes()).action,ffi::FolderNoticeAction::None));
        let mut f=new_folder_notice("work","parent","Folder",false);f.cancel();assert!(matches!(f.submit("work",true,false,&folder_nodes()).action,ffi::FolderNoticeAction::None));
    }
    #[test] fn folder_notice_error_acknowledgement_never_deletes() {
        let mut f=new_folder_notice("work","parent","Folder",true);let copy=f.copy();assert_eq!(copy.accept,"OK");assert!(copy.cancel.is_empty());assert!(!copy.default_cancel);
        let r=f.submit("personal",false,true,&[]);assert!(matches!(r.action,ffi::FolderNoticeAction::Acknowledge));assert!(r.workspace.is_empty()&&r.folder.is_empty());f.finish_attempt(true);assert!(matches!(f.submit("work",true,false,&folder_nodes()).action,ffi::FolderNoticeAction::None));
    }
    #[test] fn folder_notice_layout_preserves_all_shaped_lines_and_separates_errors() {
        let g=notice_layout(-1,18,3,16,2);assert_eq!((g.width,g.height,g.body.height,g.error.y,g.error.height),(400,94,54,62,32));assert_eq!(g.body_lines.iter().map(|r|r.y).collect::<Vec<_>>(),vec![0,18,36]);assert_eq!(g.error_lines[1].y,78);
        assert_eq!(notice_layout(200,18,3,16,0).height,54);assert_eq!(notice_layout(200,18,0,16,1).height,16);
    }
    #[test] fn folder_notice_layout_handles_empty_narrow_and_saturated_metrics() {
        let g=notice_layout(0,18,0,16,0);assert_eq!((g.width,g.height),(0,0));
        let g=notice_layout(5,i32::MAX,2,i32::MAX,2);assert_eq!(g.height,i32::MAX);assert_eq!(g.body_lines[1].height,0);assert!(g.error_lines.iter().all(|r|r.height==0));
    }
    fn folder_nodes()->Vec<ffi::TreeNode>{vec![ffi::TreeNode{id:"parent".into(),parent:String::new(),folder:true},ffi::TreeNode{id:"pin".into(),parent:String::new(),folder:false}]}
    #[test] fn folder_form_normalizes_unicode_and_preserves_byte_limit() {
        let mut form=new_folder_form("work","",""," \u{3000}Zażółć 日本語 🦀\u{a0} ");
        let r=form.submit("work",true,false,&[],false);assert!(matches!(r.action,ffi::FolderAction::Create));assert_eq!(r.name,"Zażółć 日本語 🦀");
        let mut form=new_folder_form("work","","",&"ą".repeat(128));assert!(matches!(form.submit("work",true,false,&[],false).action,ffi::FolderAction::Create));
        let mut form=new_folder_form("work","","",&"ą".repeat(129));assert!(matches!(form.submit("work",true,false,&[],false).action,ffi::FolderAction::None));assert!(!form.error().is_empty());
        let mut form=new_folder_form("work","","","\u{200b}");assert_eq!(form.submit("work",true,false,&[],false).name,"\u{200b}");
    }
    #[test] fn folder_form_handles_empty_composition_and_editing_without_submission() {
        let mut f=new_folder_form("work","",""," \u{3000}\u{a0} ");assert!(matches!(f.submit("work",true,false,&[],false).action,ffi::FolderAction::None));assert_eq!(f.error(),"Wpisz nazwę folderu.");
        f.edit("Nowy 🦀");assert!(f.error().is_empty());assert!(matches!(f.submit("work",true,false,&[],true).action,ffi::FolderAction::None));assert!(f.error().is_empty());
        assert_eq!(f.submit("work",true,false,&[],false).name,"Nowy 🦀");
    }
    #[test] fn folder_form_keeps_target_copy_and_checks_live_folder_context() {
        let mut f=new_folder_form("work","parent","","Child");assert_eq!(f.copy().title,"Nowy podfolder");let r=f.submit("work",true,false,&folder_nodes(),false);assert!(r.expand_parent);assert_eq!((r.workspace.as_str(),r.parent.as_str()),("work","parent"));
        let mut f=new_folder_form("work","","parent","Rename");assert_eq!(f.copy().accept,"Zapisz nazwę");assert!(matches!(f.submit("work",true,false,&folder_nodes(),false).action,ffi::FolderAction::Rename));
        for (current,ready,busy,parent,folder) in [("personal",true,false,"",""),("work",false,false,"",""),("work",true,true,"",""),("work",true,false,"pin",""),("work",true,false,"missing",""),("work",true,false,"","missing"),("work",true,false,"parent","parent")] {
            let mut f=new_folder_form("work",parent,folder,"Name");assert!(matches!(f.submit(current,ready,busy,&folder_nodes(),false).action,ffi::FolderAction::None));assert!(!f.error().is_empty());
        }
    }
    #[test] fn folder_form_retries_rejection_and_blocks_duplicate_or_cancelled_accept() {
        let mut f=new_folder_form("work","","","Name");f.submit("work",true,false,&[],false);assert!(matches!(f.submit("work",true,false,&[],false).action,ffi::FolderAction::None));
        f.finish_attempt(false);assert!(!f.error().is_empty());assert!(matches!(f.submit("work",true,false,&[],false).action,ffi::FolderAction::Create));f.finish_attempt(true);assert!(matches!(f.submit("work",true,false,&[],false).action,ffi::FolderAction::None));
        let mut f=new_folder_form("work","","","Name");f.cancel();assert!(matches!(f.submit("work",true,false,&[],false).action,ffi::FolderAction::None));
    }
    #[test] fn folder_form_layout_separates_editor_and_static_text_at_font_sizes() {
        let g=folder_form_layout(-1,17,20,17,false);assert_eq!((g.width,g.height,g.input.y,g.input.height,g.error.height),(400,57,25,32,0));
        let g=folder_form_layout(250,24,42,24,true);assert_eq!((g.height,g.input.y,g.error.y),(106,32,82));assert!(g.label.y+g.label.height<g.input.y);assert!(g.input.y+g.input.height<g.error.y);
        assert_eq!(folder_form_layout(0,17,32,17,true).input.width,0);let g=folder_form_layout(100,i32::MAX,i32::MAX,i32::MAX,true);assert_eq!(g.height,i32::MAX);assert_eq!(g.input.height,0);assert_eq!(g.error.height,0);
    }
    #[test] fn folder_form_static_text_uses_the_scene_recipe() {
        let mut item=row(300.0);item.kind=17;item.title="Wpisz nazwę folderu.".into();let ops=layout(&[item]);assert_eq!(ops.len(),1);assert_eq!(ops[0].text,"Wpisz nazwę folderu.");assert_eq!(ops[0].x,0.0);
    }
    #[test] fn row_targets_follow_identity_across_reindex_and_reject_reused_slots() {
        let tabs=vec![ffi::MenuTab{index:0,high:1,low:3},ffi::MenuTab{index:1,high:1,low:2}];
        let t=resolve_row_target("work","work",1,2,"",&tabs,&[]);assert!(t.valid&&t.has_tab);assert_eq!(t.tab,1);
        let tabs=vec![ffi::MenuTab{index:0,high:2,low:2}];assert!(!resolve_row_target("work","work",1,2,"",&tabs,&[]).valid);
        assert!(!resolve_row_target("work","personal",2,2,"",&tabs,&[]).valid);
    }
    #[test] fn row_targets_reject_folders_ambiguity_missing_and_empty_workspace() {
        let pins=vec![ffi::TreeNode{id:"folder".into(),parent:String::new(),folder:true},ffi::TreeNode{id:"pin".into(),parent:String::new(),folder:false}];
        let t=resolve_row_target("work","work",0,0,"pin",&[],&pins);assert!(t.valid&&!t.has_tab);assert_eq!(t.pin,"pin");
        for (source,high,low,pin) in [("work",0,0,"folder"),("work",0,0,"missing"),("work",1,2,"pin"),("work",0,0,""),("",0,0,"pin")] {assert!(!resolve_row_target(source,"work",high,low,pin,&[],&pins).valid);}
    }
    #[test] fn sidebar_drop_uses_physical_right_half_and_exclusive_edges() {
        let r=||ffi::Rect{x:-20,y:30,width:101,height:50};
        assert!(!sidebar_drop_accepts(r(),29,30,true));assert!(sidebar_drop_accepts(r(),30,30,true));assert!(sidebar_drop_accepts(r(),80,79,true));
        assert!(!sidebar_drop_accepts(r(),81,79,true));assert!(!sidebar_drop_accepts(r(),30,80,true));assert!(!sidebar_drop_accepts(r(),30,29,true));assert!(!sidebar_drop_accepts(r(),30,30,false));
        assert!(!sidebar_drop_accepts(ffi::Rect{x:0,y:0,width:0,height:50},0,0,true));
        assert!(sidebar_drop_accepts(ffi::Rect{x:i32::MAX-20,y:0,width:100,height:20},i32::MAX,0,true)==false);
    }
    #[test] fn tab_menu_preserves_order_labels_and_orientation() {
        let mut s=menu_state();let entries=tab_context_menu(&s);assert_eq!(entries.iter().map(|e|e.command).collect::<Vec<_>>(),vec![36,0,20,7,8,0,9,0,6,10,0,23,24]);assert_eq!(entries[3].label,"Przesuń w lewo");
        s.vertical=true;let entries=tab_context_menu(&s);assert_eq!(entries[3].label,"Przesuń w górę");assert_eq!(entries[6].label,"Podział poziomy (obok siebie)");
        s.has_tab=false;s.http=false;let entries=tab_context_menu(&s);assert_eq!(entries.iter().map(|e|e.command).collect::<Vec<_>>(),vec![36,0,20,5]);
    }
    #[test] fn tab_menu_actions_revalidate_split_side_readiness_and_last_tab() {
        use ffi::SidebarAction as A;let mut s=menu_state();
        assert!(matches!(tab_menu_action(7,&s),A::MoveFirst));assert!(matches!(tab_menu_action(8,&s),A::None));assert!(matches!(tab_menu_action(9,&s),A::ToggleOrientation));
        s.tab=3;assert!(matches!(tab_menu_action(8,&s),A::MoveSecond));assert!(matches!(tab_menu_action(7,&s),A::None));
        s.split=false;assert!(matches!(tab_menu_action(6,&s),A::None));assert!(matches!(tab_menu_action(5,&s),A::OpenSplit));
        s.tab_count=1;assert!(matches!(tab_menu_action(10,&s),A::None));s.tab_count=2;assert!(matches!(tab_menu_action(10,&s),A::CloseTab));
        for mode in 0..3 {s.valid=mode!=0;s.ready=mode!=1;s.busy=mode==2;for command in 5..=10 {assert!(matches!(tab_menu_action(command,&s),A::None));}}
    }
    #[test] fn workspace_switch_motion_preserves_direction_and_duration() {
        let next=workspace_switch_motion(0,3,true,true,false);assert!(next.enabled);assert_eq!((next.offset,next.duration_ms),(32.0,180));
        let previous=workspace_switch_motion(3,0,true,true,false);assert!(previous.enabled);assert_eq!((previous.offset,previous.duration_ms),(-32.0,180));
        let far=workspace_switch_motion(0,i32::MAX,true,true,false);assert_eq!(far.offset,32.0);
    }
    #[test] fn workspace_switch_motion_respects_motion_visibility_and_identity_guards() {
        for (old,new,visible,enabled,reduced) in [(0,0,true,true,false),(-1,1,true,true,false),(0,-1,true,true,false),(0,1,false,true,false),(0,1,true,false,false),(0,1,true,true,true)] {
            let motion=workspace_switch_motion(old,new,visible,enabled,reduced);assert!(!motion.enabled);assert_eq!((motion.offset,motion.duration_ms),(0.0,0));
        }
    }
    #[test] fn workspace_preview_rows_keep_order_gaps_indent_and_partial_clip() {
        let rows=vec![ffi::PreviewRow{index:0,kind:1,depth:0,height:13},ffi::PreviewRow{index:1,kind:0,depth:2,height:34},ffi::PreviewRow{index:0,kind:3,depth:0,height:8},ffi::PreviewRow{index:2,kind:0,depth:0,height:34}];
        let p=workspace_preview_layout(201,60,20,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![1,2]);
        assert_eq!((p[0].bounds.x,p[0].bounds.y,p[0].bounds.width),(24,-5,177));
        assert_eq!((p[0].clip.x,p[0].clip.y,p[0].clip.height),(24,0,29));
        assert_eq!((p[1].bounds.y,p[1].clip.height),(39,21));
    }
    #[test] fn workspace_preview_pairs_share_live_split_geometry_and_outer_clipping() {
        let rows=vec![ffi::PreviewRow{index:5,kind:2,depth:1,height:0}];let p=workspace_preview_layout(201,40,3,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![5,6,7]);
        let g=split_layout(189,46,2);
        for (i,r) in g.children.iter().enumerate(){assert_eq!((p[i+1].bounds.x,p[i+1].bounds.y,p[i+1].bounds.width,p[i+1].bounds.height),(12+r.x,-3+r.y,r.width,r.height));}
        assert_eq!((p[0].clip.y,p[0].clip.height),(0,40));
    }
    #[test] fn workspace_preview_large_source_scroll_culls_offscreen_rows() {
        let rows:Vec<_>=(0..1000).map(|index|ffi::PreviewRow{index,kind:0,depth:0,height:0}).collect();
        let p=workspace_preview_layout(240,72,36*800,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![800,801]);
        assert!(p.iter().all(|p|p.clip.y>=0&&p.clip.y+p.clip.height<=72));
    }
    #[test] fn workspace_preview_clamps_deep_narrow_and_saturated_input() {
        let rows=vec![ffi::PreviewRow{index:0,kind:0,depth:99,height:34}];
        assert!(workspace_preview_layout(20,100,0,&rows).is_empty());
        let p=workspace_preview_layout(240,100,-5,&rows);assert_eq!((p[0].bounds.x,p[0].bounds.y),(96,0));
        assert!(workspace_preview_layout(0,0,0,&rows).is_empty());
        let rows=vec![ffi::PreviewRow{index:0,kind:3,depth:0,height:i32::MAX},ffi::PreviewRow{index:1,kind:0,depth:0,height:34}];
        assert!(workspace_preview_layout(240,100,0,&rows).is_empty());
    }
    #[test] fn workspace_preview_uses_unicode_row_display_list_without_native_labels() {
        let p=workspace_preview_layout(240,30,4,&[ffi::PreviewRow{index:0,kind:0,depth:0,height:34}]);
        let mut item=row(240.0);item.title="Zażółć 日本語 مرحبا 🦀".into();item.x=p[0].bounds.x as f32;item.y=p[0].bounds.y as f32;
        let c=&p[0].clip;item.clip=ffi::FloatRect{x:c.x as f32,y:c.y as f32,width:c.width as f32,height:c.height as f32};
        let ops=layout(&[item]);assert!(ops.iter().any(|p|p.text=="Zażółć 日本語 مرحبا 🦀"));assert!(ops.iter().any(|p|p.text=="×"));
        assert!(ops.iter().all(|p|p.clip.y==0.0&&p.clip.height==30.0));
    }
    #[test] fn workspace_swipe_reverses_skips_and_rearms_at_same_cursor() {
        let mut s=new_workspace_swipe();
        let u=s.event(0.0,0.0,200,0,4,true,false,false,false);assert!(u.started&&!u.handled);
        let u=s.event(-90.0,0.0,200,0,4,false,false,false,false);assert!(u.preview&&u.handled);assert!((s.position()-0.45).abs()<1e-9);
        s.event(80.0,0.0,200,0,4,false,false,false,false);assert!((s.position()-0.05).abs()<1e-9);
        let u=s.event(0.0,0.0,200,0,4,false,true,false,false);assert!(u.finish&&u.handled&&!s.tracking());assert_eq!(s.target(4),0);
        s.event(-240.0,0.0,200,0,4,true,false,false,false);s.event(-320.0,0.0,200,0,4,false,false,false,false);
        assert!((s.position()-2.8).abs()<1e-9);assert_eq!(s.target(4),3);
        s.event(0.0,0.0,200,0,4,false,true,false,false);
        s.event(620.0,0.0,200,3,4,true,false,false,false);assert_eq!(s.target(4),0);
    }
    #[test] fn workspace_swipe_axis_thresholds_and_vertical_lock() {
        let mut s=new_workspace_swipe();s.event(0.0,0.0,200,0,4,true,false,false,false);
        assert!(!s.event(-11.0,0.0,200,0,4,false,false,false,false).preview);
        assert!(s.event(-1.0,0.0,200,0,4,false,false,false,false).handled);
        s.event(0.0,0.0,200,0,4,true,false,false,false);
        assert!(!s.event(0.0,-12.0,200,0,4,false,false,false,false).handled);
        let u=s.event(-600.0,0.0,200,0,4,false,false,false,false);assert!(!u.preview&&!u.handled);assert_eq!(s.position(),0.0);
        s.event(-15.0,10.0,200,0,4,true,false,false,false);assert_eq!(s.position(),0.075);
        // Horizontal lock survives subsequent vertical movement.
        assert!(s.event(0.0,100.0,200,0,4,false,false,false,false).handled);
    }
    #[test] fn workspace_swipe_momentum_finishes_once_and_closed_stream_stays_closed() {
        let mut s=new_workspace_swipe();s.event(-140.0,0.0,200,0,4,true,false,false,false);
        let u=s.event(-999.0,0.0,200,0,4,false,false,true,false);assert!(u.finish&&u.handled&&!s.tracking());assert_eq!(s.position(),0.7);assert_eq!(s.target(4),1);
        let u=s.event(999.0,0.0,200,0,4,false,false,true,false);assert!(!u.finish&&!u.started&&u.handled);assert_eq!(s.position(),0.7);
        let u=s.event(-100.0,0.0,200,0,4,false,false,false,false);assert!(!u.started&&!u.preview);
        s.cancel();assert!(!s.tracking());assert!(!s.event(-100.0,0.0,200,0,4,false,true,false,false).started);
        assert!(s.event(0.0,0.0,200,1,4,true,false,false,false).started);assert_eq!(s.position(),1.0);
    }
    #[test] fn workspace_swipe_phase_less_idle_finish_and_rubber_band() {
        let mut s=new_workspace_swipe();let u=s.event(-120.0,0.0,200,0,4,false,false,false,true);assert!(u.started&&u.handled&&s.phase_less());
        s.finish();assert_eq!(s.target(4),1);
        assert!(s.event(-200.0,0.0,200,1,4,false,false,false,true).started);assert_eq!(s.position(),2.0);
        s.event(10000.0,0.0,200,0,4,true,false,false,false);assert_eq!(s.position(),-0.22);assert_eq!(s.target(4),0);
        s.event(-10000.0,0.0,200,3,4,true,false,false,false);assert_eq!(s.position(),3.22);assert_eq!(s.target(4),3);
    }
    #[test] fn workspace_swipe_preview_plan_keeps_only_neighbors_and_fractional_offsets() {
        let p=swipe_layout(2.8,6,201);assert_eq!(p.center,3);assert_eq!(p.pages.iter().map(|p|p.index).collect::<Vec<_>>(),vec![2,3,4]);
        for page in p.pages{assert!((page.x-(page.index as f64-2.8)*201.0).abs()<1e-9);}
        assert_eq!(swipe_layout(-0.22,6,200).pages.len(),2);
        assert_eq!(swipe_layout(5.22,6,200).pages.len(),2);
        assert_eq!(swipe_layout(0.5,6,200).center,1);
        let empty=swipe_layout(0.0,0,200);assert_eq!(empty.center,-1);assert!(empty.pages.is_empty());
    }
    #[test] fn workspace_swipe_invalid_input_cannot_poison_transforms() {
        let mut s=new_workspace_swipe();assert!(!s.event(0.0,0.0,0,0,0,true,false,false,false).started);
        s.event(0.0,0.0,0,0,4,true,false,false,false);
        assert!(!s.event(f32::NAN,0.0,0,0,4,false,false,false,false).preview);
        assert!(!s.event(f32::INFINITY,0.0,0,0,4,false,false,false,false).preview);
        s.event(-20.0,0.0,0,0,4,false,false,false,false);assert!(s.position().is_finite());
        let p=swipe_layout(f64::NAN,4,-1);assert!(p.pages.iter().all(|p|p.x==0.0));assert_eq!(s.target(0),-1);
    }
    #[test] fn palette_static_text_owns_padding_and_tight_bounds() {
        let e=palette_text_layout(0,-1,-1,20,13);assert_eq!((e.width,e.height,e.children[0].x,e.children[0].y),(22,15,1,1));
        let h=palette_text_layout(1,640,34,280,13);assert_eq!((h.children[0].x,h.children[0].y,h.children[0].width,h.children[0].height),(18,11,280,13));
        let r=palette_text_layout(2,-1,-1,200,15);assert_eq!((r.width,r.height,r.children[0].y),(236,31,8));
        assert_eq!(palette_text_layout(2,0,0,200,15).children[0].width,0);
    }
    #[test] fn palette_chrome_has_no_empty_text_ink_and_frames_use_inset_strokes() {
        let mut e=row(400.0);e.kind=15;e.glyph=2;e.title.clear();assert!(layout(&[e]).is_empty());
        let mut h=row(400.0);h.kind=15;h.glyph=1;h.title_width=80.0;h.font_height=13.0;h.height=34.0;
        let ops=layout(&[h]);assert_eq!((ops[0].x,ops[0].y,ops[0].width),(18.0,11.0,80.0));
        let mut f=row(400.0);f.kind=16;let ops=layout(&[f]);assert_eq!((ops[0].radius,ops[1].stroke),(14.0,1.0));
    }
    #[test] fn palette_search_decoration_is_the_analytic_search_shape() {
        let mut s=row(32.0);s.kind=7;s.glyph=8;assert!(layout(&[s]).iter().any(|p|p.shape==7&&p.text.is_empty()));
    }
    #[test] fn palette_selection_wraps_and_reconciles_async_identity() {
        let mut p=new_palette_selection();assert!(!p.step(0,false));assert!(!p.moved());
        assert!(p.step(4,true));assert_eq!(p.selected(),3);assert!(p.step(4,false));assert_eq!(p.selected(),0);
        p.step(4,false);p.reconcile(7,5,true);assert_eq!(p.selected(),5);p.reconcile(2,0,false);assert_eq!(p.selected(),1);
        p.reconcile(0,0,false);assert_eq!(p.selected(),0);p.reset();assert!(!p.moved());
    }
    #[test] fn palette_geometry_preserves_editor_space_and_anchor() {
        let g=palette_input_layout(640,56,ffi::Rect{x:0,y:0,width:32,height:32},22,ffi::Rect{x:0,y:0,width:20,height:16});
        assert_eq!((g.icon.x,g.input.x,g.input.width,g.hint.x),(18,62,528,602));
        assert_eq!((palette_width(560),palette_results_height(560)),(512,300));
        let r=palette_frame(ffi::Rect{x:264,y:38,width:728,height:754},640,300);assert_eq!((r.x,r.y),(308,163));
    }
    #[test] fn palette_result_text_geometry_and_analytic_glyphs() {
        let mut r=row(400.0);r.kind=12;r.height=44.0;r.title_width=80.0;r.detail="History / 日本語".into();
        let ops=layout(&[r]);let title=ops.iter().find(|p|p.bold&&!p.text.is_empty()).unwrap();assert_eq!((title.x,title.width),(52.0,80.0));
        assert!(ops.iter().any(|p|p.middle&&p.text=="History / 日本語"));
        for (glyph,shape) in [(0,7),(1,3),(2,6)]{let mut g=row(400.0);g.kind=13;g.glyph=glyph;assert_eq!(layout(&[g])[0].shape,shape);}
    }
    #[test] fn sidebar_shell_layout_matches_pinned_and_overlay_bounds() {
        let p=shell_layout(1000,800,248,true);assert_eq!((p.content.x,p.content.width,p.sidebar.width,sidebar_grip_layout(248,754).x),(264,728,248,242));
        let overlay=shell_layout(560,560,480,false);assert_eq!((overlay.content.x,overlay.content.width,overlay.sidebar.width),(8,544,280));
        assert_eq!(shell_layout(0,0,480,true).content.width,0);
        assert_eq!(sidebar_grip_layout(i32::MIN,0).width,0);
        let mut grip=row(6.0);grip.kind=11;grip.height=200.0;grip.hovered=false;grip.active=false;assert!(layout(&[grip]).is_empty());
        let mut grip=row(6.0);grip.kind=11;grip.height=200.0;grip.hovered=true;let ops=layout(&[grip]);assert_eq!((ops[0].x,ops[0].y,ops[0].width,ops[0].height),(3.0,8.0,2.0,184.0));
    }
    #[test] fn sidebar_drag_is_anchored_clamped_and_cancelled() {
        let mut d=new_sidebar_drag();d.begin(248,100.0);assert_eq!(d.update(140.0,false).width,288);
        assert_eq!(d.update(1000.0,false).width,480);assert_eq!(d.update(-1000.0,false).width,200);assert!(d.end().changed);
        assert!(!d.update(200.0,false).changed);d.begin(248,0.0);assert_eq!(d.delta(20.0,true).width,228);d.cancel();assert!(!d.end().changed);
        d.begin(248,0.0);assert!(!d.end().changed);assert_eq!(sidebar_resize_width(i32::MAX,i32::MAX),480);
    }
    #[test] fn sidebar_auto_hide_respects_all_guards() {
        assert!(sidebar_auto_hide(false,false,false,false));
        for (p,b,i,d) in [(true,false,false,false),(false,true,false,false),(false,false,true,false),(false,false,false,true)] {assert!(!sidebar_auto_hide(p,b,i,d));}
    }
    #[test] fn pane_layout_separates_real_page_rects_in_both_orientations() {
        let g=pane_host_layout(809,600,0.5,false,true);
        assert_eq!((g.first.width,g.second.x,g.second.width,g.divider.width),(400,408,401,8));
        assert_eq!((g.first.y,g.first.height),(30,570));
        let v=pane_host_layout(600,809,0.5,true,true);
        assert_eq!((v.first.height,v.second.y,v.second.height,v.divider.y),(370,438,371,400));
        let single=pane_host_layout(600,809,0.5,false,false);assert_eq!(single.first.height,809);assert_eq!(single.first_bar.height,0);
        for width in [0,4,54,120] {
            let b=pane_bar_layout(width,30);assert!(b.menu.x+b.menu.width<=b.close.x);assert!(b.close.x+b.close.width<=width);
            assert!(pane_host_layout(width,4,f64::NAN,true,true).second.height>=0);
        }
    }
    #[test] fn split_drag_clamps_commits_and_rejects_stale_or_cancelled_events() {
        let mut drag=new_split_drag();drag.begin(0.5,100.0);
        assert_eq!(drag.update(200.0,508,false).ratio,0.7);
        assert_eq!(drag.update(10000.0,508,false).ratio,0.8);assert!(drag.end().changed);
        assert!(!drag.update(300.0,508,false).changed);
        drag.begin(0.5,100.0);assert_eq!(drag.delta(100.0,508,true).ratio,0.3);drag.cancel();assert!(!drag.end().changed);
        drag.begin(f64::NAN,0.0);assert!(!drag.end().changed);
    }
    #[test] fn pane_title_uses_tight_text_bounds_and_split_icons_have_no_label_text() {
        let mut item=row(400.0);item.kind=8;item.title_width=70.0;
        let ops=layout(&[item]);assert_eq!((ops[0].x,ops[0].width),(12.0,70.0));
        let mut empty=row(400.0);empty.kind=8;empty.title.clear();empty.title_width=2.0;assert!(layout(&[empty]).is_empty());
        let mut more=row(32.0);more.kind=7;more.glyph=6;more.fill=0;more.focused=false;
        let ops=layout(&[more]);assert_eq!(ops.len(),3);assert!(ops.iter().all(|p|p.shape==0&&p.text.is_empty()));
        let mut close=row(32.0);close.kind=7;close.glyph=7;
        assert!(layout(&[close]).iter().any(|p|p.shape==5));
    }
    fn viewport_node(parent: usize, x: f32, y: f32, width: f32, height: f32, visible: bool) -> ffi::ViewportNode {
        ffi::ViewportNode {parent, bounds: ffi::FloatRect {x, y, width, height}, visible}
    }
    #[test] fn viewport_preserves_fractional_scroll_and_card_geometry() {
        let mut v = new_viewport();
        let nodes = [viewport_node(usize::MAX, 0.0, 0.0, 200.0, 1000.0, true),
            viewport_node(0, 12.0, 36.0, 180.0, 34.0, true),
            viewport_node(0, 0.0, 800.0, 200.0, 34.0, true)];
        let p = v.project(200.0, 1000.0, 200.0, 100.0, 0.0, 45.25, &nodes);
        assert_eq!(p.y, 45.25); assert_eq!(p.nodes.len(), 2);
        let row = &p.nodes[1];
        assert_eq!((row.bounds.x, row.bounds.y, row.bounds.width, row.bounds.height), (12.0, -9.25, 180.0, 34.0));
        assert_eq!((row.clip.x, row.clip.y, row.clip.width, row.clip.height), (12.0, 0.0, 180.0, 24.75));
    }
    #[test] fn viewport_inherits_visibility_and_all_parent_clips() {
        let mut v = new_viewport();
        let nodes = [viewport_node(usize::MAX, 0.0, 0.0, 200.0, 500.0, true),
            viewport_node(0, 24.0, 10.0, 100.0, 50.0, true),
            viewport_node(1, -12.0, 30.0, 150.0, 34.0, true),
            viewport_node(0, 0.0, 0.0, 200.0, 200.0, false),
            viewport_node(3, 0.0, 0.0, 100.0, 34.0, true)];
        let p = v.project(200.0, 500.0, 200.0, 100.0, 0.0, 0.0, &nodes);
        assert_eq!(p.nodes.len(), 3);
        let child = &p.nodes[2];
        assert_eq!((child.bounds.x, child.bounds.y), (12.0, 40.0));
        assert_eq!((child.clip.x, child.clip.y, child.clip.width, child.clip.height), (24.0, 40.0, 100.0, 20.0));
    }
    #[test] fn viewport_clamps_offsets_after_shrink_and_rejects_invalid_geometry() {
        let mut v = new_viewport();
        assert_eq!(v.project(200.0, 1000.0, 200.0, 100.0, -20.0, 9000.0, &[]).y, 900.0);
        let shrunk = v.project(200.0, 40.0, 200.0, 100.0, f32::NAN, 900.0, &[]);
        assert_eq!((shrunk.x, shrunk.y), (0.0, 0.0));
        let nodes = [viewport_node(usize::MAX, f32::NAN, 0.0, 200.0, 100.0, true),
            viewport_node(7, 0.0, 0.0, 200.0, 34.0, true),
            viewport_node(usize::MAX, 0.0, 0.0, -1.0, 34.0, true)];
        assert!(v.project(200.0, 100.0, 200.0, 100.0, f32::INFINITY, -1.0, &nodes).nodes.is_empty());
        assert!(v.project(f32::INFINITY, 100.0, 0.0, -1.0, 0.0, 0.0, &nodes).nodes.is_empty());
    }
    #[test] fn every_draw_primitive_keeps_its_item_clip() {
        let mut item = row(200.0); item.y = -9.25; item.clip.height = 24.75;
        let ops = layout(&[item]); assert!(!ops.is_empty());
        assert!(ops.iter().all(|op| op.clip.y == 0.0 && op.clip.height == 24.75));
    }
    fn node(id: &str, parent: &str, folder: bool) -> ffi::TreeNode {
        ffi::TreeNode {id: id.into(), parent: parent.into(), folder}
    }
    #[test] fn tree_orders_siblings_and_retains_hidden_descendants() {
        let mut tree = new_sidebar_tree();
        let nodes = [node("child", "root", true), node("leaf", "child", false),
            node("root", "", true), node("sibling", "root", false), node("other", "", false)];
        let plan = tree.tree_plan("work", &nodes);
        assert_eq!(plan.iter().map(|e| e.index).collect::<Vec<_>>(), [2, 0, 1, 3, 4]);
        assert_eq!(plan.iter().map(|e| e.depth).collect::<Vec<_>>(), [0, 1, 2, 1, 0]);
        assert_eq!(plan[0].parent, usize::MAX); assert_eq!(plan[1].parent, 2);
        assert!(plan.iter().all(|e| e.visible));
        assert!(tree.toggle("work", "child")); assert!(tree.toggle("work", "root"));
        let hidden = tree.tree_plan("work", &nodes);
        assert_eq!(hidden.len(), nodes.len());
        assert!(hidden[0].collapsed && hidden[1].collapsed);
        assert!(hidden[0].visible && !hidden[1].visible && !hidden[2].visible && !hidden[3].visible && hidden[4].visible);
        assert!(!tree.toggle("work", "root"));
        let expanded = tree.tree_plan("work", &nodes);
        assert!(expanded[1].visible && !expanded[2].visible && expanded[3].visible);
    }
    #[test] fn tree_state_survives_rebuilds_and_isolates_workspace_ids() {
        let mut tree = new_sidebar_tree(); tree.set_collapsed("a:b", "c", true);
        assert!(!tree.collapsed("a", "b:c"));
        let nodes = [node("c", "", true)];
        for _ in 0..3 {assert!(tree.tree_plan("a:b", &nodes)[0].collapsed);}
        assert!(!tree.tree_plan("other", &nodes)[0].collapsed);
        tree.forget_workspace("other"); assert!(tree.collapsed("a:b", "c"));
        tree.forget_workspace("a:b"); assert!(!tree.collapsed("a:b", "c"));
    }
    #[test] fn tree_rejects_unreachable_and_duplicate_nodes_and_bounds_depth() {
        let tree = new_sidebar_tree();
        let nodes = [node("", "", true), node("root", "", true), node("root", "root", true),
            node("orphan", "missing", false), node("cycle-a", "cycle-b", true),
            node("cycle-b", "cycle-a", true), node("leaf", "", false), node("bad-child", "leaf", false)];
        assert_eq!(tree.tree_plan("work", &nodes).iter().map(|e| e.index).collect::<Vec<_>>(), [1, 6]);
        let mut chain = Vec::new();
        for index in 0..70 {chain.push(node(&index.to_string(), if index == 0 {"".into()} else {(index - 1).to_string()}.as_str(), true));}
        let plan = tree.tree_plan("work", &chain);
        assert_eq!(plan.len(), 65); assert_eq!(plan.last().unwrap().depth, 64);
    }
    fn tab(index: usize, pin: &str) -> ffi::TabNode {ffi::TabNode {index, pin: pin.into()}}
    #[test] fn tab_plan_groups_once_in_pane_order_and_preserves_other_tabs() {
        let tree = new_sidebar_tree();
        let tabs = [tab(3, ""), tab(7, ""), tab(9, ""), tab(12, "")];
        let p = tree.tab_plan("work", &[], &tabs, 9, 3, true);
        assert_eq!(p.slots.len(), 3);
        assert!(p.slots[0].pair); assert_eq!((p.slots[0].first, p.slots[0].second), (2, 0));
        assert_eq!(p.slots[0].pin, usize::MAX);
        assert_eq!((p.slots[1].first, p.slots[2].first), (1, 3));
        assert!(!p.slots[1].pair && !p.slots[2].pair);
    }
    #[test] fn tab_plan_anchors_pinned_pairs_by_pane_order_even_in_collapsed_folders() {
        let mut tree = new_sidebar_tree(); tree.set_collapsed("work", "folder", true);
        let nodes = [node("folder", "", true), node("a", "folder", false), node("b", "", false)];
        let tabs = [tab(0, ""), tab(1, "a"), tab(2, "b")];
        let p = tree.tab_plan("work", &nodes, &tabs, 1, 2, true);
        assert_eq!(p.slots.len(), 2); assert_eq!(p.slots[1].pin, 1);
        assert!(p.pins[0].visible && !p.pins[1].visible && !p.pins[2].visible);
        let swapped = tree.tab_plan("work", &nodes, &tabs, 2, 1, true);
        assert_eq!((swapped.slots[1].first, swapped.slots[1].second, swapped.slots[1].pin), (2, 1, 2));
        let right_pinned = tree.tab_plan("work", &nodes, &tabs, 0, 1, true);
        assert_eq!(right_pinned.slots[0].pin, 1);
    }
    #[test] fn tab_plan_falls_back_for_invalid_pairs_and_unreachable_pins() {
        let tree = new_sidebar_tree();
        let nodes = [node("orphan", "missing", false), node("good", "", false)];
        let tabs = [tab(5, "orphan"), tab(8, "good"), tab(8, "duplicate")];
        let p = tree.tab_plan("work", &nodes, &tabs, 5, 8, true);
        assert_eq!(p.slots.len(), 1); assert_eq!(p.slots[0].pin, 1); assert_eq!(p.pins.len(), 1);
        for (first, second, split) in [(5, 8, false), (5, 5, true), (5, 100, true)] {
            let p = tree.tab_plan("work", &nodes, &tabs, first, second, split);
            assert_eq!(p.slots.len(), 2); assert!(p.slots.iter().all(|s| !s.pair));
            assert_eq!((p.slots[0].first, p.slots[1].first), (0, 1));
        }
        assert!(tree.tab_plan("work", &[], &[], 0, 1, true).slots.is_empty());
    }
    #[test] fn split_tiles_fill_odd_widths_and_respect_frame_and_order() {
        let natural = split_layout(-1, -1, 2);
        assert_eq!((natural.width, natural.height), (200, 46));
        for width in [200, 201, 247, 248, 479, 480, i32::MAX] {
            let r = split_layout(width, 46, 2);
            let a = &r.children[0]; let b = &r.children[1];
            assert_eq!((a.x, a.y, a.height), (4, 4, 38));
            assert_eq!(b.x, a.x + a.width + 4);
            assert_eq!(b.x + b.width, width - 4);
            assert!((a.width - b.width).abs() <= 1);
            assert_eq!((b.y, b.height), (4, 38));
        }
    }
    #[test] fn split_layout_handles_empty_single_and_tiny_containers() {
        assert!(split_layout(200, 46, 0).children.is_empty());
        let single = split_layout(201, 46, 1);
        assert_eq!((single.children[0].x, single.children[0].width), (4, 193));
        for width in 0..16 {
            for height in 0..10 {
                for count in 0..4 {
                    let r = split_layout(width, height, count);
                    assert_eq!(r.children.len(), count);
                    for tile in r.children {
                        assert!(tile.x >= 0 && tile.y >= 0);
                        assert!(tile.width >= 0 && tile.height >= 0);
                        assert!(tile.x + tile.width <= width);
                        assert!(tile.y + tile.height <= height);
                    }
                }
            }
        }
    }
    #[test] fn folder_indentation_is_bounded_even_for_invalid_depths() {
        assert_eq!(folder_indent(i32::MIN), 0);
        for depth in 0..=8 {assert_eq!(folder_indent(depth), depth * 12);}
        assert_eq!(folder_indent(9), 96); assert_eq!(folder_indent(i32::MAX), 96);
    }
    #[test] fn action_row_drag_retains_drop_without_click_and_pending_clears() {
        use ffi::{PointerPhase as P,RowAction as A};let mut row=new_action_control();
        assert!(!row.action_pending());row.action_row_pointer(P::Press,1,1,true,true,true,true);assert!(row.action_pending());
        row.action_row_pointer(P::Drag,30,1,false,true,true,true);assert!(row.action_row_pointer(P::Release,30,1,false,true,true,true).action==A::Drop);assert!(!row.action_pending());
        row.action_row_pointer(P::Press,1,1,true,true,true,false);row.action_row_pointer(P::Drag,30,1,true,true,true,false);assert!(row.action_row_pointer(P::Release,30,1,true,true,true,false).action==A::Activate);
    }
    #[test] fn action_row_pending_input_is_retired_by_clip_disable_and_capture_loss() {
        use ffi::{PointerPhase as P,RowAction as A};let mut row=new_action_control();
        for cancel in 0..3{row.action_row_pointer(P::Press,1,1,true,true,true,true);assert!(row.action_pending());match cancel{0=>{row.action_row_pointer(P::Drag,30,1,false,true,false,true);},1=>{row.action_enabled(false);row.action_enabled(true);},_=>{row.action_cancel();}}
            assert!(!row.action_pending());assert!(row.action_row_pointer(P::Release,1,1,true,true,true,true).action!=A::Activate);assert!(!row.action_pending());}
        row.action_gesture(0,true);assert!(row.action_pending());row.action_gesture(2,true);assert!(!row.action_pending());
    }
    #[test] fn pointer_activation_requires_a_valid_press_and_cancels_outside() {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
        assert!(p.pointer_event(P::Press, 0, 0, true, true, true, false).visual == V::Pressed);
        assert!(p.pointer_event(P::Release, 0, 0, false, true, true, false).action == A::Cancel);
        p.pointer_event(P::Press, 0, 0, true, true, true, false);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Activate);
        p.pointer_event(P::Press, 0, 0, true, true, false, false);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, false, false).action == A::Cancel);
    }
    #[test] fn drag_and_disable_never_leak_clicks() {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        p.pointer_event(P::Press, 0, 0, true, true, true, true);
        p.pointer_event(P::Drag, 10, 0, false, true, true, true);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, true).action == A::Drop);
        p.pointer_event(P::Press, 0, 0, true, true, true, true);
        assert!(p.pointer_event(P::Move, 0, 0, true, false, false, false).visual == V::Disabled);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
        p.pointer_event(P::Press, 0, 0, true, true, true, true); p.cancel();
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
    }
    #[test] fn semantic_keys_follow_platform_conventions() {
        assert_eq!(key_policy(1, true), 1); assert_eq!(key_policy(2, true), 0);
        assert_eq!(key_policy(0, true), 0); assert_eq!(key_policy(1, false), 2);
        assert_eq!(key_policy(2, false), 1); assert_eq!(key_policy(u32::MAX, false), 0);
    }
    #[test] fn platform_key_policies_and_focus_loss_are_respected() {
        use ffi::{RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        assert!(!p.key_event(true, 0, true).handled);
        assert!(p.key_event(true, 1, true).action == A::Activate);
        assert!(p.key_event(false, 1, true).action == A::None);
        assert!(p.key_event(true, 2, true).visual == V::Pressed);
        assert!(p.key_event(false, 2, true).action == A::Activate);
        p.key_event(true, 2, true); p.cancel();
        assert!(!p.key_event(false, 2, true).handled);
        assert!(!p.key_event(true, 1, false).handled);
    }
    #[test] fn row_geometry_reserves_close_and_detail_without_overlap() {
        for width in [90, 200, 248, 480] {
            let g = row_layout(width, 38, 100, true, false, true);
            assert!(g.title.x + g.title.width <= g.detail.x);
            assert!(g.detail.x + g.detail.width <= g.close.x);
            assert_eq!(g.close.x + g.close.width, width - 6);
        }
        let chip = row_layout(248, 40, 100, false, true, false);
        assert_eq!(chip.title.x, 52);
        assert_eq!(chip.close.width, 0);
        for width in [i32::MIN, 0, 30, i32::MAX] {
            let g = row_layout(width, -1, i32::MAX, true, true, true);
            assert!(g.title.width >= 0 && g.detail.width >= 0 && g.close.width >= 0);
            assert_eq!(g.title.height, 0);
        }
    }
    #[test] fn close_policy_preserves_hover_across_selection_changes() {
        assert!(close_visible(true, false, false));
        assert!(!close_visible(false, false, false));
        assert!(!close_visible(true, true, false));
        for active in [false, true] {
            for tile in [false, true] { assert!(close_visible(active, tile, true)); }
        }
    }
    #[test] fn row_pointer_distinguishes_click_drag_and_capture_loss() {
        let mut p = new_row_pointer();
        p.drag(100, 100, true); assert!(!p.release());
        p.press(10, 10); p.drag(18, 10, true); assert!(!p.release());
        p.press(10, 10); p.drag(19, 10, false); assert!(!p.release());
        p.press(10, 10); p.drag(19, 10, true); p.drag(10, 10, true);
        assert!(p.release()); assert!(!p.release());
        p.press(i32::MIN, i32::MIN); p.drag(i32::MAX, i32::MAX, true);
        assert!(p.release());
        p.press(0, 0); p.drag(100, 0, true); p.cancel(); assert!(!p.release());
    }
    #[test] fn unicode_and_live_title_are_preserved() {
        let r = layout(&[row(240.0)]);
        assert!(r.iter().any(|p| p.text == "Zażółć 日本語 🦀"));
        assert!(r.iter().any(|p| p.text == "×"));
        assert_eq!(r.iter().find(|p| p.text.starts_with("Zażółć")).unwrap().width, 172.0);
    }
    #[test] fn narrow_and_invalid_rows_never_emit_negative_geometry() {
        assert!(layout(&[row(f32::NAN)]).is_empty());
        assert!(layout(&[row(-1.0)]).is_empty());
        assert!(layout(&[row(30.0)]).iter().all(|p| p.width > 0.0));
        assert!(!layout(&[row(30.0)]).iter().any(|p| p.text.starts_with("Zażółć")));
    }
    #[test] fn split_close_appears_on_hover() {
        let mut item = row(100.0); item.tile = true; item.close_visible = false;
        assert!(!layout(&[item]).iter().any(|p| p.text == "×"));
        let mut item = row(100.0); item.tile = true; item.hovered = true; item.close_visible = true;
        assert!(layout(&[item]).iter().any(|p| p.text == "×"));
    }
    #[test] fn keyboard_wraps_without_overflow_or_empty_underflow() {
        assert_eq!(cycle_tab(0, 0, true), 0);
        assert_eq!(cycle_tab(0, 3, true), 2);
        assert_eq!(cycle_tab(2, 3, false), 0);
        assert_eq!(cycle_tab(usize::MAX, 3, false), 0);
        assert_eq!(cycle_tab(0, 1, true), 0);
    }
}


// Borrowed text is valid until the next mutation. Native adapters copy/cache it
// by revision; no browser handles or profile state cross this boundary.
fn new_editor_state() -> Box<EditorState> { Box::default() }
fn editor_text(state: &EditorState) -> &[u16] { state.text() }
fn editor_info(state: &EditorState) -> ffi::EditorInfo {
    let i=state.info();
    ffi::EditorInfo { revision:i.revision,anchor:i.selection.anchor,focus:i.selection.focus,secondary:i.selection.secondary,directed:i.directed,
        enabled:i.enabled,read_only:i.read_only,composing:i.composing,composition_start:i.composition_start,
        composition_end:i.composition_end,composition_token:i.composition_token,can_undo:i.can_undo,can_redo:i.can_redo,boundaries_ready:i.boundaries_ready }
}
fn editor_update(state:&EditorState,c:sidebar_editor::Change)->ffi::EditorUpdate {
    ffi::EditorUpdate {admitted:c.admitted,text_changed:c.text,selection_changed:c.selection,composition_changed:c.composition,committed:c.committed,info:editor_info(state)}
}
fn editor_install_boundaries(state:&mut EditorState,revision:u64,stops:&[usize],words:&[usize])->bool {state.install_boundaries(revision,stops,words)}
fn editor_begin_composition(state:&mut EditorState)->u64 {state.begin_composition()}
fn editor_reset(state: &mut EditorState, text: &[u16])->ffi::EditorUpdate {let change=state.reset(text);editor_update(state,change)}
fn editor_availability(state: &mut EditorState, enabled: bool, read_only: bool)->ffi::EditorUpdate {let change=state.availability(enabled,read_only);editor_update(state,change)}
fn editor_select(state: &mut EditorState, anchor: usize, focus: usize, secondary: bool)->ffi::EditorUpdate {let change=state.select(anchor,focus,secondary);editor_update(state,change)}
fn editor_move_to(state: &mut EditorState, index: usize, extend: bool, secondary: bool)->ffi::EditorUpdate {let change=state.move_to(index,extend,secondary);editor_update(state,change)}
fn editor_move_logical(state: &mut EditorState, backward: bool, word: bool, extend: bool)->ffi::EditorUpdate {let change=state.move_logical(backward,word,extend);editor_update(state,change)}
fn editor_insert(state: &mut EditorState, text: &[u16], typing: bool)->ffi::EditorUpdate {let change=state.insert(text,typing);editor_update(state,change)}
fn editor_erase(state: &mut EditorState, backward: bool, word: bool)->ffi::EditorUpdate {let change=state.erase(backward,word);editor_update(state,change)}
fn editor_compose(state: &mut EditorState, token: u64, text: &[u16], anchor: usize, focus: usize)->ffi::EditorUpdate {let change=state.compose(token,text,anchor,focus);editor_update(state,change)}
fn editor_confirm(state: &mut EditorState, token: u64)->ffi::EditorUpdate {let change=state.confirm(token);editor_update(state,change)}
fn editor_cancel(state: &mut EditorState, token: u64)->ffi::EditorUpdate {let change=state.cancel(token);editor_update(state,change)}
fn editor_undo(state: &mut EditorState)->ffi::EditorUpdate {let change=state.undo();editor_update(state,change)}
fn editor_redo(state: &mut EditorState)->ffi::EditorUpdate {let change=state.redo();editor_update(state,change)}

fn editor_drop(state:&mut EditorState,destination:usize,copy:bool)->ffi::EditorUpdate {let c=state.drop_selection(destination,copy);editor_update(state,c)}
fn editor_viewport(state:&mut EditorState,width:f32,height:f32,line_width:f32,caret_x:f32,ascent:f32,descent:f32,rtl:bool,padding:f32)->ffi::EditorGeometry {
    let finite=|n:f32|if n.is_finite(){n.max(0.0)}else{0.0};
    let (w,h,line,c,a,d)=(finite(width),finite(height),finite(line_width),finite(caret_x),finite(ascent),finite(descent));
    let inset=finite(padding).min(w/2.0);let cw=(w-2.0*inset).max(0.0);let room=(cw-1.0).max(0.0);
    let max_scroll=(line-room).max(0.0);let mut scroll=state.scroll().clamp(0.0,max_scroll);
    if c<scroll{scroll=c;}else if c>scroll+room{scroll=c-room;}scroll=scroll.clamp(0.0,max_scroll);state.set_scroll(scroll);
    let align=if rtl&&line<room{room-line}else{0.0};let origin=inset+align-scroll;
    let y=((h-a-d)/2.0).max(0.0);let ch=(a+d).min(h);
    ffi::EditorGeometry{clip:ffi::FloatRect{x:inset,y:0.0,width:cw,height:h},origin,baseline:y+a,caret:ffi::FloatRect{x:origin+c,y,width:1.0,height:ch}}
}
fn editor_scene(state:&EditorState,placeholder:&[u16],g:&ffi::EditorGeometry,s:&ffi::EditorStyle,selection:&[ffi::EditorSpan],composition:&[ffi::EditorSpan])->Vec<ffi::Primitive> {
    let mut ops=vec![];
    if ![s.width,s.height,s.font_size,g.origin,g.baseline,g.clip.x,g.clip.y,g.clip.width,g.clip.height].iter().all(|v|v.is_finite())||s.width<=0.0||s.height<=0.0||s.font_size<=0.0{return ops;}
    let full=||ffi::FloatRect{x:0.0,y:0.0,width:s.width,height:s.height};
    let clip=||ffi::FloatRect{x:g.clip.x,y:g.clip.y,width:g.clip.width,height:g.clip.height};
    let rect=|x,y,width,height,color,cl:ffi::FloatRect|ffi::Primitive{x,y,width,height,radius:0.0,color,stroke:0.0,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,clip:cl,editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0};
    let mut frame=rect(0.0,0.0,s.width,s.height,s.fill,full());frame.radius=if s.framed{5.0}else{0.0};ops.push(frame);
    if s.framed{let mut edge=rect(0.5,0.5,(s.width-1.0).max(0.0),(s.height-1.0).max(0.0),s.edge,full());edge.radius=4.5;edge.stroke=if s.focused{2.0}else{1.0};edge.shape=0;ops.push(edge);}
    for span in selection {if span.x.is_finite()&&span.width.is_finite()&&span.width>0.0{ops.push(rect(g.origin+span.x,g.caret.y,span.width,g.caret.height,s.selection_fill,clip()));}}
    let text=if state.text().is_empty(){placeholder}else{state.text()};
    if !text.is_empty() {
        let make=|color,cl:ffi::FloatRect|ffi::Primitive{x:0.0,y:0.0,width:s.width,height:s.height,radius:0.0,color,stroke:0.0,shape:0,text:String::from_utf16_lossy(text),font_size:s.font_size,font_weight:0,bold:false,middle:false,center:false,clip:cl,editor:true,utf16:text.to_vec(),text_x:if state.text().is_empty()&&s.right_align{g.clip.x+g.clip.width-s.placeholder_width}else{g.origin},baseline:g.baseline,text_direction:s.text_direction};
        ops.push(make(if state.text().is_empty(){s.placeholder_ink}else{s.ink},clip()));
        if !state.text().is_empty(){for span in selection{if span.x.is_finite()&&span.width.is_finite()&&span.width>0.0{let x=(g.origin+span.x).max(g.clip.x);let end=(g.origin+span.x+span.width).min(g.clip.x+g.clip.width);if end>x{ops.push(make(s.selection_ink,ffi::FloatRect{x,y:g.clip.y,width:end-x,height:g.clip.height}));}}}}
    }
    for span in composition {if [span.x,span.width,span.thickness].iter().all(|n|n.is_finite())&&span.width>0.0&&span.thickness>0.0{ops.push(rect(g.origin+span.x,g.baseline+1.0,span.width,span.thickness,span.color,clip()));}}
    if s.focused&&s.caret_visible&&state.info().selection.anchor==state.info().selection.focus {ops.push(rect(g.caret.x,g.caret.y,g.caret.width,g.caret.height,s.ink,clip()));}
    ops
}

#[cfg(test)]
mod editor_scene_tests {
 use super::*;
 #[test] fn viewport_reveals_caret_clamps_scroll_and_aligns_short_rtl(){let mut e=EditorState::default();let g=editor_viewport(&mut e,100.0,32.0,1000.0,900.0,12.0,3.0,false,8.0);assert_eq!(g.clip.width,84.0);assert_eq!(g.caret.x,91.0);assert_eq!(e.scroll(),817.0);let g=editor_viewport(&mut e,100.0,32.0,1000.0,0.0,12.0,3.0,false,8.0);assert_eq!(g.caret.x,8.0);assert_eq!(e.scroll(),0.0);let g=editor_viewport(&mut e,100.0,32.0,30.0,30.0,12.0,3.0,true,8.0);assert_eq!(g.origin,61.0);assert_eq!(g.caret.x,91.0);}
 #[test] fn viewport_rejects_nonfinite_and_handles_tiny_fields(){let mut e=EditorState::default();let g=editor_viewport(&mut e,f32::NAN,-10.0,f32::INFINITY,f32::NAN,12.0,3.0,false,8.0);assert_eq!(g.clip.width,0.0);assert_eq!(g.clip.height,0.0);assert!(g.origin.is_finite()&&g.caret.x.is_finite());}
 #[test] fn editor_keeps_full_utf16_and_recolors_whole_shaped_line_with_clips(){let mut e=EditorState::default();e.reset(&"office אבג".encode_utf16().collect::<Vec<_>>());e.select(1,4,false);let g=editor_viewport(&mut e,100.0,32.0,70.0,20.0,12.0,3.0,false,8.0);let s=ffi::EditorStyle{width:100.0,height:32.0,font_size:13.0,ink:0xff000000,fill:0xffffffff,edge:0xff777777,selection_fill:0xff4444ff,selection_ink:0xffffffff,placeholder_ink:0xff888888,text_direction:0,placeholder_width:0.0,right_align:false,framed:true,focused:true,caret_visible:true};let selection=[ffi::EditorSpan{x:6.0,width:18.0,thickness:0.0,color:0}];let scene=editor_scene(&e,&[],&g,&s,&selection,&[]);let text=scene.iter().filter(|p|p.editor).collect::<Vec<_>>();assert_eq!(text.len(),2);assert_eq!(text[0].utf16,e.text());assert_eq!(text[0].utf16,text[1].utf16);assert_eq!(text[0].text_x,text[1].text_x);assert_eq!(text[1].clip.width,18.0);assert_eq!(text[1].color,0xffffffff);assert!(scene.iter().all(|p|p.shape!=2));}
}

fn editor_policy(policy:ffi::EditorSelectionPolicy)->sidebar_editor::SelectionPolicy {use sidebar_editor::SelectionPolicy as P;match policy{ffi::EditorSelectionPolicy::Retain=>P::Retain,ffi::EditorSelectionPolicy::Extend=>P::Extend,ffi::EditorSelectionPolicy::Caret=>P::Caret,_=>P::None}}
fn editor_move_policy(state:&mut EditorState,index:usize,policy:ffi::EditorSelectionPolicy,secondary:bool)->ffi::EditorUpdate {let c=state.move_with_policy(index,editor_policy(policy),secondary);editor_update(state,c)}
fn editor_navigate(state:&mut EditorState,backward:bool,word:bool,policy:ffi::EditorSelectionPolicy)->ffi::EditorUpdate {let c=state.navigate(backward,word,editor_policy(policy));editor_update(state,c)}
fn editor_kill(state:&mut EditorState,end:bool)->ffi::EditorKill {let k=state.kill_to_boundary(end);ffi::EditorKill{update:editor_update(state,k.change),start:k.start,end:k.end,before_revision:k.before_revision}}
fn editor_yank(state:&mut EditorState,text:&[u16])->ffi::EditorUpdate {let c=state.yank(text);editor_update(state,c)}
fn editor_transpose(state:&mut EditorState)->ffi::EditorUpdate {let c=state.transpose();editor_update(state,c)}

fn editor_orient(state:&mut EditorState,anchor:usize,focus:usize,secondary:bool)->ffi::EditorUpdate {let c=state.orient(anchor,focus,secondary);editor_update(state,c)}

fn editor_select_word(state:&mut EditorState)->ffi::EditorUpdate {let c=state.select_word();editor_update(state,c)}
fn editor_pointer_begin(state:&mut EditorState,index:usize,secondary:bool,extend:bool,clicks:u8)->ffi::EditorUpdate {let c=state.pointer_begin(index,secondary,extend,clicks);editor_update(state,c)}
fn editor_pointer_drag(state:&mut EditorState,index:usize,secondary:bool)->ffi::EditorUpdate {let c=state.pointer_drag(index,secondary);editor_update(state,c)}
fn editor_pointer_end(state:&mut EditorState) {state.pointer_end()}
fn editor_pointer_active(state:&EditorState)->bool {state.pointer_active()}

#[cfg(test)]
mod palette_editor_presentation_tests {
 use super::*;
 #[test] fn borderless_padding_reveals_caret_and_rejects_nonfinite_insets(){
  let mut e=EditorState::default();
  let g=editor_viewport(&mut e,100.0,32.0,1000.0,900.0,16.0,4.0,false,0.0);
  assert_eq!(g.clip.x,0.0);assert_eq!(g.clip.width,100.0);assert_eq!(g.caret.x,99.0);assert_eq!(e.scroll(),801.0);
  for inset in [f32::NAN,f32::INFINITY,-20.0] {
   let g=editor_viewport(&mut e,100.0,32.0,20.0,0.0,16.0,4.0,false,inset);
   assert_eq!(g.clip.x,0.0);assert_eq!(g.clip.width,100.0);assert_eq!(g.origin,0.0);
  }
  let g=editor_viewport(&mut e,10.0,32.0,20.0,0.0,16.0,4.0,false,1000.0);
  assert_eq!(g.clip.x,5.0);assert_eq!(g.clip.width,0.0);assert!(g.origin.is_finite());
 }
 #[test] fn palette_scene_uses_requested_text_size_without_a_field_frame(){
  let mut e=EditorState::default();e.reset(&"Search 日本語".encode_utf16().collect::<Vec<_>>());
  let g=editor_viewport(&mut e,200.0,32.0,100.0,100.0,16.0,4.0,false,0.0);
  let style=ffi::EditorStyle{width:200.0,height:32.0,font_size:17.0,ink:0xff000000,fill:0xffffffff,edge:0xff777777,selection_fill:0xff4444ff,selection_ink:0xffffffff,placeholder_ink:0xff888888,text_direction:0,placeholder_width:0.0,right_align:false,framed:false,focused:true,caret_visible:true};
  let scene=editor_scene(&e,&[],&g,&style,&[],&[]);
  assert!(scene.iter().all(|op|op.stroke==0.0&&op.radius==0.0));
  let text=scene.iter().find(|op|op.editor).unwrap();assert_eq!(text.font_size,17.0);assert_eq!(text.utf16,e.text());assert_eq!(text.text_x,0.0);
 }
}
