// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Control layout, presentation and input policy; browser state and command ownership
// remain in WorkspaceShell. Only opaque IDs, display text and metrics cross the ABI.
// Display text may include URLs; browser URL objects, profile objects/directories
// and WebContents remain native, and projected text never authorizes an operation.
mod sidebar_editor;
use sidebar_editor::EditorState;
// The design system (docs/design-system): tokens, workspace palettes, recipes.
// The shell reads palettes through design_palette(); layout() takes radii from the tokens.
#[allow(dead_code)]
mod design;
mod design_tokens;
// UI language: Polish source strings, English catalogue (t, tf).
mod i18n;
// Time Machine: the recorded timeline of each workspace's tabs.
mod timeline;
use timeline::{new_timeline, timeline_card, tab_count_copy, timeline_clock, timeline_deck, timeline_spring, timeline_follow, timeline_pan, timeline_time_at, timeline_window, timeline_zoom, Timeline};
#[allow(unused_imports)]
use i18n::{t, tf};

#[cxx::bridge(namespace = "zitter::sidebar")]
mod ffi {
    enum EditorSelectionPolicy { None, Retain, Extend, Caret }
    struct EditorGeometry { clip: FloatRect, origin: f32, baseline: f32, caret: FloatRect }
    struct EditorSpan { x: f32, width: f32, thickness: f32, color: u32 }
    struct EditorStyle { width: f32, height: f32, font_size: f32, ink: u32, fill: u32, edge: u32, selection_fill: u32, selection_ink: u32, placeholder_ink: u32, text_direction: u32, placeholder_width: f32, right_align: bool, framed: bool, focused: bool, caret_visible: bool }

    struct EditorInfo { revision: u64, anchor: usize, focus: usize, secondary: bool, directed: bool, enabled: bool, read_only: bool, composing: bool, composition_start: usize, composition_end: usize, composition_token: u64, can_undo: bool, can_redo: bool, boundaries_ready: bool }
    struct EditorKill { update: EditorUpdate, start: usize, end: usize, before_revision: u64 }
    struct EditorUpdate { admitted: bool, text_changed: bool, selection_changed: bool, composition_changed: bool, committed: bool, info: EditorInfo }
    struct NodeToggleView { checked: bool, progress: f32, hover: f32, animating: bool }
    struct SelectLayout { width: i32, height: i32, label: Rect, arrow: Rect }
    struct SelectOption { key: String, label: String, search: String }
    struct ToolsCopy { title: String, intro: String, tail: String, initial_status: String, fields: Vec<String>, pickers: Vec<String>, headings: Vec<String>, actions: Vec<String>, choices: Vec<String> }
    struct PageDialogCopy { title: String, identity: String, message: Vec<u16>, accept: String, cancel: String, show_cancel: bool, default_cancel: bool, prompt: bool }
    struct ToolsActionContext { shortcut_field: bool, provider_field: bool, provider_selected: bool, shortcut_composing: bool, provider_composing: bool, shortcut_selected: bool }
    struct ToolsDispatch { admitted: bool, action: i32, recording: bool, sync_recording: bool, record_status: String }
    struct ToolsOutcome { refresh: bool, status: String }
    enum ToolsCapture { Escape, Invalid, Valid }
    struct ToolsCaptureResult { admitted: bool, stop_recording: bool, accept_chord: bool, status: String }
    enum ToolsStatus { WriteFailed }
    enum PreferenceOutcome { Text, Site, SiteRemove, Value, General }
    enum PreferenceControlKind { Text, Toggle }
    enum PaletteResultKind { Tab, Pin, Workspace, History }
    struct PasswordPromptCopy { title: String, lines: Vec<String>, accept: String, cancel: String }
    #[derive(Debug)]
    enum OperationStatusEvent { Checking, Cancelled, Done, StopCapture }
    #[derive(Debug)]
    enum ClearDataEvent { Busy, NothingSelected, HistoryForbidden, Clearing, Cleared, Failed }
    struct ClearDataConfirm { valid: bool, title: String, description: String, confirm: String, cancel: String }
    enum ImportStage { Reading, Preview, Importing, SavingBrowsingData, Finished }
    struct ImportStatusReport { stage: ImportStage, browser: String, error: String, profiles: usize, spaces: usize, folders: usize, tabs: usize, skipped: usize, ok: bool, added_profiles: usize, added_spaces: usize, browsing_data: bool, history: usize, cookies: usize, data_skipped: usize, data_failed: bool }
    #[derive(Debug)]
    enum ProfileStatusEvent { DuplicateName, Creating, CreateFailed, Created, CreateNotSaved, Renamed, RenameFailed, Deleting, Deleted, DeleteNotSaved, LoadFailed }
    struct PreferenceControlView { enabled: bool, tooltip: String }
    struct PreferenceStatus { text: String, error: bool }
    enum ToolsNodeKind { Hint, Heading, Editor, Choice, Action, BeginActions, EndActions }
    enum ToolsNodeRole { None, ShortcutChoice, ShortcutChord, ProviderChoice, ProviderName, ProviderKeyword, ProviderUrl }
    struct ToolsFormNode { kind: ToolsNodeKind, role: ToolsNodeRole, text: Vec<u16>, action: i32, choices: Vec<String>, enabled: bool, visible: bool, flex: bool, spacing: i32 }
    struct ToolsFormPlan { title: Vec<u16>, status: Vec<u16>, nodes: Vec<ToolsFormNode> }
    struct ToolsShortcutItem { id: String, title: Vec<u16>, chord: Vec<u16> }
    struct ToolsProviderItem { id: String, name: Vec<u16>, keyword: Vec<u16>, url: Vec<u16>, default_provider: bool }
    struct ToolsFormChoice { key: String, label: Vec<u16> }
    struct ToolsFormChoices { choices: Vec<ToolsFormChoice>, enabled: bool }
    struct ToolsChoiceDraft { valid: bool, page: usize, create: bool, target: String, chord: Vec<u16>, name: Vec<u16>, keyword: Vec<u16>, url: Vec<u16>, stop_recording: bool }
    enum PreferenceRowKind { Theme, Pinned, Animations, Search, Toggle, Text, Picker, Action, Directory, SiteEditor, SiteRules, ClearData, ShortcutHint, WorkspaceNames, FoldersCollapsed, Language, TimeMachine, TimeMachineDays }
    #[repr(u32)]
    #[derive(Debug)]
    enum FormCommand { None, WorkspaceSave, WorkspaceToggle, WorkspaceRemove, WorkspaceProfiles, WorkspaceImport, ProfileCreate, ProfileRename, ProfileDelete, ProfileBack, ImportApply, PreferenceText, PreferenceSite, PreferenceClear, PreferenceDirectory, History, Workspace, Passwords, Profiles, Import, Proxy, Certificates, Providers, Shortcuts, TimeMachine, WorkspaceDismiss, PreferencesDismiss, ImportDismiss, ToolsDismiss, PreferenceCategory, PreferenceMore, WorkspaceColorPreset, WorkspaceEmojiPreset }
    #[derive(Debug)]
    enum FormPanel { None, Workspace, Profiles, Import, Preferences, Tools }
    struct FormCommandBinding { command: FormCommand, panel: FormPanel, key: String, index: usize }
    struct FormCommandView { bound: bool, dismiss: bool, index: usize }
    enum PreferenceActionKind { None, History, Workspace, Passwords, Profiles, Import, Proxy, Certificates, Providers, Shortcuts, TimeMachine, ClearTimeline }
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
    /// Every color role of the design system for one window (design.rs).
    struct DesignPalette { bg_void: u32, bg_base: u32, bg_raised: u32, bg_hover: u32, line: u32, line_strong: u32, ink: u32, ink_muted: u32, ink_faint: u32, accent: u32, on_accent: u32, accent_soft: u32, accent_ink: u32, zing: u32, on_zing: u32, zing_ink: u32, focus: u32, ok: u32, warn: u32, danger: u32, scrim: u32 }
    // Time Machine timeline (timeline.rs). Times are milliseconds since the Unix epoch.
    #[derive(Clone, Debug)]
    struct TimelineTab { url: String, title: String }
    struct TimelineFrame { t_ms: i64, workspace: String, profile: String, active: i32, tabs: Vec<TimelineTab> }
    #[derive(Clone)]
    struct TimelineMark { t_ms: i64, workspace: String, name: String }
    #[derive(Debug)]
    enum TimelineRecord { Skip, Append, Replace }
    struct TimelineStats { frames: usize, marks: usize, bytes: usize }
    struct TimelineBounds { valid: bool, first_ms: i64, last_ms: i64 }
    struct TimelineMoment { tabs: Vec<TimelineTab>, active: i32, open_at_t: Vec<bool>, profile: String }
    struct TimelineClosed { valid: bool, t_ms: i64, tab: TimelineTab }
    struct TimelineSceneInput { width: f32, height: f32, t_ms: i64, quantum_ms: i64, now_ms: i64, tz_offset_min: i32, start_ms: i64, end_ms: i64 }
    struct TimelineSpan { start_ms: i64, end_ms: i64 }
    struct TimelineClock { day_line: String, digits: Vec<u32>, handle: String, value_text: String, restore: String, range: String }
    struct TimelineCard { x: f32, z: f32, angle: f32, opacity: f32, order: i32 }
    struct TimelineSpring { value: f32, velocity: f32, settled: bool }
    struct AppearanceTarget { valid: bool, color: u32, emoji: String }
    struct PeekCopy { initial: String, back: String, forward: String, promote: String, split: String, close: String }
    struct WorkspaceCopy { name_label: String, placeholder: String, profile_label: String, profile_ax: String, profile_hint: String, save_hint: String, close: String, appearance: String, color: String, color_ax: String, emoji: String, emoji_ax: String, emoji_placeholder: String, more_emoji: String, more: String, manage: String, import_label: String, remove_label: String }
    enum WorkspaceFormAction { None, Create, Edit }
    struct WorkspaceFormRequest { action: WorkspaceFormAction, workspace: String, name: String, profile: String }
    struct WorkspaceFormView { creating: bool, title: String, description: String, save_label: String, secondary_label: String, appearance: bool, enabled: bool, remove_enabled: bool, error: String }
    struct ProfileOption { id: String, name: String }
    struct ProfileUse { workspace: String, name: String, profile: String }
    struct ProfileCopy { title: String, hint: String, name_label: String, name_ax: String, placeholder: String, rename_ax: String, create: String, rename: String, cancel: String, remove_label: String, back: String }
    enum ProfileAction { None, Create, Rename, Delete, ConfirmDelete }
    struct ProfileRequest { action: ProfileAction, id: String, name: String, workspace: String }
    // edit: row actions (pencil, trash) accept input; rename: the pencil may open the editor.
    struct ProfileControls { create: bool, edit: bool, rename: bool, confirming: String, status: String }
    // One profile row: the initial stands in for an avatar; detail names the spaces using it.
    struct ProfileRow { id: String, name: String, initial: String, detail: String, current: bool, rename_ax: String, remove_ax: String }
    struct NoticeCopy { title: String, body: String, accept: String, cancel: String, default_cancel: bool }
    enum FolderNoticeAction { None, Delete, Acknowledge }
    struct FolderNoticeRequest { action: FolderNoticeAction, workspace: String, folder: String }
    struct NoticeLayout { width: i32, height: i32, body: Rect, error: Rect, body_lines: Vec<Rect>, error_lines: Vec<Rect> }
    struct FolderCopy { title: String, label: String, accept: String, cancel: String }
    enum FolderAction { None, Create, Rename }
    struct FolderRequest { action: FolderAction, workspace: String, parent: String, folder: String, name: String, expand_parent: bool }
    struct FolderLayout { width: i32, height: i32, label: Rect, input: Rect, error: Rect }
    struct MenuTab { index: usize, high: u64, low: u64 }
    // Visible sidebar rows in visual order; pinned rows come before tab rows.
    struct ReorderRow { bounds: Rect, pinned: bool, folder: bool }
    // row == usize::MAX appends to the pinned section (it has no rows yet).
    struct ReorderTarget { valid: bool, row: usize, after: bool, into: bool, pinned: bool }
    struct RowTarget { valid: bool, has_tab: bool, tab: usize, pin: String }
    struct TabMenuState {
        ready: bool, busy: bool, valid: bool, tab: usize, has_tab: bool,
        // members: the Split View group in pane order (tab indices).
        split: bool, members: Vec<usize>,
        vertical: bool, tab_count: usize, http: bool,
    }
    struct TabMenuEntry { command: u32, label: String }
    struct LinkRoute { id: String, label: String }
    struct LinkMenuState { ready: bool, host: String, source: String, has_rule: bool, rule_workspace: String, routes: Vec<LinkRoute> }
    // kind: 0 item, 1 separator, 2 route submenu header, 3 route radio item (submenu member).
    struct LinkMenuEntry { command: i32, label: String, kind: u32 }
    #[derive(Debug)]
    enum LinkMenuCommand { None, Peek, NewTab, Split, SetRule, RemoveRule }
    struct LinkMenuAction { command: LinkMenuCommand, workspace: String }
    struct FolderMenuState { ready: bool, busy: bool, workspace: String, folder: String }
    enum DownloadPhase { InProgress, Complete, Cancelled, Interrupted }
    // Byte quantities arrive pre-formatted by the platform locale service.
    struct DownloadFacts { phase: DownloadPhase, blocked: bool, removed: bool, can_resume: bool, paused: bool, received: String, total: String, speed: String, remaining_seconds: i64, host: String }
    // A downloads row (design: Downloads): the state as a symbol and a word in its
    // tone (0 none, 1 ok, 2 warn, 3 danger), then the muted facts.
    struct DownloadRow { tone: u8, state: String, meta: String }
    struct CommandWorkspace { id: String, name: String, asleep: bool, same_profile: bool, capturing: bool }
    struct CommandContext { page: bool, http_page: bool, split: bool, can_back: bool, can_forward: bool, tab_count: usize, reopen: bool, workspaces: Vec<CommandWorkspace> }
    struct CommandEntry { id: String, title: String, detail: String, target: String }
    struct PalettePlanInput { text: String, command_palette: bool, provider: String, resolved: String, keywords: Vec<String>, results: usize, commands: usize }
    #[derive(Debug)]
    enum PaletteSource { Primary, Result, Command }
    struct PaletteSlot { source: PaletteSource, index: usize }
    struct PalettePlan { commands_only: bool, keyword_search: bool, primary_title: String, primary_detail: String, primary_icon: u32, slots: Vec<PaletteSlot> }
    struct FolderMenuEntry { command: i32, label: String }
    #[derive(Debug)]
    enum FolderMenuCommand { None, Create, Rename, Delete }
    struct FolderMenuAction { command: FolderMenuCommand, workspace: String, folder: String }
    struct WorkspaceMenuProfile { id: String, name: String }
    struct WorkspaceMenuState { ready: bool, busy: bool, workspace: String, workspace_count: usize, profile: String, profiles: Vec<WorkspaceMenuProfile>, color: u32, emoji: String }
    // group: 0 main, 1 profile, 2 color, 3 emoji submenu; kind: 0 item, 1 separator, 2 submenu header, 3 radio, 4 check.
    struct WorkspaceMenuEntry { command: i32, label: String, kind: u32, group: u32 }
    #[derive(Debug)]
    enum WorkspaceMenuCommand { None, AssignProfile, NewProfile, EditProfile, Settings, Remove, Color, Emoji, ClearEmoji, CustomColor, CustomEmoji }
    struct WorkspaceMenuAction { command: WorkspaceMenuCommand, workspace: String, profile: String, color: u32, emoji: String }
    enum SidebarAction { None, OpenSplit, CloseSplit, MoveEarlier, MoveLater, ToggleOrientation, CloseTab, Separate, CloseGroup }
    // kind: 0 row, 1 section label, 2 split pair (three consecutive items), 3 gap.
    // A split group (kind 2) consumes 1 + members consecutive items.
    struct PreviewRow { index: usize, kind: u32, depth: i32, height: i32, members: usize }
    struct PreviewPlacement { index: usize, bounds: Rect, clip: Rect }
    struct WorkspaceMotion { enabled: bool, offset: f64, duration_ms: u32 }
    struct SwipeUpdate { started: bool, preview: bool, finish: bool, handled: bool }
    struct SwipePage { index: i32, x: f64 }
    struct SwipeLayout { center: i32, pages: Vec<SwipePage> }
    struct PaletteInputLayout { icon: Rect, input: Rect, hint: Rect }
    struct WidthChange { width: i32, changed: bool }
    struct ShellLayout { content: Rect, edge: Rect, sidebar: Rect }
    // One page and title bar per pane; dividers[i] sits between panes i and i + 1.
    struct PaneHostLayout { pages: Vec<Rect>, bars: Vec<Rect>, dividers: Vec<Rect> }
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
        // 0: row, 1: section label, 2: split group background, 3: New Tab action, 4: address, 5: workspace heading, 6: scroll thumb, 7: control glyph, 8: pane title, 9: split grip, 10: rounded backing, 11: sidebar grip, 12: palette result, 13: result icon, 14: separator, 15: palette static text, 16: palette frame, 17: form static text, 18: appearance color swatch, 19: form text action, 20: checkbox mark, 21: closed selector, 22: preference category, 23: preferences dismiss, 24: preference surface, 25: history result row, 26: progress indicator, 27: Peek title (middle elision and physical RTL alignment), 28: workspace color hue ring, 29: workspace color saturation x tone field (both: `badge` the stored color, `tile` the dark scheme, `fill` the surface, `ink` the knob edge, `title` the ring's emoji).
        kind: u32, glyph: u32,
        title: String, monogram: String, detail: String,
        title_width: f32, detail_ink: u32, detail_font_size: f32,
        ink: u32, fill: u32, badge: u32,
        // 1 px inset ring drawn over `fill` (0 = none); the design system's card edge.
        edge: u32,
        // Keyboard focus ring colour (0 = the recipe's own focus bar).
        focus: u32,
        active: bool, hovered: bool, tile: bool, close: bool, close_visible: bool, focused: bool,
        font_size: f32, font_height: f32, font_weight: i32,
        // A design system text style for every text this item draws (design::TextStyleId; 0 = none).
        text_style: u32,
        clip: FloatRect,
    }
    struct TreeNode { id: String, parent: String, folder: bool }
    struct TreeEntry { index: usize, parent: usize, depth: i32, collapsed: bool, visible: bool }
    struct TabNode { index: usize, pin: String }
    // A plain slot holds `first`; a split group holds all `members` (first == members[0]).
    struct TabSlot { first: usize, members: Vec<usize>, pin: usize, pair: bool }
    struct PinVisibility { index: usize, visible: bool }
    struct TabPlan { slots: Vec<TabSlot>, pins: Vec<PinVisibility> }
    enum PointerPhase { Enter, Exit, Move, Press, Drag, Release }
    enum RowVisual { Normal, Hovered, Pressed, Disabled }
    enum RowAction { None, Activate, Drop, Cancel }
    struct RowInputResult { visual: RowVisual, action: RowAction, handled: bool }
    struct ActionNodeKey { kind: u32, high: u64, low: u64, name: String }
    struct ActionNodePlan { admitted: bool, ids: Vec<u64>, created: usize, retired: usize }
    struct WorkspaceSceneInput { key: String, name: Vec<u16>, label: Vec<u16>, sleeping: bool, active: bool, enabled: bool }
    struct WorkspaceSceneChild { id: u64, parent: u64, key: String, name: Vec<u16>, label: Vec<u16>, active: bool, enabled: bool, cancel_input: bool }
    struct WorkspaceScene { root: u64, children: Vec<WorkspaceSceneChild> }
    struct PaletteSceneInput { key: String, title: Vec<u16>, detail: Vec<u16>, icon: u32 }
    struct PaletteSceneChild { id: u64, parent: u64, key: String, title: Vec<u16>, detail: Vec<u16>, icon: u32, highlighted: bool, height: i32, cancel_input: bool }
    struct PaletteScene { root: u64, children: Vec<PaletteSceneChild> }
    struct PaletteTarget { valid: bool, index: usize }



    struct ActionSnapshot { enabled: bool, focused: bool, on: bool, is_default: bool, visual: RowVisual }
    struct Rect { x: i32, y: i32, width: i32, height: i32 }
    struct RowLayout { title: Rect, detail: Rect, close: Rect }
    #[derive(Clone, Copy)]
    struct FormGroupStyle { vertical: bool, spacing: i32, intrinsic: bool, dialog: bool, shared: bool }
    struct FormScene { root: u64, children: Vec<u64> }
    struct FormMeasure { id: u64, width: i32, height: i32 }
    struct FormCell { id: u64, rect: Rect }
    struct FormLayout { admitted: bool, width: i32, height: i32, children: Vec<FormCell> }
    struct SiteRuleSceneInput { key: String, detail: Vec<u16>, editable: bool, source_index: usize }
    struct SiteRuleSceneChild { id: u64, parent: u64, key: String, detail: Vec<u16>, editable: bool, cancel_input: bool }
    struct SiteRuleScene { root: u64, children: Vec<SiteRuleSceneChild> }
    struct SiteRuleTarget { valid: bool, index: usize }
    struct SidebarPinInput { id: String, parent: String, title: Vec<u16>, monogram: Vec<u16>, color: u32, folder: bool, collapsed: bool, revealed: bool }
    struct SidebarPinBinding { valid: bool, id: u64, key: String, folder: bool, collapsed: bool, group: u64 }
    struct SidebarPinTarget { valid: bool, folder: bool, collapsed: bool, group: u64 }
    struct SidebarPinTreeEntry { index: usize, key: String, id: u64, parent: u64, group: u64, depth: i32, collapsed: bool, visible: bool }
    struct SidebarPinTree { root: u64, entries: Vec<SidebarPinTreeEntry> }
    struct SidebarTabInput { high: u64, low: u64, index: usize, pin: String }
    struct SidebarTabBinding { valid: bool, id: u64, high: u64, low: u64, index: usize, can_close: bool }
    struct SidebarTabTree { root: u64, pair: u64, parent: u64, visible: bool, slots: Vec<TabSlot>, pins: Vec<PinVisibility> }
    struct SidebarTabTarget { valid: bool, index: usize }
    struct SidebarRowVisual { valid: bool, tile: bool, height: i32 }
    struct SidebarRowPresentation { valid: bool, title: Vec<u16>, monogram: Vec<u16>, color: u32, tile: bool, folder: bool, height: i32 }
    struct SidebarTabUpdate { admitted: bool, changed: bool, title_changed: bool, geometry_changed: bool }
    struct SidebarRowStyle { font_size: f32, ink: u32, card: u32, hover: u32, edge: u32, focus: u32, muted: u32 }
    struct SidebarRowScene { valid: bool, item: Item }
    struct ColumnChild { width: i32, height: i32, top: i32, left: i32, bottom: i32, right: i32 }
    struct ColumnLayout { width: i32, height: i32, children: Vec<Rect> }
    struct PanelStyle { width: i32, height: i32, vertical: bool, spacing: i32, top: i32, left: i32, bottom: i32, right: i32, flex: usize }
    #[derive(Clone)]
    struct Primitive {
        x: f32, y: f32, width: f32, height: f32,
        radius: f32, color: u32, stroke: f32, shape: u32,
        text: String, font_size: f32, font_weight: i32, bold: bool, middle: bool, center: bool, right_align: bool, clip: FloatRect,
        editor: bool, utf16: Vec<u16>, text_x: f32, baseline: f32, text_direction: u32,
        // Design system text: 0 system font, 1 display, 2 sans, 3 mono; letter spacing in em.
        family: u32, italic: bool, tracking: f32,
    }
    /// The design system's spacing and radius tokens, for native panel layout.
    /// recipe::button for one kind (0 primary, 1 zing, 2 outline, 3 quiet, 4 danger,
    /// 5 danger strong) and state (0 rest, 1 hover, 2 pressed, 3 disabled).
    struct DesignButton { height: f32, radius: f32, padding_x: f32, fill: u32, ink: u32, edge: u32, edge_width: f32 }
    struct DesignToggle { width: f32, height: f32, thumb: f32, inset: f32, track: u32, edge: u32, edge_width: f32, thumb_fill: u32 }
    struct DesignMetrics { space_0_5: i32, space_1: i32, space_2: i32, space_3: i32, space_4: i32, space_6: i32, space_8: i32, space_12: i32, radius_xs: i32, radius_sm: i32, radius_md: i32, radius_lg: i32 }
    /// A design system text style (design_tokens::text) for native measuring.
    struct DesignTextStyle { family: u32, size: f32, line_height: f32, weight: i32, italic: bool, tracking: f32, caps: bool }
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

        fn tools_copy(page: usize) -> ToolsCopy;
        fn page_dialog_copy(kind: u32, unload: bool, origin: &str, profile: &str, message: &[u16]) -> PageDialogCopy;
        type ToolsForm;
        fn new_tools_form() -> Box<ToolsForm>;
        fn tools_select(self: &mut ToolsForm, page: usize);
        fn tools_page(self: &ToolsForm) -> usize;
        fn tools_reset(self: &mut ToolsForm);
        fn tools_record(self: &mut ToolsForm, recording: bool);
        fn tools_recording(self: &ToolsForm) -> bool;
        fn tools_capture(self: &mut ToolsForm, capture: ToolsCapture) -> ToolsCaptureResult;
        fn tools_status(status: ToolsStatus) -> String;
        fn tools_dispatch(self: &mut ToolsForm, action: i32, context: &ToolsActionContext) -> ToolsDispatch;
        fn tools_outcome(self: &ToolsForm, ok: bool, pending: bool) -> ToolsOutcome;
        fn tools_form_plan(self: &ToolsForm) -> ToolsFormPlan;
        fn tools_shortcut_choices(self: &mut ToolsForm, items: Vec<ToolsShortcutItem>) -> ToolsFormChoices;
        fn tools_provider_choices(self: &mut ToolsForm, items: Vec<ToolsProviderItem>) -> ToolsFormChoices;
        fn tools_choice_select(self: &mut ToolsForm, key: &str) -> ToolsChoiceDraft;
        fn tools_choice_draft(self: &ToolsForm, key: &str) -> ToolsChoiceDraft;
        fn preference_catalogue(system_certificates: bool) -> PreferenceCatalogue;
        fn preference_site_policies(index: usize) -> Vec<String>;
        type PreferencesForm;
        fn new_preferences_form(categories: usize) -> Box<PreferencesForm>;
        fn bind_category(self: &PreferencesForm, input: &mut ActionControl, category: usize) -> bool;
        fn preference_select(self: &mut PreferencesForm, category: usize) -> bool;
        fn preference_current(self: &PreferencesForm) -> usize;
        fn preference_profile(self: &mut PreferencesForm, profile: &str) -> bool;
        fn preference_detach(self: &mut PreferencesForm);
        fn preference_edited(self: &mut PreferencesForm, key: &str);
        fn preference_saved(self: &mut PreferencesForm, key: &str, ok: bool);
        fn preference_sync_text(self: &mut PreferencesForm, key: &str, present: bool, editable: bool, composing: bool, profile_changed: bool) -> bool;
        fn preference_can_save(self: &PreferencesForm, enabled: bool, composing: bool) -> bool;
        fn preference_outcome(self: &mut PreferencesForm, outcome: PreferenceOutcome, key: &str, ok: bool) -> PreferenceStatus;
        fn preference_status(self: &PreferencesForm) -> PreferenceStatus;
        fn preference_control_view(kind: PreferenceControlKind, present: bool, editable: bool) -> PreferenceControlView;
        fn preferences_profile_banner(profile: &str) -> String;
        fn profile_status_copy(event: ProfileStatusEvent) -> String;
        fn palette_result_detail(kind: PaletteResultKind, label: &str) -> String;
        fn password_prompt_copy(update: bool, origin: &str, username: &str, profile: &str) -> PasswordPromptCopy;
        fn operation_status_copy(event: OperationStatusEvent) -> String;
        fn clear_data_status(event: ClearDataEvent, profile: &str) -> PreferenceStatus;
        fn clear_data_confirm(profile: &str, period: i32, history: bool, cookies: bool, cache: bool) -> ClearDataConfirm;
        fn import_status_copy(report: &ImportStatusReport) -> String;
        fn site_rule_detail(primary: &str, name: &str, setting: i32, secondary: &str, editable: bool) -> String;
        fn site_rules_empty(saved: bool) -> String;
        fn preference_reset_rules(self: &mut PreferencesForm);
        fn preference_more_rules(self: &mut PreferencesForm);
        fn preference_rule_limit(self: &PreferencesForm) -> usize;
        fn preference_filter(self: &mut PreferencesForm, query: Vec<u16>, rows: &[PreferenceRow]) -> PreferenceVisibility;
        fn node_select_selected(self: &ActionControl) -> usize;
        fn node_select_has_selection(self: &ActionControl) -> bool;
        fn node_select_selected_key(self: &ActionControl) -> String;
        fn node_select_option_key(self: &ActionControl, index: usize) -> String;
        fn node_select_replace_options(self: &mut ActionControl, options: Vec<SelectOption>, fallback_last: bool, select_last: bool) -> bool;
        fn node_select_can_choose(self: &ActionControl, token: u64, key: &str, enabled: bool) -> bool;
        fn node_select_choose_key(self: &mut ActionControl, token: u64, key: &str, enabled: bool) -> bool;
        fn node_select_close_menu_at(self: &mut ActionControl, token: u64, now_us: u64) -> bool;
        fn node_select_dismiss_popup(self: &mut ActionControl, now_us: u64, popup_present: bool);
        fn node_select_open_pointer(self: &mut ActionControl, enabled: bool, now_us: u64, guard_us: u64) -> u64;
        fn node_select_select_index(self: &mut ActionControl, index: usize) -> bool;
        fn node_select_clear_selection(self: &mut ActionControl);
        fn node_select_open(self: &mut ActionControl, enabled: bool) -> u64;
        fn node_select_expanded(self: &ActionControl) -> bool;
        fn node_select_choose(self: &mut ActionControl, token: u64, index: usize, enabled: bool) -> bool;
        fn node_select_reset_prefix(self: &mut ActionControl);
        fn node_select_input(self: &mut ActionControl, lower_text: &str, now_ms: u64, enabled: bool) -> bool;
        fn node_select_init(self: &mut ActionControl, options: Vec<SelectOption>) -> bool;
        fn node_select_options(self: &ActionControl) -> Vec<SelectOption>;
        fn select_menu_key(key: u32) -> bool;
        fn select_layout(width: i32, height: i32, text_width: i32, control_height: i32, rtl: bool) -> SelectLayout;
        fn toggle_scene(style: &ToggleStyle) -> Vec<Primitive>;
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
        fn appearance_command_target(command: FormCommand, index: usize) -> AppearanceTarget;
        fn appearance_presets(emoji: bool) -> Vec<AppearancePreset>;
        fn design_palette(dark: bool, workspace_color: u32) -> DesignPalette;
        type Timeline;
        fn new_timeline() -> Box<Timeline>;
        fn timeline_record(self: &mut Timeline, frame: &TimelineFrame, force: bool) -> TimelineRecord;
        fn timeline_load(self: &mut Timeline, frame: &TimelineFrame, replace: bool) -> bool;
        fn timeline_mark(self: &mut Timeline, mark: &TimelineMark) -> bool;
        fn timeline_prune(self: &mut Timeline, cutoff_ms: i64, max_bytes: usize) -> bool;
        fn timeline_forget(self: &mut Timeline, profile: &str, from_ms: i64, to_ms: i64) -> bool;
        fn timeline_clear(self: &mut Timeline);
        fn timeline_frames(self: &Timeline) -> Vec<TimelineFrame>;
        fn timeline_marks(self: &Timeline) -> Vec<TimelineMark>;
        fn timeline_stats(self: &Timeline) -> TimelineStats;
        fn timeline_bounds(self: &Timeline, workspace: &str) -> TimelineBounds;
        fn timeline_query(self: &Timeline, workspace: &str, t_ms: i64, quantum_ms: i64) -> TimelineMoment;
        fn timeline_step(self: &Timeline, workspace: &str, t_ms: i64, forward: bool) -> i64;
        fn timeline_snap(self: &Timeline, workspace: &str, t_ms: i64) -> i64;
        fn timeline_last_closed(self: &Timeline, workspace: &str, open: &[String]) -> TimelineClosed;
        fn timeline_scene(self: &Timeline, workspace: &str, input: &TimelineSceneInput, palette: &DesignPalette) -> Vec<Primitive>;
        fn timeline_clock(t_ms: i64, now_ms: i64, tz_offset_min: i32, tabs: usize, quantum_ms: i64) -> TimelineClock;
        fn timeline_window(t_ms: i64, now_ms: i64) -> TimelineSpan;
        fn timeline_time_at(input: &TimelineSceneInput, x: f32) -> i64;
        fn timeline_pan(input: &TimelineSceneInput, dx: f32, first_ms: i64) -> TimelineSpan;
        fn timeline_zoom(input: &TimelineSceneInput, factor: f32, x: f32, first_ms: i64) -> TimelineSpan;
        fn timeline_follow(start_ms: i64, end_ms: i64, t_ms: i64, now_ms: i64, first_ms: i64) -> TimelineSpan;
        fn timeline_deck(count: usize, front: usize) -> Vec<TimelineCard>;
        fn timeline_card(offset: f32) -> TimelineCard;
        fn tab_count_copy(count: usize) -> String;
        fn timeline_spring(value: f32, velocity: f32, target: f32, dt: f32, stiffness: f32, damping: f32) -> TimelineSpring;
        fn design_same_workspace_color(a: u32, b: u32) -> bool;
        fn design_text_style(id: u32) -> DesignTextStyle;
        fn design_metrics() -> DesignMetrics;
        fn design_button(dark: bool, workspace_color: u32, kind: u32, state: u32) -> DesignButton;
        fn design_toggle(dark: bool, workspace_color: u32, on: bool) -> DesignToggle;
        fn design_font_family(family: u32) -> String;
        fn appearance_selected(emoji: bool, index: usize, color: u32, current_emoji: &str) -> bool;
        fn appearance_row_layout(width: i32, height: i32, editor_width: i32, editor_height: i32, emoji: bool) -> ColumnLayout;
        fn appearance_pill_layout(width: i32, height: i32, count: usize) -> ColumnLayout;
        // The color wheel (design.rs wheel): the stored color a press picks, 0 for none.
        fn color_ring_target(current: u32, x: f32, y: f32, size: f32) -> u32;
        fn color_pad_target(current: u32, x: f32, y: f32, width: f32, height: f32) -> u32;
        // Arrow keys: axis 0 hue, 1 saturation, 2 tone.
        fn color_wheel_step(current: u32, axis: u32, delta: i32) -> u32;
        fn color_wheel_description(current: u32, ring: bool) -> String;
        type WorkspaceForm;
        fn new_workspace_form() -> Box<WorkspaceForm>;
        fn workspace_copy() -> WorkspaceCopy;
        fn peek_copy() -> PeekCopy;
        fn peek_title(title: &[u16]) -> Vec<u16>;
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
        fn profile_rows(profiles: &[ProfileOption], uses: &[ProfileUse], current: &str) -> Vec<ProfileRow>;
        fn profile_confirm_copy(name: &str) -> String;
        fn open(self: &mut ProfileForm, selected: &str, workspace: &str);
        fn select(self: &mut ProfileForm, selected: &str);
        fn reconcile(self: &mut ProfileForm, profiles: &[ProfileOption], prefer_new: bool);
        fn set_status(self: &mut ProfileForm, status: &str);
        fn status(self: &ProfileForm) -> &str;
        fn selection(self: &ProfileForm) -> &str;
        fn controls(self: &mut ProfileForm, ready: bool, busy: bool, profiles: &[ProfileOption], uses: &[ProfileUse]) -> ProfileControls;
        fn request(self: &mut ProfileForm, action: u32, name: &str, ready: bool, busy: bool, profiles: &[ProfileOption], uses: &[ProfileUse]) -> ProfileRequest;
        fn finish_attempt(self: &mut ProfileForm, accepted: bool);
        fn cancel(self: &mut ProfileForm);
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
        fn sidebar_reorder_target(rows: &[ReorderRow], y: i32) -> ReorderTarget;
        fn tab_context_menu(state: &TabMenuState) -> Vec<TabMenuEntry>;
        fn link_context_menu(state: &LinkMenuState) -> Vec<LinkMenuEntry>;
        fn link_menu_enabled(command: i32, state: &LinkMenuState) -> bool;
        fn link_menu_checked(command: i32, state: &LinkMenuState) -> bool;
        fn link_menu_action(command: i32, state: &LinkMenuState) -> LinkMenuAction;
        fn folder_context_menu(state: &FolderMenuState) -> Vec<FolderMenuEntry>;
        fn palette_plan(input: &PalettePlanInput) -> PalettePlan;
        fn download_row(facts: &DownloadFacts) -> DownloadRow;
        fn command_catalogue(context: &CommandContext, query: &str) -> Vec<CommandEntry>;
        fn folder_menu_enabled(command: i32, state: &FolderMenuState) -> bool;
        fn folder_menu_action(command: i32, state: &FolderMenuState) -> FolderMenuAction;
        fn workspace_context_menu(state: &WorkspaceMenuState) -> Vec<WorkspaceMenuEntry>;
        fn workspace_menu_command(command: i32, profiles: usize) -> bool;
        fn workspace_menu_enabled(command: i32, state: &WorkspaceMenuState) -> bool;
        fn workspace_menu_checked(command: i32, state: &WorkspaceMenuState) -> bool;
        fn workspace_menu_action(command: i32, state: &WorkspaceMenuState) -> WorkspaceMenuAction;
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
        fn new_palette_graph() -> Box<ActionGraph>;
        fn palette_reset(self: &mut ActionGraph);
        fn palette_selected(self: &ActionGraph) -> usize;
        fn palette_moved(self: &ActionGraph) -> bool;
        fn palette_step(self: &mut ActionGraph, count: usize, backwards: bool) -> bool;
        fn palette_reconcile_selection(self: &mut ActionGraph, count: usize, preserved: usize, found: bool);
        fn reconcile_palette(self: &mut ActionGraph, items: Vec<PaletteSceneInput>) -> ActionNodePlan;
        fn palette_scene(self: &ActionGraph) -> PaletteScene;
        fn palette_target(self: &ActionGraph, key: &str, id: u64) -> PaletteTarget;
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
        fn pane_host_layout(width: i32, height: i32, weights: &[f64], vertical: bool) -> PaneHostLayout;
        fn split_pair_ratio(weights: &[f64], divider: usize) -> f64;
        fn split_apply_ratio(weights: &[f64], divider: usize, ratio: f64) -> Vec<f64>;
        fn split_normalized(weights: &[f64], count: usize) -> Vec<f64>;
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
                    members: &[usize], split: bool) -> TabPlan;
        fn collapsed(self: &SidebarTree, workspace: &str, id: &str) -> bool;
        fn set_collapsed(self: &mut SidebarTree, workspace: &str, id: &str, value: bool);
        fn toggle(self: &mut SidebarTree, workspace: &str, id: &str) -> bool;
        fn forget_workspace(self: &mut SidebarTree, workspace: &str);
        fn set_default_collapsed(self: &mut SidebarTree, value: bool);
        fn revealed(self: &SidebarTree, workspace: &str, id: &str) -> bool;
        fn set_revealed(self: &mut SidebarTree, workspace: &str, id: &str, value: bool);
        type ActionGraph;
        fn new_action_graph() -> Box<ActionGraph>;
        fn reconcile(self: &mut ActionGraph, keys: Vec<ActionNodeKey>) -> ActionNodePlan;
        fn reconcile_chrome(self: &mut ActionGraph, items: Vec<WorkspaceSceneInput>) -> ActionNodePlan;
        fn workspace_scene(self: &ActionGraph) -> WorkspaceScene;
        fn node_parent_id(self: &ActionControl) -> u64;
        fn owns_input(self: &ActionGraph, input: &ActionControl) -> bool;
        fn acquire(self: &mut ActionGraph, key: &ActionNodeKey) -> Box<ActionControl>;
        fn new_form_graph(vertical: bool, spacing: i32, intrinsic: bool, dialog: bool) -> Box<ActionGraph>;
        fn form_scene(self: &ActionGraph) -> FormScene;
        fn form_style(self: &ActionGraph) -> FormGroupStyle;
        fn form_set_shared(self: &mut ActionGraph, shared: bool) -> bool;
        fn reorder_form_actions(self: &mut ActionGraph, ids: Vec<u64>) -> bool;
        fn form_layout(self: &ActionGraph, width: i32, height: i32, preferred: bool, link_limit: i32, top: i32, left: i32, bottom: i32, right: i32, children: &[FormMeasure]) -> FormLayout;
        fn reconcile_site_rules(self: &mut ActionGraph, items: Vec<SiteRuleSceneInput>) -> ActionNodePlan;
        fn site_rule_scene(self: &ActionGraph) -> SiteRuleScene;
        fn site_rule_target(self: &ActionGraph, key: &str, id: u64) -> SiteRuleTarget;
        fn reconcile_sidebar(self: &mut ActionGraph, pins: Vec<SidebarPinInput>, tabs: Vec<SidebarTabInput>) -> ActionNodePlan;
        fn reconcile_sidebar_layout(self: &mut ActionGraph, pins: Vec<SidebarPinInput>, tabs: Vec<SidebarTabInput>, members: Vec<usize>, split: bool) -> ActionNodePlan;
        fn sidebar_tabs(self: &ActionGraph) -> SidebarTabTree;
        fn tab_binding(self: &ActionControl) -> SidebarTabBinding;
        fn pin_binding(self: &ActionControl) -> SidebarPinBinding;
        fn pin_tree(self: &ActionGraph) -> SidebarPinTree;
        fn pin_target(self: &ActionGraph, key: &str, id: u64) -> SidebarPinTarget;
        fn set_pin_collapsed(self: &mut ActionGraph, input: &ActionControl, collapsed: bool) -> bool;
        fn project_tab(self: &mut ActionGraph, input: &ActionControl, title: Vec<u16>, monogram: Vec<u16>, color: u32, tile: bool) -> SidebarTabUpdate;
        fn sidebar_visual(self: &ActionControl) -> SidebarRowVisual;
        fn sidebar_presentation(self: &ActionControl) -> SidebarRowPresentation;
        fn sidebar_title(self: &ActionControl) -> Vec<u16>;
        fn sidebar_set_title(self: &mut ActionControl, title: Vec<u16>) -> bool;
        fn sidebar_set_monogram(self: &mut ActionControl, monogram: Vec<u16>, color: u32) -> bool;
        fn sidebar_scene(self: &ActionControl, bounds: FloatRect, clip: FloatRect, style: SidebarRowStyle, close: bool, close_visible: bool) -> SidebarRowScene;
        fn tab_target(self: &ActionGraph, high: u64, low: u64, id: u64, close: bool) -> SidebarTabTarget;
        fn append_form_action(self: &mut ActionGraph) -> Box<ActionControl>;
        fn retire_form_action(self: &mut ActionGraph, id: u64) -> bool;
        type FormCommandTicket;
        fn take_command(self: &mut FormCommandTicket) -> FormCommandBinding;
        type ActionControl;
        fn defer_form_command(self: &ActionControl) -> Box<FormCommandTicket>;
        fn action_node_id(self: &ActionControl) -> u64;
        fn bind_appearance_preset(self: &mut ActionControl, emoji: bool, index: usize) -> bool;
        fn node_form_command_init(self: &mut ActionControl, command: FormCommand, key: &str) -> bool;
        fn node_form_command(self: &ActionControl) -> FormCommandBinding;
        fn node_form_view(self: &ActionControl) -> FormCommandView;
        fn action_admission_epoch(self: &ActionControl) -> u64;
        fn action_admits(self: &ActionControl, epoch: u64) -> bool;
        fn node_model_kind(self: &ActionControl) -> u32;
        fn node_checkbox_init(self: &mut ActionControl, checked: bool) -> bool;
        fn node_checked(self: &ActionControl) -> bool;
        fn node_set_checked(self: &mut ActionControl, checked: bool) -> bool;
        fn node_flip_checked(self: &mut ActionControl, enabled: bool) -> bool;
        fn node_toggle_init(self: &mut ActionControl) -> bool;
        fn node_toggle_checked(self: &ActionControl) -> bool;
        fn node_toggle_set(self: &mut ActionControl, checked: bool) -> bool;
        fn node_toggle_flip(self: &mut ActionControl, enabled: bool, now: u64, animate: bool) -> bool;
        fn node_toggle_hover(self: &mut ActionControl, hovered: bool, now: u64, animate: bool) -> bool;
        fn node_toggle_finish(self: &mut ActionControl) -> bool;
        fn node_toggle_view(self: &ActionControl, now: u64) -> NodeToggleView;
        fn new_action_control() -> Box<ActionControl>;
        // UI language: set once at startup from the locale; translate() looks a
        // Polish source string up for the active language.
        fn set_ui_language(locale: &str);
        fn ui_language_is_english() -> bool;
        fn translate(polish: &str) -> String;
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
        fn action_row_dragging(self: &ActionControl) -> bool;
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
#[cfg(test)]
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
fn peek_copy()->ffi::PeekCopy{ffi::PeekCopy{initial:"Peek".into(),back:t("Wstecz").into(),forward:t("Dalej").into(),promote:t("Otwórz jako kartę").into(),split:"Split View".into(),close:t("Zamknij podgląd (Escape)").into()}}
fn peek_title(title:&[u16])->Vec<u16>{"Peek · ".encode_utf16().chain(title.iter().copied()).collect()}

fn workspace_copy() -> ffi::WorkspaceCopy {
    ffi::WorkspaceCopy {
        name_label: t("Nazwa").into(), placeholder: t("Np. Projekt lub Klient").into(),
        profile_label: t("Profil przeglądania").into(), profile_ax: t("Profil przestrzeni").into(),
        profile_hint: t("Przestrzenie z tym samym profilem dzielą logowania i cookies. Zmiana profilu zamknie karty tej przestrzeni.").into(),
        save_hint: t("Zapisz poniżej nazwę i profil. Wygląd zapisuje się od razu.").into(),
        close: t("Zamknij ustawienia przestrzeni").into(), appearance: t("Wygląd · zapisuje się od razu").into(),
        color: t("Kolor").into(), color_ax: t("Kolor przestrzeni #RRGGBB").into(),
        emoji: t("Ikona").into(), emoji_ax: t("Emoji przestrzeni").into(), emoji_placeholder: t("Wpisz emoji").into(), more_emoji: t("Więcej emoji…").into(),
        more: t("Więcej opcji").into(), manage: t("Zarządzaj profilami").into(), import_label: t("Importuj z przeglądarki…").into(), remove_label: t("Usuń przestrzeń").into(),
    }
}
fn workspace_appearance_error(kind: u32) -> String {
    match kind { 0 => t("Nie udało się zapisać ustawień"), 1 => t("Nie udało się zapisać koloru"), 2 => t("Nie udało się zapisać emoji"), _ => t("Podaj kolor #RRGGBB") }.into()
}
const WORKSPACE_CONFIGURATION_ERROR: &str = "Nie udało się odczytać lub zapisać przestrzeni.";
impl WorkspaceForm {
    fn reset(&mut self, current: &str) { self.workspace=current.into(); self.creating=false; self.submitting=false; }
    fn begin_create(&mut self, current: &str) { self.workspace=current.into(); self.creating=true; self.submitting=false; self.error.clear(); }
    fn creating(&self) -> bool { self.creating }
    fn view(&self, name: &str, profile_name: &str, ready: bool, busy: bool, count: usize) -> ffi::WorkspaceFormView {
        let enabled=ready&&!busy&&!self.submitting;
        ffi::WorkspaceFormView { creating:self.creating, title:if self.creating { t("Nowa przestrzeń").into() } else { name.into() }, description:tf("Profil: {}",&[profile_name]),
            save_label:if self.creating { t("Utwórz przestrzeń") } else { t("Zapisz nazwę i profil") }.into(),
            secondary_label:if self.creating { t("Anuluj") } else { t("Nowa przestrzeń…") }.into(),
            appearance:!self.creating, enabled, remove_enabled:enabled&&!self.creating&&count>1, error:self.error.clone() }
    }
    fn set_configuration_error(&mut self, error: bool) { self.error=if error { t(WORKSPACE_CONFIGURATION_ERROR).into() } else { String::new() }; }
    fn submit(&mut self, raw_name: &str, profile: &str, current: &str, ready: bool, busy: bool, composing: bool, profiles: &[ffi::ProfileOption], workspaces: &[ffi::ProfileUse]) -> ffi::WorkspaceFormRequest {
        let none=||ffi::WorkspaceFormRequest { action:ffi::WorkspaceFormAction::None, workspace:String::new(), name:String::new(), profile:String::new() };
        if composing||self.submitting { return none(); }
        if !ready||busy { self.error=t("Poczekaj na zakończenie zapisywania.").into(); return none(); }
        if self.workspace.is_empty()||self.workspace!=current||!workspaces.iter().any(|w|w.workspace==self.workspace) { self.error=t("Przestrzeń zmieniła się. Otwórz ustawienia ponownie.").into(); return none(); }
        let name=raw_name.trim_matches(|c|matches!(c,' '|'\t'|'\n'|'\r'|'\x0b'|'\x0c'));
        if name.is_empty() { self.error=t("Podaj nazwę przestrzeni.").into(); return none(); }
        if profile.is_empty()||!profiles.iter().any(|p|p.id==profile) { self.error=t("Wybierz istniejący profil.").into(); return none(); }
        self.error.clear(); self.submitting=true;
        ffi::WorkspaceFormRequest { action:if self.creating { ffi::WorkspaceFormAction::Create } else { ffi::WorkspaceFormAction::Edit }, workspace:self.workspace.clone(), name:name.into(), profile:profile.into() }
    }
    fn finish_attempt(&mut self, accepted: bool) {
        self.submitting=false;
        if accepted { self.creating=false; self.error.clear(); }
        else if self.error.is_empty() { self.error=t(WORKSPACE_CONFIGURATION_ERROR).into(); }
    }
}

pub struct ProfileForm { selected:String, workspace:String, confirmation:String, status:String, submitting:bool }
fn new_profile_form()->Box<ProfileForm>{Box::new(ProfileForm{selected:String::new(),workspace:String::new(),confirmation:String::new(),status:String::new(),submitting:false})}
fn profile_copy()->ffi::ProfileCopy {ffi::ProfileCopy{title:t("Profile przeglądania").into(),hint:t("Każdy profil ma osobne logowania, cookies i dane stron. Jeden profil może obsługiwać kilka przestrzeni.").into(),name_label:t("Nowy profil").into(),name_ax:t("Nazwa nowego profilu").into(),placeholder:t("Np. Klient, Projekt, Konto testowe").into(),rename_ax:t("Nowa nazwa profilu").into(),create:t("Utwórz profil").into(),rename:t("Zapisz").into(),cancel:t("Anuluj").into(),remove_label:t("Usuń profil i dane").into(),back:t("Wróć do przestrzeni").into()}}
fn spaces_count(n:usize)->String{let (u,h)=(n%10,n%100);if n==1{t("1 przestrzeń").into()}else if (2..=4).contains(&u)&&!(12..=14).contains(&h){tf("{} przestrzenie",&[&n.to_string()])}else{tf("{} przestrzeni",&[&n.to_string()])}}
fn profile_rows(profiles:&[ffi::ProfileOption],uses:&[ffi::ProfileUse],current:&str)->Vec<ffi::ProfileRow>{
    profiles.iter().map(|p|{
        let names:Vec<&str>=uses.iter().filter(|w|w.profile==p.id).map(|w|w.name.as_str()).collect();
        let usage=if names.is_empty(){t("Nieprzypisany do żadnej przestrzeni").to_string()}else{format!("{}: {}",spaces_count(names.len()),names.join(", "))};
        let current=!current.is_empty()&&p.id==current;
        ffi::ProfileRow{id:p.id.clone(),name:p.name.clone(),initial:p.name.chars().next().map(|c|c.to_uppercase().collect()).unwrap_or_default(),detail:if current{tf("Ta przestrzeń · {}",&[&usage])}else{usage},current,rename_ax:tf("Zmień nazwę profilu {}",&[&p.name]),remove_ax:tf("Usuń profil {}",&[&p.name])}
    }).collect()
}
fn profile_confirm_copy(name:&str)->String{tf("Usunąć profil „{}”? Logowania, cookies i dane stron tego profilu zostaną trwale usunięte. Tej operacji nie można cofnąć.",&[name])}
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
        ffi::ProfileControls{create:enabled,edit:enabled,rename:enabled&&self.workspace.is_empty(),confirming:self.confirmation.clone(),status:self.status.clone()}
    }
    fn request(&mut self,action:u32,raw_name:&str,ready:bool,busy:bool,profiles:&[ffi::ProfileOption],uses:&[ffi::ProfileUse])->ffi::ProfileRequest {
        let none=||ffi::ProfileRequest{action:ffi::ProfileAction::None,id:String::new(),name:String::new(),workspace:String::new()};
        if !ready||busy||self.submitting{self.confirmation.clear();self.status=t("Poczekaj na zakończenie operacji na profilach.").into();return none();}
        if action>2{return none();}
        let selected=profiles.iter().find(|p|p.id==self.selected);
        if action==2 {
            if selected.is_none(){self.confirmation.clear();self.status=t("Wybierz istniejący profil.").into();return none();}
            if uses.iter().any(|w|w.profile==self.selected){self.confirmation.clear();self.status=tf("Profil jest używany przez: {}. Przypisz tym przestrzeniom inny profil, aby go usunąć.",&[&self.used_names(uses)]);return none();}
            if profiles.len()<=1{self.confirmation.clear();self.status=t("Nie można usunąć ostatniego profilu.").into();return none();}
            if self.confirmation!=self.selected {self.confirmation=self.selected.clone();self.status.clear();return ffi::ProfileRequest{action:ffi::ProfileAction::ConfirmDelete,id:self.selected.clone(),name:String::new(),workspace:String::new()};}
            self.confirmation.clear();self.status.clear();self.submitting=true;return ffi::ProfileRequest{action:ffi::ProfileAction::Delete,id:self.selected.clone(),name:String::new(),workspace:String::new()};
        }
        self.confirmation.clear();let name=raw_name.trim();
        if name.is_empty(){self.status=t("Podaj nazwę profilu.").into();return none();}
        if action==1&&(selected.is_none()||!self.workspace.is_empty()){self.status=t("Wybierz profil w trybie zarządzania.").into();return none();}
        if action==0&&!self.workspace.is_empty()&&!uses.iter().any(|w|w.workspace==self.workspace){self.status=t("Przestrzeń nie jest już dostępna.").into();return none();}
        if profiles.iter().any(|p|(action==0||p.id!=self.selected)&&p.name.eq_ignore_ascii_case(name)){self.status=t("Profil o tej nazwie już istnieje.").into();return none();}
        self.status.clear();self.submitting=true;ffi::ProfileRequest{action:if action==0{ffi::ProfileAction::Create}else{ffi::ProfileAction::Rename},id:if action==0{String::new()}else{self.selected.clone()},name:name.into(),workspace:if action==0{self.workspace.clone()}else{String::new()}}
    }
    fn finish_attempt(&mut self,accepted:bool){self.submitting=false;if !accepted&&self.status.is_empty(){self.status=t("Nie udało się wykonać operacji na profilu. Spróbuj ponownie.").into();}}
    // Leaves rename or delete confirmation; the selection stays on its row.
    fn cancel(&mut self){self.confirmation.clear();self.status.clear();}
}
fn paragraph_layout(width:i32,line_height:i32,count:usize)->ffi::ColumnLayout{let g=notice_layout(width,line_height,count,0,0);ffi::ColumnLayout{width:g.width,height:g.height,children:g.body_lines}}

// Stable UI keys retain selection through model changes. Popup generations and
// captured keys reject stale native callbacks. No browser data crosses.
struct SelectState { options: Vec<ffi::SelectOption>, selected: usize, epoch: u64, expanded: bool, prefix: String, last_input: Option<u64>, last_close: Option<u64> }
fn initial_select_state()->SelectState {SelectState{options:vec![],selected:0,epoch:0,expanded:false,prefix:String::new(),last_input:None,last_close:None}}
#[cfg(test)]
fn new_select_state(options: Vec<ffi::SelectOption>) -> Box<SelectState> {let mut state=Box::new(initial_select_state());state.replace_options(options,false,false);state}
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
    ffi::PageDialogCopy{title:if unload{tf("Opuścić stronę? · {}",&[origin])}else{tf("Strona: {}",&[origin])},identity:tf("Profil: {}",&[profile]),message:message.iter().copied().take(8192).collect(),accept:if unload{t("Opuść stronę")}else{"OK"}.into(),cancel:if unload{t("Zostań")}else{t("Anuluj")}.into(),show_cancel:kind!=0,default_cancel:unload,prompt:kind==2}
}

// Page copy is UI data only. Page 1 is shortcuts, page 2 search providers
// (page 0, snapshots, became the Time Machine view).
fn tools_copy(page:usize)->ffi::ToolsCopy{
    match page.clamp(1,2){
        1=>ffi::ToolsCopy{title:t("Skróty klawiszowe").into(),intro:t("Schemat inspirowany Arc. Wybierz akcję, nagraj kombinację lub wpisz np. Cmd+Shift+J. Puste pole wyłącza skrót. Escape, skróty edycji tekstu i systemowe pozostają zarezerwowane.").into(),tail:"".into(),initial_status:t("Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.").into(),fields:vec![t("Kombinacja, np. Cmd+Shift+J").into()],pickers:vec![t("Akcja skrótu").into()],headings:vec![],actions:vec![t("Nagraj kombinację…").into(),t("Zapisz skrót").into(),t("Wyłącz wybrany skrót").into(),t("Przywróć schemat Arc").into()],choices:vec![]},
        2=>ffi::ToolsCopy{title:t("Wyszukiwarki i prefiksy").into(),intro:t("W pasku adresu wpisz prefiks i zapytanie, np. g koty lub w Warszawa. Szablon musi być adresem HTTP(S) z jednym znacznikiem {query}. Zapytania są kodowane automatycznie.").into(),tail:"".into(),initial_status:t("Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.").into(),fields:vec![t("Nazwa wyszukiwarki").into(),t("Prefiks (a–z, 0–9)").into(),t("Adres z {query}").into()],pickers:vec![t("Wyszukiwarka do edycji").into()],headings:vec![],actions:vec![t("Zapisz wyszukiwarkę").into(),t("Ustaw jako domyślną").into(),t("Usuń wyszukiwarkę").into()],choices:vec![tools_new_provider().into()]},
        _=>unreachable!(),
    }
}

// UI state only. Native code supplies committed, normalized UTF-16 and retains
// visibility/profile authorization, timers, IME and all browser-service calls.
// Reconstructing a page clears its transient query/recording state.
struct ToolShortcutChoice { key:String, id:String, chord:Vec<u16> }
struct ToolProviderChoice { key:String, item:Option<ffi::ToolsProviderItem> }
fn tools_invalid_choice()->ffi::ToolsChoiceDraft {ffi::ToolsChoiceDraft{valid:false,page:0,create:false,target:String::new(),chord:vec![],name:vec![],keyword:vec![],url:vec![],stop_recording:false}}
fn tools_new_provider()->&'static str{t("Dodaj nową wyszukiwarkę…")}
pub struct ToolsForm {
    page: usize,
    recording: bool,
    choice_epoch: u64,
    active_choice: String,
    shortcut_choices: Vec<ToolShortcutChoice>,
    provider_choices: Vec<ToolProviderChoice>,
}
fn tools_status(status: ffi::ToolsStatus) -> String {
    use ffi::ToolsStatus as S;
    match status {S::WriteFailed=>t("Nie udało się zapisać ustawień na dysku."),_=>""}.into()
}
fn new_tools_form() -> Box<ToolsForm> {
    Box::new(ToolsForm { page: 1, recording: false,
        choice_epoch: 0, active_choice:String::new(), shortcut_choices:vec![], provider_choices:vec![] })
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
        let page=page.clamp(1,2);
        if self.page!=page{self.shortcut_choices.clear();self.provider_choices.clear();self.active_choice.clear();}
        self.page=page;
    }
    fn tools_page(&self) -> usize { self.page }
    fn tools_reset(&mut self) {
        self.recording = false;
        self.shortcut_choices.clear();self.provider_choices.clear();self.active_choice.clear();
    }
    fn tools_record(&mut self, recording: bool) { self.recording = recording; }
    fn tools_recording(&self) -> bool { self.recording }
    // A captured chord is admitted only while recording; Escape and a valid
    // chord end recording, an invalid chord keeps it for another attempt.
    fn tools_capture(&mut self, capture: ffi::ToolsCapture) -> ffi::ToolsCaptureResult {
        use ffi::ToolsCapture as C;
        let rejected=||ffi::ToolsCaptureResult{admitted:false,stop_recording:false,accept_chord:false,status:String::new()};
        if !self.recording {return rejected();}
        let (stop,accept,status)=match capture {
            C::Escape=>(true,false,t("Nagrywanie anulowane.")),
            C::Invalid=>(false,false,t("Użyj Cmd / Ctrl / Alt i klawisza. Skróty edycji tekstu oraz systemowe są zarezerwowane.")),
            C::Valid=>(true,true,t("Kombinacja przechwycona. Wybierz Zapisz skrót.")),
            _=>return rejected(),
        };
        if stop {self.recording=false;}
        ffi::ToolsCaptureResult{admitted:true,stop_recording:stop,accept_chord:accept,status:status.into()}
    }
    // These are UI command IDs and field/selection facts, never domain handles.
    // Native visibility/profile/workspace/generation checks and bounds checks
    // remain mandatory before applying a command to real browser services.
    fn tools_dispatch(&mut self, action:i32, c:&ffi::ToolsActionContext) -> ffi::ToolsDispatch {
        let composing=match action {11|12=>c.shortcut_composing,20..=22=>c.provider_composing,_=>false};
        if composing {return ffi::ToolsDispatch{admitted:false,action:0,recording:self.recording,sync_recording:false,record_status:String::new()};}
        let allowed=match action {
            13=>true,10=>c.shortcut_field,11|12=>c.shortcut_field&&c.shortcut_selected,
            20..=22=>c.provider_field&&c.provider_selected,_=>false,
        };
        let command=if allowed{action}else{0};
        let sync_recording=(action!=10&&self.recording)||command==10;
        if action!=10 {self.recording=false;} else if command==10 {self.recording=true;}
        ffi::ToolsDispatch{admitted:true,action:command,recording:self.recording,sync_recording,
            record_status:if command==10{t("Naciśnij kombinację. Escape anuluje nagrywanie.").into()}else{String::new()}}
    }
    fn tools_outcome(&self,ok:bool,pending:bool)->ffi::ToolsOutcome {
        let status=if ok {if pending{t("Oczekiwanie na zgodę na opuszczenie strony…")}else{t("Gotowe. Zmiany zostały zastosowane.")}}
            else {match self.page {1=>t("Nieprawidłowa lub zajęta kombinacja. Zmień ją albo wyłącz skrót drugiej akcji."),_=>t("Sprawdź unikalny prefiks, nazwę i adres z {query}. Musi pozostać co najmniej jedna wyszukiwarka.")}};
        ffi::ToolsOutcome{refresh:ok,status:status.into()}
    }
    fn tools_form_plan(&self)->ffi::ToolsFormPlan {
        use ffi::ToolsNodeKind as K;use ffi::ToolsNodeRole as R;
        let copy=tools_copy(self.page);let mut nodes=vec![tools_form_node(K::Hint,R::None,&copy.intro,0)];
        match self.page {
            1=>{nodes.push(tools_form_node(K::Choice,R::ShortcutChoice,&copy.pickers[0],0));nodes.push(tools_form_node(K::Editor,R::ShortcutChord,&copy.fields[0],0));for (i,text) in copy.actions.iter().enumerate(){nodes.push(tools_form_node(K::Action,R::None,text,10+i as i32));}}
            _=>{nodes.push(tools_form_node(K::Choice,R::ProviderChoice,&copy.pickers[0],0));for (i,role) in [R::ProviderName,R::ProviderKeyword,R::ProviderUrl].into_iter().enumerate(){nodes.push(tools_form_node(K::Editor,role,&copy.fields[i],0));}for (i,text) in copy.actions.iter().enumerate(){nodes.push(tools_form_node(K::Action,R::None,text,20+i as i32));}}
        }
        ffi::ToolsFormPlan{title:copy.title.encode_utf16().collect(),status:copy.initial_status.encode_utf16().collect(),nodes:tools_group_actions(nodes)}
    }
    fn tools_shortcut_choices(&mut self,items:Vec<ffi::ToolsShortcutItem>)->ffi::ToolsFormChoices {
        self.shortcut_choices.clear();self.active_choice.clear();let mut choices=vec![];
        if let Some(epoch)=self.choice_epoch.checked_add(1){self.choice_epoch=epoch;
            for item in items {let key=format!("form:{}:1:{}",epoch,choices.len());let mut label=item.title;label.extend(" — ".encode_utf16());label.extend_from_slice(&item.chord);self.shortcut_choices.push(ToolShortcutChoice{key:key.clone(),id:item.id,chord:item.chord});choices.push(ffi::ToolsFormChoice{key,label});}
        }
        let enabled=!choices.is_empty();if enabled{self.active_choice=choices[0].key.clone();}else{choices.push(ffi::ToolsFormChoice{key:"empty".into(),label:t("Brak pozycji").encode_utf16().collect()});}
        ffi::ToolsFormChoices{choices,enabled}
    }
    fn tools_provider_choices(&mut self,items:Vec<ffi::ToolsProviderItem>)->ffi::ToolsFormChoices {
        self.provider_choices.clear();self.active_choice.clear();let mut choices=vec![];
        if let Some(epoch)=self.choice_epoch.checked_add(1){self.choice_epoch=epoch;
            let key=format!("form:{}:2:0",epoch);self.provider_choices.push(ToolProviderChoice{key:key.clone(),item:None});choices.push(ffi::ToolsFormChoice{key,label:tools_new_provider().encode_utf16().collect()});
            for item in items {let key=format!("form:{}:2:{}",epoch,choices.len());let mut label=item.keyword.clone();label.extend(" · ".encode_utf16());label.extend_from_slice(&item.name);if item.default_provider{label.extend(t(" · domyślna").encode_utf16());}self.provider_choices.push(ToolProviderChoice{key:key.clone(),item:Some(item)});choices.push(ffi::ToolsFormChoice{key,label});}
        }
        let enabled=!choices.is_empty();if enabled{self.active_choice=choices[0].key.clone();}else{choices.push(ffi::ToolsFormChoice{key:"empty".into(),label:t("Brak pozycji").encode_utf16().collect()});}
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
    let (button,button_ax)=match kind {K::Text=>(t("Zapisz"),tf("Zapisz: {}",&[title])),K::Action=>(t("Otwórz →"),title.into()),K::Directory=>(t("Zmień…"),t("Zmień folder pobierania").into()),_=>("",String::new())};
    ffi::PreferenceSpec{category,kind,key:key.into(),title:title.into(),detail:detail.into(),choices,placeholder:placeholder.into(),action,button:button.into(),button_ax,tooltip:if kind==K::ShortcutHint{t("Skróty klawiaturowe Zitter").into()}else{String::new()}}
}
fn preference_site_policies(index:usize)->Vec<String> {
    let mut choices=vec![t("Użyj domyślnej").into(),t("Zezwalaj").into(),t("Blokuj").into()];if index>=4{choices.push(t("Pytaj").into());}choices
}
fn preference_catalogue(system_certificates:bool)->ffi::PreferenceCatalogue {
    use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;
    let mut rows=vec![];
    rows.push(preference_spec(0,K::Theme,"",t("Motyw aplikacji"),t("Kolory interfejsu i stron obsługujących motyw systemowy."),vec![t("Systemowy").into(),t("Jasny").into(),t("Ciemny").into()],"",A::None));
    rows.push(preference_spec(0,K::Pinned,"",t("Zawsze widoczny panel kart"),t("Przypnij karty i przestrzenie przy lewej krawędzi."),vec![],"",A::None));
    rows.push(preference_spec(0,K::Animations,"",t("Animacje przestrzeni"),t("Płynne przejścia. Systemowe ograniczenie ruchu ma pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(0,K::WorkspaceNames,"",t("Nazwa aktywnej przestrzeni"),t("Pokazuj nazwę obok kropki aktywnej przestrzeni na dole panelu kart."),vec![],"",A::None));
    rows.push(preference_spec(0,K::Language,"",t("Język interfejsu"),t("Zmiana zadziała po ponownym uruchomieniu Zitter."),vec![t("Systemowy").into(),"Polski".into(),"English".into()],"",A::None));
    rows.push(preference_spec(0,K::FoldersCollapsed,"",t("Zwinięte foldery przypiętych"),t("Foldery startują zwinięte. Karta otwarta z Command Center pokazuje się pod folderem bez rozwijania reszty."),vec![],"",A::None));
    rows.push(preference_spec(1,K::Search,"",t("Domyślna wyszukiwarka"),t("Używana, gdy wpiszesz zapytanie w pasku adresu."),vec!["DuckDuckGo".into(),"Google".into(),"Bing".into()],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"referrers",t("Przekazuj adres strony odsyłającej"),t("Nagłówek Referer pomaga witrynie rozpoznać, skąd pochodzi wejście. Wyłączenie może zmienić działanie niektórych serwisów."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"https_only",t("Zawsze używaj HTTPS"),t("Wymagaj bezpiecznego połączenia. Witryny bez HTTPS mogą wyświetlić ostrzeżenie."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"third_party_cookies",t("Blokuj cookies innych firm"),t("Ogranicz śledzenie między witrynami. Niektóre logowania mogą wymagać cookies."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"do_not_track",t("Wysyłaj Do Not Track"),t("Poproś witryny o nieśledzenie. Respektowanie prośby zależy od witryny."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"fingerprint_protection",t("Ochrona przed fingerprintingiem"),t("Dodawaj niewidoczny szum do odczytów canvas, WebGL i dźwięku. Każda witryna widzi wtedy inny odcisk przeglądarki, nowy po ponownym uruchomieniu lub po zmianie ikoną odcisku w pasku. Może zakłócić edytory grafiki. Dotyczy tego profilu."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"preconnect",t("Wstępne połączenia ze stronami"),t("Przygotowuj połączenie podczas wpisywania adresu. Może użyć sieci, nawet jeśli nie otworzysz strony. Ustawienie dotyczy tego profilu."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Toggle,"link_auditing",t("Raporty kliknięć w linki"),t("Zezwalaj na wysyłanie sygnałów ping po kliknięciu odnośnika."),vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"camera",t("Kamera"),t("Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"microphone",t("Mikrofon"),t("Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"location",t("Lokalizacja"),t("Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"notifications",t("Powiadomienia"),t("Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(3,K::Toggle,"clipboard",t("Odczyt schowka"),t("Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."),vec![],"",A::None));
    rows.push(preference_spec(4,K::Picker,"zoom",t("Domyślne powiększenie"),t("Skala stron bez własnego, zapisanego powiększenia."),vec!["75%".into(),"90%".into(),"100%".into(),"110%".into(),"125%".into(),"150%".into(),"175%".into(),"200%".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"font_size",t("Rozmiar tekstu"),t("Domyślna wielkość czcionki na stronach."),vec!["12 px".into(),"14 px".into(),"16 px".into(),"18 px".into(),"20 px".into(),"24 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"minimum_font_size",t("Minimalny rozmiar tekstu"),t("Zapobiegaj wyświetlaniu zbyt drobnych liter."),vec![t("Bez limitu").into(),"10 px".into(),"12 px".into(),"14 px".into(),"16 px".into(),"18 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Picker,"fixed_font_size",t("Rozmiar tekstu o stałej szerokości"),t("Domyślna wielkość tekstu w blokach kodu i elementach monospace."),vec!["12 px".into(),"14 px".into(),"16 px".into(),"18 px".into(),"20 px".into(),"24 px".into()],"",A::None));
    rows.push(preference_spec(4,K::Text,"font_standard",t("Czcionka standardowa"),t("Nazwa rodziny czcionki używanej, gdy witryna nie wybierze własnej."),vec![],t("Np. Times New Roman"),A::None));
    rows.push(preference_spec(4,K::Text,"font_serif",t("Czcionka szeryfowa"),t("Rodzina dla tekstu serif. Musi być zainstalowana w systemie."),vec![],t("Np. Georgia"),A::None));
    rows.push(preference_spec(4,K::Text,"font_sans",t("Czcionka bezszeryfowa"),t("Rodzina dla tekstu sans-serif. Musi być zainstalowana w systemie."),vec![],t("Np. Arial"),A::None));
    rows.push(preference_spec(4,K::Text,"font_fixed",t("Czcionka o stałej szerokości"),t("Rodzina monospace, używana m.in. w blokach kodu."),vec![],t("Np. Menlo"),A::None));
    rows.push(preference_spec(4,K::Toggle,"javascript","JavaScript",t("Skrypty potrzebne do działania większości aplikacji internetowych."),vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"images",t("Wyświetlaj obrazy"),t("Pozwól witrynom ładować zdjęcia i grafiki."),vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"popups",t("Wyskakujące okna"),t("Pozwól stronom otwierać dodatkowe okna. Domyślnie zablokowane."),vec![],"",A::None));
    rows.push(preference_spec(4,K::Toggle,"sound",t("Dźwięk na stronach"),t("Domyślna zgoda na odtwarzanie dźwięku."),vec![],"",A::None));
    rows.push(preference_spec(5,K::Toggle,"save_passwords",t("Proponuj zapisywanie haseł"),t("Zapisuj dane logowania w menedżerze haseł tego profilu."),vec![],"",A::None));
    rows.push(preference_spec(5,K::Toggle,"auto_signin",t("Automatyczne logowanie"),t("Korzystaj z zapisanych danych logowania, gdy witryna na to pozwala."),vec![],"",A::None));
    rows.push(preference_spec(2,K::Action,"",t("Historia przeglądania"),t("Wyszukuj odwiedzone strony i czyść historię tego profilu."),vec![],"",A::History));
    rows.push(preference_spec(0,K::Action,"",t("Kolor i nazwa przestrzeni"),t("Własny akcent kolorystyczny i przypisanie profilu."),vec![],"",A::Workspace));
    rows.push(preference_spec(5,K::Action,"",t("Zapisane hasła"),t("Przeglądaj i usuwaj loginy aktywnego profilu."),vec![],"",A::Passwords));
    rows.push(preference_spec(5,K::Action,"",t("Profile przeglądania"),t("Oddziel cookies, historię i dane logowania między profilami."),vec![],"",A::Profiles));
    rows.push(preference_spec(5,K::Action,"",t("Import z przeglądarki"),t("Przenieś profile, przestrzenie, foldery i zakładki."),vec![],"",A::Import));
    rows.push(preference_spec(6,K::Toggle,"download_prompt",t("Pytaj, gdzie zapisać każdy plik"),t("Wybieraj miejsce zapisu przy każdym pobieraniu."),vec![],"",A::None));
    rows.push(preference_spec(6,K::Directory,"",t("Folder pobierania"),t("Domyślne miejsce zapisu plików w tym profilu."),vec![],"",A::None));
    rows.push(preference_spec(7,K::Text,"accept_languages",t("Preferowane języki witryn"),t("Kody języków w kolejności preferencji, oddzielone przecinkami. Np. pl, en-US, en. Zmiana dotyczy treści witryn; język interfejsu Zitter wybierasz osobno."),vec![],"pl, en-US, en",A::None));
    rows.push(preference_spec(7,K::Toggle,"spellcheck",t("Sprawdzaj pisownię"),t("Podkreślaj błędy w polach tekstowych na stronach. Na macOS słowniki wybiera system."),vec![],"",A::None));
    rows.push(preference_spec(7,K::Toggle,"online_spellcheck",t("Rozszerzone sprawdzanie pisowni"),t("Wysyłaj wpisywany tekst do usługi Google w celu sprawdzania pisowni. Wymaga włączonego sprawdzania pisowni."),vec![],"",A::None));
    rows.push(preference_spec(9,K::Toggle,"caret_browsing",t("Nawigacja kursorem tekstowym"),t("Poruszaj się po tekstach witryn za pomocą klawiatury i zaznaczaj je bez myszy."),vec![],"",A::None));
    rows.push(preference_spec(9,K::Toggle,"hardware_acceleration",t("Akceleracja sprzętowa"),t("Używaj procesora graficznego do renderowania. Ustawienie wspólne dla całej aplikacji; działa po ponownym uruchomieniu Zitter."),vec![],"",A::None));
    rows.push(preference_spec(9,K::Picker,"dns_mode",t("Bezpieczny DNS"),t("Wspólne dla całej aplikacji. Tryb automatyczny może używać DNS systemowego. Tryb ścisły wymaga wcześniej zapisanego adresu DNS-over-HTTPS."),vec![t("Systemowy").into(),t("Automatyczny").into(),t("Ścisły (DoH)").into()],"",A::None));
    rows.push(preference_spec(9,K::Text,"dns_templates",t("Adres serwera DNS-over-HTTPS"),t("Wspólny dla całej aplikacji. Wpisz szablon HTTPS, np. https://dns.google/dns-query{?dns}. Najpierw zapisz adres, następnie włącz tryb ścisły."),vec![],"https://…/dns-query{?dns}",A::None));
    rows.push(preference_spec(9,K::Action,"",t("Ustawienia proxy systemowego"),t("Zitter korzysta z konfiguracji proxy systemu operacyjnego."),vec![],"",A::Proxy));
    if system_certificates{rows.push(preference_spec(9,K::Action,"",t("Certyfikaty systemowe"),t("Otwórz systemowy magazyn certyfikatów i ustawienia zaufania."),vec![],"",A::Certificates));}
    rows.push(preference_spec(3,K::SiteEditor,"",t("Wyjątek dla witryny"),t("Wpisz pełny adres witryny bez ścieżki, np. https://example.com. Wyjątek zastępuje politykę domyślną w tym profilu."),vec![],"",A::None));
    rows.push(preference_spec(3,K::SiteRules,"",t("Zapisane wyjątki"),t("Własne reguły możesz usunąć, aby wrócić do ustawień domyślnych. Reguły zarządzane pozostają tylko do odczytu."),vec![],"",A::None));
    rows.push(preference_spec(8,K::ClearData,"",t("Wyczyść dane przeglądania"),t("Dotyczy wyłącznie aktywnego profilu. Pobrane pliki, zapisane hasła i przypięte karty pozostaną zachowane. Przed usunięciem zobaczysz potwierdzenie."),vec![],"",A::None));
    rows.push(preference_spec(1,K::Action,"",t("Wyszukiwarki i prefiksy"),t("Własne wyszukiwarki: g zapytanie, w hasło, yt film…"),vec![],"",A::Providers));
    rows.push(preference_spec(1,K::Action,"",t("Skróty klawiszowe"),t("Schemat Arc, własne kombinacje i wykrywanie konfliktów."),vec![],"",A::Shortcuts));
    rows.push(preference_spec(0,K::TimeMachine,"",t("Zapisuj historię kart"),t("Time Machine pamięta, jakie karty były otwarte w każdej przestrzeni. Tylko adresy, tytuły i układ, nigdy treść stron. Okna prywatne nie są zapisywane."),vec![],"",A::None));
    rows.push(preference_spec(0,K::TimeMachineDays,"",t("Przechowuj historię kart"),t("Starsze zmiany znikają same."),vec![t("7 dni").into(),t("30 dni").into(),t("90 dni").into()],"",A::None));
    rows.push(preference_spec(0,K::Action,"",t("Time Machine przestrzeni"),t("Cofnij przestrzeń do dowolnej chwili z historii kart."),vec![],"",A::TimeMachine));
    let mut clear=preference_spec(0,K::Action,"",t("Wyczyść historię kart"),t("Usuń historię Time Machine wszystkich przestrzeni na tym komputerze."),vec![],"",A::ClearTimeline);
    clear.button=t("Wyczyść").into();rows.push(clear);
    rows.push(preference_spec(1,K::ShortcutHint,"",t("Szybciej z klawiaturą"),t("⌘ L  adres i wyszukiwanie\n⌘ K  karty, przestrzenie i historia\n⌘ T  nowa karta     ⌘ ,  preferencje"),vec![],"",A::None));
    ffi::PreferenceCatalogue{
        categories:vec![t("Wygląd").into(),t("Wyszukiwanie").into(),t("Prywatność").into(),t("Uprawnienia stron").into(),t("Treści i czytelność").into(),t("Hasła i profile").into(),t("Pobieranie").into(),t("Języki i pisownia").into(),t("Dane przeglądania").into(),t("Zaawansowane").into()],descriptions:vec![t("Dopasuj Zitter do swojego rytmu.").into(),t("Wybierz, gdzie zaczyna się każde wyszukiwanie.").into(),t("Zdecyduj, jakie ślady zostawiasz w sieci.").into(),t("Ustal domyślny dostęp witryn do urządzenia.").into(),t("Wygodne czytanie i kontrola zawartości stron.").into(),t("Oddzielne logowania, wspólne możliwości.").into(),t("Wybierz miejsce dla pobieranych plików.").into(),t("Wybierz języki witryn i pomoc podczas pisania.").into(),t("Kontroluj dane zapisane w bieżącym profilu.").into(),t("Dostępność, połączenia i ustawienia silnika Chromium.").into()],rows,
        site:ffi::PreferenceSiteCopy{origin_ax:t("Adres witryny dla wyjątku").into(),placeholder:"https://example.com".into(),type_ax:t("Rodzaj uprawnienia dla witryny").into(),policy_ax:t("Polityka dla witryny").into(),save:t("Zapisz wyjątek").into(),types:vec!["JavaScript".into(),t("Obrazy").into(),t("Wyskakujące okna").into(),t("Dźwięk").into(),t("Powiadomienia").into(),t("Lokalizacja").into(),t("Kamera").into(),t("Mikrofon").into(),t("Schowek").into()]},
        clear:ffi::PreferenceClearCopy{period_ax:t("Zakres czasu czyszczenia danych").into(),periods:vec![t("Ostatnia godzina").into(),t("Ostatnie 24 godziny").into(),t("Ostatnie 7 dni").into(),t("Cały okres").into()],checks:vec![t("Historia przeglądania").into(),t("Cookies i dane witryn (może wylogować)").into(),t("Pamięć podręczna").into()],action:t("Wyczyść…").into()}
    }
}
pub struct PreferencesForm { categories: usize, selected: usize, query: Vec<u16>, profile: String, dirty: std::collections::BTreeSet<String>, rule_limit: usize, status: (&'static str, bool) }
fn new_preferences_form(categories:usize)->Box<PreferencesForm>{Box::new(PreferencesForm{categories,selected:0,query:vec![],profile:String::new(),dirty:Default::default(),rule_limit:50,status:(t(PREFERENCE_IDLE),false)})}
// Availability and managed-state copy for profile-scoped preference controls.
fn preference_control_view(kind:ffi::PreferenceControlKind,present:bool,editable:bool)->ffi::PreferenceControlView{
    use ffi::PreferenceControlKind as K;let toggle=kind==K::Toggle;
    let tooltip=if !present{if toggle{t("Profil nie jest jeszcze gotowy")}else{t("Ustawienie jest niedostępne")}}else if !editable{t("To ustawienie jest zarządzane")}else if toggle{t("Dotyczy aktywnego profilu")}else{t("Zapisz przyciskiem lub klawiszem Enter")};
    ffi::PreferenceControlView{enabled:present&&editable,tooltip:tooltip.into()}
}
// Import services report stages and counts; Rust owns the user-facing wording.
// Service-provided errors are already localized and are shown verbatim.
fn import_status_copy(r:&ffi::ImportStatusReport)->String{
    use ffi::ImportStage as S;
    match r.stage {
        S::Reading=>tf("Odczytywanie danych z {}…",&[&r.browser]),
        S::Preview=>{let mut text=if r.error.is_empty(){tf("Do importu: {} profili, {} przestrzeni, {} folderów i {} kart lub zakładek.",&[&r.profiles.to_string(),&r.spaces.to_string(),&r.folders.to_string(),&r.tabs.to_string()])}else{r.error.clone()};if r.skipped!=0{text.push_str(&tf(" Pominięte elementy: {}.",&[&r.skipped.to_string()]));}text}
        S::Importing=>t("Importowanie wybranych danych…").into(),
        S::SavingBrowsingData=>t("Zapisywanie historii i ciasteczek…").into(),
        S::Finished=>{
            let mut text=if r.ok{tf("Import zakończony. Dodano {} profili i {} przestrzeni. Możesz wybrać je na liście przestrzeni.",&[&r.added_profiles.to_string(),&r.added_spaces.to_string()])}else{t("Import się nie powiódł. Dotychczasowe przestrzenie pozostały bez zmian. Spróbuj ponownie.").into()};
            if r.browsing_data{text.push_str(&tf(" Przeniesiono {} stron historii i {} ciasteczek.",&[&r.history.to_string(),&r.cookies.to_string()]));if r.data_skipped!=0{text.push_str(&tf(" Pominięto: {}.",&[&r.data_skipped.to_string()]));}if r.data_failed{text.push_str(t(" Część danych nie została zapisana. Ponów import."));}}
            text
        }
        _=>String::new(),
    }
}
// Page-lifecycle guarded operations (beforeunload checks, sleeping capture guard).
fn operation_status_copy(event:ffi::OperationStatusEvent)->String{
    use ffi::OperationStatusEvent as E;
    match event{E::Checking=>t("Sprawdzanie niezapisanych zmian…"),E::Cancelled=>t("Anulowano. Karty i niezapisane zmiany pozostały otwarte."),E::Done=>t("Gotowe."),E::StopCapture=>t("Najpierw zatrzymaj nagrywanie lub udostępnianie w tej przestrzeni."),_=>""}.into()
}
// Search result provenance shown under each palette result.
fn palette_result_detail(kind:ffi::PaletteResultKind,label:&str)->String{
    use ffi::PaletteResultKind as K;
    match kind{K::Tab=>tf("Karta · {}",&[label]),K::Pin=>tf("Przypięta · {}",&[label]),K::Workspace=>t("Przełącz workspace").into(),K::History=>tf("Historia · {}",&[label]),_=>String::new()}
}
fn password_prompt_copy(update:bool,origin:&str,username:&str,profile:&str)->ffi::PasswordPromptCopy{
    ffi::PasswordPromptCopy{title:if update{t("Zaktualizować hasło?")}else{t("Zapisać hasło?")}.into(),lines:vec![origin.into(),tf("Użytkownik: {}",&[username]),tf("Profil: {}",&[profile]),t("Hasło będzie chronione w magazynie tego profilu.").into()],accept:if update{t("Zaktualizuj")}else{t("Zapisz hasło")}.into(),cancel:t("Nie teraz").into()}
}
// Browsing-data removal: confirmation copy and outcome wording/tone.
fn clear_periods()->[&'static str;4]{[t("Ostatnia godzina"),t("Ostatnie 24 godziny"),t("Ostatnie 7 dni"),t("Cały okres")]}
fn clear_data_status(event:ffi::ClearDataEvent,profile:&str)->ffi::PreferenceStatus{
    use ffi::ClearDataEvent as E;
    let (text,error)=match event {
        E::Busy=>(t("Poczekaj na zakończenie bieżącej operacji.").to_string(),true),
        E::NothingSelected=>(t("Wybierz co najmniej jeden rodzaj danych i zakres czasu.").into(),true),
        E::HistoryForbidden=>(t("Zasady tego profilu nie pozwalają usuwać historii.").into(),true),
        E::Clearing=>(tf("Czyszczenie danych profilu „{}”…",&[profile]),false),
        E::Cleared=>(tf("Wyczyszczono dane profilu „{}”.",&[profile]),false),
        E::Failed=>(t("Nie udało się usunąć wszystkich wybranych danych. Spróbuj ponownie.").into(),true),
        _=>(String::new(),false),
    };
    ffi::PreferenceStatus{text,error}
}
fn clear_data_confirm(profile:&str,period:i32,history:bool,cookies:bool,cache:bool)->ffi::ClearDataConfirm{
    let Some(range)=usize::try_from(period).ok().and_then(|p|clear_periods().get(p).copied()).filter(|_|history||cookies||cache) else{return ffi::ClearDataConfirm{valid:false,title:String::new(),description:String::new(),confirm:String::new(),cancel:String::new()};};
    let mut description=tf("Profil: {}\nZakres: {}\n\nZostaną usunięte:",&[profile,range]);
    if history{description.push_str(t("\n• Historia przeglądania"));}if cookies{description.push_str(t("\n• Cookies i dane witryn — może to wylogować Cię z serwisów"));}if cache{description.push_str(t("\n• Pamięć podręczna"));}
    description.push_str(t("\n\nTej operacji nie można cofnąć. Hasła, przypięte karty i pobrane pliki pozostaną zachowane."));
    ffi::ClearDataConfirm{valid:true,title:t("Wyczyścić dane przeglądania?").into(),description,confirm:t("Wyczyść dane").into(),cancel:t("Anuluj").into()}
}
// Profile service outcomes arrive as typed events; Rust owns their wording.
fn profile_status_copy(event:ffi::ProfileStatusEvent)->String{
    use ffi::ProfileStatusEvent as E;
    match event {
        E::DuplicateName=>t("Profil o tej nazwie już istnieje."),
        E::Creating=>t("Tworzenie profilu…"),
        E::CreateFailed=>t("Nie udało się utworzyć profilu. Spróbuj ponownie."),
        E::Created=>t("Profil utworzony. Możesz przypisać go do przestrzeni."),
        E::CreateNotSaved=>t("Nie udało się zapisać profilu. Profil nie został dodany. Spróbuj ponownie."),
        E::Renamed=>t("Nazwa profilu zapisana."),
        E::RenameFailed=>t("Nie udało się zapisać nazwy profilu."),
        E::Deleting=>t("Usuwanie profilu…"),
        E::Deleted=>t("Profil usunięty. Dane przeglądania zostaną wyczyszczone."),
        E::DeleteNotSaved=>t("Nie udało się zapisać zmian. Profil nie został usunięty."),
        E::LoadFailed=>t("Nie udało się wczytać profili. Uruchom aplikację ponownie."),
        _=>"",
    }.into()
}
fn preferences_profile_banner(profile:&str)->String{tf("Profil: {} · Ustawienia wspólne dla aplikacji są oznaczone w opisach.",&[profile])}
fn site_rule_detail(primary:&str,name:&str,setting:i32,secondary:&str,editable:bool)->String{
    let policy=match setting{1=>t("Zezwalaj"),2=>t("Blokuj"),3=>t("Pytaj"),_=>t("Inna polityka")};let mut detail=format!("{primary} · {name} · {policy}");
    if secondary!="*"{detail.push_str(t(" · Osadzenie: "));detail.push_str(secondary);}if !editable{detail.push_str(t(" · Zarządzane"));}detail
}
fn site_rules_empty(saved:bool)->String{if saved{t("Brak wyjątków pasujących do wyszukiwania.")}else{t("Nie masz jeszcze zapisanych wyjątków.")}.into()}
const PREFERENCE_IDLE:&str="Przełączniki zapisują się automatycznie. Pola tekstowe zatwierdź przyciskiem Zapisz lub Enter.";
// Outcome copy is keyed by the semantic write and its backend result; native
// code only reports what it attempted and projects the returned text/tone.
fn preference_outcome_text(outcome:ffi::PreferenceOutcome,key:&str,ok:bool)->&'static str {
    use ffi::PreferenceOutcome as O;
    match (outcome,ok) {
        (O::Text,true)=>t("Zapisano. Witryna może wymagać odświeżenia, aby zastosować zmianę."),
        (O::Text,false)=>match key {"accept_languages"=>t("Podaj poprawne kody języków oddzielone przecinkami, np. pl, en-US, en."),"dns_templates"=>t("Podaj poprawny szablon DNS-over-HTTPS. W trybie ścisłym adres nie może być pusty."),_=>t("Nie udało się zapisać. Podaj nazwę rodziny czcionki (do 128 znaków). Ustawienie może być zarządzane.")},
        (O::Site,true)=>t("Zapisano wyjątek w tym profilu. Odśwież witrynę, aby zastosować zmianę."),
        (O::Site,false)=>t("Nie zapisano wyjątku. Podaj adres HTTP(S) bez ścieżki i danych logowania. Sprawdź wybraną politykę; reguły zarządzane nie mogą być zmieniane."),
        (O::SiteRemove,true)=>t("Usunięto wyjątek. Witryna wróci do ustawień domyślnych."),
        (O::SiteRemove,false)=>t("Nie udało się usunąć wyjątku. Reguła może być zarządzana."),
        (O::Value,true)=>if key=="hardware_acceleration"{t("Zapisano. Uruchom ponownie Zitter, aby zmienić akcelerację sprzętową.")}else{t("Zapisano. Jeśli strona nie zastosowała zmiany, odśwież ją.")},
        (O::Value,false)=>if key=="dns_mode"{t("Nie zapisano trybu DNS. Tryb ścisły wymaga poprawnego adresu DNS-over-HTTPS; ustawienie może też być zarządzane.")}else{t("Nie udało się zmienić ustawienia. Profil może być zarządzany lub niedostępny.")},
        (O::General,true)=>t(PREFERENCE_IDLE),
        (O::General,false)=>t("Nie udało się zapisać ustawień. Spróbuj ponownie."),
        _=>"",
    }
}
impl PreferencesForm {
    fn bind_category(&self,input:&mut ActionControl,category:usize)->bool{category<self.categories&&input.init_form_command(ffi::FormCommand::PreferenceCategory,"",category)}
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
    fn preference_outcome(&mut self,outcome:ffi::PreferenceOutcome,key:&str,ok:bool)->ffi::PreferenceStatus{
        let text=preference_outcome_text(outcome,key,ok);if !text.is_empty(){self.status=(text,!ok);}self.preference_status()
    }
    fn preference_status(&self)->ffi::PreferenceStatus{ffi::PreferenceStatus{text:self.status.0.into(),error:self.status.1}}
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
struct ToggleState { slide:ToggleTransition, hover:ToggleTransition }
fn initial_toggle_state()->ToggleState{ToggleState{slide:ToggleTransition{from:0.0,target:false,started:None,duration:80},hover:ToggleTransition{from:0.0,target:false,started:None,duration:250}}}
#[cfg(test)]
fn new_toggle_state()->Box<ToggleState>{Box::new(initial_toggle_state())}
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
 let make=|x,y,width,height,radius,color,stroke|ffi::Primitive{x,y,width,height,radius,color,stroke,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,right_align:false,clip:ffi::FloatRect{x:s.clip.x,y:s.clip.y,width:s.clip.width,height:s.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0};
 // Design system Toggle (recipe::toggle): a 34×20 pill, 12 px thumb 4 px in. Off:
 // 1.5 px `line-strong` edge and an `ink-muted` thumb; on: `accent` with `on-accent`.
 let _=hover;
 let w=s.width.min(34.0);let h=s.height.min(20.0);let x=(s.width-w)/2.0;let y=(s.height-h)/2.0;
 let inset=4.0f32.min(h/2.0);let diameter=(h-2.0*inset).max(0.0);let pos=if s.rtl{1.0-p}else{p};let tx=x+inset+pos*(w-h).max(0.0);let ty=y+inset;let out=if s.pressed{1.0}else{0.0};
 let mut ops=vec![make(x,y,w,h,h/2.0,blend(s.track_on,s.track_off,p),0.0)];
 if !s.checked{ops.push(make(x+0.75,y+0.75,(w-1.5).max(0.0),(h-1.5).max(0.0),(h-1.5).max(0.0)/2.0,s.border,1.5));}
 let thumb=blend(s.thumb_on,s.thumb_off,p);
 ops.push(make(tx-out,ty-out,diameter+out*2.0,diameter+out*2.0,(diameter+out*2.0)/2.0,thumb,0.0));
 // Focus: 2 px `focus` ring, 2 px away.
 if s.focused{ops.push(make(x-3.0,y-3.0,w+6.0,h+6.0,h/2.0+3.0,s.focus,2.0));}
 ops
}

struct CheckboxState { checked: bool }
#[cfg(test)]
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
    ffi::ImportCopy { title:t("Przenieś się do Zittera").into(), dismiss:t("Zamknij import").into(), intro:t("Wybierz przeglądarkę i dane, które chcesz przenieść.").into(), source_label:t("Przeglądarka źródłowa").into(), source_ax:t("Przeglądarka do importu").into(), heading:t("Co chcesz przenieść?").into(), profiles:t("Oddzielne profile przeglądania").into(), history:t("Historia przeglądania").into(), cookies:t("Ciasteczka i sesje witryn").into(), apply_label:t("Importuj").into(), browsers:vec!["Arc".into(),"Google Chrome".into(),"Microsoft Edge".into(),"Brave".into()] }
}
fn new_import_form() -> Box<ImportForm> { Box::new(ImportForm { selection:import_defaults(), preview:import_defaults(), epoch:0, scheduled:false, waiting:false, can_apply:false, busy:false, applying:false, submitted:false, message:String::new() }) }
impl ImportForm {
    fn dispatch(&self, action: ffi::ImportAction) -> ffi::ImportDispatch {ffi::ImportDispatch {action, epoch:self.epoch, selection:self.selection.clone()}}
    fn next_epoch(&mut self) {self.epoch=self.epoch.wrapping_add(1);if self.epoch==0 {self.epoch=1;}}
    fn select(&mut self, selection: ffi::ImportSelection) -> ffi::ImportDispatch {
        if self.applying||self.submitted {return self.dispatch(ffi::ImportAction::None);}
        self.next_epoch();self.can_apply=false;self.waiting=false;
        if selection.browser>3 {self.scheduled=false;self.busy=false;self.message=t("Wybierz przeglądarkę źródłową.").into();return self.dispatch(ffi::ImportAction::None);}
        self.selection=selection;self.scheduled=true;self.busy=true;self.message=t("Odczytywanie danych…").into();self.dispatch(ffi::ImportAction::Preview)
    }
    fn cancel_scan(&mut self) {self.next_epoch();self.scheduled=false;self.waiting=false;}
    fn take_scan(&mut self, epoch: u64, visible: bool, current: ffi::ImportSelection, ready: bool, profiles_busy: bool) -> ffi::ImportDispatch {
        if !self.scheduled||epoch!=self.epoch||self.applying||self.submitted {return self.dispatch(ffi::ImportAction::None);}
        self.scheduled=false;if !visible{return self.dispatch(ffi::ImportAction::None);}
        if current!=self.selection {self.can_apply=false;return self.dispatch(ffi::ImportAction::Rescan);}
        if !ready||profiles_busy {self.waiting=true;self.busy=false;self.message=t("Poczekaj na zakończenie zapisywania.").into();return self.dispatch(ffi::ImportAction::None);}
        self.busy=true;self.dispatch(ffi::ImportAction::Preview)
    }
    fn should_resume(&self, ready: bool, profiles_busy: bool) -> bool {self.waiting&&ready&&!profiles_busy&&!self.applying&&!self.submitted}
    fn begin_preview(&mut self, selection: ffi::ImportSelection) {self.next_epoch();self.scheduled=false;self.waiting=false;self.preview=selection;self.can_apply=false;self.busy=true;}
    fn set_status(&mut self, message: &str, can_apply: bool, busy: bool, applying: bool) {self.scheduled=false;self.waiting=false;self.submitted=false;self.message=message.into();self.can_apply=can_apply;self.busy=busy;self.applying=applying;}
    fn view(&self, ready: bool, profiles_busy: bool) -> ffi::ImportView {
        let locked=self.applying||self.submitted;ffi::ImportView {message:self.message.clone(),bookmarks_label:if self.selection.browser==0 {t("Przypięte karty, foldery i Ulubione")} else {t("Zakładki i foldery")}.into(),options_enabled:!locked,close_enabled:!locked,apply_enabled:self.can_apply&&!self.busy&&!locked&&ready&&!profiles_busy&&self.preview==self.selection,applying:locked,busy:self.busy||self.submitted,can_apply:self.can_apply}
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
        if accepted {self.applying=true;self.busy=true;self.can_apply=false;self.message=t("Importowanie wybranych danych…").into();}
        else {self.message=t("Nie udało się rozpocząć importu. Spróbuj ponownie.").into();}
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

// design::WorkspaceColor::PRESETS as stored hex (checked by a test below).
const APPEARANCE_COLORS: [u32;8] = [0xff928fd4,0xffce826b,0xff53a2cd,0xffb9914c,0xffc97e9b,0xff4fac8f,0xff79a76a,0xff989898];
fn design_palette(dark:bool,workspace_color:u32)->ffi::DesignPalette{
    let p=design::palette_for_stored(if dark {design::Scheme::Noc} else {design::Scheme::Papier},workspace_color);
    ffi::DesignPalette{bg_void:p.bg_void,bg_base:p.bg_base,bg_raised:p.bg_raised,bg_hover:p.bg_hover,line:p.line,line_strong:p.line_strong,ink:p.ink,ink_muted:p.ink_muted,ink_faint:p.ink_faint,accent:p.accent,on_accent:p.on_accent,accent_soft:p.accent_soft,accent_ink:p.accent_ink,zing:p.zing,on_zing:p.on_zing,zing_ink:p.zing_ink,focus:p.focus,ok:p.ok,warn:p.warn,danger:p.danger,scrim:p.scrim}
}
// Two stored colors are the same workspace color within what a hex can tell apart.
fn design_same_workspace_color(a:u32,b:u32)->bool{design::WorkspaceColor::from_argb(a).same(design::WorkspaceColor::from_argb(b))}
const APPEARANCE_EMOJIS: [&str;8] = ["💼","🏠","🚀","🎨","📚","🎮","🛒","🎬"];
fn appearance_presets(emoji: bool) -> Vec<ffi::AppearancePreset> {
    if emoji { APPEARANCE_EMOJIS.iter().map(|e|ffi::AppearancePreset { color:0, emoji:(*e).into(), label:tf("Emoji {}",&[e]) }).collect() }
    else { APPEARANCE_COLORS.iter().zip(workspace_color_names()).map(|(&c,name)|ffi::AppearancePreset { color:c, emoji:String::new(), label:name.into() }).collect() }
}
fn appearance_command_target(command:ffi::FormCommand,index:usize)->ffi::AppearanceTarget{
    use ffi::FormCommand as C;
    match command {
        C::WorkspaceColorPreset=>if let Some(&color)=APPEARANCE_COLORS.get(index){return ffi::AppearanceTarget{valid:true,color,emoji:String::new()};},
        C::WorkspaceEmojiPreset=>if let Some(&emoji)=APPEARANCE_EMOJIS.get(index){return ffi::AppearanceTarget{valid:true,color:0,emoji:emoji.into()};},
        _=>{},
    }
    ffi::AppearanceTarget{valid:false,color:0,emoji:String::new()}
}

fn appearance_selected(emoji: bool, index: usize, color: u32, current_emoji: &str) -> bool {
    if emoji { APPEARANCE_EMOJIS.get(index).map(|e|*e==current_emoji).unwrap_or(false) }
    else { APPEARANCE_COLORS.get(index).map(|&c|design_same_workspace_color(c,color)).unwrap_or(false) }
}
fn color_ring_target(current:u32,x:f32,y:f32,size:f32)->u32{
    design::wheel::ring_hit(x,y,size).map_or(0,|h|design::wheel::target(design::WorkspaceColor::from_argb(current),Some(h),None,None).to_argb())
}
fn color_pad_target(current:u32,x:f32,y:f32,width:f32,height:f32)->u32{
    let (s,t)=design::wheel::pad_hit(x,y,width,height);design::wheel::target(design::WorkspaceColor::from_argb(current),None,Some(s),Some(t)).to_argb()
}
fn color_wheel_step(current:u32,axis:u32,delta:i32)->u32{design::wheel::step(design::WorkspaceColor::from_argb(current),axis,delta).to_argb()}
fn color_wheel_description(current:u32,ring:bool)->String{design::wheel::description(design::WorkspaceColor::from_argb(current),ring)}
fn wheel_primitive(i:&ffi::Item,x:f32,y:f32,w:f32,h:f32,radius:f32,color:u32,stroke:f32)->ffi::Primitive{
    ffi::Primitive{x,y,width:w,height:h,radius,color,stroke,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,right_align:false,clip:ffi::FloatRect{x:i.clip.x,y:i.clip.y,width:i.clip.width,height:i.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0}
}
// Kinds 28 and 29: the color wheel's ring and field, drawn from solid primitives
// (the scene has no gradients): 120 overlapping dots make the ring, a 24 x 24
// cell grid under a surface-colored rounded mask makes the field.
fn color_wheel_scene(i:&ffi::Item,out:&mut Vec<ffi::Primitive>){
    use design::wheel;let c=design::WorkspaceColor::from_argb(i.badge);let hue=c.hue_degrees();
    let disc=|out:&mut Vec<ffi::Primitive>,x:f32,y:f32,d:f32,color:u32,stroke:f32|out.push(wheel_primitive(i,x-d/2.0,y-d/2.0,d,d,d/2.0,color,stroke));
    if i.kind==28{
        let size=i.width.min(i.height);let (ox,oy)=(i.x+(i.width-size)/2.0,i.y+(i.height-size)/2.0);let (cx,cy)=(ox+size/2.0,oy+size/2.0);
        for k in 0..120{let deg=k as f32*3.0;let (x,y)=wheel::ring_knob(size,deg);disc(out,ox+x,oy+y,wheel::RING,wheel::ring_color(deg),0.0);}
        let p=design::Palette::for_workspace(if i.tile{design::Scheme::Noc}else{design::Scheme::Papier},c);
        let inner=size-2.0*wheel::INSET-2.0*wheel::RING-20.0;disc(out,cx,cy,inner+12.0,p.accent_soft,0.0);disc(out,cx,cy,inner,p.accent,0.0);
        let (kx,ky)=wheel::ring_knob(size,hue);let (kx,ky)=(ox+kx,oy+ky);
        if i.focused&&i.focus>>24!=0{disc(out,kx,ky,36.0,i.focus,2.0);}
        disc(out,kx,ky,28.0,i.fill,0.0);disc(out,kx,ky,22.0,wheel::ring_color(hue),0.0);disc(out,kx,ky,28.0,i.ink,1.0);
        if !i.title.is_empty(){let mut t=wheel_primitive(i,cx-inner/2.0,cy-inner/2.0,inner,inner,0.0,p.on_accent,0.0);t.text=i.title.clone();t.font_size=i.font_size;t.center=true;out.push(t);}
        return;
    }
    let (fx,fy,fw,fh)=(i.x+wheel::INSET,i.y+wheel::INSET,i.width-2.0*wheel::INSET,i.height-2.0*wheel::INSET);let n=24usize;let (cw,ch)=(fw/n as f32,fh/n as f32);
    for row in 0..n{let tone=1.0-(row as f32+0.5)/n as f32;for col in 0..n{let sat=(col as f32+0.5)/n as f32;out.push(wheel_primitive(i,fx+col as f32*cw,fy+row as f32*ch,cw+0.5,ch+0.5,0.0,wheel::pad_color(hue,sat,tone),0.0));}}
    // Round the field: a wide stroke in the surface color around its rounded edge.
    let r=wheel::PAD_RADIUS;out.push(wheel_primitive(i,fx-6.0,fy-6.0,fw+12.0,fh+12.0,r+6.0,i.fill,12.0));
    out.push(wheel_primitive(i,fx+0.5,fy+0.5,fw-1.0,fh-1.0,r-0.5,i.edge,1.0));
    let (kx,ky)=wheel::pad_knob(i.width,i.height,c.saturation(),c.tone());let (kx,ky)=(i.x+kx,i.y+ky);
    if i.focused&&i.focus>>24!=0{disc(out,kx,ky,32.0,i.focus,2.0);}
    disc(out,kx,ky,wheel::PAD_KNOB,i.fill,0.0);disc(out,kx,ky,wheel::PAD_KNOB-6.0,wheel::pad_color(hue,c.saturation(),c.tone()),0.0);disc(out,kx,ky,wheel::PAD_KNOB,i.ink,1.0);
}
fn appearance_row_layout(width: i32, height: i32, editor_width: i32, editor_height: i32, emoji: bool) -> ffi::ColumnLayout {
    // The emoji row ends with the "more emoji" tile; its field only takes its own width.
    let count=if emoji {APPEARANCE_EMOJIS.len() as i32+1} else {APPEARANCE_COLORS.len() as i32};let button_width=if emoji {38} else {32};let button_height=if emoji {38} else {32};
    // Tiles sit 6 px apart when the row has room, and touch when it does not.
    let gap=if emoji&&(width<0||width>=count*button_width+(count-1)*6) {6} else {0};let natural=count*button_width+(count-1)*gap;
    let editor_height=editor_height.max(0);let width=if width<0 {(natural+8i32).saturating_add(editor_width.max(0))} else {width};
    let height=if height<0 {button_height.max(editor_height)} else {height};let control_width=width.min(natural)-(count-1)*gap;
    let mut children=Vec::with_capacity(count as usize+2);let mut x=0;
    for index in 0..count { let width=control_width/count+if index<control_width%count {1} else {0};let h=button_height.min(height);children.push(ffi::Rect {x,y:(height-h)/2,width,height:h});x+=width+if index+1<count {gap} else {0}; }
    let gap=(width-x).min(8);children.push(ffi::Rect{x,y:0,width:gap,height});x+=gap;
    let h=editor_height.min(height);let field=if emoji {(width-x).min(editor_width.max(0))} else {width-x};children.push(ffi::Rect{x,y:(height-h)/2,width:field,height:h});
    ffi::ColumnLayout {width,height,children}
}
// Color presets as pills: four columns of 30 px pills, 6 px apart, then an empty
// spacer and field slot so the row keeps the appearance rows' child contract.
fn appearance_pill_layout(width:i32,height:i32,count:usize)->ffi::ColumnLayout{
    let (columns,gap,pill)=(4i32,6i32,30i32);let count=count.min(64) as i32;let rows=(count+columns-1)/columns;
    let natural_height=rows*pill+(rows-1).max(0)*gap;let width=if width<0{columns*92+(columns-1)*gap}else{width};let height=if height<0{natural_height}else{height};
    let cell=((width-(columns-1)*gap)/columns).max(0);let mut children=Vec::with_capacity(count as usize+2);
    for i in 0..count{let (c,r)=(i%columns,i/columns);children.push(ffi::Rect{x:c*(cell+gap),y:r*(pill+gap),width:cell,height:pill.min(height)});}
    children.push(ffi::Rect{x:0,y:height,width:0,height:0});children.push(ffi::Rect{x:0,y:height,width:0,height:0});
    ffi::ColumnLayout{width,height:natural_height.max(0).max(if height<0{0}else{height}),children}
}

pub struct FolderNotice { workspace:String, folder:String, title:String, error_only:bool, error:String, submitting:bool, closed:bool }
fn new_folder_notice(workspace:&str,folder:&str,title:&str,error_only:bool)->Box<FolderNotice>{Box::new(FolderNotice{workspace:workspace.into(),folder:folder.into(),title:title.into(),error_only,error:String::new(),submitting:false,closed:false})}
impl FolderNotice {
    fn copy(&self)->ffi::NoticeCopy {
        if self.error_only{return ffi::NoticeCopy{title:t("Nie udało się zapisać folderu").into(),body:t("Sprawdź wolne miejsce na dysku i spróbuj ponownie.").into(),accept:"OK".into(),cancel:String::new(),default_cancel:false};}
        ffi::NoticeCopy{title:tf("Usunąć folder „{}”?",&[&self.title]),body:t("Folder i cała jego przypięta zawartość zostaną usunięte. Tej operacji nie można cofnąć. Strony już otwarte w kartach pozostaną otwarte.").into(),accept:t("Usuń folder").into(),cancel:t("Anuluj").into(),default_cancel:true}
    }
    fn error(&self)->&str{&self.error}
    fn submit(&mut self,current:&str,ready:bool,busy:bool,nodes:&[ffi::TreeNode])->ffi::FolderNoticeRequest {
        let none=||ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::None,workspace:String::new(),folder:String::new()};
        if self.closed||self.submitting{return none();}
        if self.error_only{self.submitting=true;return ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::Acknowledge,workspace:String::new(),folder:String::new()};}
        if self.workspace.is_empty()||self.workspace!=current{self.error=t("Przestrzeń zmieniła się. Otwórz potwierdzenie ponownie.").into();return none();}
        if !ready||busy{self.error=t("Poczekaj na zakończenie zapisywania.").into();return none();}
        if self.folder.is_empty()||!nodes.iter().any(|n|n.id==self.folder&&n.folder){self.error=t("Folder nie jest już dostępny.").into();return none();}
        self.error.clear();self.submitting=true;ffi::FolderNoticeRequest{action:ffi::FolderNoticeAction::Delete,workspace:self.workspace.clone(),folder:self.folder.clone()}
    }
    fn finish_attempt(&mut self,accepted:bool){if !self.submitting{return;}self.submitting=false;self.closed=accepted;if !accepted{self.error=t("Nie udało się usunąć folderu. Spróbuj ponownie.").into();}}
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
    fn copy(&self)->ffi::FolderCopy{ffi::FolderCopy{title:if !self.folder.is_empty(){t("Zmień nazwę folderu")}else if self.parent.is_empty(){t("Nowy folder")}else{t("Nowy podfolder")}.into(),label:t("Nazwa folderu").into(),accept:if self.folder.is_empty(){t("Dodaj folder")}else{t("Zapisz nazwę")}.into(),cancel:t("Anuluj").into()}}
    fn edit(&mut self,name:&str){if !self.closed&&!self.submitting&&self.name!=name{self.name=name.into();self.error.clear();}}
    fn error(&self)->&str{&self.error}
    fn submit(&mut self,current:&str,ready:bool,busy:bool,nodes:&[ffi::TreeNode],composing:bool)->ffi::FolderRequest {
        let none=||ffi::FolderRequest{action:ffi::FolderAction::None,workspace:String::new(),parent:String::new(),folder:String::new(),name:String::new(),expand_parent:false};
        if self.closed||self.submitting||composing{return none();}
        if self.workspace.is_empty()||self.workspace!=current{self.error=t("Przestrzeń zmieniła się. Otwórz formularz ponownie.").into();return none();}
        if !ready||busy{self.error=t("Poczekaj na zakończenie zapisywania.").into();return none();}
        let target=if self.folder.is_empty(){&self.parent}else{&self.folder};
        if (!self.folder.is_empty()&&!self.parent.is_empty())||(!target.is_empty()&&!nodes.iter().any(|n|n.id==*target&&n.folder)){self.error=t("Folder nie jest już dostępny.").into();return none();}
        // Unicode White_Space matches Chromium's kWhitespaceUTF16 set. The
        // 256-byte UTF-8 limit is the existing application persistence contract.
        let name=self.name.trim();
        if name.is_empty(){self.error=t("Wpisz nazwę folderu.").into();return none();}
        if name.len()>256{self.error=t("Nazwa folderu jest za długa.").into();return none();}
        self.error.clear();self.submitting=true;
        ffi::FolderRequest{action:if self.folder.is_empty(){ffi::FolderAction::Create}else{ffi::FolderAction::Rename},workspace:self.workspace.clone(),parent:self.parent.clone(),folder:self.folder.clone(),name:name.into(),expand_parent:!self.parent.is_empty()}
    }
    // This acknowledges application admission, not completion of async storage.
    fn finish_attempt(&mut self,accepted:bool){if !self.submitting{return;}self.submitting=false;self.closed=accepted;if !accepted{self.error=t("Nie udało się zapisać folderu. Spróbuj ponownie.").into();}}
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
    // Anywhere on the page: the drop slot picks the pane edge (left, right, between).
    x>=left && x<left+bounds.width as i64 && y>=top && y<top+bounds.height as i64
}
// Pane position of the menu's tab inside the Split View group.
fn sidebar_reorder_target(rows:&[ffi::ReorderRow],y:i32)->ffi::ReorderTarget {
    let target=|row:usize,after:bool,into:bool|ffi::ReorderTarget{valid:true,row,after,into,pinned:rows[row].pinned};
    let Some(first)=rows.first() else{return ffi::ReorderTarget{valid:false,row:0,after:false,into:false,pinned:false};};
    let top=|r:&ffi::ReorderRow|i64::from(r.bounds.y);let bottom=|r:&ffi::ReorderRow|i64::from(r.bounds.y)+i64::from(r.bounds.height.max(0));
    let y=i64::from(y);
    if y<top(first) {
        // Above the first tab with no pins yet: start the pinned section.
        return if first.pinned{target(0,false,false)}else{ffi::ReorderTarget{valid:true,row:usize::MAX,after:true,into:false,pinned:true}};
    }
    for (index,row) in rows.iter().enumerate() {
        let (start,end)=(top(row),bottom(row));
        if y>=start&&y<end {
            let height=end-start;
            if row.folder&&y>=start+height/4&&y<end-height/4{return target(index,false,true);}
            return target(index,y>=start+height/2,false);
        }
        // In the gap before the next row, the nearer edge wins.
        if let Some(next)=rows.get(index+1){if y>=end&&y<top(next){return if y-end<top(next)-y{target(index,true,false)}else{target(index+1,false,false)};}}
    }
    target(rows.len()-1,true,false)
}
fn tab_menu_position(s:&ffi::TabMenuState)->Option<usize>{if !s.valid||!s.has_tab||!s.split||s.members.len()<2{return None;}s.members.iter().position(|&m|m==s.tab)}
fn tab_menu_enabled(command:u32,s:&ffi::TabMenuState)->bool {
    if !s.valid||!s.ready||s.busy{return false;}
    let position=tab_menu_position(s);let paired=position.is_some();
    match command {5=>!paired,6|9|11|12=>paired,7=>position.is_some_and(|p|p>0),8=>position.is_some_and(|p|p+1<s.members.len()),10=>s.has_tab&&s.tab_count>1,_=>false}
}
fn tab_menu_action(command:u32,s:&ffi::TabMenuState)->ffi::SidebarAction {
    use ffi::SidebarAction as A;if !tab_menu_enabled(command,s){return A::None;}
    match command{5=>A::OpenSplit,6=>A::CloseSplit,7=>A::MoveEarlier,8=>A::MoveLater,9=>A::ToggleOrientation,10=>A::CloseTab,11=>A::Separate,12=>A::CloseGroup,_=>A::None}
}
fn tab_context_menu(s:&ffi::TabMenuState)->Vec<ffi::TabMenuEntry> {
    // Design (ContextMenu): verbs in groups, the destructive actions last after
    // a separator.
    let mut entries=Vec::new();let mut add=|command,label:&str|entries.push(ffi::TabMenuEntry{command,label:label.into()});
    let split=tab_menu_position(s).is_some();
    add(36,t("Nowy folder…"));add(20,t("Otwórz w Peek"));
    if split{
        add(0,"");add(7,if s.vertical{t("Przesuń w górę")}else{t("Przesuń w lewo")});add(8,if s.vertical{t("Przesuń w dół")}else{t("Przesuń w prawo")});
        add(9,if s.vertical{t("Podział poziomy (obok siebie)")}else{t("Podział pionowy (jedna nad drugą)")});add(11,t("Rozdziel"));
        // With two panes, taking one out is the same as separating them.
        if s.members.len()>2{add(6,t("Wyjmij kartę ze splitu"));}
    } else {add(5,t("Otwórz obok w Split View"));}
    if s.http{add(0,"");add(23,t("Zawsze otwieraj tę domenę tutaj"));add(24,t("Usuń regułę dla tej domeny"));}
    if split{add(0,"");add(10,t("Zamknij kartę"));add(12,t("Zamknij wszystkie karty w splicie"));}
    entries
}

// Link/route menu commands share IDs with the native menu delegate contract.
const LINK_PEEK:i32=20;const LINK_NEW_TAB:i32=21;const LINK_SPLIT:i32=22;const LINK_RULE_HERE:i32=23;const LINK_REMOVE_RULE:i32=24;const LINK_ROUTES:i32=25;const LINK_ROUTE_BASE:i32=1000;
fn link_route(command:i32,s:&ffi::LinkMenuState)->Option<&ffi::LinkRoute>{if command<LINK_ROUTE_BASE{return None;}s.routes.get((command-LINK_ROUTE_BASE) as usize)}
fn link_context_menu(s:&ffi::LinkMenuState)->Vec<ffi::LinkMenuEntry>{
    let mut entries=Vec::new();let mut add=|command,label:String,kind|entries.push(ffi::LinkMenuEntry{command,label,kind});
    add(LINK_PEEK,t("Otwórz w Peek").into(),0);add(LINK_NEW_TAB,t("Otwórz w nowej karcie").into(),0);add(LINK_SPLIT,t("Otwórz obok w Split View").into(),0);add(0,String::new(),1);
    add(LINK_RULE_HERE,t("Zawsze otwieraj tę domenę tutaj").into(),0);add(LINK_ROUTES,tf("Otwieraj {} w…",&[&s.host]),2);
    for (index,route) in s.routes.iter().enumerate(){add(LINK_ROUTE_BASE+index as i32,route.label.clone(),3);}
    add(LINK_REMOVE_RULE,t("Usuń regułę dla tej domeny").into(),0);entries
}
fn link_menu_enabled(command:i32,s:&ffi::LinkMenuState)->bool{
    if !s.ready{return false;}
    match command{LINK_REMOVE_RULE=>s.has_rule,LINK_PEEK|LINK_NEW_TAB|LINK_SPLIT|LINK_RULE_HERE|LINK_ROUTES=>true,_=>link_route(command,s).is_some()}
}
fn link_menu_checked(command:i32,s:&ffi::LinkMenuState)->bool{s.has_rule&&link_route(command,s).is_some_and(|route|route.id==s.rule_workspace)}
fn link_menu_action(command:i32,s:&ffi::LinkMenuState)->ffi::LinkMenuAction{
    use ffi::LinkMenuCommand as C;let action=|command,workspace:&str|ffi::LinkMenuAction{command,workspace:workspace.into()};
    if !link_menu_enabled(command,s){return action(C::None,"");}
    match command{LINK_PEEK=>action(C::Peek,&s.source),LINK_NEW_TAB=>action(C::NewTab,&s.source),LINK_SPLIT=>action(C::Split,&s.source),LINK_RULE_HERE=>action(C::SetRule,&s.source),LINK_REMOVE_RULE=>action(C::RemoveRule,""),_=>match link_route(command,s){Some(route)=>action(C::SetRule,&route.id),None=>action(C::None,"")}}
}

// Address/command palette ordering. Native services resolve URLs/providers and
// supply results/commands; Rust decides the primary entry and the final order.
fn palette_plan(p:&ffi::PalettePlanInput)->ffi::PalettePlan{
    use ffi::PaletteSource as S;let text=p.text.as_str();
    let commands_only=p.command_palette&&text.starts_with('>');
    let keyword_search=text.find([' ','\t']).is_some_and(|end|{let keyword=text[..end].to_ascii_lowercase();p.keywords.iter().any(|k|*k==keyword)});
    let mut slots=Vec::new();let primary=!text.is_empty()&&!commands_only;
    if primary{slots.push(ffi::PaletteSlot{source:S::Primary,index:0});}
    if !commands_only{slots.extend((0..p.results).map(|index|ffi::PaletteSlot{source:S::Result,index}));}
    if p.command_palette&&!commands_only&&!keyword_search&&!text.is_empty()&&p.results>0{slots.rotate_left(1);}
    if p.command_palette{let position=if keyword_search&&!slots.is_empty(){1}else{0};let commands=(0..p.commands).map(|index|ffi::PaletteSlot{source:S::Command,index}).collect::<Vec<_>>();slots.splice(position..position,commands);}
    let search=!p.provider.is_empty();
    ffi::PalettePlan{commands_only,keyword_search,primary_title:if !primary{String::new()}else if search{tf("Szukaj w {}",&[&p.provider])}else{t("Otwórz").into()},primary_detail:if !primary{String::new()}else if search{p.text.clone()}else{p.resolved.clone()},primary_icon:if primary&&!search{1}else{0},slots}
}

fn download_row(f:&ffi::DownloadFacts)->ffi::DownloadRow{
    use ffi::DownloadPhase as P;
    let row=|tone:u8,state:String,meta:String|ffi::DownloadRow{tone,state,meta};
    let sizes=||if f.total.is_empty(){f.received.clone()}else{format!("{} / {}",f.received,f.total)};
    if f.blocked{return row(3,format!("⚠ {}",t("Zablokowane")),t("Ochrona Chromium — plik nie jest gotowy do użycia").into());}
    match f.phase {
        P::Cancelled=>row(3,format!("⚠ {}",t("Anulowano")),String::new()),
        P::Complete=>if f.removed{row(2,format!("⚠ {}",t("Plik przeniesiony lub usunięty")),String::new())}else{
            row(1,format!("✓ {}",t("Gotowe")),if f.host.is_empty(){f.received.clone()}else{format!("{} · {}",f.received,f.host)})},
        P::Interrupted=>row(3,format!("⚠ {}",if f.can_resume{t("Przerwano — możesz wznowić")}else{t("Przerwano — pobierz ponownie ze strony")}),sizes()),
        _=>if f.paused{row(2,format!("‖ {}",t("Wstrzymano")),sizes())}else{
            let mut meta=sizes();
            if !f.speed.is_empty(){meta.push_str(&format!(" · {}/s",f.speed));}
            if f.remaining_seconds>=0{meta.push_str(&format!(" · {} s",f.remaining_seconds.max(1)));}
            row(0,String::new(),meta)
        }
    }
}

// Command Center catalogue. Native code supplies page/workspace facts and binds
// each entry to the current tab/page; Rust owns entries, wording and matching.
fn command_fold(text:&str)->String{text.to_lowercase().chars().map(|c|match c{'ą'=>'a','ć'=>'c','ę'=>'e','ł'=>'l','ń'=>'n','ó'=>'o','ś'=>'s','ź'|'ż'=>'z',_=>c}).collect()}
fn command_catalogue(c:&ffi::CommandContext,query:&str)->Vec<ffi::CommandEntry>{
    let only=query.starts_with('>');let query=if only{&query[1..]}else{query};let query=command_fold(query.trim());
    let words=query.split([' ','\t']).filter(|w|!w.is_empty()).collect::<Vec<_>>();
    let mut out=Vec::new();
    let mut add=|id:&str,title:String,detail:&str,aliases:&str,target:&str|{let haystack=command_fold(&format!("{title} {detail} {aliases}"));if words.iter().all(|w|haystack.contains(w)){out.push(ffi::CommandEntry{id:id.into(),title,detail:tf("Polecenie · {}",&[detail]),target:target.into()});}};
    for (id,title,detail,aliases) in [("new_tab",t("Nowa karta"),t("Otwórz pasek nowej karty"),"new tab"),("save_snapshot",t("Zapisz snapshot przestrzeni"),t("Szybki punkt przywracania w Time Machine"),"zapis sesji save snapshot backup"),("timeline","Time Machine",t("Snapshoty i zamknięte karty"),"przywróć restore historia sesji"),("settings",t("Ustawienia przeglądarki"),t("Wygląd, prywatność i ustawienia profilu"),"settings preferences"),("shortcuts",t("Konfiguruj skróty klawiszowe"),t("Nagraj lub zmień kombinacje klawiszy"),"keyboard shortcuts"),("engines",t("Konfiguruj wyszukiwarki"),t("Prefiksy i domyślna wyszukiwarka"),"search engines"),("sidebar",t("Pokaż / ukryj panel boczny"),t("Karty i przestrzenie"),"sidebar menu"),("spaces",t("Zarządzaj przestrzeniami"),t("Dodaj lub edytuj space i przypisanie profilu"),"workspace spaces"),("profiles",t("Zarządzaj profilami"),t("Dodaj profil i zarządzaj jego danymi"),"profile accounts"),("import",t("Importuj z przeglądarki"),t("Wybierz przeglądarkę i zakres importu"),"import arc chrome edge brave zakladki profile"),("history",t("Historia przeglądania"),t("Przeglądaj, szukaj i czyść historię profilu"),"history visited historia wyczysc"),("downloads",t("Pobrania"),t("Postęp, pauza, wznowienie i Finder"),"downloads pobierz pliki transfer")]{add(id,title.into(),detail,aliases,"");}
    if c.page{
        add("split",(if c.split{t("Zakończ Split View")}else{t("Otwórz Split View")}).into(),t("Dwie karty obok siebie"),"split podziel porównaj compare","");
        add("reload",t("Odśwież kartę").into(),t("Załaduj bieżącą stronę ponownie"),"reload refresh","");
        add("find",t("Znajdź na stronie").into(),t("Szukaj tekstu na bieżącej stronie"),"find search szukaj znajdz ctrl+f","");
        add("print",t("Drukuj").into(),t("Wydrukuj stronę albo zapisz ją jako PDF"),"print drukuj pdf","");
        if c.http_page{add("copy_url",t("Kopiuj adres strony").into(),t("Adres bieżącej karty do schowka"),"copy url link adres kopiuj","");}
        if c.can_back{add("back",t("Wstecz").into(),t("Poprzednia strona tej karty"),"back","");}
        if c.can_forward{add("forward",t("Dalej").into(),t("Następna strona tej karty"),"forward","");}
        add("passwords",t("Hasła tego profilu").into(),t("Zapisane logowania"),"passwords logins","");
        if c.tab_count>1{add("close_tab",t("Zamknij bieżącą kartę").into(),t("Możesz ją odzyskać w Time Machine"),"close tab","");add("close_other_tabs",t("Zamknij pozostałe karty").into(),t("Zostaje tylko bieżąca karta i przypięte"),"close other tabs zamknij inne","");}
        if c.split{add("left_pane",t("Przejdź do lewej karty Split View").into(),t("Ustaw aktywną stronę"),"focus left pane","");add("right_pane",t("Przejdź do prawej karty Split View").into(),t("Ustaw aktywną stronę"),"focus right pane","");}
    }
    if c.reopen{add("reopen",t("Przywróć zamkniętą kartę").into(),t("Ostatnia karta w tym profilu i space"),"reopen closed tab","");}
    let mut awake_background=false;
    for w in &c.workspaces{
        add("select_space",if w.asleep{tf("Obudź przestrzeń: {}",&[&w.name])}else{tf("Przełącz przestrzeń: {}",&[&w.name])},if w.asleep{t("Przywróć zapisany układ kart")}else{t("Przejdź do space")},"switch space workspace wake",&w.id);
        if c.page&&w.same_profile{add("move_tab",tf("Przenieś kartę do: {}",&[&w.name]),t("Ten sam profil · zachowuje formularze i historię karty"),"move tab przenies karte",&w.id);}
        else if c.page&&c.http_page{add("copy_tab",tf("Otwórz kopię karty w: {}",&[&w.name]),t("Inny profil · nowe logowanie i dane strony"),"copy tab skopiuj karte",&w.id);}
        awake_background|=!w.asleep&&!w.capturing;
        if !w.asleep&&!w.capturing{add("sleep_space",tf("Uśpij przestrzeń: {}",&[&w.name]),t("Zwolnij pamięć · strony przeładują się po obudzeniu; zapisz formularze"),"sleep space memory",&w.id);}
    }
    if awake_background{add("sleep_others",t("Uśpij pozostałe przestrzenie").into(),t("Pomiń aktywne nagrywanie · inne strony przeładują się; zapisz formularze"),"sleep other spaces memory pamiec","");}
    // The initial view stays compact; ">" reveals the full catalogue.
    if query.is_empty()&&!only{out.truncate(6);}out
}

// Folder menu IDs follow the native menu delegate contract; an empty folder is the root.
const FOLDER_NEW:i32=36;const FOLDER_RENAME:i32=37;const FOLDER_DELETE:i32=38;
fn folder_context_menu(s:&ffi::FolderMenuState)->Vec<ffi::FolderMenuEntry>{
    let entry=|command,label:&str|ffi::FolderMenuEntry{command,label:label.into()};
    let mut entries=vec![entry(FOLDER_NEW,if s.folder.is_empty(){t("Nowy folder…")}else{t("Nowy podfolder…")})];
    if !s.folder.is_empty(){entries.extend([entry(0,""),entry(FOLDER_RENAME,t("Zmień nazwę…")),entry(FOLDER_DELETE,t("Usuń folder"))]);}entries
}
fn folder_menu_enabled(command:i32,s:&ffi::FolderMenuState)->bool{
    s.ready&&!s.busy&&!s.workspace.is_empty()&&match command{FOLDER_NEW=>true,FOLDER_RENAME|FOLDER_DELETE=>!s.folder.is_empty(),_=>false}
}
fn folder_menu_action(command:i32,s:&ffi::FolderMenuState)->ffi::FolderMenuAction{
    use ffi::FolderMenuCommand as C;
    if !folder_menu_enabled(command,s){return ffi::FolderMenuAction{command:C::None,workspace:String::new(),folder:String::new()};}
    ffi::FolderMenuAction{command:match command{FOLDER_NEW=>C::Create,FOLDER_RENAME=>C::Rename,_=>C::Delete},workspace:s.workspace.clone(),folder:s.folder.clone()}
}

// Workspace menu IDs follow the native menu delegate contract.
const WS_SET_PROFILE:i32=1;const WS_NEW_PROFILE:i32=2;const WS_EDIT_PROFILE:i32=3;const WS_SETTINGS:i32=4;const WS_REMOVE:i32=30;const WS_COLOR_MENU:i32=31;const WS_EMOJI_MENU:i32=32;const WS_CUSTOM_COLOR:i32=33;const WS_CUSTOM_EMOJI:i32=34;const WS_CLEAR_EMOJI:i32=35;const WS_COLOR_BASE:i32=40;const WS_EMOJI_BASE:i32=50;const WS_PROFILE_BASE:i32=100;
fn workspace_color_names()->[&'static str;8]{[t("Fiolet"),t("Koral"),t("Niebo"),t("Bursztyn"),t("Róż"),t("Mięta"),t("Zieleń"),t("Grafit")]}
fn indexed(command:i32,base:i32,count:usize)->Option<usize>{(command>=base&&((command-base) as usize)<count).then(||(command-base) as usize)}
fn workspace_menu_command(command:i32,profiles:usize)->bool{
    matches!(command,WS_SET_PROFILE..=WS_SETTINGS|WS_REMOVE..=WS_CLEAR_EMOJI)||indexed(command,WS_COLOR_BASE,APPEARANCE_COLORS.len()).is_some()||indexed(command,WS_EMOJI_BASE,APPEARANCE_EMOJIS.len()).is_some()||indexed(command,WS_PROFILE_BASE,profiles).is_some()
}
fn workspace_context_menu(s:&ffi::WorkspaceMenuState)->Vec<ffi::WorkspaceMenuEntry>{
    let mut entries=Vec::new();let mut add=|command:i32,label:&str,kind:u32,group:u32|entries.push(ffi::WorkspaceMenuEntry{command,label:label.into(),kind,group});
    for (index,profile) in s.profiles.iter().enumerate(){add(WS_PROFILE_BASE+index as i32,&profile.name,3,1);}
    add(0,"",1,1);add(WS_EDIT_PROFILE,t("Edytuj profile…"),0,1);add(WS_NEW_PROFILE,t("Dodaj nowy profil…"),0,1);
    for (index,name) in workspace_color_names().iter().enumerate(){add(WS_COLOR_BASE+index as i32,name,4,2);}
    add(0,"",1,2);add(WS_CUSTOM_COLOR,t("Własny kolor…"),0,2);
    for (index,emoji) in APPEARANCE_EMOJIS.iter().enumerate(){add(WS_EMOJI_BASE+index as i32,emoji,4,3);}
    add(0,"",1,3);add(WS_CUSTOM_EMOJI,t("Własna ikonka…"),0,3);add(WS_CLEAR_EMOJI,t("Bez ikonki (kropka)"),4,3);
    add(WS_SET_PROFILE,t("Profil przeglądania"),2,0);add(WS_COLOR_MENU,t("Kolor przestrzeni"),2,0);add(WS_EMOJI_MENU,t("Ikonka przestrzeni"),2,0);add(0,"",1,0);
    // The destructive action comes last, after its own separator (ContextMenu).
    add(WS_SETTINGS,t("Nazwa i ustawienia…"),0,0);add(0,"",1,0);add(WS_REMOVE,t("Usuń przestrzeń"),0,0);entries
}
fn workspace_menu_enabled(command:i32,s:&ffi::WorkspaceMenuState)->bool{
    if !s.ready||s.workspace.is_empty()||!workspace_menu_command(command,s.profiles.len()){return false;}
    if command==WS_REMOVE{return !s.busy&&s.workspace_count>1;}true
}
fn workspace_menu_checked(command:i32,s:&ffi::WorkspaceMenuState)->bool{
    if let Some(index)=indexed(command,WS_PROFILE_BASE,s.profiles.len()){return s.profiles[index].id==s.profile;}
    if let Some(index)=indexed(command,WS_COLOR_BASE,APPEARANCE_COLORS.len()){return design_same_workspace_color(s.color,APPEARANCE_COLORS[index]);}
    if let Some(index)=indexed(command,WS_EMOJI_BASE,APPEARANCE_EMOJIS.len()){return s.emoji==APPEARANCE_EMOJIS[index];}
    command==WS_CLEAR_EMOJI&&s.emoji.is_empty()
}
fn workspace_menu_action(command:i32,s:&ffi::WorkspaceMenuState)->ffi::WorkspaceMenuAction{
    use ffi::WorkspaceMenuCommand as C;let mut action=ffi::WorkspaceMenuAction{command:C::None,workspace:s.workspace.clone(),profile:String::new(),color:0,emoji:String::new()};
    if !workspace_menu_enabled(command,s){action.workspace.clear();return action;}
    action.command=match command{
        WS_NEW_PROFILE=>C::NewProfile,WS_EDIT_PROFILE=>{action.profile=s.profile.clone();C::EditProfile},WS_SETTINGS=>C::Settings,WS_REMOVE=>C::Remove,WS_CUSTOM_COLOR=>C::CustomColor,WS_CUSTOM_EMOJI=>C::CustomEmoji,WS_CLEAR_EMOJI=>C::ClearEmoji,
        _=>if let Some(index)=indexed(command,WS_PROFILE_BASE,s.profiles.len()){action.profile=s.profiles[index].id.clone();C::AssignProfile}
           else if let Some(index)=indexed(command,WS_COLOR_BASE,APPEARANCE_COLORS.len()){action.color=APPEARANCE_COLORS[index];C::Color}
           else if let Some(index)=indexed(command,WS_EMOJI_BASE,APPEARANCE_EMOJIS.len()){action.emoji=APPEARANCE_EMOJIS[index].into();C::Emoji}
           else{C::None},
    };
    if action.command==C::None{action.workspace.clear();}action
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
            let g=split_layout(w,h,row.members.max(2));
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

// Command Center: 560 px, narrower only in a smaller window; at most 8 rows of 36 px.
fn palette_width(window_width:i32)->i32{window_width.saturating_sub(48).clamp(120,560)}
fn palette_results_height(content_height:i32)->i32{content_height.saturating_sub(230).clamp(90,8*36+7*2+16+1)}
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
// Fewer than two weights is a single page without bars or dividers.
fn pane_host_layout(width: i32, height: i32, weights: &[f64], vertical: bool) -> ffi::PaneHostLayout {
    let width=width.max(0);let height=height.max(0);
    if weights.len()<2 {return ffi::PaneHostLayout{pages:vec![ffi::Rect{x:0,y:0,width,height}],bars:vec![],dividers:vec![]};}
    let weights=split_normalized(weights,weights.len());let count=weights.len();
    let extent=if vertical{height}else{width};
    let gaps=(8*(count as i32-1)).min(extent);let gap=gaps/(count as i32-1);let available=extent-gap*(count as i32-1);
    let (mut pages,mut bars,mut dividers)=(Vec::with_capacity(count),Vec::with_capacity(count),Vec::with_capacity(count-1));
    let (mut start,mut sum)=(0,0.0);
    for (index,weight) in weights.iter().enumerate() {
        sum+=weight;let end=if index+1==count{available}else{((f64::from(available)*sum) as i32).clamp(0,available)};
        let offset=start+gap*index as i32;let size=(end-start).max(0);start=end.max(start);
        let r=if vertical{ffi::Rect{x:0,y:offset,width,height:size}}else{ffi::Rect{x:offset,y:0,width:size,height}};
        if index+1<count{dividers.push(if vertical{ffi::Rect{x:0,y:offset+size,width,height:gap}}else{ffi::Rect{x:offset+size,y:0,width:gap,height}});}
        // SplitView: a 28 px host bar above each pane.
        bars.push(ffi::Rect{x:r.x,y:r.y,width:r.width,height:28.min(r.height)});
        pages.push(ffi::Rect{x:r.x,y:r.y+28.min(r.height),width:r.width,height:(r.height-28).max(0)});
    }
    ffi::PaneHostLayout{pages,bars,dividers}
}
// Pane weights always sum to one; invalid input becomes equal panes.
fn split_normalized(weights: &[f64], count: usize) -> Vec<f64> {
    if count==0 {return vec![];}
    let valid=weights.len()==count&&weights.iter().all(|w|w.is_finite()&&*w>0.0);
    let total:f64=if valid{weights.iter().sum()}else{0.0};
    if !valid||total<=0.0 {return vec![1.0/count as f64;count];}
    weights.iter().map(|w|w/total).collect()
}
// A divider drags only its two neighbours: their shared extent is the 0..1 range.
fn split_pair_ratio(weights: &[f64], divider: usize) -> f64 {
    let w=split_normalized(weights,weights.len());if divider+1>=w.len(){return 0.5;}
    let pair=w[divider]+w[divider+1];if pair<=0.0{0.5}else{w[divider]/pair}
}
fn split_apply_ratio(weights: &[f64], divider: usize, ratio: f64) -> Vec<f64> {
    let mut w=split_normalized(weights,weights.len());if divider+1>=w.len()||!ratio.is_finite(){return w;}
    let pair=w[divider]+w[divider+1];let ratio=ratio.clamp(0.2,0.8);
    w[divider]=pair*ratio;w[divider+1]=pair-w[divider];w
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
// Folders start in `default_collapsed`; `overrides` lists the ones the user
// flipped. A revealed item stays shown under its collapsed folder (the folder
// opens just far enough for it) until that folder is toggled.
#[derive(Default)]
struct SidebarTree { overrides: BTreeMap<String, BTreeSet<String>>, default_collapsed: bool, revealed: BTreeMap<String, BTreeSet<String>> }
fn new_sidebar_tree() -> Box<SidebarTree> { Box::new(SidebarTree::default()) }
fn set_member(sets: &mut BTreeMap<String, BTreeSet<String>>, workspace: &str, id: &str, value: bool) {
    if value {sets.entry(workspace.into()).or_default().insert(id.into());}
    else if let Some(ids) = sets.get_mut(workspace) {
        ids.remove(id);
        if ids.is_empty() {sets.remove(workspace);}
    }
}
impl SidebarTree {
    fn collapsed(&self, workspace: &str, id: &str) -> bool {
        self.default_collapsed != self.overrides.get(workspace).is_some_and(|ids| ids.contains(id))
    }
    fn set_collapsed(&mut self, workspace: &str, id: &str, value: bool) {
        set_member(&mut self.overrides, workspace, id, value != self.default_collapsed);
    }
    // A new default applies to every folder: earlier flips and peeks are dropped.
    fn set_default_collapsed(&mut self, value: bool) {
        if self.default_collapsed != value {self.default_collapsed = value; self.overrides.clear(); self.revealed.clear();}
    }
    fn revealed(&self, workspace: &str, id: &str) -> bool {
        self.revealed.get(workspace).is_some_and(|ids| ids.contains(id))
    }
    fn set_revealed(&mut self, workspace: &str, id: &str, value: bool) {
        set_member(&mut self.revealed, workspace, id, value);
    }
    fn toggle(&mut self, workspace: &str, id: &str) -> bool {
        let collapsed = !self.collapsed(workspace, id);
        self.set_collapsed(workspace, id, collapsed); collapsed
    }
    fn forget_workspace(&mut self, workspace: &str) {self.overrides.remove(workspace);self.revealed.remove(workspace);}
    fn tab_plan(&self, workspace: &str, nodes: &[ffi::TreeNode], tabs: &[ffi::TabNode],
                members: &[usize], split: bool) -> ffi::TabPlan {
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
        // A group needs two or more distinct, present tabs; anything else falls back to rows.
        let distinct: BTreeSet<usize> = members.iter().copied().collect();
        let group: Option<Vec<usize>> = if split && members.len() >= 2 && distinct.len() == members.len() {
            members.iter().map(|m| positions.get(m).copied()).collect()
        } else {None};
        let mut slots = Vec::with_capacity(order.len()); let mut emitted = false;
        for index in order {
            if let Some(group) = &group {
                if group.contains(&index) {
                    if !emitted {
                        let pin = group.iter().find_map(|&i| reachable.get(tabs[i].pin.as_str()).copied()).unwrap_or(usize::MAX);
                        slots.push(ffi::TabSlot {first: group[0], members: group.clone(), pin, pair: true});
                        emitted = true;
                    }
                    continue;
                }
            }
            slots.push(ffi::TabSlot {first: index, members: vec![], pin: usize::MAX, pair: false});
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
                        let shown = !collapsed || self.revealed(workspace, &nodes[child].id);
                        pending.push((child, index, depth + 1, visible && shown));
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
struct ActionState { admission_epoch:u64, pointer:RowPointer, enabled:bool, focused:bool, on:bool, is_default:bool, on_press:bool, visual:ffi::RowVisual }
fn initial_action_state()->ActionState{ActionState{admission_epoch:1,pointer:RowPointer::default(),enabled:true,focused:false,on:false,is_default:false,on_press:false,visual:ffi::RowVisual::Normal}}
fn set_ui_language(locale:&str){i18n::set_language(locale)}
fn ui_language_is_english()->bool{i18n::language()==i18n::Language::English}
fn translate(polish:&str)->String{i18n::translate(polish)}
fn new_action_control()->Box<ActionControl>{Box::new(ActionControl{storage:ActionStorage::Standalone(initial_action_state())})}
fn action_preferred_size(dot:bool,label:bool)->ffi::Rect{ffi::Rect{x:0,y:0,width:if dot{if label{30}else{24}}else{32},height:if dot{28}else{32}}}
impl ActionState {
    fn action_cancel(&mut self)->bool{self.admission_epoch=if self.admission_epoch==0{0}else{self.admission_epoch.checked_add(1).unwrap_or(0)};let visual=if self.enabled{ffi::RowVisual::Normal}else{ffi::RowVisual::Disabled};let changed=self.pointer.origin.is_some()||self.pointer.hovered||self.pointer.key_armed||self.visual!=visual;self.pointer.cancel();self.visual=visual;changed}
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
    fn action_row_dragging(&self)->bool{self.pointer.origin.is_some()&&self.pointer.dragging}
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
        kind @ (7|8) if (key.high!=0||key.low!=0)&&!key.name.is_empty()=>Some((kind,key.high,key.low,key.name.clone())),
        15 if key.high==0&&key.low==0&&key.name=="sidebar.pair"=>Some((15,0,0,key.name.clone())),
        12 if key.high==0&&key.low==0&&key.name.is_empty()=>Some((12,0,0,String::new())),
        11 if key.high==0&&key.low==0&&matches!(key.name.as_str(),"chrome.workspaces"|"tools.history"|"palette.results"|"preferences.site-rules"|"sidebar.tabs"|"sidebar.pins")=>Some((11,0,0,key.name.clone())),
        kind @ (2|3|4|5|6|13|14) if key.high==0&&key.low==0&&!key.name.is_empty()=>Some((kind,0,0,key.name.clone())),
        _=>None,
    }
}
// Typed payloads belong to the graph record, not the native adapter's Box.
// Optional indirection keeps ordinary action records from embedding large models.
struct WorkspacePresentation {name:Vec<u16>,label:Vec<u16>,cancel_input:bool}
struct PalettePresentation {title:Vec<u16>,detail:Vec<u16>,icon:u32,index:usize,cancel_input:bool}
struct PaletteSceneState {selection:PaletteSelection,dirty:bool}
struct SiteRulePresentation {detail:Vec<u16>,editable:bool,source_index:usize,cancel_input:bool}
struct SidebarRowPresentation {title:Vec<u16>,monogram:Vec<u16>,title_utf8:String,monogram_utf8:String,color:u32,tile:bool,folder:bool}
impl SidebarRowPresentation {fn height(&self)->i32{if self.tile{38}else{34}}}
struct SidebarTabModel {high:u64,low:u64,index:usize,pin:String,pair:u64,visible:bool,can_close:bool,presentation:Option<SidebarRowPresentation>}
struct SidebarPinModel {row_shown:bool,key:String,parent_key:String,index:usize,rank:usize,depth:i32,reachable:bool,visible:bool,group:u64,base_title:Vec<u16>,collapsed:bool,revealed:bool,presentation:SidebarRowPresentation}
struct SidebarPinGroupModel {owner:u64,visible:bool}
fn pin_title(base:&[u16],folder:bool,collapsed:bool)->Vec<u16>{if !folder{return base.to_vec();}let mut title=Vec::with_capacity(base.len()+2);title.extend_from_slice(&[if collapsed{0x25b8}else{0x25be},0x20]);title.extend_from_slice(base);title}
impl SidebarPinModel {
    fn from_input(input:ffi::SidebarPinInput)->Self{let title=pin_title(&input.title,input.folder,input.collapsed);Self{row_shown:true,key:input.id,parent_key:input.parent,index:0,rank:usize::MAX,depth:0,reachable:false,visible:false,group:0,base_title:if input.folder{input.title}else{vec![]},collapsed:input.collapsed,revealed:input.revealed,presentation:SidebarRowPresentation{title_utf8:String::from_utf16_lossy(&title),monogram_utf8:if input.folder{"📁".into()}else{String::from_utf16_lossy(&input.monogram)},title,monogram:input.monogram,color:input.color,tile:false,folder:input.folder}}}
}
#[cfg(test)] impl From<&str> for ffi::SidebarPinInput{fn from(id:&str)->Self{Self{id:id.into(),parent:String::new(),title:id.encode_utf16().collect(),monogram:vec![],color:0,folder:false,collapsed:false,revealed:false}}}

struct FormCommandModel {command:ffi::FormCommand,key:String,index:usize}
// The semantic command owns its panel; native code only resolves the widget.
fn form_command_panel(command:ffi::FormCommand)->ffi::FormPanel {
    use ffi::{FormCommand as C,FormPanel as P};
    match command {
        C::WorkspaceSave|C::WorkspaceToggle|C::WorkspaceRemove|C::WorkspaceProfiles|C::WorkspaceImport|C::WorkspaceDismiss|C::WorkspaceColorPreset|C::WorkspaceEmojiPreset=>P::Workspace,
        C::ProfileCreate|C::ProfileRename|C::ProfileDelete|C::ProfileBack=>P::Profiles,
        C::ImportApply|C::ImportDismiss=>P::Import,
        C::ToolsDismiss=>P::Tools,
        C::PreferenceText|C::PreferenceSite|C::PreferenceClear|C::PreferenceDirectory|C::History|C::Workspace|C::Passwords|C::Profiles|C::Import|C::Proxy|C::Certificates|C::Providers|C::Shortcuts|C::TimeMachine|C::PreferencesDismiss|C::PreferenceCategory|C::PreferenceMore=>P::Preferences,
        _=>P::None,
    }
}

fn valid_form_command(command:ffi::FormCommand,key:&str)->bool{use ffi::FormCommand as C;match command{C::PreferenceText=>!key.is_empty(),C::WorkspaceSave|C::WorkspaceToggle|C::WorkspaceRemove|C::WorkspaceProfiles|C::WorkspaceImport|C::ProfileCreate|C::ProfileRename|C::ProfileDelete|C::ProfileBack|C::ImportApply|C::PreferenceSite|C::PreferenceClear|C::PreferenceDirectory|C::History|C::Workspace|C::Passwords|C::Profiles|C::Import|C::Proxy|C::Certificates|C::Providers|C::Shortcuts|C::TimeMachine|C::WorkspaceDismiss|C::PreferencesDismiss|C::ImportDismiss|C::ToolsDismiss|C::PreferenceMore=>key.is_empty(),_=>false}}
struct SidebarTabsModel {members:Vec<usize>,split:bool,plan:ffi::TabPlan}
struct SidebarPairModel {members:Vec<(u64,u64)>,visible:bool}
enum NodeModel { SidebarTabs(SidebarTabsModel), SidebarPair(SidebarPairModel), SidebarPinGroup(SidebarPinGroupModel), SidebarPin(SidebarPinModel), FormCommand(FormCommandModel), Checkbox(CheckboxState), Toggle(ToggleState), Select(SelectState), Workspace(WorkspacePresentation), Palette(PalettePresentation), PaletteRoot(PaletteSceneState), FormRoot(ffi::FormGroupStyle), SiteRule(SiteRulePresentation), SidebarTab(SidebarTabModel) }
impl NodeModel {
    fn row_presentation(&self)->Option<&SidebarRowPresentation>{match self{Self::SidebarTab(model)=>model.presentation.as_ref(),Self::SidebarPin(model)=>Some(&model.presentation),_=>None}}
    fn row_presentation_mut(&mut self)->Option<&mut SidebarRowPresentation>{match self{Self::SidebarTab(model)=>model.presentation.as_mut(),Self::SidebarPin(model)=>Some(&mut model.presentation),_=>None}}

    // Popup tokens and IME prefix belong to one input lease, unlike selection.
    fn detach_input(&mut self){if let Self::Select(s)=self{s.dismiss();s.reset_prefix();s.last_close=None;}}
}
struct ActionNodeRecord { kind:u32, owner:Rc<()>, id:u64, parent:u64, lease:u64, attached:bool, alive:bool, state:ActionState, model:Option<Box<NodeModel>> }
impl ActionNodeRecord {fn retire(&mut self){self.alive=false;self.attached=false;self.state.action_focus(false);self.state.action_cancel();self.model=None;self.parent=0;}}
pub struct ActionGraph { owner:Rc<()>, nodes:BTreeMap<ActionNodeIdentity,Rc<RefCell<ActionNodeRecord>>>, order:Vec<u64>, next_id:u64 }
fn new_action_graph()->Box<ActionGraph>{Box::new(ActionGraph{owner:Rc::new(()),nodes:BTreeMap::new(),order:vec![],next_id:1})}
fn new_palette_graph()->Box<ActionGraph>{let mut graph=new_action_graph();assert!(graph.reconcile_palette(vec![]).admitted);graph}

fn default_form_style()->ffi::FormGroupStyle{ffi::FormGroupStyle{vertical:false,spacing:0,intrinsic:false,dialog:false,shared:false}}
fn new_form_graph(vertical:bool,spacing:i32,intrinsic:bool,dialog:bool)->Box<ActionGraph>{let mut graph=new_action_graph();let root=graph.next_id;graph.next_id+=1;graph.nodes.insert((12,0,0,String::new()),Rc::new(RefCell::new(ActionNodeRecord{kind:12,owner:graph.owner.clone(),id:root,parent:0,lease:0,attached:false,alive:true,state:initial_action_state(),model:Some(Box::new(NodeModel::FormRoot(ffi::FormGroupStyle{vertical,spacing,intrinsic,dialog,shared:false})))})));graph.order.push(root);graph}

impl ActionGraph {
    fn rejected(&self)->ffi::ActionNodePlan{ffi::ActionNodePlan{admitted:false,ids:self.order.clone(),created:0,retired:0}}
    fn reconcile(&mut self,keys:Vec<ffi::ActionNodeKey>)->ffi::ActionNodePlan{self.reconcile_with_parents(keys,None)}
    fn reconcile_with_parents(&mut self,keys:Vec<ffi::ActionNodeKey>,parents:Option<&BTreeMap<ActionNodeIdentity,Option<ActionNodeIdentity>>>)->ffi::ActionNodePlan{
        let Some(identities)=keys.iter().map(action_node_key).collect::<Option<Vec<_>>>() else{return self.rejected();};
        let unique=identities.iter().cloned().collect::<BTreeSet<_>>();
        if unique.len()!=identities.len(){return self.rejected();}
        if unique.iter().any(|key|key.0==10&&!unique.contains(&(1,key.1,key.2,String::new()))){return self.rejected();}
        if unique.iter().any(|key|matches!(key.0,7|8)&&!unique.contains(&(if key.0==7{8}else{7},key.1,key.2,key.3.clone()))){return self.rejected();}
        if unique.iter().any(|key|key.0==14&&!unique.contains(&(2,0,0,key.3.clone()))){return self.rejected();}
        if unique.contains(&(15,0,0,"sidebar.pair".into()))&&!unique.contains(&(11,0,0,"sidebar.tabs".into())){return self.rejected();}
        if parents.is_none(){if let Some(node)=self.nodes.get(&(15,0,0,"sidebar.pair".into())){if unique.contains(&(15,0,0,"sidebar.pair".into())){if let Some(NodeModel::SidebarPair(pair))=node.borrow().model.as_deref(){if pair.members.iter().any(|&(high,low)|!unique.contains(&(1,high,low,String::new()))){return self.rejected();}}}}}
        if parents.is_none()&&unique.iter().any(|key|key.0==2&&self.nodes.get(key).is_some_and(|node|matches!(node.borrow().model.as_deref(),Some(NodeModel::SidebarPin(p)) if p.presentation.folder))&&!unique.contains(&(14,0,0,key.3.clone()))){return self.rejected();}
        // A retained logical child cannot outlive its Rust-owned parent. Whole
        // subtree removal is valid; dangling retained edges reject atomically.
        if self.nodes.values().any(|node|node.borrow().parent!=0){
            let retained_ids=unique.iter().filter_map(|key|self.nodes.get(key).map(|n|n.borrow().id)).collect::<BTreeSet<_>>();
            if unique.iter().any(|key|self.nodes.get(key).is_some_and(|node|{let node=node.borrow();node.parent!=0&&!retained_ids.contains(&node.parent)&&parents.is_none_or(|edges|!edges.contains_key(key))})){return self.rejected();}
        }
        if let Some(edges)=parents{
            if edges.keys().any(|key|!unique.contains(key)){return self.rejected();}
            let by_id=self.nodes.iter().filter(|(key,_)|unique.contains(*key)).map(|(key,node)|(node.borrow().id,key.clone())).collect::<BTreeMap<_,_>>();
            let mut effective=BTreeMap::new();
            for key in &unique{let parent=if let Some(parent)=edges.get(key){parent.clone()}else if let Some(node)=self.nodes.get(key){let id=node.borrow().parent;if id==0{None}else{let Some(parent)=by_id.get(&id) else{return self.rejected();};Some(parent.clone())}}else{None};if parent.as_ref().is_some_and(|p|!unique.contains(p)){return self.rejected();}effective.insert(key.clone(),parent);}
            for key in &unique{let mut visited=BTreeSet::new();let mut cursor=key;while let Some(Some(parent))=effective.get(cursor){if !visited.insert(cursor){return self.rejected();}cursor=parent;}}
        }
        let created=identities.iter().filter(|key|!self.nodes.contains_key(*key)).count();
        if self.next_id.checked_add(created as u64).is_none(){return self.rejected();}
        let mut retired=0;
        self.nodes.retain(|key,node|{if unique.contains(key){return true;}let mut node=node.borrow_mut();node.retire();retired+=1;false});
        let mut order=Vec::with_capacity(identities.len());
        for key in identities {
            let kind=key.0;let node=self.nodes.entry(key).or_insert_with(||{let id=self.next_id;self.next_id+=1;Rc::new(RefCell::new(ActionNodeRecord{kind,owner:self.owner.clone(),id,parent:0,lease:0,attached:false,alive:true,state:initial_action_state(),model:None}))});
            order.push(node.borrow().id);
        }
        if let Some(edges)=parents{for (key,parent) in edges{let id=parent.as_ref().map_or(0,|key|self.nodes[key].borrow().id);self.nodes[key].borrow_mut().parent=id;}}
        self.order=order.clone();ffi::ActionNodePlan{admitted:true,ids:order,created,retired}
    }
    // Kind 9 is private to form membership: native projections cannot create or
    // acquire these keys. The group receives one lease with its fresh Rust ID.
    fn append_form_action(&mut self)->Box<ActionControl>{
        let group=(12,0,0,String::new());let missing=!self.nodes.contains_key(&group);
        // Reserve both root and member before mutation so ID exhaustion cannot
        // leave a partial scene behind. A live empty group retains its root.
        let Some(next)=self.next_id.checked_add(if missing{2}else{1}) else{return Box::new(ActionControl{storage:ActionStorage::Retired});};
        if missing{let root=self.next_id;self.next_id+=1;self.nodes.insert(group.clone(),Rc::new(RefCell::new(ActionNodeRecord{kind:12,owner:self.owner.clone(),id:root,parent:0,lease:0,attached:false,alive:true,state:initial_action_state(),model:Some(Box::new(NodeModel::FormRoot(default_form_style())))})));self.order.push(root);}
        let parent={let mut root=self.nodes[&group].borrow_mut();if root.model.is_none(){root.model=Some(Box::new(NodeModel::FormRoot(default_form_style())));}root.id};let id=self.next_id;
        let node=Rc::new(RefCell::new(ActionNodeRecord{kind:9,owner:self.owner.clone(),id,parent,lease:1,attached:true,alive:true,state:initial_action_state(),model:None}));
        self.nodes.insert((9,id,0,String::new()),node.clone());self.order.push(id);self.next_id=next;
        Box::new(ActionControl{storage:ActionStorage::Shared{node,lease:1}})
    }
    fn form_scene(&self)->ffi::FormScene{let root=self.nodes.get(&(12,0,0,String::new())).map_or(0,|node|node.borrow().id);let children=self.order.iter().copied().filter(|id|self.nodes.contains_key(&(9,*id,0,String::new()))).collect();ffi::FormScene{root,children}}
    fn form_style(&self)->ffi::FormGroupStyle{self.nodes.get(&(12,0,0,String::new())).and_then(|node|{let node=node.borrow();if let Some(NodeModel::FormRoot(style))=node.model.as_deref(){Some(*style)}else{None}}).unwrap_or_else(default_form_style)}
    fn cancel_form_input(&mut self){for (key,node) in &self.nodes{if key.0==9{let mut node=node.borrow_mut();node.state.action_cancel();if let Some(model)=node.model.as_deref_mut(){model.detach_input();}}}}
    fn form_set_shared(&mut self,shared:bool)->bool{let changed={let Some(node)=self.nodes.get(&(12,0,0,String::new())) else{return false;};let mut node=node.borrow_mut();let Some(NodeModel::FormRoot(style))=node.model.as_deref_mut() else{return false;};if style.shared==shared{return false;}style.shared=shared;true};if changed{self.cancel_form_input();}changed}
    fn reorder_form_actions(&mut self,ids:Vec<u64>)->bool{
        let current=self.form_scene();if current.root==0||ids.len()!=current.children.len(){return false;}
        let unique=ids.iter().copied().collect::<BTreeSet<_>>();if unique.len()!=ids.len()||unique.iter().any(|id|!self.nodes.contains_key(&(9,*id,0,String::new()))){return false;}
        if current.children==ids{return true;}self.order.retain(|id|!unique.contains(id));self.order.extend(ids);self.cancel_form_input();true
    }
    fn form_layout(&self,width:i32,height:i32,preferred:bool,link_limit:i32,top:i32,left:i32,bottom:i32,right:i32,children:&[ffi::FormMeasure])->ffi::FormLayout{
        let invalid=||ffi::FormLayout{admitted:false,width:0,height:0,children:vec![]};let Some(root)=self.nodes.get(&(12,0,0,String::new())) else{return invalid();};let root=root.borrow();let Some(NodeModel::FormRoot(style))=root.model.as_deref() else{return invalid();};if style.shared{return invalid();}
        let mut cursor=0;let mut measured=Vec::with_capacity(children.len());
        for child in children{let Some(record)=self.nodes.get(&(9,child.id,0,String::new())) else{return invalid();};if record.borrow().parent!=root.id{return invalid();}let Some(offset)=self.order[cursor..].iter().position(|id|*id==child.id) else{return invalid();};cursor+=offset+1;measured.push(ffi::ColumnChild{width:child.width,height:child.height,top:0,left:0,bottom:0,right:0});}
        let width=if preferred&&style.intrinsic{-1}else{width};let layout=if style.dialog{dialog_actions_layout(width,height,style.spacing,link_limit,top,left,bottom,right,&measured)}else{action_group_layout(width,height,style.vertical,style.spacing,&measured)};
        ffi::FormLayout{admitted:true,width:layout.width,height:layout.height,children:children.iter().zip(layout.children).map(|(child,rect)|ffi::FormCell{id:child.id,rect}).collect()}
    }
    fn retire_form_action(&mut self,id:u64)->bool{
        let Some(node)=self.nodes.remove(&(9,id,0,String::new())) else{return false;};
        node.borrow_mut().retire();self.order.retain(|old|*old!=id);true
    }
    fn reconcile_chrome(&mut self,items:Vec<ffi::WorkspaceSceneInput>)->ffi::ActionNodePlan{
        let group=(11,0,0,"chrome.workspaces".to_owned());
        if items.iter().any(|item|self.nodes.get(&(3,0,0,item.key.clone())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::Workspace(_))))){return self.rejected();}
        let previous_by_id=self.nodes.iter().filter(|(key,_)|key.0==3).map(|(key,node)|(node.borrow().id,key.3.as_str())).collect::<BTreeMap<_,_>>();
        let previous=self.order.iter().filter_map(|id|previous_by_id.get(id).copied()).collect::<Vec<_>>();
        let geometry_changed=previous.into_iter().ne(items.iter().map(|i|i.key.as_str()))||items.iter().any(|item|self.nodes.get(&(3,0,0,item.key.clone())).is_some_and(|node|{let n=node.borrow();matches!(n.model.as_deref(),Some(NodeModel::Workspace(p)) if p.label!=item.label)}));
        let mut keys=Vec::with_capacity(9+items.len());
        for name in ["sidebar.add","header.sidebar","header.back","header.forward","header.reload","sidebar.address","sidebar.new-tab"]{keys.push(ffi::ActionNodeKey{kind:4,high:0,low:0,name:name.into()});}
        keys.push(ffi::ActionNodeKey{kind:11,high:0,low:0,name:group.3.clone()});
        for item in &items{keys.push(ffi::ActionNodeKey{kind:3,high:0,low:0,name:item.key.clone()});}
        let plan=self.reconcile(keys);if !plan.admitted{return plan;}
        let root=self.nodes[&group].borrow().id;
        for item in items{let mut node=self.nodes[&(3,0,0,item.key)].borrow_mut();let mut name=t("Przestrzeń: ").encode_utf16().collect::<Vec<_>>();name.extend(item.name);if item.sleeping{name.extend(t(" · uśpiona").encode_utf16());}node.parent=root;node.state.action_on(item.active);node.state.action_enabled(item.enabled);if geometry_changed{node.state.action_cancel();}if let Some(NodeModel::Workspace(p))=node.model.as_deref_mut(){p.name=name;p.label=item.label;p.cancel_input=geometry_changed;}else{node.model=Some(Box::new(NodeModel::Workspace(WorkspacePresentation{name,label:item.label,cancel_input:geometry_changed})));}}
        plan
    }
    fn workspace_scene(&self)->ffi::WorkspaceScene{
        let root=self.nodes.get(&(11,0,0,"chrome.workspaces".into())).map_or(0,|node|node.borrow().id);
        let by_id=self.nodes.iter().map(|(key,node)|(node.borrow().id,(key,node))).collect::<BTreeMap<_,_>>();
        let children=self.order.iter().filter_map(|id|{let (key,node)=by_id.get(id)?;let node=node.borrow();let Some(NodeModel::Workspace(p))=node.model.as_deref() else{return None;};if root==0||node.parent!=root{return None;}Some(ffi::WorkspaceSceneChild{id:node.id,parent:node.parent,key:key.3.clone(),name:p.name.clone(),label:p.label.clone(),active:node.state.on,enabled:node.state.enabled,cancel_input:p.cancel_input})}).collect();
        ffi::WorkspaceScene{root,children}
    }
    fn read_palette<R>(&self,f:impl FnOnce(&PaletteSceneState)->R)->Option<R>{let node=self.nodes.get(&(11,0,0,"palette.results".into()))?.borrow();if let Some(NodeModel::PaletteRoot(state))=node.model.as_deref(){Some(f(state))}else{None}}
    fn write_palette<R>(&mut self,f:impl FnOnce(&mut PaletteSceneState)->R)->Option<R>{let mut node=self.nodes.get(&(11,0,0,"palette.results".into()))?.borrow_mut();if let Some(NodeModel::PaletteRoot(state))=node.model.as_deref_mut(){Some(f(state))}else{None}}
    fn cancel_palette_input(&mut self){for node in self.nodes.values(){let mut node=node.borrow_mut();if let Some(NodeModel::Palette(p))=node.model.as_deref_mut(){p.cancel_input=true;node.state.action_cancel();}}}
    fn palette_reset(&mut self){self.write_palette(|state|{state.selection.reset();state.dirty=true;});self.cancel_palette_input();}
    fn palette_selected(&self)->usize{self.read_palette(|state|state.selection.selected()).unwrap_or(0)}
    fn palette_moved(&self)->bool{self.read_palette(|state|state.selection.moved()).unwrap_or(false)}
    fn palette_step(&mut self,count:usize,backwards:bool)->bool{let changed=self.write_palette(|state|{let changed=state.selection.step(count,backwards);state.dirty|=changed;changed}).unwrap_or(false);if changed{self.cancel_palette_input();}changed}
    fn palette_reconcile_selection(&mut self,count:usize,preserved:usize,found:bool){let changed=self.write_palette(|state|{let before=state.selection.selected();state.selection.reconcile(count,preserved,found);let changed=before!=state.selection.selected();state.dirty|=changed;changed}).unwrap_or(false);if changed{self.cancel_palette_input();}}
    fn reconcile_palette(&mut self,items:Vec<ffi::PaletteSceneInput>)->ffi::ActionNodePlan{
        let group=(11,0,0,"palette.results".to_owned());
        if self.nodes.get(&group).is_some_and(|n|n.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::PaletteRoot(_)))){return self.rejected();}
        if items.iter().any(|item|item.icon>2||self.nodes.get(&(6,0,0,item.key.clone())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::Palette(_))))){return self.rejected();}
        let previous_by_id=self.nodes.iter().filter(|(key,_)|key.0==6).map(|(key,node)|(node.borrow().id,key.3.as_str())).collect::<BTreeMap<_,_>>();
        let order_changed=self.order.iter().filter_map(|id|previous_by_id.get(id).copied()).ne(items.iter().map(|i|i.key.as_str()));
        let mut keys=Vec::with_capacity(1+items.len());keys.push(ffi::ActionNodeKey{kind:11,high:0,low:0,name:group.3.clone()});for item in &items{keys.push(ffi::ActionNodeKey{kind:6,high:0,low:0,name:item.key.clone()});}
        let plan=self.reconcile(keys);if !plan.admitted{return plan;}
        let root={let mut node=self.nodes[&group].borrow_mut();if node.model.is_none(){node.model=Some(Box::new(NodeModel::PaletteRoot(PaletteSceneState{selection:PaletteSelection::default(),dirty:false})));}node.id};
        self.write_palette(|state|{let before=state.selection.selected();state.selection.reconcile(items.len(),0,false);state.dirty|=before!=state.selection.selected();});
        let selection_changed=self.read_palette(|state|state.dirty).unwrap_or(false);let mut invalidated=order_changed||selection_changed;
        for (index,item) in items.into_iter().enumerate(){let mut node=self.nodes[&(6,0,0,item.key)].borrow_mut();let content_changed=matches!(node.model.as_deref(),Some(NodeModel::Palette(p)) if p.title!=item.title||p.detail!=item.detail||p.icon!=item.icon);invalidated|=content_changed;let cancel_input=false;node.parent=root;
            if let Some(NodeModel::Palette(p))=node.model.as_deref_mut(){p.title=item.title;p.detail=item.detail;p.icon=item.icon;p.index=index;p.cancel_input=cancel_input;}
            else{node.model=Some(Box::new(NodeModel::Palette(PalettePresentation{title:item.title,detail:item.detail,icon:item.icon,index,cancel_input})));}
        }if invalidated{self.cancel_palette_input();}self.write_palette(|state|state.dirty=false);plan
    }
    fn palette_scene(&self)->ffi::PaletteScene{
        let root=self.nodes.get(&(11,0,0,"palette.results".into())).map_or(0,|n|n.borrow().id);let selected=self.palette_selected();
        let by_id=self.nodes.iter().map(|(key,node)|(node.borrow().id,(key,node))).collect::<BTreeMap<_,_>>();
        let children=self.order.iter().filter_map(|id|{let (key,node)=by_id.get(id)?;let node=node.borrow();let Some(NodeModel::Palette(p))=node.model.as_deref() else{return None;};if root==0||node.parent!=root{return None;}Some(ffi::PaletteSceneChild{id:node.id,parent:node.parent,key:key.3.clone(),title:p.title.clone(),detail:p.detail.clone(),icon:p.icon,highlighted:p.index==selected,height:36,cancel_input:p.cancel_input})}).collect();ffi::PaletteScene{root,children}
    }
    fn palette_target(&self,key:&str,id:u64)->ffi::PaletteTarget{
        let invalid=||ffi::PaletteTarget{valid:false,index:0};let Some(node)=self.nodes.get(&(6,0,0,key.to_owned())) else{return invalid();};let node=node.borrow();if !node.alive||!node.attached||!node.state.enabled||id==0||id!=node.id{return invalid();}if let Some(NodeModel::Palette(p))=node.model.as_deref(){ffi::PaletteTarget{valid:true,index:p.index}}else{invalid()}
    }
    fn reconcile_site_rules(&mut self,items:Vec<ffi::SiteRuleSceneInput>)->ffi::ActionNodePlan{
        let mut sources=BTreeSet::new();if items.iter().any(|item|!sources.insert(item.source_index)||self.nodes.get(&(13,0,0,item.key.clone())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SiteRule(_))))){return self.rejected();}
        let previous_by_id=self.nodes.iter().filter(|(key,_)|key.0==13).map(|(key,node)|(node.borrow().id,key.3.as_str())).collect::<BTreeMap<_,_>>();let mut invalidated=self.order.iter().filter_map(|id|previous_by_id.get(id).copied()).ne(items.iter().map(|i|i.key.as_str()));
        let group=(11,0,0,"preferences.site-rules".to_owned());let mut keys=Vec::with_capacity(1+items.len());keys.push(ffi::ActionNodeKey{kind:11,high:0,low:0,name:group.3.clone()});for item in &items{keys.push(ffi::ActionNodeKey{kind:13,high:0,low:0,name:item.key.clone()});}let plan=self.reconcile(keys);if !plan.admitted{return plan;}
        let root=self.nodes[&group].borrow().id;
        for item in items{let mut node=self.nodes[&(13,0,0,item.key)].borrow_mut();invalidated|=matches!(node.model.as_deref(),Some(NodeModel::SiteRule(p)) if p.detail!=item.detail||p.editable!=item.editable||p.source_index!=item.source_index);node.parent=root;
            if let Some(NodeModel::SiteRule(p))=node.model.as_deref_mut(){p.detail=item.detail;p.editable=item.editable;p.source_index=item.source_index;p.cancel_input=false;}else{node.model=Some(Box::new(NodeModel::SiteRule(SiteRulePresentation{detail:item.detail,editable:item.editable,source_index:item.source_index,cancel_input:false})));}
        }
        if invalidated{for node in self.nodes.values(){let mut node=node.borrow_mut();if let Some(NodeModel::SiteRule(p))=node.model.as_deref_mut(){p.cancel_input=true;node.state.action_cancel();}}}plan
    }
    fn site_rule_scene(&self)->ffi::SiteRuleScene{
        let root=self.nodes.get(&(11,0,0,"preferences.site-rules".into())).map_or(0,|node|node.borrow().id);let by_id=self.nodes.iter().map(|(key,node)|(node.borrow().id,(key,node))).collect::<BTreeMap<_,_>>();
        let children=self.order.iter().filter_map(|id|{let (key,node)=by_id.get(id)?;let node=node.borrow();let Some(NodeModel::SiteRule(p))=node.model.as_deref() else{return None;};if root==0||node.parent!=root{return None;}Some(ffi::SiteRuleSceneChild{id:node.id,parent:node.parent,key:key.3.clone(),detail:p.detail.clone(),editable:p.editable,cancel_input:p.cancel_input})}).collect();ffi::SiteRuleScene{root,children}
    }
    fn site_rule_target(&self,key:&str,id:u64)->ffi::SiteRuleTarget{
        let invalid=||ffi::SiteRuleTarget{valid:false,index:0};let Some(node)=self.nodes.get(&(13,0,0,key.to_owned())) else{return invalid();};let node=node.borrow();if !node.alive||!node.attached||!node.state.enabled||id==0||id!=node.id{return invalid();}if let Some(NodeModel::SiteRule(p))=node.model.as_deref(){if p.editable{return ffi::SiteRuleTarget{valid:true,index:p.source_index};}}invalid()
    }
    fn reconcile_sidebar(&mut self,pins:Vec<ffi::SidebarPinInput>,tabs:Vec<ffi::SidebarTabInput>)->ffi::ActionNodePlan{self.reconcile_sidebar_layout(pins,tabs,vec![],false)}
    fn reconcile_sidebar_layout(&mut self,pins:Vec<ffi::SidebarPinInput>,tabs:Vec<ffi::SidebarTabInput>,members:Vec<usize>,split:bool)->ffi::ActionNodePlan{
        if pins.iter().any(|pin|(pin.collapsed&&!pin.folder)||self.nodes.get(&(2,0,0,pin.id.clone())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SidebarPin(_))))||self.nodes.get(&(14,0,0,pin.id.clone())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SidebarPinGroup(_)))))||tabs.iter().any(|tab|self.nodes.get(&(10,tab.high,tab.low,String::new())).is_some_and(|node|node.borrow().model.is_some())){return self.rejected();}
        let mut indices=BTreeSet::new();if tabs.iter().any(|tab|!indices.insert(tab.index)||self.nodes.get(&(1,tab.high,tab.low,String::new())).is_some_and(|node|node.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SidebarTab(_))))){return self.rejected();}
        if self.nodes.get(&(11,0,0,"sidebar.tabs".into())).is_some_and(|n|n.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SidebarTabs(_))))||self.nodes.get(&(15,0,0,"sidebar.pair".into())).is_some_and(|n|n.borrow().model.as_deref().is_some_and(|m|!matches!(m,NodeModel::SidebarPair(_)))){return self.rejected();}
        let old_layout=self.nodes.get(&(11,0,0,"sidebar.tabs".into())).and_then(|n|{let n=n.borrow();if let Some(NodeModel::SidebarTabs(m))=n.model.as_deref(){Some((m.members.clone(),m.split))}else{None}});let old_pair_parent=self.nodes.get(&(15,0,0,"sidebar.pair".into())).map(|n|n.borrow().parent);
        let nodes=pins.iter().map(|p|ffi::TreeNode{id:p.id.clone(),parent:p.parent.clone(),folder:p.folder}).collect::<Vec<_>>();let mut tree=new_sidebar_tree();for pin in &pins{if pin.folder&&pin.collapsed{tree.set_collapsed("",&pin.id,true);}if pin.revealed{tree.set_revealed("",&pin.id,true);}}let peeking=pins.iter().filter(|p|p.revealed).map(|p|p.parent.clone()).collect::<BTreeSet<_>>();let tab_nodes=tabs.iter().map(|t|ffi::TabNode{index:t.index,pin:t.pin.clone()}).collect::<Vec<_>>();let tab_plan=tree.tab_plan("",&nodes,&tab_nodes,&members,split);let pair_slot=tab_plan.slots.iter().find(|slot|slot.pair).map(|slot|(slot.members.clone(),slot.pin));let pin_shown=tab_plan.pins.iter().map(|p|(p.index,p.visible)).collect::<BTreeMap<_,_>>();let layout=tree.tree_plan("",&nodes);let positions=layout.iter().enumerate().map(|(rank,e)|(e.index,(rank,e))).collect::<BTreeMap<_,_>>();
        let tab_root=(11,0,0,"sidebar.tabs".to_owned());let pin_root=(11,0,0,"sidebar.pins".to_owned());let mut keys=Vec::with_capacity(2+pins.len()*2+tabs.len()*2);let mut edges=BTreeMap::new();
        for root in [&tab_root,&pin_root]{keys.push(ffi::ActionNodeKey{kind:11,high:0,low:0,name:root.3.clone()});edges.insert(root.clone(),None);}
        for (index,pin) in pins.iter().enumerate(){let parent=positions.get(&index).map(|(_,entry)|if entry.parent==usize::MAX{pin_root.clone()}else{(14,0,0,pins[entry.parent].id.clone())});let key=(2,0,0,pin.id.clone());keys.push(ffi::ActionNodeKey{kind:2,high:0,low:0,name:pin.id.clone()});edges.insert(key,parent.clone());if pin.folder{keys.push(ffi::ActionNodeKey{kind:14,high:0,low:0,name:pin.id.clone()});edges.insert((14,0,0,pin.id.clone()),parent);}}
        let pair_key=(15,0,0,"sidebar.pair".to_owned());let pair_parent=pair_slot.as_ref().map(|(_,pin)|if *pin==usize::MAX{tab_root.clone()}else{edges[&(2,0,0,pins[*pin].id.clone())].clone().unwrap_or_else(||pin_root.clone())});
        if let Some(parent)=&pair_parent{keys.push(ffi::ActionNodeKey{kind:15,high:0,low:0,name:"sidebar.pair".into()});edges.insert(pair_key.clone(),Some(parent.clone()));}
        let can_close=tabs.len()>1;for (position,tab) in tabs.iter().enumerate(){let key=(1,tab.high,tab.low,String::new());keys.push(ffi::ActionNodeKey{kind:1,high:tab.high,low:tab.low,name:String::new()});edges.insert(key.clone(),Some(if pair_slot.as_ref().is_some_and(|(group,_)|group.contains(&position)){pair_key.clone()}else{tab_root.clone()}));if can_close{keys.push(ffi::ActionNodeKey{kind:10,high:tab.high,low:tab.low,name:String::new()});edges.insert((10,tab.high,tab.low,String::new()),Some(key));}}
        let old_order=self.order.clone();let plan=self.reconcile_with_parents(keys,Some(&edges));if !plan.admitted{return plan;}let mut invalidated=old_order!=plan.ids||old_layout.as_ref()!=Some(&(members.clone(),split));
        for (index,pin) in pins.into_iter().enumerate(){let key=(2,0,0,pin.id.clone());let group=if pin.folder{self.nodes[&(14,0,0,pin.id.clone())].borrow().id}else{0};let mut next=SidebarPinModel::from_input(pin);next.index=index;next.group=group;next.row_shown=pin_shown.get(&index).copied().unwrap_or(true);if let Some((rank,entry))=positions.get(&index){next.rank=*rank;next.depth=entry.depth;next.reachable=true;next.visible=entry.visible&&next.row_shown;}
            let mut node=self.nodes[&key].borrow_mut();let changed=match node.model.as_deref(){Some(NodeModel::SidebarPin(p))=>p.parent_key!=next.parent_key||p.rank!=next.rank||p.visible!=next.visible||p.collapsed!=next.collapsed||p.revealed!=next.revealed||p.presentation.title!=next.presentation.title||p.presentation.monogram!=next.presentation.monogram||p.presentation.color!=next.presentation.color||p.presentation.folder!=next.presentation.folder,_=>true};if group!=0{self.nodes[&(14,0,0,next.key.clone())].borrow_mut().model=Some(Box::new(NodeModel::SidebarPinGroup(SidebarPinGroupModel{owner:node.id,visible:positions.get(&index).is_some_and(|(_,entry)|entry.visible)&&(!next.collapsed||peeking.contains(&next.key))})));}node.model=Some(Box::new(NodeModel::SidebarPin(next)));if changed{node.state.action_cancel();}}
        let pair_id=pair_slot.as_ref().map_or(0,|_|self.nodes[&pair_key].borrow().id);let pair_visible=pair_slot.as_ref().is_none_or(|(_,pin)|*pin==usize::MAX||positions.get(pin).is_some_and(|(_,entry)|entry.visible));
        if let Some((group,_))=&pair_slot{let mut pair=self.nodes[&pair_key].borrow_mut();invalidated|=old_pair_parent!=Some(pair.parent);pair.model=Some(Box::new(NodeModel::SidebarPair(SidebarPairModel{members:group.iter().map(|&i|(tabs[i].high,tabs[i].low)).collect(),visible:pair_visible})));}
        for (position,tab) in tabs.into_iter().enumerate(){let pair=if pair_slot.as_ref().is_some_and(|(group,_)|group.contains(&position)){pair_id}else{0};let visible=pair==0||pair_visible;let mut node=self.nodes[&(1,tab.high,tab.low,String::new())].borrow_mut();invalidated|=matches!(node.model.as_deref(),Some(NodeModel::SidebarTab(p)) if p.index!=tab.index||p.pin!=tab.pin||p.pair!=pair||p.visible!=visible||p.can_close!=can_close);if let Some(NodeModel::SidebarTab(p))=node.model.as_deref_mut(){p.index=tab.index;p.pin=tab.pin;p.pair=pair;p.visible=visible;p.can_close=can_close;}else{node.model=Some(Box::new(NodeModel::SidebarTab(SidebarTabModel{high:tab.high,low:tab.low,index:tab.index,pin:tab.pin,pair,visible,can_close,presentation:None})));}}
        self.nodes[&tab_root].borrow_mut().model=Some(Box::new(NodeModel::SidebarTabs(SidebarTabsModel{members,split,plan:tab_plan})));
        if invalidated{for (key,node) in &self.nodes{if matches!(key.0,1|2|10){node.borrow_mut().state.action_cancel();}}}plan
    }
    fn sidebar_tabs(&self)->ffi::SidebarTabTree{let root=self.nodes.get(&(11,0,0,"sidebar.tabs".into())).map_or(0,|n|n.borrow().id);let pair=self.nodes.get(&(15,0,0,"sidebar.pair".into())).map(|n|n.borrow());let mut result=ffi::SidebarTabTree{root,pair:pair.as_ref().map_or(0,|p|p.id),parent:pair.as_ref().map_or(0,|p|p.parent),visible:pair.as_ref().is_none_or(|p|matches!(p.model.as_deref(),Some(NodeModel::SidebarPair(m)) if m.visible)),slots:vec![],pins:vec![]};if let Some(node)=self.nodes.get(&(11,0,0,"sidebar.tabs".into())){if let Some(NodeModel::SidebarTabs(m))=node.borrow().model.as_deref(){result.slots=m.plan.slots.iter().map(|s|ffi::TabSlot{first:s.first,members:s.members.clone(),pin:s.pin,pair:s.pair}).collect();result.pins=m.plan.pins.iter().map(|p|ffi::PinVisibility{index:p.index,visible:p.visible}).collect();}}result}
    fn refresh_pair_visibility(&mut self){let Some(pair)=self.nodes.get(&(15,0,0,"sidebar.pair".into())).cloned() else{return;};let parent=pair.borrow().parent;let visible=self.nodes.values().find_map(|node|{let node=node.borrow();if node.id!=parent{return None;}Some(match node.model.as_deref(){Some(NodeModel::SidebarPinGroup(m))=>m.visible,_=>true})}).unwrap_or(false);let (id,members)={let mut pair=pair.borrow_mut();let id=pair.id;let Some(NodeModel::SidebarPair(m))=pair.model.as_deref_mut() else{return;};m.visible=visible;(id,m.members.clone())};for (high,low) in members{if let Some(node)=self.nodes.get(&(1,high,low,String::new())){let mut node=node.borrow_mut();if let Some(NodeModel::SidebarTab(m))=node.model.as_deref_mut(){if m.pair==id&&m.visible!=visible{m.visible=visible;node.state.action_cancel();if let Some(close)=self.nodes.get(&(10,high,low,String::new())){close.borrow_mut().state.action_cancel();}}}}}}
    fn pin_tree(&self)->ffi::SidebarPinTree{let root=self.nodes.get(&(11,0,0,"sidebar.pins".into())).map_or(0,|n|n.borrow().id);let mut ordered=self.nodes.values().filter_map(|node|{let node=node.borrow();if let Some(NodeModel::SidebarPin(p))=node.model.as_deref(){if p.reachable{return Some((p.rank,ffi::SidebarPinTreeEntry{index:p.index,key:p.key.clone(),id:node.id,parent:node.parent,group:p.group,depth:p.depth,collapsed:p.collapsed,visible:p.visible}));}}None}).collect::<Vec<_>>();ordered.sort_by_key(|(rank,_)|*rank);ffi::SidebarPinTree{root,entries:ordered.into_iter().map(|(_,entry)|entry).collect()}}
    fn refresh_pin_visibility(&mut self){
        // `open` groups show every child; a collapsed group still shows (and is
        // itself shown for) the children revealed under it.
        let tree=self.pin_tree();let revealed=|key:&str|self.nodes.get(&(2,0,0,key.to_owned())).is_some_and(|n|matches!(n.borrow().model.as_deref(),Some(NodeModel::SidebarPin(p)) if p.revealed));
        let peeking=tree.entries.iter().filter(|e|revealed(&e.key)).map(|e|e.parent).collect::<BTreeSet<_>>();let flags=tree.entries.iter().map(|e|revealed(&e.key)).collect::<Vec<_>>();
        let mut open=BTreeMap::from([(tree.root,true)]);let mut shown_groups=BTreeMap::from([(tree.root,true)]);
        for (entry,revealed) in tree.entries.into_iter().zip(flags){let shown=open.get(&entry.parent).copied().unwrap_or(false)||(revealed&&shown_groups.get(&entry.parent).copied().unwrap_or(false));let mut row=self.nodes[&(2,0,0,entry.key.clone())].borrow_mut();if let Some(NodeModel::SidebarPin(p))=row.model.as_deref_mut(){let next=shown&&p.row_shown;let changed=p.visible!=next;p.visible=next;if changed{row.state.action_cancel();}}drop(row);if entry.group!=0{let mut group=self.nodes[&(14,0,0,entry.key)].borrow_mut();if let Some(NodeModel::SidebarPinGroup(p))=group.model.as_deref_mut(){p.visible=shown&&(!entry.collapsed||peeking.contains(&entry.group));open.insert(entry.group,shown&&!entry.collapsed);shown_groups.insert(entry.group,p.visible);}}}self.refresh_pair_visibility();}
    fn pin_target(&self,key:&str,id:u64)->ffi::SidebarPinTarget{let invalid=||ffi::SidebarPinTarget{valid:false,folder:false,collapsed:false,group:0};let Some(node)=self.nodes.get(&(2,0,0,key.into())) else{return invalid();};let node=node.borrow();if !node.alive||!node.attached||!node.state.enabled||id==0||node.id!=id{return invalid();}if let Some(NodeModel::SidebarPin(pin))=node.model.as_deref(){if !pin.reachable||!pin.visible{return invalid();}if pin.presentation.folder{let Some(group)=self.nodes.get(&(14,0,0,key.into())) else{return invalid();};let group=group.borrow();if !matches!(group.model.as_deref(),Some(NodeModel::SidebarPinGroup(p)) if p.owner==id)||group.id!=pin.group||group.parent!=node.parent{return invalid();}}ffi::SidebarPinTarget{valid:true,folder:pin.presentation.folder,collapsed:pin.collapsed,group:pin.group}}else{invalid()}}
    fn set_pin_collapsed(&mut self,input:&ActionControl,collapsed:bool)->bool{if !self.owns_input(input){return false;}let binding=input.pin_binding();if !binding.valid||!binding.folder{return false;}{let Some(node)=self.nodes.get(&(2,0,0,binding.key)) else{return false;};let mut node=node.borrow_mut();let Some(NodeModel::SidebarPin(pin))=node.model.as_deref_mut() else{return false;};if pin.collapsed!=collapsed{pin.collapsed=collapsed;pin.presentation.title=pin_title(&pin.base_title,true,collapsed);pin.presentation.title_utf8=String::from_utf16_lossy(&pin.presentation.title);}}
        // Toggling a folder ends the peek of the items revealed under it.
        let folder=input.pin_binding().key;for node in self.nodes.values(){if let Some(NodeModel::SidebarPin(p))=node.borrow_mut().model.as_deref_mut(){if p.parent_key==folder{p.revealed=false;}}}self.refresh_pin_visibility();true}
    fn project_tab(&mut self,input:&ActionControl,title:Vec<u16>,monogram:Vec<u16>,color:u32,tile:bool)->ffi::SidebarTabUpdate{
        let rejected=||ffi::SidebarTabUpdate{admitted:false,changed:false,title_changed:false,geometry_changed:false};
        if !self.owns_input(input){return rejected();}let binding=input.tab_binding();if !binding.valid{return rejected();}
        let key=(1,binding.high,binding.low,String::new());let Some(record)=self.nodes.get(&key) else{return rejected();};
        let mut record=record.borrow_mut();if record.id!=binding.id{return rejected();}let Some(NodeModel::SidebarTab(model))=record.model.as_deref_mut() else{return rejected();};
        let title_changed=model.presentation.as_ref().is_none_or(|p|p.title!=title);let geometry_changed=model.presentation.as_ref().is_none_or(|p|p.tile!=tile);
        let changed=title_changed||geometry_changed||model.presentation.as_ref().is_none_or(|p|p.monogram!=monogram||p.color!=color);
        if changed{model.presentation=Some(SidebarRowPresentation{title_utf8:String::from_utf16_lossy(&title),monogram_utf8:String::from_utf16_lossy(&monogram),title,monogram,color,tile,folder:false});}
        if geometry_changed{record.state.action_cancel();drop(record);if let Some(close)=self.nodes.get(&(10,binding.high,binding.low,String::new())){close.borrow_mut().state.action_cancel();}}
        ffi::SidebarTabUpdate{admitted:true,changed,title_changed,geometry_changed}
    }
    fn tab_target(&self,high:u64,low:u64,id:u64,close:bool)->ffi::SidebarTabTarget{
        let invalid=||ffi::SidebarTabTarget{valid:false,index:0};let Some(node)=self.nodes.get(&(1,high,low,String::new())) else{return invalid();};let node=node.borrow();if !node.alive||!node.attached||!node.state.enabled||id==0||node.id!=id{return invalid();}let Some(NodeModel::SidebarTab(model))=node.model.as_deref() else{return invalid();};if !model.visible{return invalid();}if close{if !model.can_close{return invalid();}let Some(button)=self.nodes.get(&(10,high,low,String::new())) else{return invalid();};let button=button.borrow();if !button.alive||!button.attached||!button.state.enabled||button.parent!=id{return invalid();}}ffi::SidebarTabTarget{valid:true,index:model.index}
    }
    // IDs are scope-local. Compare a live lease's owner token in O(1), never
    // infer ownership from numeric IDs or a graph allocation's reusable address.
    fn owns_input(&self,input:&ActionControl)->bool{input.read_node(|node|Rc::ptr_eq(&self.owner,&node.owner)).unwrap_or(false)}
    fn acquire(&mut self,key:&ffi::ActionNodeKey)->Box<ActionControl>{
        if matches!(key.kind,11|12|14|15){return Box::new(ActionControl{storage:ActionStorage::Retired});}
        let node=action_node_key(key).and_then(|key|self.nodes.get(&key)).cloned();
        let Some(node)=node else{return Box::new(ActionControl{storage:ActionStorage::Retired});};
        let lease={let mut record=node.borrow_mut();let Some(lease)=record.lease.checked_add(1) else{return Box::new(ActionControl{storage:ActionStorage::Retired});};if let Some(model)=record.model.as_deref_mut(){model.detach_input();}record.lease=lease;record.attached=true;if !matches!(record.model.as_deref(),Some(NodeModel::Workspace(_))){record.state.action_enabled(true);}record.state.action_focus(false);record.state.action_cancel();lease};
        Box::new(ActionControl{storage:ActionStorage::Shared{node,lease}})
    }
}
impl Drop for ActionGraph {fn drop(&mut self){for node in self.nodes.values(){let mut record=node.borrow_mut();record.retire();}}}
enum ActionStorage { Standalone(ActionState), Shared{node:Rc<RefCell<ActionNodeRecord>>,lease:u64}, Retired }
struct ActionControl { storage:ActionStorage }
// A queued command holds no node or native adapter alive. Lease and cancellation
// epoch distinguish a new attachment/context even if numeric IDs are unchanged.
struct FormCommandTicket { source:Option<(std::rc::Weak<RefCell<ActionNodeRecord>>,u64,u64)> }
fn empty_form_command()->ffi::FormCommandBinding{ffi::FormCommandBinding{command:ffi::FormCommand::None,panel:ffi::FormPanel::None,key:String::new(),index:0}}
fn record_form_command(record:&ActionNodeRecord)->ffi::FormCommandBinding{
    if record.alive&&record.attached&&record.state.enabled&&record.state.admission_epoch!=0{
        if let Some(NodeModel::FormCommand(model))=record.model.as_deref(){return ffi::FormCommandBinding{command:model.command,panel:form_command_panel(model.command),key:model.key.clone(),index:model.index};}
    }
    empty_form_command()
}
impl FormCommandTicket {
    fn take_command(&mut self)->ffi::FormCommandBinding{
        let Some((source,lease,epoch))=self.source.take() else{return empty_form_command();};
        let Some(node)=source.upgrade() else{return empty_form_command();};let mut record=node.borrow_mut();
        if record.lease!=lease||record.state.admission_epoch!=epoch{return empty_form_command();}
        let command=record_form_command(&record);
        // Consume sibling tickets issued in the same context as well as this Box.
        if command.command!=ffi::FormCommand::None{record.state.action_cancel();}
        command
    }
}

impl ActionControl {
    fn read<R>(&self,f:impl FnOnce(&ActionState)->R)->Option<R>{match &self.storage{ActionStorage::Standalone(state)=>Some(f(state)),ActionStorage::Shared{node,lease}=>{let record=node.borrow();(record.alive&&record.attached&&record.lease==*lease).then(||f(&record.state))},ActionStorage::Retired=>None}}
    fn write<R>(&mut self,f:impl FnOnce(&mut ActionState)->R)->Option<R>{match &mut self.storage{ActionStorage::Standalone(state)=>Some(f(state)),ActionStorage::Shared{node,lease}=>{let mut record=node.borrow_mut();if !record.alive||!record.attached||record.lease!=*lease{return None;}Some(f(&mut record.state))},ActionStorage::Retired=>None}}
    fn read_node<R>(&self,f:impl FnOnce(&ActionNodeRecord)->R)->Option<R>{
        let ActionStorage::Shared{node,lease}=&self.storage else{return None;};let record=node.borrow();
        (record.alive&&record.attached&&record.lease==*lease).then(||f(&record))
    }
    fn write_node<R>(&mut self,f:impl FnOnce(&mut ActionNodeRecord)->R)->Option<R>{
        let ActionStorage::Shared{node,lease}=&self.storage else{return None;};let mut record=node.borrow_mut();
        if !record.alive||!record.attached||record.lease!=*lease{return None;}Some(f(&mut record))
    }
    fn node_select_init(&mut self,options:Vec<ffi::SelectOption>)->bool{self.write_node(|r|match r.model.as_deref(){Some(NodeModel::Select(_))=>true,Some(_)=>false,None=>{let mut state=initial_select_state();if !state.replace_options(options,false,false){return false;}r.model=Some(Box::new(NodeModel::Select(state)));true}}).unwrap_or(false)}
    fn node_select_options(&self)->Vec<ffi::SelectOption>{self.read_node(|r|if let Some(NodeModel::Select(s))=r.model.as_deref(){s.options.iter().map(|o|ffi::SelectOption{key:o.key.clone(),label:o.label.clone(),search:o.search.clone()}).collect()}else{vec![]}).unwrap_or_default()}
    fn node_select_selected(&self)->usize{self.read_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref(){s.selected()}else{usize::MAX}}).unwrap_or_else(||usize::MAX)}
    fn node_select_has_selection(&self)->bool{self.read_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref(){s.has_selection()}else{false}}).unwrap_or_else(||false)}
    fn node_select_selected_key(&self)->String{self.read_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref(){s.selected_key().to_owned()}else{String::new()}}).unwrap_or_else(||String::new())}
    fn node_select_option_key(&self,index: usize)->String{self.read_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref(){s.option_key(index).to_owned()}else{String::new()}}).unwrap_or_else(||String::new())}
    fn node_select_replace_options(&mut self,options: Vec<ffi::SelectOption>, fallback_last: bool, select_last: bool)->bool{self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.replace_options(options,fallback_last,select_last)}else{false}}).unwrap_or_else(||false)}
    fn node_select_can_choose(&self,token: u64, key: &str, enabled: bool)->bool{self.read_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref(){s.can_choose(token,key,enabled&&gate)}else{false}}).unwrap_or_else(||false)}
    fn node_select_choose_key(&mut self,token: u64, key: &str, enabled: bool)->bool{self.write_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.choose_key(token,key,enabled&&gate)}else{false}}).unwrap_or_else(||false)}
    fn node_select_close_menu_at(&mut self,token: u64, now_us: u64)->bool{self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.close_menu_at(token,now_us)}else{false}}).unwrap_or_else(||false)}
    fn node_select_dismiss_popup(&mut self,now_us: u64, popup_present: bool)->(){self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.dismiss_popup(now_us,popup_present)}else{()}}).unwrap_or_else(||())}
    fn node_select_open_pointer(&mut self,enabled: bool, now_us: u64, guard_us: u64)->u64{self.write_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.open_pointer(enabled&&gate,now_us,guard_us)}else{0}}).unwrap_or_else(||0)}
    fn node_select_select_index(&mut self,index: usize)->bool{self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.select_index(index)}else{false}}).unwrap_or_else(||false)}
    fn node_select_clear_selection(&mut self)->(){self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.clear_selection()}else{()}}).unwrap_or_else(||())}
    fn node_select_open(&mut self,enabled: bool)->u64{self.write_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.open(enabled&&gate)}else{0}}).unwrap_or_else(||0)}
    fn node_select_expanded(&self)->bool{self.read_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref(){s.expanded()}else{false}}).unwrap_or_else(||false)}
    fn node_select_choose(&mut self,token: u64, index: usize, enabled: bool)->bool{self.write_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.choose(token,index,enabled&&gate)}else{false}}).unwrap_or_else(||false)}
    fn node_select_reset_prefix(&mut self)->(){self.write_node(|r|{if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.reset_prefix()}else{()}}).unwrap_or_else(||())}
    fn node_select_input(&mut self,lower_text: &str, now_ms: u64, enabled: bool)->bool{self.write_node(|r|{let gate=r.state.enabled;if let Some(NodeModel::Select(s))=r.model.as_deref_mut(){s.input(lower_text,now_ms,enabled&&gate)}else{false}}).unwrap_or_else(||false)}
    fn node_parent_id(&self)->u64{self.read_node(|node|node.parent).unwrap_or(0)}
    fn pin_binding(&self)->ffi::SidebarPinBinding{self.read_node(|node|if let Some(NodeModel::SidebarPin(pin))=node.model.as_deref(){Some(ffi::SidebarPinBinding{valid:true,id:node.id,key:pin.key.clone(),folder:pin.presentation.folder,collapsed:pin.collapsed,group:pin.group})}else{None}).flatten().unwrap_or(ffi::SidebarPinBinding{valid:false,id:0,key:String::new(),folder:false,collapsed:false,group:0})}
    fn sidebar_visual(&self)->ffi::SidebarRowVisual{self.read_node(|r|r.model.as_deref().and_then(NodeModel::row_presentation).map(|p|ffi::SidebarRowVisual{valid:true,tile:p.tile,height:p.height()})).flatten().unwrap_or(ffi::SidebarRowVisual{valid:false,tile:false,height:0})}
    fn sidebar_presentation(&self)->ffi::SidebarRowPresentation{self.read_node(|r|r.model.as_deref().and_then(NodeModel::row_presentation).map(|p|ffi::SidebarRowPresentation{valid:true,title:p.title.clone(),monogram:p.monogram.clone(),color:p.color,tile:p.tile,folder:p.folder,height:p.height()})).flatten().unwrap_or(ffi::SidebarRowPresentation{valid:false,title:vec![],monogram:vec![],color:0,tile:false,folder:false,height:0})}
    fn sidebar_title(&self)->Vec<u16>{self.read_node(|r|r.model.as_deref().and_then(NodeModel::row_presentation).map(|p|p.title.clone())).flatten().unwrap_or_default()}
    fn sidebar_set_title(&mut self,title:Vec<u16>)->bool{self.write_node(|r|{if let Some(NodeModel::SidebarPin(pin))=r.model.as_deref_mut(){let display=pin_title(&title,pin.presentation.folder,pin.collapsed);if pin.presentation.title==display{return false;}if pin.presentation.folder{pin.base_title=title;}pin.presentation.title_utf8=String::from_utf16_lossy(&display);pin.presentation.title=display;return true;}let Some(p)=r.model.as_deref_mut().and_then(NodeModel::row_presentation_mut) else{return false;};if p.title==title{return false;}p.title_utf8=String::from_utf16_lossy(&title);p.title=title;true}).unwrap_or(false)}
    fn sidebar_set_monogram(&mut self,monogram:Vec<u16>,color:u32)->bool{self.write_node(|r|{let Some(p)=r.model.as_deref_mut().and_then(NodeModel::row_presentation_mut) else{return false;};if p.monogram==monogram&&p.color==color{return false;}p.monogram_utf8=if p.folder{"📁".into()}else{String::from_utf16_lossy(&monogram)};p.monogram=monogram;p.color=color;true}).unwrap_or(false)}
    fn sidebar_scene(&self,bounds:ffi::FloatRect,clip:ffi::FloatRect,style:ffi::SidebarRowStyle,close:bool,close_visible:bool)->ffi::SidebarRowScene{
        let mut item=ffi::Item{x:bounds.x,y:bounds.y,width:bounds.width,height:bounds.height,kind:0,glyph:0,title:String::new(),monogram:String::new(),detail:String::new(),title_width:0.0,detail_ink:0,detail_font_size:0.0,ink:style.ink,fill:0,badge:0,edge:0,focus:style.focus,active:false,hovered:false,tile:false,close:false,close_visible:false,focused:false,font_size:style.font_size,font_height:0.0,font_weight:0,text_style:0,clip};
        let valid=self.read_node(|r|{let Some(p)=r.model.as_deref().and_then(NodeModel::row_presentation) else{return false;};let hovered=r.state.enabled&&matches!(r.state.visual,ffi::RowVisual::Hovered|ffi::RowVisual::Pressed);item.title=p.title_utf8.clone();item.monogram=p.monogram_utf8.clone();item.badge=if p.folder{0}else{p.color};item.close=close&&matches!(r.model.as_deref(),Some(NodeModel::SidebarTab(m)) if m.can_close);item.close_visible=item.close&&close_visible;item.active=r.state.on;item.hovered=hovered;if !r.state.on&&!hovered{item.ink=style.muted;}item.tile=p.tile;item.focused=r.state.focused;item.fill=if p.tile{(style.card&0xffffff)|((if r.state.on{0xff}else if hovered{0x8c}else{0x59})<<24)}else if r.state.on{style.card}else if hovered{style.hover}else{0};item.edge=if r.state.on&&!p.tile{style.edge}else{0};true}).unwrap_or(false);
        ffi::SidebarRowScene{valid,item}
    }
    fn tab_binding(&self)->ffi::SidebarTabBinding{self.read_node(|node|if let Some(NodeModel::SidebarTab(p))=node.model.as_deref(){Some(ffi::SidebarTabBinding{valid:true,id:node.id,high:p.high,low:p.low,index:p.index,can_close:p.can_close})}else{None}).flatten().unwrap_or(ffi::SidebarTabBinding{valid:false,id:0,high:0,low:0,index:0,can_close:false})}
    fn node_model_kind(&self)->u32{self.read_node(|r|match r.model.as_deref(){Some(NodeModel::SidebarPin(_))=>10,Some(NodeModel::SidebarTab(_))=>9,Some(NodeModel::FormCommand(_))=>8,Some(NodeModel::Checkbox(_))=>1,Some(NodeModel::Toggle(_))=>2,Some(NodeModel::Select(_))=>3,Some(NodeModel::Workspace(_))=>4,Some(NodeModel::Palette(_))=>6,Some(NodeModel::SiteRule(_))=>7,Some(NodeModel::SidebarTabs(_))|Some(NodeModel::SidebarPair(_))|Some(NodeModel::SidebarPinGroup(_))|Some(NodeModel::PaletteRoot(_))|Some(NodeModel::FormRoot(_))|None=>0}).unwrap_or(0)}
    fn node_checkbox_init(&mut self,checked:bool)->bool{self.write_node(|r|match r.model.as_deref(){Some(NodeModel::Checkbox(_))=>true,Some(_)=>false,None=>{r.model=Some(Box::new(NodeModel::Checkbox(CheckboxState{checked})));true}}).unwrap_or(false)}
    fn node_checked(&self)->bool{self.read_node(|r|matches!(r.model.as_deref(),Some(NodeModel::Checkbox(s)) if s.checked())).unwrap_or(false)}
    fn node_set_checked(&mut self,checked:bool)->bool{self.write_node(|r|if let Some(NodeModel::Checkbox(s))=r.model.as_deref_mut(){s.set_checked(checked)}else{false}).unwrap_or(false)}
    fn node_flip_checked(&mut self,enabled:bool)->bool{self.write_node(|r|if let Some(NodeModel::Checkbox(s))=r.model.as_deref_mut(){s.toggle(enabled&&r.state.enabled)}else{false}).unwrap_or(false)}
    fn node_toggle_init(&mut self)->bool{self.write_node(|r|match r.model.as_deref(){Some(NodeModel::Toggle(_))=>true,Some(_)=>false,None=>{r.model=Some(Box::new(NodeModel::Toggle(initial_toggle_state())));true}}).unwrap_or(false)}
    fn node_toggle_checked(&self)->bool{self.read_node(|r|matches!(r.model.as_deref(),Some(NodeModel::Toggle(s)) if s.toggle_checked())).unwrap_or(false)}
    fn node_toggle_set(&mut self,checked:bool)->bool{self.write_node(|r|if let Some(NodeModel::Toggle(s))=r.model.as_deref_mut(){s.toggle_set(checked);true}else{false}).unwrap_or(false)}
    fn node_toggle_flip(&mut self,enabled:bool,now:u64,animate:bool)->bool{self.write_node(|r|if let Some(NodeModel::Toggle(s))=r.model.as_deref_mut(){s.toggle_flip(enabled&&r.state.enabled,now,animate)}else{false}).unwrap_or(false)}
    fn node_toggle_hover(&mut self,hovered:bool,now:u64,animate:bool)->bool{self.write_node(|r|if let Some(NodeModel::Toggle(s))=r.model.as_deref_mut(){s.toggle_hover(hovered,now,animate);true}else{false}).unwrap_or(false)}
    fn node_toggle_finish(&mut self)->bool{self.write_node(|r|if let Some(NodeModel::Toggle(s))=r.model.as_deref_mut(){s.toggle_finish();true}else{false}).unwrap_or(false)}
    fn node_toggle_view(&self,now:u64)->ffi::NodeToggleView{self.read_node(|r|if let Some(NodeModel::Toggle(s))=r.model.as_deref(){Some(ffi::NodeToggleView{checked:s.toggle_checked(),progress:s.toggle_progress(now),hover:s.toggle_hover_progress(now),animating:s.toggle_animating(now)})}else{None}).flatten().unwrap_or(ffi::NodeToggleView{checked:false,progress:0.0,hover:0.0,animating:false})}
    fn retired_input()->ffi::RowInputResult{ffi::RowInputResult{visual:ffi::RowVisual::Disabled,action:ffi::RowAction::None,handled:false}}
    fn action_node_id(&self)->u64{match &self.storage{ActionStorage::Shared{node,lease}=>{let record=node.borrow();if record.alive&&record.attached&&record.lease==*lease{record.id}else{0}},_=>0}}
    // Semantic commands belong to private form members, never display labels.
    // Rebinding to another command requires a new node; stale adapters fail shut.
    fn init_form_command(&mut self,command:ffi::FormCommand,key:&str,index:usize)->bool{
        self.write_node(|r|{if r.kind!=9{return false;}match r.model.as_deref(){Some(NodeModel::FormCommand(m))=>m.command==command&&m.key==key&&m.index==index,Some(_)=>false,None=>{r.model=Some(Box::new(NodeModel::FormCommand(FormCommandModel{command,key:key.into(),index})));true}}}).unwrap_or(false)
    }
    fn bind_appearance_preset(&mut self,emoji:bool,index:usize)->bool{
        let command=if emoji{ffi::FormCommand::WorkspaceEmojiPreset}else{ffi::FormCommand::WorkspaceColorPreset};
        appearance_command_target(command,index).valid&&self.init_form_command(command,"",index)
    }
    fn node_form_command_init(&mut self,command:ffi::FormCommand,key:&str)->bool{valid_form_command(command,key)&&self.init_form_command(command,key,0)}
    fn node_form_command(&self)->ffi::FormCommandBinding{self.read_node(record_form_command).unwrap_or_else(empty_form_command)}
    fn defer_form_command(&self)->Box<FormCommandTicket>{
        let source=match &self.storage{
            ActionStorage::Shared{node,lease} if self.node_form_command().command!=ffi::FormCommand::None=>Some((Rc::downgrade(node),*lease,self.action_admission_epoch())),
            _=>None,
        };
        Box::new(FormCommandTicket{source})
    }

    fn node_form_view(&self)->ffi::FormCommandView{
        self.read_node(|r|if let Some(NodeModel::FormCommand(m))=r.model.as_deref(){Some(ffi::FormCommandView{bound:true,dismiss:matches!(m.command,ffi::FormCommand::WorkspaceDismiss|ffi::FormCommand::PreferencesDismiss|ffi::FormCommand::ImportDismiss|ffi::FormCommand::ToolsDismiss),index:m.index})}else{None}).flatten().unwrap_or(ffi::FormCommandView{bound:false,dismiss:false,index:0})
    }
    fn action_admission_epoch(&self)->u64{self.read(|s|s.admission_epoch).unwrap_or(0)}
    fn action_admits(&self,epoch:u64)->bool{epoch!=0&&self.read(|s|s.enabled&&s.admission_epoch==epoch).unwrap_or(false)}
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
    fn action_row_dragging(&self)->bool{self.read(ActionState::action_row_dragging).unwrap_or(false)}
    fn action_row_pointer(&mut self,phase:ffi::PointerPhase,x:i32,y:i32,inside:bool,trigger:bool,visible:bool,drop:bool)->ffi::RowInputResult{self.write(|s|s.action_row_pointer(phase,x,y,inside,trigger,visible,drop)).unwrap_or_else(Self::retired_input)}
    fn action_key(&mut self,pressed:bool,key:u32,mac:bool,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_key(pressed,key,mac,visible)).unwrap_or_else(Self::retired_input)}
    fn action_gesture(&mut self,kind:u32,visible:bool)->ffi::RowInputResult{self.write(|s|s.action_gesture(kind,visible)).unwrap_or_else(Self::retired_input)}
}
impl Drop for ActionControl {fn drop(&mut self){if let ActionStorage::Shared{node,lease}=&self.storage{let mut record=node.borrow_mut();if record.alive&&record.lease==*lease{record.attached=false;record.state.action_focus(false);record.state.action_cancel();if let Some(model)=record.model.as_deref_mut(){model.detach_input();}}}}}

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

// The design system shows a tab's close button only on hover, active or not.
fn close_visible(_active: bool, _tile: bool, hovered: bool) -> bool {
    hovered
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
    // The space rail is centred in the footer, short of the 28 px „+”
    // IconButton that ends it, centred in the 32 px row.
    const BUTTON: i32 = 28; const GAP: i32 = 4;
    let width=width.max(0); let middle=(width-BUTTON-GAP).max(0);
    let overflow=content_width>middle;
    let height=if overflow {47} else {32};
    let strip_width=if overflow {middle} else {content_width.max(0)};
    let strip_x=((width-strip_width)/2).min(middle-strip_width).max(0);
    let button_y=(32-BUTTON)/2;
    ffi::ColumnLayout {width,height,children:vec![
        ffi::Rect{x:strip_x,y:0,width:strip_width,height},
        ffi::Rect{x:middle,y:0,width:1,height:1},
        ffi::Rect{x:(width-BUTTON).max(0),y:button_y,width:BUTTON.min(width),height:BUTTON},
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
    let mut rect=|x:f32,y:f32,w:f32,h:f32,r:f32,color:u32,stroke:f32|{if w>0.0&&h>0.0{ops.push(ffi::Primitive{x,y,width:w,height:h,radius:r,color,stroke,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,right_align:false,clip:ffi::FloatRect{x:i.clip.x,y:i.clip.y,width:i.clip.width,height:i.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0});}};
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
    if i.kind==22&&!i.title.is_empty(){let x=if i.tile{i.x+10.0}else{i.x+36.0};let w=(i.width-46.0).max(0.0);if w>0.0{ops.push(ffi::Primitive{x,y:i.y,width:w,height:i.height,radius:0.0,color:i.ink,stroke:0.0,shape:0,text:i.title.clone(),font_size:i.font_size,font_weight:0,bold:i.active,middle:false,center:false,right_align:false,clip:ffi::FloatRect{x:i.clip.x,y:i.clip.y,width:i.clip.width,height:i.clip.height},editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0});}}
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
        let start = out.len();
        layout_item(i, &mut out);
        focus_ring(i, start, &mut out);
        apply_text_style(i, start, &mut out);
    }
    out
}
// Keyboard focus as the design system draws it: a 2 px ring in `focus` inside
// the control's own shape, replacing the 2 px focus bar the recipes draw when
// no ring colour is given. Controls with their own focus outline keep it.
fn focus_ring(i: &ffi::Item, start: usize, out: &mut Vec<ffi::Primitive>) {
    if !i.focused || i.focus >> 24 == 0 || out.len() <= start { return; }
    let is_bar = |p: &ffi::Primitive| p.text.is_empty() && p.height == 2.0 && p.radius == 1.0 && p.color == i.ink && p.y >= i.y + i.height - 5.0;
    let Some(bar) = out[start..].iter().position(is_bar) else { return; };
    let template = out.remove(start + bar);
    let radius = out[start..].iter().find(|p| p.text.is_empty() && p.x == i.x && p.y == i.y && p.width == i.width && p.height == i.height)
        .map_or(design_tokens::radius::SM, |p| p.radius);
    if i.width <= 2.0 || i.height <= 2.0 { return; }
    out.push(ffi::Primitive { x: i.x + 1.0, y: i.y + 1.0, width: i.width - 2.0, height: i.height - 2.0,
        radius: (radius - 1.0).max(0.0), color: i.focus, stroke: 2.0, ..template });
}
// A design system text style replaces the recipe's own font on every text the
// item draws: family, size, weight, italic, letter spacing and capitals.
fn apply_text_style(i: &ffi::Item, start: usize, out: &mut [ffi::Primitive]) {
    let Some((style, caps)) = design::text_style(i.text_style) else { return; };
    for p in out[start..].iter_mut().filter(|p| !p.text.is_empty() && !p.editor) {
        p.family = design::family_id(style.family);
        p.font_size = style.size;
        p.font_weight = i32::from(style.weight);
        p.bold = style.weight >= 600;
        p.italic = style.italic;
        p.tracking = style.letter_spacing;
        if caps { p.text = p.text.to_uppercase(); }
    }
}
// One text primitive in a design system style, leaving the item's other texts.
fn style_text(p: &mut ffi::Primitive, id: u32) {
    let Some((style, caps)) = design::text_style(id) else { return; };
    p.family = design::family_id(style.family);
    p.font_size = style.size;
    p.font_weight = i32::from(style.weight);
    p.bold = style.weight >= 600;
    p.italic = style.italic;
    p.tracking = style.letter_spacing;
    if caps { p.text = p.text.to_uppercase(); }
}
// A monogram tile paints the stored workspace color as its palette `accent`
// with `on-accent` text, in the scheme the row's ink belongs to (light ink: Noc).
fn monogram_colors(seed: u32, ink: u32) -> (u32, u32) {
    if seed >> 24 == 0 { return (0, ink); }
    let dark = design::contrast(ink, 0xff000000) > design::contrast(ink, 0xffffffff);
    let p = design_palette(dark, seed | 0xff000000);
    (p.accent, p.on_accent)
}
fn design_button(dark:bool,workspace_color:u32,kind:u32,state:u32)->ffi::DesignButton{
    use design::recipe::{ButtonKind,Interaction};
    let p=design::palette_for_stored(if dark {design::Scheme::Noc} else {design::Scheme::Papier},workspace_color);
    let kind=match kind{1=>ButtonKind::Zing,2=>ButtonKind::Outline,3=>ButtonKind::Quiet,4=>ButtonKind::Danger,5=>ButtonKind::DangerStrong,_=>ButtonKind::Primary};
    let state=match state{1=>Interaction::Hover,2=>Interaction::Pressed,3=>Interaction::Disabled,_=>Interaction::Rest};
    let b=design::recipe::button(&p,kind,state);
    ffi::DesignButton{height:b.height,radius:b.radius,padding_x:b.padding_x,fill:b.fill,ink:b.ink,edge:b.edge,edge_width:b.edge_width}
}
fn design_toggle(dark:bool,workspace_color:u32,on:bool)->ffi::DesignToggle{
    let p=design::palette_for_stored(if dark {design::Scheme::Noc} else {design::Scheme::Papier},workspace_color);
    let t=design::recipe::toggle(&p,on);
    ffi::DesignToggle{width:t.width,height:t.height,thumb:t.thumb,inset:t.inset,track:t.track,edge:t.edge,edge_width:t.edge_width,thumb_fill:t.thumb_fill}
}
fn design_text_style(id:u32)->ffi::DesignTextStyle{
    match design::text_style(id){
        Some((s,caps))=>ffi::DesignTextStyle{family:design::family_id(s.family),size:s.size,line_height:s.line_height,weight:i32::from(s.weight),italic:s.italic,tracking:s.letter_spacing,caps},
        None=>ffi::DesignTextStyle{family:0,size:0.0,line_height:0.0,weight:0,italic:false,tracking:0.0,caps:false},
    }
}
fn design_font_family(family:u32)->String{design::family_name(family).unwrap_or_default().into()}
fn design_metrics()->ffi::DesignMetrics{
    use design_tokens::{radius,space};let px=|v:f32|v.round() as i32;
    ffi::DesignMetrics{space_0_5:px(space::S0_5),space_1:px(space::S1),space_2:px(space::S2),space_3:px(space::S3),space_4:px(space::S4),space_6:px(space::S6),space_8:px(space::S8),space_12:px(space::S12),radius_xs:px(radius::XS),radius_sm:px(radius::SM),radius_md:px(radius::MD),radius_lg:px(radius::LG)}
}
fn layout_item(i: &ffi::Item, out: &mut Vec<ffi::Primitive>) {
    if ![i.x, i.y, i.width, i.height].iter().all(|v| v.is_finite())
        || i.width <= 0.0 || i.height <= 0.0
        || ![i.clip.x, i.clip.y, i.clip.width, i.clip.height].iter().all(|v| v.is_finite())
        || i.clip.width <= 0.0 || i.clip.height <= 0.0 { return; }
    if i.kind==22||i.kind==23{out.extend(preference_control_scene(i));return;}
    if i.kind==28||i.kind==29{color_wheel_scene(i,out);return;}
    let first_primitive = out.len();
    let mut emit = |x, y, w: f32, h: f32, radius, color, text: &str, size, bold| {
        if w > 0.0 && h > 0.0 {
            out.push(ffi::Primitive { x, y, width: w, height: h, radius,
                color, stroke: 0.0, shape: 0, text: text.into(), font_size: size, font_weight:0, bold, middle: false, center: false,right_align:false, editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0,
                clip: ffi::FloatRect {x: i.clip.x, y: i.clip.y, width: i.clip.width, height: i.clip.height} });
        }
    };
    if i.kind==26 {
        if !i.title_width.is_finite()||!i.detail_font_size.is_finite(){return;}
        emit(i.x,i.y,i.width,i.height,0.0,i.fill,"",0.0,false);
        let mut slice=|left:f32,right:f32|{let left=(i.width*left).round();let right=(i.width*right.min(1.0)).round();let x=if i.tile{i.x+i.width-right}else{i.x+left};emit(x,i.y,(right-left).max(0.0),i.height,0.0,i.ink,"",0.0,false);};
        if i.title_width>=0.0{slice(0.0,i.title_width.min(1.0));}
        else{let t=i.detail_font_size.clamp(0.0,1.0);if t<0.5{slice(t/2.0,t*2.0);}else if t<0.75{slice(t*3.0-1.25,1.0);slice(0.0,t-0.5);}else{let left=(t-0.75)*4.0;slice(left,left+0.25-(t-0.75));}}
        return;
    }
    if i.kind == 30 {
        // Kbd: one key in mono 11/500 on `bg-hover`, `radius-xs`, with a 1 px
        // `line` bottom edge.
        emit(i.x,i.y,i.width,i.height,design_tokens::radius::XS,i.edge,"",0.0,false);
        emit(i.x,i.y,i.width,i.height-1.0,design_tokens::radius::XS,i.fill,"",0.0,false);
        emit(i.x,i.y,i.width,i.height-1.0,0.0,i.ink,&i.title,11.0,false);
        for p in out[first_primitive..].iter_mut(){if !p.text.is_empty(){p.center=true;p.family=design::family_id(design_tokens::Family::Mono);p.font_weight=500;}}
        return;
    }
    if i.kind == 27 {
        if !i.title.is_empty(){emit(i.x,i.y,i.width,i.height,0.0,i.ink,&i.title,i.font_size,i.active);let p=out.last_mut().unwrap();p.font_weight=i.font_weight;p.middle=true;p.right_align=i.tile;}return;
    }
    if i.kind == 17 {
        if !i.title.is_empty(){emit(i.x,i.y,i.width,i.height,0.0,i.ink,&i.title,i.font_size,i.active);let last=out.last_mut().unwrap();last.font_weight=i.font_weight;last.center=i.glyph==1;}return;
    }
    if i.kind==24 {
        emit(i.x,i.y,i.width,i.height,12.0,i.fill,"",0.0,false);
        if !i.active&&i.width>1.0&&i.height>1.0 {emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,12.0,i.ink,"",0.0,false);out.last_mut().unwrap().stroke=1.0;}
        return;
    }
    if i.kind == 16 {
        let radius=design_tokens::radius::LG;
        emit(i.x,i.y,i.width,i.height,radius,i.fill,"",0.0,false);
        emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,radius-0.5,i.badge,"",0.0,false);
        if let Some(border)=out.get_mut(first_primitive+1){border.stroke=1.0;}return;
    }
    if i.kind == 15 {
        if i.glyph==0{emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,4.5,i.badge,"",0.0,false);}
        let g=palette_text_layout(i.glyph,i.width as i32,i.height as i32,i.title_width as i32,i.font_height as i32);
        let r=&g.children[0];
        if !i.title.is_empty(){emit(i.x+r.x as f32,i.y+r.y as f32,r.width as f32,r.height as f32,0.0,i.ink,&i.title,i.font_size,false);}
        if i.glyph==0{if let Some(border)=out.get_mut(first_primitive){border.stroke=1.0;}}return;
    }
    if i.kind == 14 {emit(i.x,i.y,i.width,i.height,0.0,i.fill,"",0.0,false);return;}
    if i.kind == 13 {
        let start=first_primitive;
        emit(i.x+12.0,i.y+(i.height-16.0)/2.0,16.0,16.0,0.0,i.ink,"",0.0,false);
        for p in out[start..].iter_mut(){p.shape=if i.glyph==1{3}else if i.glyph==2{6}else{7};}return;
    }
    if i.kind == 12 {
        // Command Center row: icon, name in `body`, detail in `url` when it is an
        // address, else `label`. A row without a chip colour has no icon tile.
        let chip=i.badge>>24!=0;
        emit(i.x,i.y,i.width,i.height,design_tokens::radius::SM,i.fill,"",0.0,false);
        if chip {emit(i.x+12.0,i.y+(i.height-28.0)/2.0,28.0,28.0,7.0,i.badge,"",0.0,false);}
        let g=row_layout(i.width as i32,i.height as i32,i.title_width as i32,!i.detail.is_empty(),chip,false);
        if i.focused{emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
        if !i.title.is_empty(){emit(i.x+g.title.x as f32,i.y,(g.title.width as f32).min(i.title_width.max(0.0)),i.height,0.0,i.ink,&i.title,i.font_size,true);}
        if !i.detail.is_empty(){emit(i.x+g.detail.x as f32,i.y,g.detail.width as f32,i.height,0.0,i.detail_ink,&i.detail,i.detail_font_size,false);}
        let url=!i.detail.contains(' ');
        for p in out[first_primitive..].iter_mut(){
            if p.text.is_empty(){continue;}
            if !p.bold{p.middle=true;if !chip{style_text(p,if url {design::TextStyleId::Url as u32} else {design::TextStyleId::Label as u32});}}
            else if !chip{style_text(p,design::TextStyleId::Body as u32);}
        }
        return;
    }
    if i.kind == 11 {
        if i.active||i.hovered {emit(i.x+i.width-3.0,i.y+8.0,2.0,(i.height-16.0).max(0.0),1.0,i.ink,"",0.0,false);}return;
    }
    if i.kind == 10 {
        emit(i.x,i.y,i.width,i.height,design_tokens::radius::MD,i.fill,"",0.0,false);return;
    }
    if i.kind == 8 {
        let g=pane_bar_layout(i.width as i32,i.height as i32);
        if !i.title.is_empty() {emit(i.x+g.title.x as f32,i.y,(g.title.width as f32).min(i.title_width.max(0.0)),i.height,0.0,i.ink,&i.title,i.font_size,false);}return;
    }
    if i.kind == 9 {
        // 3 px, growing to 5 px and a longer stroke on hover.
        let across=i.width>i.height;let extent=if across{i.width}else{i.height};
        let (thick,grip)=if i.hovered {(5.0f32,56.0f32.min(extent/3.0))} else {(3.0,40.0f32.min(extent/4.0))};
        emit(i.x+(i.width-if across{grip}else{thick})/2.0,i.y+(i.height-if across{thick}else{grip})/2.0,
            if across{grip}else{thick},if across{thick}else{grip},thick/2.0,i.ink,"",0.0,false);return;
    }
    if i.kind == 21 {
        if !(i.width>0.0&&i.height>0.0){return;}
        emit(i.x,i.y,i.width,i.height,6.0,i.fill,"",0.0,false);
        if i.hovered {emit(i.x,i.y,i.width,i.height,6.0,i.detail_ink,"",0.0,false);}
        let g=select_layout(i.width as i32,i.height as i32,0,0,i.tile);
        if !i.title.is_empty()&&g.label.width>0 {emit(i.x+g.label.x as f32,i.y,g.label.width as f32,i.height,0.0,i.ink,&i.title,i.font_size,i.active);}
        if g.arrow.width>0 {emit(i.x+g.arrow.x as f32,i.y,g.arrow.width as f32,i.height,0.0,i.ink,"▾",i.font_size,false);}
        emit(i.x,i.y,i.width,i.height,6.0,i.badge,"",0.0,false);if let Some(p)=out.last_mut(){p.stroke=if i.focused{2.0}else{1.0};}for p in out[first_primitive..].iter_mut(){if p.text=="▾"{p.center=true;}}
        return;
    }
    if i.kind == 20 {
        if !(i.width>0.0&&i.height>0.0){return;}
        if i.hovered {emit(i.x-3.0,i.y-3.0,i.width+6.0,i.height+6.0,12.0,(i.badge&0xffffff)|0x14000000,"",0.0,false);}
        if i.active {emit(i.x,i.y,i.width,i.height,2.0,i.fill,"",0.0,false);emit(i.x,i.y,i.width,i.height,0.0,i.ink,"✓",i.font_size,false);}
        else {emit(i.x,i.y,i.width,i.height,2.0,i.badge,"",0.0,false);}
        if i.focused {emit(i.x-2.0,i.y-2.0,i.width+4.0,i.height+4.0,4.0,i.detail_ink,"",0.0,false);}
        for p in out[first_primitive..].iter_mut() {
            if !p.text.is_empty(){p.center=true;}
            else if !i.active&&p.x==i.x&&p.y==i.y&&p.width==i.width&&p.height==i.height {p.stroke=1.5;}
            else if i.focused&&p.x==i.x-2.0&&p.y==i.y-2.0 {p.stroke=2.0;}
        }
        return;
    }
    if i.kind == 19 {
        if !(i.width>0.0&&i.height>0.0){return;}
        // A design system button passes its radius in font_height and its edge
        // width in detail_font_size; without them it is a pill with a 1 px edge.
        let pill=i.width.min(i.height).max(0.0)/2.0;
        let radius=if i.font_height>0.0 {i.font_height.min(pill)} else {pill};
        let edge_width=if i.detail_font_size>0.0 {i.detail_font_size} else {1.0};
        if i.fill>>24!=0 {emit(i.x,i.y,i.width,i.height,radius,i.fill,"",0.0,false);}
        if i.hovered&&i.detail_ink>>24!=0 {emit(i.x,i.y,i.width,i.height,radius,i.detail_ink,"",0.0,false);}
        let padding=i.title_width.max(0.0).min(i.width.max(0.0)/2.0);
        if !i.title.is_empty(){emit(i.x+padding,i.y,(i.width-padding*2.0).max(0.0),i.height,0.0,i.ink,&i.title,i.font_size,i.active);}
        if i.focused {emit(i.x+8.0,i.y+i.height-5.0,(i.width-16.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
        if i.badge>>24!=0 {let inset=edge_width/2.0;emit(i.x+inset,i.y+inset,i.width-edge_width,i.height-edge_width,(radius-inset).max(0.0),i.badge,"",0.0,false);}
        if i.badge>>24!=0 {if let Some(border)=out.last_mut(){border.stroke=edge_width;}}
        for p in out[first_primitive..].iter_mut(){if !p.text.is_empty(){p.center=true;}}
        return;
    }
    if i.kind == 18 {
        // A labelled swatch wider than tall is a pill: fill, 1 px edge, color dot, name.
        if !i.title.is_empty() && i.width > i.height * 1.6 {
            let radius=i.height/2.0;let cy=i.y+i.height/2.0;
            if i.fill>>24!=0{emit(i.x,i.y,i.width,i.height,radius,i.fill,"",0.0,false);}
            if i.edge>>24!=0{emit(i.x+0.5,i.y+0.5,i.width-1.0,i.height-1.0,radius-0.5,i.edge,"",0.0,false);}
            emit(i.x+6.0,cy-9.0,18.0,18.0,9.0,i.badge,"",0.0,false);
            emit(i.x+30.0,i.y,(i.width-36.0).max(0.0),i.height,0.0,i.ink,&i.title,i.font_size,i.active);
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut(){if p.color==i.edge&&p.x==i.x+0.5&&p.text.is_empty(){p.stroke=1.0;}}
            return;
        }
        // 14 px dot and 20 px ring in a 32 px cell; both shrink in narrow picker cells.
        let cx=i.x+i.width/2.0;let cy=i.y+i.height/2.0;let dot=(i.width.min(i.height)-6.0).clamp(6.0,14.0);let ring=dot+6.0;
        if i.active||i.hovered {emit(cx-ring/2.0,cy-ring/2.0,ring,ring,ring/2.0,i.ink,"",0.0,false);}
        emit(cx-dot/2.0,cy-dot/2.0,dot,dot,dot/2.0,i.badge,"",0.0,false);
        if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
        for p in out[first_primitive..].iter_mut() {if p.color==i.ink&&p.width==ring&&p.height==ring {p.stroke=1.0;}}
        return;
    }
    if i.kind == 7 {
        let cx=i.x+i.width/2.0; let cy=i.y+i.height/2.0;
        if i.glyph==1 && i.active && (!i.title.is_empty() || i.edge>>24!=0) {
            // The active workspace in the rail (pickers carry no edge): a raised
            // pill with its 10 px dot ringed in its own accent (or its emoji) and
            // the name in `label` 600. Without a name it closes into a 22 px disc.
            let h=22.0f32.min(i.height);let top=cy-h/2.0;
            let (x,w)=if i.title.is_empty() {(cx-h/2.0,h)} else {(i.x,i.width)};
            if i.fill>>24!=0 {emit(x,top,w,h,h/2.0,i.fill,"",0.0,false);}
            if i.edge>>24!=0 {emit(x+0.5,top+0.5,w-1.0,h-1.0,h/2.0-0.5,i.edge,"",0.0,false);}
            let lead=if i.title.is_empty() {(h-14.0)/2.0} else {6.0};
            let name_x=if i.monogram.is_empty() {
                emit(x+lead,cy-7.0,14.0,14.0,7.0,i.badge,"",0.0,false);
                emit(x+lead+2.0,cy-5.0,10.0,10.0,5.0,i.badge,"",0.0,false);
                x+26.0
            } else if i.title.is_empty() {
                emit(x,top,w,h,0.0,i.ink,&i.monogram,i.font_size.min(14.0),false);
                x+w
            } else {
                emit(x+4.0,top,20.0,h,0.0,i.ink,&i.monogram,i.font_size.min(14.0),false);
                x+28.0
            };
            if !i.title.is_empty() {emit(name_x,top,(x+w-name_x-10.0).max(0.0),h,0.0,i.detail_ink,&i.title,12.0,true);}
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut() {
                if !p.text.is_empty() {
                    if p.text==i.title && p.x==name_x {style_text(p,design::TextStyleId::Label as u32);p.font_weight=600;p.bold=true;} else {p.center=true;}
                    continue;
                }
                if (p.color==i.edge && p.x==x+0.5) || (p.width==14.0 && p.color==i.badge) {p.stroke=if p.width==14.0 {1.5} else {1.0};}
            }
            return;
        }
        if i.glyph==1 {
            if i.monogram.is_empty() {
                // An inactive workspace: its accent as a filled 8 px dot.
                if i.fill>>24!=0 {emit(cx-10.0,cy-10.0,20.0,20.0,10.0,i.fill,"",0.0,false);}
                emit(cx-4.0,cy-4.0,8.0,8.0,4.0,if i.badge>>24!=0 {i.badge} else {i.ink},"",0.0,false);
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
            } else if i.glyph==9 {
                // „+”: two 1.5 px strokes, 12 px long.
                emit(cx-6.0,cy-0.75,12.0,1.5,0.75,i.ink,"",0.0,false);emit(cx-0.75,cy-6.0,1.5,12.0,0.75,i.ink,"",0.0,false);
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
        }
        return;
    }
    if i.kind == 6 {
        emit(i.x + if i.tile {2.0} else {4.0}, i.y + if i.tile {4.0} else {2.0},
            (i.width-if i.tile {4.0} else {8.0}).max(0.0), (i.height-if i.tile {8.0} else {4.0}).max(0.0),
            3.5, i.fill, "", 0.0, false);
        return;
    }
    if i.kind == 1 {
        emit(i.x, i.y, i.width, i.height, 0.0, i.ink, &i.title, i.font_size, false);
        return;
    }
    if i.kind == 5 {
        emit(i.x + 8.0, i.y, (i.width - 16.0).max(0.0), i.height,
            0.0, i.ink, &i.title, i.font_size, true);
        return;
    }
    if i.kind == 4 {
        // A filled rounded pill and inset outline. Ring and lock strokes
        // remain analytic GPU geometry; they need no icon/font dependency.
        let pill = i.height / 2.0;
        emit(i.x, i.y, i.width, i.height, pill, i.fill, "", 0.0, false);
        let g = address_layout(i.width as i32, i.height as i32, i.title_width as i32);
        let cy = i.y + i.height / 2.0;
        // Editing (glyph 1, ⌘L): a 2 px `accent` ring and ↵ at the end.
        let editing = i.glyph == 1;
        let inset = if editing {1.0} else {0.5};
        let rings = [(i.x + inset, if editing {2.0} else {1.0}), (i.x + if i.active {14.0} else {12.0}, 1.5)];
        emit(i.x + inset, i.y + inset, i.width - 2.0 * inset, i.height - 2.0 * inset,
            pill - inset, if i.edge >> 24 != 0 {i.edge} else {(i.ink & 0x00ffffff) | 0x0d000000}, "", 0.0, false);
        if editing {emit(i.x + i.width - 30.0, i.y, 18.0, i.height, 0.0, i.detail_ink, "↵", i.detail_font_size, false);}
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
        // Host and path are set in the `url` style, the host at weight 600.
        for p in out[first_primitive..].iter_mut() {
            if !p.text.is_empty() {
                style_text(p, design::TextStyleId::Url as u32);
                if p.text == i.title { p.font_weight = 600; p.bold = true; }
                if editing && p.text == "↵" { p.center = true; }
            }
            if p.text.is_empty() {
                if let Some((_, stroke)) = rings.iter().find(|(x, _)| *x == p.x) {
                    p.stroke = *stroke;
                }
            }
        }
        return;
    }
    let radius = if i.kind == 2 {design_tokens::radius::MD} else {design_tokens::radius::SM};
    if i.fill >> 24 != 0 {
        emit(i.x, i.y, i.width, i.height, radius, i.fill, "", 0.0, false);
    }
    // The active card's edge: a 1 px inset ring, stroked below with the others.
    if i.edge >> 24 != 0 && i.width > 1.0 && i.height > 1.0 {
        emit(i.x + 0.5, i.y + 0.5, i.width - 1.0, i.height - 1.0, radius - 0.5, i.edge, "", 0.0, false);
    }
    if i.kind == 2 { return; }
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
        return;
    }
    // Favicon tile at `space-3`: the workspace color's `accent` with `on-accent`.
    let (tile_fill, tile_ink) = monogram_colors(i.badge, i.ink);
    emit(i.x + 12.0, i.y + (i.height - 16.0) / 2.0, 16.0, 16.0,
         design_tokens::radius::XS, tile_fill, "", 0.0, false);
    if !i.monogram.is_empty() {
        emit(i.x + 12.0, i.y + (i.height - 16.0) / 2.0, 16.0, 16.0,
             0.0, if i.badge >> 24 != 0 { tile_ink } else { i.ink },
             &i.monogram, 9.0, true);
    }
    // Keep a close-button hit area reserved whenever it exists, just as the
    // native interaction node does (including half-width split cards).
    let geometry = row_layout(i.width as i32, i.height as i32, 0, false, false, i.close);
    // A folder's caret is its own glyph: in the title it would pull the whole
    // line into a fallback font, larger than a tab's `body`.
    let (caret, title) = match i.title.strip_prefix("▸ ").map(|t| ("▸", t)).or_else(|| i.title.strip_prefix("▾ ").map(|t| ("▾", t))) {
        Some((caret, title)) => (Some(caret), title),
        None => (None, i.title.as_str()),
    };
    let title_x = i.x + geometry.title.x as f32 + if caret.is_some() { 14.0 } else { 0.0 };
    if let Some(caret) = caret {
        emit(i.x + geometry.title.x as f32, i.y, 12.0, i.height, 0.0, i.ink, caret, 10.0, false);
    }
    emit(title_x, i.y, (geometry.title.width as f32 - (title_x - i.x - geometry.title.x as f32)).max(0.0), i.height,
         0.0, i.ink, title, i.font_size, i.active);
    if i.close && i.close_visible {
        emit(i.x + geometry.close.x as f32 + 5.0, i.y,
             (geometry.close.width - 10).max(0) as f32, i.height,
             0.0, i.ink, "×", 18.0, false);
    }
    if i.edge >> 24 != 0 {
        for p in out[first_primitive..].iter_mut() {
            if p.text.is_empty() && p.color == i.edge && p.x == i.x + 0.5 && p.y == i.y + 0.5 {p.stroke = 1.0;}
        }
    }
    // Monogram in sans 9/700 centered on its tile; titles in `body`, the active
    // tab in `body-strong`.
    for p in out[first_primitive..].iter_mut() {
        if p.text.is_empty() || p.text == "×" { continue; }
        if p.width == 16.0 && p.x == i.x + 12.0 && p.text == i.monogram {
            p.center = true; p.family = design::family_id(design_tokens::Family::Sans); p.font_weight = 700;
        } else if p.text == title { style_text(p, if i.active {4} else {5}); }
        else if caret == Some(p.text.as_str()) { p.middle = true; }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn row(width: f32) -> ffi::Item { ffi::Item {
        x:0.0, y:0.0, width, height:34.0, kind:0, glyph:0,
        title:"Zażółć 日本語 🦀".into(), monogram:"Z".into(), detail:String::new(),
        title_width:0.0, detail_ink:0, detail_font_size:12.0, ink:0xff101010,
        fill:0xffffffff, badge:0xff556677, edge:0, focus:0, active:true, hovered:false,
        tile:false, close:true, close_visible:true, focused:false, font_size:13.0, font_height:16.0, font_weight:0, text_style:0,
        clip:ffi::FloatRect {x:0.0, y:0.0, width:width.max(0.0), height:34.0},
    }}
    fn child(width: i32, height: i32) -> ffi::ColumnChild {
        ffi::ColumnChild { width, height, top: 0, left: 0, bottom: 0, right: 0 }
    }
    #[test] fn peek_copy_and_title_keep_original_actions_and_utf16(){
        let c=peek_copy();assert_eq!((c.initial.as_str(),c.back.as_str(),c.forward.as_str(),c.promote.as_str(),c.split.as_str(),c.close.as_str()),("Peek","Wstecz","Dalej","Otwórz jako kartę","Split View","Zamknij podgląd (Escape)"));
        let title="日本語 🦀 אבג".encode_utf16().chain([0xd800]).collect::<Vec<_>>();let expected="Peek · ".encode_utf16().chain(title.iter().copied()).collect::<Vec<_>>();assert_eq!(peek_title(&title),expected);assert_eq!(peek_title(&[]),"Peek · ".encode_utf16().collect::<Vec<_>>());
    }
    #[test] fn peek_title_scene_preserves_middle_elision_font_clip_and_rtl(){
        let mut i=row(110.0);i.kind=27;i.font_weight=600;i.font_size=12.0;i.clip.x=3.0;i.clip.width=97.0;
        for rtl in [false,true]{i.tile=rtl;let p=layout(std::slice::from_ref(&i));assert_eq!(p.len(),1);assert_eq!(p[0].text,i.title);assert!(p[0].middle);assert_eq!(p[0].right_align,rtl);assert!(!p[0].center);assert_eq!((p[0].font_size,p[0].font_weight,p[0].clip.x,p[0].clip.width),(12.0,600,3.0,97.0));}
    }
    #[test] fn peek_title_scene_omits_empty_invalid_and_clipped_rectangles(){
        for (width,height,clip) in [(0.0,20.0,100.0),(-1.0,20.0,100.0),(100.0,0.0,100.0),(100.0,20.0,0.0),(f32::NAN,20.0,100.0)]{let mut i=row(width);i.kind=27;i.height=height;i.clip.width=clip;assert!(layout(&[i]).is_empty());}
        let mut i=row(100.0);i.kind=27;i.title.clear();assert!(layout(&[i]).is_empty());
    }
    #[test] fn ordinary_control_primitives_keep_existing_alignment_defaults(){
        for kind in [0,3,4,5,7,17,19,20,21,22,23,25,26,30]{let mut i=row(180.0);i.kind=kind;assert!(layout(&[i]).iter().all(|p|!p.right_align));}
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
    #[test] fn folder_caret_is_its_own_glyph_and_the_title_reads_like_a_tab() {
        let mut item=row(200.0);item.title="▸ Bezoom".into();let ops=layout(std::slice::from_ref(&item));
        let caret=ops.iter().find(|p|p.text=="▸").unwrap();let title=ops.iter().find(|p|p.text=="Bezoom").unwrap();
        assert!(caret.x<title.x);let mut tab=row(200.0);tab.title="Bezoom".into();let plain=layout(std::slice::from_ref(&tab));
        let same=plain.iter().find(|p|p.text=="Bezoom").unwrap();assert_eq!((title.family,title.font_size,title.font_weight),(same.family,same.font_size,same.font_weight));
    }
    #[test] fn footer_reserves_a_horizontal_scroll_gutter_only_for_overflow() {
        let normal=footer_layout(240,60); assert_eq!(normal.height,32);
        assert_eq!((normal.children[0].x,normal.children[0].width),(90,60));
        assert_eq!(footer_layout(240,200).children[0].x,8);
        assert_eq!((normal.children[2].x,normal.children[2].width),(212,28));
        let overflow=footer_layout(200,300); assert_eq!(overflow.height,47);
        assert_eq!(overflow.children[0].width,168); assert_eq!(overflow.children[0].height,47);
        assert_eq!((overflow.children[2].x,overflow.children[2].width),(172,28)); assert_eq!(overflow.children[2].y,2);
        let dots=strip_layout(-1,32,0,&[child(24,28),child(30,28),child(24,28)]);
        assert_eq!((dots.width,dots.children[1].x,dots.children[1].y),(78,24,2));
    }
    #[test] fn footer_scene_keeps_selected_plain_and_centered_emoji_variants() {
        let mut item=row(30.0);item.kind=7;item.glyph=1;item.monogram="🚀".into();item.title.clear();
        let ops=layout(&[item]);let text=ops.iter().find(|p|p.text=="🚀").unwrap();assert!(text.center);
        let mut plain=row(24.0);plain.kind=7;plain.glyph=1;plain.monogram.clear();plain.active=false;plain.fill=0;
        let badge=plain.badge;let ops=layout(&[plain]);assert_eq!(ops.len(),1);assert_eq!((ops[0].stroke,ops[0].color,ops[0].width),(0.0,badge,8.0));
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
    fn menu_state()->ffi::TabMenuState{ffi::TabMenuState{ready:true,busy:false,valid:true,tab:7,has_tab:true,split:true,members:vec![3,7],vertical:false,tab_count:3,http:true}}
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
    fn form_shortcut(id:&str,title:&str,chord:&str)->ffi::ToolsShortcutItem{ffi::ToolsShortcutItem{id:id.into(),title:title.encode_utf16().collect(),chord:chord.encode_utf16().collect()}}
    fn form_provider(id:&str,name:&str,default_provider:bool)->ffi::ToolsProviderItem{ffi::ToolsProviderItem{id:id.into(),name:name.encode_utf16().collect(),keyword:id.encode_utf16().collect(),url:format!("https://{}.test/?q={{query}}",id).encode_utf16().collect(),default_provider}}
    #[test] fn tools_shortcut_projection_and_selection_own_labels_chords_and_record_cancellation(){
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![form_shortcut("a","Same 🦀","Cmd+J"),form_shortcut("b","Same 🦀","")]);assert!(p.enabled);assert_eq!(String::from_utf16(&p.choices[0].label).unwrap(),"Same 🦀 — Cmd+J");assert_ne!(p.choices[0].key,p.choices[1].key);
        let first=f.tools_choice_draft(&p.choices[0].key);assert!(first.valid&&first.stop_recording);assert_eq!(first.target,"a");assert_eq!(String::from_utf16(&first.chord).unwrap(),"Cmd+J");
        f.tools_record(true);let draft=f.tools_choice_select(&p.choices[1].key);assert!(draft.valid);assert_eq!(draft.target,"b");assert!(draft.chord.is_empty());assert!(!f.tools_recording());assert!(!f.tools_choice_draft(&p.choices[0].key).valid);
    }
    #[test] fn tools_provider_projection_create_and_default_markers_preserve_native_fields(){
        let mut f=new_tools_form();f.tools_select(2);let p=f.tools_provider_choices(vec![form_provider("a","Same 🦀",false),form_provider("b","Same 🦀",true)]);assert!(p.enabled);assert_eq!(p.choices.len(),3);assert_eq!(String::from_utf16(&p.choices[0].label).unwrap(),tools_copy(2).choices[0]);assert_eq!(String::from_utf16(&p.choices[2].label).unwrap(),"b · Same 🦀 · domyślna");
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
    fn tools_context()->ffi::ToolsActionContext {ffi::ToolsActionContext{shortcut_field:true,provider_field:true,provider_selected:true,shortcut_composing:false,provider_composing:false,shortcut_selected:true}}
    #[test] fn tools_dispatch_preserves_command_and_composition_admission() {
        let mut form=new_tools_form();let mut c=tools_context();
        for action in [10,11,12,13,20,21,22] {let p=form.tools_dispatch(action,&c);assert!(p.admitted);assert_eq!(p.action,action);}
        // The snapshot form's actions (1–5) left with page 0.
        for action in [1,2,3,4,5] {assert_eq!(form.tools_dispatch(action,&c).action,0);}
        form.tools_record(true);c.shortcut_composing=true;c.provider_composing=true;
        for action in [11,12,20,21,22] {let p=form.tools_dispatch(action,&c);assert!(!p.admitted);assert!(!p.sync_recording);assert!(form.tools_recording());}
        // Record cancels preedit natively; reset remains admitted as in Views.
        for action in [10,13] {assert!(form.tools_dispatch(action,&c).admitted);}
        c=tools_context();c.shortcut_field=false;c.provider_field=false;c.shortcut_selected=false;
        for action in [10,11,12,20,21,22,-1,99] {assert_eq!(form.tools_dispatch(action,&c).action,0);}
    }
    #[test] fn tools_capture_owns_recording_transitions_and_status_copy() {
        let mut f=new_tools_form();f.tools_select(1);assert!(!f.tools_capture(ffi::ToolsCapture::Valid).admitted);
        f.tools_record(true);let invalid=f.tools_capture(ffi::ToolsCapture::Invalid);assert!(invalid.admitted&&!invalid.stop_recording&&!invalid.accept_chord);assert!(invalid.status.starts_with("Użyj Cmd"));assert!(f.tools_recording());
        let valid=f.tools_capture(ffi::ToolsCapture::Valid);assert!(valid.admitted&&valid.stop_recording&&valid.accept_chord);assert_eq!(valid.status,"Kombinacja przechwycona. Wybierz Zapisz skrót.");assert!(!f.tools_recording());assert!(!f.tools_capture(ffi::ToolsCapture::Escape).admitted);
        f.tools_record(true);let escape=f.tools_capture(ffi::ToolsCapture::Escape);assert!(escape.admitted&&escape.stop_recording&&!escape.accept_chord);assert_eq!(escape.status,"Nagrywanie anulowane.");assert!(!f.tools_recording());
        f.tools_record(true);f.tools_reset();assert!(!f.tools_capture(ffi::ToolsCapture::Valid).admitted);
        assert_eq!(tools_status(ffi::ToolsStatus::WriteFailed),"Nie udało się zapisać ustawień na dysku.");
    }
    fn import_report(stage:ffi::ImportStage)->ffi::ImportStatusReport{ffi::ImportStatusReport{stage,browser:"Arc".into(),error:String::new(),profiles:2,spaces:3,folders:4,tabs:5,skipped:0,ok:true,added_profiles:1,added_spaces:2,browsing_data:false,history:0,cookies:0,data_skipped:0,data_failed:false}}
    #[test] fn import_status_copy_formats_stages_counts_errors_and_browsing_data() {
        use ffi::ImportStage as S;
        assert_eq!(import_status_copy(&import_report(S::Reading)),"Odczytywanie danych z Arc…");
        assert_eq!(import_status_copy(&import_report(S::Preview)),"Do importu: 2 profili, 3 przestrzeni, 4 folderów i 5 kart lub zakładek.");
        let mut r=import_report(S::Preview);r.error="Nie znaleziono danych przeglądarki Arc.".into();r.skipped=7;assert_eq!(import_status_copy(&r),"Nie znaleziono danych przeglądarki Arc. Pominięte elementy: 7.");
        assert_eq!(import_status_copy(&import_report(S::Importing)),"Importowanie wybranych danych…");assert_eq!(import_status_copy(&import_report(S::SavingBrowsingData)),"Zapisywanie historii i ciasteczek…");
        assert_eq!(import_status_copy(&import_report(S::Finished)),"Import zakończony. Dodano 1 profili i 2 przestrzeni. Możesz wybrać je na liście przestrzeni.");
        let mut r=import_report(S::Finished);r.browsing_data=true;r.history=10;r.cookies=20;r.data_skipped=3;r.data_failed=true;assert_eq!(import_status_copy(&r),"Import zakończony. Dodano 1 profili i 2 przestrzeni. Możesz wybrać je na liście przestrzeni. Przeniesiono 10 stron historii i 20 ciasteczek. Pominięto: 3. Część danych nie została zapisana. Ponów import.");
        let mut r=import_report(S::Finished);r.ok=false;assert!(import_status_copy(&r).starts_with("Import się nie powiódł."));
        assert!(import_status_copy(&import_report(S{repr:99})).is_empty());
    }
    #[test] fn clear_data_confirm_and_status_cover_selection_period_and_outcomes() {
        let c=clear_data_confirm("Praca 🦀",1,true,false,true);assert!(c.valid);assert_eq!(c.description,"Profil: Praca 🦀\nZakres: Ostatnie 24 godziny\n\nZostaną usunięte:\n• Historia przeglądania\n• Pamięć podręczna\n\nTej operacji nie można cofnąć. Hasła, przypięte karty i pobrane pliki pozostaną zachowane.");assert_eq!((c.title.as_str(),c.confirm.as_str(),c.cancel.as_str()),("Wyczyścić dane przeglądania?","Wyczyść dane","Anuluj"));
        assert!(clear_data_confirm("p",3,false,true,false).description.contains("Cały okres\n\nZostaną usunięte:\n• Cookies"));for (period,h,c,k) in [(-1,true,false,false),(4,true,false,false),(0,false,false,false)]{assert!(!clear_data_confirm("p",period,h,c,k).valid);}
        use ffi::ClearDataEvent as E;let s=clear_data_status(E::Clearing,"Dom");assert_eq!((s.text.as_str(),s.error),("Czyszczenie danych profilu „Dom”…",false));let s=clear_data_status(E::Cleared,"Dom");assert_eq!((s.text.as_str(),s.error),("Wyczyszczono dane profilu „Dom”.",false));
        for e in [E::Busy,E::NothingSelected,E::HistoryForbidden,E::Failed]{let s=clear_data_status(e,"x");assert!(s.error&&!s.text.is_empty());}assert!(clear_data_status(E{repr:77},"x").text.is_empty());
    }
    #[test] fn operation_status_copy_follows_events() {
        use ffi::OperationStatusEvent as E;assert_eq!(operation_status_copy(E::Done),"Gotowe.");assert!(operation_status_copy(E::Cancelled).starts_with("Anulowano."));assert!(operation_status_copy(E::Checking).ends_with("…"));assert!(operation_status_copy(E::StopCapture).starts_with("Najpierw"));assert!(operation_status_copy(E{repr:9}).is_empty());
    }
    #[test] fn palette_result_details_and_password_prompt_copy() {
        use ffi::PaletteResultKind as K;assert_eq!(palette_result_detail(K::Tab,"Praca"),"Karta · Praca");assert_eq!(palette_result_detail(K::Pin,"Dom / Projekt"),"Przypięta · Dom / Projekt");assert_eq!(palette_result_detail(K::Workspace,"x"),"Przełącz workspace");assert_eq!(palette_result_detail(K::History,"a.test"),"Historia · a.test");assert!(palette_result_detail(K{repr:9},"x").is_empty());
        let c=password_prompt_copy(true,"https://a.test","jan","Praca");assert_eq!((c.title.as_str(),c.accept.as_str(),c.cancel.as_str()),("Zaktualizować hasło?","Zaktualizuj","Nie teraz"));assert_eq!(c.lines,vec!["https://a.test","Użytkownik: jan","Profil: Praca","Hasło będzie chronione w magazynie tego profilu."]);
        let c=password_prompt_copy(false,"o","","p");assert_eq!((c.title.as_str(),c.accept.as_str()),("Zapisać hasło?","Zapisz hasło"));assert_eq!(c.lines[1],"Użytkownik: ");
    }
    #[test] fn profile_status_events_have_distinct_copy_and_unknown_is_empty() {
        use ffi::ProfileStatusEvent as E;let all=[E::DuplicateName,E::Creating,E::CreateFailed,E::Created,E::CreateNotSaved,E::Renamed,E::RenameFailed,E::Deleting,E::Deleted,E::DeleteNotSaved,E::LoadFailed];let copies=all.iter().map(|e|profile_status_copy(*e)).collect::<std::collections::BTreeSet<_>>();assert_eq!(copies.len(),all.len());assert!(copies.iter().all(|c|!c.is_empty()));
        assert_eq!(profile_status_copy(E::Creating),"Tworzenie profilu…");assert_eq!(profile_status_copy(E::DuplicateName),"Profil o tej nazwie już istnieje.");assert!(profile_status_copy(E{repr:200}).is_empty());
    }
    #[test] fn preference_control_views_cover_missing_managed_and_editable_states() {
        use ffi::PreferenceControlKind as K;
        for (kind,present,editable,enabled,tooltip) in [(K::Text,false,false,false,"Ustawienie jest niedostępne"),(K::Text,true,false,false,"To ustawienie jest zarządzane"),(K::Text,true,true,true,"Zapisz przyciskiem lub klawiszem Enter"),(K::Toggle,false,true,false,"Profil nie jest jeszcze gotowy"),(K::Toggle,true,false,false,"To ustawienie jest zarządzane"),(K::Toggle,true,true,true,"Dotyczy aktywnego profilu")]{let v=preference_control_view(kind,present,editable);assert_eq!((v.enabled,v.tooltip.as_str()),(enabled,tooltip));}
        assert_eq!(preferences_profile_banner("Żółć 🦀"),"Profil: Żółć 🦀 · Ustawienia wspólne dla aplikacji są oznaczone w opisach.");
    }
    #[test] fn site_rule_detail_and_empty_copy_preserve_policy_embedding_and_managed_state() {
        assert_eq!(site_rule_detail("https://a.test","JavaScript",1,"*",true),"https://a.test · JavaScript · Zezwalaj");
        assert_eq!(site_rule_detail("https://a.test","Kamera",2,"https://b.test",false),"https://a.test · Kamera · Blokuj · Osadzenie: https://b.test · Zarządzane");
        assert_eq!(site_rule_detail("x","y",3,"*",true),"x · y · Pytaj");for setting in [0,4,-1]{assert!(site_rule_detail("x","y",setting,"*",true).ends_with("Inna polityka"));}
        assert_eq!(site_rules_empty(false),"Nie masz jeszcze zapisanych wyjątków.");assert_eq!(site_rules_empty(true),"Brak wyjątków pasujących do wyszukiwania.");
    }
    #[test] fn preference_outcomes_own_status_copy_tone_and_key_specific_errors() {
        use ffi::PreferenceOutcome as O;let mut f=new_preferences_form(3);let idle=f.preference_status();assert_eq!(idle.text,PREFERENCE_IDLE);assert!(!idle.error);
        let cases=[(O::Text,"font",true,"Zapisano. Witryna może wymagać"),(O::Text,"accept_languages",false,"Podaj poprawne kody języków"),(O::Text,"dns_templates",false,"Podaj poprawny szablon DNS"),(O::Text,"font",false,"Nie udało się zapisać. Podaj nazwę rodziny"),(O::Site,"",true,"Zapisano wyjątek"),(O::Site,"",false,"Nie zapisano wyjątku"),(O::SiteRemove,"",true,"Usunięto wyjątek"),(O::SiteRemove,"",false,"Nie udało się usunąć wyjątku"),(O::Value,"hardware_acceleration",true,"Zapisano. Uruchom ponownie"),(O::Value,"x",true,"Zapisano. Jeśli strona"),(O::Value,"dns_mode",false,"Nie zapisano trybu DNS"),(O::Value,"x",false,"Nie udało się zmienić ustawienia"),(O::General,"",false,"Nie udało się zapisać ustawień")];
        for (outcome,key,ok,prefix) in cases{let status=f.preference_outcome(outcome,key,ok);assert!(status.text.starts_with(prefix),"{prefix}");assert_eq!(status.error,!ok);assert_eq!(f.preference_status().text,status.text);}
        let status=f.preference_outcome(O::General,"",true);assert_eq!((status.text.as_str(),status.error),(PREFERENCE_IDLE,false));
        f.preference_outcome(O::Site,"",false);let kept=f.preference_outcome(ffi::PreferenceOutcome{repr:99},"",true);assert!(kept.error&&kept.text.starts_with("Nie zapisano wyjątku"));f.preference_profile("other");assert!(f.preference_status().error);
    }
    #[test] fn tools_dispatch_recording_and_outcomes_follow_native_flow() {
        let mut form=new_tools_form();let c=tools_context();
        for _ in 0..2 {let p=form.tools_dispatch(10,&c);assert!(p.recording&&p.sync_recording);assert!(!p.record_status.is_empty());}
        let p=form.tools_dispatch(99,&c);assert!(p.admitted&&p.sync_recording);assert!(!p.recording);assert_eq!(p.action,0);
        assert!(!form.tools_dispatch(11,&c).sync_recording);
        for page in 1..3 {form.tools_select(page);let failed=form.tools_outcome(false,true);assert!(!failed.refresh);assert!(!failed.status.is_empty());assert_eq!(form.tools_outcome(true,false).status,"Gotowe. Zmiany zostały zastosowane.");assert!(form.tools_outcome(true,true).refresh);assert!(form.tools_outcome(true,true).status.contains("Oczekiwanie"));}
        form.tools_select(2);assert!(form.tools_outcome(false,false).status.contains("{query}"));form.tools_select(1);assert!(form.tools_outcome(false,false).status.contains("kombinacja"));
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
    #[test] fn tools_form_rebuild_clears_transient_state_and_preserves_page() {
        let mut form = new_tools_form(); assert_eq!(form.tools_page(), 1);
        form.tools_select(1); form.tools_record(true);
        form.tools_reset();
        assert_eq!(form.tools_page(), 1); assert!(!form.tools_recording());
        form.tools_select(usize::MAX); assert_eq!(form.tools_page(), 2);
        form.tools_select(0); assert_eq!(form.tools_page(), 1);
    }
    #[test] fn tools_form_order_preserves_native_control_and_command_sequence() {
        use ffi::ToolsNodeKind as K;use ffi::ToolsNodeRole as R;
        let roles=[vec![R::ShortcutChoice,R::ShortcutChord],vec![R::ProviderChoice,R::ProviderName,R::ProviderKeyword,R::ProviderUrl]];
        let actions=[vec![10,11,12,13],vec![20,21,22]];
        for page in 1..3 {let mut f=new_tools_form();f.tools_select(page);let plan=f.tools_form_plan();
            assert!(plan.nodes.iter().filter(|n|n.kind==K::Editor||n.kind==K::Choice).map(|n|n.role).eq(roles[page-1].iter().copied()));
            assert_eq!(plan.nodes.iter().filter(|n|n.kind==K::Action).map(|n|n.action).collect::<Vec<_>>(),actions[page-1]);
            assert!(plan.nodes.iter().filter(|n|n.kind==K::Editor||n.kind==K::Choice||n.kind==K::Action).all(|n|!n.text.is_empty()));
        }
    }
    #[test] fn tools_form_groups_preserve_native_gaps() {
        use ffi::ToolsNodeKind as K;
        for (page,expected) in [(1,1),(2,1)]{let mut f=new_tools_form();f.tools_select(page);let plan=f.tools_form_plan();let mut stack=vec![];let mut groups=0;
            for n in &plan.nodes {match n.kind {K::BeginActions=>{assert_eq!(n.spacing,10);groups+=1;stack.push(K::EndActions);},K::EndActions=>assert!(stack.pop()==Some(n.kind)),_=>{}}assert!(!n.flex);
            }assert!(stack.is_empty());assert_eq!(groups,expected);
        }
    }
    #[test] fn tools_form_description_does_not_reset_recording_or_choice_keys() {
        let mut f=new_tools_form();f.tools_select(1);let p=f.tools_shortcut_choices(vec![form_shortcut("a","A","Cmd+J")]);let key=p.choices[0].key.clone();f.tools_record(true);let epoch=f.choice_epoch;
        for _ in 0..2 {let _=f.tools_form_plan();assert!(f.tools_recording());assert_eq!(f.choice_epoch,epoch);let draft=f.tools_choice_draft(&key);assert!(draft.valid);assert_eq!(draft.target,"a");}
    }
    #[test] fn tools_copy_adapter_roles_are_complete() {
        let shortcut=tools_copy(1);assert_eq!(shortcut.fields.len(),1);assert_eq!(shortcut.actions.len(),4);assert!(shortcut.intro.contains("Escape"));
        let provider=tools_copy(2);assert_eq!(provider.fields.len(),3);assert_eq!(provider.actions.len(),3);assert!(provider.intro.contains("{query}"));
        assert_eq!(tools_copy(usize::MAX).title,provider.title);assert_eq!(tools_copy(0).title,shortcut.title);
        for page in 1..3{let copy=tools_copy(page);assert!(!copy.title.is_empty());assert!(!copy.initial_status.is_empty());assert!(copy.fields.iter().chain(copy.actions.iter()).all(|s|!s.is_empty()));}
    }
    #[test] fn preference_catalogue_preserves_categories_counts_and_platform_gate() {
        use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;
        let c=preference_catalogue(true);assert_eq!(c.categories,["Wygląd","Wyszukiwanie","Prywatność","Uprawnienia stron","Treści i czytelność","Hasła i profile","Pobieranie","Języki i pisownia","Dane przeglądania","Zaawansowane"]);assert_eq!(c.descriptions.len(),10);assert_eq!(c.rows.len(),59);
        let mut counts=[0;10];for row in &c.rows{counts[row.category]+=1;assert!(!row.title.is_empty()&&!row.detail.is_empty());}assert_eq!(counts,[11,4,8,7,12,5,2,3,1,6]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Toggle||r.kind==K::Pinned||r.kind==K::Animations||r.kind==K::WorkspaceNames||r.kind==K::FoldersCollapsed||r.kind==K::TimeMachine).count(),28);assert_eq!(c.rows.iter().filter(|r|r.kind==K::Text).count(),6);
        let no=preference_catalogue(false);assert_eq!(no.rows.len(),58);assert!(no.rows.iter().all(|r|r.action!=A::Certificates));assert!(c.rows.iter().filter(|r|r.action!=A::Certificates).map(|r|(&r.title,&r.detail,r.category)).eq(no.rows.iter().map(|r|(&r.title,&r.detail,r.category))));
    }
    #[test] fn preference_catalogue_registered_store_keys_keep_their_control_kinds() {
        use ffi::PreferenceRowKind as K;let c=preference_catalogue(true);let mut keys=std::collections::BTreeSet::new();
        for row in &c.rows{if row.kind==K::Text||row.kind==K::Toggle||row.kind==K::Picker{assert!(!row.key.is_empty());assert!(keys.insert(row.key.clone()));}else{assert!(row.key.is_empty());}}
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Toggle).map(|r|r.key.as_str()).collect::<Vec<_>>(),["referrers","https_only","third_party_cookies","do_not_track","fingerprint_protection","preconnect","link_auditing","camera","microphone","location","notifications","clipboard","javascript","images","popups","sound","save_passwords","auto_signin","download_prompt","spellcheck","online_spellcheck","caret_browsing","hardware_acceleration"]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Text).map(|r|r.key.as_str()).collect::<Vec<_>>(),["font_standard","font_serif","font_sans","font_fixed","accept_languages","dns_templates"]);
        assert_eq!(c.rows.iter().filter(|r|r.kind==K::Picker).map(|r|r.key.as_str()).collect::<Vec<_>>(),["zoom","font_size","minimum_font_size","fixed_font_size","dns_mode"]);
        for key in ["hardware_acceleration","dns_mode","dns_templates"]{let row=c.rows.iter().find(|r|r.key==key).unwrap();assert!(row.detail.contains("całej aplikacji"));}
    }
    #[test] fn preference_catalogue_editor_and_action_accessibility_retains_native_copy() {
        use ffi::PreferenceRowKind as K;use ffi::PreferenceActionKind as A;let c=preference_catalogue(true);let mut actions=vec![];
        for row in &c.rows{match row.kind{K::Text=>{assert!(!row.placeholder.is_empty());assert_eq!(row.button,"Zapisz");assert_eq!(row.button_ax,format!("Zapisz: {}",row.title));assert!(row.action==A::None);},K::Action=>{assert_eq!(row.button,if row.action==A::ClearTimeline{"Wyczyść"}else{"Otwórz →"});assert_eq!(row.button_ax,row.title);assert!(row.action!=A::None);assert!(!actions.contains(&row.action));actions.push(row.action);},K::Directory=>{assert_eq!(row.button,"Zmień…");assert_eq!(row.button_ax,"Zmień folder pobierania");},_=>{assert!(row.button.is_empty());assert!(row.action==A::None);}}}assert_eq!(actions.len(),11);
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
        let mut s=toggle_style();let off=toggle_scene(&s);assert_eq!((off[0].x,off[0].y,off[0].width,off[0].height),(2.0,4.0,34.0,20.0));assert_eq!(off[1].stroke,1.5);assert_eq!((off[2].x,off[2].width),(6.0,12.0));
        s.checked=true;s.progress=1.0;let on=toggle_scene(&s);assert_eq!((on[1].x,on[1].width),(20.0,12.0));assert_eq!(on[0].color,s.track_on);
        s.rtl=true;let rtl=toggle_scene(&s);assert_eq!(rtl[1].x,6.0);s.focused=true;s.hover=1.0;s.pressed=true;let ops=toggle_scene(&s);assert_eq!(ops.len(),3);assert_eq!(ops[2].stroke,2.0);assert!(ops.iter().all(|p|p.text.is_empty()&&p.clip.width==38.0));
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
        let mut b=row(160.0);b.kind=19;b.height=40.0;b.font_height=0.0;b.detail_font_size=0.0;b.title_width=16.0;b.hovered=true;b.focused=true;b.detail_ink=0x14101010;let ops=layout(&[b]);assert_eq!(ops.len(),5);assert_eq!(ops[0].radius,20.0);assert_eq!(ops[2].text,"Zażółć 日本語 🦀");assert!(ops[2].center);assert_eq!(ops[2].width,128.0);assert_eq!(ops[4].stroke,1.0);
    }
    #[test] fn action_button_text_style_omits_empty_background_and_border() {
        let mut b=row(160.0);b.kind=19;b.fill=0;b.badge=0;b.active=false;b.title_width=16.0;let ops=layout(&[b]);assert_eq!(ops.len(),1);assert!(ops[0].center);assert!(!ops[0].bold);assert_eq!(ops[0].text,"Zażółć 日本語 🦀");
        let mut empty=row(160.0);empty.kind=19;empty.fill=0;empty.badge=0;empty.title.clear();assert!(layout(&[empty]).is_empty());
    }
    #[test] fn appearance_commands_resolve_exact_catalogue_targets_and_panel(){
        let mut graph=new_form_graph(false,0,false,false);
        for emoji in [false,true]{for (index,preset) in appearance_presets(emoji).iter().enumerate(){let mut input=graph.append_form_action();assert!(input.bind_appearance_preset(emoji,index));let binding=input.node_form_command();assert_eq!(binding.panel,ffi::FormPanel::Workspace);assert_eq!(binding.index,index);assert!(binding.key.is_empty());let target=appearance_command_target(binding.command,binding.index);assert!(target.valid);assert_eq!(target.color,preset.color);assert_eq!(target.emoji,preset.emoji);}}
    }
    #[test] fn appearance_commands_reject_invalid_index_and_generic_binding(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();for (emoji,index) in [(false,8),(false,usize::MAX),(true,8),(true,usize::MAX)]{assert!(!input.bind_appearance_preset(emoji,index));assert_eq!(input.node_model_kind(),0);}for command in [ffi::FormCommand::WorkspaceColorPreset,ffi::FormCommand::WorkspaceEmojiPreset]{assert!(!input.node_form_command_init(command,""));assert!(!input.node_form_command_init(command,"2"));}assert!(input.bind_appearance_preset(true,2));assert_eq!(input.node_form_command().index,2);
    }
    #[test] fn appearance_commands_do_not_change_semantics_after_binding(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();assert!(input.bind_appearance_preset(false,1));assert!(input.bind_appearance_preset(false,1));assert!(!input.bind_appearance_preset(false,2));assert!(!input.bind_appearance_preset(true,1));assert!(!input.node_form_command_init(ffi::FormCommand::WorkspaceSave,""));let target=appearance_command_target(input.node_form_command().command,input.node_form_command().index);assert!(target.valid);assert_eq!(target.color,APPEARANCE_COLORS[1]);
    }
    #[test] fn appearance_commands_disabled_retired_and_wrong_models_fail_closed(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();input.bind_appearance_preset(true,2);input.action_enabled(false);assert!(!appearance_command_target(input.node_form_command().command,2).valid);input.action_enabled(true);assert_eq!(appearance_command_target(input.node_form_command().command,2).emoji,"🚀");graph.retire_form_action(input.action_node_id());assert!(!appearance_command_target(input.node_form_command().command,2).valid);let mut checkbox=graph.append_form_action();checkbox.node_checkbox_init(false);assert!(!checkbox.bind_appearance_preset(false,0));assert!(!new_action_control().bind_appearance_preset(true,0));
    }
    #[test] fn appearance_command_target_rejects_unrelated_commands_and_out_of_bounds(){
        for command in [ffi::FormCommand::None,ffi::FormCommand::WorkspaceSave,ffi::FormCommand::PreferenceMore,ffi::FormCommand::ImportApply]{for index in [0,1,usize::MAX]{let target=appearance_command_target(command,index);assert!(!target.valid);assert_eq!(target.color,0);assert!(target.emoji.is_empty());}}for command in [ffi::FormCommand::WorkspaceColorPreset,ffi::FormCommand::WorkspaceEmojiPreset]{assert!(!appearance_command_target(command,usize::MAX).valid);}
    }
    #[test] fn appearance_command_ticket_cancels_without_retaining_emoji_or_changing_binding(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();input.bind_appearance_preset(true,5);let mut ticket=input.defer_form_command();input.action_cancel();assert_eq!(ticket.take_command().command,ffi::FormCommand::None);let binding=input.node_form_command();assert_eq!(appearance_command_target(binding.command,binding.index).emoji,"🎮");assert_eq!(input.defer_form_command().take_command().index,5);
    }
    #[test] fn appearance_presets_preserve_palette_order_labels_and_unicode() {
        let colors=appearance_presets(false);assert_eq!(colors.len(),8);assert_eq!(colors[0].color,0xff928fd4);assert_eq!(colors[7].label,"Grafit");
        for ((_,preset),color) in design::WorkspaceColor::PRESETS.iter().zip(colors.iter()) {assert_eq!(preset.to_argb(),color.color);}assert!(colors.iter().all(|p|p.emoji.is_empty()&&p.color>>24==255));
        let emojis=appearance_presets(true);assert_eq!(emojis.len(),8);assert_eq!(emojis[7].emoji,"🎬");assert_eq!(emojis[2].emoji,"🚀");assert_eq!(emojis[5].label,"Emoji 🎮");
    }
    #[test] fn focus_ring_replaces_the_focus_bar_only_when_a_colour_is_given() {
        let bar=|ops:&[ffi::Primitive],ink:u32|ops.iter().any(|p|p.height==2.0&&p.radius==1.0&&p.color==ink&&p.text.is_empty());
        let mut r=row(200.0);r.focused=true;assert!(bar(&layout(std::slice::from_ref(&r)),r.ink));
        r.focus=0xffd7ff3f;let ops=layout(std::slice::from_ref(&r));assert!(!bar(&ops,r.ink));
        let rings:Vec<_>=ops.iter().filter(|p|p.color==0xffd7ff3f).collect();assert_eq!(rings.len(),1);
        assert_eq!((rings[0].x,rings[0].y,rings[0].width,rings[0].height,rings[0].stroke,rings[0].radius),(1.0,1.0,198.0,32.0,2.0,design_tokens::radius::SM-1.0));
        let mut b=row(160.0);b.kind=19;b.height=32.0;b.title="Zapisz".into();b.fill=0xff112233;b.focused=true;b.focus=0xff16141c;let ops=layout(&[b]);
        assert_eq!(ops.iter().filter(|p|p.color==0xff16141c&&p.stroke==2.0).map(|p|p.radius).collect::<Vec<_>>(),vec![15.0]);
        let mut c=row(16.0);c.kind=20;c.height=16.0;c.focused=true;c.focus=0xffd7ff3f;c.detail_ink=0xff0000ff;assert!(layout(&[c]).iter().all(|p|p.color!=0xffd7ff3f));
        let mut off=row(200.0);off.focus=0xffd7ff3f;assert!(layout(&[off]).iter().all(|p|p.color!=0xffd7ff3f));
    }
    #[test] fn active_card_edge_is_one_inset_stroke_and_absent_without_a_colour() {
        let mut card=row(240.0);card.edge=0xff332211;let ops=layout(std::slice::from_ref(&card));
        let rings:Vec<_>=ops.iter().filter(|p|p.color==0xff332211).collect();assert_eq!(rings.len(),1);
        assert_eq!((rings[0].x,rings[0].y,rings[0].width,rings[0].stroke,rings[0].radius),(0.5,0.5,239.0,1.0,design_tokens::radius::SM-0.5));
        assert!(ops.iter().filter(|p|p.text.is_empty()&&p.color==card.fill).all(|p|p.stroke==0.0));
        card.edge=0;assert!(layout(&[card]).iter().all(|p|p.stroke==0.0||p.color!=0xff332211));
        let mut pill=row(200.0);pill.kind=4;pill.height=36.0;assert_eq!(layout(&[pill])[0].radius,18.0);
    }
    #[test] fn narrow_swatch_cells_shrink_the_dot_and_ring() {
        let mut s=row(16.0);s.kind=18;s.height=28.0;s.active=true;let ops=layout(&[s]);assert_eq!((ops[0].width,ops[0].stroke,ops[1].width),(16.0,1.0,10.0));
    }
    #[test] fn color_wheel_bridge_picks_continuous_colors() {
        let sky=design::WorkspaceColor::SKY.to_argb();let back=|c:u32|design::WorkspaceColor::from_argb(c);
        let top=back(color_ring_target(sky,100.0,19.0,200.0));assert!(top.hue_degrees()<2.0||top.hue_degrees()>358.0);assert_eq!(color_ring_target(sky,100.0,100.0,200.0),0);
        let mid=back(color_pad_target(sky,90.0,90.0,180.0,180.0));assert!((mid.saturation()-0.5).abs()<0.03&&(mid.tone()-0.5).abs()<0.03);
        assert!((back(color_wheel_step(sky,0,2)).hue_degrees()-244.0).abs()<1.5);assert_eq!(color_wheel_step(sky,7,1),sky);
        assert_eq!(color_wheel_description(sky,true),design::wheel::description(back(sky),true));assert!(color_wheel_description(sky,true).starts_with("Barwa 23"));assert_eq!(color_wheel_description(sky,false),"Nasycenie 100%, jasność 50%");
    }
    #[test] fn color_wheel_scenes_draw_a_ring_and_field_with_their_knobs() {
        let mut ring=row(200.0);ring.kind=28;ring.height=200.0;ring.clip.height=200.0;ring.badge=design::WorkspaceColor::SKY.to_argb();ring.tile=true;ring.fill=0xff1e1b28;ring.ink=0xff736d89;ring.title="💼".into();ring.font_size=34.0;
        let ops=layout(std::slice::from_ref(&ring));assert!(ops.len()>=120+5);assert_eq!(ops.iter().filter(|p|p.text=="💼").count(),1);
        assert!(ops.iter().filter(|p|p.text.is_empty()).all(|p|p.x>=-0.01&&p.y>=-0.01&&p.x+p.width<=200.01&&p.y+p.height<=200.01),"ring fits its control");
        let knob=ops.iter().find(|p|p.color==0xff736d89&&p.stroke==1.0).unwrap();let hue=design::WorkspaceColor::from_argb(ring.badge).hue_degrees();let (kx,ky)=design::wheel::ring_knob(200.0,hue);assert!((knob.x+14.0-kx).abs()<0.01&&(knob.y+14.0-ky).abs()<0.01);
        ring.focused=true;ring.focus=0xffd7ff3f;assert_eq!(layout(std::slice::from_ref(&ring)).iter().filter(|p|p.color==0xffd7ff3f&&p.stroke==2.0).count(),1);
        let mut pad=row(180.0);pad.kind=29;pad.height=180.0;pad.clip.height=180.0;pad.badge=design::WorkspaceColor::new(234.0,0.25,1.0).to_argb();pad.fill=0xff1e1b28;pad.ink=0xff736d89;pad.edge=0xff2c2839;
        let ops=layout(std::slice::from_ref(&pad));assert_eq!(ops.iter().filter(|p|p.stroke==0.0&&p.radius==0.0).count(),24*24);
        assert_eq!(ops.iter().filter(|p|p.color==0xff1e1b28&&p.stroke==12.0).count(),1,"rounded-corner mask");
        let knob=ops.iter().find(|p|p.color==0xff736d89).unwrap();let c=design::WorkspaceColor::from_argb(pad.badge);let (kx,ky)=design::wheel::pad_knob(180.0,180.0,c.saturation(),c.tone());assert!((knob.x+12.0-kx).abs()<0.01&&(knob.y+12.0-ky).abs()<0.01);
    }
    #[test] fn color_presets_lay_out_as_two_rows_of_pills_and_draw_dot_and_name() {
        let g=appearance_pill_layout(400,-1,8);assert_eq!((g.width,g.height,g.children.len()),(400,66,10));
        assert_eq!((g.children[3].x,g.children[3].width,g.children[4].y),(3*(95+6),95,36));assert_eq!((g.children[9].width,g.children[9].height),(0,0));
        assert_eq!(appearance_pill_layout(-1,-1,8).width,386);assert!(appearance_pill_layout(10,-1,8).children.iter().all(|r|r.width>=0));
        let mut p=row(95.0);p.kind=18;p.height=30.0;p.clip.height=30.0;p.title="Koral".into();p.badge=0xffcc8463;p.fill=0xff2a2550;p.edge=0xff9a92ff;p.ink=0xfff3f1f8;p.font_size=12.0;
        let ops=layout(std::slice::from_ref(&p));assert_eq!(ops.iter().filter(|o|o.text=="Koral").count(),1);
        assert_eq!(ops.iter().find(|o|o.color==0xff9a92ff).map(|o|(o.stroke,o.radius)),Some((1.0,14.5)));assert_eq!(ops.iter().find(|o|o.color==0xffcc8463).map(|o|o.width),Some(18.0));
        p.width=32.0;assert!(layout(&[p]).iter().all(|o|o.text.is_empty()));
    }
    #[test] fn design_text_styles_restyle_every_text_of_an_item() {
        let mut t=row(300.0);t.kind=17;t.title="Kolor przestrzeni".into();t.text_style=design::TextStyleId::Eyebrow as u32;
        let ops=layout(std::slice::from_ref(&t));let text=ops.iter().find(|p|!p.text.is_empty()).unwrap();
        assert_eq!((text.text.as_str(),text.family,text.font_size,text.font_weight,text.italic),("KOLOR PRZESTRZENI",2,10.5,600,false));assert!((text.tracking-0.08).abs()<1e-6);
        t.text_style=design::TextStyleId::DisplayLarge as u32;let ops=layout(std::slice::from_ref(&t));let text=ops.iter().find(|p|!p.text.is_empty()).unwrap();
        assert_eq!((text.text.as_str(),text.family,text.font_size,text.italic),("Kolor przestrzeni",1,40.0,true));
        t.text_style=0;assert!(layout(&[t]).iter().all(|p|p.family==0));
        let s=design_text_style(design::TextStyleId::Title as u32);assert_eq!((s.family,s.size,s.line_height,s.weight,s.caps),(2,17.0,22.0,600,false));assert_eq!(design_text_style(99).size,0.0);
        assert_eq!((design_font_family(1),design_font_family(3),design_font_family(9)),("Instrument Serif".to_string(),"JetBrains Mono".to_string(),String::new()));
        let m=design_metrics();assert_eq!((m.space_2,m.space_4,m.space_6,m.radius_sm,m.radius_lg),(8,16,24,8,20));
    }
    #[test] fn design_palette_bridge_carries_the_rust_palette() {
        let p=design_palette(true,APPEARANCE_COLORS[2]);let q=design::Palette::for_workspace(design::Scheme::Noc,design::WorkspaceColor::from_argb(APPEARANCE_COLORS[2]));
        assert_eq!((p.bg_void,p.bg_base,p.bg_raised,p.accent,p.on_accent,p.ink_muted,p.focus,p.scrim),(q.bg_void,q.bg_base,q.bg_raised,q.accent,q.on_accent,q.ink_muted,q.focus,q.scrim));
        assert_ne!(design_palette(false,APPEARANCE_COLORS[2]).bg_base,p.bg_base);assert!(design_same_workspace_color(0xff808080,0xff7f7f7f));assert!(!design_same_workspace_color(APPEARANCE_COLORS[0],APPEARANCE_COLORS[1]));
    }
    #[test] fn appearance_selection_matches_custom_values_and_out_of_range_indices() {
        assert!(appearance_selected(false,0,0x928fd4,""));assert!(appearance_selected(false,0,0xff9390d5,""));assert!(!appearance_selected(false,0,0xff123456,""));assert!(!appearance_selected(false,8,0xff928fd4,""));
        assert!(appearance_selected(true,2,0,"🚀"));assert!(!appearance_selected(true,2,0,"👩‍💻"));assert!(!appearance_selected(true,8,0,"🚀"));assert!(!appearance_selected(true,0,0,""));
    }
    #[test] fn appearance_row_preserves_control_sizes_and_excludes_editor() {
        for (emoji,count,bw,bh) in [(false,8,32,32),(true,9,38,38)] {
            let w=if emoji{498}else{398};let g=appearance_row_layout(w,-1,100,30,emoji);assert_eq!(g.children.len(),count+2);assert_eq!(g.height,bh.max(30));
            for r in &g.children[..count] {assert_eq!((r.width,r.height),(bw,bh));}
            let field=&g.children[count+1];assert_eq!(field.x,count as i32*bw+if emoji{(count as i32-1)*6}else{0}+8);assert_eq!(field.x+field.width,if emoji{field.x+100}else{w});
        }
    }
    #[test] fn appearance_row_bounds_remain_nonnegative_on_tiny_and_saturated_metrics() {
        for emoji in [false,true] {for width in [0,1,17,127,188,398,i32::MAX] {for height in [0,1,40] {
            let g=appearance_row_layout(width,height,100,30,emoji);for r in &g.children {assert!(r.x>=0&&r.y>=0&&r.width>=0&&r.height>=0);assert!(i64::from(r.x)+i64::from(r.width)<=i64::from(width));assert!(r.y+r.height<=height);}
        }}}
        assert_eq!(appearance_row_layout(-1,-1,100,30,false).width,364);assert_eq!(appearance_row_layout(-1,-1,i32::MAX,i32::MAX,true).width,i32::MAX);
    }
    #[test] fn appearance_swatch_draws_raw_color_outline_focus_and_clip() {
        let mut swatch=row(32.0);swatch.kind=18;swatch.height=32.0;swatch.active=false;swatch.badge=0xff84b6a4;let ops=layout(std::slice::from_ref(&swatch));assert_eq!(ops.len(),1);assert_eq!(ops[0].color,0xff84b6a4);assert_eq!(ops[0].radius,7.0);
        swatch.active=true;swatch.focused=true;let ops=layout(&[swatch]);assert_eq!(ops.len(),3);assert_eq!(ops[0].stroke,1.0);assert_eq!(ops[1].width,14.0);assert_eq!(ops[2].height,2.0);assert_eq!(ops[0].clip.width,32.0);
    }
    #[test] fn design_button_bridge_follows_the_recipe() {
        let quiet=design_button(true,APPEARANCE_COLORS[0],3,0);assert_eq!((quiet.height,quiet.padding_x,quiet.fill),(32.0,12.0,0));
        let strong=design_button(true,APPEARANCE_COLORS[0],5,0);assert_eq!(strong.edge_width,1.5);assert_eq!(strong.edge,strong.ink);
        let primary=design_button(false,APPEARANCE_COLORS[0],0,0);let p=design_palette(false,APPEARANCE_COLORS[0]);assert_eq!((primary.fill,primary.ink),(p.accent,p.on_accent));
        let t=design_toggle(true,APPEARANCE_COLORS[0],false);assert_eq!((t.width,t.height,t.edge_width),(34.0,20.0,1.5));
        let mut b=row(80.0);b.kind=19;b.height=32.0;b.font_height=8.0;b.detail_font_size=1.5;b.badge=0xffff0000;b.fill=0;b.hovered=false;b.focused=false;
        let ops=layout(&[b]);let edge=ops.iter().find(|p|p.stroke>0.0).unwrap();assert_eq!((edge.stroke,edge.radius,edge.x),(1.5,7.25,0.75));
    }
    #[test] fn address_pill_while_editing_has_a_two_pixel_accent_ring_and_return_key() {
        let pill=|glyph:u32|{let mut a=row(240.0);a.kind=4;a.glyph=glyph;a.edge=0xff7766ee;a.title="github.com".into();a.detail="/zitter".into();a.title_width=60.0;a.height=36.0;a.badge=0xff00ff00;a.active=true;a};
        let ops=layout(&[pill(1)]);let ring=ops.iter().find(|p|p.color==0xff7766ee).unwrap();assert_eq!((ring.stroke,ring.x),(2.0,1.0));assert!(ops.iter().any(|p|p.text=="↵"&&p.center));
        let ops=layout(&[pill(0)]);assert_eq!(ops.iter().find(|p|p.color==0xff7766ee).unwrap().stroke,1.0);assert!(!ops.iter().any(|p|p.text=="↵"));
    }
    #[test] fn kbd_key_is_a_mono_chip_with_a_bottom_edge() {
        let mut k=row(18.0);k.kind=30;k.title="⌘".into();k.height=18.0;k.fill=0xff222222;k.edge=0xff444444;k.ink=0xffcccccc;
        let ops=layout(&[k]);assert_eq!(ops.len(),3);
        assert_eq!((ops[0].color,ops[0].height),(0xff444444,18.0));assert_eq!((ops[1].color,ops[1].height),(0xff222222,17.0));
        assert_eq!((ops[2].family,ops[2].font_weight,ops[2].font_size,ops[2].center),(3,500,11.0,true));
    }
    #[test] fn active_workspace_is_a_raised_pill_with_ringed_dot_and_label() {
        let mut pill=row(90.0);pill.kind=7;pill.glyph=1;pill.monogram.clear();pill.title="Praca".into();pill.height=28.0;pill.fill=0xff202020;pill.edge=0xff303030;pill.badge=0xff928fd4;pill.detail_ink=0xffeeeeee;
        let ops=layout(&[pill]);
        assert_eq!((ops[0].height,ops[0].radius,ops[0].color),(22.0,11.0,0xff202020));assert_eq!(ops[1].stroke,1.0);
        assert_eq!((ops[2].width,ops[2].stroke),(14.0,1.5));assert_eq!((ops[3].width,ops[3].color),(10.0,0xff928fd4));
        let name=ops.iter().find(|p|p.text=="Praca").unwrap();assert_eq!((name.font_weight,name.color,name.center),(600,0xffeeeeee,false));
    }
    #[test] fn active_workspace_without_a_name_is_a_raised_disc_around_its_dot() {
        let mut dot=row(32.0);dot.kind=7;dot.glyph=1;dot.monogram.clear();dot.title.clear();dot.height=32.0;dot.fill=0xff202020;dot.edge=0xff303030;dot.badge=0xff928fd4;
        let ops=layout(&[dot]);assert_eq!(ops.len(),4);
        assert_eq!((ops[0].x,ops[0].width,ops[0].height,ops[0].radius),(5.0,22.0,22.0,11.0));assert_eq!(ops[1].stroke,1.0);
        assert_eq!((ops[2].x,ops[2].width,ops[2].stroke),(9.0,14.0,1.5));assert_eq!((ops[3].x,ops[3].width,ops[3].stroke),(11.0,10.0,0.0));
    }
    #[test] fn appearance_emoji_scene_uses_centered_text_and_selection_wash() {
        let mut emoji=row(30.0);emoji.kind=7;emoji.glyph=1;emoji.monogram="🚀".into();emoji.title.clear();emoji.fill=0x1f1b1a22;let ops=layout(&[emoji]);assert_eq!(ops.len(),2);assert_eq!(ops[0].radius,7.0);assert_eq!(ops[1].text,"🚀");assert!(ops[1].center);
    }
    #[test] fn action_geometry_preserves_workspace_emoji_and_chrome_metrics() {
        for (dot,label,w,h) in [(false,false,32,32),(false,true,32,32),(true,false,24,28),(true,true,30,28)]{let r=action_preferred_size(dot,label);assert_eq!((r.width,r.height),(w,h));}
    }
    fn node_tab(low:u64)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:1,high:7,low,name:String::new()}}
    fn node_pin(name:&str)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:2,high:0,low:0,name:name.into()}}
    fn node_close(low:u64)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind:10,high:7,low,name:String::new()}}
    fn node_named(kind:u32,name:&str)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind,high:0,low:0,name:name.into()}}
    fn nested_pin(id:&str,parent:&str,folder:bool,collapsed:bool)->ffi::SidebarPinInput{let mut pin=pin_input(id,id,folder,collapsed);pin.parent=parent.into();pin}
    fn pinned_tab(low:u64,index:usize,pin:&str)->ffi::SidebarTabInput{ffi::SidebarTabInput{high:7,low,index,pin:pin.into()}}
    #[test] fn sidebar_split_pair_owns_member_parents_slots_and_retires_on_unsplit(){
        let tabs=||vec![sidebar_tab(1,0),sidebar_tab(2,1),sidebar_tab(3,2)];let mut g=new_action_graph();assert!(g.reconcile_sidebar_layout(vec![],tabs(),vec![2,0],true).admitted);let a=g.acquire(&node_tab(1));let b=g.acquire(&node_tab(2));let c=g.acquire(&node_tab(3));let tree=g.sidebar_tabs();assert_ne!(tree.pair,0);assert_eq!(tree.parent,tree.root);assert!(tree.visible);assert_eq!((a.node_parent_id(),c.node_parent_id(),b.node_parent_id()),(tree.pair,tree.pair,tree.root));
        assert_eq!(tree.slots.iter().map(|s|(s.first,s.members.clone(),s.pin,s.pair)).collect::<Vec<_>>(),vec![(2,vec![2,0],usize::MAX,true),(1,vec![],usize::MAX,false)]);assert_eq!(g.acquire(&node_named(15,"sidebar.pair")).action_node_id(),0);let ids=(a.action_node_id(),c.action_node_id());
        assert!(g.reconcile_sidebar_layout(vec![],tabs(),vec![],false).admitted);let tree=g.sidebar_tabs();assert_eq!(tree.pair,0);assert!(tree.slots.iter().all(|s|!s.pair));assert_eq!((a.node_parent_id(),c.node_parent_id()),(tree.root,tree.root));assert_eq!((a.action_node_id(),c.action_node_id()),ids);assert!(g.tab_target(7,1,ids.0,false).valid);
    }
    #[test] fn sidebar_split_pair_layout_change_cancels_input_and_identical_layout_preserves_it(){
        let tabs=||vec![sidebar_tab(1,0),sidebar_tab(2,1),sidebar_tab(3,2)];let mut g=new_action_graph();g.reconcile_sidebar_layout(vec![],tabs(),vec![0,1],true);let mut a=g.acquire(&node_tab(1));let b=g.acquire(&node_tab(2));let c=g.acquire(&node_tab(3));let pair=g.sidebar_tabs().pair;a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=a.action_admission_epoch();
        let plan=g.reconcile_sidebar_layout(vec![],tabs(),vec![0,1],true);assert_eq!((plan.created,plan.retired),(0,0));assert!(a.action_pending()&&a.action_admits(epoch));
        assert!(g.reconcile_sidebar_layout(vec![],tabs(),vec![0,2],true).admitted);assert!(!a.action_pending());let tree=g.sidebar_tabs();assert_eq!(tree.pair,pair);assert_eq!((b.node_parent_id(),c.node_parent_id()),(tree.root,pair));assert_eq!(tree.slots[0].members,vec![0,2]);
        assert!(g.reconcile_sidebar_layout(vec![],tabs(),vec![0,0],true).admitted);assert_eq!(g.sidebar_tabs().pair,0);assert!(g.reconcile_sidebar_layout(vec![],tabs(),vec![0,9],true).admitted);assert_eq!(g.sidebar_tabs().pair,0);
    }
    #[test] fn sidebar_split_pair_anchors_to_pin_group_and_follows_folder_visibility(){
        let pins=||vec![nested_pin("f","",true,false),nested_pin("p","f",false,false),nested_pin("q","f",false,false)];let tabs=||vec![pinned_tab(1,0,"p"),sidebar_tab(2,1)];let mut g=new_action_graph();assert!(g.reconcile_sidebar_layout(pins(),tabs(),vec![0,1],true).admitted);let folder=g.acquire(&node_named(2,"f"));let p=g.acquire(&node_named(2,"p"));let q=g.acquire(&node_named(2,"q"));let mut a=g.acquire(&node_tab(1));let b=g.acquire(&node_tab(2));
        let tree=g.sidebar_tabs();let pin_tree=g.pin_tree();let group=pin_tree.entries[0].group;assert_eq!(tree.parent,group);assert_eq!(tree.slots[0].pin,1);assert!(tree.visible);assert_eq!((a.node_parent_id(),b.node_parent_id()),(tree.pair,tree.pair));assert_eq!(tree.pins.iter().map(|v|(v.index,v.visible)).collect::<Vec<_>>(),vec![(0,true),(1,false),(2,true)]);
        assert!(!g.pin_target("p",p.action_node_id()).valid&&g.pin_target("q",q.action_node_id()).valid);assert!(g.tab_target(7,1,a.action_node_id(),false).valid);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        assert!(g.set_pin_collapsed(&folder,true));assert!(!g.sidebar_tabs().visible);assert!(!a.action_pending());assert!(!g.tab_target(7,1,a.action_node_id(),false).valid&&!g.tab_target(7,2,b.action_node_id(),false).valid);assert!(!g.pin_target("q",q.action_node_id()).valid);
        assert!(g.set_pin_collapsed(&folder,false));assert!(g.sidebar_tabs().visible&&g.tab_target(7,2,b.action_node_id(),false).valid);assert!(!g.pin_target("p",p.action_node_id()).valid);
        assert!(g.reconcile_sidebar_layout(pins(),tabs(),vec![],false).admitted);let tree=g.sidebar_tabs();assert_eq!((tree.pair,a.node_parent_id()),(0,tree.root));assert!(!g.pin_target("p",p.action_node_id()).valid);
        assert!(g.reconcile_sidebar_layout(pins(),vec![sidebar_tab(1,0),sidebar_tab(2,1)],vec![],false).admitted);assert!(g.pin_target("p",p.action_node_id()).valid);
    }
    #[test] fn sidebar_tree_defaults_folders_collapsed_and_reveals_single_items(){
        let nodes=vec![ffi::TreeNode{id:"f".into(),parent:"".into(),folder:true},ffi::TreeNode{id:"a".into(),parent:"f".into(),folder:false},ffi::TreeNode{id:"b".into(),parent:"f".into(),folder:false}];
        let mut t=new_sidebar_tree();t.set_default_collapsed(true);
        let vis=|t:&SidebarTree|t.tree_plan("w",&nodes).iter().map(|e|(e.collapsed,e.visible)).collect::<Vec<_>>();
        assert_eq!(vis(&t),vec![(true,true),(false,false),(false,false)]);assert!(vis(&new_sidebar_tree()).iter().all(|&(c,v)|!c&&v));
        t.set_revealed("w","b",true);assert_eq!(vis(&t),vec![(true,true),(false,false),(false,true)]);assert!(t.revealed("w","b")&&!t.revealed("x","b"));
        assert!(!t.toggle("w","f"));assert_eq!(vis(&t),vec![(false,true),(false,true),(false,true)]);
        t.set_collapsed("w","f",true);assert!(t.collapsed("w","f"));t.forget_workspace("w");assert!(t.collapsed("w","f")&&!t.revealed("w","b"));
    }
    #[test] fn sidebar_pin_revealed_under_collapsed_folder_peeks_until_toggled(){
        let mut g=new_action_graph();let mut peek=nested_pin("a","f",false,false);peek.revealed=true;
        assert!(g.reconcile_sidebar(vec![nested_pin("f","",true,true),peek,nested_pin("b","f",false,false)],vec![]).admitted);
        let folder=g.acquire(&node_named(2,"f"));let a=g.acquire(&node_named(2,"a"));let b=g.acquire(&node_named(2,"b"));
        let vis=|g:&ActionGraph|g.pin_tree().entries.iter().map(|e|(e.key.clone(),e.visible)).collect::<Vec<_>>();
        assert_eq!(vis(&g),vec![("f".to_string(),true),("a".to_string(),true),("b".to_string(),false)]);
        assert!(g.pin_target("a",a.action_node_id()).valid&&!g.pin_target("b",b.action_node_id()).valid);
        assert!(g.set_pin_collapsed(&folder,false));assert!(vis(&g).iter().all(|(_,v)|*v));
        assert!(g.set_pin_collapsed(&folder,true));assert!(vis(&g)[1..].iter().all(|(_,v)|!*v));
    }
    #[test] fn sidebar_split_pair_in_collapsed_folder_starts_hidden_and_unreachable_pin_falls_back_to_root(){
        let mut g=new_action_graph();assert!(g.reconcile_sidebar_layout(vec![nested_pin("f","",true,true),nested_pin("p","f",false,false)],vec![pinned_tab(1,0,"p"),sidebar_tab(2,1)],vec![0,1],true).admitted);g.acquire(&node_tab(1));let tree=g.sidebar_tabs();assert!(!tree.visible);assert!(!g.tab_target(7,1,g.nodes[&(1,7,1,String::new())].borrow().id,false).valid);
        let mut g=new_action_graph();assert!(g.reconcile_sidebar_layout(vec![nested_pin("p","missing",false,false)],vec![pinned_tab(1,0,"p"),sidebar_tab(2,1)],vec![0,1],true).admitted);let tree=g.sidebar_tabs();assert_eq!(tree.parent,tree.root);assert!(tree.visible);assert_eq!(tree.slots[0].pin,usize::MAX);
    }
    #[test] fn pin_tree_models_folder_groups_and_actual_parent_edges_in_preorder(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("leaf","nested",false,false),nested_pin("outer","",true,false),nested_pin("nested","outer",true,false)],vec![]);let tree=g.pin_tree();assert_eq!(tree.entries.iter().map(|e|e.key.as_str()).collect::<Vec<_>>(),vec!["outer","nested","leaf"]);let outer=&tree.entries[0];let nested=&tree.entries[1];let leaf=&tree.entries[2];assert_eq!((outer.index,nested.index,leaf.index),(1,2,0));assert_eq!(outer.parent,tree.root);assert_eq!(nested.parent,outer.group);assert_eq!(leaf.parent,nested.group);assert_ne!(outer.id,outer.group);assert_ne!(nested.id,nested.group);assert_eq!((outer.depth,nested.depth,leaf.depth),(0,1,2));let input=g.acquire(&node_named(2,"leaf"));assert_eq!(input.node_parent_id(),nested.group);assert_eq!(g.acquire(&node_named(14,"outer")).action_node_id(),0);
    }
    #[test] fn pin_tree_collapse_revokes_descendant_targets_and_input_before_native_visibility(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("outer","",true,false),nested_pin("inner","outer",true,false),nested_pin("leaf","inner",false,false)],vec![]);let outer=g.acquire(&node_named(2,"outer"));let mut inner=g.acquire(&node_named(2,"inner"));let mut leaf=g.acquire(&node_named(2,"leaf"));inner.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);leaf.action_key(true,1,false,true);assert!(g.pin_target("leaf",leaf.action_node_id()).valid);assert!(g.set_pin_collapsed(&outer,true));assert!(!inner.action_pending()&&!leaf.action_pending());assert!(!g.pin_target("inner",inner.action_node_id()).valid&&!g.pin_target("leaf",leaf.action_node_id()).valid);assert_eq!(g.pin_tree().entries.iter().map(|e|e.visible).collect::<Vec<_>>(),vec![true,false,false]);g.set_pin_collapsed(&outer,false);assert!(g.pin_target("leaf",leaf.action_node_id()).valid);
    }
    #[test] fn pin_tree_reopening_parent_preserves_nested_collapsed_state(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("outer","",true,false),nested_pin("inner","outer",true,true),nested_pin("leaf","inner",false,false)],vec![]);let outer=g.acquire(&node_named(2,"outer"));g.set_pin_collapsed(&outer,true);g.set_pin_collapsed(&outer,false);let tree=g.pin_tree();assert!(tree.entries[0].visible&&tree.entries[1].visible&&!tree.entries[2].visible);assert!(tree.entries[1].collapsed);
    }
    #[test] fn pin_tree_reparent_off_deleted_group_preserves_child_id_and_lease(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("folder","",true,false),nested_pin("leaf","folder",false,false)],vec![]);let mut leaf=g.acquire(&node_named(2,"leaf"));let id=leaf.action_node_id();let old_parent=leaf.node_parent_id();let group=g.nodes[&(14,0,0,"folder".into())].clone();leaf.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(g.reconcile_sidebar(vec![nested_pin("leaf","",false,false)],vec![]).admitted);assert_eq!(leaf.action_node_id(),id);assert_ne!(leaf.node_parent_id(),old_parent);assert_eq!(leaf.node_parent_id(),g.pin_tree().root);assert!(!leaf.action_pending());assert!(!group.borrow().alive&&group.borrow().model.is_none());assert!(g.pin_target("leaf",id).valid);
    }
    #[test] fn pin_tree_orphans_cycles_and_children_of_pages_stay_dormant_until_repaired(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("a","b",true,false),nested_pin("b","a",true,false),nested_pin("missing","absent",false,false),nested_pin("page","",false,false),nested_pin("child","page",false,false)],vec![]);assert_eq!(g.pin_tree().entries.len(),1);let a=g.acquire(&node_named(2,"a"));let id=a.action_node_id();assert_eq!(a.node_parent_id(),0);assert!(!g.pin_target("a",id).valid);g.reconcile_sidebar(vec![nested_pin("a","",true,false),nested_pin("b","a",true,false)],vec![]);assert_eq!(a.action_node_id(),id);assert!(g.pin_target("a",id).valid);assert_eq!(g.pin_tree().entries.len(),2);
    }
    #[test] fn pin_tree_depth_bound_matches_existing_tree_plan_contract(){
        let mut g=new_action_graph();let pins=(0..67).map(|i|nested_pin(&format!("folder-{i}"),&if i==0{String::new()}else{format!("folder-{}",i-1)},true,false)).collect();g.reconcile_sidebar(pins,vec![]);let tree=g.pin_tree();assert_eq!(tree.entries.len(),65);assert_eq!(tree.entries.last().unwrap().depth,64);let deep=g.acquire(&node_named(2,"folder-65"));assert_eq!(deep.node_parent_id(),0);assert!(!g.pin_target("folder-65",deep.action_node_id()).valid);
    }
    #[test] fn pin_tree_role_change_retires_group_and_moves_repaired_child_atomically(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![nested_pin("owner","",true,false),nested_pin("child","owner",false,false)],vec![]);let owner=g.acquire(&node_named(2,"owner"));let child=g.acquire(&node_named(2,"child"));let group=owner.pin_binding().group;let child_id=child.action_node_id();assert!(g.reconcile_sidebar(vec![nested_pin("owner","",false,false),nested_pin("child","",false,false)],vec![]).admitted);assert_eq!(owner.pin_binding().group,0);assert_eq!(child.action_node_id(),child_id);assert_eq!(child.node_parent_id(),g.pin_tree().root);assert!(g.order.iter().all(|id|*id!=group));
    }
    #[test] fn pin_tree_generic_reconcile_cannot_remove_retained_folder_group_or_owner(){
        let (mut g,input)=pin_fixture();let before=g.order.clone();assert!(!g.reconcile(vec![node_named(11,"sidebar.pins"),node_named(2,"folder")]).admitted);assert!(!g.reconcile(vec![node_named(11,"sidebar.pins"),node_named(14,"folder")]).admitted);assert_eq!(g.order,before);assert!(g.pin_target("folder",input.action_node_id()).valid);
    }
    #[test] fn parent_transaction_rejects_missing_nodes_and_cycles_without_mutation(){
        let mut g=new_action_graph();let keys=||vec![node_named(4,"a"),node_named(4,"b")];g.reconcile(keys());let mut input=g.acquire(&node_named(4,"a"));input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let before=g.order.clone();let a=(4,0,0,"a".into());let b=(4,0,0,"b".into());let missing=(4,0,0,"missing".into());for edges in [BTreeMap::from([(a.clone(),Some(a.clone()))]),BTreeMap::from([(a.clone(),Some(b.clone())),(b.clone(),Some(a.clone()))]),BTreeMap::from([(a.clone(),Some(missing.clone()))]),BTreeMap::from([(missing,None)])]{assert!(!g.reconcile_with_parents(keys(),Some(&edges)).admitted);assert_eq!(g.order,before);assert_eq!(input.node_parent_id(),0);assert!(input.action_pending());}
    }
    #[test] fn parent_transaction_checks_cycles_through_unprojected_retained_edges(){
        let mut g=new_action_graph();let keys=||vec![node_named(4,"a"),node_named(4,"b")];let a=(4,0,0,"a".into());let b=(4,0,0,"b".into());assert!(g.reconcile_with_parents(keys(),Some(&BTreeMap::from([(b.clone(),Some(a.clone()))]))).admitted);let input=g.acquire(&node_named(4,"b"));let parent=input.node_parent_id();assert!(!g.reconcile_with_parents(keys(),Some(&BTreeMap::from([(a,Some(b))]))).admitted);assert_eq!(input.node_parent_id(),parent);
    }
    #[test] fn parent_transaction_id_exhaustion_cannot_retire_or_reparent_existing_records(){
        let (mut g,input)=pin_fixture();let id=input.action_node_id();let parent=input.node_parent_id();let before=g.order.clone();g.next_id=u64::MAX;assert!(!g.reconcile_sidebar(vec![nested_pin("folder","new",true,false),nested_pin("new","",true,false)],vec![]).admitted);assert_eq!(g.order,before);assert_eq!((input.action_node_id(),input.node_parent_id()),(id,parent));
    }
    #[test] fn pin_tree_snapshot_is_owned_and_group_identity_survives_rename(){
        let (mut g,input)=pin_fixture();let group=input.pin_binding().group;let mut snapshot=g.pin_tree();snapshot.entries[0].key.clear();snapshot.entries.clear();assert_eq!(g.pin_tree().entries[0].key,"folder");g.reconcile_sidebar(vec![pin_input("folder","Renamed",true,false)],vec![]);assert_eq!(input.pin_binding().group,group);assert_eq!(g.pin_tree().entries[0].group,group);
    }
    fn pin_input(id:&str,title:&str,folder:bool,collapsed:bool)->ffi::SidebarPinInput{ffi::SidebarPinInput{id:id.into(),parent:String::new(),title:title.encode_utf16().collect(),monogram:vec![b'P' as u16],color:0xff123456,folder,collapsed,revealed:false}}
    fn pin_fixture()->(Box<ActionGraph>,Box<ActionControl>){let mut g=new_action_graph();assert!(g.reconcile_sidebar(vec![pin_input("folder","Dokumenty 日本語 🦀",true,false)],vec![]).admitted);let input=g.acquire(&node_named(2,"folder"));(g,input)}
    #[test] fn pin_model_owns_role_title_monogram_and_collection_parent(){
        let (g,input)=pin_fixture();assert_eq!(input.node_model_kind(),10);let binding=input.pin_binding();assert!(binding.valid&&binding.folder&&!binding.collapsed);assert_eq!(binding.key,"folder");assert_eq!(input.node_parent_id(),g.nodes[&(11,0,0,"sidebar.pins".into())].borrow().id);let p=input.sidebar_presentation();assert!(p.valid&&p.folder&&!p.tile);assert_eq!(p.height,34);assert_eq!(p.title,"▾ Dokumenty 日本語 🦀".encode_utf16().collect::<Vec<_>>());let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert!(scene.valid);assert_eq!(scene.item.monogram,"📁");assert_eq!(scene.item.badge,0);assert!(!scene.item.close&&!scene.item.close_visible);
    }
    #[test] fn pin_collapse_uses_current_rust_base_title_and_updates_scene_cache(){
        let (mut g,mut input)=pin_fixture();let id=input.action_node_id();input.action_focus(true);input.action_on(true);input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=input.action_admission_epoch();assert!(g.set_pin_collapsed(&input,true));assert_eq!(input.sidebar_title(),"▸ Dokumenty 日本語 🦀".encode_utf16().collect::<Vec<_>>());assert!(input.pin_binding().collapsed);assert!(input.sidebar_set_title("Renamed folder".encode_utf16().collect()));assert!(g.set_pin_collapsed(&input,false));assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).item.title,"▾ Renamed folder");assert_eq!(input.action_node_id(),id);assert!(input.action_pending()&&input.action_admits(epoch)&&input.action_snapshot().focused&&input.action_snapshot().on);
    }
    #[test] fn pin_reconcile_rename_preserves_node_id_and_cancels_stale_press(){
        let (mut g,mut input)=pin_fixture();let id=input.action_node_id();input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let plan=g.reconcile_sidebar(vec![pin_input("folder","Renamed",true,true)],vec![]);assert!(plan.admitted);assert_eq!((plan.created,plan.retired),(0,0));assert_eq!(input.action_node_id(),id);assert!(!input.action_pending());assert!(input.pin_binding().collapsed);assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).item.title,"▸ Renamed");assert!(g.set_pin_collapsed(&input,false));assert_eq!(input.sidebar_title(),"▾ Renamed".encode_utf16().collect::<Vec<_>>());
    }
    #[test] fn pin_identical_projection_preserves_focus_on_and_pending_input(){
        let (mut g,mut input)=pin_fixture();input.action_focus(true);input.action_on(true);input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=input.action_admission_epoch();g.reconcile_sidebar(vec![pin_input("folder","Dokumenty 日本語 🦀",true,false)],vec![]);assert!(input.action_pending()&&input.action_admits(epoch)&&input.action_snapshot().focused&&input.action_snapshot().on);
    }
    #[test] fn pin_leaf_scene_owns_monogram_color_and_cannot_toggle_folder_state(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![pin_input("leaf","Leaf",false,false)],vec![]);let mut input=g.acquire(&node_named(2,"leaf"));assert!(!g.set_pin_collapsed(&input,true));assert!(!input.pin_binding().folder);let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert_eq!(scene.item.title,"Leaf");assert_eq!(scene.item.monogram,"P");assert_eq!(scene.item.badge,0xff123456);assert!(!scene.item.close);assert!(input.sidebar_set_monogram(vec![0xd83e,0xdd80],0xffaabbcc));let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false);assert_eq!(scene.item.monogram,"🦀");assert_eq!(scene.item.badge,0xffaabbcc);
    }
    #[test] fn pin_role_change_updates_same_identity_and_rejects_old_folder_operation(){
        let (mut g,input)=pin_fixture();let id=input.action_node_id();g.reconcile_sidebar(vec![pin_input("folder","Now a page",false,false)],vec![]);assert_eq!(input.action_node_id(),id);assert!(!g.pin_target("folder",id).folder);assert!(!g.set_pin_collapsed(&input,true));assert_eq!(input.sidebar_title(),"Now a page".encode_utf16().collect::<Vec<_>>());assert!(!input.sidebar_presentation().folder);assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).item.monogram,"P");
    }
    #[test] fn pin_invalid_projection_is_atomic_for_ids_roles_and_existing_payloads(){
        let (mut g,mut input)=pin_fixture();input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let before=g.order.clone();for pins in [vec![pin_input("","Bad",false,false)],vec![pin_input("folder","Bad",false,true)],vec![pin_input("folder","Changed",true,false),pin_input("folder","Duplicate",true,false)]]{assert!(!g.reconcile_sidebar(pins,vec![]).admitted);assert_eq!(g.order,before);assert!(input.action_pending());assert_eq!(input.sidebar_title(),"▾ Dokumenty 日本語 🦀".encode_utf16().collect::<Vec<_>>());}
    }
    #[test] fn pin_targets_require_live_enabled_current_lease_and_known_key(){
        let (mut g,mut input)=pin_fixture();let id=input.action_node_id();assert!(g.pin_target("folder",id).valid);assert!(!g.pin_target("folder",0).valid&&!g.pin_target("folder",id+1).valid&&!g.pin_target("missing",id).valid);input.action_enabled(false);assert!(!g.pin_target("folder",id).valid);assert!(g.set_pin_collapsed(&input,true));assert!(input.sidebar_presentation().valid);input.action_enabled(true);assert!(g.pin_target("folder",id).valid);drop(input);assert!(!g.pin_target("folder",id).valid);
    }
    #[test] fn pin_foreign_and_replaced_adapter_cannot_toggle_or_retrieve_presentation(){
        let (mut g,old)=pin_fixture();let (mut foreign,_other)=pin_fixture();assert!(!foreign.set_pin_collapsed(&old,true));let mut current=g.acquire(&node_named(2,"folder"));assert!(!old.pin_binding().valid);assert!(!old.sidebar_presentation().valid);assert!(!g.set_pin_collapsed(&old,true));current.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);drop(old);assert!(current.action_pending());assert!(g.set_pin_collapsed(&current,true));
    }
    #[test] fn pin_retirement_releases_properties_and_readmission_uses_fresh_identity(){
        let (mut g,input)=pin_fixture();let id=input.action_node_id();let record=if let ActionStorage::Shared{node,..}=&input.storage{node.clone()}else{panic!("record missing")};g.reconcile_sidebar(vec![],vec![]);assert!(!input.pin_binding().valid&&!input.sidebar_visual().valid);assert!(input.sidebar_title().is_empty());assert!(record.borrow().model.is_none());g.reconcile_sidebar(vec![pin_input("folder","Fresh",true,false)],vec![]);let fresh=g.acquire(&node_named(2,"folder"));assert_ne!(fresh.action_node_id(),id);assert_eq!(fresh.sidebar_title(),"▾ Fresh".encode_utf16().collect::<Vec<_>>());drop(g);assert!(!fresh.sidebar_presentation().valid);
    }
    #[test] fn pin_surrogates_and_owned_snapshots_preserve_raw_text_without_native_title_copy(){
        let mut g=new_action_graph();let mut data=pin_input("folder","",true,true);data.title=vec![0xd800,b'a' as u16,0xdc00];g.reconcile_sidebar(vec![data],vec![]);let input=g.acquire(&node_named(2,"folder"));let mut snapshot=input.sidebar_presentation();assert_eq!(snapshot.title,vec![0x25b8,0x20,0xd800,b'a' as u16,0xdc00]);snapshot.title.clear();assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).item.title,"▸ �a�");g.set_pin_collapsed(&input,false);assert_eq!(input.sidebar_title(),vec![0x25be,0x20,0xd800,b'a' as u16,0xdc00]);
    }
    #[test] fn pin_membership_order_changes_cancel_retained_input_and_preserve_parent(){
        let (mut g,mut input)=pin_fixture();let parent=input.node_parent_id();input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);g.reconcile_sidebar(vec![pin_input("other","Other",false,false),pin_input("folder","Dokumenty 日本語 🦀",true,false)],vec![]);assert!(!input.action_pending());assert_eq!(input.node_parent_id(),parent);assert!(g.pin_target("folder",input.action_node_id()).valid);
    }
    fn projected_tab()->(Box<ActionGraph>,Box<ActionControl>){let mut g=tab_scene_fixture();let input=g.acquire(&node_tab(1));assert!(g.project_tab(&input,"Żółć 日本語 🦀".encode_utf16().collect(),vec![b'Z' as u16],0xff123456,false).admitted);(g,input)}
    fn tab_style()->ffi::SidebarRowStyle{ffi::SidebarRowStyle{font_size:13.0,ink:0xff112233,card:0xff778899,hover:0x22112233,edge:0xff445566,focus:0xffd7ff3f,muted:0xff667788}}
    fn tab_box()->ffi::FloatRect{ffi::FloatRect{x:3.0,y:5.0,width:200.0,height:34.0}}
    #[test] fn tab_properties_own_raw_utf16_and_prepare_utf8_scene_text(){
        let (g,input)=projected_tab();let p=input.sidebar_presentation();assert!(p.valid&&!p.tile);assert_eq!(p.title,"Żółć 日本語 🦀".encode_utf16().collect::<Vec<_>>());assert_eq!(p.monogram,vec![b'Z' as u16]);assert_eq!((p.color,p.height),(0xff123456,34));let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,false);assert!(scene.valid);assert_eq!(scene.item.title,"Żółć 日本語 🦀");assert_eq!(scene.item.monogram,"Z");assert_eq!(scene.item.badge,0xff123456);assert_eq!(g.tab_target(7,1,input.action_node_id(),false).index,4);
    }
    #[test] fn tab_properties_title_and_monogram_updates_preserve_input_selection_and_target(){
        let (g,mut input)=projected_tab();input.action_on(true);input.action_focus(true);input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=input.action_admission_epoch();assert!(input.sidebar_set_title("Changed".encode_utf16().collect()));assert!(input.sidebar_set_monogram(vec![0xd83e,0xdd80],0xffaabbcc));assert!(input.action_pending()&&input.action_admits(epoch)&&input.action_snapshot().focused&&input.action_snapshot().on);let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert_eq!(scene.item.title,"Changed");assert_eq!(scene.item.monogram,"🦀");assert_eq!(scene.item.fill,tab_style().card);assert_eq!(g.tab_target(7,1,input.action_node_id(),false).index,4);
    }
    #[test] fn tab_properties_identical_projection_is_idle_and_tile_changes_cancel_both_leases(){
        let (mut g,mut input)=projected_tab();let mut close=g.acquire(&node_close(1));input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);close.action_key(true,1,false,true);let epoch=input.action_admission_epoch();let same=g.project_tab(&input,input.sidebar_title(),vec![b'Z' as u16],0xff123456,false);assert!(same.admitted&&!same.changed&&!same.title_changed&&!same.geometry_changed);assert!(input.action_pending()&&close.action_pending()&&input.action_admits(epoch));let tile=g.project_tab(&input,input.sidebar_title(),vec![b'Z' as u16],0xff123456,true);assert!(tile.admitted&&tile.changed&&!tile.title_changed&&tile.geometry_changed);assert!(!input.action_pending()&&!close.action_pending()&&!input.action_admits(epoch));assert_eq!(input.sidebar_visual().height,38);assert!(input.sidebar_visual().tile);
    }
    #[test] fn tab_properties_snapshots_are_owned_and_disabled_projection_stays_available(){
        let (mut g,mut input)=projected_tab();let mut snapshot=input.sidebar_presentation();snapshot.title.clear();snapshot.monogram.clear();assert!(!input.sidebar_title().is_empty());input.action_enabled(false);let changed=g.project_tab(&input,"Managed title".encode_utf16().collect(),vec![b'M' as u16],0xffaabbcc,false);assert!(changed.admitted&&changed.title_changed);assert!(input.sidebar_visual().valid);assert_eq!(input.sidebar_title(),"Managed title".encode_utf16().collect::<Vec<_>>());assert!(!g.tab_target(7,1,input.action_node_id(),false).valid);let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert!(scene.valid&&!scene.item.hovered);assert_eq!(scene.item.title,"Managed title");
    }
    #[test] fn tab_properties_retired_stale_foreign_and_wrong_models_cannot_project(){
        let (mut g,mut input)=projected_tab();let mut foreign=tab_scene_fixture();assert!(!foreign.project_tab(&input,vec![],vec![],0,false).admitted);let replacement=g.acquire(&node_tab(1));assert!(!input.sidebar_presentation().valid);assert!(!input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).valid);assert!(!input.sidebar_set_title(vec![1]));assert!(!g.project_tab(&input,vec![],vec![],0,false).admitted);assert!(replacement.sidebar_presentation().valid);let mut wrong=g.append_form_action();assert!(!g.project_tab(&wrong,vec![],vec![],0,false).admitted);assert!(!wrong.sidebar_set_monogram(vec![1],0));drop(g);assert!(!replacement.sidebar_visual().valid);
    }
    #[test] fn tab_properties_preserve_unpaired_surrogates_and_refresh_utf8_cache(){
        let (mut g,mut input)=projected_tab();let raw=vec![0xd800,b'a' as u16,0xdc00];g.project_tab(&input,raw.clone(),vec![0xdfff],0xff123456,false);assert_eq!(input.sidebar_title(),raw);let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,true);assert_eq!(scene.item.title,"�a�");assert_eq!(scene.item.monogram,"�");assert!(!scene.item.close_visible);assert!(input.sidebar_set_title("Normal".encode_utf16().collect()));assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,false).item.title,"Normal");
    }
    #[test] fn tab_properties_rust_scene_matches_normal_tile_active_hover_and_focus_policy(){
        let (mut g,mut input)=projected_tab();let normal=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,false);assert_eq!(normal.item.fill,0);input.action_pointer(ffi::PointerPhase::Enter,1,1,true,true,true);assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true).item.fill,tab_style().hover);g.project_tab(&input,input.sidebar_title(),vec![b'Z' as u16],0xff123456,true);assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,false).item.fill,0x59778899);input.action_pointer(ffi::PointerPhase::Enter,1,1,true,true,true);assert_eq!(input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true).item.fill,0x8c778899);input.action_on(true);input.action_focus(true);let active=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert_eq!(active.item.fill,0xff778899);assert!(active.item.tile&&active.item.active&&active.item.focused&&active.item.close&&active.item.close_visible);assert_eq!((active.item.x,active.item.y,active.item.width,active.item.height),(3.0,5.0,200.0,34.0));
    }
    #[test] fn tab_properties_model_survives_backend_reindex_and_readmission_releases_old_text(){
        let (mut g,input)=projected_tab();let id=input.action_node_id();g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,99),sidebar_tab(2,101)]);assert_eq!(input.sidebar_title(),"Żółć 日本語 🦀".encode_utf16().collect::<Vec<_>>());assert_eq!(g.tab_target(7,1,id,false).index,99);g.reconcile_sidebar(vec![],vec![]);assert!(input.sidebar_title().is_empty());g.reconcile_sidebar(vec![],vec![sidebar_tab(1,0)]);let next=g.acquire(&node_tab(1));assert_ne!(next.action_node_id(),id);assert!(!next.sidebar_visual().valid);assert!(!next.sidebar_presentation().valid);
    }
    #[test] fn tab_properties_close_shape_intersects_rust_cardinality_and_native_presence(){
        let (mut g,input)=projected_tab();assert!(!input.sidebar_scene(tab_box(),tab_box(),tab_style(),false,true).item.close_visible);g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,4)]);let single=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert!(single.valid);assert!(!single.item.close&&!single.item.close_visible);g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(2,41)]);let multiple=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);assert!(multiple.item.close&&multiple.item.close_visible);
    }
    #[test] fn tab_properties_scene_recipe_uses_stored_title_monogram_and_reserved_close_area(){
        let (_g,input)=projected_tab();let scene=input.sidebar_scene(tab_box(),tab_box(),tab_style(),true,true);let ops=layout(&[scene.item]);let title=ops.iter().find(|op|op.text=="Żółć 日本語 🦀").unwrap();assert_eq!(title.x,39.0);assert_eq!(title.width,132.0);assert!(ops.iter().any(|op|op.text=="Z"));assert!(ops.iter().any(|op|op.text=="×"));
    }
    fn sidebar_tab(low:u64,index:usize)->ffi::SidebarTabInput{ffi::SidebarTabInput{high:7,low,index,pin:String::new()}}
    fn tab_scene_fixture()->Box<ActionGraph>{let mut g=new_action_graph();assert!(g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(2,41)]).admitted);g}
    #[test] fn sidebar_scene_owns_collection_parents_tab_payload_and_close_edges(){
        let mut g=tab_scene_fixture();let a=g.acquire(&node_tab(1));let close=g.acquire(&node_close(1));let pin=g.acquire(&node_named(2,"folder"));let root=g.nodes[&(11,0,0,"sidebar.tabs".into())].borrow().id;let pins=g.nodes[&(11,0,0,"sidebar.pins".into())].borrow().id;assert_ne!(root,pins);assert_eq!(a.node_parent_id(),root);assert_eq!(pin.node_parent_id(),pins);assert_eq!(close.node_parent_id(),a.action_node_id());assert_eq!(a.node_model_kind(),9);assert_eq!(a.tab_binding().index,4);assert!(a.tab_binding().can_close);assert!(!pin.tab_binding().valid&&!close.tab_binding().valid);assert_eq!(g.acquire(&node_named(11,"sidebar.tabs")).action_node_id(),0);
    }
    #[test] fn sidebar_targets_use_sparse_backend_indices_and_retarget_surviving_identity(){
        let mut g=tab_scene_fixture();let a=g.acquire(&node_tab(2));let _close=g.acquire(&node_close(2));let id=a.action_node_id();assert_eq!(g.tab_target(7,2,id,false).index,41);assert!(g.tab_target(7,2,id,true).valid);g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(2,40)]);assert_eq!(a.action_node_id(),id);assert_eq!(a.tab_binding().index,40);assert_eq!(g.tab_target(7,2,id,false).index,40);assert!(!g.tab_target(7,2,id,true).valid);g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(2,99)]);assert_eq!(g.tab_target(7,2,id,false).index,99);
    }
    #[test] fn sidebar_close_cardinality_retires_and_recreates_only_close_identity(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![],vec![sidebar_tab(1,8)]);let a=g.acquire(&node_tab(1));let id=a.action_node_id();assert!(!a.tab_binding().can_close);g.reconcile_sidebar(vec![],vec![sidebar_tab(1,8),sidebar_tab(2,12)]);let close=g.acquire(&node_close(1));let close_id=close.action_node_id();assert!(a.tab_binding().can_close&&g.tab_target(7,1,id,true).valid);g.reconcile_sidebar(vec![],vec![sidebar_tab(1,8)]);assert_eq!(a.action_node_id(),id);assert_eq!(close.action_node_id(),0);g.reconcile_sidebar(vec![],vec![sidebar_tab(1,8),sidebar_tab(2,12)]);let next=g.acquire(&node_close(1));assert_ne!(next.action_node_id(),close_id);assert_eq!(a.action_node_id(),id);assert!(g.tab_target(7,1,id,true).valid);
    }
    #[test] fn sidebar_invalid_identity_pin_and_source_collisions_reject_atomically(){
        let mut g=tab_scene_fixture();let mut a=g.acquire(&node_tab(1));a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let before=g.order.clone();for (pins,tabs) in [(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(1,41)]),(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(2,4)]),(vec!["folder".into()],vec![ffi::SidebarTabInput{high:0,low:0,index:0,pin:String::new()}]),(vec!["".into()],vec![sidebar_tab(1,4)]),(vec!["folder".into(),"folder".into()],vec![sidebar_tab(1,4)])]{assert!(!g.reconcile_sidebar(pins,tabs).admitted);assert_eq!(g.order,before);assert!(a.action_pending());assert_eq!(a.tab_binding().index,4);}
    }
    #[test] fn sidebar_identical_projection_preserves_input_focus_selection_and_epoch(){
        let mut g=tab_scene_fixture();let mut a=g.acquire(&node_tab(1));a.action_focus(true);a.action_on(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=a.action_admission_epoch();let id=a.action_node_id();let p=g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(2,41)]);assert_eq!((p.created,p.retired),(0,0));assert!(a.action_pending()&&a.action_snapshot().focused&&a.action_snapshot().on&&a.action_admits(epoch));assert_eq!(a.action_node_id(),id);
    }
    #[test] fn sidebar_membership_order_and_index_changes_cancel_all_retained_input(){
        let mut g=tab_scene_fixture();let mut a=g.acquire(&node_tab(1));let mut close=g.acquire(&node_close(2));a.action_focus(true);a.action_on(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);close.action_key(true,1,false,true);assert!(close.action_pending());let epoch=a.action_admission_epoch();g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(2,41),sidebar_tab(1,4)]);assert!(!a.action_pending()&&!close.action_pending()&&!a.action_admits(epoch));assert!(a.action_snapshot().focused&&a.action_snapshot().on);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(2,41),sidebar_tab(1,5)]);assert!(!a.action_pending());assert_eq!(a.tab_binding().index,5);
    }
    #[test] fn sidebar_targets_gate_live_main_and_close_leases_enabled_and_current_ids(){
        let mut g=tab_scene_fixture();let id=g.nodes[&(1,7,1,String::new())].borrow().id;assert!(!g.tab_target(7,1,id,false).valid);let mut a=g.acquire(&node_tab(1));assert!(g.tab_target(7,1,id,false).valid);assert!(!g.tab_target(7,1,id,true).valid);let mut close=g.acquire(&node_close(1));assert!(g.tab_target(7,1,id,true).valid);close.action_enabled(false);assert!(!g.tab_target(7,1,id,true).valid);assert!(g.tab_target(7,1,id,false).valid);close.action_enabled(true);a.action_enabled(false);assert!(!g.tab_target(7,1,id,false).valid&&!g.tab_target(7,1,id,true).valid);a.action_enabled(true);assert!(!g.tab_target(7,1,0,false).valid&&!g.tab_target(7,1,id+1,false).valid&&!g.tab_target(7,99,id,false).valid);drop(close);assert!(!g.tab_target(7,1,id,true).valid);drop(a);assert!(!g.tab_target(7,1,id,false).valid);
    }
    #[test] fn sidebar_full_128_bit_identity_does_not_alias_equal_low_words(){
        let mut g=new_action_graph();g.reconcile_sidebar(vec![],vec![ffi::SidebarTabInput{high:7,low:1,index:5,pin:String::new()},ffi::SidebarTabInput{high:8,low:1,index:99,pin:String::new()}]);let a=g.acquire(&node_tab(1));let other=ffi::ActionNodeKey{kind:1,high:8,low:1,name:String::new()};let b=g.acquire(&other);assert_ne!(a.action_node_id(),b.action_node_id());assert_eq!((a.tab_binding().high,b.tab_binding().high),(7,8));assert_eq!(g.tab_target(8,1,b.action_node_id(),false).index,99);assert!(!g.tab_target(8,1,a.action_node_id(),false).valid);
    }
    #[test] fn sidebar_replacement_lease_revokes_old_binding_without_old_drop_mutating_new(){
        let mut g=tab_scene_fixture();let old=g.acquire(&node_tab(1));let id=old.action_node_id();let mut next=g.acquire(&node_tab(1));assert!(!old.tab_binding().valid);assert_eq!(next.tab_binding().id,id);next.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);drop(old);assert!(next.action_pending());assert!(g.owns_input(&next));
    }
    #[test] fn sidebar_parent_orphan_rejects_and_whole_scene_retirement_drops_payloads(){
        let mut g=tab_scene_fixture();let a=g.acquire(&node_tab(1));let record=if let ActionStorage::Shared{node,..}=&a.storage{node.clone()}else{panic!("missing record")};let before=g.order.clone();assert!(!g.reconcile(vec![node_tab(1),node_close(1)]).admitted);assert_eq!(g.order,before);assert!(g.reconcile(vec![]).admitted);assert!(!a.tab_binding().valid);assert!(record.borrow().model.is_none());assert_eq!(a.node_parent_id(),0);
    }
    #[test] fn sidebar_empty_collections_keep_roots_and_foreign_owner_never_matches(){
        let mut g=tab_scene_fixture();let a=g.acquire(&node_tab(1));let mut foreign=tab_scene_fixture();assert!(!foreign.owns_input(&a));let roots=[g.nodes[&(11,0,0,"sidebar.tabs".into())].borrow().id,g.nodes[&(11,0,0,"sidebar.pins".into())].borrow().id];let empty=g.reconcile_sidebar(vec![],vec![]);assert_eq!(empty.ids,roots);assert!(!a.tab_binding().valid);assert_eq!(g.reconcile_sidebar(vec![],vec![]).created,0);let b=foreign.acquire(&node_tab(1));drop(foreign);assert!(!b.tab_binding().valid);
    }
    #[test] fn sidebar_reconciliation_rejects_typed_payload_collisions_for_rows_pins_and_close(){
        for key in [node_tab(1),node_close(1),node_named(2,"folder")]{let mut g=new_action_graph();g.reconcile(vec![node_tab(1),node_close(1),node_named(2,"folder")]);let mut typed=g.acquire(&key);assert!(typed.node_checkbox_init(true));let before=g.order.clone();assert!(!g.reconcile_sidebar(vec!["folder".into()],vec![sidebar_tab(1,4),sidebar_tab(2,41)]).admitted);assert_eq!(g.order,before);assert!(typed.node_checked());}
    }
    #[test] fn panel_categories_bind_indices_from_the_owning_form_count(){
        let form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);for category in 0..10{let mut input=graph.append_form_action();assert!(form.bind_category(&mut input,category));assert_eq!(input.node_model_kind(),8);let command=input.node_form_command();assert_eq!(command.command,ffi::FormCommand::PreferenceCategory);assert_eq!(command.index,category);assert!(command.key.is_empty());let view=input.node_form_view();assert!(view.bound&&!view.dismiss);assert_eq!(view.index,category);}
    }
    #[test] fn panel_categories_reject_out_of_range_and_unbound_command_bypass(){
        let form=new_preferences_form(3);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();for category in [3,usize::MAX]{assert!(!form.bind_category(&mut input,category));assert!(!input.node_form_view().bound);}assert!(!new_preferences_form(0).bind_category(&mut input,0));assert!(!input.node_form_command_init(ffi::FormCommand::PreferenceCategory,""));assert!(!input.node_form_command_init(ffi::FormCommand::PreferenceCategory,"1"));assert!(form.bind_category(&mut input,2));assert!(!form.bind_category(&mut input,3));assert_eq!(input.node_form_command().index,2);
    }
    #[test] fn panel_category_targets_are_immutable_and_independent_of_selection(){
        let mut form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();assert!(form.bind_category(&mut input,7));assert!(form.bind_category(&mut input,7));assert!(!form.bind_category(&mut input,2));assert!(form.preference_select(4));assert_eq!(input.node_form_command().index,7);assert!(!input.node_form_command_init(ffi::FormCommand::PreferencesDismiss,""));assert_eq!(input.node_form_command().command,ffi::FormCommand::PreferenceCategory);
    }
    #[test] fn panel_category_presentation_survives_disabling_without_admitting_actions(){
        let form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();form.bind_category(&mut input,6);input.action_enabled(false);assert_eq!(input.node_form_command().command,ffi::FormCommand::None);let view=input.node_form_view();assert!(view.bound&&!view.dismiss);assert_eq!(view.index,6);input.action_enabled(true);assert_eq!(input.node_form_command().index,6);
    }
    #[test] fn panel_dismiss_commands_share_rust_presentation_and_keep_distinct_targets(){
        let mut graph=new_form_graph(false,0,false,false);for command in [ffi::FormCommand::WorkspaceDismiss,ffi::FormCommand::PreferencesDismiss,ffi::FormCommand::ImportDismiss,ffi::FormCommand::ToolsDismiss]{let mut input=graph.append_form_action();assert!(input.node_form_command_init(command,""));assert_eq!(input.node_form_command().command,command);assert!(input.node_form_view().bound&&input.node_form_view().dismiss);input.action_enabled(false);assert!(input.node_form_view().dismiss);assert_eq!(input.node_form_command().command,ffi::FormCommand::None);assert!(!input.node_form_command_init(command,"unexpected"));}
    }
    #[test] fn panel_commands_retirement_releases_category_presentation_and_payload(){
        let form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();form.bind_category(&mut input,8);let epoch=input.action_admission_epoch();assert!(graph.retire_form_action(input.action_node_id()));assert!(!input.node_form_view().bound);assert_eq!(input.node_form_command().command,ffi::FormCommand::None);assert!(!input.action_admits(epoch));assert!(!form.bind_category(&mut input,8));
    }
    #[test] fn panel_presentation_reads_preserve_pending_input_and_admission_epoch(){
        let form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();form.bind_category(&mut input,2);input.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let epoch=input.action_admission_epoch();for _ in 0..32{let view=input.node_form_view();assert!(view.bound);assert_eq!(view.index,2);}assert!(input.action_pending()&&input.action_admits(epoch));assert!(input.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action==ffi::RowAction::Activate);
    }
    #[test] fn panel_category_binding_rejects_wrong_typed_and_non_form_models(){
        let form=new_preferences_form(10);let mut graph=new_form_graph(true,6,false,false);let mut input=graph.append_form_action();input.node_toggle_init();assert!(!form.bind_category(&mut input,0));assert_eq!(input.node_model_kind(),2);assert!(!input.node_form_view().bound);let mut standalone=new_action_control();assert!(!form.bind_category(&mut standalone,0));let mut other=new_action_graph();other.reconcile(vec![node_tab(1)]);assert!(!form.bind_category(&mut other.acquire(&node_tab(1)),0));
    }
    #[test] fn panel_dismiss_owner_drop_and_context_cancellation_revoke_dispatch(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();input.node_form_command_init(ffi::FormCommand::ImportDismiss,"");let epoch=input.action_admission_epoch();input.action_cancel();assert!(!input.action_admits(epoch));assert!(input.node_form_view().dismiss);drop(graph);assert!(!input.node_form_view().bound);assert_eq!(input.node_form_command().command,ffi::FormCommand::None);
    }
    fn queued_form_command(command:ffi::FormCommand)->(Box<ActionGraph>,Box<ActionControl>,Box<FormCommandTicket>){let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();assert!(input.node_form_command_init(command,""));let ticket=input.defer_form_command();(graph,input,ticket)}
    #[test] fn deferred_form_command_is_single_use_and_consumes_siblings(){
        let (_graph,input,mut ticket)=queued_form_command(ffi::FormCommand::PreferenceMore);let mut sibling=input.defer_form_command();let epoch=input.action_admission_epoch();assert_eq!(ticket.take_command().command,ffi::FormCommand::PreferenceMore);assert!(!input.action_admits(epoch));assert_eq!(ticket.take_command().command,ffi::FormCommand::None);assert_eq!(sibling.take_command().command,ffi::FormCommand::None);assert_eq!(input.defer_form_command().take_command().command,ffi::FormCommand::PreferenceMore);
    }
    #[test] fn deferred_form_command_idle_cancel_rejects_return_to_same_context(){
        let (_graph,mut input,mut ticket)=queued_form_command(ffi::FormCommand::PreferenceMore);assert!(!input.action_cancel());assert_eq!(input.node_form_command().command,ffi::FormCommand::PreferenceMore);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);
    }
    #[test] fn deferred_form_command_disable_reenable_and_terminal_epoch_fail_closed(){
        let (_graph,mut input,mut ticket)=queued_form_command(ffi::FormCommand::ImportApply);input.action_enabled(false);input.action_enabled(true);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);input.write(|state|state.admission_epoch=u64::MAX);let mut ticket=input.defer_form_command();input.action_cancel();assert_eq!(ticket.take_command().command,ffi::FormCommand::None);assert_eq!(input.defer_form_command().take_command().command,ffi::FormCommand::None);
    }
    #[test] fn deferred_form_command_rejects_retired_and_replaced_input_leases(){
        let (mut graph,input,mut ticket)=queued_form_command(ffi::FormCommand::ProfileDelete);let id=input.action_node_id();let node=graph.nodes[&(9,id,0,String::new())].clone();node.borrow_mut().lease+=1;let replacement=ActionControl{storage:ActionStorage::Shared{node,lease:2}};drop(input);assert_eq!(replacement.action_node_id(),id);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);let mut ticket=replacement.defer_form_command();graph.retire_form_action(id);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);
    }
    #[test] fn deferred_form_command_graph_drop_and_same_numeric_ids_are_isolated(){
        let (graph,input,mut ticket)=queued_form_command(ffi::FormCommand::WorkspaceSave);let (_other_graph,other,mut other_ticket)=queued_form_command(ffi::FormCommand::WorkspaceSave);assert_eq!(input.action_node_id(),other.action_node_id());drop(graph);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);assert_eq!(other_ticket.take_command().command,ffi::FormCommand::WorkspaceSave);
    }
    #[test] fn deferred_form_command_ticket_does_not_retain_node_or_model(){
        let (graph,input,mut ticket)=queued_form_command(ffi::FormCommand::ProfileRename);let weak=ticket.source.as_ref().unwrap().0.clone();assert_eq!(weak.strong_count(),2);drop(input);drop(graph);assert_eq!(weak.strong_count(),0);assert_eq!(ticket.take_command().command,ffi::FormCommand::None);
    }
    #[test] fn deferred_form_command_rejects_unbound_standalone_and_other_models(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();assert_eq!(input.defer_form_command().take_command().command,ffi::FormCommand::None);input.node_checkbox_init(true);assert_eq!(input.defer_form_command().take_command().command,ffi::FormCommand::None);assert_eq!(new_action_control().defer_form_command().take_command().command,ffi::FormCommand::None);
    }
    #[test] fn deferred_form_command_preserves_category_and_preference_key_without_borrow(){
        let mut graph=new_form_graph(false,0,false,false);let mut input=graph.append_form_action();input.node_form_command_init(ffi::FormCommand::PreferenceText,"font_standard");let result=input.defer_form_command().take_command();assert_eq!(result.key,"font_standard");assert_eq!(result.panel,ffi::FormPanel::Preferences);assert_eq!(input.node_form_command().key,"font_standard");let mut category=graph.append_form_action();new_preferences_form(10).bind_category(&mut category,7);assert_eq!(category.defer_form_command().take_command().index,7);
    }
    #[test] fn form_command_panels_cover_workspace_profile_and_import_actions(){
        use ffi::{FormCommand as C,FormPanel as P};let mut g=new_form_graph(false,0,false,false);
        for (panel,commands) in [(P::Workspace,vec![C::WorkspaceSave,C::WorkspaceToggle,C::WorkspaceRemove,C::WorkspaceProfiles,C::WorkspaceImport,C::WorkspaceDismiss]),(P::Profiles,vec![C::ProfileCreate,C::ProfileRename,C::ProfileDelete,C::ProfileBack]),(P::Import,vec![C::ImportApply,C::ImportDismiss]),(P::Tools,vec![C::ToolsDismiss])]{for command in commands{let mut input=g.append_form_action();assert!(input.node_form_command_init(command,""));assert_eq!(input.node_form_command().panel,panel);}}
        let mut keyed=g.append_form_action();assert!(!keyed.node_form_command_init(C::ToolsDismiss,"page"));assert_eq!(keyed.node_model_kind(),0);
    }
    #[test] fn preference_command_panel_includes_navigation_and_dynamic_pagination(){
        use ffi::{FormCommand as C,FormPanel as P};let mut g=new_form_graph(false,0,false,false);
        for command in [C::PreferenceSite,C::PreferenceClear,C::PreferenceDirectory,C::History,C::Workspace,C::Passwords,C::Profiles,C::Import,C::Proxy,C::Certificates,C::Providers,C::Shortcuts,C::TimeMachine,C::PreferencesDismiss,C::PreferenceMore]{let mut input=g.append_form_action();assert!(input.node_form_command_init(command,""));assert_eq!(input.node_form_command().panel,P::Preferences);}
        let mut input=g.append_form_action();assert!(input.node_form_command_init(C::PreferenceText,"dns_templates"));assert_eq!(input.node_form_command().panel,P::Preferences);let mut category=g.append_form_action();assert!(new_preferences_form(10).bind_category(&mut category,9));assert_eq!(category.node_form_command().panel,P::Preferences);
    }
    #[test] fn form_panel_is_revoked_with_input_admission(){
        use ffi::{FormCommand as C,FormPanel as P};let mut g=new_form_graph(false,0,false,false);let mut input=g.append_form_action();assert_eq!(input.node_form_command().panel,P::None);assert!(input.node_form_command_init(C::ProfileCreate,""));input.action_enabled(false);assert_eq!(input.node_form_command().panel,P::None);input.action_enabled(true);assert_eq!(input.node_form_command().panel,P::Profiles);g.retire_form_action(input.action_node_id());assert_eq!(input.node_form_command().panel,P::None);
    }
    #[test] fn pagination_command_rejects_retargeting_and_cancels_on_retirement(){
        use ffi::FormCommand as C;let mut g=new_form_graph(false,0,false,false);let mut input=g.append_form_action();assert!(!input.node_form_command_init(C::PreferenceMore,"profile"));assert!(input.node_form_command_init(C::PreferenceMore,""));assert!(!input.node_form_command_init(C::ImportApply,""));input.action_key(true,1,false,true);assert!(input.action_pending());g.retire_form_action(input.action_node_id());assert!(input.action_key(false,1,false,true).action==ffi::RowAction::None);assert_eq!(input.node_form_command().command,C::None);
    }
    #[test] fn form_commands_own_semantics_and_keys_independently_of_labels(){
        let mut g=new_form_graph(false,8,false,false);let mut a=g.append_form_action();assert!(a.node_form_command_init(ffi::FormCommand::PreferenceText,"accept_languages"));assert_eq!(a.node_model_kind(),8);assert_eq!(a.node_form_command().command,ffi::FormCommand::PreferenceText);assert_eq!(a.node_form_command().key,"accept_languages");assert!(a.node_form_command_init(ffi::FormCommand::PreferenceText,"accept_languages"));assert!(!a.node_form_command_init(ffi::FormCommand::PreferenceText,"dns_templates"));assert!(!a.node_form_command_init(ffi::FormCommand::ProfileDelete,""));assert_eq!(a.node_form_command().key,"accept_languages");
    }
    #[test] fn form_commands_reject_invalid_domains_and_typed_model_collisions(){
        let mut g=new_form_graph(false,0,false,false);let mut a=g.append_form_action();for (c,k) in [(ffi::FormCommand::None,""),(ffi::FormCommand::PreferenceText,""),(ffi::FormCommand::ImportApply,"extra")]{assert!(!a.node_form_command_init(c,k));assert_eq!(a.node_model_kind(),0);}assert!(a.node_checkbox_init(true));assert!(!a.node_form_command_init(ffi::FormCommand::ProfileDelete,""));let mut standalone=new_action_control();assert!(!standalone.node_form_command_init(ffi::FormCommand::ProfileDelete,""));let mut other=new_action_graph();other.reconcile(vec![node_tab(1)]);let mut tab=other.acquire(&node_tab(1));assert!(!tab.node_form_command_init(ffi::FormCommand::ImportApply,""));
    }
    #[test] fn form_commands_cover_all_production_semantics(){
        use ffi::FormCommand as C;let commands=[C::WorkspaceSave,C::WorkspaceToggle,C::WorkspaceRemove,C::WorkspaceProfiles,C::WorkspaceImport,C::ProfileCreate,C::ProfileRename,C::ProfileDelete,C::ProfileBack,C::ImportApply,C::PreferenceSite,C::PreferenceClear,C::PreferenceDirectory,C::History,C::Workspace,C::Passwords,C::Profiles,C::Import,C::Proxy,C::Certificates,C::Providers,C::Shortcuts,C::TimeMachine];let mut g=new_form_graph(false,0,false,false);for command in commands{let mut a=g.append_form_action();assert!(a.node_form_command_init(command,""));assert_eq!(a.node_form_command().command,command);assert!(a.node_form_command().key.is_empty());}
    }
    #[test] fn form_commands_disabled_retired_and_owner_drop_fail_closed(){
        let mut g=new_form_graph(false,0,false,false);let mut a=g.append_form_action();a.node_form_command_init(ffi::FormCommand::ProfileDelete,"");let id=a.action_node_id();a.action_enabled(false);assert_eq!(a.node_form_command().command,ffi::FormCommand::None);a.action_enabled(true);assert_eq!(a.node_form_command().command,ffi::FormCommand::ProfileDelete);assert!(g.retire_form_action(id));assert_eq!(a.node_form_command().command,ffi::FormCommand::None);let mut b=g.append_form_action();b.node_form_command_init(ffi::FormCommand::ImportApply,"");drop(g);assert_eq!(b.node_form_command().command,ffi::FormCommand::None);
    }
    #[test] fn form_admission_cancellation_invalidates_even_an_idle_activation(){
        let mut g=new_form_graph(false,0,false,false);let mut a=g.append_form_action();let epoch=a.action_admission_epoch();assert!(a.action_admits(epoch));assert!(!a.action_cancel());assert!(!a.action_admits(epoch));assert!(a.action_admits(a.action_admission_epoch()));assert!(!a.action_admits(0));
    }
    #[test] fn form_admission_disable_reenable_and_focus_loss_do_not_resurrect_events(){
        let mut a=new_action_control();let epoch=a.action_admission_epoch();a.action_enabled(false);a.action_enabled(true);assert!(!a.action_admits(epoch));let epoch=a.action_admission_epoch();a.action_focus(false);assert!(!a.action_admits(epoch));
    }
    #[test] fn form_admission_read_projection_preserves_valid_events(){
        let mut g=new_form_graph(false,8,false,false);let mut a=g.append_form_action();a.node_form_command_init(ffi::FormCommand::WorkspaceSave,"");let epoch=a.action_admission_epoch();a.action_snapshot();a.node_form_command();g.form_scene();g.form_style();assert!(a.action_admits(epoch));g.reorder_form_actions(vec![a.action_node_id()]);assert!(a.action_admits(epoch));
    }
    #[test] fn form_admission_reorder_cancels_but_preserves_semantic_model(){
        let mut g=new_form_graph(false,8,false,false);let mut a=g.append_form_action();let b=g.append_form_action();a.node_form_command_init(ffi::FormCommand::ProfileRename,"");let epoch=a.action_admission_epoch();assert!(g.reorder_form_actions(vec![b.action_node_id(),a.action_node_id()]));assert!(!a.action_admits(epoch));assert_eq!(a.node_form_command().command,ffi::FormCommand::ProfileRename);
    }
    #[test] fn form_admission_retirement_and_epoch_exhaustion_never_revalidate(){
        let mut g=new_form_graph(false,0,false,false);let a=g.append_form_action();let epoch=a.action_admission_epoch();g.retire_form_action(a.action_node_id());assert_eq!(a.action_admission_epoch(),0);assert!(!a.action_admits(epoch));let mut b=new_action_control();b.write(|s|s.admission_epoch=u64::MAX);b.action_cancel();assert_eq!(b.action_admission_epoch(),0);b.action_cancel();assert!(!b.action_admits(0));assert!(!b.action_admits(1));
    }
    fn site_rule(key:&str,detail:&str,editable:bool,source_index:usize)->ffi::SiteRuleSceneInput{ffi::SiteRuleSceneInput{key:key.into(),detail:detail.encode_utf16().collect(),editable,source_index}}
    fn site_tree()->Box<ActionGraph>{let mut g=new_action_graph();assert!(g.reconcile_site_rules(vec![site_rule("a","First",true,3),site_rule("b","Managed",false,7)]).admitted);g}
    #[test] fn site_rule_tree_owns_parent_order_copy_editability_and_scope(){
        let mut g=site_tree();let tree=g.site_rule_scene();assert_ne!(tree.root,0);assert_eq!(tree.children.len(),2);assert!(tree.children.iter().all(|n|n.parent==tree.root));assert!(tree.children[0].editable&&!tree.children[1].editable);assert_eq!(tree.children[0].detail,"First".encode_utf16().collect::<Vec<_>>());let a=g.acquire(&node_named(13,"a"));assert_eq!(a.node_model_kind(),7);assert_eq!(a.node_parent_id(),tree.root);assert!(g.owns_input(&a));assert_eq!(g.acquire(&node_named(11,"preferences.site-rules")).action_node_id(),0);
    }
    #[test] fn site_rule_target_resolves_backend_index_after_display_order_and_source_shift(){
        let mut g=site_tree();let a=g.acquire(&node_named(13,"a"));let id=a.action_node_id();assert_eq!(g.site_rule_target("a",id).index,3);assert!(g.site_rule_target("a",id).valid);g.reconcile_site_rules(vec![site_rule("b","Managed",false,0),site_rule("a","Updated",true,9)]);assert_eq!(g.site_rule_target("a",id).index,9);assert_eq!(g.site_rule_scene().children[1].id,id);assert!(g.site_rule_target("a",id).valid);
    }
    #[test] fn site_rule_target_rejects_managed_unbound_disabled_missing_and_stale_nodes(){
        let mut g=site_tree();let raw=g.site_rule_scene();assert!(!g.site_rule_target("a",raw.children[0].id).valid);let mut a=g.acquire(&node_named(13,"a"));let b=g.acquire(&node_named(13,"b"));let id=a.action_node_id();assert!(!g.site_rule_target("b",b.action_node_id()).valid);assert!(!g.site_rule_target("missing",id).valid);assert!(!g.site_rule_target("a",0).valid);assert!(!g.site_rule_target("b",id).valid);a.action_enabled(false);assert!(!g.site_rule_target("a",id).valid);a.action_enabled(true);g.reconcile_site_rules(vec![site_rule("a","Managed now",false,3)]);assert!(!g.site_rule_target("a",id).valid);g.reconcile_site_rules(vec![]);assert!(!g.site_rule_target("a",id).valid);
    }
    #[test] fn site_rule_identical_projection_preserves_node_lease_and_pending_input(){
        let mut g=site_tree();let mut a=g.acquire(&node_named(13,"a"));let id=a.action_node_id();a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let plan=g.reconcile_site_rules(vec![site_rule("a","First",true,3),site_rule("b","Managed",false,7)]);assert_eq!((plan.created,plan.retired),(0,0));assert_eq!(a.action_node_id(),id);assert!(a.action_pending()&&a.action_snapshot().focused);assert!(g.site_rule_scene().children.iter().all(|n|!n.cancel_input));
    }
    #[test] fn site_rule_batch_changes_cancel_sibling_input_preserve_values_ids_and_focus(){
        let mut g=site_tree();let mut a=g.acquire(&node_named(13,"a"));let id=a.action_node_id();a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);g.reconcile_site_rules(vec![site_rule("a","First",true,3),site_rule("b","Changed policy",true,7)]);assert!(!a.action_pending());assert!(a.action_snapshot().focused);assert_eq!(a.action_node_id(),id);assert!(g.site_rule_scene().children.iter().all(|n|n.cancel_input));
    }
    #[test] fn site_rule_invalid_keys_sources_and_model_kinds_reject_atomically(){
        let mut g=site_tree();let mut a=g.acquire(&node_named(13,"a"));a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let before=g.site_rule_scene();for items in [vec![site_rule("a","Changed",false,1),site_rule("a","Duplicate",true,2)],vec![site_rule("a","Changed",false,1),site_rule("c","Same source",true,1)],vec![site_rule("","Empty",true,0)]]{assert!(!g.reconcile_site_rules(items).admitted);assert!(a.action_pending());assert_eq!(g.site_rule_scene().children[0].detail,before.children[0].detail);assert_eq!(g.site_rule_target("a",a.action_node_id()).index,3);}let mut other=new_action_graph();other.reconcile(vec![node_named(13,"a")]);let mut checked=other.acquire(&node_named(13,"a"));checked.node_checkbox_init(true);assert!(!other.reconcile_site_rules(vec![site_rule("a","First",true,0)]).admitted);assert!(checked.node_checked());
    }
    #[test] fn site_rule_hidden_removal_releases_payload_and_reappearance_has_new_id(){
        let mut g=site_tree();let a=g.acquire(&node_named(13,"a"));let id=a.action_node_id();let record=if let ActionStorage::Shared{node,..}=&a.storage{node.clone()}else{panic!("record missing")};g.reconcile_site_rules(vec![site_rule("b","Managed",false,7)]);assert_eq!(a.action_node_id(),0);assert_eq!(a.node_parent_id(),0);assert!(record.borrow().model.is_none());g.reconcile_site_rules(vec![site_rule("a","First",true,3),site_rule("b","Managed",false,7)]);assert_ne!(g.site_rule_scene().children[0].id,id);assert!(!g.site_rule_target("a",id).valid);
    }
    #[test] fn site_rule_replacement_lease_invalidates_old_adapter_but_keeps_projection(){
        let mut g=site_tree();let mut old=g.acquire(&node_named(13,"a"));let id=old.action_node_id();let current=g.acquire(&node_named(13,"a"));assert_eq!(current.action_node_id(),id);assert!(!g.owns_input(&old));assert!(g.owns_input(&current));assert_eq!(old.action_node_id(),0);assert!(!old.action_key(true,1,true,true).handled);assert_eq!(g.site_rule_target("a",id).index,3);drop(old);assert!(g.owns_input(&current));
    }
    #[test] fn site_rule_snapshots_own_raw_utf16_and_roots_survive_empty_results(){
        let mut g=new_action_graph();let mut item=site_rule("a","日本語 🦀",true,usize::MAX);item.detail.push(0xd800);let expected=item.detail.clone();g.reconcile_site_rules(vec![item]);let a=g.acquire(&node_named(13,"a"));let id=a.action_node_id();let mut snapshot=g.site_rule_scene();let root=snapshot.root;assert_eq!(snapshot.children[0].detail,expected);snapshot.children[0].detail.clear();assert_eq!(g.site_rule_scene().children[0].detail,expected);assert_eq!(g.site_rule_target("a",id).index,usize::MAX);g.reconcile_site_rules(vec![]);assert_eq!(g.site_rule_scene().root,root);assert!(g.site_rule_scene().children.is_empty());assert_eq!(a.node_model_kind(),0);
    }
    #[test] fn site_rule_parent_graph_retirement_and_equal_id_foreign_scopes_are_fenced(){
        let mut left=site_tree();let mut right=site_tree();let mut a=left.acquire(&node_named(13,"a"));let b=right.acquire(&node_named(13,"a"));assert_eq!(a.action_node_id(),b.action_node_id());assert_eq!(a.node_parent_id(),b.node_parent_id());assert!(!left.owns_input(&b)&&!right.owns_input(&a));assert!(!left.reconcile(vec![node_named(13,"a")]).admitted);drop(left);assert_eq!(a.node_parent_id(),0);assert_eq!(a.node_model_kind(),0);assert!(!a.action_key(true,1,true,true).handled);assert!(right.owns_input(&b));
    }
    #[test] fn form_root_owns_parent_order_style_and_is_not_interactive(){
        let mut g=new_form_graph(true,8,true,false);let a=g.append_form_action();let b=g.append_form_action();let scene=g.form_scene();assert_ne!(scene.root,0);assert_eq!(scene.children,vec![a.action_node_id(),b.action_node_id()]);assert_eq!(a.node_parent_id(),scene.root);assert_eq!(b.node_parent_id(),scene.root);let style=g.form_style();assert!(style.vertical&&style.intrinsic&&!style.dialog&&!style.shared);assert_eq!(style.spacing,8);let root=ffi::ActionNodeKey{kind:12,high:0,low:0,name:String::new()};assert_eq!(g.acquire(&root).action_node_id(),0);assert!(!g.retire_form_action(scene.root));
    }
    #[test] fn form_append_reserves_root_and_member_atomically_near_id_exhaustion(){
        let mut g=new_action_graph();g.next_id=u64::MAX-1;assert_eq!(g.append_form_action().action_node_id(),0);assert!(g.nodes.is_empty()&&g.order.is_empty());g.next_id=u64::MAX-2;let a=g.append_form_action();let scene=g.form_scene();assert_eq!(scene.root,u64::MAX-2);assert_eq!(a.action_node_id(),u64::MAX-1);assert_eq!(g.append_form_action().action_node_id(),0);assert_eq!(g.form_scene().children,scene.children);assert!(g.retire_form_action(a.action_node_id()));assert_eq!(g.form_scene().root,scene.root);
    }
    #[test] fn form_reorder_preserves_models_focus_and_cancels_input(){
        let mut g=new_form_graph(false,12,false,false);let mut a=g.append_form_action();let mut b=g.append_form_action();a.node_checkbox_init(true);b.node_select_init(keyed_options(&[("a","Alpha"),("b","Beta")]));let token=b.node_select_open(true);b.node_select_input("b",10,true);a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let aid=a.action_node_id();let bid=b.action_node_id();assert!(g.reorder_form_actions(vec![bid,aid]));assert_eq!(g.form_scene().children,vec![bid,aid]);assert!(a.node_checked()&&a.action_snapshot().focused);assert!(!a.action_pending());assert!(!b.node_select_expanded());assert_eq!(b.node_select_selected_key(),"b");assert!(!b.node_select_choose_key(token,"a",true));assert!(b.node_select_input("a",11,true));assert_eq!(b.node_select_selected_key(),"a");
    }
    #[test] fn form_invalid_reorder_is_atomic_and_identical_order_keeps_press(){
        let mut g=new_form_graph(false,0,false,false);let mut a=g.append_form_action();let b=g.append_form_action();let before=g.form_scene();a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);for ids in [vec![],vec![a.action_node_id(),a.action_node_id()],vec![before.root,b.action_node_id()],vec![a.action_node_id(),u64::MAX]]{assert!(!g.reorder_form_actions(ids));assert_eq!(g.form_scene().children,before.children);assert!(a.action_pending());}assert!(g.reorder_form_actions(before.children));assert!(a.action_pending());
    }
    #[test] fn form_layout_matches_legacy_recipe_across_style_bounds_and_saturation(){
        for vertical in [false,true]{for intrinsic in [false,true]{for dialog in [false,true]{for spacing in [-3,0,8,i32::MAX]{let mut g=new_form_graph(vertical,spacing,intrinsic,dialog);let handles=(0..3).map(|_|g.append_form_action()).collect::<Vec<_>>();let measures=handles.iter().zip([(40,30),(80,50),(10,20)]).map(|(a,(width,height))|ffi::FormMeasure{id:a.action_node_id(),width,height}).collect::<Vec<_>>();let columns=measures.iter().map(|m|ffi::ColumnChild{width:m.width,height:m.height,top:0,left:0,bottom:0,right:0}).collect::<Vec<_>>();for width in [-1,0,1,80,400,i32::MAX]{for height in [-1,0,50,i32::MAX]{for preferred in [false,true]{let actual=g.form_layout(width,height,preferred,90,8,12,10,12,&measures);assert!(actual.admitted);let expected_width=if preferred&&intrinsic{-1}else{width};let expected=if dialog{dialog_actions_layout(expected_width,height,spacing,90,8,12,10,12,&columns)}else{action_group_layout(expected_width,height,vertical,spacing,&columns)};assert_eq!((actual.width,actual.height),(expected.width,expected.height));assert_eq!(actual.children.len(),expected.children.len());for ((cell,rect),measure) in actual.children.iter().zip(expected.children.iter()).zip(measures.iter()){assert_eq!(cell.id,measure.id);assert_eq!((cell.rect.x,cell.rect.y,cell.rect.width,cell.rect.height),(rect.x,rect.y,rect.width,rect.height));}}}}}}}}
    }
    #[test] fn form_layout_accepts_hidden_subsequence_but_rejects_stale_duplicate_or_wrong_order(){
        let mut g=new_form_graph(false,8,false,false);let a=g.append_form_action();let b=g.append_form_action();let c=g.append_form_action();let metric=|id|ffi::FormMeasure{id,width:40,height:30};let layout=g.form_layout(-1,-1,false,80,0,0,0,0,&[metric(a.action_node_id()),metric(c.action_node_id())]);assert!(layout.admitted);assert_eq!(layout.width,88);for ids in [[a.action_node_id(),a.action_node_id()],[c.action_node_id(),a.action_node_id()],[g.form_scene().root,b.action_node_id()],[a.action_node_id(),u64::MAX]]{assert!(!g.form_layout(400,40,false,80,0,0,0,0,&[metric(ids[0]),metric(ids[1])]).admitted);}assert!(g.retire_form_action(a.action_node_id()));assert!(!g.form_layout(400,40,false,80,0,0,0,0,&[metric(a.action_node_id())]).admitted);
    }
    #[test] fn form_shared_layout_retains_style_and_cancels_transient_selector_input(){
        let mut g=new_form_graph(true,12,true,true);let mut a=g.append_form_action();a.node_select_init(keyed_options(&[("a","Alpha"),("b","Beta")]));a.node_select_open(true);a.node_select_select_index(1);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(g.form_set_shared(true));let style=g.form_style();assert!(style.vertical&&style.intrinsic&&style.dialog&&style.shared);assert_eq!(style.spacing,12);assert!(!a.action_pending());assert_eq!(a.node_select_selected_key(),"b");assert!(!g.form_layout(400,40,false,80,0,0,0,0,&[]).admitted);assert!(!g.form_set_shared(true));assert!(g.form_set_shared(false));assert!(g.form_layout(400,40,false,80,0,0,0,0,&[]).admitted);
    }
    #[test] fn form_member_retirement_preserves_root_and_siblings_then_subtree_drop_revokes_all(){
        let mut g=new_form_graph(false,0,false,false);let mut a=g.append_form_action();let mut b=g.append_form_action();a.node_checkbox_init(true);b.node_toggle_init();let root=g.form_scene().root;let aid=a.action_node_id();assert!(g.retire_form_action(aid));assert_eq!(a.node_parent_id(),0);assert_eq!(a.node_model_kind(),0);assert_eq!(b.node_parent_id(),root);assert_eq!(b.node_model_kind(),2);assert!(g.reconcile(vec![]).admitted);assert_eq!(g.form_scene().root,0);assert_eq!(b.node_parent_id(),0);assert!(!b.node_toggle_flip(true,0,true));assert!(!g.form_layout(100,40,false,80,0,0,0,0,&[]).admitted);
    }
    #[test] fn form_bare_append_initializes_a_projected_root_without_touching_other_nodes(){
        let mut g=new_action_graph();assert!(g.reconcile(vec![node_pin("other"),ffi::ActionNodeKey{kind:12,high:0,low:0,name:String::new()}]).admitted);let mut other=g.acquire(&node_pin("other"));other.action_on(true);let a=g.append_form_action();assert_eq!(a.node_parent_id(),g.form_scene().root);assert!(other.action_snapshot().on);assert!(g.form_layout(100,40,false,80,0,0,0,0,&[ffi::FormMeasure{id:a.action_node_id(),width:30,height:20}]).admitted);
    }
    #[test] fn form_scene_snapshots_are_owned_and_roots_have_independent_owners(){
        let mut left=new_form_graph(true,8,true,false);let mut right=new_form_graph(false,0,false,true);let a=left.append_form_action();let b=right.append_form_action();assert_eq!(a.action_node_id(),b.action_node_id());assert_eq!(a.node_parent_id(),b.node_parent_id());assert!(left.owns_input(&a)&&right.owns_input(&b));assert!(!left.owns_input(&b));let mut snapshot=left.form_scene();snapshot.children.clear();assert_eq!(left.form_scene().children.len(),1);assert!(left.form_style().vertical&&!right.form_style().vertical);drop(left);assert_eq!(a.node_parent_id(),0);
    }
    fn palette_item(key:&str,title:&str,icon:u32)->ffi::PaletteSceneInput{ffi::PaletteSceneInput{key:key.into(),title:title.encode_utf16().collect(),detail:"Detail 🦀".encode_utf16().collect(),icon}}
    fn palette_tree()->Box<ActionGraph>{let mut g=new_palette_graph();assert!(g.reconcile_palette(vec![palette_item("a","First",0),palette_item("b","Second",1)]).admitted);g}
    #[test] fn palette_root_owns_selection_and_structural_parent(){
        let mut g=palette_tree();let scene=g.palette_scene();assert_ne!(scene.root,0);assert_eq!(scene.children.len(),2);assert!(scene.children.iter().all(|n|n.parent==scene.root&&n.height==36));assert!(scene.children[0].highlighted&&!scene.children[1].highlighted);let a=g.acquire(&node_named(6,"a"));assert_eq!(a.node_model_kind(),6);assert_eq!(a.node_parent_id(),scene.root);assert_eq!(g.acquire(&node_named(11,"palette.results")).action_node_id(),0);
    }
    #[test] fn palette_graph_selection_matches_legacy_across_steps_clamps_and_reset(){
        let mut g=new_palette_graph();let mut oracle=new_palette_selection();for count in [0,1,4,usize::MAX,2,0]{for backwards in [false,true,false]{assert_eq!(g.palette_step(count,backwards),oracle.step(count,backwards));assert_eq!((g.palette_selected(),g.palette_moved()),(oracle.selected(),oracle.moved()));}for (index,found) in [(99,false),(1,true),(0,true)]{g.palette_reconcile_selection(count,index,found);oracle.reconcile(count,index,found);assert_eq!((g.palette_selected(),g.palette_moved()),(oracle.selected(),oracle.moved()));}}g.palette_reset();oracle.reset();assert_eq!((g.palette_selected(),g.palette_moved()),(oracle.selected(),oracle.moved()));
    }
    #[test] fn palette_identical_projection_keeps_focus_pending_input_and_lease(){
        let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));let id=a.action_node_id();a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let plan=g.reconcile_palette(vec![palette_item("a","First",0),palette_item("b","Second",1)]);assert_eq!((plan.created,plan.retired),(0,0));assert_eq!(a.action_node_id(),id);assert!(a.action_snapshot().focused&&a.action_pending());assert!(g.palette_scene().children.iter().all(|n|!n.cancel_input));assert!(a.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action==ffi::RowAction::Activate);
    }
    #[test] fn palette_target_resolves_current_index_after_reorder_without_rebinding(){
        let mut g=palette_tree();let a=g.acquire(&node_named(6,"a"));let b=g.acquire(&node_named(6,"b"));let id=a.action_node_id();assert!(g.palette_target("a",id).valid);assert_eq!(g.palette_target("a",id).index,0);g.reconcile_palette(vec![palette_item("b","Second",1),palette_item("a","Updated",2)]);assert_eq!(a.action_node_id(),id);assert_eq!(g.palette_target("a",id).index,1);assert_eq!(g.palette_target("b",b.action_node_id()).index,0);assert_eq!(g.palette_scene().children[1].icon,2);
    }
    #[test] fn palette_batch_content_change_cancels_sibling_press_keeps_focus(){
        let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));let mut b=g.acquire(&node_named(6,"b"));a.action_focus(true);for row in [&mut a,&mut b]{row.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);}g.reconcile_palette(vec![palette_item("a","Renamed",2),palette_item("b","Second",1)]);let scene=g.palette_scene();assert!(scene.children[0].cancel_input&&scene.children[1].cancel_input);assert!(!a.action_pending()&&!b.action_pending());assert!(a.action_snapshot().focused);
    }
    #[test] fn palette_reorder_cancels_all_presses_preserves_ids_and_focus(){
        let mut g=palette_tree();let before=g.palette_scene();let mut a=g.acquire(&node_named(6,"a"));let mut b=g.acquire(&node_named(6,"b"));a.action_focus(true);for row in [&mut a,&mut b]{row.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);}g.reconcile_palette(vec![palette_item("b","Second",1),palette_item("a","First",0)]);let next=g.palette_scene();assert_eq!(next.root,before.root);assert_eq!(next.children[1].id,before.children[0].id);assert!(next.children.iter().all(|n|n.cancel_input));assert!(!a.action_pending()&&!b.action_pending());assert!(a.action_snapshot().focused);
    }
    #[test] fn palette_keyboard_selection_cancels_immediately_and_projects_highlight(){
        let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(g.palette_step(2,false));assert!(!a.action_pending());assert_eq!(g.palette_selected(),1);assert!(g.palette_moved());g.reconcile_palette(vec![palette_item("a","First",0),palette_item("b","Second",1)]);let scene=g.palette_scene();assert!(!scene.children[0].highlighted&&scene.children[1].highlighted);assert!(scene.children.iter().all(|n|n.cancel_input));g.reconcile_palette(vec![palette_item("a","First",0),palette_item("b","Second",1)]);assert!(g.palette_scene().children.iter().all(|n|!n.cancel_input));
    }
    #[test] fn palette_reset_cancels_input_and_keeps_root_and_rows(){
        let mut g=palette_tree();let root=g.palette_scene().root;g.palette_step(2,false);let mut a=g.acquire(&node_named(6,"a"));let id=a.action_node_id();a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);g.palette_reset();assert!(!a.action_pending());assert_eq!((g.palette_selected(),g.palette_moved()),(0,false));g.reconcile_palette(vec![palette_item("a","First",0),palette_item("b","Second",1)]);assert_eq!(g.palette_scene().root,root);assert_eq!(a.action_node_id(),id);assert!(g.palette_scene().children[0].highlighted);
    }
    #[test] fn palette_target_rejects_unattached_disabled_missing_and_stale_ids(){
        let mut g=palette_tree();let id=g.palette_scene().children[0].id;assert!(!g.palette_target("a",id).valid);let mut a=g.acquire(&node_named(6,"a"));assert!(g.palette_target("a",id).valid);for (key,id) in [("missing",id),("b",id),("a",0),("a",id+100)]{assert!(!g.palette_target(key,id).valid);}a.action_enabled(false);assert!(!g.palette_target("a",id).valid);a.action_enabled(true);assert!(g.palette_target("a",id).valid);g.reconcile_palette(vec![palette_item("b","Second",1)]);assert!(!g.palette_target("a",id).valid);g.reconcile_palette(vec![palette_item("a","New visit",0)]);let b=g.acquire(&node_named(6,"a"));assert_ne!(b.action_node_id(),id);assert!(!g.palette_target("a",id).valid);assert!(g.palette_target("a",b.action_node_id()).valid);
    }
    #[test] fn palette_invalid_projection_preserves_model_input_and_selection(){
        let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);let old=g.palette_scene();for items in [vec![palette_item("a","First",0),palette_item("a","Duplicate",1)],vec![palette_item("","Empty",0)],vec![palette_item("a","Wrong icon",3)]]{assert!(!g.reconcile_palette(items).admitted);assert!(a.action_pending());assert_eq!(g.palette_scene().children[0].title,old.children[0].title);assert_eq!(g.palette_selected(),0);assert!(g.palette_target("a",a.action_node_id()).valid);}
    }
    #[test] fn palette_snapshots_preserve_raw_utf16_and_are_owned(){
        let mut g=new_palette_graph();let mut item=palette_item("a","日本語 🦀",2);item.title.push(0xd800);let title=item.title.clone();g.reconcile_palette(vec![item]);let mut snapshot=g.palette_scene();assert_eq!(snapshot.children[0].title,title);snapshot.children[0].title.clear();snapshot.children[0].detail.clear();assert_eq!(g.palette_scene().children[0].title,title);assert!(!g.palette_scene().children[0].detail.is_empty());
    }
    #[test] fn palette_empty_results_clamp_selection_preserve_root_and_retire_rows(){
        let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));let root=g.palette_scene().root;g.palette_step(2,false);g.reconcile_palette(vec![]);assert_eq!(g.palette_selected(),0);assert_eq!(g.palette_scene().root,root);assert!(g.palette_scene().children.is_empty());assert_eq!(a.node_parent_id(),0);assert_eq!(a.node_model_kind(),0);assert!(!a.action_key(true,1,true,true).handled);g.reconcile_palette(vec![palette_item("c","New",0)]);assert!(g.palette_scene().children[0].highlighted);
    }
    #[test] fn palette_wrong_model_orphan_and_graph_retirement_reject_stale_input(){
        let mut other=new_action_graph();other.reconcile(vec![node_named(6,"a")]);let mut old=other.acquire(&node_named(6,"a"));old.node_checkbox_init(true);assert!(!other.reconcile_palette(vec![palette_item("a","First",0)]).admitted);assert!(old.node_checked());let mut g=palette_tree();let mut a=g.acquire(&node_named(6,"a"));assert!(!g.reconcile(vec![node_named(6,"a")]).admitted);g.reconcile(vec![]);assert_eq!(g.palette_scene().root,0);assert!(!g.palette_step(3,false));assert_eq!(a.node_model_kind(),0);assert!(!a.action_key(true,1,true,true).handled);
    }
    #[test] fn palette_owner_checks_reject_foreign_graphs_with_equal_ids(){
        let mut g=palette_tree();let mut other=palette_tree();let a=g.acquire(&node_named(6,"a"));let b=other.acquire(&node_named(6,"a"));assert_eq!(a.action_node_id(),b.action_node_id());assert_eq!(a.node_parent_id(),b.node_parent_id());assert!(g.owns_input(&a)&&other.owns_input(&b));assert!(!g.owns_input(&b)&&!other.owns_input(&a));drop(g);assert_eq!(a.node_parent_id(),0);
    }
    #[test] fn scene_owner_tokens_reject_equal_ids_from_foreign_graphs(){
        let mut left=workspace_tree();let mut right=workspace_tree();let a=left.acquire(&node_named(3,"work"));let b=right.acquire(&node_named(3,"work"));assert_eq!(a.action_node_id(),b.action_node_id());assert_eq!(a.node_parent_id(),b.node_parent_id());assert!(left.owns_input(&a)&&right.owns_input(&b));assert!(!left.owns_input(&b)&&!right.owns_input(&a));let plain=new_action_control();assert!(!left.owns_input(&plain));left.reconcile(vec![]);assert!(!left.owns_input(&a));
    }
    #[test] fn scene_owner_checks_reject_replaced_leases_and_cover_workspace_nodes(){
        let mut g=workspace_tree();let a=g.acquire(&node_named(3,"work"));assert!(g.owns_input(&a));let b=g.acquire(&node_named(3,"work"));assert!(!g.owns_input(&a));assert!(g.owns_input(&b));drop(a);assert!(g.owns_input(&b));let root=g.acquire(&node_named(11,"chrome.workspaces"));assert!(!g.owns_input(&root));
    }
    fn space(key:&str,name:&str,label:&str)->ffi::WorkspaceSceneInput{ffi::WorkspaceSceneInput{key:key.into(),name:name.encode_utf16().collect(),label:label.encode_utf16().collect(),sleeping:false,active:false,enabled:true}}
    fn workspace_tree()->Box<ActionGraph>{let mut g=new_action_graph();assert!(g.reconcile_chrome(vec![space("work","Praca",""),space("personal","Prywatne","")]).admitted);g}
    #[test] fn workspace_tree_owns_parent_order_and_utf16_presentation(){
        let mut g=new_action_graph();let mut item=space("w","日本語 🦀","🚀");item.sleeping=true;item.active=true;item.name.push(0xd800);assert!(g.reconcile_chrome(vec![item]).admitted);let tree=g.workspace_scene();assert_ne!(tree.root,0);assert_eq!(tree.children.len(),1);let child=&tree.children[0];assert_eq!(child.parent,tree.root);assert_ne!(child.id,tree.root);assert_eq!(child.key,"w");let mut name="Przestrzeń: 日本語 🦀".encode_utf16().collect::<Vec<_>>();name.push(0xd800);name.extend(" · uśpiona".encode_utf16());assert_eq!(child.name,name);assert_eq!(child.label,"🚀".encode_utf16().collect::<Vec<_>>());assert!(child.active&&child.enabled);let a=g.acquire(&node_named(3,"w"));assert_eq!(a.node_parent_id(),tree.root);assert_eq!(a.node_model_kind(),4);
    }
    #[test] fn workspace_container_cannot_acquire_input_or_arbitrary_identity(){
        let mut g=workspace_tree();let root=g.acquire(&node_named(11,"chrome.workspaces"));assert_eq!(root.action_node_id(),0);assert!(!root.action_snapshot().enabled);let old=g.workspace_scene().root;assert!(!g.reconcile(vec![node_named(11,"forged")]).admitted);assert_eq!(g.workspace_scene().root,old);
    }
    #[test] fn workspace_invalid_projection_is_atomic_for_tree_properties_and_input(){
        let mut g=workspace_tree();let old=g.workspace_scene();let mut a=g.acquire(&node_named(3,"work"));a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(a.action_pending());for items in [vec![space("work","Changed","🚀"),space("work","Duplicate","")],vec![space("","Empty","")]]{assert!(!g.reconcile_chrome(items).admitted);let tree=g.workspace_scene();assert_eq!(tree.root,old.root);assert_eq!(tree.children[0].name,old.children[0].name);assert_eq!(tree.children[0].id,old.children[0].id);assert!(a.action_pending()&&a.action_snapshot().focused);}
    }
    #[test] fn workspace_removal_retires_payload_and_parent_for_retained_adapter(){
        let mut g=workspace_tree();let mut a=g.acquire(&node_named(3,"personal"));let record=if let ActionStorage::Shared{node,..}=&a.storage{node.clone()}else{panic!("missing record")};let id=a.action_node_id();assert!(g.reconcile_chrome(vec![space("work","Praca","")]).admitted);assert_eq!(g.workspace_scene().children.len(),1);assert_eq!(a.action_node_id(),0);assert_eq!(a.node_parent_id(),0);assert!(record.borrow().model.is_none());assert!(!a.action_key(true,1,true,true).handled);assert!(g.reconcile_chrome(vec![space("work","Praca",""),space("personal","Again","")]).admitted);assert_ne!(g.workspace_scene().children[1].id,id);
    }
    #[test] fn workspace_name_selection_refresh_preserves_focus_and_pending_input(){
        let mut g=workspace_tree();let tree=g.workspace_scene();let mut a=g.acquire(&node_named(3,"work"));a.action_focus(true);assert!(a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true).handled);let mut work=space("work","Renamed","");work.active=true;work.sleeping=true;let plan=g.reconcile_chrome(vec![work,space("personal","Prywatne","")]);assert_eq!((plan.created,plan.retired),(0,0));let next=g.workspace_scene();assert_eq!(tree.root,next.root);assert_eq!(tree.children[0].id,next.children[0].id);assert!(!next.children[0].cancel_input);assert!(a.action_pending()&&a.action_snapshot().focused&&a.action_snapshot().on);assert!(a.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action==ffi::RowAction::Activate);
    }
    #[test] fn workspace_reorder_cancels_press_preserves_focus_ids_and_parent(){
        let mut g=workspace_tree();let tree=g.workspace_scene();let mut a=g.acquire(&node_named(3,"work"));a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(a.action_pending());g.reconcile_chrome(vec![space("personal","Prywatne",""),space("work","Praca","")]);let next=g.workspace_scene();assert_eq!(next.root,tree.root);assert_eq!(next.children[1].id,tree.children[0].id);assert!(next.children.iter().all(|n|n.parent==tree.root&&n.cancel_input));assert!(!a.action_pending());assert!(a.action_snapshot().focused);assert!(a.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action!=ffi::RowAction::Activate);
    }
    #[test] fn workspace_emoji_geometry_change_cancels_sibling_press(){
        let mut g=workspace_tree();let mut a=g.acquire(&node_named(3,"personal"));a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);assert!(a.action_pending());g.reconcile_chrome(vec![space("work","Praca","🚀"),space("personal","Prywatne","")]);assert!(g.workspace_scene().children.iter().all(|n|n.cancel_input));assert!(!a.action_pending());assert!(a.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action!=ffi::RowAction::Activate);
    }
    #[test] fn workspace_readiness_survives_adapter_acquisition_and_reenable(){
        let mut g=new_action_graph();let mut item=space("work","Praca","");item.enabled=false;g.reconcile_chrome(vec![item]);let mut a=g.acquire(&node_named(3,"work"));assert!(!a.action_snapshot().enabled);assert!(!a.action_key(true,1,true,true).handled);assert!(!g.workspace_scene().children[0].enabled);g.reconcile_chrome(vec![space("work","Praca","")]);assert!(a.action_snapshot().enabled);assert!(a.action_key(true,1,true,true).handled);
    }
    #[test] fn workspace_orphan_projection_rejects_but_whole_subtree_can_retire(){
        let mut g=workspace_tree();let tree=g.workspace_scene();let mut a=g.acquire(&node_named(3,"work"));assert!(!g.reconcile(vec![node_named(3,"work")]).admitted);assert_eq!(a.node_parent_id(),tree.root);assert!(g.reconcile(vec![]).admitted);assert_eq!(a.node_parent_id(),0);assert_eq!(g.workspace_scene().root,0);assert!(g.workspace_scene().children.is_empty());assert!(!a.action_key(true,1,true,true).handled);
    }
    #[test] fn workspace_rebind_keeps_tree_properties_and_rejects_stale_adapter(){
        let mut g=workspace_tree();let mut a=g.acquire(&node_named(3,"work"));let id=a.action_node_id();let root=a.node_parent_id();a.action_key(true,1,true,true);let b=g.acquire(&node_named(3,"work"));assert_eq!(b.action_node_id(),id);assert_eq!(b.node_parent_id(),root);assert_eq!(b.node_model_kind(),4);assert!(!a.action_key(false,1,true,true).handled);assert_eq!(a.node_parent_id(),0);assert_eq!(g.workspace_scene().children[0].name,"Przestrzeń: Praca".encode_utf16().collect::<Vec<_>>());drop(a);assert_eq!(b.node_parent_id(),root);
    }
    fn selector_node()->(Box<ActionGraph>,Box<ActionControl>){let mut g=new_action_graph();g.reconcile(vec![node_pin("select")]);let mut a=g.acquire(&node_pin("select"));assert!(a.node_select_init(keyed_options(&[("a","Alpha"),("b","Beta"),("c","日本語 🦀")])));(g,a)}
    #[test] fn node_selector_preserves_options_selection_and_id_on_rebind(){
        let (mut g,mut old)=selector_node();let id=old.action_node_id();assert!(old.node_select_select_index(2));let mut next=g.acquire(&node_pin("select"));assert!(next.node_select_init(keyed_options(&[("x","Ignored defaults")])));assert_eq!(next.node_model_kind(),3);assert_eq!(next.action_node_id(),id);assert_eq!(next.node_select_selected(),2);assert_eq!(next.node_select_selected_key(),"c");assert_eq!(next.node_select_options()[2].label,"日本語 🦀");assert!(!old.node_select_select_index(0));drop(old);assert_eq!(next.node_select_selected_key(),"c");
    }
    #[test] fn node_selector_handoff_cancels_popup_and_prefix_not_value(){
        let (mut g,mut old)=selector_node();let token=old.node_select_open(true);assert!(old.node_select_expanded());assert!(old.node_select_input("b",10,true));let mut next=g.acquire(&node_pin("select"));assert!(!next.node_select_expanded());assert_eq!(next.node_select_selected_key(),"b");assert!(next.node_select_input("a",11,true));assert_eq!(next.node_select_selected_key(),"a");assert!(!old.node_select_close_menu_at(token,900));assert!(!next.node_select_choose_key(token,"c",true));let current=next.node_select_open(true);assert_ne!(current,token);assert!(!next.node_select_close_menu_at(token,901));assert!(next.node_select_can_choose(current,"c",true));
    }
    #[test] fn node_selector_drop_cancels_transients_and_resets_old_popup_guard(){
        let (mut g,mut a)=selector_node();a.node_select_select_index(1);let t=a.node_select_open(true);assert!(a.node_select_close_menu_at(t,100));let t=a.node_select_open(true);drop(a);let record=g.nodes.values().next().unwrap().borrow();let Some(NodeModel::Select(s))=record.model.as_deref() else{panic!("model lost")};assert!(!s.expanded());assert!(s.prefix.is_empty());assert_eq!(s.last_close,None);drop(record);let mut b=g.acquire(&node_pin("select"));assert_eq!(b.node_select_selected_key(),"b");assert!(!b.node_select_choose(t,0,true));assert_ne!(b.node_select_open_pointer(true,101,100),0);
    }
    #[test] fn node_selector_disabled_input_rejects_all_user_routes_allows_projection(){
        let (_g,mut a)=selector_node();let token=a.node_select_open(true);a.action_enabled(false);assert!(!a.node_select_can_choose(token,"b",true));assert!(!a.node_select_choose_key(token,"b",true));assert!(!a.node_select_choose(token,1,true));assert!(!a.node_select_input("b",0,true));assert_eq!(a.node_select_open(true),0);assert_eq!(a.node_select_open_pointer(true,1000,100),0);assert!(a.node_select_select_index(1));assert!(a.node_select_replace_options(keyed_options(&[("b","Updated"),("d","Delta")]),false,false));assert_eq!(a.node_select_selected_key(),"b");a.node_select_clear_selection();assert!(!a.node_select_has_selection());a.action_enabled(true);assert_eq!(a.node_select_open(false),0);assert!(!a.node_select_input("d",0,false));
    }
    #[test] fn node_selector_rejects_wrong_kind_standalone_and_invalid_initial_options(){
        let mut plain=new_action_control();assert!(!plain.node_select_init(vec![]));let mut g=new_action_graph();let mut a=g.append_form_action();assert!(!a.node_select_init(keyed_options(&[("x","One"),("x","Two")])));assert_eq!(a.node_model_kind(),0);assert!(a.node_checkbox_init(true));assert!(!a.node_select_init(vec![]));let mut b=g.append_form_action();assert!(b.node_select_init(vec![]));assert_eq!(b.node_model_kind(),3);assert!(!b.node_checkbox_init(true));assert!(!b.node_toggle_init());assert_eq!(b.node_select_open(true),0);
    }
    #[test] fn node_selector_retirement_frees_options_despite_retained_adapter(){
        let (mut g,mut a)=selector_node();let token=a.node_select_open(true);let record=if let ActionStorage::Shared{node,..}=&a.storage{node.clone()}else{panic!("record missing")};g.reconcile(vec![]);assert!(record.borrow().model.is_none());assert_eq!(a.node_model_kind(),0);assert!(!a.node_select_init(vec![]));assert!(a.node_select_options().is_empty());assert!(!a.node_select_has_selection());assert_eq!(a.node_select_selected(),usize::MAX);assert!(!a.node_select_choose_key(token,"b",true));assert!(!a.node_select_replace_options(keyed_options(&[("a","Alpha")]),false,false));
    }
    #[test] fn node_selector_invalid_replacement_preserves_opening_and_siblings(){
        let (_g,mut a)=selector_node();let token=a.node_select_open(true);assert!(!a.node_select_replace_options(keyed_options(&[("","Empty")]),false,false));assert!(!a.node_select_replace_options(keyed_options(&[("x","One"),("x","Two")]),false,false));assert!(a.node_select_can_choose(token,"b",true));let mut g=new_action_graph();let mut b=g.append_form_action();b.node_select_init(keyed_options(&[("z","Other")]));assert!(a.node_select_choose_key(token,"b",true));assert_eq!(b.node_select_selected_key(),"z");
    }
    #[test] fn node_selector_pointer_guard_and_replacement_match_legacy_reducer(){
        let (_g,mut a)=selector_node();let mut oracle=new_select_state(keyed_options(&[("a","Alpha"),("b","Beta"),("c","日本語 🦀")]));let t=a.node_select_open(true);assert_eq!(t,oracle.open(true));assert_eq!(a.node_select_close_menu_at(t,1000),oracle.close_menu_at(t,1000));for now in [999,1000,1100,1101]{assert_eq!(a.node_select_open_pointer(true,now,100),oracle.open_pointer(true,now,100));assert_eq!(a.node_select_expanded(),oracle.expanded());}a.node_select_dismiss_popup(1200,true);oracle.dismiss_popup(1200,true);assert_eq!(a.node_select_open_pointer(true,1300,100),oracle.open_pointer(true,1300,100));assert!(a.node_select_replace_options(keyed_options(&[("b","Beta renamed"),("a","Alpha")]),true,true));oracle.replace_options(keyed_options(&[("b","Beta renamed"),("a","Alpha")]),true,true);assert_eq!(a.node_select_selected_key(),oracle.selected_key());assert_eq!(a.node_select_option_key(99),"");
    }
    #[test] fn node_selector_prefix_matches_legacy_across_timeout_and_backward_clock(){
        let (_g,mut a)=selector_node();let mut oracle=new_select_state(keyed_options(&[("a","Alpha"),("b","Beta"),("c","日本語 🦀")]));for (text,now) in [("b",10),("e",11),("日",1011),("本",1012),("a",5),("\n",6),("",7)]{assert_eq!(a.node_select_input(text,now,true),oracle.input(text,now,true));assert_eq!(a.node_select_selected_key(),oracle.selected_key());}a.node_select_reset_prefix();oracle.reset_prefix();assert_eq!(a.node_select_input("b",8,true),oracle.input("b",8,true));
    }
    #[test] fn node_checkbox_model_survives_adapter_drop_and_reacquire(){
        let mut g=new_action_graph();g.reconcile(vec![node_pin("checked")]);let id;
        {let mut a=g.acquire(&node_pin("checked"));id=a.action_node_id();assert!(a.node_checkbox_init(false));assert!(a.node_set_checked(true));}
        assert!(g.nodes.values().next().unwrap().borrow().model.is_some());let mut b=g.acquire(&node_pin("checked"));assert_eq!(b.action_node_id(),id);assert!(b.node_checkbox_init(false));assert!(b.node_checked());assert_eq!(b.node_model_kind(),1);
    }
    #[test] fn node_toggle_model_keeps_target_and_animation_on_rebind(){
        let mut g=new_action_graph();g.reconcile(vec![node_pin("toggle")]);let mut old=g.acquire(&node_pin("toggle"));assert!(old.node_toggle_init());assert!(old.node_toggle_flip(true,100,true));old.node_toggle_hover(true,100,true);let p=old.node_toggle_view(120);assert!(p.checked&&p.animating);assert!(p.progress>0.0&&p.progress<1.0);
        let mut next=g.acquire(&node_pin("toggle"));assert!(next.node_toggle_init());let q=next.node_toggle_view(120);assert_eq!((q.checked,q.progress,q.hover,q.animating),(p.checked,p.progress,p.hover,p.animating));assert_eq!(old.node_model_kind(),0);assert!(!old.node_toggle_set(false));assert!(next.node_toggle_checked());
    }
    #[test] fn node_model_binding_rejects_wrong_kind_standalone_and_retired(){
        let mut plain=new_action_control();assert!(!plain.node_checkbox_init(true));assert!(!plain.node_toggle_init());let mut g=new_action_graph();let mut a=g.append_form_action();assert!(a.node_checkbox_init(true));assert!(!a.node_toggle_init());assert!(a.node_checked());let mut b=g.append_form_action();assert!(b.node_toggle_init());assert!(!b.node_checkbox_init(true));assert_eq!(b.node_model_kind(),2);let id=a.action_node_id();g.retire_form_action(id);assert!(!a.node_checkbox_init(true));assert!(!a.node_toggle_init());
    }
    #[test] fn node_checked_user_mutation_requires_enabled_lease_but_projection_is_allowed(){
        let mut g=new_action_graph();let mut a=g.append_form_action();a.node_checkbox_init(false);a.action_enabled(false);assert!(!a.node_flip_checked(true));assert!(a.node_set_checked(true));assert!(a.node_checked());a.action_enabled(true);assert!(!a.node_flip_checked(false));assert!(a.node_flip_checked(true));assert!(!a.node_checked());
        let mut b=g.append_form_action();b.node_toggle_init();b.action_enabled(false);assert!(!b.node_toggle_flip(true,0,true));assert!(b.node_toggle_set(true));assert!(b.node_toggle_checked());b.action_enabled(true);assert!(!b.node_toggle_flip(false,0,true));assert!(b.node_toggle_flip(true,0,false));assert!(!b.node_toggle_checked());
    }
    #[test] fn node_model_retirement_releases_payload_while_adapter_is_retained(){
        let mut g=new_action_graph();let mut a=g.append_form_action();a.node_checkbox_init(true);let record=if let ActionStorage::Shared{node,..}=&a.storage{node.clone()}else{panic!("missing record")};assert!(record.borrow().model.is_some());assert!(g.retire_form_action(a.action_node_id()));assert!(record.borrow().model.is_none());assert_eq!(a.node_model_kind(),0);assert!(!a.node_checked());
        let mut b=g.append_form_action();b.node_toggle_init();b.node_toggle_flip(true,0,true);let record=if let ActionStorage::Shared{node,..}=&b.storage{node.clone()}else{panic!("missing record")};drop(g);assert!(record.borrow().model.is_none());assert_eq!(b.node_model_kind(),0);assert!(!b.node_toggle_view(20).animating);assert!(!b.node_toggle_init());
    }
    #[test] fn node_toggle_snapshot_matches_legacy_reducer_across_interruption_and_clocks(){
        let mut g=new_action_graph();let mut a=g.append_form_action();a.node_toggle_init();let mut old=new_toggle_state();
        for (now,animate,hover) in [(100,true,true),(120,true,false),(125,true,true),(200,false,false),(50,true,true)]{assert_eq!(a.node_toggle_flip(true,now,animate),old.toggle_flip(true,now,animate));a.node_toggle_hover(hover,now,animate);old.toggle_hover(hover,now,animate);for time in [now,now+20,now+300]{let p=a.node_toggle_view(time);assert_eq!((p.checked,p.progress,p.hover,p.animating),(old.toggle_checked(),old.toggle_progress(time),old.toggle_hover_progress(time),old.toggle_animating(time)));}}
    }
    #[test] fn node_models_are_isolated_and_invalid_reconciliation_keeps_payloads(){
        let mut g=new_action_graph();g.reconcile(vec![node_pin("a"),node_pin("b")]);let mut a=g.acquire(&node_pin("a"));let mut b=g.acquire(&node_pin("b"));a.node_checkbox_init(true);b.node_toggle_init();b.node_toggle_set(true);let rejected=g.reconcile(vec![node_pin("a"),node_pin("a")]);assert!(!rejected.admitted);assert!(a.node_checked());assert!(b.node_toggle_checked());a.node_set_checked(false);assert!(b.node_toggle_checked());assert!(!a.node_toggle_set(false));assert!(!b.node_set_checked(false));
    }
    #[test] fn node_toggle_finish_stops_both_transitions_and_retirement_cannot_restart(){
        let mut g=new_action_graph();let mut a=g.append_form_action();a.node_toggle_init();a.node_toggle_flip(true,100,true);a.node_toggle_hover(true,100,true);assert!(a.node_toggle_view(120).animating);assert!(a.node_toggle_finish());let p=a.node_toggle_view(120);assert_eq!((p.checked,p.progress,p.hover,p.animating),(true,1.0,0.0,false));g.retire_form_action(a.action_node_id());assert!(!a.node_toggle_finish());assert!(!a.node_toggle_set(true));assert!(!a.node_toggle_hover(true,200,true));let p=a.node_toggle_view(220);assert_eq!((p.checked,p.progress,p.hover,p.animating),(false,0.0,0.0,false));
    }
    #[test] fn form_action_selector_press_policy_is_scoped_and_retirement_cannot_reopen(){
        let mut g=new_action_graph();let mut select=g.append_form_action();let mut ordinary=g.append_form_action();select.action_on_press(true);let id=select.action_node_id();
        assert!(select.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true).action==ffi::RowAction::Activate);
        assert!(ordinary.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true).action==ffi::RowAction::None);
        assert!(g.retire_form_action(id));assert!(select.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true).action==ffi::RowAction::None);assert!(!select.action_snapshot().enabled);
        assert!(ordinary.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).action==ffi::RowAction::Activate);
    }
    #[test] fn form_action_membership_allocates_unique_ids_without_native_keys(){
        let mut g=new_action_graph();let mut a=g.append_form_action();let b=g.append_form_action();assert_ne!(a.action_node_id(),0);assert_ne!(a.action_node_id(),b.action_node_id());assert_eq!(g.form_scene().children,vec![a.action_node_id(),b.action_node_id()]);
        a.action_on(true);a.action_default(true);assert!(a.action_snapshot().on);assert!(a.action_snapshot().is_default);assert!(!b.action_snapshot().on);assert_eq!(g.nodes.len(),3);
    }
    #[test] fn form_action_removal_retires_retained_input_and_new_members_never_reuse_ids(){
        let mut g=new_action_graph();let mut a=g.append_form_action();let b=g.append_form_action();let id=a.action_node_id();a.action_focus(true);a.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        assert!(g.retire_form_action(id));assert_eq!(a.action_node_id(),0);assert!(!a.action_snapshot().enabled);assert!(!a.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).handled);assert!(!a.action_key(true,1,true,true).handled);assert!(!g.retire_form_action(id));assert_eq!(g.form_scene().children,vec![b.action_node_id()]);assert_eq!(g.nodes.len(),2);
        let c=g.append_form_action();assert!(c.action_node_id()>b.action_node_id());assert_ne!(c.action_node_id(),id);
    }
    #[test] fn form_action_owner_drop_invalidates_adapters_that_outlive_group(){
        let mut a={let mut g=new_action_graph();g.append_form_action()};assert_eq!(a.action_node_id(),0);assert!(!a.action_enabled(true));assert!(!a.action_gesture(1,true).handled);
    }
    #[test] fn form_action_id_exhaustion_is_transactional_and_retirement_still_works(){
        let mut g=new_action_graph();let a=g.append_form_action();let id=a.action_node_id();g.next_id=u64::MAX;let rejected=g.append_form_action();assert_eq!(rejected.action_node_id(),0);assert_eq!(g.form_scene().children,vec![id]);assert_eq!(g.nodes.len(),2);assert!(g.retire_form_action(id));assert!(g.form_scene().children.is_empty());assert_ne!(g.form_scene().root,0);
    }
    #[test] fn form_action_private_keys_cannot_be_injected_or_reacquired_by_projection(){
        let mut g=new_action_graph();let a=g.append_form_action();let id=a.action_node_id();let key=ffi::ActionNodeKey{kind:9,high:id,low:0,name:String::new()};let rejected=g.reconcile(vec![ffi::ActionNodeKey{kind:9,high:id,low:0,name:String::new()}]);assert!(!rejected.admitted);assert_eq!(a.action_node_id(),id);assert_eq!(g.acquire(&key).action_node_id(),0);assert_eq!(a.action_node_id(),id);
    }
    #[test] fn form_action_retirement_cannot_remove_projected_tabs_or_split_pairs(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_tab(1),node_pane(7,"work",1),node_pane(8,"work",1)]);for id in p.ids.iter().copied(){assert!(!g.retire_form_action(id));}assert_eq!(g.order,p.ids);assert!(!g.retire_form_action(0));
    }
    fn node_pane(kind:u32,workspace:&str,low:u64)->ffi::ActionNodeKey{ffi::ActionNodeKey{kind,high:7,low,name:workspace.into()}}
    #[test] fn action_graph_pane_actions_are_paired_and_keep_ids_on_same_target_refresh(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_pane(7,"work",1),node_pane(8,"work",1)]);assert!(p.admitted);assert_ne!(p.ids[0],p.ids[1]);
        let q=g.reconcile(vec![node_pane(8,"work",1),node_pane(7,"work",1)]);assert_eq!(q.ids,vec![p.ids[1],p.ids[0]]);assert_eq!((q.created,q.retired),(0,0));
    }
    #[test] fn action_graph_pane_incomplete_or_invalid_target_is_transactional(){
        let mut g=new_action_graph();let ids=g.reconcile(vec![node_pane(7,"work",1),node_pane(8,"work",1)]).ids;
        for keys in [vec![node_pane(7,"work",1)],vec![node_pane(8,"work",1)],vec![node_pane(7,"work",1),node_pane(8,"other",1)],vec![node_pane(7,"",1),node_pane(8,"",1)],vec![ffi::ActionNodeKey{kind:7,high:0,low:0,name:"work".into()}]]{let rejected=g.reconcile(keys);assert!(!rejected.admitted);assert_eq!(rejected.ids,ids);}
    }
    #[test] fn action_graph_pane_retarget_hide_and_workspace_change_revoke_old_handles(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_pane(7,"work",1),node_pane(8,"work",1)]);let mut old=g.acquire(&node_pane(8,"work",1));old.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        let q=g.reconcile(vec![node_pane(7,"work",2),node_pane(8,"work",2)]);assert_eq!((q.created,q.retired),(2,2));assert!(q.ids[0]>p.ids[1]);assert_eq!(old.action_node_id(),0);assert!(!old.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).handled);
        let next=g.acquire(&node_pane(8,"work",2));g.reconcile(vec![node_pane(7,"personal",2),node_pane(8,"personal",2)]);assert_eq!(next.action_node_id(),0);
        let hidden=g.acquire(&node_pane(7,"personal",2));assert_eq!(g.reconcile(vec![]).retired,2);assert_eq!(hidden.action_node_id(),0);
    }
    #[test] fn action_graph_pane_adapter_rebind_cancels_press_without_changing_target_identity(){
        let mut g=new_action_graph();g.reconcile(vec![node_pane(7,"work",1),node_pane(8,"work",1)]);let mut old=g.acquire(&node_pane(8,"work",1));let id=old.action_node_id();old.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        let next=g.acquire(&node_pane(8,"work",1));assert_eq!(next.action_node_id(),id);assert!(!next.action_pending());assert_eq!(old.action_node_id(),0);assert!(!old.action_key(true,1,true,true).handled);
        let mut inactive=g.acquire(&ffi::ActionNodeKey{kind:0,high:0,low:0,name:String::new()});assert_eq!(inactive.action_node_id(),0);assert!(!inactive.action_gesture(1,true).handled);
    }
    #[test] fn action_graph_palette_targets_reorder_and_old_index_leases_cannot_activate(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_named(6,"context/target-a/0"),node_named(6,"context/target-b/0")]);let mut old=g.acquire(&node_named(6,"context/target-a/0"));old.action_pointer(ffi::PointerPhase::Press,1,1,true,true,true);
        let q=g.reconcile(vec![node_named(6,"context/target-b/0"),node_named(6,"context/target-a/0")]);assert_eq!(q.ids,vec![p.ids[1],p.ids[0]]);let next=g.acquire(&node_named(6,"context/target-a/0"));assert_eq!(next.action_node_id(),p.ids[0]);assert!(!next.action_pending());assert!(!old.action_pointer(ffi::PointerPhase::Release,1,1,true,true,true).handled);
        g.reconcile(vec![node_named(6,"other-context/target-a/0")]);assert_eq!(next.action_node_id(),0);
    }
    #[test] fn action_graph_palette_duplicate_targets_and_result_namespaces_are_distinct(){
        let mut g=new_action_graph();let p=g.reconcile(vec![node_named(6,"target/0"),node_named(6,"target/1"),node_named(5,"target/0")]);assert!(p.admitted);assert_eq!(p.ids.iter().copied().collect::<BTreeSet<_>>().len(),3);
        let invalid=g.reconcile(vec![node_named(6,"")]);assert!(!invalid.admitted);assert_eq!(invalid.ids,p.ids);
    }
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
    #[test] fn workspace_notice_shared_geometry_retains_blank_paragraph_lines_and_errors() {
        let g=notice_layout(400,20,5,18,2);assert_eq!(g.body_lines.len(),5);assert_eq!((g.body.height,g.error.y,g.height),(100,108,144));assert_eq!(g.body_lines[3].y,60);
        let g=notice_layout(400,20,5,18,0);assert_eq!(g.height,100);assert!(g.error_lines.is_empty());
    }
    #[test] fn workspace_form_preserves_copy_and_profile_change_warning() {
        let c=workspace_copy();assert!(c.profile_hint.contains("Zmiana profilu zamknie karty"));assert!(c.profile_hint.contains("dzielą logowania i cookies"));assert!(c.save_hint.contains("Wygląd zapisuje się od razu"));
        assert_eq!(workspace_appearance_error(2),"Nie udało się zapisać emoji");assert_eq!(workspace_appearance_error(3),"Podaj kolor #RRGGBB");
    }
    #[test] fn workspace_form_tracks_create_cancel_and_edit_presentation() {
        let mut f=new_workspace_form();f.reset("work");let v=f.view("Praca","Pracowy",true,false,2);assert!(!v.creating);assert!(v.appearance&&v.enabled&&v.remove_enabled);assert_eq!(v.description,"Profil: Pracowy");
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
        let mut f=new_profile_form();f.open("spare","");let mut profiles=profile_options();profiles.reverse();f.reconcile(&profiles,false);assert_eq!(f.selected,"spare");
        profiles.remove(0);f.reconcile(&profiles,false);assert_eq!(f.selected,"used");assert!(matches!(f.request(2,"",true,false,&profiles,&profile_uses()).action,ffi::ProfileAction::None));
        profiles.push(ffi::ProfileOption{id:"new".into(),name:"New".into()});f.reconcile(&profiles,true);assert_eq!(f.selected,"new");
    }
    #[test] fn profile_form_keeps_used_last_busy_and_unready_profiles_safe() {
        let mut f=new_profile_form();f.open("used","");let c=f.controls(true,false,&profile_options(),&profile_uses());assert!(c.edit&&c.rename);assert!(c.status.is_empty());
        assert!(matches!(f.request(2,"",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::None));assert!(f.status().contains("Praca"));
        f.open("spare","");for (ready,busy) in [(false,false),(true,true)]{let c=f.controls(ready,busy,&profile_options(),&[]);assert!(!c.create&&!c.edit&&!c.rename);}
        let mut profiles=profile_options();profiles.remove(0);assert!(matches!(f.request(2,"",true,false,&profiles,&[]).action,ffi::ProfileAction::None));
    }
    #[test] fn profile_form_requires_fresh_same_target_delete_confirmation() {
        let mut f=new_profile_form();f.open("spare","");assert!(matches!(f.request(2,"",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::ConfirmDelete));
        let c=f.controls(true,false,&profile_options(),&profile_uses());assert_eq!(c.confirming,"spare");assert!(c.status.is_empty());
        f.cancel();assert!(f.controls(true,false,&profile_options(),&profile_uses()).confirming.is_empty());assert!(matches!(f.request(2,"",true,false,&profile_options(),&profile_uses()).action,ffi::ProfileAction::ConfirmDelete));
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
    #[test] fn profile_rows_name_spaces_with_polish_plurals_and_mark_current() {
        let profiles=vec![ffi::ProfileOption{id:"w".into(),name:"work".into()},ffi::ProfileOption{id:"ł".into(),name:"łódź".into()}];
        let uses:Vec<ffi::ProfileUse>=["A","B","C"].iter().map(|n|ffi::ProfileUse{workspace:n.to_string(),name:n.to_string(),profile:"w".into()}).collect();
        let rows=profile_rows(&profiles,&uses,"w");assert_eq!(rows[0].initial,"W");assert!(rows[0].current);assert_eq!(rows[0].detail,"Ta przestrzeń · 3 przestrzenie: A, B, C");
        assert_eq!(rows[1].initial,"Ł");assert!(!rows[1].current);assert_eq!(rows[1].detail,"Nieprzypisany do żadnej przestrzeni");assert_eq!(rows[1].remove_ax,"Usuń profil łódź");
        for (n,text) in [(1,"1 przestrzeń"),(2,"2 przestrzenie"),(5,"5 przestrzeni"),(12,"12 przestrzeni"),(22,"22 przestrzenie")]{assert_eq!(spaces_count(n),text);}
        assert!(profile_rows(&profiles,&[],"").iter().all(|r|!r.current));
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
    #[test] fn sidebar_drop_takes_the_whole_page_with_exclusive_edges() {
        let r=||ffi::Rect{x:-20,y:30,width:101,height:50};
        assert!(sidebar_drop_accepts(r(),-20,30,true));assert!(!sidebar_drop_accepts(r(),-21,30,true));
        assert!(sidebar_drop_accepts(r(),29,30,true));assert!(sidebar_drop_accepts(r(),30,30,true));assert!(sidebar_drop_accepts(r(),80,79,true));
        assert!(!sidebar_drop_accepts(r(),81,79,true));assert!(!sidebar_drop_accepts(r(),30,80,true));assert!(!sidebar_drop_accepts(r(),30,29,true));assert!(!sidebar_drop_accepts(r(),30,30,false));
        assert!(!sidebar_drop_accepts(ffi::Rect{x:0,y:0,width:0,height:50},0,0,true));
        // No overflow at the coordinate limits.
        assert!(sidebar_drop_accepts(ffi::Rect{x:i32::MAX-20,y:0,width:100,height:20},i32::MAX,0,true));assert!(!sidebar_drop_accepts(ffi::Rect{x:i32::MAX-20,y:0,width:100,height:20},i32::MAX-21,0,true));
    }
    fn link_state()->ffi::LinkMenuState{ffi::LinkMenuState{ready:true,host:"example.com".into(),source:"work".into(),has_rule:true,rule_workspace:"home".into(),routes:vec![ffi::LinkRoute{id:"work".into(),label:"Praca · Główny".into()},ffi::LinkRoute{id:"home".into(),label:"Dom · Prywatny".into()}]}}
    #[test] fn link_menu_preserves_order_labels_route_submenu_and_checks() {
        let s=link_state();let menu=link_context_menu(&s);assert_eq!(menu.iter().map(|e|(e.command,e.kind)).collect::<Vec<_>>(),vec![(20,0),(21,0),(22,0),(0,1),(23,0),(25,2),(1000,3),(1001,3),(24,0)]);assert_eq!(menu[5].label,"Otwieraj example.com w…");assert_eq!(menu[7].label,"Dom · Prywatny");assert_eq!(menu[0].label,"Otwórz w Peek");
        assert!(!link_menu_checked(1000,&s)&&link_menu_checked(1001,&s)&&!link_menu_checked(1002,&s)&&!link_menu_checked(24,&s));let mut none=link_state();none.has_rule=false;assert!(!link_menu_checked(1001,&none));
        assert!(link_menu_enabled(24,&s)&&!link_menu_enabled(24,&none)&&link_menu_enabled(23,&none)&&link_menu_enabled(1001,&s)&&!link_menu_enabled(1002,&s)&&!link_menu_enabled(999,&s)&&!link_menu_enabled(0,&s));
        let mut empty=link_state();empty.routes.clear();assert_eq!(link_context_menu(&empty).iter().filter(|e|e.kind==3).count(),0);
    }
    #[test] fn link_menu_actions_capture_source_route_and_revalidate_readiness() {
        use ffi::LinkMenuCommand as C;let s=link_state();
        for (command,expected,workspace) in [(20,C::Peek,"work"),(21,C::NewTab,"work"),(22,C::Split,"work"),(23,C::SetRule,"work"),(24,C::RemoveRule,""),(1000,C::SetRule,"work"),(1001,C::SetRule,"home"),(1002,C::None,""),(25,C::None,""),(5,C::None,"")]{let a=link_menu_action(command,&s);assert_eq!(a.command,expected,"{command}");assert_eq!(a.workspace,workspace);}
        let mut busy=link_state();busy.ready=false;for command in [20,21,22,23,24,1000]{assert_eq!(link_menu_action(command,&busy).command,C::None);assert!(!link_menu_enabled(command,&busy));}
        let mut ruleless=link_state();ruleless.has_rule=false;assert_eq!(link_menu_action(24,&ruleless).command,C::None);
    }
    fn workspace_menu_state()->ffi::WorkspaceMenuState{ffi::WorkspaceMenuState{ready:true,busy:false,workspace:"work".into(),workspace_count:2,profile:"p2".into(),profiles:vec![ffi::WorkspaceMenuProfile{id:"p1".into(),name:"Główny".into()},ffi::WorkspaceMenuProfile{id:"p2".into(),name:"Praca".into()}],color:APPEARANCE_COLORS[1],emoji:"🚀".into()}}
    #[test] fn workspace_menu_preserves_structure_labels_groups_and_checks() {
        let s=workspace_menu_state();let menu=workspace_context_menu(&s);let group=|g:u32|menu.iter().filter(|e|e.group==g).map(|e|(e.command,e.kind)).collect::<Vec<_>>();
        assert_eq!(group(0),vec![(1,2),(31,2),(32,2),(0,1),(4,0),(0,1),(30,0)]);assert_eq!(group(1),vec![(100,3),(101,3),(0,1),(3,0),(2,0)]);assert_eq!(group(2),vec![(40,4),(41,4),(42,4),(43,4),(44,4),(45,4),(46,4),(47,4),(0,1),(33,0)]);assert_eq!(group(3),vec![(50,4),(51,4),(52,4),(53,4),(54,4),(55,4),(56,4),(57,4),(0,1),(34,0),(35,4)]);
        assert_eq!(menu.iter().find(|e|e.command==41).unwrap().label,"Koral");assert_eq!(menu.iter().find(|e|e.command==101).unwrap().label,"Praca");assert_eq!(menu.iter().find(|e|e.command==52).unwrap().label,"🚀");
        assert!(workspace_menu_checked(101,&s)&&!workspace_menu_checked(100,&s)&&workspace_menu_checked(41,&s)&&!workspace_menu_checked(40,&s)&&workspace_menu_checked(52,&s)&&!workspace_menu_checked(35,&s)&&!workspace_menu_checked(102,&s));
        let mut plain=workspace_menu_state();plain.emoji.clear();assert!(workspace_menu_checked(35,&plain)&&!workspace_menu_checked(52,&plain));
        assert!(workspace_menu_command(1,0)&&workspace_menu_command(47,0)&&!workspace_menu_command(48,0)&&workspace_menu_command(57,0)&&!workspace_menu_command(58,0)&&!workspace_menu_command(100,0)&&workspace_menu_command(101,2)&&!workspace_menu_command(5,9)&&!workspace_menu_command(20,9)&&!workspace_menu_command(36,9));
    }
    #[test] fn workspace_menu_actions_resolve_targets_and_revalidate_state() {
        use ffi::WorkspaceMenuCommand as C;let s=workspace_menu_state();
        let a=workspace_menu_action(100,&s);assert_eq!((a.command,a.workspace.as_str(),a.profile.as_str()),(C::AssignProfile,"work","p1"));
        let a=workspace_menu_action(3,&s);assert_eq!((a.command,a.profile.as_str()),(C::EditProfile,"p2"));
        let a=workspace_menu_action(43,&s);assert_eq!((a.command,a.color),(C::Color,APPEARANCE_COLORS[3]));
        let a=workspace_menu_action(50,&s);assert_eq!((a.command,a.emoji.as_str()),(C::Emoji,"💼"));
        for (command,expected) in [(2,C::NewProfile),(4,C::Settings),(30,C::Remove),(33,C::CustomColor),(34,C::CustomEmoji),(35,C::ClearEmoji),(1,C::None),(31,C::None),(102,C::None),(48,C::None),(58,C::None),(5,C::None)]{let a=workspace_menu_action(command,&s);assert_eq!(a.command,expected,"{command}");assert_eq!(a.workspace.is_empty(),expected==C::None);}
        let mut single=workspace_menu_state();single.workspace_count=1;assert_eq!(workspace_menu_action(30,&single).command,C::None);assert!(workspace_menu_enabled(4,&single));
        let mut busy=workspace_menu_state();busy.busy=true;assert!(!workspace_menu_enabled(30,&busy)&&workspace_menu_enabled(40,&busy));
        let mut stale=workspace_menu_state();stale.ready=false;for command in [2,3,4,30,40,50,100]{assert_eq!(workspace_menu_action(command,&stale).command,C::None);}
        let mut gone=workspace_menu_state();gone.workspace.clear();assert_eq!(workspace_menu_action(4,&gone).command,C::None);
    }
    #[test] fn folder_menu_labels_enablement_and_captured_targets() {
        use ffi::FolderMenuCommand as C;let state=|folder:&str|ffi::FolderMenuState{ready:true,busy:false,workspace:"work".into(),folder:folder.into()};
        let root=folder_context_menu(&state(""));assert_eq!(root.iter().map(|e|(e.command,e.label.as_str())).collect::<Vec<_>>(),vec![(36,"Nowy folder…")]);
        let nested=folder_context_menu(&state("f"));assert_eq!(nested.iter().map(|e|(e.command,e.label.as_str())).collect::<Vec<_>>(),vec![(36,"Nowy podfolder…"),(0,""),(37,"Zmień nazwę…"),(38,"Usuń folder")]);
        assert!(folder_menu_enabled(36,&state(""))&&!folder_menu_enabled(37,&state(""))&&!folder_menu_enabled(38,&state(""))&&folder_menu_enabled(38,&state("f"))&&!folder_menu_enabled(30,&state("f")));
        for (command,expected) in [(36,C::Create),(37,C::Rename),(38,C::Delete),(35,C::None)]{let a=folder_menu_action(command,&state("f"));assert_eq!(a.command,expected);if expected!=C::None{assert_eq!((a.workspace.as_str(),a.folder.as_str()),("work","f"));}else{assert!(a.workspace.is_empty());}}
        let a=folder_menu_action(36,&state(""));assert_eq!((a.command,a.folder.as_str()),(C::Create,""));
        let mut busy=state("f");busy.busy=true;let mut stale=state("f");stale.ready=false;let mut gone=state("f");gone.workspace.clear();
        for s in [busy,stale,gone]{for command in [36,37,38]{assert_eq!(folder_menu_action(command,&s).command,C::None);}}
    }
    fn palette_input(text:&str,command:bool,provider:&str,results:usize,commands:usize)->ffi::PalettePlanInput{ffi::PalettePlanInput{text:text.into(),command_palette:command,provider:provider.into(),resolved:"https://resolved.test/".into(),keywords:vec!["g".into(),"ddg".into()],results,commands}}
    fn palette_order(p:&ffi::PalettePlan)->Vec<(u8,usize)>{p.slots.iter().map(|s|(match s.source{ffi::PaletteSource::Primary=>0,ffi::PaletteSource::Result=>1,_=>2},s.index)).collect()}
    #[test] fn palette_plan_orders_primary_results_and_commands_like_native() {
        let p=palette_plan(&palette_input("kot",false,"Google",2,0));assert_eq!(palette_order(&p),vec![(0,0),(1,0),(1,1)]);assert_eq!((p.primary_title.as_str(),p.primary_detail.as_str(),p.primary_icon),("Szukaj w Google","kot",0));
        let p=palette_plan(&palette_input("example.com",false,"",0,0));assert_eq!((p.primary_title.as_str(),p.primary_detail.as_str(),p.primary_icon),("Otwórz","https://resolved.test/",1));
        let p=palette_plan(&palette_input("kot",true,"Google",2,2));assert_eq!(palette_order(&p),vec![(2,0),(2,1),(1,0),(1,1),(0,0)]);assert!(!p.keyword_search);
        let p=palette_plan(&palette_input("G kot",true,"Google",1,2));assert!(p.keyword_search);assert_eq!(palette_order(&p),vec![(0,0),(2,0),(2,1),(1,0)]);
        let p=palette_plan(&palette_input("gx kot",true,"Google",1,0));assert!(!p.keyword_search);assert_eq!(palette_order(&p),vec![(1,0),(0,0)]);
        let p=palette_plan(&palette_input(">split",true,"",3,1));assert!(p.commands_only);assert_eq!(palette_order(&p),vec![(2,0)]);assert!(p.primary_title.is_empty());
        let p=palette_plan(&palette_input("",true,"",3,1));assert_eq!(palette_order(&p),vec![(2,0),(1,0),(1,1),(1,2)]);
        let p=palette_plan(&palette_input(">x",false,"",1,5));assert!(!p.commands_only);assert_eq!(palette_order(&p),vec![(0,0),(1,0)]);
        let p=palette_plan(&palette_input("ddg\tkot",true,"DuckDuckGo",0,1));assert!(p.keyword_search);assert_eq!(palette_order(&p),vec![(0,0),(2,0)]);
    }
    fn download_facts(phase:ffi::DownloadPhase)->ffi::DownloadFacts{ffi::DownloadFacts{phase,blocked:false,removed:false,can_resume:true,paused:false,received:"5 MB".into(),total:"10 MB".into(),speed:"1 MB".into(),remaining_seconds:0,host:"example.com".into()}}
    #[test] fn download_rows_show_state_as_symbol_and_word_in_its_tone() {
        use ffi::DownloadPhase as P;let r=|f:&ffi::DownloadFacts|{let r=download_row(f);(r.tone,r.state,r.meta)};
        assert_eq!(r(&download_facts(P::InProgress)),(0,String::new(),"5 MB / 10 MB · 1 MB/s · 1 s".into()));
        let mut f=download_facts(P::InProgress);f.paused=true;assert_eq!(r(&f),(2,"‖ Wstrzymano".into(),"5 MB / 10 MB".into()));
        let mut f=download_facts(P::InProgress);f.total.clear();f.speed.clear();f.remaining_seconds=-1;assert_eq!(r(&f).2,"5 MB");
        assert_eq!(r(&download_facts(P::Complete)),(1,"✓ Gotowe".into(),"5 MB · example.com".into()));
        let mut f=download_facts(P::Complete);f.removed=true;assert_eq!(r(&f).0,2);
        assert_eq!(r(&download_facts(P::Cancelled)).1,"⚠ Anulowano");let mut f=download_facts(P::Interrupted);assert_eq!(r(&f).0,3);assert!(r(&f).1.contains("możesz wznowić"));f.can_resume=false;assert!(r(&f).1.contains("pobierz ponownie"));
        let mut f=download_facts(P::Complete);f.blocked=true;assert_eq!((r(&f).0,r(&f).1),(3,"⚠ Zablokowane".into()));
    }
    fn command_context()->ffi::CommandContext{ffi::CommandContext{page:true,http_page:true,split:false,can_back:true,can_forward:false,tab_count:2,reopen:true,workspaces:vec![ffi::CommandWorkspace{id:"home".into(),name:"Dom".into(),asleep:false,same_profile:true,capturing:false},ffi::CommandWorkspace{id:"club".into(),name:"Klub".into(),asleep:true,same_profile:false,capturing:false}]}}
    #[test] fn command_catalogue_lists_conditional_entries_with_targets() {
        let all=command_catalogue(&command_context(),">");let ids=all.iter().map(|e|e.id.as_str()).collect::<Vec<_>>();
        assert_eq!(ids,vec!["new_tab","save_snapshot","timeline","settings","shortcuts","engines","sidebar","spaces","profiles","import","history","downloads","split","reload","find","print","copy_url","back","passwords","close_tab","close_other_tabs","reopen","select_space","move_tab","sleep_space","select_space","copy_tab","sleep_others"]);
        assert_eq!(all[0].detail,"Polecenie · Otwórz pasek nowej karty");assert_eq!(all[12].title,"Otwórz Split View");
        let wake=all.iter().find(|e|e.target=="club"&&e.id=="select_space").unwrap();assert_eq!(wake.title,"Obudź przestrzeń: Klub");assert_eq!(all.iter().find(|e|e.id=="move_tab").unwrap().target,"home");
        let mut split=command_context();split.split=true;split.can_forward=true;split.tab_count=1;split.reopen=false;split.page=true;let ids=command_catalogue(&split,">").into_iter().map(|e|e.id).collect::<Vec<_>>();
        assert!(ids.contains(&"left_pane".into())&&ids.contains(&"forward".into())&&!ids.contains(&"close_tab".into())&&!ids.contains(&"reopen".into()));
        let mut blank=command_context();blank.page=false;blank.workspaces[0].capturing=true;let ids=command_catalogue(&blank,">").into_iter().map(|e|e.id).collect::<Vec<_>>();
        assert!(!ids.iter().any(|i|matches!(i.as_str(),"split"|"reload"|"move_tab"|"copy_tab"|"sleep_space"|"sleep_others")));
    }
    #[test] fn command_catalogue_matches_folded_words_and_compacts_initial_view() {
        let c=command_context();assert_eq!(command_catalogue(&c,"").len(),6);assert_eq!(command_catalogue(&c,">").len(),28);
        let found=command_catalogue(&c,"PRZYWROC  karte");assert_eq!(found.iter().map(|e|e.id.as_str()).collect::<Vec<_>>(),vec!["reopen"]);
        let found=command_catalogue(&c,">  uśpij\tdom ");assert_eq!(found.iter().map(|e|(e.id.as_str(),e.target.as_str())).collect::<Vec<_>>(),vec![("sleep_space","home")]);
        assert!(command_catalogue(&c,"nonexistent").is_empty());assert_eq!(command_fold("ĄĆĘŁŃÓŚŹŻ Żółć"),"acelnoszz zolc");
    }
    #[test] fn tab_menu_preserves_order_labels_and_orientation() {
        let mut s=menu_state();let entries=tab_context_menu(&s);assert_eq!(entries.iter().map(|e|e.command).collect::<Vec<_>>(),vec![36,20,0,7,8,9,11,0,23,24,0,10,12]);assert_eq!(entries[3].label,"Przesuń w lewo");assert_eq!(entries[6].label,"Rozdziel");assert_eq!(entries[12].label,"Zamknij wszystkie karty w splicie");
        s.vertical=true;let entries=tab_context_menu(&s);assert_eq!(entries[3].label,"Przesuń w górę");assert_eq!(entries[5].label,"Podział poziomy (obok siebie)");
        // Three or more panes can also lose a single member.
        s.members=vec![3,7,9];let entries=tab_context_menu(&s);assert_eq!(entries.iter().map(|e|e.command).collect::<Vec<_>>(),vec![36,20,0,7,8,9,11,6,0,23,24,0,10,12]);
        s.has_tab=false;s.http=false;let entries=tab_context_menu(&s);assert_eq!(entries.iter().map(|e|e.command).collect::<Vec<_>>(),vec![36,20,5]);
    }
    #[test] fn tab_menu_moves_middle_panes_both_ways_and_separates_groups() {
        use ffi::SidebarAction as A;let mut s=menu_state();s.members=vec![3,7,9];
        assert!(matches!(tab_menu_action(7,&s),A::MoveEarlier));assert!(matches!(tab_menu_action(8,&s),A::MoveLater));
        assert!(matches!(tab_menu_action(11,&s),A::Separate));assert!(matches!(tab_menu_action(6,&s),A::CloseSplit));assert!(matches!(tab_menu_action(12,&s),A::CloseGroup));
        s.tab=9;assert!(matches!(tab_menu_action(8,&s),A::None));s.tab=4;assert!(matches!(tab_menu_action(11,&s),A::None));assert!(matches!(tab_menu_action(12,&s),A::None));assert!(matches!(tab_menu_action(5,&s),A::OpenSplit));
        s.members=vec![4];assert!(matches!(tab_menu_action(11,&s),A::None));
    }
    #[test] fn reorder_target_picks_nearest_edge_folders_and_pin_section() {
        let row=|y:i32,pinned:bool,folder:bool|ffi::ReorderRow{bounds:ffi::Rect{x:0,y,width:200,height:34},pinned,folder};
        let rows=[row(10,true,true),row(46,true,false),row(100,false,false),row(136,false,false)];
        let t=sidebar_reorder_target(&rows,0);assert!(t.valid&&t.pinned&&!t.after);assert_eq!(t.row,0);
        let t=sidebar_reorder_target(&rows,27);assert!(t.into);assert_eq!(t.row,0);
        let t=sidebar_reorder_target(&rows,12);assert!(!t.into&&!t.after);
        let t=sidebar_reorder_target(&rows,70);assert_eq!((t.row,t.after,t.pinned),(1,true,true));
        let t=sidebar_reorder_target(&rows,95);assert_eq!((t.row,t.after,t.pinned),(2,false,false));
        let t=sidebar_reorder_target(&rows,500);assert_eq!((t.row,t.after,t.pinned),(3,true,false));
        let tabs=[row(100,false,false)];let t=sidebar_reorder_target(&tabs,50);assert!(t.valid&&t.pinned);assert_eq!(t.row,usize::MAX);
        assert!(!sidebar_reorder_target(&[],10).valid);
    }
    #[test] fn tab_menu_actions_revalidate_split_side_readiness_and_last_tab() {
        use ffi::SidebarAction as A;let mut s=menu_state();
        assert!(matches!(tab_menu_action(7,&s),A::MoveEarlier));assert!(matches!(tab_menu_action(8,&s),A::None));assert!(matches!(tab_menu_action(9,&s),A::ToggleOrientation));
        s.tab=3;assert!(matches!(tab_menu_action(8,&s),A::MoveLater));assert!(matches!(tab_menu_action(7,&s),A::None));
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
        let rows=vec![ffi::PreviewRow{index:0,kind:1,depth:0,height:13,members:0},ffi::PreviewRow{index:1,kind:0,depth:2,height:34,members:0},ffi::PreviewRow{index:0,kind:3,depth:0,height:8,members:0},ffi::PreviewRow{index:2,kind:0,depth:0,height:34,members:0}];
        let p=workspace_preview_layout(201,60,20,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![1,2]);
        assert_eq!((p[0].bounds.x,p[0].bounds.y,p[0].bounds.width),(24,-5,177));
        assert_eq!((p[0].clip.x,p[0].clip.y,p[0].clip.height),(24,0,29));
        assert_eq!((p[1].bounds.y,p[1].clip.height),(39,21));
    }
    #[test] fn workspace_preview_pairs_share_live_split_geometry_and_outer_clipping() {
        let rows=vec![ffi::PreviewRow{index:5,kind:2,depth:1,height:0,members:0}];let p=workspace_preview_layout(201,40,3,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![5,6,7]);
        let g=split_layout(189,46,2);
        for (i,r) in g.children.iter().enumerate(){assert_eq!((p[i+1].bounds.x,p[i+1].bounds.y,p[i+1].bounds.width,p[i+1].bounds.height),(12+r.x,-3+r.y,r.width,r.height));}
        assert_eq!((p[0].clip.y,p[0].clip.height),(0,40));
        let rows=vec![ffi::PreviewRow{index:5,kind:2,depth:0,height:0,members:3}];
        assert_eq!(workspace_preview_layout(201,60,0,&rows).iter().map(|p|p.index).collect::<Vec<_>>(),vec![5,6,7,8]);
    }
    #[test] fn workspace_preview_large_source_scroll_culls_offscreen_rows() {
        let rows:Vec<_>=(0..1000).map(|index|ffi::PreviewRow{index,kind:0,depth:0,height:0,members:0}).collect();
        let p=workspace_preview_layout(240,72,36*800,&rows);
        assert_eq!(p.iter().map(|p|p.index).collect::<Vec<_>>(),vec![800,801]);
        assert!(p.iter().all(|p|p.clip.y>=0&&p.clip.y+p.clip.height<=72));
    }
    #[test] fn workspace_preview_clamps_deep_narrow_and_saturated_input() {
        let rows=vec![ffi::PreviewRow{index:0,kind:0,depth:99,height:34,members:0}];
        assert!(workspace_preview_layout(20,100,0,&rows).is_empty());
        let p=workspace_preview_layout(240,100,-5,&rows);assert_eq!((p[0].bounds.x,p[0].bounds.y),(96,0));
        assert!(workspace_preview_layout(0,0,0,&rows).is_empty());
        let rows=vec![ffi::PreviewRow{index:0,kind:3,depth:0,height:i32::MAX,members:0},ffi::PreviewRow{index:1,kind:0,depth:0,height:34,members:0}];
        assert!(workspace_preview_layout(240,100,0,&rows).is_empty());
    }
    #[test] fn workspace_preview_uses_unicode_row_display_list_without_native_labels() {
        let p=workspace_preview_layout(240,30,4,&[ffi::PreviewRow{index:0,kind:0,depth:0,height:34,members:0}]);
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
        let mut f=row(400.0);f.kind=16;let ops=layout(&[f]);assert_eq!((ops[0].radius,ops[1].stroke),(design_tokens::radius::LG,1.0));
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
        assert_eq!((palette_width(560),palette_results_height(560),palette_width(1200)),(512,319,560));
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
        let g=pane_host_layout(809,600,&[0.5,0.5],false);
        assert_eq!((g.pages[0].width,g.pages[1].x,g.pages[1].width,g.dividers[0].width),(400,408,401,8));
        assert_eq!((g.pages[0].y,g.pages[0].height),(28,572));
        let v=pane_host_layout(600,809,&[0.5,0.5],true);
        assert_eq!((v.pages[0].height,v.pages[1].y,v.pages[1].height,v.dividers[0].y),(372,436,373,400));
        let single=pane_host_layout(600,809,&[1.0],false);assert_eq!(single.pages[0].height,809);assert!(single.bars.is_empty()&&single.dividers.is_empty());
        for width in [0,4,54,120] {
            let b=pane_bar_layout(width,30);assert!(b.menu.x+b.menu.width<=b.close.x);assert!(b.close.x+b.close.width<=width);
            assert!(pane_host_layout(width,4,&[f64::NAN,1.0],true).pages[1].height>=0);
            let many=pane_host_layout(width,40,&[1.0,1.0,1.0,1.0],false);assert!(many.pages.iter().all(|p|p.width>=0&&p.x+p.width<=width));
        }
    }
    #[test] fn pane_layout_tiles_many_panes_and_dividers_resize_only_neighbours() {
        let g=pane_host_layout(824,600,&[1.0,1.0,1.0],false);
        assert_eq!(g.pages.iter().map(|p|(p.x,p.width)).collect::<Vec<_>>(),vec![(0,269),(277,269),(554,270)]);
        assert_eq!(g.dividers.iter().map(|d|d.x).collect::<Vec<_>>(),vec![269,546]);
        let w=split_apply_ratio(&[0.25,0.25,0.5],0,0.75);assert!((w[0]-0.375).abs()<1e-9&&(w[1]-0.125).abs()<1e-9&&(w[2]-0.5).abs()<1e-9);
        assert!((split_pair_ratio(&w,0)-0.75).abs()<1e-9);assert!((split_pair_ratio(&w,1)-0.2).abs()<1e-9);
        assert_eq!(split_apply_ratio(&[0.5,0.5],0,0.99)[0],0.8);assert_eq!(split_pair_ratio(&[0.5,0.5],7),0.5);
        assert_eq!(split_normalized(&[0.0,1.0],2),vec![0.5,0.5]);assert_eq!(split_normalized(&[2.0,6.0],2),vec![0.25,0.75]);assert_eq!(split_normalized(&[1.0],3).len(),3);
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
        let p = tree.tab_plan("work", &[], &tabs, &[9, 3], true);
        assert_eq!(p.slots.len(), 3);
        assert!(p.slots[0].pair); assert_eq!((p.slots[0].first, p.slots[0].members.clone()), (2, vec![2, 0]));
        assert_eq!(p.slots[0].pin, usize::MAX);
        assert_eq!((p.slots[1].first, p.slots[2].first), (1, 3));
        assert!(!p.slots[1].pair && !p.slots[2].pair);
    }
    #[test] fn tab_plan_anchors_pinned_pairs_by_pane_order_even_in_collapsed_folders() {
        let mut tree = new_sidebar_tree(); tree.set_collapsed("work", "folder", true);
        let nodes = [node("folder", "", true), node("a", "folder", false), node("b", "", false)];
        let tabs = [tab(0, ""), tab(1, "a"), tab(2, "b")];
        let p = tree.tab_plan("work", &nodes, &tabs, &[1, 2], true);
        assert_eq!(p.slots.len(), 2); assert_eq!(p.slots[1].pin, 1);
        assert!(p.pins[0].visible && !p.pins[1].visible && !p.pins[2].visible);
        let swapped = tree.tab_plan("work", &nodes, &tabs, &[2, 1], true);
        assert_eq!((swapped.slots[1].members.clone(), swapped.slots[1].pin), (vec![2, 1], 2));
        let right_pinned = tree.tab_plan("work", &nodes, &tabs, &[0, 1], true);
        assert_eq!(right_pinned.slots[0].pin, 1);
    }
    #[test] fn tab_plan_groups_three_or_more_tabs_into_one_slot() {
        let tree = new_sidebar_tree();
        let tabs = [tab(3, ""), tab(7, ""), tab(9, ""), tab(12, "")];
        let p = tree.tab_plan("work", &[], &tabs, &[12, 3, 9], true);
        assert_eq!(p.slots.len(), 2); assert_eq!(p.slots[0].members, vec![3, 0, 2]); assert_eq!(p.slots[1].first, 1);
        assert!(tree.tab_plan("work", &[], &tabs, &[12, 3, 12], true).slots.iter().all(|s| !s.pair));
    }
    #[test] fn tab_plan_falls_back_for_invalid_pairs_and_unreachable_pins() {
        let tree = new_sidebar_tree();
        let nodes = [node("orphan", "missing", false), node("good", "", false)];
        let tabs = [tab(5, "orphan"), tab(8, "good"), tab(8, "duplicate")];
        let p = tree.tab_plan("work", &nodes, &tabs, &[5, 8], true);
        assert_eq!(p.slots.len(), 1); assert_eq!(p.slots[0].pin, 1); assert_eq!(p.pins.len(), 1);
        for (first, second, split) in [(5, 8, false), (5, 5, true), (5, 100, true)] {
            let p = tree.tab_plan("work", &nodes, &tabs, &[first, second], split);
            assert_eq!(p.slots.len(), 2); assert!(p.slots.iter().all(|s| !s.pair));
            assert_eq!((p.slots[0].first, p.slots[1].first), (0, 1));
        }
        assert!(tree.tab_plan("work", &[], &[], &[0, 1], true).slots.is_empty());
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
        assert!(!close_visible(true, false, false));
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
    let rect=|x,y,width,height,color,cl:ffi::FloatRect|ffi::Primitive{x,y,width,height,radius:0.0,color,stroke:0.0,shape:0,text:String::new(),font_size:0.0,font_weight:0,bold:false,middle:false,center:false,right_align:false,clip:cl,editor:false,utf16:vec![],text_x:0.0,baseline:0.0,text_direction:0,family:0,italic:false,tracking:0.0};
    let mut frame=rect(0.0,0.0,s.width,s.height,s.fill,full());frame.radius=if s.framed{5.0}else{0.0};ops.push(frame);
    if s.framed{let mut edge=rect(0.5,0.5,(s.width-1.0).max(0.0),(s.height-1.0).max(0.0),s.edge,full());edge.radius=4.5;edge.stroke=if s.focused{2.0}else{1.0};edge.shape=0;ops.push(edge);}
    for span in selection {if span.x.is_finite()&&span.width.is_finite()&&span.width>0.0{ops.push(rect(g.origin+span.x,g.caret.y,span.width,g.caret.height,s.selection_fill,clip()));}}
    let text=if state.text().is_empty(){placeholder}else{state.text()};
    if !text.is_empty() {
        let make=|color,cl:ffi::FloatRect|ffi::Primitive{x:0.0,y:0.0,width:s.width,height:s.height,radius:0.0,color,stroke:0.0,shape:0,text:String::from_utf16_lossy(text),font_size:s.font_size,font_weight:0,bold:false,middle:false,center:false,right_align:false,clip:cl,editor:true,utf16:text.to_vec(),text_x:if state.text().is_empty()&&s.right_align{g.clip.x+g.clip.width-s.placeholder_width}else{g.origin},baseline:g.baseline,text_direction:s.text_direction,family:0,italic:false,tracking:0.0};
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
