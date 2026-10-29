#include "base/i18n/number_formatting.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/vector_icons.h"
#include "ui/views/mouse_constants.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_observer.h"
#include "ui/base/class_property.h"
#include "ui/views/window/dialog_client_view.h"
#include "chromium/prototype/page_dialogs.h"
#include "base/i18n/time_formatting.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/i18n.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/scoped_observation.h"
#include "base/command_line.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_layout.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/hash/hash.h"
#include "base/no_destructor.h"
#include "base/i18n/break_iterator.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/paint_record.h"
#include "ui/compositor/paint_recorder.h"
#include "components/url_formatter/url_fixer.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/hit_test.h"
#include "ui/base/menu_source_utils.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/gfx/text_utils.h"
#include "ui/gfx/text_elider.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/prefix_selector.h"
#include "ui/base/ime/input_method.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"
#include "ui/views/controls/resize_area.h"
#include "ui/base/cursor/cursor.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/native_theme/native_theme.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_delegate.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/closure_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider_key.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/dialog_model_field.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"

#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#include "chromium/prototype/workspace_frame_mac.h"
#endif

namespace zitter::prototype {class SceneParentNodes;}
DEFINE_UI_CLASS_PROPERTY_TYPE(zitter::prototype::SceneParentNodes*)

namespace zitter::prototype {
class SidebarNodeGraphState {
 public:
  explicit SidebarNodeGraphState(bool palette=false):graph(palette?sidebar::new_palette_graph():sidebar::new_action_graph()){}
  rust::Box<sidebar::ActionGraph> graph;
};
struct SidebarNodeBinding {std::shared_ptr<SidebarNodeGraphState> scope;uint32_t kind=0;std::array<uint64_t,2> identity{};std::string name;};
rust::Box<sidebar::ActionControl> AcquireSidebarInput(const SidebarNodeBinding& binding){
  auto state=binding.scope?binding.scope->graph->acquire({binding.kind,binding.identity[0],binding.identity[1],binding.name}):sidebar::new_action_control();
  // Kind zero in a scope is an intentionally inactive adapter, with a retired handle.
  CHECK(!binding.scope||binding.kind==0||state->action_node_id()!=0)<<"Unreconciled sidebar node";return state;
}
class ImportFormState {
 public:
  rust::Box<sidebar::ImportForm> form=sidebar::new_import_form();
};
class WorkspaceFormState {
 public:
  rust::Box<sidebar::WorkspaceForm> form=sidebar::new_workspace_form();
};
class ToolsFormState {
 public:
  rust::Box<sidebar::ToolsForm> form=sidebar::new_tools_form();
};
class PreferencesFormState {
 public:
  explicit PreferencesFormState(size_t categories):form(sidebar::new_preferences_form(categories)){}
  rust::Box<sidebar::PreferencesForm> form;
};
class ProfileFormState {
 public:
  rust::Box<sidebar::ProfileForm> form=sidebar::new_profile_form();
};
// Rust owns form action membership. Native pointers only identify adapters for
// removal notifications; never read a child state while its destructor runs.
class SceneGroupNodes final : public views::ViewObserver {
 public:
  explicit SceneGroupNodes(views::View* owner,bool vertical=false,int spacing=0,bool intrinsic=false,bool dialog=false):graph_(sidebar::new_form_graph(vertical,spacing,intrinsic,dialog)){observation_.Observe(owner);}
  template<class T,class... Args> std::unique_ptr<T> Make(Args&&... args){
    auto input=graph_->append_form_action();const auto id=input->action_node_id();CHECK_NE(id,0u);
    auto view=std::make_unique<T>(std::forward<Args>(args)...,std::move(input));
    CHECK(members_.emplace(view.get(),Member{id,+[](views::View* view){auto* typed=static_cast<T*>(view);if constexpr(requires{typed->CancelInteraction();})typed->CancelInteraction();else typed->OnMouseCaptureLost();}}).second);return view;
  }
  uint64_t Id(const views::View* view) const{const auto found=members_.find(view);return found==members_.end()?0:found->second.id;}
  sidebar::FormGroupStyle Style() const{return graph_->form_style();}
  void Share(bool shared){if(graph_->form_set_shared(shared))CancelAdapters();}
  sidebar::FormLayout Geometry(int width,int height,bool preferred,int link,int top,int left,int bottom,int right,const std::vector<sidebar::FormMeasure>& measured) const{return graph_->form_layout(width,height,preferred,link,top,left,bottom,right,rust::Slice<const sidebar::FormMeasure>(measured.data(),measured.size()));}
  void RetireAll(){graph_->reconcile({});members_.clear();}
  void OnViewHierarchyChanged(views::View* owner,const views::ViewHierarchyChangedDetails& details) override{
    if(details.is_add||details.parent!=owner)return;
    const auto found=members_.find(details.child.get());if(found==members_.end())return;
    // The child may already be in its base destructor. Only the registered ID
    // is safe here; cancellation callbacks are never invoked during removal.
    CHECK(graph_->retire_form_action(found->second.id));members_.erase(found);
  }
  void OnChildViewReordered(views::View* owner,views::View*) override{rust::Vec<uint64_t> ids;for(const auto& child:owner->children())if(const auto id=Id(child.get()))ids.push_back(id);CHECK(graph_->reorder_form_actions(std::move(ids)));CancelAdapters();}
  void OnViewIsDeleting(views::View*) override{RetireAll();observation_.Reset();}
 private:
  void CancelAdapters(){if(!observation_.IsObserving())return;for(const auto& child:observation_.GetSource()->children()){const auto found=members_.find(child.get());if(found!=members_.end()){found->second.cancel(child.get());child->SchedulePaint();}}}
  struct Member {uint64_t id;void(*cancel)(views::View*);};
  rust::Box<sidebar::ActionGraph> graph_;
  std::map<const views::View*,Member> members_;
  base::ScopedObservation<views::View,views::ViewObserver> observation_{this};
};

// A property owns the native scope adapter; Rust owns its structural root and
// ordered control membership. The scope observes direct removal and reordering.
class SceneParentNodes final {
 public:
  explicit SceneParentNodes(views::View* parent):nodes_(parent){}
  template<class T,class... Args> std::unique_ptr<T> Make(Args&&... args){return nodes_.Make<T>(std::forward<Args>(args)...);}
 private:
  SceneGroupNodes nodes_;
};

namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFolderNameField);
// Color roles. The values only name a role: Tone() resolves each one from the
// design system palette (design.rs) for the view's scheme and workspace color.
constexpr SkColor kInk = 0xFF1B1A22;
constexpr SkColor kSubtle = 0xFF4A4757;
constexpr SkColor kMuted = 0xFF5E5B6B;
constexpr SkColor kDisabled = 0xFFA9A6B5;
constexpr SkColor kCard = SK_ColorWHITE;
constexpr SkColor kAccentWash = 0xFFEEEDFD;
constexpr SkColor kChip = 0xFFF1F0F5;
constexpr SkColor kFrameBase = 0xFFF4F3F7;
constexpr SkColor kError = 0xFFB3261E;
constexpr int kSidebarWidth = 248;
constexpr int kMinSidebarWidth = 200;
constexpr int kMaxSidebarWidth = 480;
// The sidebar never takes more than half of the window.
int SidebarColumn(int preferred, int window_width) {
  return std::min(preferred, std::max(120, window_width / 2));
}
// The design system's named workspace colors (WorkspaceColor::PRESETS).
constexpr size_t kWorkspacePresetCount = 8;
SkColor WorkspacePreset(size_t index) { return sidebar::appearance_presets(false)[index].color; }
constexpr SkColor kDefaultWorkspaceColor = 0xFF928FD4;  // Violet.
constexpr std::array<const char16_t*, 6> kWorkspaceEmojis = {
    u"💼", u"🏠", u"🚀", u"🎨", u"📚", u"🎮"};
constexpr int kGap = 8;
#if BUILDFLAG(IS_MAC)
constexpr int kTrafficLightInset = 80;
#else
constexpr int kTrafficLightInset = 8;
#endif
// Compact header commands, centered with the traffic lights.
constexpr int kHeaderButtonSize = 28;
constexpr int kHeaderLightsGap = 14;
// Monogram tiles take a workspace preset (violet, coral, sky, amber, rose, mint,
// green); painting turns it into that color's palette `accent` and `on-accent`.
constexpr std::array<SkColor, 7> kMonogramColors = {
    0xFF928FD4, 0xFFCC8463, 0xFF5AA0D0, 0xFFBC8F4D,
    0xFFCB7E96, 0xFF55AC89, 0xFF7DA667};

bool IsDark(const views::View* view) {
  return view && view->GetWidget() && view->GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark;
}
// The workspace color each top-level window is painted with. Keys are only
// compared, never dereferenced; windows the shell has not registered (bubbles,
// menus) use the color painted last.
std::map<const void*, SkColor>& WindowWorkspaceColors() {
  static base::NoDestructor<std::map<const void*, SkColor>> colors;
  return *colors;
}
SkColor& LastWorkspaceColor() {
  static SkColor color = kDefaultWorkspaceColor;
  return color;
}
const void* TopLevel(const views::View* view) {
  const auto* widget = view ? view->GetWidget() : nullptr;
  return widget ? widget->GetTopLevelWidget() : nullptr;
}
SkColor WindowWorkspaceColor(const views::View* view) {
  const auto& colors = WindowWorkspaceColors();
  const auto it = colors.find(TopLevel(view));
  return it == colors.end() ? LastWorkspaceColor() : it->second;
}
// The design system palette for a view: Noc or Papier from its color mode,
// tinted by its window's workspace color (or `workspace`, when given).
sidebar::DesignPalette Palette(const views::View* view, std::optional<SkColor> workspace = std::nullopt) {
  return sidebar::design_palette(IsDark(view), workspace.value_or(WindowWorkspaceColor(view)));
}
SkColor Tone(const views::View* view, SkColor role) {
  const auto p = Palette(view);
  switch (role) {
    case kInk: return p.ink;
    case kSubtle:
    case kMuted: return p.ink_muted;
    case kDisabled: return p.ink_faint;
    case kCard: return p.bg_raised;
    case kAccentWash: return p.accent_soft;
    case kChip: return p.bg_hover;
    case kFrameBase: return p.bg_base;
    case kError: return p.danger;
    default: return role;
  }
}
SkColor Hairline(const views::View* view) { return Palette(view).line; }
SkColor HoverWash(const views::View* view) { return Palette(view).bg_hover; }
SkColor Mix(SkColor a, SkColor b, int percent_a) {
  auto channel = [&](U8CPU x, U8CPU y) { return (x * percent_a + y * (100 - percent_a)) / 100; };
  return SkColorSetRGB(channel(SkColorGetR(a), SkColorGetR(b)),
                       channel(SkColorGetG(a), SkColorGetG(b)),
                       channel(SkColorGetB(a), SkColorGetB(b)));
}
// The workspace color tints the whole window; the frame and sidebar share bg_base.
SkColor FrameTint(SkColor workspace, const views::View* view) { return Palette(view, workspace).bg_base; }
// The window frame around the sidebar and the page: the deepest layer, `bg-void`.
SkColor FrameVoid(SkColor workspace, const views::View* view) { return Palette(view, workspace).bg_void; }

std::string Hex(SkColor color) { return base::StringPrintf("#%06X", color & 0xFFFFFF); }
std::optional<SkColor> ParseColor(std::string_view text) {
  unsigned value;
  if (text.size() != 7 || text[0] != '#' ||
      !base::HexStringToUInt(text.substr(1), &value)) return std::nullopt;
  return value | 0xFF000000;
}
struct Appearance {
  std::map<std::string, SkColor> colors = {{"work", kDefaultWorkspaceColor}, {"personal", 0xFF4FAC8F}};  // Violet, mint.
  // Tabs live in a visible sidebar by default; auto-hide is opt-in.
  bool pinned = true;
  ThemeMode theme = ThemeMode::kSystem;
  SearchEngine search = SearchEngine::kDuckDuckGo;
  bool animations = true;
  // The active space shows its name beside its dot in the sidebar rail.
  bool workspace_names = true;
  // Pinned folders start collapsed; an item opened elsewhere peeks out of its folder.
  bool folders_collapsed = true;
  int sidebar_width = kSidebarWidth;
  std::map<std::string, std::string> emoji;
};
Appearance ReadAppearance(const base::FilePath& path) {
  Appearance result;
  std::string json;
  if (!base::ReadFileToString(path, &json)) return result;
  auto dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) return result;
  for (const auto [id, value] : *dict) {
    if (value.is_string()) {
      if (auto color = ParseColor(value.GetString())) result.colors[id] = *color;
    }
  }
  result.pinned = dict->FindBool("sidebar_pinned").value_or(true);
  if (const auto* theme = dict->FindString("theme")) {
    if (*theme == "dark") result.theme = ThemeMode::kDark;
    else if (*theme == "light") result.theme = ThemeMode::kLight;
  }
  if (const auto* search = dict->FindString("search_engine")) {
    if (*search == "google") result.search = SearchEngine::kGoogle;
    else if (*search == "bing") result.search = SearchEngine::kBing;
  }
  result.animations = dict->FindBool("workspace_animations").value_or(true);
  result.workspace_names = dict->FindBool("workspace_names").value_or(true);
  result.folders_collapsed = dict->FindBool("pinned_folders_collapsed").value_or(true);
  if (const auto* emoji = dict->FindDict("workspace_emoji")) {
    for (const auto [id, value] : *emoji)
      if (value.is_string() && !value.GetString().empty() && value.GetString().size() <= 64)
        result.emoji[id] = value.GetString();
  }
  result.sidebar_width = std::clamp(dict->FindInt("sidebar_width").value_or(kSidebarWidth),
                                    kMinSidebarWidth, kMaxSidebarWidth);
  return result;
}
bool SavePatch(const base::FilePath& path, std::optional<std::string> id,
               SkColor color, std::optional<bool> pinned, std::optional<ThemeMode> theme,
               std::optional<SearchEngine> search, std::optional<bool> animations,
               std::optional<int> sidebar_width,
               std::optional<std::pair<std::string, std::string>> emoji,
               std::optional<bool> workspace_names,
               std::optional<bool> folders_collapsed) {
  auto appearance = ReadAppearance(path);
  if (id) appearance.colors[*id] = color;
  if (pinned) appearance.pinned = *pinned;
  if (theme) appearance.theme = *theme;
  if (search) appearance.search = *search;
  if (animations) appearance.animations = *animations;
  if (workspace_names) appearance.workspace_names = *workspace_names;
  if (folders_collapsed) appearance.folders_collapsed = *folders_collapsed;
  if (sidebar_width) appearance.sidebar_width = *sidebar_width;
  if (emoji && emoji->second.empty()) appearance.emoji.erase(emoji->first);
  else if (emoji) appearance.emoji[emoji->first] = emoji->second;
  base::DictValue dict;
  for (const auto& [key, value] : appearance.colors) dict.Set(key, Hex(value));
  dict.Set("sidebar_pinned", appearance.pinned);
  dict.Set("theme", appearance.theme == ThemeMode::kDark ? "dark" : appearance.theme == ThemeMode::kLight ? "light" : "system");
  dict.Set("search_engine", appearance.search == SearchEngine::kGoogle ? "google" : appearance.search == SearchEngine::kBing ? "bing" : "duckduckgo");
  dict.Set("workspace_animations", appearance.animations);
  dict.Set("workspace_names", appearance.workspace_names);
  dict.Set("pinned_folders_collapsed", appearance.folders_collapsed);
  dict.Set("sidebar_width", appearance.sidebar_width);
  base::DictValue emoji_dict;
  for (const auto& [key, value] : appearance.emoji) emoji_dict.Set(key, value);
  dict.Set("workspace_emoji", std::move(emoji_dict));
  std::string json;
  return base::JSONWriter::Write(dict, &json) &&
         base::ImportantFileWriter::WriteFileAtomically(path, json);
}

gfx::FontList Font(int size_delta, gfx::Font::Weight weight = gfx::Font::Weight::NORMAL) {
  return gfx::FontList().DeriveWithSizeDelta(size_delta).DeriveWithWeight(weight);
}
// A design system family (1 display, 2 sans, 3 mono; design.rs family_id). Falls
// back to the system font where the bundled fonts are not active (test runs).
gfx::FontList DesignFont(uint32_t family,float size,int weight,bool italic) {
  const int weight_value=std::clamp(weight>0?weight:400,100,900)/100*100;
  return gfx::FontList({std::string(sidebar::design_font_family(family))},italic?gfx::Font::ITALIC:gfx::Font::NORMAL,
      std::max(1,static_cast<int>(std::lround(size))),static_cast<gfx::Font::Weight>(weight_value));
}
void FillRound(gfx::Canvas* canvas, const gfx::Rect& rect, int radius, SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  canvas->DrawRoundRect(gfx::RectF(rect), radius, flags);
}
void StrokeRound(gfx::Canvas* canvas, const gfx::Rect& rect, int radius, SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(color);
  gfx::RectF bounds(rect);
  bounds.Inset(0.5f);
  canvas->DrawRoundRect(bounds, radius, flags);
}
void PaintIcon(gfx::Canvas* canvas, const gfx::VectorIcon& icon, const gfx::Rect& box,
               int size, SkColor color) {
  gfx::ScopedCanvas scoped(canvas);
  canvas->Translate(gfx::Vector2d(box.x() + (box.width() - size) / 2,
                                  box.y() + (box.height() - size) / 2));
  gfx::PaintVectorIcon(canvas, icon, size, color);
}
class ThemedLabel final : public views::Label {
  METADATA_HEADER(ThemedLabel, views::Label)
 public:
  ThemedLabel(std::u16string text, SkColor color) : views::Label(text), color_(color) { SetTone(color); }
  void SetTone(SkColor color) { color_ = color; SetEnabledColor(Tone(this, color_)); }
  void OnThemeChanged() override { views::Label::OnThemeChanged(); SetEnabledColor(Tone(this, color_)); }
 private:
  SkColor color_;
};
BEGIN_METADATA(ThemedLabel)
END_METADATA

// Settings surfaces follow the window theme, including live system changes.
class PreferenceSurface final : public views::View {
  METADATA_HEADER(PreferenceSurface, views::View)
 public:
  explicit PreferenceSurface(bool navigation=false,base::RepeatingClosure changed={},base::RepeatingCallback<bool()> visible={}):navigation_(navigation),changed_(std::move(changed)),visible_(std::move(visible)){}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const{sidebar::Item item{};item.kind=24;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.fill=Tone(this,navigation_?kFrameBase:kCard);item.ink=Hairline(this);item.active=navigation_;item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Notify();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Notify();}
  void AddedToWidget() override{views::View::AddedToWidget();Notify();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);Notify();}
  void OnPaint(gfx::Canvas* canvas) override{views::View::OnPaint(canvas);
    if(navigation_||!visible_||!visible_.Run()){auto item=Scene(gfx::RectF(GetLocalBounds()),gfx::RectF(GetLocalBounds()));auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));PaintEditorSceneFallback(canvas,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(ops.data(),ops.size())));}
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(nullptr);SetBorder(views::CreateEmptyBorder(navigation_?gfx::Insets():gfx::Insets(1)));Notify();
  }
 private:
  void Notify(){if(changed_)changed_.Run();}
  bool navigation_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;
};
BEGIN_METADATA(PreferenceSurface)
END_METADATA

// Bubble panels and moving row layers are translucent. Native controls also
// contain Labels, which must use grayscale text on those surfaces.
void PreparePanelText(views::View* view) {
  if (auto* label = views::AsViewClass<views::Label>(view)) label->SetSubpixelRenderingEnabled(false);
  for (const auto& child : view->children()) PreparePanelText(child);
}

views::Label* Label(views::View* parent, std::u16string text, SkColor color,
                    gfx::FontList font = gfx::FontList()) {
  auto* label = parent->AddChildView(std::make_unique<ThemedLabel>(text, color));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetFontList(font);
  return label;
}
views::View* Spacer(views::View* parent, int height) {
  auto* spacer = parent->AddChildView(std::make_unique<views::View>());
  spacer->SetPreferredSize(gfx::Size(1, height));
  return spacer;
}
std::u16string HostLabel(const GURL& url) {
  std::string host(url.host());
  if (host.starts_with("www.")) host = host.substr(4);
  return base::UTF8ToUTF16(host);
}

enum class ControlGlyph { kVector, kSidebar, kDot, kSwatch, kMore };
void PaintGlyphControl(views::View* view,ControlGlyph glyph,const gfx::VectorIcon* icon,SkColor color,bool on,const std::u16string& label,views::Button::ButtonState state,gfx::Canvas* canvas){
    const auto bounds = view->GetLocalBounds();
    if (glyph != ControlGlyph::kDot && glyph != ControlGlyph::kSwatch && view->GetEnabled() &&
        (state == views::Button::STATE_HOVERED || state == views::Button::STATE_PRESSED)) {
      FillRound(canvas, bounds, 8, HoverWash(view));
    }
    // Icon buttons: `ink-muted` at rest, `ink` on hover, `accent-ink` on `accent-soft` when on.
    const bool lit = state == views::Button::STATE_HOVERED || state == views::Button::STATE_PRESSED;
    SkColor ink = view->GetEnabled() ? Tone(view, lit && glyph != ControlGlyph::kSwatch ? kInk : color) : Tone(view, kDisabled);
    if (on && glyph == ControlGlyph::kSidebar && view->GetEnabled()) {
      const auto p = Palette(view);
      FillRound(canvas, bounds, 8, p.accent_soft);
      ink = p.accent_ink;
    }
    const auto center = bounds.CenterPoint();
    switch (glyph) {
      case ControlGlyph::kVector:
        PaintIcon(canvas, *icon, bounds, 16, ink);
        break;
      case ControlGlyph::kSidebar: {
        const gfx::Rect frame(center.x() - 8, center.y() - 7, 16, 14);
        StrokeRound(canvas, frame, 3, ink);
        canvas->FillRect(gfx::Rect(frame.x() + 5, frame.y(), 1, frame.height()), ink);
        if (on) canvas->FillRect(gfx::Rect(frame.x() + 1, frame.y() + 1, 4, frame.height() - 2), ink);
        break;
      }
      case ControlGlyph::kDot: {
        if (!label.empty()) {
          const gfx::Rect tile(center.x() - 13, center.y() - 13, 26, 26);
          if (on) FillRound(canvas, tile, 7, SkColorSetA(Tone(view, kInk), 0x1F));
          else if (state == views::Button::STATE_HOVERED) FillRound(canvas, tile, 7, HoverWash(view));
          canvas->DrawStringRectWithFlags(label, Font(3), Tone(view, kInk), tile,
                                          gfx::Canvas::TEXT_ALIGN_CENTER);
          break;
        }
        const gfx::Rect dot(center.x() - 4, center.y() - 4, 8, 8);
        if (on) FillRound(canvas, dot, 4, Tone(view, kInk));
        else StrokeRound(canvas, dot, 4, state == views::Button::STATE_HOVERED ? Tone(view, kInk) : Tone(view, kMuted));
        break;
      }
      case ControlGlyph::kMore:
        for (int dx : {-5, 0, 5}) FillRound(canvas, gfx::Rect(center.x() + dx - 1, center.y() - 1, 3, 3), 2, ink);
        break;
      case ControlGlyph::kSwatch: {
        const gfx::Rect ring(center.x() - 10, center.y() - 10, 20, 20);
        if (on || state == views::Button::STATE_HOVERED) StrokeRound(canvas, ring, 10, on ? Tone(view, kInk) : Tone(view, kMuted));
        FillRound(canvas, gfx::Rect(center.x() - 7, center.y() - 7, 14, 14), 7, color);
        break;
      }
    }
}
// The workspace switcher's rail: a recessed `bg-void` pill with an inner `line`
// edge behind the dots (the Metal surface draws its own).
class WorkspaceRail final : public views::View {
  METADATA_HEADER(WorkspaceRail, views::View)
 public:
  explicit WorkspaceRail(base::RepeatingCallback<bool()> scene_visible) : scene_visible_(std::move(scene_visible)) {}
  void OnPaint(gfx::Canvas* canvas) override {
    if (scene_visible_ && scene_visible_.Run()) return;
    if (children().empty()) return;
    const auto p = Palette(this);
    gfx::RectF rail(GetLocalBounds());
    rail.Inset(gfx::InsetsF::VH(std::max(0.f, (rail.height() - 28) / 2), 0));
    cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(p.bg_void);
    canvas->DrawRoundRect(rail, rail.height() / 2, flags);
    rail.Inset(0.5f); flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(1); flags.setColor(p.line);
    canvas->DrawRoundRect(rail, rail.height() / 2, flags);
  }
 private:
  base::RepeatingCallback<bool()> scene_visible_;
};
BEGIN_METADATA(WorkspaceRail)
END_METADATA

// 32×32 icon button with a soft hover wash, used for chrome-level commands.
class GlyphButton : public views::Button {
  METADATA_HEADER(GlyphButton, views::Button)
 public:
  using Glyph=ControlGlyph;
  GlyphButton(PressedCallback callback, std::u16string name, Glyph glyph,
              const gfx::VectorIcon* icon = nullptr)
      : views::Button(std::move(callback)), glyph_(glyph), icon_(icon) {
    SetTooltipText(name);
    GetViewAccessibility().SetName(name);
  }
  const gfx::VectorIcon* icon() const { return icon_; }
  Glyph glyph() const { return glyph_; }
  bool on() const { return on_; }
  const std::u16string& label() const { return label_; }
  void set_color(SkColor color) { color_ = color; SchedulePaint(); }
  SkColor color() const {return color_;}
  void set_on(bool on) { if (on_==on) return; on_ = on; SchedulePaint(); }
  // A workspace emoji replaces the plain dot.
  void set_label(std::u16string label) { label_ = std::move(label); PreferredSizeChanged(); }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    if (glyph_ == Glyph::kDot) return label_.empty() ? gfx::Size(24, 28) : gfx::Size(30, 28);
    return gfx::Size(32, 32);
  }
  void StateChanged(ButtonState) override { SchedulePaint(); }
  void PaintButtonContents(gfx::Canvas* canvas) override {PaintGlyphControl(this,glyph_,icon_,color_,on_,label_,GetState(),canvas);}
 private:
  Glyph glyph_;
  raw_ptr<const gfx::VectorIcon> icon_;
  SkColor color_ = kSubtle;
  bool on_ = false;
  std::u16string label_;
};
BEGIN_METADATA(GlyphButton)
END_METADATA

// IsDrawn follows View visibility only; a hidden Widget can still have a drawn
// hierarchy. Input admission and native Metal subviews must also track the host.
bool SceneIsVisible(const views::View* view){return view&&view->IsDrawn()&&view->GetWidget()&&view->GetWidget()->IsVisible();}
class SceneWindowObserver final : public views::WidgetObserver {
 public:
  explicit SceneWindowObserver(base::RepeatingClosure changed):changed_(std::move(changed)){}
  void Observe(views::Widget* widget){if(observation_.GetSource()==widget)return;observation_.Reset();if(widget)observation_.Observe(widget);}
  void OnWidgetVisibilityChanged(views::Widget*,bool) override {changed_.Run();}
  void OnWidgetDestroying(views::Widget*) override {observation_.Reset();}
 private:
  base::RepeatingClosure changed_;base::ScopedObservation<views::Widget,views::WidgetObserver> observation_{this};
};


// Active workspace pill: `space-2` inset, the 10 px dot (or emoji) and its name in
// `label` 600, closing on `space-3`.
int WorkspacePillWidth(const std::u16string& name,bool emoji){
  return std::min(140,8+(emoji?18:10)+6+gfx::GetStringWidth(name,DesignFont(2,12,600,false))+12);
}
sidebar::Item GlyphSceneData(views::View* view,ControlGlyph glyph,const gfx::VectorIcon* icon,bool on,const std::u16string& label,SkColor color,bool hovered,bool focused,gfx::RectF rect,gfx::RectF clip){
  sidebar::Item item{}; item.kind=7;
  item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
  item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
  item.glyph=glyph==GlyphButton::Glyph::kSidebar ? 0 : glyph==GlyphButton::Glyph::kDot ? 1 : glyph==GlyphButton::Glyph::kMore ? 6
      : icon==&vector_icons::kArrowBackIcon ? 3 : icon==&vector_icons::kArrowForwardIcon ? 4
      : icon==&vector_icons::kReloadCustomIcon ? 5 : icon==&vector_icons::kCloseSmallIcon ? 7 : icon==&vector_icons::kSearchIcon ? 8 : 2;
  item.active=on; item.focused=focused; item.focus=Palette(view).focus;
  item.hovered=hovered;
  item.ink=Tone(view,!view->GetEnabledInViewsSubtree() ? kDisabled : item.glyph==1 ? item.active || item.hovered ? kInk : kMuted : item.hovered ? kInk : kSubtle);
  item.monogram=base::UTF16ToUTF8(label); item.font_size=item.glyph==1&&rect.height()>=36?18:Font(3).GetFontSize();
  item.fill=item.glyph==1 ? SK_ColorTRANSPARENT : item.hovered ? HoverWash(view) : SK_ColorTRANSPARENT;
  if(item.glyph==1){
    // Each dot keeps its own workspace's accent; the active one is a raised pill.
    const auto own=sidebar::design_palette(IsDark(view),color==kSubtle?kDefaultWorkspaceColor:color);const auto p=Palette(view);
    item.badge=own.accent;item.detail_ink=p.ink;
    if(item.active&&!item.monogram.empty())item.fill=SkColorSetA(p.ink,0x1f);else if(item.hovered)item.fill=HoverWash(view);
  } else if(item.active&&item.glyph==0&&view->GetEnabledInViewsSubtree()){
    // A pressed icon button: `accent-ink` on `accent-soft`.
    const auto p=Palette(view);item.fill=p.accent_soft;item.ink=p.accent_ink;
  }
  if(glyph==GlyphButton::Glyph::kSwatch){item.kind=18;item.badge=color;item.ink=Tone(view,!view->GetEnabledInViewsSubtree()?kDisabled:item.active?kInk:kMuted);
    // A preset pill: the color's name beside its dot, the selected one on accent_soft.
    const auto name=label.empty()?view->GetTooltipText():label;
    if(rect.width()>rect.height()*1.6f&&!name.empty()){const auto p=Palette(view);item.title=base::UTF16ToUTF8(name);item.font_size=Font(-1).GetFontSize();item.text_style=6;
      item.fill=on?p.accent_soft:item.hovered?p.bg_hover:SK_ColorTRANSPARENT;item.edge=on?p.accent:p.line;}}
  return item;
}
sidebar::Item GlyphSceneItem(GlyphButton* button,gfx::RectF rect,gfx::RectF clip){return GlyphSceneData(button,button->glyph(),button->icon(),button->on(),button->label(),button->color(),button->GetEnabledInViewsSubtree()&&(button->GetState()==views::Button::STATE_HOVERED||button->GetState()==views::Button::STATE_PRESSED),button->HasFocus(),rect,clip);}
// Rasterization behind the native editor and GPU fallback consume the same
// Rust recipe; no Views background, border or hidden text layout is required.
void PaintSceneRecipe(gfx::Canvas* canvas,const sidebar::Item& item,const gfx::FontList& font,bool styled_text=false) {
  const auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));
  for(const auto& op:ops) {
    gfx::ScopedCanvas clipped(canvas);canvas->ClipRect(gfx::Rect(op.clip.x,op.clip.y,op.clip.width,op.clip.height));
    if(op.text.size()){
      auto text=base::UTF8ToUTF16(std::string(op.text));auto text_font=op.family?DesignFont(op.family,op.font_size,op.font_weight,op.italic):font;
      if(styled_text&&!op.family)text_font=font.DeriveWithSizeDelta(std::max(1,static_cast<int>(op.font_size))-font.GetFontSize());
      if(styled_text)text=gfx::ElideText(text,text_font,op.width,op.middle?gfx::ELIDE_MIDDLE:gfx::ELIDE_TAIL);
      canvas->DrawStringRectWithFlags(text,text_font,op.color,gfx::Rect(op.x,op.y,op.width,op.height),op.center?gfx::Canvas::TEXT_ALIGN_CENTER:op.right_align?gfx::Canvas::TEXT_ALIGN_RIGHT:gfx::Canvas::TEXT_ALIGN_LEFT);
    }
    else {cc::PaintFlags flags;flags.setAntiAlias(true);flags.setColor(op.color);if(op.stroke>0){flags.setStyle(cc::PaintFlags::kStroke_Style);flags.setStrokeWidth(op.stroke);}canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);}
  }
}

// A decorative image has no action controller, capture, focus ring or input state.
class SceneSearchIcon final : public views::View {
  METADATA_HEADER(SceneSearchIcon,views::View)
 public:
  explicit SceneSearchIcon(base::RepeatingCallback<bool()> visible):visible_(std::move(visible)) {
    SetTooltipText(T(u"Szukaj"));GetViewAccessibility().SetName(T(u"Szukaj"));GetViewAccessibility().SetRole(ax::mojom::Role::kImage);
    SetFocusBehavior(FocusBehavior::NEVER);SetCanProcessEventsWithinSubtree(false);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {const auto r=sidebar::action_preferred_size(false,false);return gfx::Size(r.width,r.height);}
  // The Command Center field opens with the ket's italic stroke in `accent-ink`.
  sidebar::Item SceneItem(gfx::RectF bounds,gfx::RectF clip) {
    sidebar::Item item{};item.kind=17;item.glyph=1;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();
    item.clip={clip.x(),clip.y(),clip.width(),clip.height()};item.title="|";item.ink=Palette(this).accent_ink;item.font_size=28;item.text_style=2;return item;}
  // While results load the ket gives way to `zt-loader`: three 5 px dots (zing,
  // accent, zing) jittering 1 px in five steps of 52 ms, offset by 90/170 ms.
  void SetLoading(bool loading){
    if(loading_==loading)return;loading_=loading;step_=0;
    if(loading_&&!gfx::Animation::PrefersReducedMotion())timer_.Start(FROM_HERE,base::Milliseconds(52),base::BindRepeating(&SceneSearchIcon::Tick,base::Unretained(this)));else timer_.Stop();
    GetViewAccessibility().SetName(loading_?T(u"Wczytywanie wyników"):T(u"Szukaj"));SchedulePaint();}
  bool loading() const{return loading_;}
  void OnPaint(gfx::Canvas* canvas) override {if(visible_&&visible_.Run())return;
    if(!loading_){canvas->DrawStringRectWithFlags(u"|",DesignFont(1,28,400,true),Palette(this).accent_ink,GetLocalBounds(),gfx::Canvas::TEXT_ALIGN_CENTER);return;}
    static constexpr std::array<std::pair<int,int>,5> kJitter={{{0,0},{1,-1},{-1,1},{1,1},{-1,-1}}};
    const auto p=Palette(this);const auto c=GetLocalBounds().CenterPoint();
    for(int dot=0;dot<3;++dot){const auto [dx,dy]=kJitter[(step_+(dot==0?0:dot==1?2:3))%5];
      FillRound(canvas,gfx::Rect(c.x()-10+dot*8+dx,c.y()-2+dy,5,5),3,dot==1?p.accent:p.zing);}}
 private:
  void Tick(){step_=(step_+1)%5;SchedulePaint();}
  base::RepeatingCallback<bool()> visible_;bool loading_=false;int step_=0;base::RepeatingTimer timer_;
};
BEGIN_METADATA(SceneSearchIcon)
END_METADATA

// Event/focus/AX delivery remains native; state and activation live in Rust.
class RustActionView : public views::View {
  METADATA_HEADER(RustActionView,views::View)
 public:
  RustActionView(views::Button::PressedCallback callback,std::u16string name,ControlGlyph glyph,const gfx::VectorIcon* icon,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,bool native_focus_ring,rust::Box<sidebar::ActionControl> state=sidebar::new_action_control())
      :callback_(std::move(callback)),glyph_(glyph),icon_(icon),visible_(std::move(visible)),changed_(std::move(changed)),state_(std::move(state)){
    SetTooltipText(name);auto& ax=GetViewAccessibility();ax.SetName(name);ax.SetRole(ax::mojom::Role::kButton);ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);SetFocusBehavior(views::PlatformStyle::kDefaultFocusBehavior);if(native_focus_ring)views::FocusRing::Install(this);
  }
  void OnTooltipTextChanged(const std::u16string& previous) override{views::View::OnTooltipTextChanged(previous);auto& ax=GetViewAccessibility();if((ax.GetCachedName().empty()||ax.GetCachedName()==previous)&&!GetTooltipText().empty())ax.SetName(GetTooltipText());}
  uint64_t LogicalNodeId() const{return state_->action_node_id();}
  uint32_t ModelKind() const{return state_->node_model_kind();}
  uint64_t LogicalParentId() const{return state_->node_parent_id();}
  bool BelongsTo(const SidebarNodeGraphState& scope) const{return scope.graph->owns_input(*state_);}
  void BindNodeInput(const SidebarNodeBinding& binding){
    state_=AcquireSidebarInput(binding);state_->action_focus(HasFocus());SyncEnabled();
    auto& ax=GetViewAccessibility();if(state_->action_snapshot().enabled)ax.SetDefaultActionVerb(ActionVerb());else ax.RemoveDefaultActionVerb();
    SchedulePaint();
  }
  sidebar::ActionSnapshot Snapshot() const{return state_->action_snapshot();}
  void SetActivationOnPress(bool value){if(state_->action_on_press(value))SchedulePaint();}
  void SetDefault(bool value){if(!state_->action_default(value))return;GetViewAccessibility().SetIsDefault(value);const ui::Accelerator enter(ui::VKEY_RETURN,ui::EF_NONE);if(value)AddAccelerator(enter);else RemoveAccelerator(enter);}
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override{return sidebar::key_policy(event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0,true)!=0;}
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override{SyncEnabled();const auto before=Snapshot().visual;const bool pending=Pending();return Apply(state_->action_accelerator(accelerator.key_code()==ui::VKEY_RETURN?2:0,accelerator.modifiers()!=ui::EF_NONE,SceneIsVisible(this)),accelerator.ToKeyEvent(),before,pending);}
  void SetColor(SkColor color){if(color_==color)return;color_=color;SchedulePaint();}
  void SetCallback(views::Button::PressedCallback callback){Cancel();command_callback_.Reset();callback_=std::move(callback);}
  void SetFormCommand(sidebar::FormCommand command,const std::string& key,base::RepeatingCallback<void(views::View*,const sidebar::FormCommandBinding&)> callback){CHECK(state_->node_form_command_init(command,key));Cancel();callback_=views::Button::PressedCallback();command_callback_=std::move(callback);PreferredSizeChanged();SchedulePaint();}
  void SetAppearancePreset(bool emoji,size_t index,base::RepeatingCallback<void(views::View*,const sidebar::FormCommandBinding&)> callback){CHECK(state_->bind_appearance_preset(emoji,index));Cancel();callback_=views::Button::PressedCallback();command_callback_=std::move(callback);}
  void SetPreferenceCategory(const sidebar::PreferencesForm& form,size_t category,base::RepeatingCallback<void(views::View*,const sidebar::FormCommandBinding&)> callback){CHECK(form.bind_category(*state_,category));Cancel();callback_=views::Button::PressedCallback();command_callback_=std::move(callback);PreferredSizeChanged();SchedulePaint();}
  rust::Box<sidebar::FormCommandTicket> DeferFormCommand() const{return state_->defer_form_command();}
  void CancelInput(){Cancel();}
  bool Pending() const{return state_->action_pending();}
  // The Rust graph may already hold `on` (no delta here), so the dot re-measures
  // whenever it differs from the state its preferred size was taken in.
  void SetOn(bool on){const bool changed=state_->action_on(on);if(glyph_==ControlGlyph::kDot&&measured_on_!=on){measured_on_=on;PreferredSizeChanged();}if(changed)SchedulePaint();}
  // The active workspace slides out of the rail as a pill with its name.
  void SetPillName(std::u16string name){if(pill_name_==name)return;pill_name_=std::move(name);PreferredSizeChanged();SchedulePaint();}
  const std::u16string& pill_name() const{return pill_name_;}
  void SetIcon(const gfx::VectorIcon* icon){if(icon_==icon)return;icon_=icon;SchedulePaint();}
  void SetLabel(std::u16string label){if(label_==label)return;label_=std::move(label);PreferredSizeChanged();SchedulePaint();}
  void SyncViewportVisibility(){if(!Interactive())Cancel();}
  bool HandleAccessibleAction(const ui::AXActionData& action) override{if(action.action==ax::mojom::Action::kDoDefault&&SceneIsVisible(this)&&GetEnabledInViewsSubtree())ScrollViewToVisible();return views::View::HandleAccessibleAction(action);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{const auto r=sidebar::action_preferred_size(glyph_==ControlGlyph::kDot,!label_.empty());
    if(glyph_==ControlGlyph::kDot&&!pill_name_.empty()&&state_->action_snapshot().on)return gfx::Size(WorkspacePillWidth(pill_name_,!label_.empty()),r.height);
    return gfx::Size(r.width,r.height);}
  sidebar::Item SceneItem(gfx::RectF rect,gfx::RectF clip){SyncEnabled();const auto s=state_->action_snapshot();auto item=GlyphSceneData(this,glyph_,icon_,s.on,label_,color_,s.enabled&&(s.visual==sidebar::RowVisual::Hovered||s.visual==sidebar::RowVisual::Pressed),s.focused,rect,clip);
    if(glyph_==ControlGlyph::kDot&&s.on){const auto p=Palette(this);item.title=base::UTF16ToUTF8(pill_name_);item.fill=p.bg_raised;item.edge=p.line;}return item;}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);if(changed_)changed_.Run();}
  bool OnMousePressed(const ui::MouseEvent& event) override{return Pointer(sidebar::PointerPhase::Press,event);}
  bool OnMouseDragged(const ui::MouseEvent& event) override{return Pointer(sidebar::PointerPhase::Drag,event);}
  void OnMouseReleased(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Release,event);}
  void OnMouseEntered(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Enter,event);}
  void OnMouseExited(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Exit,event);}
  void OnMouseMoved(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Move,event);}
  bool OnKeyPressed(const ui::KeyEvent& event) override{return Key(true,event);}
  bool OnKeyReleased(const ui::KeyEvent& event) override{return Key(false,event);}
  void OnGestureEvent(ui::GestureEvent* event) override{SyncEnabled();const uint32_t kind=event->type()==ui::EventType::kGestureTapDown?0:event->type()==ui::EventType::kGestureTap?1:event->type()==ui::EventType::kGestureTapCancel||event->type()==ui::EventType::kGestureEnd?2:99;const auto before=state_->action_snapshot().visual;const bool pending=state_->action_pending();const auto r=state_->action_gesture(kind,Interactive()&&(kind==2||(HitTestPoint(event->location())&&GetVisibleBounds().Contains(event->location()))));if(r.handled)event->SetHandled();Apply(r,*event,before,pending);}
  void OnFocus() override{views::View::OnFocus();state_->action_focus(true);SchedulePaint();}
  void OnBlur() override{state_->action_focus(false);views::View::OnBlur();SchedulePaint();}
  void OnMouseCaptureLost() override{Cancel();}
  void EnabledChanged(){SyncEnabled();SchedulePaint();}
  void VisibilityChanged(views::View* starting,bool visible) override{views::View::VisibilityChanged(starting,visible);if(!SceneIsVisible(this))Cancel();}
  void AddedToWidget() override{views::View::AddedToWidget();window_.Observe(GetWidget());}
  void RemovedFromWidget() override{window_.Observe(nullptr);Cancel();views::View::RemovedFromWidget();}
  void PaintChildren(const views::PaintInfo& info) override{if(!visible_||!visible_.Run())views::View::PaintChildren(info);}
  void OnPaint(gfx::Canvas* canvas) override{if(visible_&&visible_.Run())return;SyncEnabled();const auto v=state_->action_snapshot();const auto paint=v.visual==sidebar::RowVisual::Disabled?views::Button::STATE_DISABLED:v.visual==sidebar::RowVisual::Pressed?views::Button::STATE_PRESSED:v.visual==sidebar::RowVisual::Hovered?views::Button::STATE_HOVERED:views::Button::STATE_NORMAL;
    // Workspace dots paint the Rust recipe (own accent, active pill); vector icons stay native.
    if(glyph_==ControlGlyph::kDot){const auto bounds=gfx::RectF(GetLocalBounds());PaintSceneRecipe(canvas,SceneItem(bounds,bounds),Font(3),true);return;}
    PaintGlyphControl(this,glyph_,icon_,color_,v.on,label_,paint,canvas);}
 protected:
  sidebar::ActionControl& NodeControl(){return *state_;}
  const sidebar::ActionControl& NodeControl() const{return *state_;}
  virtual ax::mojom::DefaultActionVerb ActionVerb() const{return ax::mojom::DefaultActionVerb::kPress;}
  virtual void ActivateAction(const ui::Event& event){
    const auto epoch=state_->action_admission_epoch();
    const auto id=GetProperty(views::kElementIdentifierKey);
    if(id){
      const auto node=state_->action_node_id();views::ViewTracker alive(this);
      views::ElementTrackerViews::GetInstance()->NotifyViewActivated(id,this);
      if(!alive.view())return;
      // An observer may retain an adapter after removing it, or retarget it.
      // Object lifetime alone does not keep the graph-authorized action valid.
      if(node&&(state_->action_node_id()!=node||!state_->action_snapshot().enabled))return;
    }
    // Observers can hide/detach an ancestor, disable then reenable, or refresh
    // a form context without deleting the adapter or changing its logical ID.
    if(!state_->action_admits(epoch)||!Interactive()||!GetWidget()||!GetEnabledInViewsSubtree())return;
    if(command_callback_){auto command=state_->node_form_command();if(command.command!=sidebar::FormCommand::None)command_callback_.Run(this,command);return;}
    if(callback_)callback_.Run(event);
  }
 private:
  void SyncEnabled(){
    const bool enabled=GetEnabledInViewsSubtree();state_->action_enabled(enabled);
    auto& ax=GetViewAccessibility();const auto verb=enabled?ActionVerb():ax::mojom::DefaultActionVerb::kNone;
    // Rust input and native AX projections can be updated independently. A
    // matching input state alone does not prove the AX verb is synchronized.
    if(ax.GetDefaultActionVerb()==verb)return;
    if(enabled)ax.SetDefaultActionVerb(verb);else ax.RemoveDefaultActionVerb();
  }
  bool Interactive() const{return SceneIsVisible(this)&&!GetVisibleBounds().IsEmpty();}
  void Cancel(){if(state_->action_cancel())SchedulePaint();}
  void WindowChanged(){Cancel();state_->action_focus(HasFocus());SchedulePaint();}
  bool Pointer(sidebar::PointerPhase phase,const ui::MouseEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;const bool pending=state_->action_pending();return Apply(state_->action_pointer(phase,event.x(),event.y(),phase!=sidebar::PointerPhase::Exit&&HitTestPoint(event.location())&&GetVisibleBounds().Contains(event.location()),event.IsLeftMouseButton(),Interactive()),event,before,pending);}
  bool Key(bool pressed,const ui::KeyEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;const bool pending=state_->action_pending();const uint32_t key=event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0;return Apply(state_->action_key(pressed,key,true,Interactive()),event,before,pending);}
  bool Apply(const sidebar::RowInputResult& result,const ui::Event& event,sidebar::RowVisual before,bool pending){const bool handled=result.handled;if(result.visual!=before)SchedulePaint();else if(pending!=state_->action_pending()&&changed_)changed_.Run();if(result.action==sidebar::RowAction::Activate){ActivateAction(event);return handled;}return handled;}
  SkColor color_=kSubtle;std::u16string label_;std::u16string pill_name_;bool measured_on_=false;base::RepeatingCallback<void(views::View*,const sidebar::FormCommandBinding&)> command_callback_;views::Button::PressedCallback callback_;ControlGlyph glyph_;raw_ptr<const gfx::VectorIcon> icon_;base::RepeatingCallback<bool()> visible_;base::RepeatingClosure changed_;rust::Box<sidebar::ActionControl> state_;SceneWindowObserver window_{base::BindRepeating(&RustActionView::WindowChanged,base::Unretained(this))};base::CallbackListSubscription enabled_changed_{AddEnabledInViewsSubtreeChangedCallback(base::BindRepeating(&RustActionView::EnabledChanged,base::Unretained(this)))};
};
BEGIN_METADATA(RustActionView)
END_METADATA
std::unique_ptr<views::View> CreateSceneAction(views::Button::PressedCallback callback,std::u16string name,ControlGlyph glyph,const gfx::VectorIcon* icon,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,bool native_focus_ring=true,SidebarNodeBinding binding={}){
  return std::make_unique<RustActionView>(std::move(callback),std::move(name),glyph,icon,std::move(visible),std::move(changed),native_focus_ring,AcquireSidebarInput(binding));
}
void SetActionColor(views::View* view,SkColor color){
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetColor(color);return;}
  static_cast<GlyphButton*>(view)->set_color(color);
}
bool IsGlyphAction(views::View* view){
  if(views::AsViewClass<RustActionView>(view))return true;
  return views::AsViewClass<GlyphButton>(view)!=nullptr;
}

void SetActionCallback(views::View* view,views::Button::PressedCallback callback){
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetCallback(std::move(callback));return;}
  static_cast<views::Button*>(view)->SetCallback(std::move(callback));
}
void CancelActionInput(views::View* view){
  if(auto* control=views::AsViewClass<RustActionView>(view))control->CancelInput();
}
void SetActionOn(views::View* view,bool on){
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetOn(on);return;}
  static_cast<GlyphButton*>(view)->set_on(on);
}
void SetActionLabel(views::View* view,std::u16string label){
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetLabel(std::move(label));return;}
  static_cast<GlyphButton*>(view)->set_label(std::move(label));
}
sidebar::Item ActionSceneItem(views::View* view,gfx::RectF rect,gfx::RectF clip){if(auto* control=views::AsViewClass<RustActionView>(view))return control->SceneItem(rect,clip);return GlyphSceneItem(static_cast<GlyphButton*>(view),rect,clip);}

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(SceneParentNodes,kSceneParentNodes)
// Marks the indent holder of a pinned row (as opposed to a nested folder group).
DEFINE_UI_CLASS_PROPERTY_KEY(bool,kPinRowHolder,false)
template<class T,class... Args> std::unique_ptr<T> MakeParentSceneControl(views::View* parent,Args&&... args){
  auto* scope=parent->GetProperty(kSceneParentNodes);
  if(!scope)scope=parent->SetProperty(kSceneParentNodes,std::make_unique<SceneParentNodes>(parent));
  return scope->Make<T>(std::forward<Args>(args)...);
}

// Rust lays out preset controls and the native editor separately. The canvas
// covers only preset bounds; fallback consumes the same Rust scene recipe.
class AppearanceRow final : public views::View {
  METADATA_HEADER(AppearanceRow,views::View)
 public:
  void SyncWindowVisibility(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  explicit AppearanceRow(bool emoji):emoji_(emoji),count_(sidebar::appearance_presets(emoji).size()+(emoji?1:0)) {}

  views::View* AddPicker(views::Button::PressedCallback callback,std::u16string label,GlyphButton::Glyph glyph) {
    return AddChildView(nodes_.Make<RustActionView>(std::move(callback),std::move(label),glyph,nullptr,
        base::BindRepeating([]{return true;}),base::BindRepeating(&AppearanceRow::Schedule,weak_factory_.GetWeakPtr()),false));
  }
  void SyncViewport(){if(children().size()!=Count()+2)return;for(size_t i=0;i<Count();++i)if(auto* control=views::AsViewClass<RustActionView>(children()[i].get()))control->SyncViewportVisibility();}
  void CancelInput(){if(children().size()!=Count()+2)return;for(size_t i=0;i<Count();++i)children()[i]->OnMouseCaptureLost();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  gfx::Rect scene_bounds() const{return metal_visible()?frame_:gfx::Rect();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {const auto g=Geometry(available.width().is_bounded()?available.width().value():-1,-1);return gfx::Size(g.width,g.height);}
  void Layout(PassKey) override {const auto g=Geometry(width(),height());if(g.children.size()!=children().size())return;for(size_t i=0;i<g.children.size();++i){const auto& r=g.children[i];children()[i]->SetBounds(r.x,r.y,r.width,r.height);}Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override {views::View::OnBoundsChanged(previous);Schedule();}
  void AddedToWidget() override {views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override {window_visibility_.Observe(nullptr);surface_.reset();frame_={};views::View::RemovedFromWidget();}
  void OnThemeChanged() override {views::View::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void VisibilityChanged(views::View* changed,bool visible) override {views::View::VisibilityChanged(changed,visible);if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();frame_={};}Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(Controls())))PaintSceneRecipe(canvas,item,Font(3));Schedule();}
 private:
  SceneGroupNodes nodes_{this};
  size_t Count() const{return count_;}
  sidebar::ColumnLayout Geometry(int width,int height) const {if(!emoji_)return sidebar::appearance_pill_layout(width,height,Count());const auto field=children().size()==Count()+2?children().back()->GetPreferredSize():gfx::Size();return sidebar::appearance_row_layout(width,height,field.width(),field.height(),emoji_);}
  gfx::Rect Controls() const {return children().size()==Count()+2?GetMirroredRect(gfx::Rect(0,0,children()[Count()-1]->bounds().right(),height())):gfx::Rect();}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {
    std::vector<sidebar::Item> items;if(children().size()!=Count()+2)return items;
    for(size_t i=0;i<Count();++i){auto* button=children()[i].get();auto bounds=gfx::RectF(button->GetMirroredBounds());bounds.Offset(dx,dy);auto child_clip=bounds;child_clip.Intersect(clip);items.push_back(ActionSceneItem(button,bounds,child_clip));}return items;
  }
  void Schedule(){SyncViewport();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&AppearanceRow::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();auto local=GetVisibleBounds();local.Intersect(Controls());
    if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();frame_={};if(was)SchedulePaint();return;}
    if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;
    frame_=GetBoundsInScreen();frame_.Offset(local.x(),local.y());frame_.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame_.size())));
    surface_->Update(GetWidget()->GetNativeWindow(),frame_,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();
  }
  bool emoji_;size_t count_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;gfx::Rect frame_;SceneWindowObserver window_visibility_{base::BindRepeating(&AppearanceRow::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<AppearanceRow> weak_factory_{this};
};
BEGIN_METADATA(AppearanceRow)
END_METADATA
// The workspace color wheel (design.rs wheel): the hue ring or the saturation x
// tone field. Paints from the scene, picks any color and hands the stored color
// to `pick`; arrows step 5 degrees of hue or 5% of a level.
class ColorWheelControl final : public views::View {
  METADATA_HEADER(ColorWheelControl,views::View)
 public:
  ColorWheelControl(bool ring,base::RepeatingCallback<SkColor()> current,base::RepeatingCallback<std::u16string()> emoji,base::RepeatingCallback<void(SkColor)> pick)
      :ring_(ring),current_(std::move(current)),emoji_(std::move(emoji)),pick_(std::move(pick)){
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(ring?T(u"Barwa przestrzeni"):T(u"Nasycenie i jasność przestrzeni"));
    GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
    Refresh();
  }
  void Refresh(){GetViewAccessibility().SetValue(base::UTF8ToUTF16(std::string(sidebar::color_wheel_description(current_.Run(),ring_))));SchedulePaint();}
  bool ring() const{return ring_;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return ring_?gfx::Size(184,184):gfx::Size(180,180);}
  void OnPaint(gfx::Canvas* canvas) override{
    const auto palette=Palette(this);sidebar::Item item{};item.kind=ring_?28:29;item.width=width();item.height=height();item.clip={0,0,float(width()),float(height())};
    item.badge=current_.Run();item.tile=IsDark(this);item.fill=palette.bg_raised;item.ink=palette.line_strong;item.edge=palette.line;item.focus=palette.focus;item.focused=HasFocus();
    if(ring_){item.title=base::UTF16ToUTF8(emoji_.Run());item.font_size=34;}
    PaintSceneRecipe(canvas,item,Font(3),true);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override{if(!PickAt(event.location()))return false;RequestFocus();return true;}
  bool OnMouseDragged(const ui::MouseEvent& event) override{PickAt(event.location());return true;}
  bool OnKeyPressed(const ui::KeyEvent& event) override{
    const bool horizontal=event.key_code()==ui::VKEY_LEFT||event.key_code()==ui::VKEY_RIGHT;
    const int delta=event.key_code()==ui::VKEY_RIGHT||event.key_code()==ui::VKEY_UP?1:event.key_code()==ui::VKEY_LEFT||event.key_code()==ui::VKEY_DOWN?-1:0;
    if(!delta)return false;Pick(sidebar::color_wheel_step(current_.Run(),ring_?0:horizontal?1:2,delta));return true;
  }
  bool HandleAccessibleAction(const ui::AXActionData& action) override{
    if(action.action!=ax::mojom::Action::kIncrement&&action.action!=ax::mojom::Action::kDecrement)return views::View::HandleAccessibleAction(action);
    Pick(sidebar::color_wheel_step(current_.Run(),ring_?0:1,action.action==ax::mojom::Action::kIncrement?1:-1));return true;
  }
  void OnFocus() override{views::View::OnFocus();SchedulePaint();}
  void OnBlur() override{views::View::OnBlur();SchedulePaint();}
 private:
  // A ring press off the band (the centre disc) picks nothing.
  bool PickAt(const gfx::Point& p){
    const SkColor target=ring_?sidebar::color_ring_target(current_.Run(),p.x(),p.y(),std::min(width(),height()))
                              :sidebar::color_pad_target(current_.Run(),p.x(),p.y(),width(),height());
    if(!target)return false;Pick(target);return true;
  }
  void Pick(SkColor color){if(!sidebar::design_same_workspace_color(color,current_.Run()))pick_.Run(color);}
  bool ring_;base::RepeatingCallback<SkColor()> current_;base::RepeatingCallback<std::u16string()> emoji_;base::RepeatingCallback<void(SkColor)> pick_;
};
BEGIN_METADATA(ColorWheelControl)
END_METADATA

views::View* AppearancePicker(views::View* parent,views::Button::PressedCallback callback,std::u16string label,GlyphButton::Glyph glyph) {
  if(auto* row=views::AsViewClass<AppearanceRow>(parent))return row->AddPicker(std::move(callback),std::move(label),glyph);
  return parent->AddChildView(std::make_unique<GlyphButton>(std::move(callback),std::move(label),glyph));
}

class ScenePreferenceButton final : public RustActionView {
  METADATA_HEADER(ScenePreferenceButton,RustActionView)
 public:
  ScenePreferenceButton(views::Button::PressedCallback callback,std::u16string text,base::RepeatingClosure painted,rust::Box<sidebar::ActionControl> input)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false,std::move(input)),text_(std::move(text)){}
  void SetSelected(bool selected){SetOn(selected);}
  bool selected() const{return Snapshot().on;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return NodeControl().node_form_view().dismiss?gfx::Size(32,32):gfx::Size(200,40);}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const{
    const auto state=Snapshot();const auto presentation=NodeControl().node_form_view();sidebar::Item item{};item.kind=presentation.dismiss?23:22;item.glyph=presentation.index;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.title=base::UTF16ToUTF8(text_);item.active=state.on;item.focused=state.focused;item.tile=base::i18n::IsRTL();item.font_size=Font(0).GetFontSize();item.ink=Tone(this,state.enabled?kInk:kDisabled);item.fill=state.on?Tone(this,kCard):state.enabled&&(state.visual==sidebar::RowVisual::Hovered||state.visual==sidebar::RowVisual::Pressed)?HoverWash(this):SK_ColorTRANSPARENT;item.detail_ink=SkColorSetA(Tone(this,kInk),0x0f);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;
  }
 private:
  std::u16string text_;
};
BEGIN_METADATA(ScenePreferenceButton)
END_METADATA
class ScenePreferenceControls final : public views::View {
  METADATA_HEADER(ScenePreferenceControls,views::View)
 public:
  explicit ScenePreferenceControls(bool dismiss):nodes_(this,!dismiss,dismiss?0:6){}
  views::View* AddControl(views::Button::PressedCallback callback,std::u16string title){return AddChildView(nodes_.Make<ScenePreferenceButton>(std::move(callback),std::move(title),base::BindRepeating(&ScenePreferenceControls::Schedule,weak_factory_.GetWeakPtr())));}
  size_t frame_count() const{return surface_?surface_->frame_count():0;}
  void SyncInput(){for(const auto& child:children())static_cast<ScenePreferenceButton*>(child.get())->SyncViewportVisibility();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  void CancelInput(){for(const auto& child:children())child->OnMouseCaptureLost();}
  void SyncWindowVisibility(){if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{if(GetVisibleBounds().IsEmpty())CancelInput();Schedule();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{auto g=Geometry(nodes_.Style().vertical&&available.width().is_bounded()?available.width().value():-1,-1);return gfx::Size(g.width,g.height);}
  void Layout(PassKey) override{auto g=Geometry(width(),height());for(size_t i=0;i<children().size();++i){auto r=g.children[i];children()[i]->SetBounds(r.x,r.y,r.width,r.height);}Schedule();}
  void AddedToWidget() override{views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Schedule();}
  void OnThemeChanged() override{views::View::OnThemeChanged();SchedulePaint();Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Schedule();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);SyncWindowVisibility();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,Font(0,item.active?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL));Schedule();}
 private:
  SceneGroupNodes nodes_;
  sidebar::ColumnLayout Geometry(int width,int height) const{std::vector<sidebar::ColumnChild> measured;for(const auto& child:children()){auto size=child->GetPreferredSize();measured.push_back({size.width(),size.height(),0,0,0,0});}const auto style=nodes_.Style();return sidebar::action_group_layout(width,height,style.vertical,style.spacing,rust::Slice<const sidebar::ColumnChild>(measured.data(),measured.size()));}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const{std::vector<sidebar::Item> items;for(const auto& child:children()){auto bounds=gfx::RectF(child->GetMirroredBounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);items.push_back(static_cast<ScenePreferenceButton*>(child.get())->Scene(bounds,local));}return items;}
  void Schedule(){SyncInput();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&ScenePreferenceControls::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,nodes_.Style().vertical?kFrameBase:kCard));if(was!=metal_visible())SchedulePaint();}
  bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&ScenePreferenceControls::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<ScenePreferenceControls> weak_factory_{this};
};
BEGIN_METADATA(ScenePreferenceControls)
END_METADATA

class SceneTextButton final : public RustActionView {
  METADATA_HEADER(SceneTextButton,RustActionView)
 public:
  SceneTextButton(views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style,base::RepeatingClosure painted,rust::Box<sidebar::ActionControl> input)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false,std::move(input)),text_(std::move(text)),style_(style) {}
  void UseNativeMinimum(){native_minimum_=true;}
  // A destructive action: its text in the palette's danger color.
  void SetDanger(){kind_=4;SchedulePaint();}
  // A destructive confirmation in a dialog: the danger pill with a 1.5 px edge.
  void SetDangerStrong(){kind_=5;SchedulePaint();}
  void SetQuiet(){kind_=3;PreferredSizeChanged();SchedulePaint();}
  // A quiet action that reads as the toast's one action: `accent-ink` text.
  void SetAccentInk(){accent_ink_=true;SchedulePaint();}
  void SetPrimary(){kind_=0;PreferredSizeChanged();SchedulePaint();}
  // Creating something new (a space, a profile): the one lime `zing` button.
  void SetZing(bool zing){if(zing)kind_=1;else kind_.reset();PreferredSizeChanged();SchedulePaint();}
  gfx::Size GetMinimumSize() const override{if(!native_minimum_)return RustActionView::GetMinimumSize();const auto font=TextFont();return gfx::Size(views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_DIALOG_BUTTON_MINIMUM_WIDTH),std::max(0,views::LayoutProvider::GetControlHeightForFont(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY,font)-std::max(font.GetHeight(),views::TypographyProvider::Get().GetLineHeight(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY))));}
  void SetText(std::u16string text){if(text==text_)return;OnMouseCaptureLost();text_=std::move(text);GetViewAccessibility().SetName(text_);SetTooltipText(text_);PreferredSizeChanged();SchedulePaint();}
  // Buttons are set in `body-strong`.
  static gfx::FontList TextFont(){return DesignFont(2,13,600,false);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {const auto g=Metrics(-1,-1);return gfx::Size(g.width,g.height);}
  // recipe::button: kind from the platform style (prominent = primary, default =
  // outline, text = quiet) unless set; fill, ink, edge and radius per state.
  sidebar::DesignButton Style() const {
    const auto state=Snapshot();const auto visual=state.visual;
    const uint32_t interaction=!state.enabled?3:visual==sidebar::RowVisual::Pressed?2:visual==sidebar::RowVisual::Hovered?1:0;
    return sidebar::design_button(IsDark(this),WindowWorkspaceColor(this),Kind(),interaction);
  }
  uint32_t Kind() const {return kind_?*kind_:style_==ui::ButtonStyle::kProminent?0:style_==ui::ButtonStyle::kText?3:2;}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const {
    const auto state=Snapshot();const auto b=Style();
    sidebar::Item item{};item.kind=19;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.title=base::UTF16ToUTF8(text_);item.font_size=13;item.active=true;item.text_style=4;
    item.title_width=b.padding_x;item.focused=state.focused;item.focus=Palette(this).focus;item.hovered=false;
    item.ink=accent_ink_?Palette(this).accent_ink:b.ink;item.fill=b.fill;item.badge=b.edge;item.font_height=b.radius;item.detail_font_size=b.edge_width;
    item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;
  }
 private:
  sidebar::ActionButtonLayout Metrics(int width,int height) const {const auto b=Style();return sidebar::action_button_layout(width,height,gfx::GetStringWidth(text_,TextFont())+2,static_cast<int>(b.height),static_cast<int>(b.padding_x),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_DIALOG_BUTTON_MINIMUM_WIDTH));}
  std::u16string text_;ui::ButtonStyle style_;bool native_minimum_=false;bool accent_ink_=false;std::optional<uint32_t> kind_;
};
BEGIN_METADATA(SceneTextButton)
END_METADATA
class SceneActionGroup final : public views::View {
  METADATA_HEADER(SceneActionGroup,views::View)
 public:
  void SyncWindowVisibility(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{for(const auto& child:children())if(!child->GetVisible()||child->GetVisibleBounds().IsEmpty())child->OnMouseCaptureLost();Schedule();}
  SceneActionGroup(bool vertical,int spacing,bool intrinsic=false,bool dialog=false):nodes_(this,vertical,spacing,intrinsic,dialog) {}
  void Share(base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed){shared_visible_=std::move(visible);shared_changed_=std::move(changed);nodes_.Share(bool(shared_visible_));SetSidebarPanelLayout(this,false,gfx::Insets(),nodes_.Style().spacing);}
  bool shared() const{return bool(shared_visible_);}
  void Append(std::vector<sidebar::Item>& items,gfx::Point origin,gfx::Rect clip) const{auto next=Items(origin.x(),origin.y(),gfx::RectF(clip));items.insert(items.end(),std::make_move_iterator(next.begin()),std::make_move_iterator(next.end()));}
  views::View* AddAction(views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style){auto* button=AddChildView(nodes_.Make<SceneTextButton>(std::move(callback),std::move(text),style,base::BindRepeating(&SceneActionGroup::Schedule,weak_factory_.GetWeakPtr())));if(shared())button->UseNativeMinimum();return button;}
  bool metal_visible() const{return shared()?shared_visible_.Run():surface_&&surface_->visible();}
  gfx::Rect metal_bounds_for_testing() const{return surface_?surface_->screen_bounds_for_testing():gfx::Rect();}
  bool contains_text(std::string_view text) const{return surface_&&surface_->visible()&&surface_->contains_text_for_testing(text);}
  void CancelInput(){for(const auto& child:children())child->OnMouseCaptureLost();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {if(shared())return views::View::CalculatePreferredSize(available);const auto g=Geometry(available.width().is_bounded()?available.width().value():-1,-1,true);return gfx::Size(g.width,g.height);}
  void Layout(PassKey) override {if(shared()){LayoutSuperclass<views::View>(this);Schedule();return;}const auto g=Geometry(width(),height());size_t i=0;for(const auto& child:children())if(child->GetVisible()){const auto& cell=g.children[i++];CHECK_EQ(nodes_.Id(child.get()),cell.id);const auto& r=cell.rect;child->SetBounds(r.x,r.y,r.width,r.height);}Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override {views::View::OnBoundsChanged(previous);Schedule();}
  void AddedToWidget() override {views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override {window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void OnThemeChanged() override {views::View::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void VisibilityChanged(views::View* view,bool visible) override {views::View::VisibilityChanged(view,visible);if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,SceneTextButton::TextFont());Schedule();}
 private:
  SceneGroupNodes nodes_{this};
  sidebar::FormLayout Geometry(int width,int height,bool preferred=false) const {std::vector<sidebar::FormMeasure> measured;for(const auto& child:children())if(child->GetVisible()){const auto size=child->GetPreferredSize();measured.push_back({nodes_.Id(child.get()),size.width(),size.height()});}auto* provider=views::LayoutProvider::Get();const auto insets=provider->GetInsetsMetric(views::INSETS_DIALOG_BUTTON_ROW);auto geometry=nodes_.Geometry(width,height,preferred,provider->GetDistanceMetric(views::DISTANCE_BUTTON_MAX_LINKABLE_WIDTH),insets.top(),insets.left(),insets.bottom(),insets.right(),measured);CHECK(geometry.admitted);return geometry;}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {std::vector<sidebar::Item> out;for(const auto& child:children())if(child->GetVisible()){auto bounds=gfx::RectF(child->GetMirroredBounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);out.push_back(static_cast<SceneTextButton*>(child.get())->Scene(bounds,local));}return out;}
  void Schedule(){for(const auto& child:children())static_cast<SceneTextButton*>(child.get())->SyncViewportVisibility();if(shared()){shared_changed_.Run();return;}if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneActionGroup::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,nodes_.Style().dialog?GetColorProvider()->GetColor(ui::kColorDialogBackground):Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  base::RepeatingCallback<bool()> shared_visible_;base::RepeatingClosure shared_changed_;
  bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneActionGroup::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneActionGroup> weak_factory_{this};
};
BEGIN_METADATA(SceneActionGroup)
END_METADATA

// Toast (design: Toast): one undoable event at the bottom of the window. The
// noun is bold, „Cofnij” is a quiet button, and hovering holds the timer.
class ToastView final : public views::View {
  METADATA_HEADER(ToastView,views::View)
 public:
  ToastView(base::RepeatingClosure undo,base::RepeatingClosure close,base::RepeatingCallback<void(bool)> hovered):hovered_(std::move(hovered)){
    message_=AddChildView(std::make_unique<views::View>());message_->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal));
    lead_=message_->AddChildView(std::make_unique<views::Label>());noun_=message_->AddChildView(std::make_unique<views::Label>());
    lead_->SetFontList(gfx::FontList(DesignFont(2,12,500,false)));noun_->SetFontList(gfx::FontList(DesignFont(2,12,600,false)));
    auto* actions=AddChildView(std::make_unique<SceneActionGroup>(false,4));actions_=actions;
    undo_=actions->AddAction(base::BindRepeating(undo),T(u"Cofnij"),ui::ButtonStyle::kText);views::AsViewClass<SceneTextButton>(undo_)->SetQuiet();views::AsViewClass<SceneTextButton>(undo_)->SetAccentInk();
    close_=AddChildView(CreateSceneAction(base::BindRepeating(close),T(u"Zamknij komunikat"),ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,{},base::DoNothing()));
    SetNotifyEnterExitOnChild(true);GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }
  void SetMessage(std::u16string lead,std::u16string noun){lead_->SetText(lead);noun_->SetText(noun);GetViewAccessibility().SetName(lead+noun);PreferredSizeChanged();}
  void SetAction(std::u16string label){views::AsViewClass<SceneTextButton>(undo_)->SetText(std::move(label));actions_->InvalidateLayout();PreferredSizeChanged();}
  std::u16string text() const{return std::u16string(lead_->GetText())+std::u16string(noun_->GetText());}
  views::View* undo() const{return undo_;}
  // `space-4` in, the message, „Cofnij” and close at their natural widths, `space-2` out.
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{
    const int width=16+message_->GetPreferredSize().width()+12+actions_->GetPreferredSize().width()+4+close_->GetPreferredSize().width()+8;
    return gfx::Size(std::min(420,width),44);}
  void Layout(PassKey) override{
    const auto close=close_->GetPreferredSize(),actions=actions_->GetPreferredSize(),message=message_->GetPreferredSize();
    auto centred=[&](views::View* view,int x,gfx::Size size){view->SetBounds(x,(height()-size.height())/2,size.width(),size.height());};
    const int close_x=width()-8-close.width(),actions_x=close_x-4-actions.width();
    centred(close_,close_x,close);centred(actions_,actions_x,actions);
    centred(message_,16,gfx::Size(std::max(0,actions_x-12-16),message.height()));}
  void OnThemeChanged() override{views::View::OnThemeChanged();const auto p=Palette(this);for(auto* label:{lead_.get(),noun_.get()}){label->SetEnabledColor(p.ink);label->SetBackgroundColor(p.bg_raised);}SchedulePaint();}
  void OnPaint(gfx::Canvas* canvas) override{canvas->DrawColor(Palette(this).bg_raised);}
  void OnMouseEntered(const ui::MouseEvent&) override{hovered_.Run(true);}
  void OnMouseExited(const ui::MouseEvent&) override{hovered_.Run(false);}
 private:
  raw_ptr<views::View> message_=nullptr,actions_=nullptr,close_=nullptr,undo_=nullptr;raw_ptr<views::Label> lead_=nullptr,noun_=nullptr;base::RepeatingCallback<void(bool)> hovered_;
};
BEGIN_METADATA(ToastView)
END_METADATA

// The dimmed window under the Command Center (`scrim`); a click on it closes
// the palette.
class ScrimView final : public views::View {
  METADATA_HEADER(ScrimView,views::View)
 public:
  explicit ScrimView(base::RepeatingClosure pressed):pressed_(std::move(pressed)){}
  void OnPaint(gfx::Canvas* canvas) override{canvas->DrawColor(Palette(this).scrim);}
  bool OnMousePressed(const ui::MouseEvent&) override{pressed_.Run();return true;}
 private:
  base::RepeatingClosure pressed_;
};
BEGIN_METADATA(ScrimView)
END_METADATA
void CancelSceneActions(views::View* root){if(!root)return;if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->CancelInput();return;}if(auto* controls=views::AsViewClass<ScenePreferenceControls>(root)){controls->CancelInput();return;}for(const auto& child:root->children())CancelSceneActions(child.get());}
// Font shaping stays a platform service; no Label is constructed for the scene.
class ScenePeekTitle final : public views::View {
  METADATA_HEADER(ScenePeekTitle,views::View)
 public:
  ScenePeekTitle(std::u16string text,base::RepeatingClosure changed):text_(std::move(text)),changed_(std::move(changed)){GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);SetFocusBehavior(FocusBehavior::NEVER);}
  void SetText(std::u16string text){if(text_==text)return;text_=std::move(text);natural_.reset();GetViewAccessibility().SetName(text_);UpdateTooltip();PreferredSizeChanged();SchedulePaint();}
  static gfx::FontList TextFont(){return Font(-1,gfx::Font::Weight::SEMIBOLD);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return NaturalSize();}
  gfx::Size GetMinimumSize() const override{return gfx::Size(std::min(NaturalSize().width(),gfx::GetStringWidth(gfx::kEllipsisUTF16,TextFont())),LineHeight());}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const{sidebar::Item i{};i.kind=27;i.x=bounds.x();i.y=bounds.y();i.width=bounds.width();i.height=bounds.height();i.title=base::UTF16ToUTF8(text_);i.font_size=TextFont().GetFontSize();i.font_weight=static_cast<int>(gfx::Font::Weight::SEMIBOLD);i.active=true;i.tile=base::i18n::IsRTL();i.ink=Tone(this,GetEnabledInViewsSubtree()?kInk:kDisabled);i.clip={clip.x(),clip.y(),clip.width(),clip.height()};return i;}
  void OnBoundsChanged(const gfx::Rect& old) override{views::View::OnBoundsChanged(old);UpdateTooltip();}
  // Like views::Label, only an elided title handles tooltips.
  views::View* GetTooltipHandlerForPoint(const gfx::Point& point) override{return GetTooltipText().empty()?nullptr:views::View::GetTooltipHandlerForPoint(point);}
  void OnThemeChanged() override{views::View::OnThemeChanged();natural_.reset();PreferredSizeChanged();UpdateTooltip();changed_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);changed_.Run();}
 private:
  static int LineHeight(){return std::max(TextFont().GetHeight(),views::TypographyProvider::Get().GetLineHeight(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY));}
  gfx::Size NaturalSize() const{if(!natural_){auto shaped=gfx::RenderText::CreateRenderText();shaped->SetFontList(TextFont());shaped->SetCursorEnabled(false);shaped->SetWordWrapBehavior(gfx::TRUNCATE_LONG_WORDS);shaped->SetMinLineHeight(LineHeight());shaped->SetDirectionalityMode(gfx::DIRECTIONALITY_FROM_TEXT);shaped->SetText(text_);natural_=shaped->GetStringSize();natural_->set_height(std::max(natural_->height(),LineHeight()));}return *natural_;}
  void UpdateTooltip(){const auto size=NaturalSize();SetTooltipText(size.width()>width()||size.height()>height()?text_:std::u16string());}
  std::u16string text_;base::RepeatingClosure changed_;mutable std::optional<gfx::Size> natural_;
};
BEGIN_METADATA(ScenePeekTitle)
END_METADATA
class ScenePeekToolbar final : public views::View {
  METADATA_HEADER(ScenePeekToolbar,views::View)
 public:
  explicit ScenePeekToolbar(std::array<views::Button::PressedCallback,5> callbacks){
    const auto c=sidebar::peek_copy();const auto changed=base::BindRepeating(&ScenePeekToolbar::Schedule,weak_factory_.GetWeakPtr());
    auto glyph=[&](size_t index,const rust::String& name,const gfx::VectorIcon* icon){AddChildView(nodes_.Make<RustActionView>(std::move(callbacks[index]),base::UTF8ToUTF16(std::string(name)),ControlGlyph::kVector,icon,base::BindRepeating([]{return true;}),changed,false));};
    glyph(0,c.back,&vector_icons::kArrowBackIcon);glyph(1,c.forward,&vector_icons::kArrowForwardIcon);
    title_=AddChildView(std::make_unique<ScenePeekTitle>(base::UTF8ToUTF16(std::string(c.initial)),changed));
    for(size_t index:{2u,3u}){auto* action=AddChildView(nodes_.Make<SceneTextButton>(std::move(callbacks[index]),base::UTF8ToUTF16(std::string(index==2?c.promote:c.split)),ui::ButtonStyle::kDefault,changed));action->UseNativeMinimum();}
    glyph(4,c.close,&vector_icons::kCloseIcon);SetSidebarPanelLayout(this,false,gfx::Insets(),6,title_,true);
  }
  views::View* title() const{return title_;}
  void SetTitle(std::u16string title){const auto units=sidebar::peek_title(rust::Slice<const uint16_t>(reinterpret_cast<const uint16_t*>(title.data()),title.size()));title_->SetText(std::u16string(units.begin(),units.end()));}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  gfx::Rect scene_bounds() const{return metal_visible()?surface_->screen_bounds_for_testing():gfx::Rect();}
  void CancelInput(){for(const auto& child:children())if(auto* action=views::AsViewClass<RustActionView>(child.get()))action->CancelInput();}
  void Layout(PassKey) override{LayoutSuperclass<views::View>(this);Schedule();}
  void AddedToWidget() override{views::View::AddedToWidget();window_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_.Observe(nullptr);CancelInput();surface_.reset();views::View::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& old) override{views::View::OnBoundsChanged(old);Schedule();}
  void OnThemeChanged() override{views::View::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,item.kind==27?ScenePeekTitle::TextFont():SceneTextButton::TextFont(),true);}
 private:
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const{std::vector<sidebar::Item> items;for(const auto& child:children())if(child->GetVisible()){auto box=gfx::RectF(child->GetMirroredBounds());box.Offset(dx,dy);auto visible=box;visible.Intersect(clip);if(visible.IsEmpty())continue;if(auto* title=views::AsViewClass<ScenePeekTitle>(child.get()))items.push_back(title->Scene(box,visible));else if(auto* text=views::AsViewClass<SceneTextButton>(child.get()))items.push_back(text->Scene(box,visible));else items.push_back(static_cast<RustActionView*>(child.get())->SceneItem(box,visible));}return items;}
  void WindowChanged(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}
  void Schedule(){for(const auto& child:children())if(auto* action=views::AsViewClass<RustActionView>(child.get()))action->SyncViewportVisibility();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&ScenePeekToolbar::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(surface_){auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(local.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));}if(was!=metal_visible())SchedulePaint();}
  SceneGroupNodes nodes_{this};raw_ptr<ScenePeekTitle> title_=nullptr;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_{base::BindRepeating(&ScenePeekToolbar::WindowChanged,base::Unretained(this))};base::WeakPtrFactory<ScenePeekToolbar> weak_factory_{this};
};
BEGIN_METADATA(ScenePeekToolbar)
END_METADATA
class SceneToggle final : public RustActionView {
  METADATA_HEADER(SceneToggle,RustActionView)
 public:
  SceneToggle(std::u16string name,rust::Box<sidebar::ActionControl> input):RustActionView({},std::move(name),ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),{},false,std::move(input)){
    CHECK(NodeControl().node_toggle_init());
    GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);RefreshAX();
  }
  bool On() const{return NodeControl().node_toggle_checked();}
  void SetOn(bool value){NodeControl().node_toggle_set(value);RefreshAX();Schedule();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool timer_running_for_testing() const{return timer_.IsRunning();}
  void CancelInteraction(){OnMouseCaptureLost();NodeControl().node_toggle_finish();timer_.Stop();Schedule();}
  void SyncWindowVisibility(){if(!SceneIsVisible(this)){CancelInteraction();if(surface_)surface_->Hide();}Schedule();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(38,28);}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override{RustActionView::OnBoundsChanged(previous);Schedule();}
  void OnThemeChanged() override{RustActionView::OnThemeChanged();Schedule();}
  void AddedToWidget() override{RustActionView::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);timer_.Stop();surface_.reset();RustActionView::RemovedFromWidget();}
  void VisibilityChanged(views::View* changed,bool visible) override{RustActionView::VisibilityChanged(changed,visible);SyncWindowVisibility();}
  void OnFocus() override{RustActionView::OnFocus();Schedule();}
  void OnBlur() override{RustActionView::OnBlur();Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{RustActionView::OnDidSchedulePaint(rect);const auto input=Snapshot();NodeControl().node_toggle_hover(input.enabled&&(input.visual==sidebar::RowVisual::Hovered||input.visual==sidebar::RowVisual::Pressed),Now(),gfx::Animation::ShouldRenderRichAnimation());Schedule();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible()){auto scene=Scene();PaintEditorSceneFallback(canvas,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())));}Schedule();}
 protected:
  void ActivateAction(const ui::Event& event) override{if(!NodeControl().node_toggle_flip(Snapshot().enabled&&GetEnabledInViewsSubtree()&&SceneIsVisible(this),Now(),gfx::Animation::ShouldRenderRichAnimation()))return;RefreshAX();Schedule();RustActionView::ActivateAction(event);}
 private:
  static uint64_t Now(){return (base::TimeTicks::Now()-base::TimeTicks()).InMilliseconds();}
  void RefreshAX(){GetViewAccessibility().SetCheckedState(On()?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);}
  rust::Vec<sidebar::Primitive> Scene() const{
    const auto input=Snapshot();const auto model=NodeControl().node_toggle_view(Now());const bool enabled=input.enabled;sidebar::ToggleStyle s{};s.width=width();s.height=height();s.checked=model.checked;s.pressed=input.visual==sidebar::RowVisual::Pressed;s.focused=input.focused;s.rtl=base::i18n::IsRTL();s.progress=model.progress;s.hover=model.hover;
    // recipe::toggle; disabled drops to `ink-faint` on a `line` edge.
    const auto palette=Palette(this);const auto on=sidebar::design_toggle(IsDark(this),WindowWorkspaceColor(this),true);const auto off=sidebar::design_toggle(IsDark(this),WindowWorkspaceColor(this),false);
    s.track_on=enabled?on.track:palette.line;s.track_off=SkColorSetA(off.edge,0);s.thumb_on=enabled?on.thumb_fill:palette.ink_faint;s.thumb_off=enabled?off.thumb_fill:palette.ink_faint;s.thumb_hover=s.thumb_on;s.halo=0;s.focus=palette.focus;s.border=enabled?off.edge:palette.line;s.clip={0,0,float(width()),float(height())};return sidebar::toggle_scene(s);
  }
  void Schedule(){SyncViewportVisibility();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneToggle::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void Tick(){if(!metal_visible())SchedulePaint();Schedule();}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){timer_.Stop();if(surface_)surface_->Hide();return;}auto scene=Scene();for(auto& op:scene){op.x-=local.x();op.y-=local.y();op.clip={0,0,float(local.width()),float(local.height())};}if(!surface_)surface_=CreateSidebarMetal();const bool was=metal_visible();if(surface_){auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());surface_->UpdateScene(GetWidget()->GetNativeWindow(),frame,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())),Tone(this,kCard));}if(was!=metal_visible())SchedulePaint();if(NodeControl().node_toggle_view(Now()).animating){if(!timer_.IsRunning())timer_.Start(FROM_HERE,base::Milliseconds(16),this,&SceneToggle::Tick);}else timer_.Stop();}
  base::RepeatingTimer timer_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneToggle::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneToggle> weak_factory_{this};
};
BEGIN_METADATA(SceneToggle)
END_METADATA
bool ToggleOn(views::View* control){
  auto* scene=views::AsViewClass<SceneToggle>(control);return scene->On();
}
void SetToggleOn(views::View* control,bool value){
  auto* scene=views::AsViewClass<SceneToggle>(control);scene->SetOn(value);
}
views::View* PreferenceToggle(views::View* parent,std::u16string name){
  return parent->AddChildView(MakeParentSceneControl<SceneToggle>(parent,std::move(name)));
}

class SceneCheckbox final : public RustActionView {
  METADATA_HEADER(SceneCheckbox,RustActionView)
 public:
  SceneCheckbox(views::Button::PressedCallback callback,std::u16string text,base::RepeatingClosure painted,rust::Box<sidebar::ActionControl> input)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false,std::move(input)),text_(std::move(text)) {
    CHECK(NodeControl().node_checkbox_init(false));GetViewAccessibility().SetRole(ax::mojom::Role::kCheckBox);RefreshAX();
  }
  bool Checked() const{return NodeControl().node_checked();}
  void SetChecked(bool checked){if(!NodeControl().node_set_checked(checked))return;RefreshAX();SchedulePaint();}
  void SetText(std::u16string text){if(text==text_)return;OnMouseCaptureLost();text_=std::move(text);measured_width_=-1;GetViewAccessibility().SetName(text_);SetTooltipText(text_);PreferredSizeChanged();SchedulePaint();}
  static gfx::FontList TextFont(){return views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {const auto g=Geometry(available.width().is_bounded()?available.width().value():-1);return gfx::Size(g.width,g.height);}
  void OnThemeChanged() override {RustActionView::OnThemeChanged();measured_width_=-1;PreferredSizeChanged();SchedulePaint();}
  std::vector<sidebar::Item> Scene(gfx::RectF bounds,gfx::RectF clip) const {
    const auto g=Geometry(bounds.width());const auto* provider=GetColorProvider();const auto input=Snapshot();const bool enabled=input.enabled;const auto font=TextFont();std::vector<sidebar::Item> out;
    sidebar::Item mark{};mark.kind=20;mark.x=bounds.x()+g.mark.x;mark.y=bounds.y()+g.mark.y;mark.width=g.mark.width;mark.height=g.mark.height;mark.active=Checked();mark.focused=input.focused;mark.hovered=enabled&&(input.visual==sidebar::RowVisual::Hovered||input.visual==sidebar::RowVisual::Pressed);mark.font_size=font.GetFontSize();
    mark.fill=provider->GetColor(enabled?ui::kColorCheckboxContainer:ui::kColorCheckboxContainerDisabled);mark.ink=provider->GetColor(enabled?ui::kColorCheckboxCheck:ui::kColorCheckboxCheckDisabled);mark.badge=provider->GetColor(enabled?ui::kColorCheckboxOutline:ui::kColorCheckboxOutlineDisabled);mark.detail_ink=provider->GetColor(ui::kColorFocusableBorderFocused);mark.clip={clip.x(),clip.y(),clip.width(),clip.height()};out.push_back(std::move(mark));
    for(size_t i=0;i<g.lines.size();++i){const auto& r=g.lines[i];sidebar::Item item{};item.kind=17;item.x=bounds.x()+r.x;item.y=bounds.y()+r.y;item.width=r.width;item.height=r.height;item.title=lines_[i];item.font_size=font.GetFontSize();item.active=font.GetFontWeight()>=gfx::Font::Weight::MEDIUM;item.ink=provider->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_LABEL,enabled?views::style::STYLE_PRIMARY:views::style::STYLE_DISABLED));item.clip={clip.x(),clip.y(),clip.width(),clip.height()};out.push_back(std::move(item));}return out;
  }
 protected:
  ax::mojom::DefaultActionVerb ActionVerb() const override{return Checked()?ax::mojom::DefaultActionVerb::kUncheck:ax::mojom::DefaultActionVerb::kCheck;}
  void ActivateAction(const ui::Event& event) override {if(!NodeControl().node_flip_checked(Snapshot().enabled&&GetEnabledInViewsSubtree()&&SceneIsVisible(this)))return;RefreshAX();SchedulePaint();RustActionView::ActivateAction(event);}
 private:
  void RefreshAX(){if(GetEnabledInViewsSubtree())GetViewAccessibility().SetDefaultActionVerb(ActionVerb());else GetViewAccessibility().RemoveDefaultActionVerb();GetViewAccessibility().SetCheckedState(Checked()?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);}
  sidebar::CheckboxLayout Geometry(int requested) const {
    const auto font=TextFont();const auto padding=views::LayoutProvider::Get()->GetInsetsMetric(views::INSETS_CHECKBOX_RADIO_BUTTON);const int width=requested<0?gfx::GetStringWidth(text_,font)+padding.width()+24:requested;const int label_width=std::max(0,width-padding.width()-24);
    if(measured_width_!=label_width||measured_font_!=font.GetFontSize()){lines_=ShapeSidebarParagraph(base::UTF16ToUTF8(text_),font.GetFontSize(),label_width,font.GetFontWeight()>=gfx::Font::Weight::MEDIUM);measured_width_=label_width;measured_font_=font.GetFontSize();}
    return sidebar::checkbox_layout(width,font.GetHeight(),lines_.size(),padding.left(),padding.top(),padding.right(),padding.bottom(),base::i18n::IsRTL());
  }
  std::u16string text_;mutable std::vector<std::string> lines_;mutable int measured_width_=-1,measured_font_=0;
};
BEGIN_METADATA(SceneCheckbox)
END_METADATA
class SceneCheckboxGroup final : public views::View {
  METADATA_HEADER(SceneCheckboxGroup,views::View)
 public:
  void SyncWindowVisibility(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Schedule();}
  explicit SceneCheckboxGroup(int spacing){SetLayoutManager(CreateSidebarColumnLayout(spacing));}
  views::View* AddChoice(views::Button::PressedCallback callback,std::u16string text){return AddChildView(nodes_.Make<SceneCheckbox>(std::move(callback),std::move(text),base::BindRepeating(&SceneCheckboxGroup::Schedule,weak_factory_.GetWeakPtr())));}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  void CancelInput(){for(const auto& child:children())child->OnMouseCaptureLost();}
  void Layout(PassKey) override {LayoutSuperclass<views::View>(this);Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override {views::View::OnBoundsChanged(previous);Schedule();}
  void AddedToWidget() override {views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override {window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void OnThemeChanged() override {views::View::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void VisibilityChanged(views::View* changed,bool visible) override {views::View::VisibilityChanged(changed,visible);if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,SceneCheckbox::TextFont());Schedule();}
 private:
  SceneGroupNodes nodes_{this};
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {std::vector<sidebar::Item> out;for(const auto& child:children())if(child->GetVisible()){auto bounds=gfx::RectF(child->GetMirroredBounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);auto items=static_cast<SceneCheckbox*>(child.get())->Scene(bounds,local);for(auto& item:items)out.push_back(std::move(item));}return out;}
  void Schedule(){for(const auto& child:children())static_cast<SceneCheckbox*>(child.get())->SyncViewportVisibility();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneCheckboxGroup::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneCheckboxGroup::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneCheckboxGroup> weak_factory_{this};
};
BEGIN_METADATA(SceneCheckboxGroup)
END_METADATA
void CancelSceneCheckboxes(views::View* root){if(!root)return;if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->CancelInput();return;}for(const auto& child:root->children())CancelSceneCheckboxes(child.get());}
// Native IME protocol only; prefix matching and selected state live in Rust.
class SceneSelectInput final : public views::PrefixSelector {
 public:
  SceneSelectInput(views::View* host,base::RepeatingCallback<void(std::u16string)> input,base::RepeatingClosure reset):views::PrefixSelector(nullptr,host),input_(std::move(input)),reset_(std::move(reset)){}
  void InsertText(const std::u16string& text,InsertTextCursorBehavior) override {input_.Run(text);}
  void InsertChar(const ui::KeyEvent& event) override {input_.Run(std::u16string(1,event.GetCharacter()));}
  void OnInputMethodChanged() override {reset_.Run();}
 private:
  base::RepeatingCallback<void(std::u16string)> input_;base::RepeatingClosure reset_;
};
// Each native menu owns its opening's key snapshot and generation. A callback
// from a replaced model must never borrow a newer opening's token or indices.
class SceneSelectMenu final : public ui::SimpleMenuModel,public ui::SimpleMenuModel::Delegate {
 public:
  SceneSelectMenu(std::vector<std::pair<std::string,std::u16string>> items,std::string selected,uint64_t token,base::RepeatingCallback<bool(uint64_t,const std::string&)> enabled,base::RepeatingCallback<void(uint64_t,const std::string&)> activate)
      :ui::SimpleMenuModel(this),selected_(std::move(selected)),token_(token),enabled_(std::move(enabled)),activate_(std::move(activate)) {for(auto& item:items){AddRadioItem(keys_.size(),item.second,1);keys_.push_back(std::move(item.first));}}
  bool IsCommandIdChecked(int command) const override {return Valid(command)&&keys_[command]==selected_;}
  bool IsCommandIdEnabled(int command) const override {return Valid(command)&&enabled_.Run(token_,keys_[command]);}
  void ExecuteCommand(int command,int) override {if(Valid(command))activate_.Run(token_,keys_[command]);}
 private:
  bool Valid(int command) const {return command>=0&&static_cast<size_t>(command)<keys_.size();}
  std::vector<std::string> keys_;std::string selected_;uint64_t token_;base::RepeatingCallback<bool(uint64_t,const std::string&)> enabled_;base::RepeatingCallback<void(uint64_t,const std::string&)> activate_;
};
class SceneSelect final : public RustActionView {
  METADATA_HEADER(SceneSelect,RustActionView)
 public:
  void SyncWindowVisibility(){OnMouseCaptureLost();if(!SceneIsVisible(this)){CancelMenu();if(surface_)surface_->Hide();}Schedule();}

  SceneSelect(std::vector<std::u16string> labels,std::u16string name,base::RepeatingClosure changed,rust::Box<sidebar::ActionControl> input)
      :RustActionView({},std::move(name),ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),{},false,InitInput(labels,std::move(input))),labels_(ReadLabels(NodeControl())),changed_(std::move(changed)),input_(this,base::BindRepeating(&SceneSelect::Input,base::Unretained(this)),base::BindRepeating(&SceneSelect::ResetPrefix,base::Unretained(this))) {
    SetActivationOnPress(true);GetViewAccessibility().SetRole(ax::mojom::Role::kComboBoxSelect);GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);GetViewAccessibility().SetDefaultActionVerb(ActionVerb());RefreshAX();
  }
  ~SceneSelect() override {if(GetInputMethod())GetInputMethod()->DetachTextInputClient(&input_);}
  std::optional<size_t> Selected() const {return NodeControl().node_select_has_selection()?std::optional<size_t>(NodeControl().node_select_selected()):std::nullopt;}
  std::string SelectedKey() const{return std::string(NodeControl().node_select_selected_key());}
  size_t Count() const {return labels_.size();}
  std::u16string LabelAt(size_t index) const{return index<labels_.size()?labels_[index]:std::u16string();}
  void SetSelectedIndex(std::optional<size_t> index){if(index&&*index>=Count())return;OnMouseCaptureLost();CancelMenu();if(index)NodeControl().node_select_select_index(*index);else NodeControl().node_select_clear_selection();RefreshAX();Schedule();}
  void SetChangedCallback(base::RepeatingClosure changed){changed_=std::move(changed);}
  void CancelInteraction(){OnMouseCaptureLost();CancelMenu();Schedule();}
  void SetOptions(const std::vector<std::pair<std::string,std::u16string>>& choices){
    bool same=choices.size()==labels_.size();
    for(size_t i=0;same&&i<choices.size();++i)same=choices[i].first==std::string(NodeControl().node_select_option_key(i))&&choices[i].second==labels_[i];
    if(same)return;
    OnMouseCaptureLost();CancelMenu();rust::Vec<sidebar::SelectOption> options;std::vector<std::u16string> labels;
    for(const auto& [key,label]:choices){options.push_back({key,base::UTF16ToUTF8(label),base::UTF16ToUTF8(base::i18n::ToLower(label))});labels.push_back(label);}
    if(!NodeControl().node_select_replace_options(std::move(options),false,false))return;
    labels_=std::move(labels);RefreshAX();PreferredSizeChanged();Schedule();
  }
  void SetProfiles(const std::vector<ProfileInfo>& profiles,bool fallback_last,bool select_last){
    OnMouseCaptureLost();CancelMenu();rust::Vec<sidebar::SelectOption> options;std::vector<std::u16string> labels;
    for(const auto& profile:profiles){auto label=base::UTF8ToUTF16(profile.name);options.push_back({profile.id,profile.name,base::UTF16ToUTF8(base::i18n::ToLower(label))});labels.push_back(std::move(label));}
    if(!NodeControl().node_select_replace_options(std::move(options),fallback_last,select_last))return;
    labels_=std::move(labels);RefreshAX();PreferredSizeChanged();Schedule();
  }
  void SelectForTesting(size_t index){if(index>=Count()||!Snapshot().enabled||!GetEnabledInViewsSubtree()||!SceneIsVisible(this))return;CancelMenu();const auto token=NodeControl().node_select_open(true);if(!NodeControl().node_select_choose(token,index,true))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {int widest=0;for(const auto& text:labels_)widest=std::max(widest,gfx::GetStringWidth(text,TextFont()));const auto g=sidebar::select_layout(-1,-1,widest,views::LayoutProvider::GetControlHeightForFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY,TextFont()),false);return gfx::Size(g.width,g.height);}
  bool OnKeyPressed(const ui::KeyEvent& event) override {if(!Snapshot().enabled||!GetEnabledInViewsSubtree())return false;if(!sidebar::select_menu_key(event.key_code()))return false;ShowMenu(ui::mojom::MenuSourceType::kKeyboard);return true;}
  bool OnKeyReleased(const ui::KeyEvent&) override {return false;}
  void OnFocus() override {RustActionView::OnFocus();if(GetInputMethod())GetInputMethod()->SetFocusedTextInputClient(&input_);}
  void OnBlur() override {if(GetInputMethod())GetInputMethod()->DetachTextInputClient(&input_);ResetPrefix();RustActionView::OnBlur();}
  bool HandleAccessibleAction(const ui::AXActionData& action) override {if(action.action==ax::mojom::Action::kDoDefault&&Snapshot().enabled&&GetEnabledInViewsSubtree()){ShowMenu(ui::mojom::MenuSourceType::kKeyboard);return true;}return RustActionView::HandleAccessibleAction(action);}
  void VisibilityChanged(views::View* changed,bool visible) override {RustActionView::VisibilityChanged(changed,visible);if(!SceneIsVisible(this)){OnMouseCaptureLost();CancelMenu();if(surface_)surface_->Hide();}Schedule();}
  void AddedToWidget() override {RustActionView::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override {window_visibility_.Observe(nullptr);if(GetInputMethod())GetInputMethod()->DetachTextInputClient(&input_);CancelMenu();surface_.reset();RustActionView::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& previous) override {RustActionView::OnBoundsChanged(previous);Schedule();}
  void OnThemeChanged() override {RustActionView::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {RustActionView::OnDidSchedulePaint(rect);if(!GetEnabledInViewsSubtree())CancelMenu();Schedule();}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Schedule();}
  gfx::Rect metal_bounds_for_testing() const{return surface_?surface_->screen_bounds_for_testing():gfx::Rect();}
  void OnPaint(gfx::Canvas* canvas) override {if(!metal_visible())PaintSceneRecipe(canvas,Scene(0,0,gfx::RectF(GetLocalBounds())),TextFont());Schedule();}
 protected:
  ax::mojom::DefaultActionVerb ActionVerb() const override{return ax::mojom::DefaultActionVerb::kOpen;}
  void ActivateAction(const ui::Event& event) override {if(Snapshot().enabled&&GetEnabledInViewsSubtree()&&SceneIsVisible(this))ShowMenu(ui::GetMenuSourceTypeForEvent(event));}
 private:
  static rust::Box<sidebar::ActionControl> InitInput(const std::vector<std::u16string>& labels,rust::Box<sidebar::ActionControl> input){rust::Vec<sidebar::SelectOption> options;for(size_t i=0;i<labels.size();++i)options.push_back({base::NumberToString(i),base::UTF16ToUTF8(labels[i]),base::UTF16ToUTF8(base::i18n::ToLower(labels[i]))});CHECK(input->node_select_init(std::move(options)));return input;}
  static std::vector<std::u16string> ReadLabels(const sidebar::ActionControl& input){std::vector<std::u16string> labels;for(const auto& option:input.node_select_options())labels.push_back(base::UTF8ToUTF16(std::string(option.label)));return labels;}
  static gfx::FontList TextFont(){return views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY);}
  void ResetPrefix(){NodeControl().node_select_reset_prefix();}
  void Input(std::u16string text){if(!Snapshot().enabled||!SceneIsVisible(this)||!HasFocus())return;const auto lower=base::UTF16ToUTF8(base::i18n::ToLower(text));if(!NodeControl().node_select_input(lower,(base::TimeTicks::Now()-base::TimeTicks()).InMilliseconds(),GetEnabledInViewsSubtree()))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  void RefreshAX(){const auto selected=Selected();GetViewAccessibility().SetValue(selected?labels_[*selected]:std::u16string());if(NodeControl().node_select_expanded())GetViewAccessibility().SetIsExpanded();else GetViewAccessibility().SetIsCollapsed();}
  static uint64_t Now(){return (base::TimeTicks::Now()-base::TimeTicks()).InMicroseconds();}
  void CancelMenu(){NodeControl().node_select_dismiss_popup(Now(),bool(runner_));ResetPrefix();RefreshAX();auto old=std::move(runner_);old.reset();}
  void Closed(uint64_t token){if(!NodeControl().node_select_close_menu_at(token,Now()))return;runner_.reset();RefreshAX();Schedule();}
  bool CanChoose(uint64_t token,const std::string& key) const {return NodeControl().node_select_can_choose(token,key,Snapshot().enabled&&GetEnabledInViewsSubtree()&&SceneIsVisible(this));}
  void Choose(uint64_t token,const std::string& key){if(!NodeControl().node_select_choose_key(token,key,Snapshot().enabled&&GetEnabledInViewsSubtree()&&SceneIsVisible(this)))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  void ShowMenu(ui::mojom::MenuSourceType source){
    if(!Snapshot().enabled||!GetWidget()||!SceneIsVisible(this)||!GetEnabledInViewsSubtree())return;if(GetVisibleBounds().IsEmpty())ScrollViewToVisible();if(GetVisibleBounds().IsEmpty())return;CancelMenu();const auto token=source==ui::mojom::MenuSourceType::kKeyboard?NodeControl().node_select_open(true):NodeControl().node_select_open_pointer(true,Now(),views::kMinimumTimeBetweenButtonClicks.InMicroseconds());if(!token)return;
    std::vector<std::pair<std::string,std::u16string>> items;for(size_t i=0;i<labels_.size();++i)items.emplace_back(std::string(NodeControl().node_select_option_key(i)),labels_[i]);
    const auto weak=weak_factory_.GetWeakPtr();menu_=std::make_unique<SceneSelectMenu>(std::move(items),std::string(NodeControl().node_select_selected_key()),token,base::BindRepeating([](base::WeakPtr<SceneSelect> owner,uint64_t epoch,const std::string& key){return owner&&owner->CanChoose(epoch,key);},weak),base::BindRepeating(&SceneSelect::Choose,weak));
    runner_=std::make_unique<views::MenuRunner>(menu_.get(),views::MenuRunner::COMBOBOX,base::BindRepeating(&SceneSelect::Closed,weak,token));RefreshAX();Schedule();if(runner_&&NodeControl().node_select_expanded())runner_->RunMenuAt(GetWidget(),nullptr,GetBoundsInScreen(),views::MenuAnchorPosition::kTopLeft,source);
  }
  sidebar::Item Scene(float dx,float dy,gfx::RectF clip) const {sidebar::Item item{};item.kind=21;item.x=dx;item.y=dy;item.width=width();item.height=height();if(const auto selected=Selected())item.title=base::UTF16ToUTF8(labels_[*selected]);const auto* colors=GetColorProvider();const auto input=Snapshot();const bool enabled=input.enabled;item.font_size=TextFont().GetFontSize();item.active=TextFont().GetFontWeight()>=gfx::Font::Weight::MEDIUM;item.tile=base::i18n::IsRTL();item.focused=input.focused;item.hovered=enabled&&(input.visual==sidebar::RowVisual::Hovered||NodeControl().node_select_expanded());item.ink=colors->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_LABEL,enabled?views::style::STYLE_PRIMARY:views::style::STYLE_DISABLED));item.fill=colors->GetColor(enabled?ui::kColorComboboxBackground:ui::kColorComboboxBackgroundDisabled);item.badge=colors->GetColor(input.focused?ui::kColorFocusableBorderFocused:enabled?ui::kColorButtonBorder:ui::kColorButtonBorderDisabled);item.detail_ink=colors->GetColor(ui::kColorSysStateHoverOnSubtle);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;}
  void Schedule(){SyncViewportVisibility();if((runner_||NodeControl().node_select_expanded())&&(!SceneIsVisible(this)||GetVisibleBounds().IsEmpty()))CancelMenu();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneSelect::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());std::vector<sidebar::Item> items;items.push_back(Scene(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size()))));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  std::vector<std::u16string> labels_;base::RepeatingClosure changed_;SceneSelectInput input_;std::unique_ptr<SceneSelectMenu> menu_;std::unique_ptr<views::MenuRunner> runner_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneSelect::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneSelect> weak_factory_{this};
};
BEGIN_METADATA(SceneSelect)
END_METADATA
size_t ImportSourceIndex(views::View* view){
  auto* scene=views::AsViewClass<SceneSelect>(view);return scene->Selected().value_or(0);
}
std::optional<size_t> PickerIndex(views::View* picker){
  auto* scene=views::AsViewClass<SceneSelect>(picker);return scene->Selected();
}
size_t PickerCount(views::View* picker){
  auto* scene=views::AsViewClass<SceneSelect>(picker);return scene->Count();
}
void SetPickerIndex(views::View* picker,std::optional<size_t> index){
  auto* scene=views::AsViewClass<SceneSelect>(picker);scene->SetSelectedIndex(index);
}
// Programmatic refresh does not steal an unchanged popup/IME interaction.
void SyncPickerIndex(views::View* picker,std::optional<size_t> index){if(PickerIndex(picker)!=index)SetPickerIndex(picker,index);}
void SetPickerCallback(views::View* picker,base::RepeatingClosure callback){
  auto* scene=views::AsViewClass<SceneSelect>(picker);scene->SetChangedCallback(std::move(callback));
}
views::View* ChoicePicker(views::View* parent,std::u16string name,std::vector<std::u16string> labels){
  return parent->AddChildView(MakeParentSceneControl<SceneSelect>(parent,std::move(labels),std::move(name),base::RepeatingClosure()));
}
void ReplacePickerOptions(views::View* picker,const std::vector<std::pair<std::string,std::u16string>>& choices){
  views::AsViewClass<SceneSelect>(picker)->SetOptions(choices);
}
void ReplacePickerLabels(views::View* picker,const std::vector<std::u16string>& labels){
  std::vector<std::pair<std::string,std::u16string>> choices;
  for(size_t i=0;i<labels.size();++i)choices.emplace_back(base::NumberToString(i),labels[i]);
  ReplacePickerOptions(picker,choices);
  SetPickerIndex(picker,0); // Native site-policy model replacement starts at Default.
}
void RefreshChoiceViewports(views::View* root){
  if(auto* group=views::AsViewClass<ScenePreferenceControls>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* scene=views::AsViewClass<SceneToggle>(root)){scene->OnVisibleBoundsChanged();return;}
  if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* scene=views::AsViewClass<SceneSelect>(root)){scene->OnVisibleBoundsChanged();return;}
  if(auto* action=views::AsViewClass<RustActionView>(root)){action->SyncViewportVisibility();return;}
  for(const auto& child:root->children())RefreshChoiceViewports(child.get());
}
void CancelChoiceInteractions(views::View* root){
  if(auto* group=views::AsViewClass<ScenePreferenceControls>(root)){group->CancelInput();return;}
  if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->CancelInput();return;}
  if(auto* scene=views::AsViewClass<SceneToggle>(root)){scene->CancelInteraction();return;}
  if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->CancelInput();return;}
  if(auto* scene=views::AsViewClass<SceneSelect>(root)){scene->CancelInteraction();return;}
  for(const auto& child:root->children())CancelChoiceInteractions(child.get());
}
views::View* ProfilePicker(views::View* parent,std::u16string name,base::RepeatingClosure changed){
  return parent->AddChildView(MakeParentSceneControl<SceneSelect>(parent,std::vector<std::u16string>(),std::move(name),std::move(changed)));
}
void ReplaceProfilePickerOptions(views::View* picker,const std::vector<ProfileInfo>& profiles,bool fallback_last,bool select_last){
  views::AsViewClass<SceneSelect>(picker)->SetProfiles(profiles,fallback_last,select_last);
}
views::View* AddImportCheck(views::View* parent,std::u16string text,views::Button::PressedCallback callback) {
  return views::AsViewClass<SceneCheckboxGroup>(parent)->AddChoice(std::move(callback),std::move(text));
}
bool CheckValue(views::View* button) {return views::AsViewClass<SceneCheckbox>(button)->Checked();}
void SetCheckValue(views::View* button,bool checked) {views::AsViewClass<SceneCheckbox>(button)->SetChecked(checked);}
void SetCheckText(views::View* button,std::u16string text) {views::AsViewClass<SceneCheckbox>(button)->SetText(std::move(text));}

struct ToolsClearPresentation {std::u16string label;bool enabled;};
ToolsClearPresentation ToolsClearControls(bool busy){
  const auto value=sidebar::tools_clear_controls(busy);return {base::UTF8ToUTF16(std::string(value.label)),value.enabled};
}

views::View* ActionGroup(views::View* parent,bool vertical,int spacing) {
  return parent->AddChildView(std::make_unique<SceneActionGroup>(vertical,spacing));
}
views::View* AddAction(views::View* parent,views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style=ui::ButtonStyle::kDefault) {
  return views::AsViewClass<SceneActionGroup>(parent)->AddAction(std::move(callback),std::move(text),style);
}
void SetActionText(views::View* button,std::u16string text) {
  views::AsViewClass<SceneTextButton>(button)->SetText(std::move(text));
}

// Scene buttons share the action-group recipe and retain one native
// event/focus/accessibility adapter.
views::View* PreferenceAction(views::View* parent,views::Button::PressedCallback callback,std::u16string text,bool stretch=false) {
  // A button in a horizontal preference row keeps its intrinsic width, even
  // when Views asks for its height at the full row width. Vertical list actions
  // retain the original stretch behavior.
  parent=parent->AddChildView(std::make_unique<SceneActionGroup>(stretch,0,!stretch));
  return AddAction(parent,std::move(callback),std::move(text));
}

// Editing/IME remains a platform service; result selection is scene state.
class PaletteView final : public views::View {
  METADATA_HEADER(PaletteView,views::View)
 public:
  explicit PaletteView(base::RepeatingClosure painted):painted_(std::move(painted)) {
    scope_=std::make_shared<SidebarNodeGraphState>(true);
  }
  const std::shared_ptr<SidebarNodeGraphState>& node_scope() const{return scope_;}
  void SetEditor(views::View* editor){editor_=editor;}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {
    views::View::OnDidSchedulePaint(rect);
    // Native caret/preedit repainting does not change a GPU scene. Text/data
    // changes schedule explicitly; retain other invalidations (e.g. scrollbar).
    if(editor_&&scene()&&views::View::ConvertRectToTarget(editor_,this,editor_->GetLocalBounds()).Contains(rect))return;
    painted_.Run();
  }
  bool scene() const {
    return bool(scope_);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if(scene()){sidebar::Item frame{};frame.kind=16;frame.width=width();frame.height=height();frame.fill=Tone(this,kCard);frame.badge=Hairline(this);frame.clip={0,0,float(width()),float(height())};PaintSceneRecipe(canvas,frame,Font(-2));return;}
    views::View::OnPaint(canvas);
  }
  void Reset() {
    if(scope_){scope_->graph->palette_reset();return;}
    index_=0;moved_=false;
  }
  size_t selected() const {
    if(scope_)return scope_->graph->palette_selected();
    return index_;
  }
  bool moved() const {
    if(scope_)return scope_->graph->palette_moved();
    return moved_;
  }
  void Step(size_t count,bool backwards) {
    if(scope_){static_cast<void>(scope_->graph->palette_step(count,backwards));return;}
    if(count){moved_=true;index_=(index_+(backwards?count-1:1))%count;}
  }
  void Reconcile(size_t count,size_t preserved,bool found) {
    if(scope_){scope_->graph->palette_reconcile_selection(count,preserved,found);return;}
    if(found)index_=preserved;index_=std::min(index_,count?count-1:0);
  }
 private:
  std::shared_ptr<SidebarNodeGraphState> scope_;
  raw_ptr<views::View> editor_=nullptr;
  base::RepeatingClosure painted_;
  size_t index_=0;bool moved_=false;
};
BEGIN_METADATA(PaletteView)
END_METADATA

class ChromeHeaderView final : public views::View {
  METADATA_HEADER(ChromeHeaderView,views::View)
 public:
  explicit ChromeHeaderView(base::RepeatingClosure painted) : painted_(std::move(painted)) {}
  void OnDidSchedulePaint(const gfx::Rect& rect) override { views::View::OnDidSchedulePaint(rect); painted_.Run(); }
  void OnPaint(gfx::Canvas* canvas) override { views::View::OnPaint(canvas); painted_.Run(); }
 private:
  base::RepeatingClosure painted_;
};
BEGIN_METADATA(ChromeHeaderView)
END_METADATA

// A sidebar tab, a "New tab" action or a palette suggestion.
// Row presentation shared by the Rust row adapter and native preview snapshots.
struct RowRecipe {
  struct Spec {
    std::u16string title;
    std::u16string detail;
    raw_ptr<const gfx::VectorIcon> icon = nullptr;  // Palette chip / action glyph.
    std::u16string monogram;                // Tab favicon stand-in.
    SkColor monogram_color = kMonogramColors[0];
    bool active = false;
    bool highlighted = false;
    bool chip = false;
    bool history = false;  // Shared-canvas, two-line history recipe.
    bool sidebar_row = false;  // Migrated control: text belongs to the scene.
    base::RepeatingCallback<bool()> scene_visible;  // Chrome control paint gate.
    bool address = false;
    bool secure = false;
    bool blank = false;
    bool tile = false;  // One half of a Split View pair: always a filled card.
    bool stacked = false;  // Two lines (title over detail) beside a large monogram.
    bool kbd = false;  // The detail is a shortcut, drawn as Kbd keys after the title.
    std::u16string header;  // A palette group's eyebrow, drawn above its first row.
    bool editing = false;   // AddressPill while its address is being edited (⌘L).
    bool audible = false;   // A tab playing sound: a speaker in `accent-ink`.
    int height = 34;
    views::Button::PressedCallback close;
    base::RepeatingCallback<void(gfx::Point)> drop;
    // Row, row-local grab point and screen pointer while a drop-enabled row is
    // dragged; the pointer is nullopt when the drag ends.
    base::RepeatingCallback<void(views::View*,gfx::Point,std::optional<gfx::Point>)> drag;
  };
  static void PaintPreview(gfx::Canvas* canvas, const gfx::Rect& rect, const Spec& spec, const views::View* theme) {
    gfx::ScopedCanvas scoped(canvas); canvas->Translate(rect.OffsetFromOrigin());
    const gfx::Rect bounds(rect.size());
    PaintDecorations(canvas, bounds, spec, theme, false);
    const int lead = spec.chip ? 52 : 36;
    const int trail = spec.close ? 32 : 10;
    canvas->DrawStringRectWithFlags(spec.title,
        Font(0, spec.active || spec.chip ? gfx::Font::Weight::MEDIUM : gfx::Font::Weight::NORMAL),
        Tone(theme, kInk), gfx::Rect(lead, 0, std::max(0, rect.width() - lead - trail), rect.height()), gfx::Canvas::TEXT_ALIGN_LEFT);
    if (spec.close && spec.active && !spec.tile)
      PaintIcon(canvas, vector_icons::kCloseSmallIcon, gfx::Rect(rect.width() - 30, 0, 24, rect.height()), 14, Tone(theme, kSubtle));
  }
  static void PaintDecorations(gfx::Canvas* canvas, const gfx::Rect& bounds, const Spec& spec,
                               const views::View* theme, bool hovered) {
    if (spec.address) {
      FillRound(canvas, bounds, 10, SkColorSetA(Tone(theme, kCard), hovered ? 0xD9 : 0x99));
      StrokeRound(canvas, bounds, 10, SkColorSetA(Tone(theme, kInk), 0x0D));
      PaintIcon(canvas, spec.secure ? vector_icons::kLockIcon : vector_icons::kSearchIcon,
          gfx::Rect(10, 0, 16, bounds.height()), 14, Tone(theme, kMuted));
      return;
    }
    if (spec.tile) {
      FillRound(canvas, bounds, 9, SkColorSetA(Tone(theme, kCard), spec.active ? 0xFF : hovered ? 0x8C : 0x59));
      if (spec.active) StrokeRound(canvas, bounds, 9, SkColorSetA(Tone(theme, kInk), 0x0F));
    } else if (spec.active) {
      FillRound(canvas, bounds, 8, Tone(theme, kCard));
      StrokeRound(canvas, bounds, 8, SkColorSetA(Tone(theme, kInk), 0x0F));
    } else if (spec.highlighted) {
      FillRound(canvas, bounds, 9, Tone(theme, kAccentWash));
    } else if (hovered) {
      FillRound(canvas, bounds, 8, HoverWash(theme));
    }
    if (spec.chip) {
      const gfx::Rect chip(12, (bounds.height() - 28) / 2, 28, 28);
      FillRound(canvas, chip, 7, Tone(theme, kChip));
      if (spec.icon) PaintIcon(canvas, *spec.icon, chip, 15, Tone(theme, kSubtle));
    } else if (spec.icon) {
      PaintIcon(canvas, *spec.icon, gfx::Rect(10, 0, 16, bounds.height()), 16, Tone(theme, kSubtle));
    } else {
      const int size = spec.stacked ? 30 : 16;
      const gfx::Rect mono(10, (bounds.height() - size) / 2, size, size);
      const auto tile = sidebar::design_palette(IsDark(theme), spec.monogram_color);
      FillRound(canvas, mono, spec.stacked ? 8 : 4, tile.accent);
      canvas->DrawStringRectWithFlags(spec.monogram, Font(spec.stacked ? 0 : -4, gfx::Font::Weight::SEMIBOLD),
                                      tile.on_accent, mono, gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }
};
// Input, enabled/focus/visual state and drag admission live in Rust. This node
// adapts native events, accessibility, capture, window visibility and font paint.
// The address pill: `bg-raised` pill with a `line` edge, host and path in the
// `url` style (host 600), and a secure page's lock in `ok`.
sidebar::Item AddressRowItem(views::View* row,const RowRecipe::Spec& spec,gfx::RectF rect,gfx::RectF clip,int,bool hovered,bool focused){
  sidebar::Item item{};const auto p=Palette(row);
  item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
  item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
  item.kind=4; item.title=base::UTF16ToUTF8(spec.title); item.detail=base::UTF16ToUTF8(spec.detail);
  item.title_width=gfx::GetStringWidth(spec.title,DesignFont(3,12,600,false));
  item.font_size=12; item.detail_font_size=12; item.detail_ink=p.ink_muted;
  item.ink=row->GetEnabledInViewsSubtree()&&!spec.blank?p.ink:p.ink_muted;
  item.badge=spec.secure?p.ok:p.ink_muted; item.active=spec.secure; item.hovered=hovered;
  item.fill=p.bg_raised; item.edge=p.line; item.focused=focused; item.focus=p.focus;
  if(spec.editing){item.glyph=1;item.edge=p.accent;}
  return item;
}
// A command row (icon, title) whose shortcut detail is a row of Kbd keys, one per
// symbol, `space-2` after the title.
std::vector<sidebar::Item> CommandRowItems(views::View* row,const RowRecipe::Spec& spec,gfx::RectF rect,gfx::RectF clip,bool hovered,bool focused){
  const auto p=Palette(row);std::vector<sidebar::Item> items;
  sidebar::Item item{};item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();
  item.clip={clip.x(),clip.y(),clip.width(),clip.height()};item.kind=3;item.title=base::UTF16ToUTF8(spec.title);
  item.font_size=13;item.text_style=5;item.ink=row->GetEnabledInViewsSubtree()&&!spec.blank?p.ink:p.ink_muted;item.badge=hovered?p.ink:p.ink_muted;
  item.hovered=hovered;item.fill=hovered?p.bg_hover:SK_ColorTRANSPARENT;item.focused=focused;item.focus=p.focus;
  items.push_back(item);
  float x=rect.x()+36+gfx::GetStringWidth(spec.title,DesignFont(2,13,400,false))+8;
  const std::u16string keys=spec.detail;
  for(size_t i=0;i<keys.size();){
    size_t next=i+(base::IsAsciiAlpha(keys[i])&&i+1<keys.size()&&base::IsAsciiAlpha(keys[i+1])?keys.size()-i:1);  // "tab", "esc": one key.
    sidebar::Item key{};key.kind=30;key.title=base::UTF16ToUTF8(keys.substr(i,next-i));
    const float w=std::max(18.f,gfx::GetStringWidth(keys.substr(i,next-i),DesignFont(3,11,500,false))+10.f);
    key.x=x;key.y=rect.y()+(rect.height()-18)/2;key.width=w;key.height=18;key.clip=item.clip;
    key.fill=p.bg_hover;key.edge=p.line;key.ink=p.ink_muted;
    if(x+w<=rect.right()-8)items.push_back(key);x+=w+2;i=next;
  }
  return items;
}
// A Command Center row: `accent-soft` with the name and icon in `accent-ink` when
// selected, `bg-hover` on hover, no tile under the icon.
// A palette group starts with its `eyebrow` (space-3 above, space-1 below) inside
// the first row, so rows stay one per result and the keyboard never lands on it.
// SidebarTab rows (and pinned rows, folders) sit `space-1` apart.
constexpr int kSidebarRowGap=4;
constexpr int kPaletteHeader=30;
gfx::RectF PaletteRowBody(const RowRecipe::Spec& spec,gfx::RectF rect){if(!spec.header.empty())rect.Inset(gfx::InsetsF::TLBR(kPaletteHeader,0,0,0));return rect;}
sidebar::Item PaletteHeaderItem(views::View* row,const RowRecipe::Spec& spec,gfx::RectF rect,gfx::RectF clip){
  sidebar::Item item{};item.kind=17;item.x=rect.x()+12;item.y=rect.y()+12;item.width=std::max(0.f,rect.width()-24);item.height=14;
  item.clip={clip.x(),clip.y(),clip.width(),clip.height()};item.title=base::UTF16ToUTF8(spec.header);item.ink=Palette(row).ink_muted;item.font_size=10.5f;item.text_style=7;return item;
}
sidebar::Item PaletteRowItem(views::View* row,const RowRecipe::Spec& spec,gfx::RectF rect,gfx::RectF clip,bool hovered,bool focused){
  rect=PaletteRowBody(spec,rect);
  const auto p=Palette(row);sidebar::Item item{};item.kind=12;
  item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
  item.title=base::UTF16ToUTF8(spec.title);item.detail=base::UTF16ToUTF8(spec.detail);item.title_width=gfx::GetStringWidth(spec.title,DesignFont(2,13,400,false))+2;
  item.ink=spec.highlighted?p.accent_ink:p.ink;item.detail_ink=p.ink_muted;item.badge=SK_ColorTRANSPARENT;item.font_size=13;item.detail_font_size=12;
  item.active=spec.highlighted;item.hovered=hovered;item.focused=focused;item.focus=p.focus;item.fill=spec.highlighted?p.accent_soft:hovered?p.bg_hover:SK_ColorTRANSPARENT;
  return item;
}
rust::Vec<uint16_t> SceneUtf16(std::u16string_view text){rust::Vec<uint16_t> units;units.reserve(text.size());for(auto unit:text)units.push_back(unit);return units;}
class RustRowView final : public views::View {
  METADATA_HEADER(RustRowView,views::View)
 public:
  using Spec=RowRecipe::Spec;
  using Watch=base::RepeatingCallback<void(views::View*,base::RepeatingClosure)>;
  RustRowView(views::Button::PressedCallback callback,Spec spec,base::RepeatingClosure changed,Watch watch,rust::Box<sidebar::ActionControl> state,SidebarNodeBinding close_binding)
      :spec_(std::move(spec)),callback_(std::move(callback)),changed_(std::move(changed)),watch_(std::move(watch)),state_(std::move(state)){
    SetNotifyEnterExitOnChild(true);SetTooltipText(spec_.title);auto& ax=GetViewAccessibility();ax.SetName(spec_.title);ax.SetRole(ax::mojom::Role::kButton);ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);SetFocusBehavior(views::PlatformStyle::kDefaultFocusBehavior);if(!spec_.history)views::FocusRing::Install(this);state_->action_on(spec_.active);if(spec_.chip)ax.SetDescription(spec_.detail);
    if(spec_.close){close_=AddChildView(CreateSceneAction(std::move(spec_.close),T(u"Zamknij kartę"),ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,{},base::BindRepeating(&RustRowView::ChildChanged,weak_factory_.GetWeakPtr()),true,std::move(close_binding)));UpdateClose();}
    if(state_->sidebar_visual().valid){SyncSidebarAX();ClearSidebarVisuals(spec_);}
  }
  ~RustRowView() override{if(watch_)watch_.Run(this,{});}
  uint64_t LogicalNodeId() const{return state_->action_node_id();}
  uint64_t LogicalParentId() const{return state_->node_parent_id();}
  bool BelongsTo(const SidebarNodeGraphState& scope) const{return scope.graph->owns_input(*state_);}
  uint32_t ModelKind() const{return state_->node_model_kind();}
  void ProjectHistory(const sidebar::HistorySceneChild& child){
    CHECK(spec_.history);auto text=[](const rust::Vec<uint16_t>& units){return std::u16string(units.begin(),units.end());};
    if(child.cancel_input)CancelInput();
    auto same=[](const std::u16string& value,const rust::Vec<uint16_t>& units){return value.size()==units.size()&&std::equal(value.begin(),value.end(),units.begin());};
    if(!same(spec_.title,child.title))SetTitle(text(child.title));
    if(!same(spec_.detail,child.detail))SetDetail(text(child.detail));
    if(spec_.height!=child.height){spec_.height=child.height;PreferredSizeChanged();}
    auto& ax=GetViewAccessibility();if(!same(ax.GetCachedName(),child.url)){auto url=text(child.url);SetTooltipText(url);ax.SetName(url);}ax.SetDescription(text(child.detail));
    // Rust can cancel input before the adapter sees a visual-state delta.
    SchedulePaint();
  }
  void ProjectPalette(const sidebar::PaletteSceneChild& child){
    CHECK(spec_.chip&&!spec_.history);auto text=[](const rust::Vec<uint16_t>& units){return std::u16string(units.begin(),units.end());};
    auto same=[](const std::u16string& value,const rust::Vec<uint16_t>& units){return value.size()==units.size()&&std::equal(value.begin(),value.end(),units.begin());};
    if(child.cancel_input)CancelInput();
    if(!same(spec_.title,child.title))SetTitle(text(child.title));if(!same(spec_.detail,child.detail))SetDetail(text(child.detail));
    spec_.highlighted=child.highlighted;spec_.icon=child.icon==2?&vector_icons::kSettingsIcon:child.icon==1?&vector_icons::kArrowForwardIcon:&vector_icons::kSearchIcon;
    if(spec_.height!=child.height){spec_.height=child.height;PreferredSizeChanged();}
    GetViewAccessibility().SetName(spec_.title);GetViewAccessibility().SetDescription(spec_.detail);SchedulePaint();
  }
  void SetPaletteHeader(std::u16string header,int row_height){if(spec_.header==header&&spec_.height==row_height+(header.empty()?0:kPaletteHeader))return;spec_.header=std::move(header);spec_.height=row_height+(spec_.header.empty()?0:kPaletteHeader);PreferredSizeChanged();SchedulePaint();}
  sidebar::SidebarTabBinding TabBinding() const{return state_->tab_binding();}
  sidebar::SidebarPinBinding PinBinding() const{return state_->pin_binding();}
  bool HasSidebarProperties() const{return state_->sidebar_visual().valid;}
  bool ProjectPinCollapsed(SidebarNodeGraphState& scope,bool collapsed){if(!scope.graph->set_pin_collapsed(*state_,collapsed))return false;SyncSidebarAX();SchedulePaint();return true;}
  base::WeakPtr<RustRowView> GetWeakPtr(){return weak_factory_.GetWeakPtr();}
  bool AttachedClose() const{return close_&&close_->parent()==this&&close_->GetVisible()&&close_->GetEnabledInViewsSubtree();}
  uint32_t RetainedFocusPart(const views::View* focused) const{return focused==this?1:close_&&focused==close_?2:0;}
  bool TabCloseNeedsCallback(const SidebarNodeGraphState& scope) const{if(!close_)return true;auto* control=views::AsViewClass<RustActionView>(close_.get());return !control||!control->BelongsTo(scope)||!control->LogicalNodeId();}
  void RestoreFocusPart(uint32_t part){if(part==2&&AttachedClose())close_->RequestFocus();else RequestFocus();}
  void ProjectTabSpec(SidebarNodeGraphState& scope,Spec spec){CHECK(!spec_.history&&!spec_.chip&&!spec_.address);CancelInput();const auto update=scope.graph->project_tab(*state_,SceneUtf16(spec.title),SceneUtf16(spec.monogram),spec.monogram_color,spec.tile);CHECK(update.admitted);ClearSidebarVisuals(spec);spec_=std::move(spec);measured_.reset();if(update.title_changed)SyncSidebarAX();if(update.geometry_changed)PreferredSizeChanged();UpdateClose();SchedulePaint();}
  void ProjectPinAdapter(Spec spec){CHECK(PinBinding().valid);CancelInput();ClearSidebarVisuals(spec);spec_=std::move(spec);SyncSidebarAX();PreferredSizeChanged();SchedulePaint();}
  void RefreshPinProperties(){CHECK(PinBinding().valid);CancelInput();SyncSidebarAX();SchedulePaint();}
  Spec SidebarSnapshot() const{Spec spec;spec.sidebar_row=true;const auto presentation=state_->sidebar_presentation();if(presentation.valid){spec.title=std::u16string(presentation.title.begin(),presentation.title.end());spec.monogram=std::u16string(presentation.monogram.begin(),presentation.monogram.end());spec.monogram_color=presentation.color;spec.icon=presentation.folder?&vector_icons::kFolderFilledIcon:nullptr;spec.tile=presentation.tile;spec.height=presentation.height;spec.active=state_->action_snapshot().on;}return spec;}
  sidebar::SidebarRowScene SidebarScene(gfx::RectF bounds,gfx::RectF clip) const{return state_->sidebar_scene({bounds.x(),bounds.y(),bounds.width(),bounds.height()},{clip.x(),clip.y(),clip.width(),clip.height()},{float(Font(0).GetFontSize()),Tone(this,kInk),Tone(this,kCard),HoverWash(this),Hairline(this),Palette(this).focus,Tone(this,kMuted)},has_close_button(),close_button_visible());}
  void ConfigureTabClose(bool wanted,views::Button::PressedCallback callback,const SidebarNodeBinding& binding){
    if(close_){auto* control=views::AsViewClass<RustActionView>(close_.get());if(!wanted||!control||!binding.scope||!control->BelongsTo(*binding.scope)||!control->LogicalNodeId()){auto* old=close_.get();close_=nullptr;RemoveChildViewT(old);}}
    if(wanted&&!close_){CHECK(callback);close_=AddChildView(CreateSceneAction(std::move(callback),T(u"Zamknij kartę"),ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,{},base::BindRepeating(&RustRowView::ChildChanged,weak_factory_.GetWeakPtr()),true,binding));}
    UpdateClose();InvalidateLayout();SchedulePaint();
  }
  void ViewHierarchyChanged(const views::ViewHierarchyChangedDetails& details) override{if(!details.is_add&&details.parent==this&&details.child==close_)close_=nullptr;views::View::ViewHierarchyChanged(details);}
  const Spec& preview_spec() const{return spec_;}
  void SetCallback(views::Button::PressedCallback callback){CancelInput();callback_=std::move(callback);}
  // `zt-tremor`: 1 px shakes in four steps over 180 ms (off under reduced motion).
  void Tremor(){if(gfx::Animation::PrefersReducedMotion())return;tremor_step_=0;tremor_timer_.Start(FROM_HERE,base::Milliseconds(45),base::BindRepeating(&RustRowView::TremorStep,base::Unretained(this)));}
  void SetAudible(bool audible){if(spec_.audible==audible)return;spec_.audible=audible;SchedulePaint();if(changed_)changed_.Run();}
  void SetEditing(bool editing){if(spec_.editing==editing)return;spec_.editing=editing;SchedulePaint();if(changed_)changed_.Run();}
  void SetActive(bool active){if(!state_->action_on(active))return;if(!state_->sidebar_visual().valid)spec_.active=active;measured_.reset();UpdateClose();SchedulePaint();}
  void SetTitle(std::u16string title){if(HasSidebarProperties()){if(state_->sidebar_set_title(SceneUtf16(title))){SyncSidebarAX();SchedulePaint();}return;}if(spec_.title==title)return;spec_.title=std::move(title);measured_.reset();SetTooltipText(spec_.title);GetViewAccessibility().SetName(spec_.title);SchedulePaint();}
  void SetDetail(std::u16string detail){if(spec_.detail==detail)return;spec_.detail=std::move(detail);InvalidateLayout();SchedulePaint();}
  void SetAddress(std::u16string host,std::u16string path,bool blank,bool secure){SetTitle(std::move(host));SetDetail(std::move(path));spec_.blank=blank;spec_.secure=secure;GetViewAccessibility().SetDescription(spec_.title+spec_.detail);GetViewAccessibility().SetName(T(u"Adres strony"));SetTooltipText(T(u"Szukaj lub wpisz adres"));InvalidateLayout();SchedulePaint();}
  void OnTooltipTextChanged(const std::u16string& previous) override{views::View::OnTooltipTextChanged(previous);auto& ax=GetViewAccessibility();if((ax.GetCachedName().empty()||ax.GetCachedName()==previous)&&!GetTooltipText().empty())ax.SetName(GetTooltipText());}
  bool history() const{return spec_.history;}
  sidebar::Item Scene(gfx::RectF box,gfx::RectF clip) const {
    const auto& spec=preview_spec();sidebar::Item item{};item.kind=25;
    item.x=box.x();item.y=box.y();item.width=box.width();item.height=box.height();
    item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
    item.title=base::UTF16ToUTF8(spec.title);item.detail=base::UTF16ToUTF8(spec.detail);
    item.title_width=scene_title_width();item.font_size=Font(0).GetFontSize();
    item.detail_font_size=Font(-1).GetFontSize();item.ink=Tone(this,kInk);item.detail_ink=Tone(this,kMuted);
    item.badge=spec.monogram_color;item.monogram=base::UTF16ToUTF8(spec.monogram);
    item.hovered=GetState()==views::Button::STATE_HOVERED||GetState()==views::Button::STATE_PRESSED;
    item.fill=item.hovered?HoverWash(this):SK_ColorTRANSPARENT;item.focused=HasFocus();item.focus=Palette(this).focus;return item;
  }
  void SetMonogram(std::u16string monogram,SkColor color){if(HasSidebarProperties()){if(state_->sidebar_set_monogram(SceneUtf16(monogram),color))SchedulePaint();return;}if(spec_.monogram==monogram&&spec_.monogram_color==color)return;spec_.monogram=std::move(monogram);spec_.monogram_color=color;SchedulePaint();}
  int scene_title_width() const{if(!spec_.address&&!spec_.chip&&spec_.detail.empty())return 0;if(!measured_)measured_=gfx::GetStringWidth(spec_.title,Font(0,spec_.active||spec_.chip||spec_.address?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL))+2;return *measured_;}
  views::Button::ButtonState GetState() const{const auto v=state_->action_snapshot().visual;return v==sidebar::RowVisual::Disabled?views::Button::STATE_DISABLED:v==sidebar::RowVisual::Pressed?views::Button::STATE_PRESSED:v==sidebar::RowVisual::Hovered?views::Button::STATE_HOVERED:views::Button::STATE_NORMAL;}
  bool has_close_button() const{return close_!=nullptr;}
  bool close_button_visible() const{return close_&&close_->GetVisible();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{const auto tab=state_->sidebar_visual();return gfx::Size(200,tab.valid?tab.height:spec_.height);}
  void Layout(PassKey) override{const auto r=Geometry().close;if(close_)close_->SetBounds(r.x,r.y,r.width,r.height);}
  bool HandleAccessibleAction(const ui::AXActionData& action) override{if(action.action==ax::mojom::Action::kDoDefault&&SceneIsVisible(this)&&GetEnabledInViewsSubtree())ScrollViewToVisible();return views::View::HandleAccessibleAction(action);}
  bool OnMousePressed(const ui::MouseEvent& event) override{return Pointer(sidebar::PointerPhase::Press,event);}
  bool OnMouseDragged(const ui::MouseEvent& event) override{return Pointer(sidebar::PointerPhase::Drag,event);}
  void OnMouseReleased(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Release,event);}
  void OnMouseEntered(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Enter,event);}
  void OnMouseExited(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Exit,event);}
  void OnMouseMoved(const ui::MouseEvent& event) override{Pointer(sidebar::PointerPhase::Move,event);}
  bool OnKeyPressed(const ui::KeyEvent& event) override{return Key(true,event);}
  bool OnKeyReleased(const ui::KeyEvent& event) override{return Key(false,event);}
  void OnGestureEvent(ui::GestureEvent* event) override{SyncEnabled();const uint32_t kind=event->type()==ui::EventType::kGestureTapDown?0:event->type()==ui::EventType::kGestureTap?1:event->type()==ui::EventType::kGestureTapCancel||event->type()==ui::EventType::kGestureEnd?2:99;const auto before=state_->action_snapshot().visual;auto result=state_->action_gesture(kind,Interactive()&&(kind==2||(HitTestPoint(event->location())&&GetVisibleBounds().Contains(event->location()))));if(result.handled)event->SetHandled();Apply(result,*event,before);}
  void OnFocus() override{views::View::OnFocus();state_->action_focus(true);SchedulePaint();}
  void OnBlur() override{state_->action_focus(false);UpdateClose();UpdateWatch();views::View::OnBlur();SchedulePaint();}
  void OnMouseCaptureLost() override{CancelInput();EndDrag();}
  void VisibilityChanged(views::View* starting,bool visible) override{views::View::VisibilityChanged(starting,visible);if(!SceneIsVisible(this))CancelInput();}
  void AddedToWidget() override{views::View::AddedToWidget();window_.Observe(GetWidget());}
  void RemovedFromWidget() override{window_.Observe(nullptr);CancelInput();views::View::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);if(changed_)changed_.Run();}
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_.reset();SchedulePaint();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);if(changed_)changed_.Run();}
  void CancelInput(){const bool changed=state_->action_cancel();if(close_)CancelActionInput(close_);UpdateClose();UpdateWatch();if(changed)SchedulePaint();}
  void SyncViewport(){if(!Interactive())CancelInput();else if(close_){if(auto* close=views::AsViewClass<RustActionView>(close_.get()))close->SyncViewportVisibility();}}
  void PaintChildren(const views::PaintInfo& info) override{if(!spec_.scene_visible||!spec_.scene_visible.Run())views::View::PaintChildren(info);}
  void OnPaint(gfx::Canvas* canvas) override{
    if(spec_.scene_visible&&spec_.scene_visible.Run())return;SyncEnabled();if(state_->sidebar_visual().valid){
      // The compositor raster paints the same Rust recipe as the Metal surface;
      // the close child paints itself.
      const auto bounds=gfx::RectF(GetLocalBounds());auto scene=SidebarScene(bounds,bounds);
      if(scene.valid){if(scene.item.tile){const auto spec=SidebarSnapshot();const auto visual=state_->action_snapshot().visual;RowRecipe::PaintDecorations(canvas,GetLocalBounds(),spec,this,visual==sidebar::RowVisual::Hovered||visual==sidebar::RowVisual::Pressed);}
        scene.item.close_visible=false;
        // A folder shows the folder icon in the row's ink, never an emoji.
        const bool folder=state_->sidebar_presentation().folder;if(folder)scene.item.monogram=rust::String();
        if(!scene.item.tile){PaintSceneRecipe(canvas,scene.item,Font(0),true);if(folder)PaintIcon(canvas,vector_icons::kFolderFilledIcon,gfx::Rect(12,(height()-16)/2,16,16),16,scene.item.ink);
          // The close button takes this place on hover.
          if(spec_.audible&&!(close_&&close_->GetVisible()))PaintIcon(canvas,vector_icons::kVolumeUpIcon,gfx::Rect(width()-30,(height()-14)/2,14,14),14,Palette(this).accent_ink);}else{const auto geometry=Geometry();const auto& title=geometry.title;canvas->DrawStringRectWithFlags(SidebarSnapshot().title,DesignFont(2,13,scene.item.active?600:400,false),scene.item.ink,gfx::Rect(title.x,title.y,title.width,title.height),gfx::Canvas::TEXT_ALIGN_LEFT);}}
      return;}
    if(spec_.chip&&!spec_.address){const auto bounds=gfx::RectF(GetLocalBounds());const auto v=state_->action_snapshot();
      if(!spec_.header.empty())PaintSceneRecipe(canvas,PaletteHeaderItem(this,spec_,bounds,bounds),Font(0),true);
      PaintSceneRecipe(canvas,PaletteRowItem(this,spec_,bounds,bounds,v.visual==sidebar::RowVisual::Hovered||v.visual==sidebar::RowVisual::Pressed,HasFocus()),Font(0),true);
      if(spec_.icon){const auto p=Palette(this);const auto body=gfx::ToEnclosingRect(PaletteRowBody(spec_,bounds));PaintIcon(canvas,*spec_.icon,gfx::Rect(12,body.y(),16,body.height()),16,spec_.highlighted?p.accent_ink:p.ink_muted);}return;}
    if(spec_.kbd){const auto bounds=gfx::RectF(GetLocalBounds());const auto v=state_->action_snapshot();for(const auto& item:CommandRowItems(this,spec_,bounds,bounds,v.visual==sidebar::RowVisual::Hovered||v.visual==sidebar::RowVisual::Pressed,HasFocus()))PaintSceneRecipe(canvas,item,Font(0),true);return;}
    gfx::ScopedCanvas tremor(canvas);canvas->Translate(gfx::Vector2d(tremor_dx_,0));
    if(spec_.address){const auto bounds=gfx::RectF(GetLocalBounds());const auto v=state_->action_snapshot();PaintSceneRecipe(canvas,AddressRowItem(this,spec_,bounds,bounds,scene_title_width(),v.visual==sidebar::RowVisual::Hovered||v.visual==sidebar::RowVisual::Pressed,HasFocus()),Font(0),true);return;}if(spec_.history){const auto bounds=gfx::RectF(GetLocalBounds());PaintSceneRecipe(canvas,Scene(bounds,bounds),Font(0),true);return;}const auto v=state_->action_snapshot().visual;RowRecipe::PaintDecorations(canvas,GetLocalBounds(),spec_,this,v==sidebar::RowVisual::Hovered||v==sidebar::RowVisual::Pressed);
    const auto geometry=Geometry();const auto& r=geometry.title;canvas->DrawStringRectWithFlags(spec_.title,Font(0,spec_.active||spec_.chip||spec_.address?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL),Tone(this,spec_.blank?kMuted:kInk),gfx::Rect(r.x,r.y,r.width,r.height),gfx::Canvas::TEXT_ALIGN_LEFT);
    if(!spec_.detail.empty()){const auto& d=geometry.detail;canvas->DrawStringRectWithFlags(gfx::ElideText(spec_.detail,Font(spec_.address?0:-1),d.width,spec_.address||spec_.stacked?gfx::ELIDE_TAIL:gfx::ELIDE_MIDDLE),Font(spec_.address?0:-1),Tone(this,kMuted),gfx::Rect(d.x,d.y,d.width,d.height),gfx::Canvas::TEXT_ALIGN_LEFT);}
  }
  void TremorStep(){static constexpr std::array<int,4> kOffsets={-1,1,-1,0};if(tremor_step_>=static_cast<int>(kOffsets.size())){tremor_timer_.Stop();return;}tremor_dx_=kOffsets[tremor_step_++];SchedulePaint();if(changed_)changed_.Run();}
 private:
  int tremor_dx_=0,tremor_step_=0;base::RepeatingTimer tremor_timer_;
  static void ClearSidebarVisuals(Spec& spec){std::u16string{}.swap(spec.title);std::u16string{}.swap(spec.monogram);spec.monogram_color=SK_ColorTRANSPARENT;spec.icon=nullptr;spec.active=false;spec.tile=false;spec.height=0;}
  void SyncSidebarAX(){const auto title=state_->sidebar_title();const std::u16string text(title.begin(),title.end());SetTooltipText(text);GetViewAccessibility().SetName(text);}
  sidebar::RowLayout Geometry() const{if(spec_.stacked){auto g=sidebar::row_layout(width(),height(),0,false,false,close_!=nullptr);const int lead=50,half=height()/2;const int w=std::max(0,g.title.width+g.title.x-lead);g.title={lead,2,w,half-2};g.detail={lead,half,w,half-4};return g;}if(spec_.address)return sidebar::address_layout(width(),height(),scene_title_width());return sidebar::row_layout(width(),height(),scene_title_width(),!spec_.detail.empty(),spec_.chip,close_!=nullptr);}
  bool Interactive() const{return SceneIsVisible(this)&&!GetVisibleBounds().IsEmpty();}
  void SyncEnabled(){if(!state_->action_enabled(GetEnabledInViewsSubtree()))return;auto& ax=GetViewAccessibility();if(GetEnabledInViewsSubtree())ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);else ax.RemoveDefaultActionVerb();UpdateClose();UpdateWatch();SchedulePaint();}
  void EnabledChanged(){SyncEnabled();}
  void WindowChanged(){CancelInput();state_->action_focus(HasFocus());SchedulePaint();}
  void UpdateClose(){if(close_){const auto v=state_->action_snapshot();const auto tab=state_->sidebar_visual();close_->SetVisible(sidebar::close_visible(v.on,tab.valid?tab.tile:spec_.tile,v.visual==sidebar::RowVisual::Hovered||v.visual==sidebar::RowVisual::Pressed));}}
  void ChildChanged(){UpdateWatch();if(changed_)changed_.Run();}
  void UpdateWatch(){if(!watch_)return;bool pending=state_->action_pending();if(close_)if(auto* action=views::AsViewClass<RustActionView>(close_.get()))pending=pending||action->Pending();watch_.Run(this,pending?base::BindRepeating(&RustRowView::SyncViewport,weak_factory_.GetWeakPtr()):base::RepeatingClosure());}
  bool Pointer(sidebar::PointerPhase phase,const ui::MouseEvent& event){
    if(phase==sidebar::PointerPhase::Press)grab_=event.location();
    const bool handled=PointerInput(phase,event);
    if(phase==sidebar::PointerPhase::Drag&&spec_.drag&&state_->action_row_dragging()){dragging_=true;auto drag=spec_.drag;drag.Run(this,grab_,display::Screen::Get()->GetCursorScreenPoint());}
    else if(phase==sidebar::PointerPhase::Release)EndDrag();
    return handled;
  }
  void EndDrag(){if(!dragging_)return;dragging_=false;if(spec_.drag){auto drag=spec_.drag;drag.Run(this,grab_,std::nullopt);}}
  bool dragging_=false;
  gfx::Point grab_;
  bool PointerInput(sidebar::PointerPhase phase,const ui::MouseEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;return Apply(state_->action_row_pointer(phase,event.x(),event.y(),phase!=sidebar::PointerPhase::Exit&&HitTestPoint(event.location())&&GetVisibleBounds().Contains(event.location()),event.IsLeftMouseButton(),Interactive(),!spec_.drop.is_null()),event,before);}
  bool Key(bool pressed,const ui::KeyEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;return Apply(state_->action_key(pressed,event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0,true,Interactive()),event,before);}
  bool Apply(const sidebar::RowInputResult& result,const ui::Event& event,sidebar::RowVisual before){UpdateClose();UpdateWatch();if(result.visual!=before)SchedulePaint();const bool handled=result.handled;if(result.action==sidebar::RowAction::Activate&&callback_){callback_.Run(event);return handled;}if(result.action==sidebar::RowAction::Drop&&spec_.drop){auto drop=spec_.drop;drop.Run(display::Screen::Get()->GetCursorScreenPoint());return handled;}return handled;}
  Spec spec_;views::Button::PressedCallback callback_;base::RepeatingClosure changed_;Watch watch_;raw_ptr<views::View> close_=nullptr;mutable std::optional<int> measured_;rust::Box<sidebar::ActionControl> state_;SceneWindowObserver window_{base::BindRepeating(&RustRowView::WindowChanged,base::Unretained(this))};base::CallbackListSubscription enabled_changed_{AddEnabledInViewsSubtreeChangedCallback(base::BindRepeating(&RustRowView::EnabledChanged,base::Unretained(this)))};base::WeakPtrFactory<RustRowView> weak_factory_{this};
};
BEGIN_METADATA(RustRowView)
END_METADATA
class ScenePinGroup final : public views::View {
  METADATA_HEADER(ScenePinGroup,views::View)
 public:
  ScenePinGroup(const std::shared_ptr<SidebarNodeGraphState>& scope,uint64_t id):scope_(scope),id_(id){}
  uint64_t AdapterId() const{return id_;}
  bool Matches(const std::shared_ptr<SidebarNodeGraphState>& scope,uint64_t id) const{return scope_.lock()==scope&&id_==id;}
  uint64_t NodeId() const{const auto scope=scope_.lock();if(!scope)return 0;for(const auto& entry:scope->graph->pin_tree().entries)if(entry.group==id_)return id_;return 0;}
  uint64_t ParentId() const{const auto scope=scope_.lock();if(!scope)return 0;for(const auto& entry:scope->graph->pin_tree().entries)if(entry.group==id_)return entry.parent;return 0;}
 private:
  std::weak_ptr<SidebarNodeGraphState> scope_;uint64_t id_;
};
BEGIN_METADATA(ScenePinGroup)
END_METADATA
std::unique_ptr<views::View> CreateSceneRow(views::Button::PressedCallback callback,RowRecipe::Spec spec,base::RepeatingClosure changed,base::RepeatingCallback<void(views::View*,base::RepeatingClosure)> watch,SidebarNodeBinding binding={}){
  auto state=AcquireSidebarInput(binding);SidebarNodeBinding close_binding;
  if(binding.scope&&state->tab_binding().valid){const auto update=binding.scope->graph->project_tab(*state,SceneUtf16(spec.title),SceneUtf16(spec.monogram),spec.monogram_color,spec.tile);CHECK(update.admitted);}
  if(binding.scope&&spec.close){CHECK_EQ(binding.kind,1u);close_binding={binding.scope,10,binding.identity,{}};}
  return std::make_unique<RustRowView>(std::move(callback),std::move(spec),std::move(changed),std::move(watch),std::move(state),std::move(close_binding));
}
template<typename Callback> decltype(auto) VisitRow(views::View* view,Callback callback){
  return callback(views::AsViewClass<RustRowView>(view));
}
bool IsRow(views::View* view){
  return views::AsViewClass<RustRowView>(view)!=nullptr;
}
const RowRecipe::Spec& RowSpec(views::View* view){return VisitRow(view,[](auto* row)->const RowRecipe::Spec&{return row->preview_spec();});}
void SetRowActive(views::View* view,bool value){VisitRow(view,[value](auto* row){row->SetActive(value);});}
void TremorRow(views::View* view){if(auto* row=views::AsViewClass<RustRowView>(view))row->Tremor();}
void SetRowAudible(views::View* view,bool value){if(auto* row=views::AsViewClass<RustRowView>(view))row->SetAudible(value);}
void SetCreateAction(views::View* view,bool creating){if(auto* button=views::AsViewClass<SceneTextButton>(view))button->SetZing(creating);}
void SetRowEditing(views::View* view,bool value){if(auto* row=views::AsViewClass<RustRowView>(view))row->SetEditing(value);}
void SetRowTitle(views::View* view,std::u16string value){VisitRow(view,[&value](auto* row){row->SetTitle(std::move(value));});}
void SetRowDetail(views::View* view,std::u16string value){VisitRow(view,[&value](auto* row){row->SetDetail(std::move(value));});}
void SetRowMonogram(views::View* view,std::u16string value,SkColor color){VisitRow(view,[&value,color](auto* row){row->SetMonogram(std::move(value),color);});}
void SetRowCallback(views::View* view,views::Button::PressedCallback value){VisitRow(view,[&value](auto* row){row->SetCallback(std::move(value));});}
template<typename Callback> decltype(auto) WithRowSnapshot(views::View* view,Callback callback){
  if(auto* row=views::AsViewClass<RustRowView>(view);row&&row->HasSidebarProperties()){const auto snapshot=row->SidebarSnapshot();return callback(snapshot);}
  return callback(RowSpec(view));
}
bool RowHovered(views::View* view){return VisitRow(view,[](auto* row){return row->GetState()==views::Button::STATE_HOVERED||row->GetState()==views::Button::STATE_PRESSED;});}
int RowTitleWidth(views::View* view){return VisitRow(view,[](auto* row){return row->scene_title_width();});}
bool RowHasClose(views::View* view){return VisitRow(view,[](auto* row){return row->has_close_button();});}
bool RowCloseVisible(views::View* view){return VisitRow(view,[](auto* row){return row->close_button_visible();});}
void SetRowAddress(views::View* view,std::u16string host,std::u16string path,bool blank,bool secure){VisitRow(view,[&](auto* row){row->SetAddress(std::move(host),std::move(path),blank,secure);});}
void SetPreferenceNavigationSelected(views::View* control,bool selected){
  views::AsViewClass<ScenePreferenceButton>(control)->SetSelected(selected);
}


// One compact sidebar item containing the two pages in visual pane order.
class SplitPair final : public views::View {
  METADATA_HEADER(SplitPair, views::View)
 public:
  SplitPair() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(T(u"Split View — para kart"));
    SetLayoutManager(CreateSidebarSplitLayout());
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    // Two ordinary rows must share the sidebar width, not request two widths.
    return SidebarSplitPreferredSize();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    FillRound(canvas, GetLocalBounds(), 12, SkColorSetA(Tone(this, kCard), 0x33));
  }
};
BEGIN_METADATA(SplitPair)
END_METADATA

// Address chrome uses the shared row host and the same named AX adapter.
std::unique_ptr<views::View> CreateAddressRow(views::Button::PressedCallback callback,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,base::RepeatingCallback<void(views::View*,base::RepeatingClosure)> watch,SidebarNodeBinding binding={}){
  RowRecipe::Spec spec;spec.address=true;spec.sidebar_row=true;spec.height=36;  // AddressPill: 36 px
  spec.scene_visible=std::move(visible);
  auto row=CreateSceneRow(std::move(callback),std::move(spec),std::move(changed),std::move(watch),std::move(binding));row->GetViewAccessibility().SetName(T(u"Adres strony"));row->SetTooltipText(T(u"Szukaj lub wpisz adres"));return row;
}

// Scene-owned text with one native static-text accessibility node. The Views
// backend retains its ordinary Label; Metal never constructs a hidden Label.
class WorkspaceHeading final : public views::View {
  METADATA_HEADER(WorkspaceHeading, views::View)
 public:
  explicit WorkspaceHeading(base::RepeatingCallback<bool()> visible) : visible_(std::move(visible)) {
    SetPreferredSize(gfx::Size(200, 24));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
    SetText(T(u"Praca"));
  }
  void SetText(std::u16string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    GetViewAccessibility().SetName(text_);
    SchedulePaint();
  }
  const std::u16string& text() const { return text_; }
  void OnPaint(gfx::Canvas* canvas) override {
    if (visible_ && visible_.Run()) return;
    auto bounds = GetLocalBounds(); bounds.Inset(gfx::Insets::VH(0, 8));
    canvas->DrawStringRectWithFlags(text_, Font(-1, gfx::Font::Weight::SEMIBOLD),
        Tone(this, kSubtle), bounds, gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:
  std::u16string text_;
  base::RepeatingCallback<bool()> visible_;
};
BEGIN_METADATA(WorkspaceHeading)
END_METADATA

// Static palette text retains a native AX node and font-metrics adapter.
// The factory leaves ordinary Labels untouched in the default Views backend.
class PaletteText final : public views::View {
  METADATA_HEADER(PaletteText,views::View)
 public:
  PaletteText(std::u16string text,uint32_t kind,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      :text_(std::move(text)),kind_(kind),font_(Font(kind==2?-1:-2)),painted_(std::move(painted)),visible_(std::move(visible)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);
  }
  void SetText(std::u16string text) {
    if(text_==text)return;text_=std::move(text);measured_.reset();
    GetViewAccessibility().SetName(text_);
    PreferredSizeChanged();SchedulePaint();painted_.Run();
  }
  const std::u16string& text() const{return text_;}
  int text_width() const{if(!measured_)measured_=text_.empty()?0:gfx::GetStringWidth(text_,font_)+2;return *measured_;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {
    const auto g=sidebar::palette_text_layout(kind_,-1,-1,text_width(),font_.GetHeight());return gfx::Size(g.width,g.height);
  }
  gfx::Rect text_bounds() const {
    const auto g=sidebar::palette_text_layout(kind_,width(),height(),text_width(),font_.GetHeight());const auto& r=g.children[0];return gfx::Rect(r.x,r.y,r.width,r.height);
  }
  sidebar::Item SceneItem(gfx::RectF rect,gfx::RectF clip) const {
    sidebar::Item item{};item.kind=15;item.glyph=kind_;item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
    item.title=base::UTF16ToUTF8(text_);item.title_width=text_width();item.font_size=font_.GetFontSize();item.font_height=font_.GetHeight();item.ink=Tone(this,kind_==2?kError:kMuted);item.badge=Hairline(this);return item;
  }
  // The footer (kind 1): `bg-base` under a `line`, then three shortcuts, each a
  // row of Kbd keys and a verb in `label`.
  std::vector<sidebar::Item> SceneItems(gfx::RectF rect,gfx::RectF clip) const {
    if(kind_!=1)return {SceneItem(rect,clip)};
    const auto p=Palette(this);std::vector<sidebar::Item> items;const sidebar::FloatRect c{clip.x(),clip.y(),clip.width(),clip.height()};
    sidebar::Item fill{};fill.kind=14;fill.x=rect.x();fill.y=rect.y();fill.width=rect.width();fill.height=rect.height();fill.fill=p.bg_base;fill.clip=c;items.push_back(fill);
    sidebar::Item line{};line.kind=14;line.x=rect.x();line.y=rect.y();line.width=rect.width();line.height=1;line.fill=p.line;line.clip=c;items.push_back(line);
    float x=rect.x()+18;const float cy=rect.y()+rect.height()/2;
    const std::array<std::pair<std::vector<std::u16string>,std::u16string>,3> hints{{{{u"↑",u"↓"},T(u"wybierz")},{{u"↵"},T(u"otwórz")},{{u"esc"},T(u"zamknij")}}};
    for(const auto& [keys,word]:hints){
      for(const auto& key:keys){sidebar::Item k{};k.kind=30;k.title=base::UTF16ToUTF8(key);k.width=std::max(18.f,gfx::GetStringWidth(key,DesignFont(3,11,500,false))+10.f);k.height=18;k.x=x;k.y=cy-9;k.fill=p.bg_hover;k.edge=p.line;k.ink=p.ink_muted;k.clip=c;items.push_back(k);x+=k.width+2;}
      sidebar::Item w{};w.kind=17;w.title=base::UTF16ToUTF8(word);w.x=x+4;w.width=gfx::GetStringWidth(word,DesignFont(2,12,500,false))+2;w.y=rect.y();w.height=rect.height();w.ink=p.ink_muted;w.font_size=12;w.text_style=6;w.clip=c;items.push_back(w);
      x=w.x+w.width+16;
    }
    return items;
  }
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_.reset();PreferredSizeChanged();painted_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void OnPaint(gfx::Canvas* canvas) override {
    if(visible_&&visible_.Run())return;
    const auto bounds=gfx::RectF(GetLocalBounds());for(const auto& item:SceneItems(bounds,bounds))PaintSceneRecipe(canvas,item,font_,true);
  }
 private:
  std::u16string text_;uint32_t kind_;gfx::FontList font_;
  base::RepeatingClosure painted_;base::RepeatingCallback<bool()> visible_;
  mutable std::optional<int> measured_;
};
BEGIN_METADATA(PaletteText)
END_METADATA

// The modal host retains system focus/buttons/IME. Rust owns this form's
// copy, validation/submission state, geometry and static text display list.
// A scene-owned single-body modal, retaining only native window/frame,
// scrolling, typography, focus and AX services. The caller owns this delegate
// and destroys its CLIENT_OWNS_WIDGET widget before deleting the delegate.
class SceneModalDialog final : public views::BubbleDialogDelegate {
 public:
  SceneModalDialog(std::unique_ptr<views::View> body,bool control,bool modal=true)
      :views::BubbleDialogDelegate(views::BubbleAnchor(),views::BubbleBorder::Arrow::NONE,views::BubbleBorder::DIALOG_SHADOW,true){
    SetModalType(modal?ui::mojom::ModalType::kWindow:ui::mojom::ModalType::kNone);SetShowCloseButton(!modal);if(!modal)set_close_on_deactivate(false);SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    // Design: Dialog — 400 px wide, `radius-lg`, `space-6` at the sides.
    auto* provider=views::LayoutProvider::Get();set_fixed_width(400);set_corner_radius(20);
    const auto sides=gfx::Insets::VH(0,24);
    // Modal bodies retain the native model host's 2px scroll workaround.
    const int scroll_margin=modal?2:0;
    auto contents=std::make_unique<views::View>();SetSidebarPanelLayout(contents.get(),true,gfx::Insets::TLBR(scroll_margin,sides.left(),scroll_margin,sides.right()),0);contents->AddChildView(std::move(body));
    if(modal){PanelViewport* viewport=nullptr;auto scroll=CreatePanelViewport(&viewport,base::BindRepeating([](views::View* root){RefreshChoiceViewports(root);auto paint=[&](auto&& self,views::View* view)->void{view->SchedulePaint();for(const auto& child:view->children())self(self,child.get());};paint(paint,root);},base::Unretained(contents.get())),{0,provider->GetDistanceMetric(views::DISTANCE_MODAL_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT),false,true});viewport->SetPanelContents(std::move(contents));SetContentsView(std::move(scroll));}
    else SetContentsView(std::move(contents));
    set_margins(gfx::Insets::TLBR(std::max(0,provider->GetDistanceMetric(control?views::DISTANCE_DIALOG_CONTENT_MARGIN_TOP_CONTROL:views::DISTANCE_DIALOG_CONTENT_MARGIN_TOP_TEXT)-scroll_margin),0,std::max(0,provider->GetDistanceMetric(control?views::DISTANCE_DIALOG_CONTENT_MARGIN_BOTTOM_CONTROL:views::DISTANCE_DIALOG_CONTENT_MARGIN_BOTTOM_TEXT)-scroll_margin),0));
  }
  views::ClientView* CreateClientView(views::Widget* widget) override{
    auto* client=CreateSceneDialogClient(widget,TransferOwnershipOfContentsView());client->SetPaintToLayer(layer_type());client->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(GetCornerRadius()));client->layer()->SetIsFastRoundedCorner(true);return client;
  }
  views::View* GetInitiallyFocusedView() override{
    if(HasConfiguredInitiallyFocusedView())return views::WidgetDelegate::GetInitiallyFocusedView();
    return GetDefaultDialogButton()?DialogActionButton(GetWidget(),GetDefaultDialogButton()==static_cast<int>(ui::mojom::DialogButton::kOk)):nullptr;
  }
};
class FolderFormView final : public views::View {
  METADATA_HEADER(FolderFormView,views::View)
 public:
  void SyncWindowVisibility(){if(!SceneIsVisible(this))for(auto& surface:surfaces_)if(surface)surface->Hide();Schedule();}

  FolderFormView(const std::string& workspace,const std::string& parent,const std::string& folder,std::u16string initial,
      base::RepeatingCallback<bool(FolderFormView*)> submit)
      :form_(sidebar::new_folder_form(workspace,parent,folder,base::UTF16ToUTF8(initial))),copy_(form_->copy()),submit_(std::move(submit)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    for(auto& part:parts_){part=AddChildView(std::make_unique<views::View>());part->GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);}
    parts_[1]->GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
    parts_[0]->GetViewAccessibility().SetName(base::UTF8ToUTF16(std::string(copy_.label)));parts_[1]->SetVisible(false);
    field_=AddEditor(this);field_->GetViewAccessibility().SetName(base::UTF8ToUTF16(std::string(copy_.label)));
    field_->SetText(initial);
    field_->SetCallbacks(base::BindRepeating([](base::WeakPtr<FolderFormView> form){
      if(form){form->form_->edit(base::UTF16ToUTF8(form->field_->GetText()));form->Refresh();}
    },weak()),{});
  }
  ~FolderFormView() override{field_->SetCallbacks({},{});}
  base::WeakPtr<FolderFormView> weak(){return weak_factory_.GetWeakPtr();}
  EditorControl* field() const{return field_;}
  std::u16string title() const{return base::UTF8ToUTF16(std::string(copy_.title));}
  std::u16string accept_label() const{return base::UTF8ToUTF16(std::string(copy_.accept));}
  std::u16string cancel_label() const{return base::UTF8ToUTF16(std::string(copy_.cancel));}
  bool Accept(){return field_->IsIMEComposing()?false:submit_.Run(this);}
  void Cancel(){field_->ClearCompositionText();form_->cancel();}
  sidebar::FolderRequest Prepare(const std::string& current,bool ready,bool busy,const std::vector<PinnedItem>& pins) {
    form_->edit(base::UTF16ToUTF8(field_->GetText()));std::vector<sidebar::TreeNode> nodes;nodes.reserve(pins.size());for(const auto& pin:pins)nodes.push_back({pin.id,pin.parent_id,pin.folder});
    auto request=form_->submit(current,ready,busy,rust::Slice<const sidebar::TreeNode>(nodes.data(),nodes.size()),field_->IsIMEComposing());Refresh();return request;
  }
  void Finish(bool accepted){form_->finish_attempt(accepted);Refresh();}
  bool metal_visible() const{return surfaces_[0]&&surfaces_[0]->visible()&&(error_.empty()||(surfaces_[1]&&surfaces_[1]->visible()));}
  bool contains_scene_text(std::string_view text) const{for(const auto& surface:surfaces_)if(surface&&surface->visible()&&surface->contains_text_for_testing(text))return true;return false;}
  const std::u16string& error_text() const{return error_;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {
    const auto g=Geometry(available.width().is_bounded()?available.width().value():-1);return gfx::Size(g.width,g.height);
  }
  void Layout(PassKey) override {
    const auto g=Geometry(width());auto rect=[](const sidebar::Rect& r){return gfx::Rect(r.x,r.y,r.width,r.height);};
    parts_[0]->SetBoundsRect(rect(g.label));field_->view()->SetBoundsRect(rect(g.input));parts_[1]->SetBoundsRect(rect(g.error));Schedule();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();SetBackground(views::CreateSolidBackground(Tone(this,kCard)));
    Schedule();
  }
  void AddedToWidget() override{views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);for(auto& surface:surfaces_)surface.reset();views::View::RemovedFromWidget();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {
    views::View::OnDidSchedulePaint(rect);if(field_&&views::View::ConvertRectToTarget(field_->view(),this,field_->view()->GetLocalBounds()).Contains(rect))return;Schedule();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    for(size_t i=0;i<parts_.size();++i)if(parts_[i]->GetVisible()&&(!surfaces_[i]||!surfaces_[i]->visible()))PaintSceneRecipe(canvas,Item(i,gfx::RectF(parts_[i]->GetMirroredBounds())),Font(-1));
    Schedule();
  }
 private:
  sidebar::FolderLayout Geometry(int width) const{return sidebar::folder_form_layout(width,Font(-1).GetHeight(),field_?field_->view()->GetPreferredSize().height():32,Font(-1).GetHeight(),!error_.empty());}
  sidebar::Item Item(size_t i,gfx::RectF r) const {
    sidebar::Item item{};item.kind=17;item.x=r.x();item.y=r.y();item.width=r.width();item.height=r.height();item.clip={r.x(),r.y(),r.width(),r.height()};
    item.title=i?base::UTF16ToUTF8(error_):std::string(copy_.label);item.ink=Tone(this,i?kError:kMuted);item.font_size=Font(-1).GetFontSize();return item;
  }
  void Refresh() {
    const auto error=base::UTF8ToUTF16(std::string(form_->error()));if(error==error_)return;error_=error;
    parts_[1]->GetViewAccessibility().SetName(error_);parts_[1]->SetVisible(!error_.empty());
    if(!error_.empty()&&GetWidget())parts_[1]->GetViewAccessibility().NotifyEvent(ax::mojom::Event::kAlert,true);
    PreferredSizeChanged();SchedulePaint();
    if(GetWidget())if(auto* host=GetWidget()->widget_delegate()->AsBubbleDialogDelegate())host->SizeToContents();Schedule();
  }
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&FolderFormView::UpdateMetal,weak()));}
  void UpdateMetal() {
    pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();
    for(size_t i=0;i<parts_.size();++i) {
      auto& surface=surfaces_[i];const bool was=surface&&surface->visible();
      if(!SceneIsVisible(parts_[i])||parts_[i]->GetVisibleBounds().IsEmpty()){if(surface)surface->Hide();if(was)SchedulePaint();continue;}
      if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
      const auto local=parts_[i]->GetVisibleBounds();auto frame=parts_[i]->GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());
      auto full=gfx::RectF(parts_[i]->GetLocalBounds());full.Offset(-local.x(),-local.y());auto item=Item(i,full);item.clip={0,0,float(frame.width()),float(frame.height())};
      surface->Update(GetWidget()->GetNativeWindow(),frame,{item},Tone(this,kCard));if(was!=surface->visible())SchedulePaint();
    }
  }
  rust::Box<sidebar::FolderForm> form_;sidebar::FolderCopy copy_;base::RepeatingCallback<bool(FolderFormView*)> submit_;
  std::array<raw_ptr<views::View>,2> parts_{};raw_ptr<EditorControl> field_=nullptr;
  std::array<std::unique_ptr<SidebarMetal>,2> surfaces_;std::u16string error_;bool pending_=false;
  SceneWindowObserver window_visibility_{base::BindRepeating(&FolderFormView::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<FolderFormView> weak_factory_{this};
};
BEGIN_METADATA(FolderFormView)
END_METADATA
FolderFormView* FindFolderForm(views::View* view) {
  if(auto* form=views::AsViewClass<FolderFormView>(view))return form;
  for(const auto& child:view->children())if(auto* form=FindFolderForm(child.get()))return form;return nullptr;
}

class NoticeBodyView : public views::View {
  METADATA_HEADER(NoticeBodyView,views::View)
 public:
  void SyncWindowVisibility(){if(!SceneIsVisible(this))for(auto& surface:surfaces_)if(surface)surface->Hide();Schedule();}

  explicit NoticeBodyView(sidebar::NoticeCopy copy):copy_(std::move(copy)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    for(auto& part:parts_){part=AddChildView(std::make_unique<views::View>());part->GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);}
    parts_[1]->GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
    parts_[0]->GetViewAccessibility().SetName(base::UTF8ToUTF16(std::string(copy_.body)));parts_[1]->SetVisible(false);
  }
  const sidebar::NoticeCopy& copy() const{return copy_;}
  bool metal_visible() const{return surfaces_[0]&&surfaces_[0]->visible()&&(error_.empty()||(surfaces_[1]&&surfaces_[1]->visible()));}
  bool contains_scene_text(std::string_view text) const{for(const auto& surface:surfaces_)if(surface&&surface->visible()&&surface->contains_text_for_testing(text))return true;return false;}
  const std::u16string& error_text() const{return error_;}
  size_t body_line_count() const{Geometry(width());return lines_[0].size();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {
    const auto g=Geometry(available.width().is_bounded()?available.width().value():-1);return gfx::Size(g.width,g.height);
  }
  void Layout(PassKey) override{const auto g=Geometry(width());parts_[0]->SetBounds(g.body.x,g.body.y,g.body.width,g.body.height);parts_[1]->SetBounds(g.error.x,g.error.y,g.error.width,g.error.height);Schedule();}
  void OnThemeChanged() override{views::View::OnThemeChanged();SetBackground(views::CreateSolidBackground(Tone(this,kCard)));measured_width_=-1;PreferredSizeChanged();Schedule();}
  void AddedToWidget() override{views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);for(auto& surface:surfaces_)surface.reset();views::View::RemovedFromWidget();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto g=Geometry(width());
    for(size_t i=0;i<2;++i)if(parts_[i]->GetVisible()&&(!surfaces_[i]||!surfaces_[i]->visible())) {
      const auto clip=gfx::RectF(parts_[i]->GetMirroredBounds());const auto items=Items(i,g,0,0,clip);
      for(const auto& item:items)PaintSceneRecipe(canvas,item,Font(i?-1:0));
    }
    Schedule();
  }
 private:
  sidebar::NoticeLayout Geometry(int requested_width) const {
    const int width=requested_width<0?400:requested_width;const auto body_font=Font(0),error_font=Font(-1);
    if(width!=measured_width_||body_font.GetFontSize()!=measured_body_font_||error_font.GetFontSize()!=measured_error_font_||error_!=measured_error_) {
      lines_[0]=ShapeSidebarParagraph(std::string(copy_.body),body_font.GetFontSize(),width);
      lines_[1]=ShapeSidebarParagraph(base::UTF16ToUTF8(error_),error_font.GetFontSize(),width);
      measured_width_=width;measured_body_font_=body_font.GetFontSize();measured_error_font_=error_font.GetFontSize();measured_error_=error_;
    }
    return sidebar::notice_layout(width,body_font.GetHeight(),lines_[0].size(),error_font.GetHeight(),lines_[1].size());
  }
  std::vector<sidebar::Item> Items(size_t i,const sidebar::NoticeLayout& g,float dx,float dy,gfx::RectF clip) const {
    const auto& rects=i?g.error_lines:g.body_lines;std::vector<sidebar::Item> items;items.reserve(rects.size());
    for(size_t n=0;n<rects.size();++n){const auto& r=rects[n];sidebar::Item item{};item.kind=17;item.x=r.x+dx;item.y=r.y+dy;item.width=r.width;item.height=r.height;item.title=lines_[i][n];item.font_size=Font(i?-1:0).GetFontSize();item.ink=Tone(this,i?kError:kInk);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};items.push_back(std::move(item));}return items;
  }
 protected:
  void SetError(std::u16string error) {
    if(error==error_)return;error_=std::move(error);
    parts_[1]->GetViewAccessibility().SetName(error_);parts_[1]->SetVisible(!error_.empty());if(error_.empty()&&surfaces_[1])surfaces_[1]->Hide();
    if(!error_.empty()&&GetWidget())parts_[1]->GetViewAccessibility().NotifyEvent(ax::mojom::Event::kAlert,true);
    PreferredSizeChanged();SchedulePaint();if(GetWidget())if(auto* host=GetWidget()->widget_delegate()->AsBubbleDialogDelegate())host->SizeToContents();Schedule();
  }
 private:
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&NoticeBodyView::UpdateMetal,renderer_weak_factory_.GetWeakPtr()));}
  void UpdateMetal() {
    pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const auto g=Geometry(width());
    for(size_t i=0;i<2;++i){auto& surface=surfaces_[i];const bool was=surface&&surface->visible();
      const auto local=parts_[i]->GetVisibleBounds();if(!SceneIsVisible(parts_[i])||local.IsEmpty()){if(surface)surface->Hide();if(was)SchedulePaint();continue;}
      if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
      auto frame=parts_[i]->GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());
      const auto items=Items(i,g,-parts_[i]->x()-local.x(),-parts_[i]->y()-local.y(),gfx::RectF(gfx::Rect(frame.size())));
      surface->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=surface->visible())SchedulePaint();
    }
  }
  sidebar::NoticeCopy copy_;
  std::array<raw_ptr<views::View>,2> parts_{};std::array<std::unique_ptr<SidebarMetal>,2> surfaces_;std::u16string error_;bool pending_=false;
  mutable std::array<std::vector<std::string>,2> lines_;mutable int measured_width_=-1,measured_body_font_=0,measured_error_font_=0;mutable std::u16string measured_error_;
  SceneWindowObserver window_visibility_{base::BindRepeating(&NoticeBodyView::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<NoticeBodyView> renderer_weak_factory_{this};
};
BEGIN_METADATA(NoticeBodyView)
END_METADATA
class FolderNoticeView final : public NoticeBodyView {
  METADATA_HEADER(FolderNoticeView,NoticeBodyView)
 public:
  FolderNoticeView(const std::string& workspace,const std::string& folder,const std::string& title,bool error_only,base::RepeatingCallback<bool(FolderNoticeView*)> submit)
      :FolderNoticeView(sidebar::new_folder_notice(workspace,folder,title,error_only),std::move(submit)) {}
  base::WeakPtr<FolderNoticeView> weak(){return weak_factory_.GetWeakPtr();}
  bool Accept(){return submit_.Run(this);}
  void Cancel(){notice_->cancel();}
  sidebar::FolderNoticeRequest Prepare(const std::string& current,bool ready,bool busy,const std::vector<PinnedItem>& pins) {
    std::vector<sidebar::TreeNode> nodes;nodes.reserve(pins.size());for(const auto& pin:pins)nodes.push_back({pin.id,pin.parent_id,pin.folder});
    auto request=notice_->submit(current,ready,busy,rust::Slice<const sidebar::TreeNode>(nodes.data(),nodes.size()));SetError(base::UTF8ToUTF16(std::string(notice_->error())));return request;
  }
  void Finish(bool accepted){notice_->finish_attempt(accepted);SetError(base::UTF8ToUTF16(std::string(notice_->error())));}
 private:
  FolderNoticeView(rust::Box<sidebar::FolderNotice> notice,base::RepeatingCallback<bool(FolderNoticeView*)> submit)
      :NoticeBodyView(notice->copy()),notice_(std::move(notice)),submit_(std::move(submit)) {}
  rust::Box<sidebar::FolderNotice> notice_;base::RepeatingCallback<bool(FolderNoticeView*)> submit_;base::WeakPtrFactory<FolderNoticeView> weak_factory_{this};
};
BEGIN_METADATA(FolderNoticeView)
END_METADATA
FolderNoticeView* FindFolderNotice(views::View* view) {
  if(auto* notice=views::AsViewClass<FolderNoticeView>(view))return notice;
  for(const auto& child:view->children())if(auto* notice=FindFolderNotice(child.get()))return notice;return nullptr;
}

class PreferenceCopy final : public views::View {
  METADATA_HEADER(PreferenceCopy,views::View)
 public:
  PreferenceCopy(std::u16string text,SkColor tone,int delta,gfx::Font::Weight weight,bool multiline,base::RepeatingClosure changed,base::RepeatingCallback<bool()> visible,bool native_style=false,bool center=false,int fixed_width=0)
      :text_(std::move(text)),tone_(tone),delta_(delta),weight_(weight),multiline_(multiline),native_style_(native_style),center_(center),fixed_width_(fixed_width),changed_(std::move(changed)),visible_(std::move(visible)){GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);SetCanProcessEventsWithinSubtree(false);}
  const std::u16string& text() const{return text_;}
  gfx::FontList font() const{if(style_){const auto s=sidebar::design_text_style(style_);return DesignFont(s.family,s.size,s.weight,s.italic);}return native_style_?views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY):Font(delta_,weight_);}
  // An inner page title: `display-l`, Instrument Serif italic.
  void SetDisplay(){SetTextStyle(2);}
  // A design text style (design::TextStyleId) instead of the panel copy font.
  void SetTextStyle(uint32_t style){if(style_==style)return;style_=style;measured_width_=-1;natural_width_=-1;PreferredSizeChanged();SchedulePaint();Notify();}
  // A design role colour for the text (status tones), over the tone.
  void SetInk(std::optional<SkColor> ink){if(ink_==ink)return;ink_=ink;SchedulePaint();Notify();}
  SkColor Ink() const{if(ink_)return *ink_;return native_style_?GetColorProvider()->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_LABEL,GetEnabledInViewsSubtree()?views::style::STYLE_PRIMARY:views::style::STYLE_DISABLED)):Tone(this,GetEnabledInViewsSubtree()?tone_:kDisabled);}
  void SetText(std::u16string text){if(text==text_)return;text_=std::move(text);GetViewAccessibility().SetName(text_);measured_width_=-1;natural_width_=-1;PreferredSizeChanged();SchedulePaint();Notify();}
  void SetTone(SkColor tone){if(tone_==tone)return;tone_=tone;SchedulePaint();Notify();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{if(multiline_&&fixed_width_>0&&!text_.empty()){const auto g=Geometry(fixed_width_);return gfx::Size(fixed_width_,g.height);}const int natural=NaturalWidth();const int width=available.width().is_bounded()?std::min(natural,available.width().value()):natural;auto g=Geometry(width);return gfx::Size(width,g.height);}
  void Append(std::vector<sidebar::Item>& items,gfx::Point origin,gfx::Rect clip) const{auto g=Geometry(width());for(size_t i=0;i<lines_.size();++i){auto r=g.children[i];gfx::Rect box(origin.x()+r.x,origin.y()+r.y,r.width,r.height);auto visible=box;visible.Intersect(clip);if(visible.IsEmpty()||lines_[i].empty())continue;sidebar::Item item{};item.kind=17;item.x=box.x();item.y=box.y();item.width=box.width();item.height=box.height();item.title=lines_[i];item.ink=Ink();item.glyph=center_?1:0;item.font_size=font().GetFontSize();item.font_weight=static_cast<int>(native_style_?font().GetFontWeight():weight_);if(style_)item.font_weight=sidebar::design_text_style(style_).weight;item.active=item.font_weight>=static_cast<int>(gfx::Font::Weight::MEDIUM);item.text_style=style_;item.clip={float(visible.x()),float(visible.y()),float(visible.width()),float(visible.height())};items.push_back(std::move(item));}}
  bool HasFullText(const SidebarMetal& surface,std::string_view text) const{if(base::UTF16ToUTF8(text_)!=text||!SceneIsVisible(this)||GetVisibleBounds().height()!=height())return false;Geometry(width());return !lines_.empty()&&std::all_of(lines_.begin(),lines_.end(),[&](const auto& line){return line.empty()||surface.contains_text_for_testing(line);});}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Notify();}
  void AddedToWidget() override{views::View::AddedToWidget();Notify();}
  void RemovedFromWidget() override{Notify();views::View::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Notify();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);Notify();}
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_width_=-1;natural_width_=-1;PreferredSizeChanged();Notify();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Notify();}
  void OnPaint(gfx::Canvas* canvas) override{if(!visible_||!visible_.Run()){std::vector<sidebar::Item> items;Append(items,gfx::Point(),GetLocalBounds());for(const auto& item:items)PaintSceneRecipe(canvas,item,font());}}
 private:
  void Notify(){if(changed_)changed_.Run();}
  int NaturalWidth() const{const auto f=font();if(natural_width_>=0&&measured_font_==f.GetFontSize())return natural_width_;int width=0;size_t start=0;do{const auto end=text_.find(u'\n',start);width=std::max(width,gfx::GetStringWidth(text_.substr(start,end==std::u16string::npos?end:end-start),f));if(end==std::u16string::npos)break;start=end+1;}while(start<=text_.size());natural_width_=width;return width;}
  sidebar::ColumnLayout Geometry(int width) const{const auto f=font();width=std::max(0,width);if(width!=measured_width_||f.GetFontSize()!=measured_font_){lines_.clear();if(multiline_&&width>0){
      if(!wrapped_)wrapped_=gfx::RenderText::CreateRenderText();
      wrapped_->SetCursorEnabled(false);wrapped_->SetFontList(f);wrapped_->SetText(text_);wrapped_->SetMultiline(true);wrapped_->SetHorizontalAlignment(gfx::ALIGN_LEFT);wrapped_->SetElideBehavior(gfx::NO_ELIDE);wrapped_->SetWordWrapBehavior(gfx::TRUNCATE_LONG_WORDS);wrapped_->SetMinLineHeight(std::max(f.GetHeight(),views::TypographyProvider::Get().GetLineHeight(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY)));wrapped_->SetDisplayRect(gfx::Rect(0,0,width,0));
      for(size_t i=0;i<wrapped_->GetNumLines();++i){const auto start=std::min(text_.size(),wrapped_->GetTextIndexOfLine(i));const auto end=std::max(start,std::min(text_.size(),wrapped_->GetTextIndexOfLine(i+1)));auto line=text_.substr(start,end-start);while(!line.empty()&&(line.back()==u'\n'||line.back()==u'\r'))line.pop_back();lines_.push_back(base::UTF16ToUTF8(line));}
    }else lines_.push_back(base::UTF16ToUTF8(text_));if(lines_.empty())lines_.push_back("");measured_width_=width;measured_font_=f.GetFontSize();}return sidebar::paragraph_layout(width,std::max(f.GetHeight(),views::TypographyProvider::Get().GetLineHeight(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY)),lines_.size());}
  std::u16string text_;SkColor tone_;int delta_;gfx::Font::Weight weight_;bool multiline_;bool native_style_,center_;uint32_t style_=0;std::optional<SkColor> ink_;int fixed_width_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;mutable int measured_width_=-1,measured_font_=0,natural_width_=-1;mutable std::vector<std::string> lines_;mutable std::unique_ptr<gfx::RenderText> wrapped_;
};
BEGIN_METADATA(PreferenceCopy)
END_METADATA
void SetPanelText(views::View* view,std::u16string text);
// A composite adapter retains its existing text and inner action-group scopes.
// The outer row lease belongs to the Rust site-rule list, never a rule backend.
class SiteRuleSceneRow final : public views::View {
  METADATA_HEADER(SiteRuleSceneRow,views::View)
 public:
  explicit SiteRuleSceneRow(rust::Box<sidebar::ActionControl> input):input_(std::move(input)){}
  uint64_t NodeId() const{return input_->action_node_id();}
  uint64_t ParentId() const{return input_->node_parent_id();}
  uint32_t ModelKind() const{return input_->node_model_kind();}
  bool BelongsTo(const SidebarNodeGraphState& scope) const{return scope.graph->owns_input(*input_);}
  base::WeakPtr<SiteRuleSceneRow> GetWeakPtr(){return weak_factory_.GetWeakPtr();}
  void SetParts(views::View* copy,views::View* remove){copy_=copy;remove_=remove;}
  void Project(const sidebar::SiteRuleSceneChild& child){
    CHECK(copy_&&remove_);if(child.cancel_input)remove_->OnMouseCaptureLost();
    const std::u16string detail(child.detail.begin(),child.detail.end());SetPanelText(copy_,detail);remove_->SetEnabled(child.editable);remove_->GetViewAccessibility().SetName(TF(u"Usuń wyjątek: {}",{detail}));remove_->SchedulePaint();
  }
 private:
  rust::Box<sidebar::ActionControl> input_;
  raw_ptr<views::View> copy_=nullptr;raw_ptr<views::View> remove_=nullptr;
  base::WeakPtrFactory<SiteRuleSceneRow> weak_factory_{this};
};
BEGIN_METADATA(SiteRuleSceneRow)
END_METADATA

class SceneProgress final : public views::View {
  METADATA_HEADER(SceneProgress,views::View)
 public:
  SceneProgress(base::RepeatingClosure changed,base::RepeatingCallback<bool()> visible):changed_(std::move(changed)),visible_(std::move(visible)){GetViewAccessibility().SetRole(ax::mojom::Role::kProgressIndicator);SetCanProcessEventsWithinSubtree(false);}
  // Downloads progress: a 3 px `accent` bar on `bg-hover` (recipe::progress).
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(1,3);}
  void SetValue(double value){const double next=!std::isfinite(value)||value<0||value>1?-1:value;if(next==value_)return;value_=next;if(value_<0)started_=base::TimeTicks::Now();AX();SchedulePaint();changed_.Run();}
  bool moving() const{return value_<0;}
  // A paused transfer keeps its bar in `warn`.
  void SetPaused(bool paused){if(paused_==paused)return;paused_=paused;SchedulePaint();changed_.Run();}
  sidebar::Item Scene(gfx::Rect box,gfx::Rect clip) const{sidebar::Item item{};item.kind=26;item.x=box.x();item.y=box.y();item.width=box.width();item.height=box.height();item.title_width=value_;item.detail_font_size=started_.is_null()?0:std::fmod((base::TimeTicks::Now()-started_).InSecondsF(),2.0)/2.0;item.tile=base::i18n::IsRTL();item.ink=paused_?Palette(this).warn:Palette(this).accent;item.fill=Palette(this).bg_hover;item.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};return item;}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{changed_.Run();}
  void OnBoundsChanged(const gfx::Rect& old) override{views::View::OnBoundsChanged(old);changed_.Run();}
  void AddedToWidget() override{views::View::AddedToWidget();AX();changed_.Run();}
  void VisibilityChanged(views::View* view,bool visible) override{views::View::VisibilityChanged(view,visible);AX();changed_.Run();}
  void OnThemeChanged() override{views::View::OnThemeChanged();changed_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);changed_.Run();}
  void OnPaint(gfx::Canvas* canvas) override{if(!visible_.Run())PaintSceneRecipe(canvas,Scene(GetLocalBounds(),GetLocalBounds()),Font(0));}
 private:
  void AX(){if(value_<0){GetViewAccessibility().RemoveValue();return;}if(!GetWidget()||!GetWidget()->IsVisible())return;const int percent=static_cast<int>(value_*100);if(percent!=announced_){announced_=percent;GetViewAccessibility().SetValue(base::FormatPercent(percent));}}
  double value_=0;bool paused_=false;int announced_=-1;base::TimeTicks started_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;
};
BEGIN_METADATA(SceneProgress)
END_METADATA
class PreferenceChromeRenderer : public views::WidgetObserver {
 public:
  explicit PreferenceChromeRenderer(views::View* root):root_(root){}
  ~PreferenceChromeRenderer() override{timer_.Stop();observation_.Reset();}
  bool visible() const{return surface_&&surface_->visible();}
  bool order_correct() const{return visible()&&surface_->underlay_order_correct_for_testing();}
  bool animating() const{return timer_.IsRunning();}
  size_t frame_count() const{return surface_?surface_->frame_count():0;}
  std::string Debug() const{return "root="+root_->GetBoundsInScreen().ToString()+" visible="+root_->GetVisibleBounds().ToString()+" widget="+std::to_string(root_->GetWidget()!=nullptr)+" drawn="+std::to_string(SceneIsVisible(root_))+" canvas="+std::to_string(visible())+" order="+std::to_string(order_correct())+" ops="+std::to_string(last_ops_.size())+" frames="+std::to_string(frame_count());}
  bool contains_text(std::string_view text) const{if(!visible())return false;if(surface_->contains_text_for_testing(text))return true;auto find=[&](auto&& self,views::View* view)->bool{if(auto* copy=views::AsViewClass<PreferenceCopy>(view))if(copy->HasFullText(*surface_,text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,root_);}
  bool excludes(views::View* view) const{if(!visible()||!view)return false;auto rect=Projected(view);if(rect.IsEmpty())return false;for(const auto& op:last_ops_){gfx::RectF clip(op.clip.x,op.clip.y,op.clip.width,op.clip.height);if(clip.Intersects(gfx::RectF(rect)))return false;}return true;}
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&PreferenceChromeRenderer::Update,weak_factory_.GetWeakPtr()));}
  void OnWidgetVisibilityChanged(views::Widget*,bool visible) override{if(!visible){auto cancel=[&](auto&& self,views::View* view)->void{if(auto* row=views::AsViewClass<RustRowView>(view))row->CancelInput();if(auto* group=views::AsViewClass<SceneActionGroup>(view);group&&group->shared())group->CancelInput();for(const auto& child:view->children())self(self,child.get());};cancel(cancel,root_);timer_.Stop();if(surface_)surface_->Hide();}Schedule();}
  void OnWidgetBoundsChanged(views::Widget*,const gfx::Rect&) override{Schedule();}
  void OnWidgetDestroying(views::Widget*) override{timer_.Stop();observation_.Reset();if(surface_)surface_->Hide();}
 private:
  gfx::Rect Projected(views::View* view) const{auto rect=view->GetVisibleBounds();rect.Offset(view->GetBoundsInScreen().x()-root_->GetBoundsInScreen().x(),view->GetBoundsInScreen().y()-root_->GetBoundsInScreen().y());rect.Intersect(root_->GetLocalBounds());return rect;}
  void Collect(views::View* view,std::vector<sidebar::Item>& backgrounds,std::vector<sidebar::Item>& text,std::vector<sidebar::FloatRect>& holes){if(!SceneIsVisible(view))return;auto clip=Projected(view);if(clip.IsEmpty())return;auto box=view->GetBoundsInScreen();box.Offset(-root_->GetBoundsInScreen().x(),-root_->GetBoundsInScreen().y());if(auto* progress=views::AsViewClass<SceneProgress>(view)){text.push_back(progress->Scene(box,clip));moving_|=progress->moving();return;}if(auto* actions=views::AsViewClass<SceneActionGroup>(view);actions&&actions->shared()){actions->Append(text,box.origin(),clip);return;}if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->Append(text,box.origin(),clip);return;}if(auto* row=views::AsViewClass<RustRowView>(view);row&&row->history()){text.push_back(row->Scene(gfx::RectF(box),gfx::RectF(clip)));return;}if(auto* surface=views::AsViewClass<PreferenceSurface>(view))backgrounds.push_back(surface->Scene(gfx::RectF(box),gfx::RectF(clip)));if(views::AsViewClass<SceneActionGroup>(view)||views::AsViewClass<SceneCheckboxGroup>(view)||views::AsViewClass<ScenePreferenceControls>(view)||views::AsViewClass<SceneSelect>(view)||views::AsViewClass<SceneToggle>(view)||views::AsViewClass<views::Button>(view)||view->GetViewAccessibility().GetCachedRole()==ax::mojom::Role::kTextField){holes.push_back({float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())});return;}for(const auto& child:view->children())Collect(child.get(),backgrounds,text,holes);}
  void Update(){pending_=false;auto* widget=root_->GetWidget();if(!widget)return;if(observation_.GetSource()!=widget){observation_.Reset();observation_.Observe(widget);}widget->LayoutRootViewIfNecessary();const bool was=visible();if(!SceneIsVisible(root_)){timer_.Stop();if(surface_)surface_->Hide();return;}moving_=false;std::vector<sidebar::Item> backgrounds,text;std::vector<sidebar::FloatRect> holes;Collect(root_,backgrounds,text,holes);backgrounds.insert(backgrounds.end(),std::make_move_iterator(text.begin()),std::make_move_iterator(text.end()));last_ops_=sidebar::preference_chrome_scene(rust::Slice<const sidebar::Item>(backgrounds.data(),backgrounds.size()),rust::Slice<const sidebar::FloatRect>(holes.data(),holes.size()));if(!surface_)surface_=CreateSidebarMetal();if(surface_)surface_->UpdateUnderlay(widget->GetNativeWindow(),root_->GetBoundsInScreen(),UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(last_ops_.data(),last_ops_.size())));if(was!=visible())root_->SchedulePaint();if(moving_){if(!timer_.IsRunning())timer_.Start(FROM_HERE,base::Milliseconds(16),base::BindRepeating(&PreferenceChromeRenderer::Tick,base::Unretained(this)));}else timer_.Stop();}
  void Tick(){if(!visible())root_->SchedulePaint();Schedule();}
  base::RepeatingTimer timer_;bool moving_=false;
  raw_ptr<views::View> root_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;rust::Vec<sidebar::Primitive> last_ops_;base::ScopedObservation<views::Widget,views::WidgetObserver> observation_{this};base::WeakPtrFactory<PreferenceChromeRenderer> weak_factory_{this};
};

// A file's extension on a 32 px `bg-hover` tile, `mono` 9 px 600 in capitals
// (design: Downloads).
class SceneFileTile final : public views::View {
  METADATA_HEADER(SceneFileTile,views::View)
 public:
  SceneFileTile(){SetCanProcessEventsWithinSubtree(false);GetViewAccessibility().SetIsIgnored(true);}
  void SetExtension(std::u16string extension){extension=base::i18n::ToUpper(extension.substr(0,4));if(extension_==extension)return;extension_=std::move(extension);SchedulePaint();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(32,32);}
  void OnPaint(gfx::Canvas* canvas) override{const auto p=Palette(this);FillRound(canvas,GetLocalBounds(),4/* radius-xs */,p.bg_hover);
    canvas->DrawStringRectWithFlags(extension_,DesignFont(3,9,600,false),p.ink_muted,GetLocalBounds(),gfx::Canvas::TEXT_ALIGN_CENTER);}
 private:
  std::u16string extension_;
};
BEGIN_METADATA(SceneFileTile)
END_METADATA

class PanelSceneImpl final : public PanelScene {
 public:
  explicit PanelSceneImpl(views::View* root):root_(root),renderer_(root){}
  std::unique_ptr<views::View> MakeText(std::u16string text,bool multiline,bool center,int fixed_width) override{
    return std::make_unique<PreferenceCopy>(std::move(text),kInk,0,gfx::Font::Weight::NORMAL,multiline,base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()),true,center,fixed_width);
  }
  std::unique_ptr<views::View> MakeSelect(views::View* parent,std::vector<std::u16string> labels,std::u16string name) override{return MakeParentSceneControl<SceneSelect>(parent,std::move(labels),std::move(name),base::RepeatingClosure());}
  void SetText(views::View* view,std::u16string text) override{static_cast<PreferenceCopy*>(view)->SetText(std::move(text));}
  std::unique_ptr<views::View> MakeActions(int spacing) override{auto group=std::make_unique<SceneActionGroup>(false,spacing);group->Share(base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()),base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()));return group;}
  views::View* AddAction(views::View* group,base::RepeatingClosure callback,std::u16string text) override{return static_cast<SceneActionGroup*>(group)->AddAction(std::move(callback),std::move(text),ui::ButtonStyle::kDefault);}
  void SetActionText(views::View* button,std::u16string text) override{static_cast<SceneTextButton*>(button)->SetText(std::move(text));}
  std::unique_ptr<views::View> MakeProgress() override{return std::make_unique<SceneProgress>(base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()));}
  void SetProgress(views::View* view,double value) override{static_cast<SceneProgress*>(view)->SetValue(value);}
  void SetTextStyle(views::View* text,uint32_t style,Tone tone) override{auto* copy=static_cast<PreferenceCopy*>(text);copy->SetTextStyle(style);
    const auto p=Palette(root_);copy->SetInk(tone==Tone::kOk?std::optional<SkColor>(p.ok):tone==Tone::kWarn?std::optional<SkColor>(p.warn):tone==Tone::kDanger?std::optional<SkColor>(p.danger):tone==Tone::kMuted?std::optional<SkColor>(p.ink_muted):std::nullopt);}
  std::unique_ptr<views::View> MakeFileTile() override{return std::make_unique<SceneFileTile>();}
  void SetFileTile(views::View* tile,std::u16string extension) override{static_cast<SceneFileTile*>(tile)->SetExtension(std::move(extension));}
  views::View* AddIconAction(views::View* parent,base::RepeatingClosure callback,std::u16string name,const gfx::VectorIcon* icon) override{
    return parent->AddChildView(CreateSceneAction(base::BindRepeating(callback),std::move(name),ControlGlyph::kVector,icon,{},base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr())));}
  void SetIconAction(views::View* button,std::u16string name,const gfx::VectorIcon* icon) override{if(auto* action=views::AsViewClass<RustActionView>(button))action->SetIcon(icon);button->SetTooltipText(name);button->GetViewAccessibility().SetName(name);}
  void SetProgressPaused(views::View* progress,bool paused) override{static_cast<SceneProgress*>(progress)->SetPaused(paused);}
  bool animating() const override{return renderer_.animating();}
  void Schedule() override{renderer_.Schedule();}
  base::RepeatingClosure ViewportChangedCallback() override{return base::BindRepeating(&PanelSceneImpl::ViewportChanged,weak_factory_.GetWeakPtr());}
  bool visible() const override{return renderer_.visible();}
  bool contains_text(std::string_view text) const override{return renderer_.contains_text(text);}
  bool excludes(views::View* view) const override{return renderer_.excludes(view);}
  bool order_correct() const override{return renderer_.order_correct();}
 private:
  void ViewportChanged(){RefreshChoiceViewports(root_);renderer_.Schedule();}
  raw_ptr<views::View> root_;
  PreferenceChromeRenderer renderer_;
  base::WeakPtrFactory<PanelSceneImpl> weak_factory_{this};
};

// Reusable static paragraph/heading adapter: no native Label or hidden text
// measurement. CoreText shapes lines; Rust places them; Metal presents them.
class SceneParagraph final : public views::View {
  METADATA_HEADER(SceneParagraph,views::View)
 public:
  void SyncWindowVisibility(){if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  SceneParagraph(std::u16string text,int delta,bool bold,SkColor ink):text_(std::move(text)),delta_(delta),bold_(bold),ink_(ink) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);
  }
  void SetText(std::u16string text){if(text==text_)return;text_=std::move(text);GetViewAccessibility().SetName(text_);measured_width_=-1;PreferredSizeChanged();SchedulePaint();Schedule();}
  // A design system text style (design.rs TextStyleId) instead of the delta/bold font.
  void SetTextStyle(uint32_t style){style_=style;measured_width_=-1;PreferredSizeChanged();SchedulePaint();Schedule();}
  bool contains_scene_text(std::string_view text) const{
    if(!surface_||!surface_->visible())return false;if(surface_->contains_text_for_testing(text))return true;
    return !lines_.empty()&&base::UTF16ToUTF8(text_)==text&&std::all_of(lines_.begin(),lines_.end(),[&](const auto& line){return surface_->contains_text_for_testing(line);});
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{const auto g=Geometry(available.width().is_bounded()?available.width().value():200);return gfx::Size(g.width,g.height);}
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_width_=-1;PreferredSizeChanged();Schedule();}
  void OnVisibleBoundsChanged() override{Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Schedule();}
  void AddedToWidget() override{views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void VisibilityChanged(views::View* view,bool visible) override{views::View::VisibilityChanged(view,visible);if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {
    if(!surface_||!surface_->visible()){const auto items=Items(0,0,gfx::RectF(GetLocalBounds()));for(const auto& item:items)PaintSceneRecipe(canvas,item,Font(delta_,bold_?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL));}Schedule();
  }
 private:
  sidebar::ColumnLayout Geometry(int requested) const {
    const int width=std::max(0,requested);
    if(style_){
      const auto s=sidebar::design_text_style(style_);const auto text=base::UTF16ToUTF8(s.caps?base::i18n::ToUpper(text_):text_);
      if(width!=measured_width_||measured_font_!=-static_cast<int>(style_)){lines_=ShapeSidebarParagraphWeight(text,s.size,width,s.weight);measured_width_=width;measured_font_=-static_cast<int>(style_);}
      return sidebar::paragraph_layout(width,static_cast<int>(std::lround(s.line_height)),lines_.size());
    }
    const auto font=Font(delta_,bold_?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL);
    if(width!=measured_width_||font.GetFontSize()!=measured_font_){lines_=ShapeSidebarParagraph(base::UTF16ToUTF8(text_),font.GetFontSize(),width,bold_);measured_width_=width;measured_font_=font.GetFontSize();}
    return sidebar::paragraph_layout(width,font.GetHeight(),lines_.size());
  }
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {
    const auto g=Geometry(width());std::vector<sidebar::Item> items;items.reserve(g.children.size());
    for(size_t i=0;i<g.children.size();++i){const auto& r=g.children[i];sidebar::Item item{};item.kind=17;item.x=r.x+dx;item.y=r.y+dy;item.width=r.width;item.height=r.height;item.title=lines_[i];item.font_size=Font(delta_).GetFontSize();item.active=bold_;item.text_style=style_;item.ink=Tone(this,ink_);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};items.push_back(std::move(item));}return items;
  }
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneParagraph::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=surface_&&surface_->visible();const auto local=GetVisibleBounds();
    if(!SceneIsVisible(this)||local.IsEmpty()||text_.empty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}
    if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;
    auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));
    surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=surface_->visible())SchedulePaint();
  }
  std::u16string text_;int delta_;bool bold_;SkColor ink_;uint32_t style_=0;mutable int measured_width_=-1,measured_font_=0;mutable std::vector<std::string> lines_;
  std::unique_ptr<SidebarMetal> surface_;bool pending_=false;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneParagraph::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneParagraph> weak_factory_{this};
};
BEGIN_METADATA(SceneParagraph)
END_METADATA

// The design system's panel kit: text in the tokens' styles and sections laid
// out with the spacing tokens (docs/design-system).
const sidebar::DesignMetrics& Metrics(){static const sidebar::DesignMetrics metrics=sidebar::design_metrics();return metrics;}
enum TextStyle : uint32_t { kDisplayLarge=2, kTitleText=3, kBodyStrong=4, kBodyText=5, kLabelText=6, kEyebrow=7 };
SceneParagraph* PanelText(views::View* parent,std::u16string text,TextStyle style,SkColor role){
  auto* paragraph=parent->AddChildView(std::make_unique<SceneParagraph>(std::move(text),0,false,role));paragraph->SetTextStyle(style);return paragraph;
}
// A vertical stack, `gap` px between children.
views::View* PanelStack(views::View* parent,int gap){
  auto* stack=parent->AddChildView(std::make_unique<views::View>());
  stack->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,gfx::Insets(),gap))->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  return stack;
}
// A section: an uppercase label over its content, space-2 apart.
views::View* PanelSection(views::View* parent,std::u16string label){
  auto* section=PanelStack(parent,Metrics().space_2);PanelText(section,std::move(label),kEyebrow,kMuted);return section;
}
// Equal columns, space-2 apart; each child added to it takes one.
views::View* PanelColumns(views::View* parent){
  auto* row=parent->AddChildView(std::make_unique<views::View>());
  auto* layout=row->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),Metrics().space_2));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStart);layout->SetDefaultFlex(1);return row;
}
// The space's icon on its accent_soft tile (48 px, radius-md + 2), as the panel header shows it.
class SpaceIconTile final : public views::View {
  METADATA_HEADER(SpaceIconTile,views::View)
 public:
  // `icon` gives the emoji, or the name's initial (`initial`) when there is none.
  explicit SpaceIconTile(base::RepeatingCallback<std::pair<std::u16string,bool>()> icon):icon_(std::move(icon)){GetViewAccessibility().SetIsIgnored(true);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(48,48);}
  void OnPaint(gfx::Canvas* canvas) override{
    const auto p=Palette(this);const int radius=Metrics().radius_md+2;FillRound(canvas,GetLocalBounds(),radius,p.accent_soft);
    cc::PaintFlags line;line.setAntiAlias(true);line.setStyle(cc::PaintFlags::kStroke_Style);line.setStrokeWidth(1);line.setColor(p.line);
    gfx::RectF edge(GetLocalBounds());edge.Inset(0.5f);canvas->DrawRoundRect(edge,radius-0.5f,line);
    const auto [text,initial]=icon_.Run();if(text.empty())return;
    canvas->DrawStringRectWithFlags(text,initial?DesignFont(1,30,400,true):Font(10),initial?p.accent_ink:p.ink,GetLocalBounds(),gfx::Canvas::TEXT_ALIGN_CENTER);
  }
 private:
  base::RepeatingCallback<std::pair<std::u16string,bool>()> icon_;
};
BEGIN_METADATA(SpaceIconTile)
END_METADATA
void SetPanelText(views::View* view,std::u16string text) {
  if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->SetText(std::move(text));return;}
  auto* scene=views::AsViewClass<SceneParagraph>(view);scene->SetText(std::move(text));
}
sidebar::ImportSelection ImportUiSelection(const BrowserImportOptions& o){return {static_cast<uint32_t>(o.browser),o.separate_profiles,o.bookmarks,o.history,o.cookies};}
BrowserImportOptions ImportServiceOptions(const sidebar::ImportSelection& s){BrowserImportOptions o;o.browser=static_cast<ImportBrowser>(s.browser);o.separate_profiles=s.separate_profiles;o.bookmarks=s.bookmarks;o.history=s.history;o.cookies=s.cookies;return o;}
std::vector<sidebar::ProfileOption> ProfileOptions(const ShellState& state){std::vector<sidebar::ProfileOption> out;out.reserve(state.profiles.size());for(const auto& p:state.profiles)out.push_back({p.id,p.name});return out;}
std::vector<sidebar::ProfileUse> ProfileUses(const ShellState& state){std::vector<sidebar::ProfileUse> out;out.reserve(state.workspaces.size());for(const auto& w:state.workspaces)out.push_back({w.id,w.name,w.profile_id});return out;}

class EdgeView final : public views::View {
  METADATA_HEADER(EdgeView, views::View)
 public:
  explicit EdgeView(base::RepeatingClosure show) : show_(std::move(show)) {}
  void OnMouseEntered(const ui::MouseEvent&) override { show_.Run(); }
  void OnPaint(gfx::Canvas* canvas) override {
    FillRound(canvas, gfx::Rect(2, std::max(0, height() / 2 - 18), 3, 36), 2, color);
  }
  SkColor color = kDefaultWorkspaceColor;
 private:
  base::RepeatingClosure show_;
};
BEGIN_METADATA(EdgeView)
END_METADATA

// Right-edge handle that resizes the sidebar; it lights up while hovered.
// Controls own their behavior; Views supplies event/window/accessibility delivery.
class ResizeHandle {
 public:
  virtual ~ResizeHandle()=default;
  virtual views::View* view()=0;
  virtual bool hovered() const=0;
  virtual bool active() const{return false;}
  virtual void SetVertical(bool){}
  virtual void CancelDrag(){}
};

class RustResizeHandle final : public views::View,public ResizeHandle {
  METADATA_HEADER(RustResizeHandle,views::View)
 public:
  RustResizeHandle(base::RepeatingCallback<int()> width,base::RepeatingCallback<void(int,bool)> applied,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      :width_(std::move(width)),applied_(std::move(applied)),painted_(std::move(painted)),visible_(std::move(visible)){width_drag_.emplace(sidebar::new_sidebar_drag());Init();GetViewAccessibility().SetName(T(u"Zmień szerokość panelu bocznego"));SetProperty(views::kViewIgnoredByLayoutKey,true);}
  RustResizeHandle(base::RepeatingCallback<double()> ratio,base::RepeatingCallback<int()> extent,base::RepeatingCallback<void(double,bool)> changed,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      :ratio_(std::move(ratio)),extent_(std::move(extent)),changed_(std::move(changed)),painted_(std::move(painted)),visible_(std::move(visible)){split_drag_.emplace(sidebar::new_split_drag());Init();}
  views::View* view() override{return this;}
  bool hovered() const override{return hovered_;}
  bool active() const override{return width_drag_?(*width_drag_)->active():(*split_drag_)->active();}
  void SetVertical(bool vertical) override{vertical_=vertical;}
  void CancelDrag() override{if(width_drag_)(*width_drag_)->cancel();else (*split_drag_)->cancel();SchedulePaint();}
  ui::Cursor GetCursor(const ui::MouseEvent&) override{return GetEnabled()?ui::Cursor(vertical_?ui::mojom::CursorType::kNorthSouthResize:ui::mojom::CursorType::kEastWestResize):ui::Cursor();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void OnMouseEntered(const ui::MouseEvent&) override{hovered_=true;SchedulePaint();}
  void OnMouseExited(const ui::MouseEvent&) override{hovered_=false;SchedulePaint();}
  bool OnMousePressed(const ui::MouseEvent& event) override{if(!event.IsOnlyLeftMouseButton()||!GetEnabledInViewsSubtree())return false;Begin(event.location());return true;}
  bool OnMouseDragged(const ui::MouseEvent& event) override{if(!event.IsLeftMouseButton()||!GetEnabledInViewsSubtree())return false;Update(event.location());return true;}
  void OnMouseReleased(const ui::MouseEvent& event) override{if(GetEnabledInViewsSubtree()&&(width_drag_||(*split_drag_)->moved()))Update(event.location());End();}
  void OnMouseCaptureLost() override{End();}
  void VisibilityChanged(views::View* starting,bool visible) override{views::View::VisibilityChanged(starting,visible);if(!visible)CancelDrag();}
  void OnGestureEvent(ui::GestureEvent* event) override{
    if(!GetEnabledInViewsSubtree())return;
    if(event->type()==ui::EventType::kGestureTapDown||event->type()==ui::EventType::kGestureScrollBegin){if(!active())Begin(event->location());}
    else if(event->type()==ui::EventType::kGestureScrollUpdate){const double delta=vertical_?event->details().scroll_y():event->details().scroll_x();if(width_drag_)Apply((*width_drag_)->delta(delta,Reverse()),false);else Apply((*split_drag_)->delta(delta,extent_.Run(),Reverse()),false);}
    else if(event->type()==ui::EventType::kGestureEnd)End();
    else if(event->type()==ui::EventType::kGestureTapCancel)CancelDrag();
    else return;
    event->SetHandled();
  }
  void OnPaint(gfx::Canvas* canvas) override{
    if(visible_&&visible_.Run())return;
    if(width_drag_&&!hovered_&&!active())return;
    if(split_drag_)canvas->FillRect(GetLocalBounds(),Tone(this,kChip));
    sidebar::Item item{};item.kind=width_drag_?11:9;item.width=width();item.height=height();item.active=active();item.hovered=hovered_;item.ink=width_drag_?SkColorSetA(Tone(this,kInk),active()?0x40:0x26):Tone(this,hovered_?kSubtle:kDisabled);item.clip={0,0,float(width()),float(height())};const auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));for(const auto& op:ops){cc::PaintFlags flags;flags.setAntiAlias(true);flags.setColor(op.color);canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);}
  }
 private:
  void Init(){GetViewAccessibility().SetRole(ax::mojom::Role::kSplitter);}
  double Pointer(gfx::Point point){views::View::ConvertPointToScreen(this,&point);return vertical_?point.y():point.x();}
  bool Reverse() const{return !vertical_&&base::i18n::IsRTL();}
  void Begin(gfx::Point point){if(width_drag_)(*width_drag_)->begin(width_.Run(),Pointer(point));else (*split_drag_)->begin(ratio_.Run(),Pointer(point));SchedulePaint();}
  void Update(gfx::Point point){if(width_drag_)Apply((*width_drag_)->update(Pointer(point),Reverse()),false);else Apply((*split_drag_)->update(Pointer(point),extent_.Run(),Reverse()),false);}
  void End(){if(width_drag_)Apply((*width_drag_)->end(),true);else Apply((*split_drag_)->end(),true);SchedulePaint();}
  void Apply(const sidebar::WidthChange& change,bool done){if(change.changed)applied_.Run(change.width,done);}
  void Apply(const sidebar::DragRatio& change,bool done){if(change.changed)changed_.Run(change.ratio,done);}
  std::optional<rust::Box<sidebar::SidebarDrag>> width_drag_;
  std::optional<rust::Box<sidebar::SplitDrag>> split_drag_;
  bool vertical_=false,hovered_=false;
  base::RepeatingCallback<int()> width_;
  base::RepeatingCallback<void(int,bool)> applied_;
  base::RepeatingCallback<double()> ratio_;
  base::RepeatingCallback<int()> extent_;
  base::RepeatingCallback<void(double,bool)> changed_;
  base::RepeatingClosure painted_;
  base::RepeatingCallback<bool()> visible_;
};
BEGIN_METADATA(RustResizeHandle)
END_METADATA
std::unique_ptr<views::View> CreateSidebarGrip(ResizeHandle** handle,base::RepeatingCallback<void(int,bool)> resized,base::RepeatingCallback<int()> width,base::RepeatingCallback<void(int,bool)> applied,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible){
  auto view=std::make_unique<RustResizeHandle>(std::move(width),std::move(applied),std::move(painted),std::move(visible));*handle=view.get();return view;
}

// A ScrollView may paint on a separate layer from the outer sidebar. Forward
// invalidations before rasterization: Metal must update even if the compositor
// drops an empty native paint record. No polling timer or idle redraw.
class SidebarListView final : public views::View {
 public:
  SidebarListView(base::RepeatingClosure painted, base::RepeatingCallback<bool()> metal_visible)
      : painted_(std::move(painted)), metal_visible_(std::move(metal_visible)) {}
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    painted_.Run();
  }
  void OnDidSchedulePaint(const gfx::Rect& rect) override {
    views::View::OnDidSchedulePaint(rect);
    painted_.Run();
  }
  void PaintChildren(const views::PaintInfo& info) override {
    if (metal_visible_.Run()) { ++metal_paints_; return; }
    ++native_paints_;
    views::View::PaintChildren(info);
  }
  size_t native_paints() const { return native_paints_; }
  size_t metal_paints() const { return metal_paints_; }
 private:
  base::RepeatingClosure painted_;
  base::RepeatingCallback<bool()> metal_visible_;
  size_t native_paints_ = 0;
  size_t metal_paints_ = 0;
};

class SidebarView final : public views::View {
  METADATA_HEADER(SidebarView, views::View)
 public:
  explicit SidebarView(base::RepeatingCallback<void(bool,bool)> leave) : leave_(std::move(leave)) {
    SetNotifyEnterExitOnChild(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    leave_.Run(GetBoundsInScreen().Contains(display::Screen::Get()->GetCursorScreenPoint()),event.IsLeftMouseButton());
  }
  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (grip) {
      const auto r=sidebar::sidebar_grip_layout(width(),height());grip->SetBounds(r.x,r.y,r.width,r.height);
    }
  }
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    if (painted) painted.Run();
  }
  void OnDidSchedulePaint(const gfx::Rect& rect) override {
    views::View::OnDidSchedulePaint(rect);
    if (painted) painted.Run();
  }
  base::RepeatingClosure painted;
  raw_ptr<views::View> grip = nullptr;
  raw_ptr<ResizeHandle> grip_handle = nullptr;
 private:
  base::RepeatingCallback<void(bool,bool)> leave_;
};
BEGIN_METADATA(SidebarView)
END_METADATA

// Only viewport-sized paint records travel under the fingers. Native rows,
// WebContents and profile selection stay untouched until the gesture ends.
class SwipePage final : public ui::LayerDelegate {
 public:
  SwipePage(cc::PaintRecord record, gfx::Size size) : record_(std::move(record)), layer_(ui::Layer::Create(ui::LAYER_TEXTURED)) {
    layer_->set_delegate(this); layer_->SetFillsBoundsOpaquely(false);
    layer_->SetBounds(gfx::Rect(size));
    layer_->SchedulePaint(gfx::Rect(size));
  }
  ~SwipePage() override { layer_->set_delegate(nullptr); }
  ui::Layer* layer() { return layer_.get(); }
  void OnPaintLayer(const ui::PaintContext& context) override {
    ui::PaintRecorder recorder(context, layer_->size());
    recorder.canvas()->sk_canvas()->drawPicture(record_);
  }
  void OnDeviceScaleFactorChanged(float, float) override { layer_->SchedulePaint(gfx::Rect(layer_->size())); }
 private:
  // Compositor-only decoration: no View/AX location notifications per delta.
  cc::PaintRecord record_;
  std::unique_ptr<ui::Layer> layer_;
};
class WorkspaceSwipePreview final : public views::View {
  METADATA_HEADER(WorkspaceSwipePreview, views::View)
 public:
  explicit WorkspaceSwipePreview(base::RepeatingCallback<cc::PaintRecord(int)> capture)
      : capture_(std::move(capture)) {
    GetViewAccessibility().SetIsIgnored(true);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer(); layer()->SetMasksToBounds(true);
    layer()->SetFillsBoundsOpaquely(false);
  }
  void SetSceneCapture(base::RepeatingCallback<std::vector<sidebar::Item>(int)> capture,unsigned background) {
    scene_capture_=std::move(capture);background_=background;
  }
  void Move(double position, int count, bool settle) {
    const auto plan=SidebarSwipeLayout(position,count,width());
    const int center=plan.center;
    center_ = center;
    if(scene_capture_&&GetWidget()) {
      if(scene_size_!=size()){scene_size_=size();scenes_.clear();surfaces_.clear();records_.clear();pages_.clear();}
      for(auto it=surfaces_.begin();it!=surfaces_.end();) {
        if(std::abs(it->first-center)<=1){++it;continue;}it=surfaces_.erase(it);
      }
      bool ready=!plan.pages.empty();
      for(const auto& placement:plan.pages) {
        const int i=placement.index;
        const bool captured=!scenes_.contains(i);
        if(captured)scenes_[i]=scene_capture_.Run(i);
        auto& surface=surfaces_[i];if(!surface)surface=CreateSidebarMetal();
        if(!surface){ready=false;continue;}
        surface->UpdatePreview(GetWidget()->GetNativeWindow(),GetBoundsInScreen(),placement.x,scenes_[i],background_,settle,captured);
        ready=ready&&surface->visible();
      }
      while(scenes_.size()>8) {
        auto farthest=std::max_element(scenes_.begin(),scenes_.end(),[&](const auto& a,const auto& b){return std::abs(a.first-center)<std::abs(b.first-center);});scenes_.erase(farthest);
      }
      metal_owned_=ready;
      if(ready){pages_.clear();return;}
      // A partial GPU preview must never cover an otherwise complete fallback.
      for(auto& [index,surface]:surfaces_)if(surface)surface->Hide();
    }
    for (auto it = pages_.begin(); it != pages_.end();) {
      if (std::abs(it->first - center) <= 1) { ++it; continue; }
      it = pages_.erase(it);
    }
    for (const auto& placement : plan.pages) {
      const int i=placement.index;
      if (!pages_.contains(i)) {
        if (!records_.contains(i)) {records_[i] = capture_.Run(i);++native_capture_count_;}
        auto page = std::make_unique<SwipePage>(records_[i], size());
        layer()->Add(page->layer()); pages_[i] = std::move(page);
      }
      auto* page = pages_[i].get();
      gfx::Transform transform; transform.Translate(placement.x, 0);
      if (settle) {
        ui::ScopedLayerAnimationSettings settings(page->layer()->GetAnimator());
        settings.SetTransitionDuration(base::Milliseconds(160));
        settings.SetTweenType(gfx::Tween::EASE_OUT); settings.CacheRenderSurface();
        page->layer()->SetTransform(transform);
      } else {
        page->layer()->GetAnimator()->StopAnimating();
        page->layer()->SetTransform(transform);
      }
    }
    while (records_.size() > 8) {
      auto farthest = std::max_element(records_.begin(), records_.end(), [&](const auto& a, const auto& b) {
        return std::abs(a.first - center) < std::abs(b.first - center);
      });
      records_.erase(farthest);
    }
  }
  size_t page_count() const {
    if(metal_owned_)return surfaces_.size();
    return pages_.size();
  }
  bool metal_owned() const {
    return metal_owned_;
  }
  size_t native_capture_count() const{return native_capture_count_;}
  size_t metal_frame_count() const {
    size_t count=0;
    for(const auto& [index,surface]:surfaces_)if(surface)count+=surface->frame_count();
    return count;
  }
  bool contains_scene_text(std::string_view text) const {
    if(metal_owned_)for(const auto& [index,surface]:surfaces_)if(surface&&surface->visible()&&surface->contains_text_for_testing(text))return true;
    return false;
  }
  int center() const { return center_; }
 private:
  int center_ = -1;
  size_t native_capture_count_=0;
  base::RepeatingCallback<std::vector<sidebar::Item>(int)> scene_capture_;
  std::map<int,std::vector<sidebar::Item>> scenes_;
  std::map<int,std::unique_ptr<SidebarMetal>> surfaces_;
  gfx::Size scene_size_;
  unsigned background_=0;
  bool metal_owned_=false;
  base::RepeatingCallback<cc::PaintRecord(int)> capture_;
  std::map<int, std::unique_ptr<SwipePage>> pages_;
  std::map<int, cc::PaintRecord> records_;
};
BEGIN_METADATA(WorkspaceSwipePreview)
END_METADATA

class WorkspaceSwitchPreview final : public views::View {
  METADATA_HEADER(WorkspaceSwitchPreview,views::View)
 public:
  WorkspaceSwitchPreview(std::vector<sidebar::Item> items,unsigned background)
      :items_(std::move(items)),background_(background) {
    GetViewAccessibility().SetIsIgnored(true);SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey,true);
  }
  bool Prepare(double offset,base::OnceClosure presented) {
    surface_=CreateSidebarMetal();if(!surface_||!GetWidget())return false;
    surface_->SetNextPresentationCallback(std::move(presented));
    surface_->UpdatePreview(GetWidget()->GetNativeWindow(),GetBoundsInScreen(),offset,items_,background_,false,true);
    return surface_->visible();
  }
  void Animate(base::TimeDelta duration,base::OnceClosure done){surface_->AnimatePreview(0,duration,std::move(done));}
  bool visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return surface_&&surface_->contains_text_for_testing(text);}
 private:
  std::vector<sidebar::Item> items_;
  unsigned background_;
  std::unique_ptr<SidebarMetal> surface_;
};
BEGIN_METADATA(WorkspaceSwitchPreview)
END_METADATA


std::unique_ptr<views::View> CreateSplitDivider(ResizeHandle** handle,views::ResizeAreaDelegate* delegate,base::RepeatingCallback<double()> ratio,base::RepeatingCallback<int()> extent,base::RepeatingCallback<void(double,bool)> changed,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible){
  auto view=std::make_unique<RustResizeHandle>(std::move(ratio),std::move(extent),std::move(changed),std::move(painted),std::move(visible));*handle=view.get();return view;
}

// Slim header above each half of a split: page title, pane menu and close.
class PaneBar final : public views::View {
  METADATA_HEADER(PaneBar, views::View)
 public:
  PaneBar(views::Button::PressedCallback menu, views::Button::PressedCallback close,
      base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      : painted_(std::move(painted)),visible_(std::move(visible)) {
    node_graph_=std::make_shared<SidebarNodeGraphState>();
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    menu_=AddChildView(CreateSceneAction(std::move(menu),T(u"Opcje Split View"),ControlGlyph::kMore,nullptr,visible_,painted_,true,{node_graph_}));
    close_=AddChildView(CreateSceneAction(std::move(close),T(u"Zamknij kartę w Split View"),ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,visible_,painted_,true,{node_graph_}));
  }
  void CancelInput(){CancelActionInput(menu_);CancelActionInput(close_);}
  // A bar survives tab replacement and index reuse. Revoke pending input when
  // its domain target changes; title/focus updates keep an ordinary click alive.
  void SetTarget(const std::string& workspace,std::optional<size_t> index,
                 std::array<uint64_t,2> identity,bool visible){
    if(workspace_==workspace&&index_==index&&identity_==identity&&target_visible_==visible)return;
    CancelInput();workspace_=workspace;index_=index;identity_=identity;target_visible_=visible;
    const bool live=visible&&index.has_value()&&!workspace.empty()&&(identity[0]!=0||identity[1]!=0);
    rust::Vec<sidebar::ActionNodeKey> keys;if(live){keys.push_back({7,identity[0],identity[1],workspace});keys.push_back({8,identity[0],identity[1],workspace});}
    CHECK(node_graph_->graph->reconcile(std::move(keys)).admitted)<<"Invalid split-pane logical node projection";
    // The native adapters survive retargeting. Renew leases even when only the
    // domain index changed, so a pending press cannot follow an index shift.
    static_cast<RustActionView*>(menu_.get())->BindNodeInput({node_graph_,live?7u:0u,identity,workspace});
    static_cast<RustActionView*>(close_.get())->BindNodeInput({node_graph_,live?8u:0u,identity,workspace});
  }
  void SetTitle(std::u16string title,bool active) {
    if(text_==title && active_==active)return;
    if(active_!=active&&parent())parent()->InvalidateLayout();  // The host moves the active outline.
    if(text_!=title)measured_.reset();text_=std::move(title);active_=active;
    GetViewAccessibility().SetName(text_);
    SchedulePaint();painted_.Run();
  }
  const std::u16string& text() const{return text_;}
  bool active() const{return active_;}
  int text_width() const{if(!measured_)measured_=gfx::GetStringWidth(text_,DesignFont(3,12,400,false))+2;return *measured_;}
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_.reset();painted_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void Layout(PassKey) override {
    const auto g=sidebar::pane_bar_layout(width(),height());menu_->SetBounds(g.menu.x,g.menu.y,g.menu.width,g.menu.height);close_->SetBounds(g.close.x,g.close.y,g.close.width,g.close.height);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if(visible_ && visible_.Run())return;
    const auto g=sidebar::pane_bar_layout(width(),height());
    canvas->DrawStringRectWithFlags(text_,DesignFont(3,12,400,false),active_?Palette(this).ink:Palette(this).ink_muted,gfx::Rect(g.title.x,g.title.y,g.title.width,g.title.height),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
  views::View* menu() const { return menu_; }
  views::View* close() const { return close_; }
 private:
  std::shared_ptr<SidebarNodeGraphState> node_graph_;
  bool active_=false;
  std::u16string text_;
  mutable std::optional<int> measured_;
  base::RepeatingClosure painted_;
  base::RepeatingCallback<bool()> visible_;
  raw_ptr<views::View> menu_ = nullptr;
  raw_ptr<views::View> close_ = nullptr;
  std::string workspace_;
  std::optional<size_t> index_;
  std::array<uint64_t,2> identity_{};
  bool target_visible_=false;
};
BEGIN_METADATA(PaneBar)
END_METADATA

// Keeps WebViews owned by the existing content host while laying out only the
// selected pages. Each divider resizes only its two neighbouring panes.
// SplitView: the active pane's 1.5 px `accent` outline (radius-md) on its own
// transparent layer above the page layers; it never takes events.
class PaneOutline final : public views::View {
  METADATA_HEADER(PaneOutline,views::View)
 public:
  PaneOutline(){SetPaintToLayer();layer()->SetFillsBoundsOpaquely(false);SetCanProcessEventsWithinSubtree(false);GetViewAccessibility().SetIsIgnored(true);}
  void SetCard(std::optional<gfx::Rect> card){if(card_==card)return;card_=card;SchedulePaint();}
  void OnPaint(gfx::Canvas* canvas) override{
    if(!card_)return;gfx::RectF ring(*card_);ring.Inset(0.75f);
    cc::PaintFlags flags;flags.setAntiAlias(true);flags.setStyle(cc::PaintFlags::kStroke_Style);flags.setStrokeWidth(1.5f);flags.setColor(Palette(this).accent);
    canvas->DrawRoundRect(ring,12-0.75f,flags);}
 private:
  std::optional<gfx::Rect> card_;
};
BEGIN_METADATA(PaneOutline)
END_METADATA

class PaneHost final : public views::View, public views::ResizeAreaDelegate {
  METADATA_HEADER(PaneHost, views::View)
 public:
  PaneHost(base::RepeatingCallback<void(size_t, double, bool)> changed,
           base::RepeatingCallback<void(size_t, views::View*)> menu,
           base::RepeatingCallback<void(size_t)> close,base::RepeatingClosure painted,
           base::RepeatingCallback<bool(size_t)> visible)
      : painted_(std::move(painted)),changed_(std::move(changed)) {
    for (size_t pane = 0; pane < kMaxSplitPanes; ++pane) {
      auto* bar = AddChildView(std::make_unique<PaneBar>(views::Button::PressedCallback(),
          base::BindRepeating(close, pane),painted_,base::BindRepeating(visible,SurfaceIndex(false,pane))));
      SetActionCallback(bar->menu(),base::BindRepeating(menu,pane,base::Unretained(bar->menu())));
      bar->SetVisible(false);
      bars_[pane] = bar;
    }
    for (size_t divider = 0; divider + 1 < kMaxSplitPanes; ++divider) {
      ResizeHandle* handle=nullptr;
      dividers_[divider]=AddChildView(CreateSplitDivider(&handle,this,
          base::BindRepeating([](PaneHost* host,size_t divider){return sidebar::split_pair_ratio(host->WeightSlice(),divider);},base::Unretained(this),divider),
          base::BindRepeating([](PaneHost* host,size_t divider){return host->PairExtent(divider);},base::Unretained(this),divider),
          base::BindRepeating(&PaneHost::ApplyRustRatio,base::Unretained(this),divider),painted_,base::BindRepeating(visible,SurfaceIndex(true,divider))));
      handles_[divider]=handle;
      dividers_[divider]->GetViewAccessibility().SetName(T(u"Zmień szerokość paneli"));
      dividers_[divider]->SetVisible(false);
    }
    outline_=AddChildView(std::make_unique<PaneOutline>());
  }
  // Metal surfaces: bar 0, bar 1, divider 0, bar 2, divider 1, ...
  // (The active outline is the last child; see SetPanes.)
  static size_t SurfaceIndex(bool divider,size_t index){return divider?2*index+2:index==0?0:2*index-1;}
  views::View* surface(size_t index) const{return index%2==0&&index>0?dividers_[index/2-1].get():bars_[index==0?0:(index+1)/2].get();}
  PaneBar* bar(size_t pane) const { return pane<bars_.size()?bars_[pane].get():nullptr; }
  views::View* divider(size_t index=0) const{return index<dividers_.size()?dividers_[index].get():nullptr;}
  ResizeHandle* divider_handle(size_t index=0) const{return index<handles_.size()?handles_[index].get():nullptr;}
  size_t pane_count() const{return panes_.size();}
  void SetPanes(std::vector<views::View*> panes, std::vector<double> weights, bool vertical) {
    if (panes.size() > kMaxSplitPanes) panes.resize(kMaxSplitPanes);
    if (panes.size() < 2) weights.clear();
    else { auto normalized=sidebar::split_normalized(rust::Slice<const double>(weights.data(),weights.size()),panes.size()); weights.assign(normalized.begin(),normalized.end()); }
    const bool same_panes=panes.size()==panes_.size()&&std::equal(panes.begin(),panes.end(),panes_.begin());
    if (same_panes && weights_ == weights && vertical_ == vertical) return;
    if(!same_panes || vertical_!=vertical){for(auto& handle:handles_)handle->CancelDrag();for(auto& bar:bars_)bar->CancelInput();}
    panes_.assign(panes.begin(),panes.end()); weights_ = std::move(weights); vertical_ = vertical;
    ReorderChildView(outline_, children().size() - 1);  // Above the page layers added since.
    const bool split=panes_.size()>=2;
    for(size_t i=0;i<handles_.size();++i){handles_[i]->SetVertical(vertical);dividers_[i]->SetVisible(split&&i+1<panes_.size());}
    for(size_t i=0;i<bars_.size();++i)bars_[i]->SetVisible(split&&i<panes_.size());
    InvalidateLayout();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    painted_.Run();
  }
  void Layout(PassKey) override {
    const auto g=sidebar::pane_host_layout(width(),height(),WeightSlice(),vertical_);
    auto set=[](views::View* view,const sidebar::Rect& r){if(view)view->SetBounds(r.x,r.y,r.width,r.height);};
    for(size_t i=0;i<panes_.size()&&i<g.pages.size();++i)set(panes_[i],g.pages[i]);
    for(size_t i=0;i<g.bars.size()&&i<bars_.size();++i)set(bars_[i],g.bars[i]);
    for(size_t i=0;i<g.dividers.size()&&i<dividers_.size();++i)set(dividers_[i],g.dividers[i]);
    outline_->SetBounds(0,0,width(),height());std::optional<gfx::Rect> card;
    for(size_t i=0;i<g.bars.size()&&i<g.pages.size()&&i<bars_.size();++i)if(bars_[i]->active()){card=gfx::Rect(g.bars[i].x,g.bars[i].y,g.bars[i].width,g.bars[i].height+g.pages[i].height);}
    static_cast<PaneOutline*>(outline_.get())->SetCard(panes_.size()>=2?card:std::nullopt);
    LayoutSuperclass<views::View>(this);painted_.Run();
  }
  // Native ResizeArea fallback: the first visible divider follows the pointer.
  void OnResize(int amount, bool done) override {
    if (panes_.size() < 2) return;
    if (!resizing_) { initial_ratio_ = sidebar::split_pair_ratio(WeightSlice(),0); resizing_ = true; }
    ApplyRustRatio(0, initial_ratio_ + static_cast<double>(amount) / std::max(1, PairExtent(0) - 8), done);
    if (done) resizing_ = false;
  }
 private:
  rust::Slice<const double> WeightSlice() const{return rust::Slice<const double>(weights_.data(),weights_.size());}
  // Extent of the two panes beside a divider, including the divider itself.
  int PairExtent(size_t divider) const{
    const int extent=vertical_?height():width();if(weights_.size()<2||divider+1>=weights_.size())return extent;
    return static_cast<int>(extent*(weights_[divider]+weights_[divider+1]));
  }
  void ApplyRustRatio(size_t divider,double ratio,bool done){
    auto weights=sidebar::split_apply_ratio(WeightSlice(),divider,ratio);weights_.assign(weights.begin(),weights.end());
    InvalidateLayout();painted_.Run();changed_.Run(divider,sidebar::split_pair_ratio(WeightSlice(),divider),done);
  }
  base::RepeatingClosure painted_;
  std::vector<raw_ptr<views::View>> panes_;
  std::vector<double> weights_;
  std::array<raw_ptr<views::View>, kMaxSplitPanes - 1> dividers_ = {};
  std::array<raw_ptr<ResizeHandle>, kMaxSplitPanes - 1> handles_ = {};
  std::array<raw_ptr<PaneBar>, kMaxSplitPanes> bars_ = {};
  raw_ptr<views::View> outline_ = nullptr;
  base::RepeatingCallback<void(size_t, double, bool)> changed_;
  double initial_ratio_ = 0.5;
  bool resizing_ = false;
  bool vertical_ = false;
};
BEGIN_METADATA(PaneHost)
END_METADATA

// Frame: tinted ground, header row (traffic lights + page commands) and the
// page as a rounded white card. Pinned, the sidebar column owns the header.
class ShellRoot final : public views::View {
  METADATA_HEADER(ShellRoot, views::View)
 public:
  ShellRoot() = default;
  gfx::Size GetMinimumSize() const override { return gfx::Size(560, 560); }
  void Layout(PassKey) override {
    const int column = pinned ? SidebarColumn(sidebar_width, width()) + kGap : 0;
    const int header_width = header->GetPreferredSize().width();
    // Traffic lights and page commands share one axis through the middle of
    // the header band; the commands start a fixed gap after the lights.
    constexpr int kAxis = kShellHeaderHeight / 2;
    int header_x = kTrafficLightInset;
#if BUILDFLAG(IS_MAC)
    if (const auto lights = AlignWorkspaceWindowButtons(GetWidget(), kAxis); !lights.IsEmpty())
      header_x = lights.right() + kHeaderLightsGap;
#endif
    header->SetBounds(header_x, kAxis - kHeaderButtonSize / 2, header_width, kHeaderButtonSize);
    const int top = kShellHeaderHeight;
    content->SetBounds(column + kGap, top, std::max(0, width() - column - 2 * kGap),
                       std::max(0, height() - top - kGap));
    edge->SetBounds(0, kShellHeaderHeight, kGap, std::max(0, height() - kShellHeaderHeight));
    edge->SetVisible(!pinned);
    const auto g=sidebar::shell_layout(width(),height(),sidebar_width,pinned);
    content->SetBounds(g.content.x,g.content.y,g.content.width,g.content.height);
    edge->SetBounds(g.edge.x,g.edge.y,g.edge.width,g.edge.height);
    LayoutSuperclass<views::View>(this);
  }
  // Empty frame and header space moves the window, like a native titlebar.
  bool IsDraggable(const gfx::Point& point) {
    if (content->bounds().Contains(point)) return false;
    auto* target = GetEventHandlerForPoint(point);
    return target == this || target == header.get();
  }
  raw_ptr<views::View> header = nullptr;
  raw_ptr<views::View> content = nullptr;
  raw_ptr<views::View> edge = nullptr;
  bool pinned = true;
  int sidebar_width = kSidebarWidth;
};
BEGIN_METADATA(ShellRoot)
END_METADATA

class ShellClientView final : public views::ClientView {
  METADATA_HEADER(ShellClientView, views::ClientView)
 public:
  ShellClientView(views::Widget* widget, views::View* contents)
      : views::ClientView(widget, contents) {}
  int NonClientHitTest(const gfx::Point& point) override {
    const int result = views::ClientView::NonClientHitTest(point);
    auto* root = views::AsViewClass<ShellRoot>(contents_view());
    if (result != HTCLIENT || !root || !parent()) return result;
    gfx::Point local = point;
    views::View::ConvertPointToTarget(parent(), root, &local);
    return root->IsDraggable(local) ? HTCAPTION : result;
  }
};
BEGIN_METADATA(ShellClientView)
END_METADATA

void RegisterKeys(views::Widget* widget, ui::AcceleratorTarget* target,
                  const BrowserTools& tools, bool panel = false, bool install_native = true) {
  for (const auto& shortcut : tools.shortcuts) if (shortcut.accelerator.key_code() != ui::VKEY_UNKNOWN)
    widget->GetFocusManager()->RegisterAccelerator(shortcut.accelerator, ui::AcceleratorManager::kHighPriority, target);
#if BUILDFLAG(IS_MAC)
  constexpr int primary = ui::EF_COMMAND_DOWN;
#else
  constexpr int primary = ui::EF_CONTROL_DOWN;
#endif
  // Preserve Zitter's first two original chords until these actions are remapped.
  const auto defaults = ArcShortcuts();
  for (int i = 0; i < 2; ++i) {
    const auto id = "space" + std::to_string(i + 1);
    auto current = std::find_if(tools.shortcuts.begin(), tools.shortcuts.end(), [&](const auto& item) { return item.id == id; });
    auto original = std::find_if(defaults.begin(), defaults.end(), [&](const auto& item) { return item.id == id; });
    if (current != tools.shortcuts.end() && current->accelerator == original->accelerator)
      widget->GetFocusManager()->RegisterAccelerator(ui::Accelerator(static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), primary | ui::EF_ALT_DOWN), ui::AcceleratorManager::kHighPriority, target);
  }
  widget->GetFocusManager()->RegisterAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, 0), ui::AcceleratorManager::kHighPriority, target);
#if BUILDFLAG(IS_MAC)
  if (install_native) InstallWorkspaceKeyboardRouting(widget, panel);
#endif
}

// Typed text → destination, or an invalid GURL for anything but HTTP(S).
GURL ResolveTyped(const std::string& text, SearchEngine engine) {
  GURL url(text);
  const bool bare_address = text.find('.') != std::string::npos ||
                            text.starts_with("localhost") ||
                            text.starts_with("[::1]");
  if ((!url.has_scheme() && !bare_address) || text.find(' ') != std::string::npos) {
    const std::string prefix = engine == SearchEngine::kGoogle ? "https://www.google.com/search?q="
        : engine == SearchEngine::kBing ? "https://www.bing.com/search?q=" : "https://duckduckgo.com/?q=";
    url = GURL(prefix + base::EscapeQueryParamValue(text, true));
  } else if (!url.has_scheme() || text.starts_with("localhost:") ||
             (text.find("://") == std::string::npos && url.scheme().find('.') != std::string::npos)) {
    // GURL can read example.com:8080 as a nonstandard scheme. Chromium's
    // address fixer recognizes the dotted host and port correctly.
    url = url_formatter::FixupURL(text);
  }
  return url.SchemeIsHTTPOrHTTPS() ? url : GURL();
}
}  // namespace
bool SceneActionPressedForTesting(views::View* view){
  if(auto* action=views::AsViewClass<RustActionView>(view))return action->Snapshot().visual==sidebar::RowVisual::Pressed;
  auto* button=views::AsViewClass<views::Button>(view);return button&&button->GetState()==views::Button::STATE_PRESSED;
}
bool PageDialogActionsVisibleForTesting(views::View* actions,std::string_view text){
  auto* group=views::AsViewClass<SceneActionGroup>(actions);return group&&group->contains_text(text);
}
std::unique_ptr<views::View> CreatePageDialogActions(views::Widget* widget){
  auto* delegate=widget->widget_delegate()->AsDialogDelegate();
  auto group=std::make_unique<SceneActionGroup>(false,views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL),true,true);
  auto add=[&](ui::mojom::DialogButton type){
    if(!(delegate->buttons()&static_cast<int>(type)))return;
    auto* button=static_cast<SceneTextButton*>(group->AddAction({},delegate->GetDialogButtonLabel(type),delegate->GetDialogButtonStyle(type)));
    // Dialog: „Anuluj” is quiet; the action is primary, or danger strong when the
    // dialog defaults to Cancel because the action destroys something.
    if(type==ui::mojom::DialogButton::kCancel)button->SetQuiet();
    else if(delegate->GetDefaultDialogButton()==static_cast<int>(ui::mojom::DialogButton::kCancel))button->SetDangerStrong();
    else button->SetPrimary();
    button->SetID(static_cast<int>(type));button->SetProperty(views::kElementIdentifierKey,type==ui::mojom::DialogButton::kOk?views::DialogClientView::kOkButtonElementId:views::DialogClientView::kCancelButtonElementId);button->SetGroup(6666);button->SetEnabled(delegate->IsDialogButtonEnabled(type));button->SetDefault(delegate->GetIsDefault(type));
    button->SetCallback(base::BindRepeating([](base::WeakPtr<views::Widget> widget,views::View* button,ui::mojom::DialogButton type,const ui::Event& event){
      if(!widget||!widget->IsVisible())return;
      auto* delegate=widget->widget_delegate()->AsDialogDelegate();
      if(!delegate||delegate->GetDialogClientView()->IsPossiblyUnintendedInteraction(event,delegate->ShouldAllowKeyEventsDuringInputProtection())||delegate->ShouldIgnoreButtonPressedEventHandling(button,event))return;
      if(type==ui::mojom::DialogButton::kOk)delegate->AcceptDialog();else delegate->CancelDialog();
    },widget->GetWeakPtr(),base::Unretained(button),type));
  };
  if(views::PlatformStyle::kIsOkButtonLeading){add(ui::mojom::DialogButton::kOk);add(ui::mojom::DialogButton::kCancel);}else{add(ui::mojom::DialogButton::kCancel);add(ui::mojom::DialogButton::kOk);}
  return group;
}

std::unique_ptr<views::DialogDelegate> CreateSceneModalDialog(std::unique_ptr<views::View> body,bool control,bool modal){
  return std::make_unique<SceneModalDialog>(std::move(body),control,modal);
}
std::unique_ptr<PanelScene> CreatePanelScene(views::View* root){
  return std::make_unique<PanelSceneImpl>(root);
}
std::unique_ptr<PageDialogScene> CreatePageDialogScene(views::View* root){return CreatePanelScene(root);}
class PreferenceChromeState final : public PreferenceChromeRenderer {public:using PreferenceChromeRenderer::PreferenceChromeRenderer;};

WorkspaceShell::Callbacks::Callbacks() = default;
WorkspaceShell::Callbacks::Callbacks(Callbacks&&) = default;
WorkspaceShell::Callbacks& WorkspaceShell::Callbacks::operator=(Callbacks&&) = default;
WorkspaceShell::Callbacks::~Callbacks() = default;

WorkspaceShell::WorkspaceShell(base::FilePath path, Callbacks callbacks)
    : settings_path_(std::move(path)), callbacks_(std::move(callbacks)),
      io_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  workspace_swipe_ = std::make_unique<WorkspaceSwipeState>();
  state_.workspace_id = "work";
  sidebar_tree_.SetDefaultCollapsed(true);
  ui::NativeTheme::GetInstanceForNativeUi()->AddObserver(this);
}
WorkspaceShell::~WorkspaceShell() { ui::NativeTheme::GetInstanceForNativeUi()->RemoveObserver(this); Detach(); }

// static
views::WidgetDelegate::ClientViewFactory WorkspaceShell::ClientViewFactory() {
  return base::BindOnce([](views::Widget* widget, views::View* contents)
                            -> std::unique_ptr<views::ClientView> {
    return std::make_unique<ShellClientView>(widget, contents);
  });
}

std::unique_ptr<views::View> WorkspaceShell::Build() {
  ReconcileChromeNodes();
  auto root = std::make_unique<ShellRoot>();
  root_ = root.get();
  root->SetBackground(views::CreateSolidBackground(FrameVoid(workspace_color("work"), root_)));
  auto weak = weak_factory_.GetWeakPtr();
  metal_header_=CreateSidebarMetal();
  auto* header = root->AddChildView(std::make_unique<ChromeHeaderView>(
      base::BindRepeating(&WorkspaceShell::ScheduleMetalHeader,weak)));
  header_view_=header;
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),2))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  if (metal_header_) header->SetLayoutManager(CreateSidebarDotsLayout(2));
  auto header_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    return shell && shell->metal_header_visible();
  },weak);
  header_sidebar_button_=header->AddChildView(CreateSceneAction(
      base::BindRepeating(&WorkspaceShell::ToggleSidebar,weak),T(u"Pokaż lub ukryj karty"),
      GlyphButton::Glyph::kSidebar,nullptr,header_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalHeader,weak),true,{chrome_node_graph_,4,{},"header.sidebar"}));
  const std::array<std::pair<const gfx::VectorIcon*, std::u16string>, 3> commands = {{
      {&vector_icons::kArrowBackIcon, T(u"Wstecz")},
      {&vector_icons::kArrowForwardIcon, T(u"Dalej")},
      {&vector_icons::kReloadCustomIcon, T(u"Odśwież")}}};
  const std::array<const char*,3> command_nodes={"header.back","header.forward","header.reload"};
  for (size_t i = 0; i < commands.size(); ++i) {
    nav_buttons_[i] = header->AddChildView(CreateSceneAction(
        base::BindRepeating(callbacks_.page_command, static_cast<PageCommand>(i)),
        commands[i].second, GlyphButton::Glyph::kVector, commands[i].first,header_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalHeader,weak),true,{chrome_node_graph_,4,{},command_nodes[i]}));
  }
  for (auto& child : header->children()) child->SetPreferredSize(gfx::Size(kHeaderButtonSize, kHeaderButtonSize));
  content_host_ = root->AddChildView(std::make_unique<PaneHost>(callbacks_.split_ratio,
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, size_t pane, views::View* anchor) {
        if (shell) shell->ShowSplitPaneMenu(pane, anchor->GetBoundsInScreen().bottom_left());
      }, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, size_t pane) {
        if (shell) shell->CloseSplitPane(pane);
      }, weak),base::BindRepeating(&WorkspaceShell::ScheduleMetalSplit,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,size_t index){return shell && shell->metal_split_visible(index);},weak)));
  content_host_->SetBackground(views::CreateRoundedRectBackground(kCard, 10));
  edge_ = root->AddChildView(std::make_unique<EdgeView>(
      base::BindRepeating(&WorkspaceShell::ShowSidebar, weak)));
  root->header = header;
  root->content = content_host_;
  root->edge = edge_;
  return root;
}

void WorkspaceShell::SetPanes(std::vector<views::View*> panes, std::vector<double> weights, bool vertical) {
  static_cast<PaneHost*>(content_host_.get())->SetPanes(std::move(panes), std::move(weights), vertical);
  // Size hidden native pages before WasShown can request their first frame.
  if (widget_) widget_->LayoutRootViewIfNecessary();
  ScheduleMetalSplit();
}

void WorkspaceShell::Attach(views::Widget* widget, ui::AcceleratorTarget* target) {
  widget_ = widget;
  accelerator_target_ = target;

  widget_->AddObserver(this);
  RegisterKeys(widget_, target, tools_);
  PreparePanelText(widget_->GetRootView());
  auto weak = weak_factory_.GetWeakPtr();

  // Sidebar: address pill, workspace, tabs; workspace switch and color below.
  auto sidebar = std::make_unique<SidebarView>(base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell,bool inside,bool dragging) {
        if (!shell)return;
        const bool blocked=shell->settings_visible() ||
            (shell->general_widget_ && shell->general_widget_->IsVisible()) ||
            (shell->tools_widget_ && shell->tools_widget_->IsVisible()) ||
            (shell->workspace_menu_runner_ && shell->workspace_menu_runner_->IsRunning());
        bool hide=!shell->pinned_&&!blocked&&!inside&&!dragging;
        if(shell->metal_sidebar_)hide=sidebar::sidebar_auto_hide(shell->pinned_,blocked,inside,dragging);
        if(hide) {
          shell->sidebar_widget_->Hide();
          shell->RestorePageFocus();
        }
      }, weak));
  metal_sidebar_ = CreateSidebarMetal();
  sidebar->painted = base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar, weak);
  sidebar_root_ = sidebar.get();
  sidebar->AddPreTargetHandler(this);
  sidebar->set_context_menu_controller(this);
  ResizeHandle* sidebar_handle=nullptr;
  sidebar->grip = sidebar->AddChildView(CreateSidebarGrip(&sidebar_handle,
      base::BindRepeating(&WorkspaceShell::ResizeSidebar,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell?shell->sidebar_width_:248;},weak),
      base::BindRepeating(&WorkspaceShell::ApplySidebarWidth,weak),
      base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_grip_visible();},weak)));
  sidebar->grip_handle=sidebar_handle;
  auto* pill = sidebar->AddChildView(CreateAddressRow(
      base::BindRepeating(&WorkspaceShell::OpenAddress, weak, false),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        return shell && shell->metal_sidebar_visible();
      },weak),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak),{chrome_node_graph_,4,{},"sidebar.address"}));
  url_pill_ = pill;
  pill->set_context_menu_controller(this);
  Spacer(sidebar.get(), 12);
  workspace_name_ = sidebar->AddChildView(std::make_unique<WorkspaceHeading>(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        return shell && shell->metal_sidebar_visible();
      }, weak)));
#if BUILDFLAG(IS_MAC)
  const auto* new_tab_hint = u"⌘T";
#else
  const auto* new_tab_hint = u"Ctrl+T";
#endif
  RowRecipe::Spec new_tab;
  new_tab.title = T(u"Nowa karta");
  new_tab.detail = new_tab_hint;
  new_tab.sidebar_row = true;
  new_tab.scene_visible = base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    return shell && shell->metal_sidebar_visible();
  }, weak);
  new_tab.icon = &vector_icons::kAdd2Icon;
  new_tab.kbd = true;
  new_tab_button_ = sidebar->AddChildView(CreateSceneRow(
      base::BindRepeating(&WorkspaceShell::OpenAddress,weak,true),std::move(new_tab),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak),{chrome_node_graph_,4,{},"sidebar.new-tab"}));
  std::unique_ptr<views::ScrollBar> vertical_bar;
  vertical_bar=CreateSidebarScrollBar(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak));
  PanelViewport* tab_viewport=nullptr;
  auto viewport_host=CreatePanelViewport(&tab_viewport,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),{0,std::numeric_limits<int>::max(),false},std::move(vertical_bar));tab_scroll_=tab_viewport;
  auto* scroll=sidebar->AddChildView(std::move(viewport_host));scroll->set_context_menu_controller(this);scroll->SetPreferredSize(gfx::Size(200,0));
  auto list = std::make_unique<SidebarListView>(
      base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        return shell && (shell->metal_sidebar_visible()||shell->workspace_swipe_metal_visible()||shell->workspace_switch_metal_visible());
      }, weak));
  list->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
  tab_list_=list.get();tab_scroll_->SetPanelContents(std::move(list));
  tab_list_->set_context_menu_controller(this);
  // Rust panel layout in Rust mode; identical BoxLayout fallback for Views.
  SetSidebarPanelLayout(sidebar.get(), true, gfx::Insets::TLBR(2, 4, 8, 4), 2, scroll);

  // Workspace settings: profile and color live here, not in the sidebar.
  workspace_form_=std::make_unique<WorkspaceFormState>();tools_form_=std::make_unique<ToolsFormState>();
  std::array<std::u16string,17> workspace_copy={T(u"Nazwa przestrzeni"),T(u"Np. Projekt lub Klient"),T(u"Profil przeglądania"),T(u"Profil przestrzeni"),T(u"Wspólny profil = wspólne logowanie i cookies.\nZmiana profilu zamknie karty tej przestrzeni."),T(u"Zapisz poniżej nazwę i profil. Wygląd zapisuje się od razu."),T(u"Zamknij ustawienia przestrzeni"),T(u"Wygląd · zapis automatyczny"),T(u"Kolor przestrzeni"),T(u"Kolor przestrzeni #RRGGBB"),T(u"Emoji przestrzeni"),T(u"Emoji przestrzeni"),T(u"Inne… (⌃⌘Spacja)"),T(u"Więcej opcji"),T(u"Zarządzaj profilami"),T(u"Importuj z przeglądarki…"),T(u"Usuń przestrzeń")};
  {const auto c=sidebar::workspace_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};workspace_copy={text(c.name_label),text(c.placeholder),text(c.profile_label),text(c.profile_ax),text(c.profile_hint),text(c.save_hint),text(c.close),text(c.appearance),text(c.color),text(c.color_ax),text(c.emoji),text(c.emoji_ax),text(c.emoji_placeholder),text(c.more),text(c.manage),text(c.import_label),text(c.remove_label)};}
  auto settings_text=[&](views::View* parent,std::u16string text,SkColor ink,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false)->views::View* {
    return parent->AddChildView(std::make_unique<SceneParagraph>(std::move(text),delta,weight!=gfx::Font::Weight::NORMAL,ink));
  };
  auto settings = std::make_unique<views::View>();
  settings_root_ = settings.get();
  settings->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 8));
  settings->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  settings->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto* settings_header = settings->AddChildView(std::make_unique<views::View>());
  settings_icon_=settings_header->AddChildView(std::make_unique<SpaceIconTile>(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(!shell)return std::pair<std::u16string,bool>();
    if(auto emoji=shell->workspace_emoji(shell->state_.workspace_id);!emoji.empty())return std::pair(emoji,false);
    for(const auto& workspace:shell->state_.workspaces)if(workspace.id==shell->state_.workspace_id&&!workspace.name.empty())
      return std::pair(base::i18n::ToUpper(base::UTF8ToUTF16(workspace.name).substr(0,1)),true);
    return std::pair<std::u16string,bool>();},weak)));
  auto* settings_titles=PanelStack(settings_header,2);
  settings_title_ = PanelText(settings_titles,T(u"Praca"),kDisplayLarge,kInk);
  profile_name_ = PanelText(settings_titles,u"",kLabelText,kMuted);
  SetSidebarPanelLayout(settings_header, false, gfx::Insets(), Metrics().space_3, settings_titles);
  auto dismiss_workspace=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if(shell){shell->settings_widget_->Hide();shell->RestorePageFocus();}
  },weak);
  {auto* controls=settings_header->AddChildView(std::make_unique<ScenePreferenceControls>(true));BindFormCommand(controls->AddControl({},workspace_copy[6]),sidebar::FormCommand::WorkspaceDismiss);}
  auto* identity=PanelSection(settings.get(),T(u"Nazwa i profil"));
  auto* identity_columns=PanelColumns(identity);
  auto* name_column=PanelStack(identity_columns,Metrics().space_1);auto* profile_column=PanelStack(identity_columns,Metrics().space_1);
  PanelText(name_column,workspace_copy[0],kLabelText,kMuted);
  workspace_input_ = AddEditor(name_column);workspace_input_->view()->SetPreferredSize(gfx::Size(0,36));
  workspace_input_->GetViewAccessibility().SetName(workspace_copy[0]);
  workspace_input_->SetPlaceholderText(workspace_copy[1]);
  PanelText(profile_column,workspace_copy[2],kLabelText,kMuted);
  profile_picker_=ProfilePicker(profile_column,workspace_copy[3],base::BindRepeating([]{}));profile_picker_->SetPreferredSize(gfx::Size(0,36));
  auto* save_hint=settings_text(identity,workspace_copy[5],kMuted,-2,gfx::Font::Weight::NORMAL,true);
  auto* actions=ActionGroup(identity,false,8);
  edit_workspace_ = AddAction(actions,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->SubmitWorkspace(shell->creating_workspace_);
      }, weak),T(u"Zapisz nazwę i profil"),ui::ButtonStyle::kProminent);
  add_workspace_ = AddAction(actions,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if(shell)shell->ToggleWorkspaceCreation();
      }, weak),T(u"Nowa przestrzeń…"));
  BindFormCommand(edit_workspace_,sidebar::FormCommand::WorkspaceSave);BindFormCommand(add_workspace_,sidebar::FormCommand::WorkspaceToggle);
  configuration_error_ = settings_text(settings.get(),u"",kError,-2,gfx::Font::Weight::NORMAL,true);
  configuration_error_->SetVisible(false);
  auto* appearance = settings->AddChildView(std::make_unique<views::View>());
  workspace_appearance_ = appearance;
  appearance->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  PanelText(appearance,T(u"Kolor przestrzeni"),kEyebrow,kMuted);
  auto appearance_row=[&](bool emoji)->views::View* {
    return appearance->AddChildView(std::make_unique<AppearanceRow>(emoji));
  };
  auto* color_row=appearance_row(false);
  std::vector<std::pair<SkColor,std::u16string>> colors;for(const auto& p:sidebar::appearance_presets(false))colors.emplace_back(p.color,base::UTF8ToUTF16(std::string(p.label)));
  for(size_t preset=0;preset<colors.size();++preset){
    const auto& [color,label]=colors[preset];views::Button::PressedCallback callback;
    auto* swatch=AppearancePicker(color_row,std::move(callback),label,GlyphButton::Glyph::kSwatch);
    BindAppearancePreset(swatch,false,preset);
    SetActionColor(swatch,color);
  }
  // Any color is picked on the wheel below; the presets are named pills.
  Spacer(color_row, 1);color_row->AddChildView(std::make_unique<views::View>());color_presets_=color_row;
  color_error_ = settings_text(appearance,u"",kError,-2,gfx::Font::Weight::NORMAL,true);
  color_error_->SetVisible(false);
  // Any color: the hue ring and the saturation x tone field beside it.
  {auto* wheel=appearance->AddChildView(std::make_unique<views::View>());
   wheel->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),22))->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
   auto current=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell?shell->workspace_color(shell->state_.workspace_id):kDefaultWorkspaceColor;},weak);
   auto emoji=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell?shell->workspace_emoji(shell->state_.workspace_id):std::u16string();},weak);
   auto pick=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,SkColor color){if(shell)shell->SetWorkspaceColor(shell->state_.workspace_id,color);},weak);
   for(size_t i=0;i<color_wheel_.size();++i)color_wheel_[i]=wheel->AddChildView(std::make_unique<ColorWheelControl>(i==0,current,emoji,pick));}
  Spacer(appearance, Metrics().space_2);
  PanelText(appearance,workspace_copy[10],kEyebrow,kMuted);
  auto* emoji_row=appearance_row(true);views::BoxLayout* emoji_layout=nullptr;
  std::vector<std::pair<std::u16string,std::u16string>> emojis;for(const auto* e:kWorkspaceEmojis)emojis.emplace_back(e,TF(u"Emoji {}",{e}));
  emojis.clear();for(const auto& p:sidebar::appearance_presets(true))emojis.emplace_back(base::UTF8ToUTF16(std::string(p.emoji)),base::UTF8ToUTF16(std::string(p.label)));
  for(size_t preset=0;preset<emojis.size();++preset){
    const auto& [emoji,label]=emojis[preset];views::Button::PressedCallback callback;
    auto* pick=AppearancePicker(emoji_row,std::move(callback),label,GlyphButton::Glyph::kDot);
    BindAppearancePreset(pick,true,preset);
    SetActionLabel(pick,emoji);
  }
  // The last tile opens the system Emoji & Symbols viewer. It types into the
  // focused field, so the field stays as a zero-width receiver at the row's end.
  {auto* more_emoji=AppearancePicker(emoji_row,base::BindRepeating(&WorkspaceShell::OpenEmojiPicker,weak),base::UTF8ToUTF16(std::string(sidebar::workspace_copy().more_emoji)),GlyphButton::Glyph::kDot);
   SetActionLabel(more_emoji,u"+");}
  Spacer(emoji_row, 1)->SetPreferredSize(gfx::Size(8, 1));
  emoji_input_ = AddEditor(emoji_row);emoji_input_->view()->SetPreferredSize(gfx::Size(0,28));
  emoji_input_->GetViewAccessibility().SetName(workspace_copy[11]);
  emoji_input_->SetPlaceholderText(workspace_copy[12]);
  if(auto* native=views::AsViewClass<views::Textfield>(emoji_input_->view())){
    native->SetFontList(Font(-1));
  native->SetBackgroundColor(kChip);
  native->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(1, 7, Hairline(root_)), gfx::Insets::VH(4, 8)));
  }
  if(emoji_layout)emoji_layout->SetFlexForView(emoji_input_->view(), 1);

  emoji_input_->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(shell&&shell->emoji_input_&&!shell->emoji_input_->IsIMEComposing())
      shell->SetWorkspaceEmoji(shell->state_.workspace_id,std::u16string(shell->emoji_input_->GetText()));
  },weak),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleAppearanceEditorKey(true,event);},weak));

  auto* more = settings->AddChildView(std::make_unique<views::View>());
  workspace_more_ = more;
  PanelText(more,workspace_copy[13],kEyebrow,kMuted);
  auto* more_actions=ActionGroup(more,false,0);
  [[maybe_unused]] auto* workspace_profiles=AddAction(more_actions,
      base::BindRepeating(&WorkspaceShell::OpenProfiles, weak, std::string(), std::string()), workspace_copy[14],ui::ButtonStyle::kText);
  [[maybe_unused]] auto* workspace_import=AddAction(ActionGroup(more,false,0),
      base::BindRepeating(&WorkspaceShell::OpenImport, weak), workspace_copy[15],ui::ButtonStyle::kText);
  remove_workspace_ = AddAction(ActionGroup(more,false,0),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->ConfirmRemoveWorkspace(shell->state_.workspace_id);
      }, weak), workspace_copy[16],ui::ButtonStyle::kText);
  views::AsViewClass<SceneTextButton>(remove_workspace_)->SetDanger();
  BindFormCommand(remove_workspace_,sidebar::FormCommand::WorkspaceRemove);BindFormCommand(workspace_profiles,sidebar::FormCommand::WorkspaceProfiles);BindFormCommand(workspace_import,sidebar::FormCommand::WorkspaceImport);

  settings->SetLayoutManager(CreateSidebarColumnLayout(Metrics().space_4));settings->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(Metrics().space_6)));appearance->SetLayoutManager(CreateSidebarColumnLayout(Metrics().space_2));more->SetLayoutManager(CreateSidebarColumnLayout(Metrics().space_2));
  // Appearance first (saved at once), then the name and profile with the save row.
  settings->ReorderChildView(appearance,settings->GetIndexOf(settings_header).value()+1);
  save_hint->SetVisible(false);

  auto* footer = sidebar->AddChildView(std::make_unique<views::View>());
  sidebar_footer_=footer;
  auto footer_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    return shell && shell->metal_sidebar_visible();
  },weak);
  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  footer_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  ReconcileChromeNodes();
  pin_button_ = footer->AddChildView(CreateSceneAction(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->SetPinned(!shell->pinned_);
      }, weak), T(u"Przypnij panel"), GlyphButton::Glyph::kSidebar,nullptr,footer_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),true,{chrome_node_graph_,4,{},"pin"}));
  Spacer(footer, 1);
  std::unique_ptr<views::ScrollBar> workspace_bar;
  workspace_bar=CreateSidebarScrollBar(footer_visible,true);
  PanelViewport* workspace_viewport=nullptr;
  auto* dot_scroll=footer->AddChildView(CreatePanelViewport(&workspace_viewport,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),{-1,-1,true,false,false,false,true},std::move(workspace_bar)));
  workspace_scroll_=workspace_viewport;
  dot_scroll->SetPreferredSize(gfx::Size(0,32));
  auto dots = std::make_unique<WorkspaceRail>(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak));
  dots->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  workspace_dots_=dots.get();
  workspace_scroll_->SetPanelContents(std::move(dots));
  workspace_dots_->SetLayoutManager(CreateSidebarDotsLayout(4));
  footer_layout->SetFlexForView(dot_scroll, 1);
  Spacer(footer, 1);
  workspace_settings_button_=footer->AddChildView(CreateSceneAction(
      base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings, weak),
      T(u"Ustawienia przestrzeni"), GlyphButton::Glyph::kVector, &vector_icons::kTuneIcon,footer_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),true,{chrome_node_graph_,4,{},"settings"}));
  footer->SetLayoutManager(CreateSidebarFooterLayout());

  // Command palette: input, suggestions, keyboard hints.
  auto palette=std::make_unique<PaletteView>(base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak));
  metal_palette_=CreateSidebarMetal();
  palette_root_ = palette.get();
  palette->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  palette->SetBorder(views::CreateEmptyBorder(1));
  auto chrome_visible=[weak](size_t index){return base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,size_t index){return shell&&shell->metal_palette_chrome_visible(index);},weak,index);};
  auto chrome_painted=base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak);
  auto palette_text=[&](views::View* parent,std::u16string text,uint32_t kind,[[maybe_unused]] size_t index)->views::View* {
    return parent->AddChildView(std::make_unique<PaletteText>(std::move(text),kind,chrome_painted,chrome_visible(index)));
  };
  auto* input_row = palette->AddChildView(std::make_unique<views::View>());
  auto* input_layout = input_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 18), 12));
  input_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  input_row->SetPreferredSize(gfx::Size(560, 56));
  std::unique_ptr<views::View> search_control;
  search_control=std::make_unique<SceneSearchIcon>(chrome_visible(0));
  auto* search_icon=input_row->AddChildView(std::move(search_control));
  palette_chrome_views_[0]=search_icon;
  search_icon->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  search_icon->SetCanProcessEventsWithinSubtree(false);
  address_ = AddEditor(input_row);
  address_->GetViewAccessibility().SetName(T(u"Szukaj lub wpisz adres"));
  address_->SetPlaceholderText(T(u"Szukaj lub wpisz adres"));
  address_->SetPresentation(17,0,false,Tone(root_,kCard));
  address_->SetCallbacks(base::BindRepeating(&WorkspaceShell::AddressEdited,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleAddressKey(event);},weak));
  palette->SetEditor(address_->view());input_layout->SetFlexForView(address_->view(),1);
  palette_chrome_views_[1]=palette_text(input_row,u"esc",0,1);
  palette_chrome_views_[1]->SetVisible(false);  // esc lives in the footer.
  if(metal_palette_){palette->SetLayoutManager(CreateSidebarColumnLayout());input_row->SetLayoutManager(CreatePaletteInputLayout());}
  address_error_=palette_text(palette.get(),u"",2,3);palette_chrome_views_[3]=address_error_;
  address_error_->SetVisible(false);
  std::unique_ptr<views::ScrollBar> palette_bar;
  if(metal_palette_)palette_bar=CreateSidebarScrollBar(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak));
  PanelViewport* palette_viewport=nullptr;
  palette->AddChildView(CreatePanelViewport(&palette_viewport,base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak),{0,300,metal_palette_!=nullptr,false,false,true},std::move(palette_bar)));
  suggestion_scroll_=palette_viewport;
  auto suggestion_list=std::make_unique<SidebarListView>(
      base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak));
  suggestion_list_=suggestion_list.get();
  suggestion_scroll_->SetPanelContents(std::move(suggestion_list));
  suggestion_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 2));
  suggestion_list_->SetBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1, 0, 0, 0), Hairline(root_)));
  if(metal_palette_) {
    suggestion_list_->SetLayoutManager(CreateSidebarColumnLayout(2));
    suggestion_list_->SetBorder(views::CreatePaddedBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1,0,0,0),Hairline(root_)),gfx::Insets(8)));
  }
  palette_chrome_views_[2]=palette_text(palette.get(),T(u"↑↓ wybierz, ↵ otwórz, esc zamknij"),1,2);

  profile_form_=std::make_unique<ProfileFormState>();
  std::array<std::u16string,11> profile_copy;
  {const auto c=sidebar::profile_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};profile_copy={text(c.title),text(c.hint),text(c.name_label),text(c.name_ax),text(c.placeholder),text(c.rename_ax),text(c.create),text(c.rename),text(c.cancel),text(c.remove_label),text(c.back)};}
  auto profiles_panel = std::make_unique<views::View>();
  profiles_root_ = profiles_panel.get();
  profiles_panel->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  // Header: title with the way back to the space it was opened from.
  auto* profile_header=profiles_panel->AddChildView(std::make_unique<views::View>());
  auto* profile_title=profile_header->AddChildView(std::make_unique<SceneParagraph>(profile_copy[0],3,true,kInk));
  profile_header->AddChildView(CreateSceneAction(base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings,weak),profile_copy[10],ControlGlyph::kVector,&vector_icons::kCloseIcon,{},{}));
  SetSidebarPanelLayout(profile_header,false,gfx::Insets(),8,profile_title,true);
  profiles_panel->AddChildView(std::make_unique<SceneParagraph>(profile_copy[1],-1,false,kMuted));
  // One row per profile; the rename and delete strips and the row status are
  // reordered beneath the selected row (LayoutProfileStrips).
  profile_list_=profiles_panel->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(profile_list_,true,gfx::Insets::TLBR(6,0,6,0),4);
  profile_rename_strip_=profile_list_->AddChildView(std::make_unique<views::View>());
  profile_rename_input_=AddEditor(profile_rename_strip_);
  profile_rename_input_->GetViewAccessibility().SetName(profile_copy[5]);
  profile_rename_input_->view()->SetPreferredSize(gfx::Size(160,32));
  // Icon actions keep their own size; a text action group would claim the whole
  // row and squeeze the field to nothing.
  rename_profile_=profile_rename_strip_->AddChildView(CreateSceneAction(base::BindRepeating(&WorkspaceShell::RenameSelectedProfile,weak),profile_copy[7],ControlGlyph::kVector,&views::kCheckIcon,{},{}));
  profile_rename_strip_->AddChildView(CreateSceneAction(base::BindRepeating(&WorkspaceShell::CancelProfileRow,weak),profile_copy[8],ControlGlyph::kVector,&vector_icons::kCloseIcon,{},{}));
  SetSidebarPanelLayout(profile_rename_strip_,false,gfx::Insets::TLBR(0,50,4,4),4,profile_rename_input_->view(),true);
  profile_rename_strip_->SetVisible(false);
  profile_confirm_strip_=profile_list_->AddChildView(std::make_unique<views::View>());
  profile_confirm_text_=profile_confirm_strip_->AddChildView(std::make_unique<SceneParagraph>(u"",-1,false,kInk));
  auto* confirm_actions=ActionGroup(profile_confirm_strip_,false,6);
  AddAction(confirm_actions,base::BindRepeating(&WorkspaceShell::CancelProfileRow,weak),profile_copy[8]);
  delete_profile_=AddAction(confirm_actions,base::BindRepeating(&WorkspaceShell::DeleteSelectedProfile,weak),profile_copy[9],ui::ButtonStyle::kProminent);
  if(auto* button=views::AsViewClass<SceneTextButton>(delete_profile_))button->SetDangerStrong();
  SetSidebarPanelLayout(profile_confirm_strip_,true,gfx::Insets::TLBR(0,50,4,0),8);
  profile_confirm_strip_->SetVisible(false);
  profile_status_=profile_list_->AddChildView(std::make_unique<SceneParagraph>(u"",-1,false,kSubtle));
  profile_status_->SetVisible(false);
  // Footer: a new profile.
  auto* profile_footer=profiles_panel->AddChildView(std::make_unique<views::View>());
  profile_footer->SetBorder(views::CreatePaddedBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1,0,0,0),Hairline(root_)),gfx::Insets::TLBR(14,0,0,0)));
  profile_footer->AddChildView(std::make_unique<SceneParagraph>(profile_copy[2],-1,true,kSubtle));
  auto* create_line=profile_footer->AddChildView(std::make_unique<views::View>());
  profile_name_input_ = AddEditor(create_line);
  profile_name_input_->GetViewAccessibility().SetName(profile_copy[3]);
  profile_name_input_->SetPlaceholderText(profile_copy[4]);
  profile_name_input_->view()->SetPreferredSize(gfx::Size(200,34));
  auto* profile_create_actions=ActionGroup(create_line,false,6);
  create_profile_ = AddAction(profile_create_actions,
      base::BindRepeating(&WorkspaceShell::SubmitProfile, weak), profile_copy[6], ui::ButtonStyle::kProminent);
  // Pin the group to its button so the field keeps the rest of the row.
  profile_create_actions->SetPreferredSize(profile_create_actions->GetPreferredSize({}));
  SetSidebarPanelLayout(create_line,false,gfx::Insets(),8,profile_name_input_->view(),true);
  profile_create_status_=profile_footer->AddChildView(std::make_unique<SceneParagraph>(u"",-1,false,kSubtle));
  profile_create_status_->SetVisible(false);
  SetSidebarPanelLayout(profile_footer,true,gfx::Insets(),8);
  profile_name_input_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleProfileEditorKey(false,event);},weak));
  profile_rename_input_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleProfileEditorKey(true,event);},weak));
  BindFormCommand(create_profile_,sidebar::FormCommand::ProfileCreate);BindFormCommand(delete_profile_,sidebar::FormCommand::ProfileDelete);

  profiles_panel->SetLayoutManager(CreateSidebarColumnLayout(10));profiles_panel->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(20)));

  import_form_=std::make_unique<ImportFormState>();
  std::array<std::u16string,10> import_copy={T(u"Przenieś się do Zittera"),T(u"Zamknij import"),T(u"Wybierz przeglądarkę i dane, które chcesz przenieść."),T(u"Przeglądarka źródłowa"),T(u"Przeglądarka do importu"),T(u"Co chcesz przenieść?"),T(u"Oddzielne profile przeglądania"),T(u"Historia przeglądania"),T(u"Ciasteczka i sesje witryn"),T(u"Importuj")};
  std::vector<std::u16string> import_browsers={u"Arc",u"Google Chrome",u"Microsoft Edge",u"Brave"};
  {const auto c=sidebar::import_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};import_copy={text(c.title),text(c.dismiss),text(c.intro),text(c.source_label),text(c.source_ax),text(c.heading),text(c.profiles),text(c.history),text(c.cookies),text(c.apply_label)};import_browsers.clear();for(const auto& b:c.browsers)import_browsers.push_back(text(b));}
  auto import_text=[&](views::View* parent,std::u16string text,SkColor ink,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false)->views::View* {
    return parent->AddChildView(std::make_unique<SceneParagraph>(std::move(text),delta,weight!=gfx::Font::Weight::NORMAL,ink));
  };
  auto import_panel = std::make_unique<views::View>();
  import_root_ = import_panel.get();
  import_panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 12));
  import_panel->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  import_panel->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto* import_header = import_panel->AddChildView(std::make_unique<views::View>());
  auto* import_title = import_text(import_header,import_copy[0],kInk,4,gfx::Font::Weight::SEMIBOLD);
  SetSidebarPanelLayout(import_header, false, gfx::Insets(), 8, import_title);
  auto dismiss_import = base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (!shell || shell->import_applying_) return;
    shell->callbacks_.dismiss_import.Run();
    shell->CloseOverlays();
    shell->RestorePageFocus();
  }, weak);
  auto* import_controls=import_header->AddChildView(std::make_unique<ScenePreferenceControls>(true));dismiss_import_=import_controls->AddControl({},import_copy[1]);BindFormCommand(dismiss_import_,sidebar::FormCommand::ImportDismiss);
  import_text(import_panel.get(),import_copy[2],kMuted,-1,gfx::Font::Weight::NORMAL,true);
  import_text(import_panel.get(),import_copy[3],kSubtle,-1);
  auto source_changed=base::BindRepeating(&WorkspaceShell::ImportSelectionChanged,weak);
  import_browser_picker_=import_panel->AddChildView(MakeParentSceneControl<SceneSelect>(import_panel.get(),import_browsers,import_copy[4],source_changed));
  Spacer(import_panel.get(), 8);
  import_text(import_panel.get(),import_copy[5],kInk,0,gfx::Font::Weight::SEMIBOLD);
  auto selection_changed = base::BindRepeating(&WorkspaceShell::ImportSelectionChanged, weak);
  views::View* import_choices=import_panel.get();
  import_choices=import_panel->AddChildView(std::make_unique<SceneCheckboxGroup>(12));
  import_profiles_ = AddImportCheck(import_choices,import_copy[6],selection_changed);
  import_bookmarks_ = AddImportCheck(import_choices,T(u"Przypięte karty, foldery i Ulubione"),selection_changed);
  for (auto* checkbox : {import_profiles_.get(), import_bookmarks_.get()}) {
    SetCheckValue(checkbox,true);
  }
  import_history_ = AddImportCheck(import_choices,import_copy[7],selection_changed);
  import_cookies_ = AddImportCheck(import_choices,import_copy[8],selection_changed);
  Spacer(import_panel.get(), 8);
  import_status_ = import_text(import_panel.get(),u"",kSubtle,-1,gfx::Font::Weight::NORMAL,true);
  import_status_->SetVisible(false);
  auto* import_actions=ActionGroup(import_panel.get(),true,0);
  apply_import_ = AddAction(import_actions,base::BindRepeating(&WorkspaceShell::SubmitImport,weak),import_copy[9]);
  BindFormCommand(apply_import_,sidebar::FormCommand::ImportApply);
  apply_import_->SetEnabled(false);
  {const auto d=sidebar::import_defaults();SetCheckValue(import_profiles_,d.separate_profiles);SetCheckValue(import_bookmarks_,d.bookmarks);SetCheckValue(import_history_,d.history);SetCheckValue(import_cookies_,d.cookies);import_panel->SetLayoutManager(CreateSidebarColumnLayout(12));import_panel->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(20)));}


  auto general = std::make_unique<views::View>();
  general_root_ = general.get();
  preferences_chrome_=std::make_unique<PreferenceChromeState>(general_root_);
  auto preference_chrome_painted=base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak);
  auto preference_chrome_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak);
  auto copy=[&](views::View* parent,std::u16string text,SkColor tone,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false){return AddPreferenceCopy(parent,std::move(text),tone,delta,static_cast<int>(weight),multiline);};
  auto* navigation = general->AddChildView(std::make_unique<PreferenceSurface>(true,preference_chrome_painted,preference_chrome_visible));
  preferences_navigation_ = navigation;
  navigation->SetPreferredSize(gfx::Size(212, 1));
  copy(navigation, u"zitter", kInk, 8, gfx::Font::Weight::BOLD);
  copy(navigation, T(u"TWOJA PRZEGLĄDARKA"), kMuted, -3, gfx::Font::Weight::MEDIUM);
  Spacer(navigation, 20);
  auto* main = general->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(general.get(),false,gfx::Insets(8),8,main);
  auto* preferences_heading = main->AddChildView(std::make_unique<views::View>());
  auto* heading_text = preferences_heading->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(heading_text,true,gfx::Insets(),5);
  if(auto* title=views::AsViewClass<PreferenceCopy>(copy(heading_text, T(u"Preferencje"), kInk, 9, gfx::Font::Weight::SEMIBOLD)))title->SetDisplay();
  copy(heading_text, T(u"Twoja przestrzeń. Twoje zasady."), kMuted, 0);
  SetSidebarPanelLayout(preferences_heading,false,gfx::Insets(),12,heading_text);
  auto dismiss=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell){shell->CloseOverlays();shell->RestorePageFocus();}},weak);
  {auto* controls=preferences_heading->AddChildView(std::make_unique<ScenePreferenceControls>(true));preferences_close_=controls->AddControl({},T(u"Zamknij preferencje (Escape)"));BindFormCommand(preferences_close_,sidebar::FormCommand::PreferencesDismiss);}
  preferences_search_ = AddEditor(main);
  preferences_search_->SetPlaceholderText(T(u"Szukaj ustawień"));
  preferences_search_->GetViewAccessibility().SetName(T(u"Szukaj we wszystkich ustawieniach"));
  preferences_search_->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(shell&&shell->general_widget_->IsVisible()&&!shell->preferences_search_->IsIMEComposing()){
      shell->ResetPreferenceRuleLimit();shell->FilterPreferences();
    }
  },weak),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){
    return shell&&shell->HandlePreferenceEditorKey(field,event);
  },weak,base::Unretained(preferences_search_.get())));
  preferences_search_->view()->SetPreferredSize(gfx::Size(240, 38));
  preferences_profile_ = copy(main, u"",kMuted,-1,gfx::Font::Weight::NORMAL,true);
  PanelViewport* preference_viewport=nullptr;
  auto general_scroll=CreatePanelViewport(&preference_viewport,base::BindRepeating(&WorkspaceShell::RefreshPreferenceViewport,weak));general_scroll_=preference_viewport;
  auto body = std::make_unique<views::View>();
  preferences_body_ = body.get();
  SetSidebarPanelLayout(body.get(),true,gfx::Insets::TLBR(0,0,8,8),16);
  std::vector<std::u16string> categories = {
      T(u"Wygląd"), T(u"Wyszukiwanie"), T(u"Prywatność"), T(u"Uprawnienia stron"), T(u"Treści i czytelność"), T(u"Hasła i profile"), T(u"Pobieranie"), T(u"Języki i pisownia"), T(u"Dane przeglądania"), T(u"Zaawansowane")};
  std::vector<std::u16string> descriptions = {
      T(u"Dopasuj Zitter do swojego rytmu."),
      T(u"Wybierz, gdzie zaczyna się każde wyszukiwanie."),
      T(u"Zdecyduj, jakie ślady zostawiasz w sieci."),
      T(u"Ustal domyślny dostęp witryn do urządzenia."),
      T(u"Wygodne czytanie i kontrola zawartości stron."),
      T(u"Oddzielne logowania, wspólne możliwości."), T(u"Wybierz miejsce dla pobieranych plików."),
      T(u"Wybierz języki witryn i pomoc podczas pisania."),
      T(u"Kontroluj dane zapisane w bieżącym profilu."),
      T(u"Dostępność, połączenia i ustawienia silnika Chromium.")};
  std::optional<sidebar::PreferenceCatalogue> preference_catalogue;
  {
    preference_catalogue=sidebar::preference_catalogue(true);
    auto strings=[](const auto& input){std::vector<std::u16string> output;for(const auto& value:input)output.push_back(base::UTF8ToUTF16(std::string(value)));return output;};
    categories=strings(preference_catalogue->categories);descriptions=strings(preference_catalogue->descriptions);
  }
  preferences_category_picker_=ChoicePicker(main,T(u"Kategoria preferencji"),categories);
  preferences_category_picker_->SetVisible(false);
  SetPickerCallback(preferences_category_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if(shell)shell->SelectPreferenceCategory(PickerIndex(shell->preferences_category_picker_).value_or(0));
  },weak));
  // This UI-only controller retains category selection across view rebuilds.
  if(!preferences_form_)preferences_form_=std::make_unique<PreferencesFormState>(categories.size());
  auto* category_controls=navigation->AddChildView(std::make_unique<ScenePreferenceControls>(false));
  for (size_t i = 0; i < categories.size(); ++i) {
    auto* button=category_controls->AddControl({},categories[i]);BindPreferenceCategory(button,i);
    auto* section = body->AddChildView(std::make_unique<views::View>());
    SetSidebarPanelLayout(section,true,gfx::Insets(),12);
    copy(section, categories[i],kInk,4,gfx::Font::Weight::SEMIBOLD);
    copy(section,descriptions[i],kMuted,-1,gfx::Font::Weight::NORMAL,true);
    preference_sections_.push_back({section, button, {}});
  }
  auto* nav_space = Spacer(navigation, 1);
  SetSidebarPanelLayout(navigation,true,gfx::Insets(16),6,nav_space);
  copy(navigation, T(u"ZITTER / PREFERENCJE"), kMuted, -3);
  BuildRustPreferenceRows(*preference_catalogue);
  preferences_empty_ = copy(body.get(), T(u"Brak pasujących ustawień. Spróbuj innego hasła."), kMuted,0,gfx::Font::Weight::NORMAL,true);
  general_scroll_->SetPanelContents(std::move(body));
  auto* main_scroll=main->AddChildView(std::move(general_scroll));
  SetSidebarPanelLayout(main,true,gfx::Insets::VH(16,20),12,main_scroll);
  general_status_ = copy(main, T(u"Przełączniki zapisują się automatycznie. Pola tekstowe zatwierdź przyciskiem Zapisz lub Enter."), kMuted,-2,gfx::Font::Weight::NORMAL,true);
  FilterPreferences();
  auto peek = std::make_unique<views::View>();
  peek->SetBackground(views::CreateRoundedRectBackground(kCard,12));peek->SetBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)));
  std::array<views::Button::PressedCallback,5> peek_actions;
  peek_actions[0]=base::BindRepeating(callbacks_.page_command,PageCommand::kBack);peek_actions[1]=base::BindRepeating(callbacks_.page_command,PageCommand::kForward);
  for(bool split:{false,true})peek_actions[split?3:2]=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,bool split){
    if(!shell)return;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,bool split){if(shell)shell->callbacks_.promote_peek.Run(split);},shell,split));
  },weak,split);
  peek_actions[4]=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell)return;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->callbacks_.close_peek.Run();},shell));},weak);
  {auto toolbar=std::make_unique<ScenePeekToolbar>(std::move(peek_actions));peek_title_=toolbar->title();peek_toolbar_=peek->AddChildView(std::move(toolbar));}
  peek_host_=peek->AddChildView(std::make_unique<views::View>());peek_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  SetSidebarPanelLayout(peek.get(),true,gfx::Insets(8),8,peek_host_);

  auto tools_panel = std::make_unique<views::View>();
  tools_root_ = tools_panel.get();
  tools_chrome_=std::make_unique<PreferenceChromeState>(tools_root_);
  auto* tools_heading = tools_panel->AddChildView(std::make_unique<views::View>());
  tools_title_ = AddToolsCopy(tools_heading,u"Time Machine",kInk,5,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
  auto dismiss_tools=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(shell){shell->SetShortcutRecording(false);shell->tools_widget_->Hide();shell->RestorePageFocus();}
  },weak);
  {auto* controls=tools_heading->AddChildView(std::make_unique<ScenePreferenceControls>(true));tools_close_=controls->AddControl({},T(u"Zamknij narzędzia"));BindFormCommand(tools_close_,sidebar::FormCommand::ToolsDismiss);}
  SetSidebarPanelLayout(tools_heading,false,gfx::Insets(),8,tools_title_);
  PanelViewport* tools_viewport=nullptr;auto tools_scroll=CreatePanelViewport(&tools_viewport,base::BindRepeating(&WorkspaceShell::RefreshToolsViewport,weak));tools_scroll_=tools_viewport;
  auto tools_body = std::make_unique<views::View>(); tools_body_ = tools_body.get();
  SetSidebarPanelLayout(tools_body_,true,gfx::Insets::TLBR(0,0,8,8),10);
  tools_scroll_->SetPanelContents(std::move(tools_body)); tools_panel->AddChildView(std::move(tools_scroll));
  tools_status_ = AddToolsCopy(tools_panel.get(),u"",kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);
  SetSidebarPanelLayout(tools_panel.get(),true,gfx::Insets(20),12,tools_scroll_->view());

  auto make_panel = [&](std::unique_ptr<views::View> contents,
                        std::unique_ptr<views::WidgetDelegate>& delegate,
                        std::unique_ptr<views::Widget>& panel, std::u16string title,
                        bool activatable = true) {
    delegate = std::make_unique<views::WidgetDelegate>();
    delegate->SetTitle(title);
    delegate->SetContentsView(std::move(contents));
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                     views::Widget::InitParams::TYPE_BUBBLE);
    params.delegate = delegate.get();
    params.SetParent(widget_);
    // A key panel retargets keystrokes to itself (workspace_keyboard_mac.mm),
    // so the always-visible sidebar, which has no text input, never takes key.
    if (!activatable) params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.rounded_corners = gfx::RoundedCornersF(12);
    panel = std::make_unique<views::Widget>();
    panel->Init(std::move(params));

    PreparePanelText(panel->GetRootView());
    panel->AddObserver(this);
    RegisterKeys(panel.get(), target, tools_, true);
  };
  make_panel(std::move(sidebar), sidebar_delegate_, sidebar_widget_, T(u"Zitter — Karty"), false);
  make_panel(std::move(palette), address_delegate_, address_widget_, T(u"Zitter — Paleta poleceń"));
  PanelViewport* settings_viewport=nullptr;
  auto settings_scroll=CreatePanelViewport(&settings_viewport,base::BindRepeating(&WorkspaceShell::RefreshFormViewports,weak),{0,10000,false});
  settings_scroll_=settings_viewport;settings_viewport->SetPanelContents(std::move(settings));
  make_panel(std::move(settings_scroll), settings_delegate_, settings_widget_, T(u"Zitter — Ustawienia przestrzeni"));
  make_panel(std::move(profiles_panel), profiles_delegate_, profiles_widget_, T(u"Zitter — Profile"));
  PanelViewport* import_viewport=nullptr;
  auto import_scroll=CreatePanelViewport(&import_viewport,base::BindRepeating(&WorkspaceShell::RefreshFormViewports,weak),{0,10000,false});
  import_scroll_=import_viewport;import_viewport->SetPanelContents(std::move(import_panel));
  make_panel(std::move(import_scroll), import_delegate_, import_widget_, T(u"Zitter — Import danych"));
  make_panel(std::move(peek), peek_delegate_, peek_widget_, u"Zitter — Peek");
  make_panel(std::move(general), general_delegate_, general_widget_, T(u"Zitter — Ustawienia"));
  make_panel(std::move(tools_panel), tools_delegate_, tools_widget_, T(u"Zitter — Narzędzia"));
  {auto toast=std::make_unique<ToastView>(base::BindRepeating(&WorkspaceShell::UndoToast,weak),base::BindRepeating(&WorkspaceShell::HideToast,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,bool hovered){if(!shell)return;if(hovered)shell->toast_timer_.Stop();else if(shell->toast_widget_->IsVisible())shell->toast_timer_.Start(FROM_HERE,base::Seconds(6),base::BindOnce(&WorkspaceShell::HideToast,shell));},weak));
   toast_=toast.get();make_panel(std::move(toast), toast_delegate_, toast_widget_, T(u"Zitter — Komunikat"), false);}
  make_panel(std::make_unique<ScrimView>(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->CloseOverlays(true);},weak)),
             scrim_delegate_, scrim_widget_, u"Zitter", false);
  PositionPanels();
  ApplyAppearance();
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path) {
    std::string data; base::ReadFileToString(path, &data);
    auto dict = data.size() <= 1024 * 1024 ? base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC) : std::nullopt;
    return dict ? BrowserTools::FromValue(*dict) : BrowserTools();
  }, settings_path_.DirName().AppendASCII("ZitterTools.json")), base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, BrowserTools loaded) {
    if (!shell) return;
    if (!shell->keys_dirty_) shell->tools_.shortcuts = std::move(loaded.shortcuts);
    if (!shell->engines_dirty_) { shell->tools_.providers = std::move(loaded.providers); shell->tools_.default_keyword = std::move(loaded.default_keyword); }
    shell->tools_ready_ = true; shell->RefreshKeyRegistrations();
    if (shell->tools_widget_->IsVisible()) shell->RefreshTools();
  }, weak));
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(ReadAppearance, settings_path_),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, Appearance appearance) {
        if (!shell) return;
        for (const auto& [id, color] : appearance.colors)
          if (!shell->color_dirty_.contains(id)) shell->colors_[id] = color;
        if (!shell->pin_dirty_) shell->pinned_ = appearance.pinned;
        if (!shell->general_dirty_.contains("theme")) shell->theme_mode_ = appearance.theme;
        if (!shell->general_dirty_.contains("search")) shell->search_engine_ = appearance.search;
        if (!shell->general_dirty_.contains("animations")) shell->workspace_animations_ = appearance.animations;
        if (!shell->general_dirty_.contains("workspace_names")) shell->workspace_names_ = appearance.workspace_names;
        if (!shell->general_dirty_.contains("folders_collapsed")) {shell->folders_collapsed_ = appearance.folders_collapsed;shell->sidebar_tree_.SetDefaultCollapsed(appearance.folders_collapsed);}
        if (!shell->sidebar_width_dirty_) shell->sidebar_width_ = appearance.sidebar_width;
        for (const auto& [id, emoji] : appearance.emoji)
          if (!shell->emoji_dirty_.contains(id)) shell->emoji_[id] = base::UTF8ToUTF16(emoji);
        shell->RebuildWorkspaceDots();
        shell->RefreshWorkspaceHeading();
        shell->appearance_ready_ = true;
        shell->ApplyAppearance();
        // The pinned sidebar already appeared with the window; reorder panels
        // only if the saved state differs, never as a side effect of loading.
        if (shell->pinned_ && !shell->sidebar_visible()) shell->ShowSidebar();
        if (!shell->pinned_ && shell->sidebar_visible()) shell->sidebar_widget_->Hide();
        // Development: --zitter-open-panel=space|sidebar|palette|address|settings|profiles|downloads|toast opens
        // that view once the window is up, so UI checks can screenshot it without
        // driving the mouse.
        if (const std::string panel = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-open-panel"); !panel.empty())
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
              base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, std::string panel) {
                if (!shell) return;
                if (panel == "space") shell->OpenWorkspaceSettings();
                else if (panel == "sidebar") shell->ShowSidebar();
                else if (panel == "palette") shell->OpenCommandPalette();
                else if (panel == "address") shell->OpenAddress(false);
                else if (panel == "settings") shell->OpenGeneralSettings();
                else if (panel == "profiles") shell->OpenProfiles();
                // A sample undo toast; its „Cofnij” does nothing.
                else if (panel == "downloads") shell->callbacks_.run_shortcut.Run("downloads");
                else if (panel == "toast") shell->ShowToast(T(u"Usunięto folder "), u"Projekty", base::DoNothing());
              }, shell->weak_factory_.GetWeakPtr(), panel),
              base::Seconds(1));
      }, weak));
}

void WorkspaceShell::Detach() {
  preferences_chrome_.reset();tools_chrome_.reset();
  for(auto& surface:metal_palette_chrome_)surface.reset();palette_chrome_views_.fill(nullptr);
  metal_palette_.reset();metal_palette_pending_=false;
  metal_sidebar_grip_.reset();
  for(auto& surface:metal_split_)surface.reset();metal_split_pending_=false;
  metal_header_.reset();metal_header_pending_=false;header_view_=nullptr;header_sidebar_button_=nullptr;
  metal_sidebar_.reset();
  metal_sidebar_pending_ = false;
  CancelWorkspaceSwipe();
  StopWorkspaceAnimation();
  sidebar_warm_timer_.Stop();
  if (sidebar_root_) sidebar_root_->RemovePreTargetHandler(this);
  history_timer_.Stop();
  import_preview_timer_.Stop();
  if (folder_dialog_) folder_dialog_->CloseNow();
  rust_folder_dialog_.reset();rust_folder_dialog_delegate_.reset();

  link_menu_runner_.reset(); link_menu_.reset(); route_menu_.reset();
  workspace_menu_runner_.reset();
  workspace_menu_.reset();
  profile_menu_.reset();
  weak_factory_.InvalidateWeakPtrs();
  if (!widget_) return;
  accelerator_target_ = nullptr;
  ResetToolsForm();
  new_tab_button_ = nullptr;
  widget_->RemoveObserver(this);
  for (auto* panel : {sidebar_widget_.get(), address_widget_.get(), settings_widget_.get(), profiles_widget_.get(), import_widget_.get(), peek_widget_.get(), general_widget_.get(), tools_widget_.get()})
    panel->RemoveObserver(this);
  search_timer_.Stop();
  for(auto* field:{snapshot_name_.get(),timeline_filter_.get(),shortcut_chord_.get(),provider_name_.get(),provider_keyword_.get(),provider_url_.get(),history_search_.get()})
    if(field)field->SetCallbacks({},{});
  address_->SetCallbacks({},{});
  emoji_input_->SetCallbacks({},{});
  preferences_search_->SetCallbacks({},{});
  site_origin_->SetCallbacks({},{});
  for(const auto& [key,field]:preference_textfields_)field->SetCallbacks({},{});
  import_scroll_ = nullptr;
  import_root_ = nullptr;
  import_status_ = nullptr;import_form_.reset();
  import_browser_picker_ = nullptr;
  import_bookmarks_ = nullptr;
  import_profiles_ = nullptr;
  import_history_ = nullptr;
  import_cookies_ = nullptr;
  apply_import_ = nullptr;
  dismiss_import_ = nullptr;
  profiles_root_ = nullptr;
  profile_list_ = nullptr; profile_rows_.clear(); profile_row_ids_.clear();
  profile_rename_strip_ = nullptr; profile_rename_input_ = nullptr; profile_confirm_strip_ = nullptr; profile_confirm_text_ = nullptr; profile_create_status_ = nullptr;
  profile_name_input_ = nullptr;
  create_profile_ = nullptr;
  rename_profile_ = nullptr;
  delete_profile_ = nullptr;
  profile_status_ = nullptr;profile_form_.reset();
  address_ = nullptr;
  color_presets_ = nullptr;
  emoji_input_ = nullptr;
  sidebar_root_ = nullptr;
  palette_root_ = nullptr;
  tab_list_ = nullptr;
  tab_scroll_ = nullptr;
  suggestion_list_ = nullptr;
  suggestion_scroll_ = nullptr;
  url_pill_ = nullptr;
  workspace_name_ = nullptr;
  profile_name_ = nullptr;
  settings_title_ = nullptr; settings_icon_ = nullptr;
  workspace_appearance_ = nullptr; workspace_more_ = nullptr;
  settings_scroll_ = nullptr;
  settings_root_ = nullptr;
  chrome_node_graph_.reset();history_node_graph_.reset();palette_node_graph_.reset();site_rule_node_graph_.reset();
  pin_button_ = nullptr;
  nav_buttons_.fill(nullptr);
  workspace_buttons_.clear();
  workspace_dots_ = nullptr; sidebar_footer_=nullptr; workspace_scroll_=nullptr; workspace_settings_button_=nullptr;
  tab_lists_.clear();
  profile_picker_ = nullptr;
  workspace_input_ = nullptr;
  add_workspace_ = nullptr;
  edit_workspace_ = nullptr;
  remove_workspace_ = nullptr;
  configuration_error_ = nullptr;workspace_form_.reset();
  address_error_ = nullptr;
  color_error_ = nullptr;
  if(preferences_form_)preferences_form_->form->preference_detach();
  preference_textfields_.clear(); preference_save_buttons_.clear(); preference_text_dirty_.clear();
  site_origin_ = nullptr; site_type_ = nullptr; site_policy_ = nullptr; site_rules_list_ = nullptr; site_rules_card_ = nullptr;
  clear_data_period_ = nullptr; clear_history_ = nullptr; clear_cookies_ = nullptr; clear_cache_ = nullptr; clear_data_button_ = nullptr;
  site_preference_rules_.clear();
  preference_sections_.clear(); preference_toggles_.clear(); preference_pickers_.clear();
  download_directory_label_ = nullptr; download_directory_button_ = nullptr;
  preferences_navigation_ = nullptr; preferences_category_picker_ = nullptr;preferences_close_=nullptr;
  preferences_search_ = nullptr; preferences_profile_ = nullptr; preferences_empty_ = nullptr; preferences_body_ = nullptr;
  general_root_ = nullptr; general_scroll_ = nullptr; general_status_ = nullptr;
  theme_picker_ = nullptr; search_engine_picker_ = nullptr;
  sidebar_check_ = nullptr; animations_check_ = nullptr; workspace_names_check_ = nullptr; folders_collapsed_check_ = nullptr; language_picker_ = nullptr;
  tools_root_ = nullptr; tools_body_ = nullptr; tools_close_=nullptr; tools_title_ = nullptr; tools_status_ = nullptr;
  tools_scroll_ = nullptr; tools_picker_ = nullptr; closed_picker_ = nullptr;
  snapshot_name_ = nullptr; timeline_filter_ = nullptr; shortcut_chord_ = nullptr;
  provider_name_ = nullptr; provider_keyword_ = nullptr; provider_url_ = nullptr;
  history_search_ = nullptr; history_period_ = nullptr; history_list_ = nullptr;
  history_more_ = nullptr; history_clear_ = nullptr;
  tools_widget_.reset(); tools_delegate_.reset();
  toast_timer_.Stop(); toast_tremor_timer_.Stop(); toast_undo_.Reset(); toast_ = nullptr; toast_widget_.reset(); toast_delegate_.reset();
  scrim_widget_.reset(); scrim_delegate_.reset();
  general_widget_.reset(); general_delegate_.reset();
  peek_host_ = nullptr; peek_title_ = nullptr;peek_toolbar_=nullptr;
  peek_widget_.reset(); peek_delegate_.reset();
  import_widget_.reset();
  import_delegate_.reset();
  profiles_widget_.reset();
  profiles_delegate_.reset();
  settings_widget_.reset();
  address_widget_.reset();
  sidebar_widget_.reset();
  settings_delegate_.reset();
  address_delegate_.reset();
  sidebar_delegate_.reset();
  widget_ = nullptr;
  root_ = nullptr;
  content_host_ = nullptr;
  edge_ = nullptr;
}

void WorkspaceShell::Update(ShellState state) {
  if (state_.profile_id != state.profile_id || state_.workspace_id != state.workspace_id) {
    search_timer_.Stop(); search_results_.clear();
  }
  bool changed = state.workspaces.size() != state_.workspaces.size() || state.sleeping_workspaces != state_.sleeping_workspaces;
  if (!changed) {
    for (size_t i = 0; i < state.workspaces.size(); ++i) {
      const auto& a = state.workspaces[i];
      const auto& b = state_.workspaces[i];
      if (a.id != b.id || a.name != b.name || a.profile_id != b.profile_id) changed = true;
    }
  }
  const bool ready_changed = state.configuration_ready != state_.configuration_ready || state.profiles_busy != state_.profiles_busy;
  const bool added_profile = state.profiles.size() > state_.profiles.size();
  const bool profiles_changed = state.profiles != state_.profiles;
  const auto previous_workspace = state_.workspace_id;
  if (swipe_overlay_ && (changed || (state.workspace_id != previous_workspace &&
      (!swipe_committing_ || state.workspace_id != swipe_commit_target_)))) CancelWorkspaceSwipe();
  const bool selected = state.workspace_id != previous_workspace;
  const bool history_profile_changed = state.profile_id != state_.profile_id;
  state_ = std::move(state);
  if (!widget_) return;
  // A removal that first asked a page (beforeunload) gets its toast once done.
  if (pending_removal_ && std::ranges::none_of(state_.workspaces, [&](const auto& w) { return w.id == pending_removal_->first; })) {
    auto [id, name] = std::move(*pending_removal_); pending_removal_.reset();
    ShowWorkspaceRemovedToast(std::move(id), std::move(name));
  }
  if(selected&&workspace_form_){for(auto* view:{color_presets_.get(),emoji_input_->view()->parent()})if(auto* row=views::AsViewClass<AppearanceRow>(view))row->CancelInput();CancelSceneActions(settings_root_);CancelSceneActions(profiles_root_);CancelSceneActions(general_root_);}
  if(profiles_changed&&profile_form_)CancelSceneActions(profiles_root_);
  if((selected||history_profile_changed)&&tools_root_)CancelChoiceInteractions(tools_root_);
  if (history_profile_changed && history_visible()) RefreshTools();
  bool refresh_dots=changed||profiles_changed;
  refresh_dots=refresh_dots||(chrome_node_graph_&&(selected||ready_changed));
  if(refresh_dots)RebuildWorkspaceDots();
  for (const auto& [id, button] : workspace_buttons_) {
    SetActionOn(button,id == state_.workspace_id);
    button->SetEnabled(state_.configuration_ready);
  }
  if (profiles_changed) {
    ReplaceProfilePickerOptions(profile_picker_,state_.profiles,false,false);
    if(profile_form_){const auto profiles=ProfileOptions(state_);profile_form_->form->reconcile(rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),added_profile);}
    renaming_profile_id_.clear();
  }
  if (changed || profiles_changed || selected) RebuildProfileRows();
  if(import_form_){RefreshRustImportControls();if(ready_changed&&import_visible()&&import_form_->form->should_resume(state_.configuration_ready,state_.profiles_busy))ImportSelectionChanged();}
  profile_picker_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  if (changed || profiles_changed || ready_changed) RefreshProfileControls();
  add_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  edit_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  remove_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy && state_.workspaces.size() > 1);
  if ((changed || selected || profiles_changed) && settings_visible()) RefreshWorkspaceSettings();
  if (general_widget_->IsVisible()) RefreshGeneralControls();
  RefreshWorkspaceHeading();
  SetPanelText(settings_title_,creating_workspace_ ? T(u"Nowa przestrzeń") : state_.workspace_name);
  SetPanelText(profile_name_,TF(u"Profil: {}", {state_.profile_name}));
  RefreshRustWorkspacePresentation();
  const bool blank = !state_.url.is_valid() || state_.url.path() == "/start.html";
  const std::string path = state_.url.path() == "/" ? std::string() : std::string(state_.url.path());
  SetRowAddress(url_pill_.get(),
      blank ? T(u"Szukaj lub wpisz adres") : HostLabel(state_.url),
      blank ? u"" : base::UTF8ToUTF16(path), blank, !blank && state_.url.SchemeIs("https"));
  for (size_t pane = 0; pane < kMaxSplitPanes; ++pane) {
    std::u16string title;
    std::array<uint64_t,2> identity{};
    std::optional<size_t> member;
    if (pane < state_.split_tabs.size()) member = state_.split_tabs[pane];
    // SplitView: each pane is labelled by its host in `url` (the page title when there is none).
    for (const auto& tab : state_.tabs) if (member == tab.index){const auto host=HostLabel(tab.url);title=host.empty()?tab.title:host;identity=tab.identity;}
    auto* bar=static_cast<PaneHost*>(content_host_.get())->bar(pane);
    bar->SetTarget(state_.workspace_id,member,identity,state_.split_visible&&member.has_value());
    bar->SetTitle(title,state_.active_pane==pane);
  }
  nav_buttons_[0]->SetEnabled(state_.can_go_back);
  nav_buttons_[1]->SetEnabled(state_.can_go_forward);
  if (selected) ApplyAppearance(false);
  RebuildTabs(previous_workspace, changed);
}

void WorkspaceShell::ReconcileSidebarNodes(TabListCache& cache,std::string_view workspace,const std::vector<PinnedItem>& pins,const std::vector<ShellTab>& tabs,const std::vector<size_t>& split_tabs,bool split){
  // Titles, URLs and active selection do not alter logical node membership.
  // Keep the fast refresh path allocation-free when identities/order are equal.
  if(cache.node_graph&&cache.pins==pins&&cache.split_tabs==split_tabs&&cache.split==split&&cache.tabs.size()==tabs.size()&&
      std::equal(cache.tabs.begin(),cache.tabs.end(),tabs.begin(),[](const auto& a,const auto& b){return a.identity==b.identity&&a.index==b.index&&a.pin_id==b.pin_id;}))return;
  if(!cache.node_graph)cache.node_graph=std::make_shared<SidebarNodeGraphState>();
  rust::Vec<sidebar::SidebarPinInput> pin_ids;for(const auto& pin:pins){const auto title=base::UTF8ToUTF16(pin.title);const auto monogram=pin.folder?std::u16string():base::i18n::ToUpper(title.substr(0,1));const auto color=pin.folder?SK_ColorTRANSPARENT:kMonogramColors[base::FastHash(pin.url.host())%kMonogramColors.size()];pin_ids.push_back({pin.id,pin.parent_id,SceneUtf16(title),SceneUtf16(monogram),color,pin.folder,pin.folder&&sidebar_tree_.Collapsed(workspace,pin.id),sidebar_tree_.Revealed(workspace,pin.id)});}rust::Vec<sidebar::SidebarTabInput> tab_ids;for(const auto& tab:tabs)tab_ids.push_back({tab.identity[0],tab.identity[1],tab.index,tab.pin_id});
  // Rust owns the split group node, its pin anchor/parent and member visibility.
  rust::Vec<size_t> members;for(size_t member:split_tabs)members.push_back(member);
  const auto plan=cache.node_graph->graph->reconcile_sidebar_layout(std::move(pin_ids),std::move(tab_ids),
      std::move(members),split);
  CHECK(plan.admitted)<<"Invalid sidebar logical node projection";
}
void WorkspaceShell::RebuildTabs(std::string_view previous_workspace, bool catalogue_changed) {
  if (state_.workspace_id.empty()) return;
  // A pin opened elsewhere (Command Center, keyboard) shows under its collapsed
  // folders on its own; the folders stay collapsed.
  bool revealed = false;
  if (state_.active_pin != revealed_for_pin_) {
    revealed_for_pin_ = state_.active_pin;
    for (std::string id = state_.active_pin; !id.empty();) {
      const auto pin = std::ranges::find(state_.pins, id, &PinnedItem::id);
      if (pin == state_.pins.end() || pin->parent_id.empty()) break;
      if (sidebar_tree_.Collapsed(state_.workspace_id, pin->parent_id) &&
          !sidebar_tree_.Revealed(state_.workspace_id, id)) {
        sidebar_tree_.SetRevealed(state_.workspace_id, id, true); revealed = true;
      }
      id = pin->parent_id;
    }
  }
  // Keep one native tree per workspace. Switching never recreates its rows.
  if (catalogue_changed) for (auto it = tab_lists_.begin(); it != tab_lists_.end();) {
    bool exists = false;
    for (const auto& workspace : state_.workspaces) if (workspace.id == it->first) exists = true;
    if (exists) { ++it; continue; }
    auto* root = it->second.root.get();
    sidebar_tree_.ForgetWorkspace(it->first);
    it = tab_lists_.erase(it);
    tab_list_->RemoveChildViewT(root);
  }
  auto& cache = tab_lists_[state_.workspace_id];
  if (revealed) cache.pins.clear();  // Re-plan the pin tree with the new peek.
  if (!cache.root) {
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(CreateSidebarColumnLayout());
    cache.root = root.get();
    root->set_context_menu_controller(this);
    cache.pins_root = root->AddChildView(std::make_unique<views::View>());
    cache.pins_root->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    cache.tabs_root = root->AddChildView(std::make_unique<views::View>());
    cache.tabs_root->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    // Attach a completed tree only once, instead of hundreds of live AX/layout mutations.
    cache.root->SetVisible(false);
    tab_list_->AddChildView(std::move(root));
  }
  const bool pins_changed = cache.pins != state_.pins;
  // Titles/URLs can change several times during a single navigation. Keep the
  // native rows, callbacks, hover/focus and accessibility nodes in that case.
  const bool same_structure = !pins_changed && cache.tabs.size() == state_.tabs.size() &&
      cache.split_tabs == state_.split_tabs && cache.split == state_.split &&
      std::equal(cache.tabs.begin(), cache.tabs.end(), state_.tabs.begin(),
          [](const ShellTab& a, const ShellTab& b) {
            return a.index == b.index && a.pin_id == b.pin_id && a.identity == b.identity;
          });
  std::optional<sidebar::SidebarPinTree> previous_pin_tree;
  if(pins_changed&&cache.node_graph)previous_pin_tree.emplace(cache.node_graph->graph->pin_tree());
  if(!same_structure)ReconcileSidebarNodes(cache,state_.workspace_id,state_.pins,state_.tabs,state_.split_tabs,state_.split);
  bool rebuild_pins=pins_changed;
  if(previous_pin_tree&&cache.pins.empty()==state_.pins.empty()){
    const auto next=cache.node_graph->graph->pin_tree();const auto& previous=*previous_pin_tree;
    bool same=next.root==previous.root&&next.entries.size()==previous.entries.size();
    if(same)for(size_t i=0;i<next.entries.size();++i){const auto& a=previous.entries[i];const auto& b=next.entries[i];if(a.id!=b.id||a.parent!=b.parent||a.group!=b.group||a.depth!=b.depth||a.collapsed!=b.collapsed||a.visible!=b.visible){same=false;break;}}
    // A matching model is insufficient if observers removed/reparented an
    // adapter. Inventory the actual tree before retaining native containers.
    if(same){
      std::map<uint64_t,RustRowView*> rows;std::map<uint64_t,ScenePinGroup*> groups;bool valid=true;
      auto inspect=[&](auto&& self,views::View* view)->void{if(auto* row=views::AsViewClass<RustRowView>(view)){const auto pin=row->PinBinding();if(pin.valid){if(!row->BelongsTo(*cache.node_graph)||!rows.emplace(pin.id,row).second)valid=false;}else if(!row->TabBinding().valid)valid=false;}if(auto* group=views::AsViewClass<ScenePinGroup>(view)){if(!group->Matches(cache.node_graph,group->AdapterId())||!groups.emplace(group->AdapterId(),group).second)valid=false;}for(const auto& child:view->children())self(self,child.get());};inspect(inspect,cache.pins_root);
      const size_t group_count=std::count_if(next.entries.begin(),next.entries.end(),[](const auto& entry){return entry.group!=0;});
      valid=valid&&rows.size()==next.entries.size()&&groups.size()==group_count&&cache.pin_buttons.size()==next.entries.size()&&cache.pins_root->children().size()==1;
      if(valid)for(const auto& entry:next.entries){const auto found=rows.find(entry.id);const auto cached=cache.pin_buttons.find(std::string(entry.key));if(found==rows.end()||cached==cache.pin_buttons.end()||cached->second!=found->second||found->second->GetWidget()!=sidebar_widget_.get()){valid=false;break;}auto* holder=found->second->parent();views::View* parent=cache.pins_root->children().front().get();if(entry.parent!=next.root){const auto group=groups.find(entry.parent);if(group==groups.end()){valid=false;break;}parent=group->second;}if(!holder||holder->parent()!=parent){valid=false;break;}if(entry.group){const auto group=groups.find(entry.group);const auto index=parent->GetIndexOf(holder);if(group==groups.end()||!index||*index+1>=parent->children().size()||parent->children()[*index+1].get()!=group->second||group->second->GetVisible()==entry.collapsed){valid=false;break;}}}
      if(valid){rebuild_pins=false;for(const auto& [id,row]:rows)row->RefreshPinProperties();cache.pins=state_.pins;}
    }
  }

  std::map<std::array<uint64_t,2>,std::unique_ptr<views::View>> retained_tabs;std::map<std::string,std::unique_ptr<views::View>> retained_pins,retained_holders;std::optional<std::string> retained_pin_focus;std::optional<std::array<uint64_t,2>> retained_focus;uint32_t retained_focus_part=0;
  const bool retain=!same_structure&&cache.node_graph;const double retained_offset=tab_scroll_->offset();
  if(retain){
    cache.tab_buttons.clear();if(rebuild_pins)cache.pin_buttons.clear();const auto* focused=cache.root->GetFocusManager()?cache.root->GetFocusManager()->GetFocusedView():nullptr;
    // Traverse actual adapters, not a cache that may contain detached views.
    auto extract=[&](auto&& self,views::View* view)->void{
      if(auto* row=views::AsViewClass<RustRowView>(view)){const auto binding=row->TabBinding();if(binding.valid&&row->BelongsTo(*cache.node_graph)&&row->parent()){
        const std::array<uint64_t,2> identity{binding.high,binding.low};if(const auto part=row->RetainedFocusPart(focused)){retained_focus=identity;retained_focus_part=part;}row->CancelInput();auto owned=row->parent()->RemoveChildViewT(row);CHECK(retained_tabs.emplace(identity,std::move(owned)).second);return;
      }if(rebuild_pins){const auto pin=row->PinBinding();if(pin.valid&&row->BelongsTo(*cache.node_graph)&&row->parent()){if(row->RetainedFocusPart(focused))retained_pin_focus=std::string(pin.key);row->CancelInput();auto* holder=row->parent();auto owned=holder->RemoveChildViewT(row);CHECK(retained_pins.emplace(std::string(pin.key),std::move(owned)).second);
        // Keep the row's indent holder too, so the whole parent path is retained.
        if(holder->children().empty()&&holder->parent()&&holder!=cache.pins_root.get()&&!views::AsViewClass<ScenePinGroup>(holder))retained_holders.emplace(std::string(pin.key),holder->parent()->RemoveChildViewT(holder));return;}}}std::vector<views::View*> children;for(const auto& child:view->children())children.push_back(child.get());for(auto* child:children)self(self,child);
    };extract(extract,cache.root);
  }
  std::map<uint64_t,std::unique_ptr<views::View>> retained_groups;std::unique_ptr<views::View> retained_pin_contents;
  if(retain&&rebuild_pins){
    // Rows are out; keep folder groups by their Rust group ID and the list
    // container, detaching deepest groups first so nesting can change.
    std::vector<ScenePinGroup*> groups;auto collect=[&](auto&& self,views::View* view)->void{for(const auto& child:view->children())self(self,child.get());if(auto* group=views::AsViewClass<ScenePinGroup>(view);group&&group->Matches(cache.node_graph,group->AdapterId()))groups.push_back(group);};collect(collect,cache.pins_root);
    for(auto* group:groups){const auto id=group->AdapterId();auto owned=group->parent()->RemoveChildViewT(group);owned->RemoveAllChildViews();retained_groups.emplace(id,std::move(owned));}
    if(cache.pins_root->children().size()==1){retained_pin_contents=cache.pins_root->RemoveChildViewT(cache.pins_root->children().front().get());retained_pin_contents->RemoveAllChildViews();}
  }
  if (rebuild_pins) {
    ++pinned_list_build_count_;
    cache.pin_buttons.clear();
    cache.pins_root->RemoveAllChildViews();
    auto contents = std::move(retained_pin_contents);
    if (!contents) {
      contents = std::make_unique<views::View>();
      contents->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    }
    if (!state_.pins.empty()) {
      Label(contents.get(), T(u"PRZYPIĘTE"), kMuted, Font(-2));
      AppendPinnedItems(contents.get(),state_.pins,cache,state_.workspace_id
          ,&retained_pins,&retained_groups,&retained_holders
      );
      Spacer(contents.get(), 8);
    }
    cache.pins_root->AddChildView(std::move(contents));
    cache.pins = state_.pins;
    cache.active_pin.clear();
    // The rebuilt pin tree no longer holds a pair placed in it; rebuild tabs too.
    cache.pin_pair = nullptr;
    cache.tabs.clear();
    cache.tab_buttons.clear();
  }
  if (!same_structure) {
    std::unique_ptr<views::View> kept_contents, kept_pair;
    if (cache.node_graph) {
      // Rows were extracted above. Keep the tab column and a pair container whose
      // Rust node survived, so container identity follows the Rust graph.
      const auto pair_id = cache.node_graph->graph->sidebar_tabs().pair;
      views::View* old_pair = cache.pin_pair ? cache.pin_pair.get() : cache.root_pair.get();
      if (old_pair && pair_id && pair_id == cache.pair_id && old_pair->parent() && old_pair->children().empty())
        kept_pair = old_pair->parent()->RemoveChildViewT(old_pair);
      if (kept_pair) cache.pin_pair = cache.root_pair = nullptr;
      if (cache.tabs_root->children().size() == 1) {
        kept_contents = cache.tabs_root->RemoveChildViewT(cache.tabs_root->children().front().get());
        kept_contents->RemoveAllChildViews();
      }
      cache.pair_id = pair_id;
    }
    if (cache.pin_pair) {
      cache.pin_pair->parent()->RemoveChildViewT(cache.pin_pair.get());
      cache.pin_pair = nullptr;
    }
    cache.root_pair = nullptr;
    cache.tab_buttons.clear();
    cache.tabs_root->RemoveAllChildViews();
    auto contents = std::move(kept_contents);
    if (!contents) {
      contents = std::make_unique<views::View>();
      contents->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    }
    auto add_tab = [&](views::View* parent, const ShellTab& tab, bool tile = false) {
      RowRecipe::Spec spec;
      spec.title = tab.title;
      spec.audible = tab.audible;
      spec.sidebar_row = true;
      if(cache.node_graph)spec.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak_factory_.GetWeakPtr());
      spec.tile = tile;
      if (tile) spec.height = 38;
      auto host = HostLabel(tab.url);
      spec.monogram = base::i18n::ToUpper((host.empty() ? tab.title : host).substr(0, 1));
      spec.monogram_color = kMonogramColors[base::FastHash(base::UTF16ToUTF8(host)) % kMonogramColors.size()];
      spec.drop=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string workspace,size_t index,std::array<uint64_t,2> identity,gfx::Point point){
        if(shell)shell->HandleSidebarDrop(std::move(workspace),index,std::move(identity),{},point);
      },weak_factory_.GetWeakPtr(),state_.workspace_id,tab.index,tab.identity);
      spec.drag=base::BindRepeating(&WorkspaceShell::HandleSidebarDrag,weak_factory_.GetWeakPtr());
      if(cache.node_graph){
        auto found=retained_tabs.find(tab.identity);const bool fresh=found==retained_tabs.end();std::unique_ptr<views::View> adapter;
        if(found!=retained_tabs.end()){adapter=std::move(found->second);retained_tabs.erase(found);static_cast<RustRowView*>(adapter.get())->ProjectTabSpec(*cache.node_graph,std::move(spec));}
        else adapter=CreateSceneRow({},std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{cache.node_graph,1,tab.identity,{}});
        auto* row=views::AsViewClass<RustRowView>(adapter.get());CHECK(row);const auto binding=row->TabBinding();CHECK(binding.valid);
        auto callback=[&](bool close){return base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,base::WeakPtr<RustRowView> row,std::weak_ptr<SidebarNodeGraphState> owner,std::string workspace,std::array<uint64_t,2> identity,uint64_t id,bool close){const auto scope=owner.lock();if(!shell||!row||!scope||shell->state_.workspace_id!=workspace||row->GetWidget()!=shell->sidebar_widget_.get()||!SceneIsVisible(row.get())||!row->GetEnabledInViewsSubtree())return;auto cache=shell->tab_lists_.find(workspace);if(cache==shell->tab_lists_.end()||cache->second.node_graph!=scope||!cache->second.root||!cache->second.root->Contains(row.get())||row->LogicalNodeId()!=id||!row->BelongsTo(*scope)||(close&&!row->AttachedClose()))return;const auto target=scope->graph->tab_target(identity[0],identity[1],id,close);if(!target.valid)return;const auto projected=std::find_if(shell->state_.tabs.begin(),shell->state_.tabs.end(),[&](const ShellTab& tab){return tab.index==target.index&&tab.identity==identity;});if(projected==shell->state_.tabs.end())return;if(close)shell->callbacks_.close_tab.Run(target.index);else shell->callbacks_.select_tab.Run(target.index);},weak_factory_.GetWeakPtr(),row->GetWeakPtr(),std::weak_ptr<SidebarNodeGraphState>(cache.node_graph),state_.workspace_id,tab.identity,binding.id,close);};
        if(fresh)row->SetCallback(callback(false));views::Button::PressedCallback close_callback;if(state_.tabs.size()>1&&row->TabCloseNeedsCallback(*cache.node_graph))close_callback=callback(true);row->ConfigureTabClose(state_.tabs.size()>1,std::move(close_callback),{cache.node_graph,10,tab.identity,{}});
        auto* attached=parent->AddChildView(std::move(adapter));cache.tab_buttons[tab.index]=attached;attached->set_context_menu_controller(this);return attached;
      }
      if (state_.tabs.size() > 1) spec.close = base::BindRepeating(callbacks_.close_tab, tab.index);
      cache.tab_buttons[tab.index] = parent->AddChildView(CreateSceneRow(
          base::BindRepeating(callbacks_.select_tab,tab.index),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{cache.node_graph,1,tab.identity,{}}));
      cache.tab_buttons[tab.index]->set_context_menu_controller(this);
      return cache.tab_buttons[tab.index].get();
    };
    std::vector<SidebarTreeNode> pin_nodes;
    std::vector<SidebarTabNode> tab_nodes;
    pin_nodes.reserve(state_.pins.size()); tab_nodes.reserve(state_.tabs.size());
    for (const auto& pin : state_.pins) pin_nodes.push_back({pin.id, pin.parent_id, pin.folder});
    for (const auto& tab : state_.tabs) tab_nodes.push_back({tab.index, tab.pin_id});
    if (cache.node_graph) {
      // The Rust graph reconciled this exact layout above; project its slots.
      const auto tree = cache.node_graph->graph->sidebar_tabs();
      cache.tab_plan = {};
      for (const auto& slot : tree.slots) cache.tab_plan.slots.push_back({slot.first, std::vector<size_t>(slot.members.begin(), slot.members.end()), slot.pin, slot.pair});
      for (const auto& pin : tree.pins) cache.tab_plan.pins.push_back({pin.index, pin.visible});
    } else
    cache.tab_plan = sidebar_tree_.Tabs(state_.workspace_id, pin_nodes, tab_nodes,
        state_.split_tabs, state_.split);
    for (const auto& slot : cache.tab_plan.slots) {
      if (!slot.pair) { add_tab(contents.get(), state_.tabs[slot.first]); continue; }
      SplitPair* pair = nullptr;
      std::unique_ptr<SplitPair> pair_view = kept_pair ? base::WrapUnique(views::AsViewClass<SplitPair>(kept_pair.release())) : std::make_unique<SplitPair>();
      CHECK(pair_view);
      if (slot.pin != SidebarTree::kRoot) {
        auto* pin_row = cache.pin_buttons.at(state_.pins[slot.pin].id)->parent();
        auto* group = pin_row->parent();
        pair = group->AddChildViewAt(std::move(pair_view), *group->GetIndexOf(pin_row));
        pair->SetProperty(views::kMarginsKey, pin_row->GetInsets());
        cache.pin_pair = pair;
      } else {
        pair = contents->AddChildView(std::move(pair_view));
        pair->ClearProperty(views::kMarginsKey);
        cache.root_pair = pair;
      }
      for (size_t member : slot.members) add_tab(pair, state_.tabs[member], true);
    }
    cache.tabs_root->AddChildView(std::move(contents));
    cache.tabs = state_.tabs;
    cache.split_tabs = state_.split_tabs;
    cache.split = state_.split;
  } else if (cache.tabs != state_.tabs) {
    for (size_t i = 0; i < state_.tabs.size(); ++i) {
      const auto& tab = state_.tabs[i];
      const auto& previous = cache.tabs[i];
      auto found = cache.tab_buttons.find(tab.index);
      if (found == cache.tab_buttons.end()) continue;
      auto* row = found->second.get();
      if (tab.title != previous.title) SetRowTitle(row,tab.title);
      if (tab.audible != previous.audible) SetRowAudible(row,tab.audible);
      if (tab.url != previous.url || tab.title != previous.title) {
        const auto host = HostLabel(tab.url);
        SetRowMonogram(row,base::i18n::ToUpper((host.empty() ? tab.title : host).substr(0, 1)),
            kMonogramColors[base::FastHash(base::UTF16ToUTF8(host)) % kMonogramColors.size()]);
      }
    }
    cache.tabs = state_.tabs;
  }
  if (cache.active_pin != state_.active_pin) {
    for (const auto& id : {cache.active_pin, state_.active_pin}) {
      auto it = cache.pin_buttons.find(id);
      if (it != cache.pin_buttons.end()) SetRowActive(it->second.get(),id==state_.active_pin);
    }
    cache.active_pin = state_.active_pin;
  }
  if (!same_structure) {
    for (const auto& pin : cache.tab_plan.pins) {
      ++pin_visibility_update_count_;
      // Hidden when a split pair stands in for it, or inside a collapsed folder
      // it was not revealed from.
      const auto& item = state_.pins[pin.index];
      const bool in_folder = item.parent_id.empty() || !sidebar_tree_.Collapsed(state_.workspace_id, item.parent_id) ||
          sidebar_tree_.Revealed(state_.workspace_id, item.id);
      cache.pin_buttons.at(item.id)->parent()->SetVisible(pin.visible && in_folder);
    }
  }
  for (const auto& [index, button] : cache.tab_buttons)
    SetRowActive(button.get(),index==state_.active_tab);
  if (previous_workspace != state_.workspace_id) {
    if (auto old = tab_lists_.find(std::string(previous_workspace)); old != tab_lists_.end()) old->second.root->SetVisible(false);
  }
  cache.root->SetVisible(true);
  if(retain){if(auto* widget=cache.root->GetWidget())widget->LayoutRootViewIfNecessary();if(previous_workspace==state_.workspace_id)tab_scroll_->ScrollTo(retained_offset);if(retained_focus){for(const auto& [index,view]:cache.tab_buttons){auto* row=views::AsViewClass<RustRowView>(view.get());if(!row)continue;const auto binding=row->TabBinding();if(binding.valid&&std::array<uint64_t,2>{binding.high,binding.low}==*retained_focus){row->ScrollViewToVisible();row->RestoreFocusPart(retained_focus_part);break;}}}if(retained_pin_focus){const auto found=cache.pin_buttons.find(*retained_pin_focus);if(found!=cache.pin_buttons.end()&&SceneIsVisible(found->second.get())){found->second->ScrollViewToVisible();found->second->RequestFocus();}}}
  // Install controllers only for new trees. A normal switch must not walk all
  // cached workspaces and their label/layout descendants again.
  if (catalogue_changed) ConfigureSidebarContextMenus(sidebar_root_);
  else if (!same_structure) ConfigureSidebarContextMenus(cache.root);
  // Model metadata can change while native paints are locked, clipped or
  // suppressed. The Rust scene must not depend on a subsequent native paint.
  ScheduleMetalSidebar();
}

size_t WorkspaceShell::CycleSidebarTab(size_t current, size_t count, bool backwards) const {
  if (metal_sidebar_) return sidebar::cycle_tab(current, count, backwards);
  if (!count) return 0;
  current = std::min(current, count - 1);
  return backwards ? (current ? current - 1 : count - 1) : (current + 1 == count ? 0 : current + 1);
}
bool WorkspaceShell::metal_sidebar_visible() const {
  return metal_sidebar_ && metal_sidebar_->visible();
}
size_t WorkspaceShell::metal_sidebar_frame_count() const {
  return metal_sidebar_ ? metal_sidebar_->frame_count() : 0;
}
bool WorkspaceShell::metal_sidebar_contains_text_for_testing(std::string_view text) const {
  return metal_sidebar_ && metal_sidebar_->contains_text_for_testing(text);
}
gfx::Rect WorkspaceShell::metal_sidebar_screen_bounds_for_testing() const {
  return metal_sidebar_ ? metal_sidebar_->screen_bounds_for_testing() : gfx::Rect();
}
bool WorkspaceShell::metal_sidebar_contains_rect_for_testing(gfx::RectF bounds, gfx::RectF clip) const {
  return metal_sidebar_ && metal_sidebar_->contains_rect_for_testing(bounds, clip);
}
size_t WorkspaceShell::native_sidebar_paint_count() const {
  return tab_list_ ? static_cast<SidebarListView*>(tab_list_.get())->native_paints() : 0;
}
size_t WorkspaceShell::metal_sidebar_paint_count() const {
  return tab_list_ ? static_cast<SidebarListView*>(tab_list_.get())->metal_paints() : 0;
}
bool WorkspaceShell::workspace_context_menu_running_for_testing() const{return workspace_menu_runner_&&workspace_menu_runner_->IsRunning();}
std::unique_ptr<views::View> WorkspaceShell::CreateWorkspaceActionForTesting(base::RepeatingClosure callback) const{return CreateSceneAction(std::move(callback),u"Test workspace",ControlGlyph::kDot,nullptr,{},base::DoNothing());}
uint32_t WorkspaceShell::control_model_kind_for_testing(views::View* view) const{
  if(auto* action=views::AsViewClass<RustActionView>(view))return action->ModelKind();
  if(auto* row=views::AsViewClass<RustRowView>(view))return row->ModelKind();
  if(auto* row=views::AsViewClass<SiteRuleSceneRow>(view))return row->ModelKind();
  return 0;
}
views::View* WorkspaceShell::header_action_for_testing(size_t index) const{return header_view_&&index<header_view_->children().size()?header_view_->children()[index].get():nullptr;}
views::View* WorkspaceShell::sidebar_grip_for_testing() const{return sidebar_root_?static_cast<SidebarView*>(sidebar_root_.get())->grip.get():nullptr;}
views::View* WorkspaceShell::split_divider_for_testing() const{return content_host_?static_cast<PaneHost*>(content_host_.get())->divider(0):nullptr;}
bool WorkspaceShell::metal_sidebar_grip_visible() const{return metal_sidebar_grip_&&metal_sidebar_grip_->visible();}
void WorkspaceShell::HideMetalSidebarGrip() {
  if(!metal_sidebar_grip_)return;
  const bool visible=metal_sidebar_grip_->visible();metal_sidebar_grip_->Hide();
  if(visible&&sidebar_root_)static_cast<SidebarView*>(sidebar_root_.get())->grip->SchedulePaint();
}
bool WorkspaceShell::metal_split_visible(size_t index) const {return index<metal_split_.size() && metal_split_[index] && metal_split_[index]->visible();}
bool WorkspaceShell::metal_split_contains_text_for_testing(size_t index,std::string_view text) const {return index<metal_split_.size() && metal_split_[index] && metal_split_[index]->contains_text_for_testing(text);}
gfx::Rect WorkspaceShell::metal_split_screen_bounds_for_testing(size_t index) const {return index<metal_split_.size() && metal_split_[index] ? metal_split_[index]->screen_bounds_for_testing() : gfx::Rect();}
void WorkspaceShell::ScheduleMetalSplit() {
  if ((!metal_header_ && !metal_sidebar_) || metal_split_pending_)return;
  metal_split_pending_=true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&WorkspaceShell::UpdateMetalSplit,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalSplit() {
  metal_split_pending_=false;
  if(!content_host_ || !widget_ || (!metal_header_ && !metal_sidebar_))return;
  widget_->LayoutRootViewIfNecessary();
  auto* host=static_cast<PaneHost*>(content_host_.get());
  for(size_t index=0;index<metal_split_.size();++index) {
    auto* view=host->surface(index);auto& surface=metal_split_[index];const bool was=surface && surface->visible();
    const bool is_bar=index%2==1||index==0;
    const auto visible=view->GetVisibleBounds();
    if(!widget_->IsVisible() || !view->IsDrawn() || visible.IsEmpty()) {
      if(surface)surface->Hide();if(was)view->SchedulePaint();continue;
    }
    if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
    auto bounds=view->GetBoundsInScreen();bounds.Offset(visible.x(),visible.y());bounds.set_size(visible.size());
    const sidebar::FloatRect clip{0,0,float(bounds.width()),float(bounds.height())};
    auto rect=gfx::RectF(view->GetBoundsInScreen());rect.Offset(-bounds.x(),-bounds.y());
    std::vector<sidebar::Item> items;
    if(is_bar) {
      auto card=gfx::RectF(host->GetBoundsInScreen());card.Offset(-bounds.x(),-bounds.y());
      sidebar::Item background{};background.kind=10;background.fill=Tone(host,kCard);background.clip=clip;
      background.x=card.x();background.y=card.y();background.width=card.width();background.height=card.height();items.push_back(std::move(background));
      auto* bar=static_cast<PaneBar*>(view);
      sidebar::Item title{};title.kind=8;title.x=rect.x();title.y=rect.y();title.width=rect.width();title.height=rect.height();title.clip=clip;
      title.title=base::UTF16ToUTF8(bar->text());title.title_width=bar->text_width();title.ink=bar->active()?Palette(bar).ink:Palette(bar).ink_muted;title.font_size=12;title.text_style=8;items.push_back(std::move(title));
      for(auto* button:{bar->menu(),bar->close()}) {
        auto box=gfx::RectF(button->GetBoundsInScreen());box.Offset(-bounds.x(),-bounds.y());
        items.push_back(ActionSceneItem(button,box,gfx::RectF(gfx::Rect(bounds.size()))));
      }
    } else {
      sidebar::Item item{};item.kind=9;item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip=clip;
      // SplitView gutter: a 3 px `line-strong` stroke that grows and turns `zing` on hover.
      const bool hovered=host->divider_handle(index/2-1)->hovered();const auto p=Palette(view);item.hovered=hovered;item.ink=hovered?p.zing:p.line_strong;items.push_back(std::move(item));
    }
    surface->Update(widget_->GetNativeWindow(),bounds,items,is_bar ? FrameVoid(workspace_color(state_.workspace_id),root_) : Tone(view,kChip));
    if(was!=surface->visible())view->SchedulePaint();
  }
}
bool WorkspaceShell::metal_header_visible() const {return metal_header_ && metal_header_->visible();}
size_t WorkspaceShell::metal_header_frame_count() const {return metal_header_ ? metal_header_->frame_count() : 0;}
gfx::Rect WorkspaceShell::metal_header_screen_bounds_for_testing() const {return metal_header_ ? metal_header_->screen_bounds_for_testing() : gfx::Rect();}
void WorkspaceShell::ScheduleMetalHeader() {
  if (!metal_header_ || metal_header_pending_) return;
  metal_header_pending_=true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&WorkspaceShell::UpdateMetalHeader,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalHeader() {
  metal_header_pending_=false;
  if (!metal_header_ || !header_view_ || !widget_) return;
  const bool was_visible=metal_header_->visible();
  if (!widget_->IsVisible()) {metal_header_->Hide();if(was_visible)header_view_->SchedulePaint();return;}
  widget_->LayoutRootViewIfNecessary();
  const auto visible=header_view_->GetVisibleBounds();
  if (visible.IsEmpty()) {metal_header_->Hide();if(was_visible)header_view_->SchedulePaint();return;}
  auto bounds=header_view_->GetBoundsInScreen();bounds.Offset(visible.x(),visible.y());bounds.set_size(visible.size());
  if(auto* control=views::AsViewClass<RustActionView>(header_sidebar_button_))control->SetOn(sidebar_visible());else static_cast<GlyphButton*>(header_sidebar_button_.get())->set_on(sidebar_visible());
  std::vector<sidebar::Item> items;items.reserve(header_view_->children().size());
  for (const auto& child : header_view_->children()) {
    auto rect=gfx::RectF(child->GetBoundsInScreen());rect.Offset(-bounds.x(),-bounds.y());
    if(auto* control=views::AsViewClass<RustActionView>(child.get()))items.push_back(control->SceneItem(rect,gfx::RectF(gfx::Rect(bounds.size()))));else items.push_back(GlyphSceneItem(static_cast<GlyphButton*>(child.get()),rect,gfx::RectF(gfx::Rect(bounds.size()))));
  }
  metal_header_->Update(widget_->GetNativeWindow(),bounds,items,FrameVoid(workspace_color(state_.workspace_id),root_));
  if (was_visible!=metal_header_->visible()) header_view_->SchedulePaint();
}
bool WorkspaceShell::metal_palette_visible() const{return metal_palette_&&metal_palette_->visible();}
bool WorkspaceShell::metal_palette_contains_text_for_testing(std::string_view text) const{return metal_palette_&&metal_palette_->contains_text_for_testing(text);}
gfx::Rect WorkspaceShell::metal_palette_screen_bounds_for_testing() const{return metal_palette_?metal_palette_->screen_bounds_for_testing():gfx::Rect();}
size_t WorkspaceShell::palette_selection_for_testing() const{return palette_root_?static_cast<PaletteView*>(palette_root_.get())->selected():0;}
bool WorkspaceShell::metal_palette_chrome_visible(size_t index) const{return index<metal_palette_chrome_.size()&&metal_palette_chrome_[index]&&metal_palette_chrome_[index]->visible();}
size_t WorkspaceShell::metal_palette_chrome_frame_count_for_testing(size_t index) const{return index<metal_palette_chrome_.size()&&metal_palette_chrome_[index]?metal_palette_chrome_[index]->frame_count():0;}
bool WorkspaceShell::metal_palette_chrome_contains_text_for_testing(size_t index,std::string_view text) const{return index<metal_palette_chrome_.size()&&metal_palette_chrome_[index]&&metal_palette_chrome_[index]->contains_text_for_testing(text);}
gfx::Rect WorkspaceShell::metal_palette_chrome_screen_bounds_for_testing(size_t index) const{return index<metal_palette_chrome_.size()&&metal_palette_chrome_[index]?metal_palette_chrome_[index]->screen_bounds_for_testing():gfx::Rect();}
void WorkspaceShell::UpdateMetalPaletteChrome() {
  if(!address_widget_||!metal_palette_)return;
  if(address_visible())address_widget_->LayoutRootViewIfNecessary();
  for(size_t i=0;i<palette_chrome_views_.size();++i) {
    auto* view=palette_chrome_views_[i].get();auto& surface=metal_palette_chrome_[i];const bool was=surface&&surface->visible();
    if(!view||!address_visible()||!view->IsDrawn()){if(surface)surface->Hide();if(was&&view)view->SchedulePaint();continue;}
    auto local=view->GetVisibleBounds();
    if(local.IsEmpty()){if(surface)surface->Hide();if(was)view->SchedulePaint();continue;}
    if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
    auto bounds=view->GetBoundsInScreen();bounds.Offset(local.x(),local.y());bounds.set_size(local.size());
    auto full=gfx::RectF(view->GetBoundsInScreen());full.Offset(-bounds.x(),-bounds.y());const auto clip=gfx::RectF(gfx::Rect(bounds.size()));
    std::vector<sidebar::Item> items;
    if(i==0)items.push_back(static_cast<SceneSearchIcon*>(view)->SceneItem(full,clip));
    else for(auto& item:static_cast<PaletteText*>(view)->SceneItems(full,clip))items.push_back(std::move(item));
    surface->Update(address_widget_->GetNativeWindow(),bounds,items,Tone(palette_root_,kCard));if(was!=surface->visible())view->SchedulePaint();
  }
}
void WorkspaceShell::ScheduleMetalPalette() {
  SyncSceneRowInput();
  if(!metal_palette_||metal_palette_pending_)return;
  metal_palette_pending_=true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&WorkspaceShell::UpdateMetalPalette,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalPalette() {
  metal_palette_pending_=false;
  if(!metal_palette_||!address_widget_||!suggestion_list_)return;
  UpdateMetalPaletteChrome();
  const bool was_visible=metal_palette_->visible();
  if(!address_visible()||!suggestion_scroll_->view()->IsDrawn()) {
    metal_palette_->Hide();if(was_visible){suggestion_list_->SchedulePaint();suggestion_scroll_->vertical_scroll_bar()->SchedulePaint();}return;
  }
  address_widget_->LayoutRootViewIfNecessary();
  auto* viewport=suggestion_list_->parent();const auto viewport_bounds=viewport->GetBoundsInScreen();
  if(viewport_bounds.IsEmpty()){metal_palette_->Hide();return;}
  const auto scroller=GetSidebarScrollBarSnapshot(suggestion_scroll_->vertical_scroll_bar());
  auto bounds=viewport_bounds;if(scroller.visible)bounds.Union(scroller.bounds);
  std::vector<SidebarViewportNode> nodes{{SidebarTree::kRoot,gfx::Rect(suggestion_list_->size()),true}};
  for(const auto& row:suggestion_list_->children())nodes.push_back({0,row->GetMirroredBounds(),row->GetVisible()});
  const gfx::PointF requested(0,static_cast<float>(suggestion_scroll_->offset()));
  const auto projection=metal_palette_->Project(suggestion_list_->size(),viewport_bounds.size(),requested,nodes);
  if(projection.offset!=requested){suggestion_scroll_->ScrollTo(projection.offset.y());ScheduleMetalPalette();return;}
  const gfx::Vector2dF origin(viewport_bounds.x()-bounds.x(),viewport_bounds.y()-bounds.y());
  std::vector<sidebar::Item> items;
  for(const auto& node:projection.nodes) {
    auto rect=node.bounds;rect.Offset(origin);auto clip=node.clip;clip.Offset(origin);
    if(node.index==0){sidebar::Item line{};line.kind=14;line.x=rect.x();line.y=rect.y();line.width=rect.width();line.height=1;line.fill=Hairline(suggestion_list_);line.clip={clip.x(),clip.y(),clip.width(),clip.height()};items.push_back(std::move(line));continue;}
    auto* row=suggestion_list_->children()[node.index-1].get();const auto& spec=RowSpec(row);
    if(!spec.header.empty())items.push_back(PaletteHeaderItem(row,spec,rect,clip));
    items.push_back(PaletteRowItem(row,spec,rect,clip,RowHovered(row),row->HasFocus()));
    const auto p=Palette(row);const auto body=PaletteRowBody(spec,rect);
    sidebar::Item icon{};icon.kind=13;icon.x=body.x();icon.y=body.y();icon.width=body.width();icon.height=body.height();icon.clip={clip.x(),clip.y(),clip.width(),clip.height()};icon.ink=spec.highlighted?p.accent_ink:p.ink_muted;
    icon.glyph=spec.icon==&vector_icons::kSettingsIcon?2:spec.icon==&vector_icons::kArrowForwardIcon?1:0;items.push_back(std::move(icon));
  }
  if(scroller.visible){auto rect=scroller.thumb;rect.Offset(-bounds.x(),-bounds.y());auto clip=scroller.bounds;clip.Offset(-bounds.x(),-bounds.y());sidebar::Item thumb{};thumb.kind=6;thumb.x=rect.x();thumb.y=rect.y();thumb.width=rect.width();thumb.height=rect.height();thumb.fill=scroller.color;thumb.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};items.push_back(std::move(thumb));}
  metal_palette_->Update(address_widget_->GetNativeWindow(),bounds,items,Tone(palette_root_,kCard));
  if(was_visible!=metal_palette_->visible()){suggestion_list_->SchedulePaint();suggestion_scroll_->vertical_scroll_bar()->SchedulePaint();}
}
void WorkspaceShell::WatchSceneRowInput(views::View* row,base::RepeatingClosure sync){
  if(sync)scene_row_input_[row]=std::move(sync);else scene_row_input_.erase(row);
}
void WorkspaceShell::SyncSceneRowInput(){
  // Cancellation can erase a watch or schedule paint. Snapshot callbacks before
  // dispatch and let each Rust controller's idempotent cancel stop recursion.
  if(!scene_row_input_.empty()){std::vector<base::RepeatingClosure> pending;for(const auto& [row,sync]:scene_row_input_)pending.push_back(sync);for(auto& sync:pending)sync.Run();}
}
void WorkspaceShell::ScheduleMetalSidebar() {
  SyncSceneRowInput();
  // Layer transforms do not deliver descendant visibility callbacks. Retire a
  // clipped action synchronously, including while a GPU update is already queued.
  if(tab_scroll_&&tab_scroll_->rust_owned()){
  for(auto* view:{pin_button_.get(),workspace_settings_button_.get()})if(view)if(auto* control=views::AsViewClass<RustActionView>(view))control->SyncViewportVisibility();
  for(const auto& [id,view]:workspace_buttons_)if(auto* control=views::AsViewClass<RustActionView>(view.get()))control->SyncViewportVisibility();
  }
  ScheduleMetalHeader();
  if (!metal_sidebar_ || metal_sidebar_pending_) return;
  metal_sidebar_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&WorkspaceShell::UpdateMetalSidebar, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalSidebar() {
  metal_sidebar_pending_ = false;
  if (!metal_sidebar_) return;
  const bool was_visible = metal_sidebar_->visible();
  if (!sidebar_visible() || swipe_overlay_ || workspace_animation_running()) {
    metal_sidebar_->Hide();HideMetalSidebarGrip();
    if (was_visible) { tab_list_->SchedulePaint(); new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint(); }
    return;
  }
  // Invalidations can reach Metal before the next native compositor paint.
  // Apply the Rust column/row layouts now, rather than publish zero/stale bounds.
  sidebar_widget_->LayoutRootViewIfNecessary();
  auto* viewport = tab_list_->parent();
  const auto list_bounds = viewport->GetBoundsInScreen();
  const auto viewport_frame = views::View::ConvertRectToTarget(viewport, sidebar_root_, viewport->GetLocalBounds());
  for (auto* control : {static_cast<views::View*>(url_pill_.get()),new_tab_button_.get()}) {
    const auto r = control->GetMirroredBounds();
    const auto a = sidebar::action_frame({r.x(), r.y(), r.width(), r.height()},
        {viewport_frame.x(), viewport_frame.y(), viewport_frame.width(), viewport_frame.height()});
    control->SetBoundsRect(sidebar_root_->GetMirroredRect(gfx::Rect(a.x, a.y, a.width, a.height)));
  }
  workspace_name_->SetBoundsRect(sidebar_root_->GetMirroredRect(gfx::Rect(
      viewport_frame.x(), workspace_name_->y(), viewport_frame.width(), workspace_name_->height())));
  const auto scroller=GetSidebarScrollBarSnapshot(tab_scroll_->vertical_scroll_bar());
  auto bounds = list_bounds;
  if (scroller.visible) bounds.Union(scroller.bounds);
  if (sidebar_footer_->IsDrawn()) bounds.Union(sidebar_footer_->GetBoundsInScreen());
  if (url_pill_->IsDrawn()) bounds.Union(url_pill_->GetBoundsInScreen());
  if (new_tab_button_->IsDrawn()) bounds.Union(new_tab_button_->GetBoundsInScreen());
  const gfx::Vector2dF list_origin(list_bounds.x() - bounds.x(), list_bounds.y() - bounds.y());
  std::vector<SidebarViewportNode> nodes;
  std::vector<views::View*> native_views;
  // Native adapters deliver parent-local mirrored geometry and visibility only.
  // Rust projects the active tree and clips it before text/color preparation.
  auto gather = [&](auto&& self, views::View* view, size_t parent) -> void {
    const size_t index = nodes.size();
    nodes.push_back({parent, view == tab_list_ ? gfx::Rect(view->size()) : view->GetMirroredBounds(), view->GetVisible()});
    native_views.push_back(view);
    if (!view->GetVisible() || IsRow(view) || views::AsViewClass<views::Label>(view)) return;
    for (const auto& child : view->children()) self(self, child.get(), index);
  };
  gather(gather, tab_list_, SidebarTree::kRoot);
  const auto requested = gfx::PointF(0,static_cast<float>(tab_scroll_->offset()));
  const auto projection = metal_sidebar_->Project(tab_list_->size(), list_bounds.size(), requested, nodes);
  if (projection.offset != requested) {
    // Direct programmatic offsets and resized content can exceed the new
    // extent. Apply Rust's normalized offset to the native input adapter too.
    tab_scroll_->ScrollTo(projection.offset.y());
    ScheduleMetalSidebar();
    return;
  }
  std::vector<sidebar::Item> items;
  items.reserve(projection.nodes.size());
  for (const auto& projected : projection.nodes) {
    auto* view = native_views[projected.index];
    auto rect = projected.bounds; rect.Offset(list_origin);
    auto clip = projected.clip; clip.Offset(list_origin);
    sidebar::Item item{};
    item.x = rect.x(); item.y = rect.y(); item.width = rect.width(); item.height = rect.height();
    item.clip = {clip.x(), clip.y(), clip.width(), clip.height()};
    if (IsRow(view)) {
      auto* row=view;
      if(auto* rust_row=views::AsViewClass<RustRowView>(row)){auto scene=rust_row->SidebarScene(rect,clip);if(scene.valid){
        // The row being dragged stays in place as a faded placeholder.
        if(row==drag_row_.view()){auto& i=scene.item;i.ink=SkColorSetA(i.ink,0x59);i.badge=SkColorSetA(i.badge,0x59);i.fill=SkColorSetA(i.fill,SkColorGetA(i.fill)/3);i.close_visible=false;}
        items.push_back(std::move(scene.item));continue;}}
      const auto& spec = RowSpec(row);
      const bool hovered=RowHovered(row);
      SkColor fill = SK_ColorTRANSPARENT;
      if (spec.tile) fill = SkColorSetA(Tone(row, kCard), spec.active ? 0xFF : hovered ? 0x8C : 0x59);
      else if (spec.active) fill = Tone(row, kCard);
      else if (spec.highlighted) fill = Tone(row, kAccentWash);
      else if (hovered) fill = HoverWash(row);
      item.kind = 0; item.title = base::UTF16ToUTF8(spec.title);
      item.monogram = base::UTF16ToUTF8(spec.icon ? u"📁" : spec.monogram);
      item.ink = Tone(row, kInk); item.fill = fill;
      item.badge = spec.icon ? SK_ColorTRANSPARENT : spec.monogram_color;
      item.active = spec.active; item.hovered = hovered; item.tile = spec.tile;
      item.close = RowHasClose(row); item.close_visible = RowCloseVisible(row);
      item.focused = row->HasFocus(); item.font_size = Font(0).GetFontSize();
      items.push_back(std::move(item));
    } else if (auto* label = views::AsViewClass<views::Label>(view)) {
      item.kind=1; item.title=base::UTF16ToUTF8(label->GetText());
      item.ink=label->GetEnabledColor(); item.font_size=label->font_list().GetFontSize();
      items.push_back(std::move(item));
    } else if (views::AsViewClass<SplitPair>(view)) {
      item.kind=2; item.fill=SkColorSetA(Tone(view,kCard),0x33);
      items.push_back(std::move(item));
    }
  }
  for (auto* control : {static_cast<views::View*>(url_pill_.get()),new_tab_button_.get()}) {
    if (!control->IsDrawn()) continue;
    auto* row=control;
    const auto& spec = RowSpec(row);
    auto rect = row->GetBoundsInScreen(); rect.Offset(-bounds.x(), -bounds.y());
    sidebar::Item item{};
    item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
    item.clip={float(rect.x()), float(rect.y()), float(rect.width()), float(rect.height())};
    if (spec.address) { items.push_back(AddressRowItem(row,spec,gfx::RectF(rect),gfx::RectF(rect),RowTitleWidth(row),RowHovered(row),row->HasFocus())); continue; }
    if (spec.kbd) { for (auto& key : CommandRowItems(row,spec,gfx::RectF(rect),gfx::RectF(rect),RowHovered(row),row->HasFocus())) items.push_back(std::move(key)); continue; }
    item.kind=spec.address ? 4 : 3; item.title=base::UTF16ToUTF8(spec.title); item.detail=base::UTF16ToUTF8(spec.detail);
    item.title_width=RowTitleWidth(row); item.font_size=Font(0).GetFontSize();
    item.detail_font_size=Font(spec.address ? 0 : -1).GetFontSize(); item.detail_ink=Tone(row,kMuted);
    item.ink=Tone(row,row->GetEnabledInViewsSubtree() && !spec.blank ? kInk : kMuted); item.badge=Tone(row,spec.address ? kMuted : kSubtle);
    item.active=spec.secure;
    item.hovered=RowHovered(row);
    item.fill=spec.address ? Tone(row,kCard) : item.hovered ? HoverWash(row) : SK_ColorTRANSPARENT; if (spec.address) item.edge=Hairline(row); item.focused=row->HasFocus(); item.focus=Palette(row).focus;
    items.push_back(std::move(item));
  }
  if (workspace_name_->IsDrawn()) {
    auto rect = workspace_name_->GetBoundsInScreen(); rect.Offset(-bounds.x(), -bounds.y());
    sidebar::Item item{}; item.kind=5;
    item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
    item.clip={float(rect.x()),float(rect.y()),float(rect.width()),float(rect.height())};
    item.title=base::UTF16ToUTF8(static_cast<WorkspaceHeading*>(workspace_name_.get())->text());
    item.ink=Tone(workspace_name_,kSubtle); item.font_size=Font(-1).GetFontSize();
    items.push_back(std::move(item));
  }
  auto append_glyph=[&](views::View* button,gfx::RectF rect,gfx::RectF clip) {
    items.push_back(ActionSceneItem(button,rect,clip));
  };
  auto local_rect=[&](views::View* view) {
    auto rect=gfx::RectF(view->GetBoundsInScreen()); rect.Offset(-bounds.x(),-bounds.y()); return rect;
  };
  for (auto* button : {pin_button_.get(),workspace_settings_button_.get()}) {
    const auto rect=local_rect(button); append_glyph(button,rect,rect);
  }
  auto* dot_viewport=workspace_dots_->parent();
  {
    // The workspace rail: a recessed `bg-void` pill with an inner `line` edge.
    auto rail=local_rect(dot_viewport);const float width=std::min<float>(rail.width(),workspace_dots_->width()+8);
    rail=gfx::RectF(rail.x()+(rail.width()-width)/2,rail.y()+(rail.height()-28)/2,width,28);
    sidebar::Item item{};item.kind=24;item.x=rail.x();item.y=rail.y();item.width=rail.width();item.height=rail.height();
    item.clip={rail.x(),rail.y(),rail.width(),rail.height()};item.fill=Palette(dot_viewport).bg_void;item.ink=Hairline(dot_viewport);
    if(workspace_dots_->width()>0)items.push_back(std::move(item));
  }
  std::vector<SidebarViewportNode> dot_nodes{{SidebarTree::kRoot,gfx::Rect(workspace_dots_->size()),true}};
  for (const auto& child : workspace_dots_->children()) dot_nodes.push_back({0,child->GetMirroredBounds(),child->GetVisible()});
  // Nodes are physical (mirrored) content bounds; in RTL the visible window
  // starts at maximum-offset, so project with the physical offset.
  const bool dot_rtl=base::i18n::IsRTL();const double dot_max=workspace_scroll_->maximum();
  const auto dot_physical=[&](double offset){return dot_rtl?dot_max-offset:offset;};
  const gfx::PointF dot_requested(static_cast<float>(dot_physical(workspace_scroll_->offset())),0);
  const auto dot_projection=metal_sidebar_->Project(workspace_dots_->size(),dot_viewport->size(),dot_requested,dot_nodes);
  if (dot_projection.offset!=dot_requested) {
    workspace_scroll_->ScrollTo(dot_physical(dot_projection.offset.x())); ScheduleMetalSidebar(); return;
  }
  const auto dot_origin=local_rect(dot_viewport).OffsetFromOrigin();
  for (const auto& node : dot_projection.nodes) if (node.index>0) {
    auto rect=node.bounds;rect.Offset(dot_origin);auto clip=node.clip;clip.Offset(dot_origin);
    append_glyph(workspace_dots_->children()[node.index-1].get(),rect,clip);
  }
  const auto horizontal=GetSidebarScrollBarSnapshot(workspace_scroll_->horizontal_scroll_bar());
  if (horizontal.visible) {
    auto rect=horizontal.thumb;rect.Offset(-bounds.x(),-bounds.y());auto clip=horizontal.bounds;clip.Offset(-bounds.x(),-bounds.y());
    sidebar::Item item{};item.kind=6;item.tile=true;item.fill=horizontal.color;
    item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();
    item.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};items.push_back(std::move(item));
  }
  if (scroller.visible) {
    auto rect=scroller.thumb; rect.Offset(-bounds.x(),-bounds.y());
    auto clip=scroller.bounds; clip.Offset(-bounds.x(),-bounds.y());
    sidebar::Item item{}; item.kind=6; item.fill=scroller.color;
    item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
    item.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};
    items.push_back(std::move(item));
  }
  if (sidebar_reorder_) {
    // Drop target: a line where the row lands, or a wash over the folder that receives it.
    auto rect=gfx::RectF(sidebar_reorder_->indicator);rect.Offset(-bounds.x(),-bounds.y());
    const auto list=gfx::RectF(list_origin.x(),list_origin.y(),list_bounds.width(),list_bounds.height());
    sidebar::Item item{};item.kind=sidebar_reorder_->into_folder?24:14;item.active=true;
    item.fill=SkColorSetA(workspace_color(state_.workspace_id),sidebar_reorder_->into_folder?0x40:0xFF);
    item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();
    item.clip={list.x(),list.y(),list.width(),list.height()};items.push_back(std::move(item));
  }
  if (auto* rust_row=views::AsViewClass<RustRowView>(drag_row_.view()); rust_row && drag_point_) {
    // A lifted copy of the dragged row follows the pointer above everything else.
    const gfx::Point origin=*drag_point_-drag_grab_.OffsetFromOrigin()-bounds.OffsetFromOrigin();
    const gfx::RectF ghost(origin.x(),origin.y(),rust_row->width(),rust_row->height());
    const gfx::RectF all(gfx::SizeF(bounds.size()));
    auto scene=rust_row->SidebarScene(ghost,all);
    if(scene.valid) {
      sidebar::Item shadow{};shadow.kind=24;shadow.active=true;shadow.fill=SkColorSetA(SK_ColorBLACK,IsDark(root_)?0x59:0x1F);
      shadow.x=ghost.x()+1;shadow.y=ghost.y()+3;shadow.width=ghost.width();shadow.height=ghost.height();shadow.clip={all.x(),all.y(),all.width(),all.height()};
      items.push_back(std::move(shadow));
      auto& i=scene.item;i.fill=SkColorSetA(Tone(root_,kCard),0xF2);i.active=true;i.hovered=false;i.focused=false;i.close_visible=false;
      items.push_back(std::move(i));
    }
  }
  const SkColor tint = FrameTint(workspace_color(state_.workspace_id), root_);
  auto* sidebar=static_cast<SidebarView*>(sidebar_root_.get());auto* grip=sidebar->grip.get();auto* handle=sidebar->grip_handle.get();
  if(!metal_sidebar_grip_)metal_sidebar_grip_=CreateSidebarMetal();
  if(metal_sidebar_grip_) {
    const bool before=metal_sidebar_grip_->visible();auto frame=grip->GetBoundsInScreen();frame.Inset(gfx::Insets::TLBR(8,3,8,1));
    sidebar::Item item{};item.kind=11;item.x=-3;item.y=-8;item.width=grip->width();item.height=grip->height();item.clip={0,0,float(frame.width()),float(frame.height())};
    item.active=handle->active();item.hovered=handle->hovered();item.ink=SkColorSetA(Tone(grip,kInk),item.active?0x40:0x26);
    metal_sidebar_grip_->Update(sidebar_widget_->GetNativeWindow(),frame,{item},pinned_?tint:Mix(tint,Tone(root_,kCard),70));
    if(before!=metal_sidebar_grip_->visible())grip->SchedulePaint();
  }
  metal_sidebar_->Update(sidebar_widget_->GetNativeWindow(), bounds, items, pinned_ ? tint : Mix(tint, Tone(root_, kCard), 70));
  // The initial native paint record can contain fallback text. Re-record it
  // once when GPU presentation takes ownership, then the control paint gate
  // keeps it empty until fallback is needed again.
  if (!was_visible && metal_sidebar_->visible()) { tab_list_->SchedulePaint(); new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint(); }
  // A failed drawable must expose freshly painted fallback content, even if
  // the last native paint was skipped while Metal still had a visible frame.
  if (was_visible && !metal_sidebar_->visible()) { tab_list_->SchedulePaint(); new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint(); }
}

void WorkspaceShell::ConfigureSidebarContextMenus(views::View* view) {
  ++sidebar_context_visit_count_;
  if (!view->context_menu_controller()) view->set_context_menu_controller(this);
  if (IsRow(view)||IsGlyphAction(view)||views::AsViewClass<views::Button>(view)) return;
  for (const auto& child : view->children()) ConfigureSidebarContextMenus(child.get());
}

void WorkspaceShell::WarmWorkspaceSidebars(std::map<std::string, std::vector<PinnedItem>> pins) {
  sidebar_warm_timer_.Stop(); sidebar_warm_queue_.clear();
  for (const auto& workspace : state_.workspaces) {
    if (workspace.id == state_.workspace_id || tab_lists_.contains(workspace.id)) continue;
    auto found = pins.find(workspace.id);
    if (found != pins.end() && !found->second.empty())
      sidebar_warm_queue_.emplace_back(workspace.id, std::move(found->second));
  }
  if (!sidebar_warm_queue_.empty()) sidebar_warm_timer_.Start(FROM_HERE, base::Milliseconds(200),
      base::BindOnce(&WorkspaceShell::WarmNextSidebar, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::WarmNextSidebar() {
  if (!widget_ || sidebar_warm_queue_.empty()) return;
  auto [workspace, pins] = std::move(sidebar_warm_queue_.front());
  sidebar_warm_queue_.erase(sidebar_warm_queue_.begin());
  const bool exists = std::ranges::any_of(state_.workspaces, [&](const auto& item) { return item.id == workspace; });
  if (exists && !tab_lists_.contains(workspace) && workspace != state_.workspace_id) {
    auto& cache = tab_lists_[workspace];
    ReconcileSidebarNodes(cache,workspace,pins,{});
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(CreateSidebarColumnLayout());
    root->SetVisible(false); root->set_context_menu_controller(this); cache.root = root.get();
    cache.pins_root = root->AddChildView(std::make_unique<views::View>());
    cache.pins_root->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    cache.tabs_root = root->AddChildView(std::make_unique<views::View>());
    cache.tabs_root->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));
    Label(contents.get(), T(u"PRZYPIĘTE"), kMuted, Font(-2));
    AppendPinnedItems(contents.get(), pins, cache, workspace);
    Spacer(contents.get(), 8);
    cache.pins_root->AddChildView(std::move(contents));
    cache.pins = std::move(pins); ++pinned_list_build_count_;
    ConfigureSidebarContextMenus(root.get());
    tab_list_->AddChildView(std::move(root));
  }
  if (!sidebar_warm_queue_.empty()) sidebar_warm_timer_.Start(FROM_HERE, base::Milliseconds(100),
      base::BindOnce(&WorkspaceShell::WarmNextSidebar, weak_factory_.GetWeakPtr()));
}

void WorkspaceShell::CancelWorkspaceSwipe(bool reset_state) {
  if(reset_state)workspace_swipe_->Cancel();
  ScheduleMetalSidebar();
  swipe_idle_timer_.Stop(); swipe_settle_timer_.Stop();
  swipe_committing_ = false;
  if (swipe_overlay_) {
    auto* overlay = swipe_overlay_.get(); swipe_overlay_ = nullptr;
    sidebar_root_->RemoveChildViewT(overlay);
    if(tab_list_)tab_list_->SchedulePaint();
    RefreshWorkspaceHeading();
    for (const auto& [id, button] : workspace_buttons_)
      SetActionOn(button,id == state_.workspace_id);
  }
}
bool WorkspaceShell::workspace_swipe_metal_visible() const {return swipe_overlay_&&static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->metal_owned();}
size_t WorkspaceShell::workspace_swipe_metal_frame_count() const {return swipe_overlay_?static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->metal_frame_count():0;}
bool WorkspaceShell::workspace_swipe_contains_scene_text(std::string_view text) const {return swipe_overlay_&&static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->contains_scene_text(text);}
size_t WorkspaceShell::workspace_swipe_native_capture_count() const {return swipe_overlay_?static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->native_capture_count():0;}
double WorkspaceShell::workspace_swipe_position() const { return workspace_swipe_->position(); }
size_t WorkspaceShell::workspace_swipe_preview_count() const {
  return swipe_overlay_ ? static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->page_count() : 0;
}
std::vector<sidebar::Item> WorkspaceShell::CaptureWorkspaceScene(std::string_view id,gfx::Size size,int offset,bool source) {
  auto* shell=this;
  const auto found=shell->tab_lists_.find(std::string(id));const TabListCache* cache=found==shell->tab_lists_.end()?nullptr:&found->second;
  const std::vector<PinnedItem>* pins=cache?&cache->pins:nullptr;
  if(!pins){auto queued=std::find_if(shell->sidebar_warm_queue_.begin(),shell->sidebar_warm_queue_.end(),[&](const auto& entry){return entry.first==id;});if(queued!=shell->sidebar_warm_queue_.end())pins=&queued->second;}
  std::vector<sidebar::Item> data;std::vector<sidebar::PreviewRow> rows;
  auto make_item=[&](const RowRecipe::Spec& spec){
    sidebar::Item item{};item.kind=0;item.title=base::UTF16ToUTF8(spec.title);item.monogram=base::UTF16ToUTF8(spec.icon?u"📁":spec.monogram);
    item.ink=Tone(shell->root_,kInk);item.badge=spec.icon?SK_ColorTRANSPARENT:spec.monogram_color;
    item.fill=spec.tile?SkColorSetA(Tone(shell->root_,kCard),spec.active?0xFF:0x59):spec.active?Tone(shell->root_,kCard):spec.highlighted?Tone(shell->root_,kAccentWash):SK_ColorTRANSPARENT;
    item.edge=spec.active&&!spec.tile?Hairline(shell->root_):SK_ColorTRANSPARENT;item.active=spec.active;item.tile=spec.tile;item.close=bool(spec.close);item.close_visible=spec.close&&spec.active&&!spec.tile;item.font_size=Font(0).GetFontSize();return item;
  };
  auto gap=[&](int height){rows.push_back({0,3,0,height});};
  auto row=[&](const RowRecipe::Spec& spec,int depth){rows.push_back({data.size(),0,depth,34});data.push_back(make_item(spec));};
  auto pair=[&](const SidebarTabSlot& slot,int depth){
    // Tab plans contain both identities; resolve before publishing a group.
    std::vector<views::View*> members;
    for(size_t member:slot.members){const auto found=cache->tab_buttons.find(cache->tabs[member].index);if(found==cache->tab_buttons.end())return;members.push_back(found->second.get());}
    rows.push_back({data.size(),2,depth,46,members.size()});sidebar::Item group{};group.kind=2;group.fill=SkColorSetA(Tone(shell->root_,kCard),0x33);data.push_back(std::move(group));
    for(auto* member:members)data.push_back(WithRowSnapshot(member,make_item));
  };
  const SidebarTabSlot* pair_slot=nullptr;if(cache)for(const auto& slot:cache->tab_plan.slots)if(slot.pair){pair_slot=&slot;break;}
  std::vector<bool> visible(pins?pins->size():0,true);if(cache)for(const auto& pin:cache->tab_plan.pins)visible[pin.index]=pin.visible;
  if(pins&&!pins->empty()) {
    rows.push_back({data.size(),1,0,Font(-2).GetHeight()});sidebar::Item label{};label.kind=1;label.title=base::UTF16ToUTF8(T(u"PRZYPIĘTE"));label.ink=Tone(shell->root_,kMuted);label.font_size=Font(-2).GetFontSize();label.text_style=7;data.push_back(std::move(label));
    std::vector<SidebarTreeNode> nodes;for(const auto& pin:*pins)nodes.push_back({pin.id,pin.parent_id,pin.folder});
    const auto plan=shell->sidebar_tree_.Plan(id,nodes);std::vector<int> groups;
    for(const auto& entry:plan) {
      if(!entry.visible)continue;
      while(!groups.empty()&&groups.back()>=entry.depth){gap(2);groups.pop_back();}
      const auto& pin=(*pins)[entry.index];auto button=cache?cache->pin_buttons.find(pin.id):std::map<std::string,raw_ptr<views::View>>::const_iterator();
      if(cache&&button!=cache->pin_buttons.end()) {
        if(pair_slot&&pair_slot->pin==entry.index)pair(*pair_slot,entry.depth);
        else if(visible[entry.index])WithRowSnapshot(button->second.get(),[&](const auto& spec){row(spec,entry.depth);});
      } else {
        RowRecipe::Spec spec;spec.title=base::UTF8ToUTF16(pin.title);
        if(pin.folder){spec.icon=&vector_icons::kFolderFilledIcon;spec.title=(entry.collapsed?u"▸ ":u"▾ ")+spec.title;}
        else {spec.monogram=base::i18n::ToUpper(spec.title.substr(0,1));spec.monogram_color=kMonogramColors[base::FastHash(pin.url.host())%kMonogramColors.size()];}
        row(spec,entry.depth);
      }
      if(pin.folder&&!entry.collapsed)groups.push_back(entry.depth);
    }
    gap(static_cast<int>(groups.size())*2+8);
  }
  if(cache)for(const auto& slot:cache->tab_plan.slots){
    if(slot.pair){if(slot.pin==SidebarTree::kRoot)pair(slot,0);continue;}
    auto button=cache->tab_buttons.find(cache->tabs[slot.first].index);if(button!=cache->tab_buttons.end())WithRowSnapshot(button->second.get(),[&](const auto& spec){row(spec,0);});
  }
  if(!cache||(cache->tabs.empty()&&!source)){RowRecipe::Spec blank;blank.title=T(u"Nowa karta");blank.monogram=u"N";row(blank,0);}
  const auto plan=sidebar::workspace_preview_layout(size.width(),size.height(),offset,rust::Slice<const sidebar::PreviewRow>(rows.data(),rows.size()));
  std::vector<sidebar::Item> items;items.reserve(plan.size());
  for(const auto& placement:plan){auto item=std::move(data[placement.index]);const auto& r=placement.bounds;const auto& c=placement.clip;item.x=r.x;item.y=r.y;item.width=r.width;item.height=r.height;item.clip={float(c.x),float(c.y),float(c.width),float(c.height)};items.push_back(std::move(item));}
  return items;
}
void WorkspaceShell::UpdateWorkspaceSwipePreview() {
  if (metal_sidebar_ && metal_sidebar_->visible()) {
    metal_sidebar_->Hide();HideMetalSidebarGrip();
    new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint();
  }
  if (!swipe_overlay_) {
    StopWorkspaceAnimation();
    auto capture = base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, int index) -> cc::PaintRecord {
      if (!shell || index < 0 || index >= static_cast<int>(shell->swipe_workspace_ids_.size())) return {};
      const auto& id = shell->swipe_workspace_ids_[index];
      const auto size = shell->swipe_overlay_->size();
      auto list = base::MakeRefCounted<cc::DisplayItemList>();
      ui::PaintContext context(list.get(), 1.f, gfx::Rect(size), false);
      {
        ui::PaintRecorder recorder(context, size);
        auto* canvas = recorder.canvas(); canvas->ClipRect(gfx::Rect(size));
        const auto cached = shell->tab_lists_.find(id);
        const TabListCache* cache = cached == shell->tab_lists_.end() ? nullptr : &cached->second;
        const std::vector<PinnedItem>* pins = cache ? &cache->pins : nullptr;
        if (!pins) {
          auto queued = std::find_if(shell->sidebar_warm_queue_.begin(), shell->sidebar_warm_queue_.end(),
              [&](const auto& entry) { return entry.first == id; });
          if (queued != shell->sidebar_warm_queue_.end()) pins = &queued->second;
        }
        int y = id == shell->swipe_source_ ? -shell->tab_scroll_->visible_rect().y() : 0;
        auto row = [&](const RowRecipe::Spec& spec, int indent, int row_height) {
          if (y + row_height > 0 && y < size.height())
            RowRecipe::PaintPreview(canvas, gfx::Rect(indent, y, std::max(0, size.width() - indent), row_height), spec, shell->root_);
          y += row_height + 2;
        };
        const SidebarTabSlot* pair_slot = nullptr;
        if (cache) for (const auto& slot : cache->tab_plan.slots) if (slot.pair) {pair_slot = &slot; break;}
        std::vector<bool> pin_visibility(pins ? pins->size() : 0, true);
        if (cache) for (const auto& pin : cache->tab_plan.pins) pin_visibility[pin.index] = pin.visible;
        auto pair = [&](const SidebarTabSlot& slot, int indent) {
          const int count = std::max<int>(2, slot.members.size());
          const int half = std::max(0, size.width() - indent - 4 * (count + 1)) / count;
          if (y + 46 > 0 && y < size.height())
            FillRound(canvas, gfx::Rect(indent, y, size.width() - indent, 46), 12, SkColorSetA(Tone(shell->root_, kCard), 0x33));
          int x = indent + 4;
          for (size_t position : slot.members) {
            const auto& tab = cache->tabs[position];
            auto button = cache->tab_buttons.find(tab.index);
            if (button == cache->tab_buttons.end()) continue;
            if (y + 38 > 0 && y < size.height())
              WithRowSnapshot(button->second.get(),[&](const auto& spec){RowRecipe::PaintPreview(canvas,gfx::Rect(x,y+4,half,38),spec,shell->root_);});
            x += half + 4;
          }
          y += 48;
        };
        if (pins && !pins->empty()) {
          const int label_height = Font(-2).GetHeight();
          canvas->DrawStringRectWithFlags(T(u"PRZYPIĘTE"), Font(-2), Tone(shell->root_, kMuted),
              gfx::Rect(0, y, size.width(), label_height), gfx::Canvas::TEXT_ALIGN_LEFT);
          y += label_height + 2;
          std::vector<SidebarTreeNode> nodes;
          nodes.reserve(pins->size());
          for (const auto& pin : *pins) nodes.push_back({pin.id, pin.parent_id, pin.folder});
          const auto plan = shell->sidebar_tree_.Plan(id, nodes);
          std::vector<int> open_groups;
          for (const auto& entry : plan) {
            if (!entry.visible) continue;
            while (!open_groups.empty() && open_groups.back() >= entry.depth) {
              y += 2; open_groups.pop_back();
            }
            const auto* pin = &(*pins)[entry.index];
            const int indent = SidebarFolderIndent(entry.depth);
            auto button = cache ? cache->pin_buttons.find(pin->id) : std::map<std::string, raw_ptr<views::View>>::const_iterator();
            if (cache && button != cache->pin_buttons.end()) {
              if (pair_slot && pair_slot->pin == entry.index) pair(*pair_slot, indent);
              else if (pin_visibility[entry.index])
                WithRowSnapshot(button->second.get(),[&](const auto& spec){row(spec,indent,34);});
            } else {
              RowRecipe::Spec spec; spec.title = base::UTF8ToUTF16(pin->title);
              if (pin->folder) {
                spec.icon = &vector_icons::kFolderFilledIcon;
                spec.title = (entry.collapsed ? u"▸ " : u"▾ ") + spec.title;
              } else {
                spec.monogram = base::i18n::ToUpper(spec.title.substr(0, 1));
                spec.monogram_color = kMonogramColors[base::FastHash(pin->url.host()) % kMonogramColors.size()];
              }
              row(spec, indent, 34);
            }
            if (pin->folder && !entry.collapsed) open_groups.push_back(entry.depth);
          }
          y += static_cast<int>(open_groups.size()) * 2 + 8;
        }
        if (cache) for (const auto& slot : cache->tab_plan.slots) {
          if (slot.pair) { if (slot.pin == SidebarTree::kRoot) pair(slot, 0); continue; }
          const auto& tab = cache->tabs[slot.first];
          auto button = cache->tab_buttons.find(tab.index);
          if (button != cache->tab_buttons.end())
            WithRowSnapshot(button->second.get(),[&](const auto& spec){row(spec,0,34);});
        }
        if (!cache || (cache->tabs.empty() && id != shell->swipe_source_)) {
          RowRecipe::Spec blank; blank.title = T(u"Nowa karta"); blank.monogram = u"N";
          row(blank, 0, 34);
        }
      }
      list->Finalize();
      return list->paint_op_buffer().DeepCopyAsRecord();
    }, weak_factory_.GetWeakPtr());
    auto preview = std::make_unique<WorkspaceSwipePreview>(std::move(capture));
    if(metal_sidebar_) {
      const auto tint=FrameTint(workspace_color(swipe_source_),root_);
      preview->SetSceneCapture(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,int index)->std::vector<sidebar::Item>{
        if(!shell||index<0||index>=static_cast<int>(shell->swipe_workspace_ids_.size())||!shell->swipe_overlay_)return {};
        const auto& id=shell->swipe_workspace_ids_[index];const auto size=shell->swipe_overlay_->size();
        const bool source=id==shell->swipe_source_;
        return shell->CaptureWorkspaceScene(id,size,source?shell->tab_scroll_->visible_rect().y():0,source);
      },weak_factory_.GetWeakPtr()),pinned_?tint:Mix(tint,Tone(root_,kCard),70));
    }
    swipe_overlay_ = sidebar_root_->AddChildView(std::move(preview));
    swipe_overlay_->SetBoundsRect(tab_scroll_->view()->bounds());
    const auto tint = FrameTint(workspace_color(swipe_source_), root_);
    swipe_overlay_->SetBackground(views::CreateSolidBackground(pinned_ ? tint : Mix(tint, Tone(root_, kCard), 70)));
  }
  auto* preview = static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get());
  const auto previous_center = preview->center();
  preview->Move(workspace_swipe_->position(), swipe_workspace_ids_.size(), false);
  const int nearest = workspace_swipe_->target(state_.workspaces.size());
  if (previous_center == nearest) return;
  const auto& workspace = state_.workspaces[nearest];
  const auto emoji = workspace_emoji(workspace.id);
  static_cast<WorkspaceHeading*>(workspace_name_.get())->SetText((emoji.empty() ? u"" : emoji + u"  ") + base::UTF8ToUTF16(workspace.name));
  for (const auto& [id, button] : workspace_buttons_)
    SetActionOn(button,id == workspace.id);
}
void WorkspaceShell::FinishWorkspaceSwipe() {
  swipe_idle_timer_.Stop(); workspace_swipe_->Finish();
  if (!swipe_overlay_) return;
  const int target = workspace_swipe_->target(swipe_workspace_ids_.size());
  swipe_commit_target_ = swipe_workspace_ids_[target]; swipe_committing_ = true;
  static_cast<WorkspaceSwipePreview*>(swipe_overlay_.get())->Move(target, swipe_workspace_ids_.size(),
      workspace_animations_ && !gfx::Animation::PrefersReducedMotion());
  const auto source = swipe_source_, destination = swipe_commit_target_;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, std::string source, std::string destination) {
        if (!shell || !shell->swipe_committing_ || shell->swipe_commit_target_ != destination || shell->state_.workspace_id != source) return;
        if (source != destination) {
          shell->callbacks_.select_workspace.Run(destination);
          if (shell->tab_scroll_) shell->tab_scroll_->ScrollTo(0);
        }
        shell->swipe_settle_timer_.Start(FROM_HERE,
            shell->workspace_animations_ && !gfx::Animation::PrefersReducedMotion() ? base::Milliseconds(180) : base::TimeDelta(),
            base::BindOnce(&WorkspaceShell::CancelWorkspaceSwipe, shell, true));
      }, weak_factory_.GetWeakPtr(), source, destination));
}
bool WorkspaceShell::HandleWorkspaceScroll(float x, float y, bool began, bool ended, bool momentum, base::TimeTicks time, bool phase_less) {
  if (!sidebar_visible() || !state_.configuration_ready || state_.profiles_busy || state_.workspaces.size() < 2 || folder_dialog_ ||
      (workspace_menu_runner_ && workspace_menu_runner_->IsRunning())) return false;
  const int index=std::find_if(state_.workspaces.begin(),state_.workspaces.end(),[&](const auto& w){return w.id==state_.workspace_id;})-state_.workspaces.begin();
  const auto update=workspace_swipe_->Event(x,y,tab_scroll_->view()->width(),index,state_.workspaces.size(),began,ended,momentum,phase_less);
  if(update.started) {
    // Retire the previous native preview/timers without resetting the new Rust
    // gesture. Workspace identities remain in the application adapter.
    CancelWorkspaceSwipe(false);
    swipe_workspace_ids_.clear();
    for(const auto& workspace:state_.workspaces)swipe_workspace_ids_.push_back(workspace.id);
    swipe_source_=state_.workspace_id;
  }
  swipe_last_time_=time;
  if(update.preview)UpdateWorkspaceSwipePreview();
  if(update.finish)FinishWorkspaceSwipe();
  else if(workspace_swipe_->tracking()&&phase_less&&!momentum)swipe_idle_timer_.Start(FROM_HERE,base::Milliseconds(140),
      base::BindOnce(&WorkspaceShell::FinishWorkspaceSwipe,weak_factory_.GetWeakPtr()));
  return update.handled;
}
void WorkspaceShell::OnScrollEvent(ui::ScrollEvent* event) {
  last_native_scroll_time_ = event->time_stamp();
  if (event->flags() & (ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)) return;
  // Cocoa currently puts finger-down/up into momentum_phase, leaving
  // scroll_event_phase unset (see GetScrollOffsets in events_mac.mm).
  // MAY_BEGIN is a fresh touch, not inertia: it must re-arm a stationary cursor.
  const bool began = event->scroll_event_phase() == ui::ScrollEventPhase::kBegan ||
      event->momentum_phase() == ui::EventMomentumPhase::MAY_BEGIN;
  const bool momentum = event->momentum_phase() == ui::EventMomentumPhase::BEGAN ||
      event->momentum_phase() == ui::EventMomentumPhase::INERTIAL_UPDATE;
  const bool phase_less = event->scroll_event_phase() == ui::ScrollEventPhase::kNone &&
      event->momentum_phase() == ui::EventMomentumPhase::END &&
      (event->x_offset() != 0 || event->y_offset() != 0) && (!workspace_swipe_->tracking() || workspace_swipe_->phase_less());
  const bool ended = !phase_less && (event->scroll_event_phase() == ui::ScrollEventPhase::kEnd ||
      event->momentum_phase() == ui::EventMomentumPhase::END);
  if (HandleWorkspaceScroll(event->x_offset(), event->y_offset(), began, ended, momentum,
      event->time_stamp(), phase_less)) event->StopPropagation();
}
void WorkspaceShell::OnMouseEvent(ui::MouseEvent* input) {
  if (!input->IsMouseWheelEvent()) return;
  auto* event = input->AsMouseWheelEvent();
  // Widget synthesizes a wheel event after an unhandled ScrollEvent. Its
  // phase-less copy must not count the delta twice or turn inertia into a swipe.
  if (event->time_stamp() == last_native_scroll_time_) return;
  if (event->flags() & (ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)) return;
  const float x = event->x_offset() ? event->x_offset() : event->IsShiftDown() ? event->y_offset() : 0;
  const float y = event->IsShiftDown() ? 0 : event->y_offset();
  if (HandleWorkspaceScroll(x, y, false, false, false, event->time_stamp(), true)) event->StopPropagation();
}

// Toggling a folder ends the peek of items revealed in it: expanded shows every
// row, collapsed hides them all. Nested folder groups keep their own state.
void WorkspaceShell::EndFolderPeek(const std::string& workspace,const std::string& folder,views::View* group,bool collapsed){
  for(const auto& pin:state_.pins)if(pin.parent_id==folder)sidebar_tree_.SetRevealed(workspace,pin.id,false);
  for(const auto& child:group->children())if(child->GetProperty(kPinRowHolder))child->SetVisible(true);
  group->SetVisible(!collapsed);
}
void WorkspaceShell::AppendPinnedItems(views::View* root, const std::vector<PinnedItem>& pins,
    TabListCache& cache,const std::string& workspace,std::map<std::string,std::unique_ptr<views::View>>* retained,
    std::map<uint64_t,std::unique_ptr<views::View>>* retained_groups,std::map<std::string,std::unique_ptr<views::View>>* retained_holders) {
  struct Entry {size_t index;uint64_t parent,group;int depth;bool collapsed,visible;};std::vector<Entry> plan;
  if(cache.node_graph){const auto tree=cache.node_graph->graph->pin_tree();for(const auto& entry:tree.entries){CHECK_LT(entry.index,pins.size());CHECK_EQ(pins[entry.index].id,std::string(entry.key));plan.push_back({entry.index,entry.parent==tree.root?0:entry.parent,entry.group,entry.depth,entry.collapsed,entry.visible});}}else
  {std::vector<SidebarTreeNode> nodes;for(const auto& pin:pins)nodes.push_back({pin.id,pin.parent_id,pin.folder});for(const auto& entry:sidebar_tree_.Plan(workspace,nodes))plan.push_back({entry.index,entry.parent==SidebarTree::kRoot?0:entry.parent+1,pins[entry.index].folder?entry.index+1:0,entry.depth,entry.collapsed,entry.visible});}
  std::map<uint64_t,views::View*> groups{{0,root}};std::map<uint64_t,bool> collapsed_groups{{0,false}};
  // Collapsed folders still show (and open just for) the items revealed in them.
  std::set<std::string> peeking;for(const auto& pin:pins)if(sidebar_tree_.Revealed(workspace,pin.id))peeking.insert(pin.parent_id);
  for(const auto& entry:plan){
    const auto* pin=&pins[entry.index];auto* parent=groups.at(entry.parent);
    const bool shown_in_parent=!collapsed_groups.at(entry.parent)||sidebar_tree_.Revealed(workspace,pin->id);
    RowRecipe::Spec spec;
    spec.sidebar_row = true;
    if(!cache.node_graph){spec.title=base::UTF8ToUTF16(pin->title);if(pin->folder){spec.icon=&vector_icons::kFolderFilledIcon;spec.title=(entry.collapsed?u"▸ ":u"▾ ")+spec.title;}}
    if(cache.node_graph)spec.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak_factory_.GetWeakPtr());
    if (!pin->folder) {
      spec.drop=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string workspace,std::string id,gfx::Point point){
        if(shell)shell->HandleSidebarDrop(std::move(workspace),std::nullopt,{},std::move(id),point);
      },weak_factory_.GetWeakPtr(),workspace,pin->id);
      spec.drag=base::BindRepeating(&WorkspaceShell::HandleSidebarDrag,weak_factory_.GetWeakPtr());
      if(!cache.node_graph){spec.monogram=base::i18n::ToUpper(spec.title.substr(0,1));spec.monogram_color=kMonogramColors[base::FastHash(pin->url.host())%kMonogramColors.size()];}
    }
    std::unique_ptr<views::View> holder;
    if(retained_holders){auto found=retained_holders->find(pin->id);if(found!=retained_holders->end()){holder=std::move(found->second);retained_holders->erase(found);}}
    auto* row = parent->AddChildView(holder?std::move(holder):std::make_unique<views::View>());
    row->SetProperty(kPinRowHolder,true);row->SetVisible(shown_in_parent);
    row->SetLayoutManager(CreateSidebarPinnedRowLayout());
    row->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, SidebarFolderIndent(entry.depth), 0, 0)));
    std::unique_ptr<views::View> adapter;[[maybe_unused]] bool fresh=true;
    if(cache.node_graph&&retained){auto found=retained->find(pin->id);if(found!=retained->end()){fresh=false;adapter=std::move(found->second);retained->erase(found);auto* control=views::AsViewClass<RustRowView>(adapter.get());CHECK(control);control->ProjectPinAdapter(std::move(spec));}}
    if(!adapter)adapter=CreateSceneRow({},std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{cache.node_graph,2,{},pin->id});
    auto* button=row->AddChildView(std::move(adapter));
    views::View* folder_group=nullptr;
    if(pin->folder){
      if(cache.node_graph){auto* control=views::AsViewClass<RustRowView>(button);CHECK(control);
        std::unique_ptr<views::View> group;if(retained_groups){auto found=retained_groups->find(entry.group);if(found!=retained_groups->end()){group=std::move(found->second);retained_groups->erase(found);}}
        if(group&&views::AsViewClass<ScenePinGroup>(group.get())->Matches(cache.node_graph,entry.group))folder_group=parent->AddChildView(std::move(group));
        else folder_group=parent->AddChildView(std::make_unique<ScenePinGroup>(cache.node_graph,entry.group));}else
      folder_group=parent->AddChildView(std::make_unique<views::View>());
      folder_group->SetLayoutManager(CreateSidebarColumnLayout(kSidebarRowGap));groups[entry.group]=folder_group;collapsed_groups[entry.group]=entry.collapsed;folder_group->SetVisible(shown_in_parent&&(!entry.collapsed||peeking.contains(pin->id)));
    }
    if(cache.node_graph){
      auto* control=views::AsViewClass<RustRowView>(button);CHECK(control);const auto binding=control->PinBinding();CHECK(binding.valid&&binding.folder==pin->folder);
      if(fresh)control->SetCallback(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,base::WeakPtr<RustRowView> row,std::weak_ptr<SidebarNodeGraphState> owner,std::string workspace,std::string key,uint64_t id){
        const auto scope=owner.lock();if(!shell||!row||!scope||shell->state_.workspace_id!=workspace||row->GetWidget()!=shell->sidebar_widget_.get()||!SceneIsVisible(row.get())||!row->GetEnabledInViewsSubtree()||row->LogicalNodeId()!=id||!row->BelongsTo(*scope))return;
        const auto cache=shell->tab_lists_.find(workspace);if(cache==shell->tab_lists_.end()||cache->second.node_graph!=scope||!cache->second.root||!cache->second.root->Contains(row.get()))return;const auto cached=cache->second.pin_buttons.find(key);if(cached==cache->second.pin_buttons.end()||cached->second!=row.get())return;
        const auto target=scope->graph->pin_target(key,id);if(!target.valid)return;const auto pin=std::find_if(shell->state_.pins.begin(),shell->state_.pins.end(),[&](const PinnedItem& item){return item.id==key&&item.folder==target.folder;});if(pin==shell->state_.pins.end())return;
        if(!target.folder){shell->callbacks_.open_pin.Run(key);return;}
        auto* holder=row->parent();auto* container=holder?holder->parent():nullptr;if(!container)return;const auto index=container->GetIndexOf(holder);if(!index||*index+1>=container->children().size())return;auto* group=views::AsViewClass<ScenePinGroup>(container->children()[*index+1].get());if(!group||!group->Matches(scope,target.group)||group->GetWidget()!=shell->sidebar_widget_.get()||!cache->second.root->Contains(group))return;
        const bool collapsed=shell->sidebar_tree_.Toggle(workspace,key);if(!row->ProjectPinCollapsed(*scope,collapsed)){shell->sidebar_tree_.SetCollapsed(workspace,key,target.collapsed);return;}shell->EndFolderPeek(workspace,key,group,collapsed);shell->ScheduleMetalSidebar();
      },weak_factory_.GetWeakPtr(),control->GetWeakPtr(),std::weak_ptr<SidebarNodeGraphState>(cache.node_graph),workspace,pin->id,binding.id));
    }else
    if(pin->folder){SetRowCallback(button,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,views::View* group,views::View* button,std::string workspace,std::string id,std::u16string title){if(!shell)return;const bool collapsed=shell->sidebar_tree_.Toggle(workspace,id);shell->EndFolderPeek(workspace,id,group,collapsed);SetRowTitle(button,(collapsed?u"▸ ":u"▾ ")+title);shell->ScheduleMetalSidebar();},weak_factory_.GetWeakPtr(),base::Unretained(folder_group),base::Unretained(button),workspace,pin->id,base::UTF8ToUTF16(pin->title)));}
    else SetRowCallback(button,base::BindRepeating(callbacks_.open_pin,pin->id));
    cache.pin_buttons[pin->id] = button;
    button->set_context_menu_controller(this);
  }
}

uint64_t WorkspaceShell::sidebar_node_id_for_testing(views::View* row) const{
  if(auto* group=views::AsViewClass<ScenePinGroup>(row))return group->NodeId();if(auto* host=views::AsViewClass<RustRowView>(row))return host->LogicalNodeId();if(auto* action=views::AsViewClass<RustActionView>(row))return action->LogicalNodeId();if(auto* site=views::AsViewClass<SiteRuleSceneRow>(row))return site->NodeId();return 0;
}
std::array<uint64_t,3> WorkspaceShell::sidebar_pair_for_testing() const{
  auto cache=tab_lists_.find(state_.workspace_id);
  if(cache!=tab_lists_.end()&&cache->second.node_graph){const auto tree=cache->second.node_graph->graph->sidebar_tabs();return {tree.pair,tree.parent,tree.visible?1u:0u};}
  return {};
}
uint64_t WorkspaceShell::scene_parent_id_for_testing(views::View* view) const{
  if(auto* group=views::AsViewClass<ScenePinGroup>(view))return group->ParentId();
  if(auto* action=views::AsViewClass<RustActionView>(view))return action->LogicalParentId();
  if(auto* row=views::AsViewClass<RustRowView>(view))return row->LogicalParentId();
  if(auto* row=views::AsViewClass<SiteRuleSceneRow>(view))return row->ParentId();
  return 0;
}
views::View* WorkspaceShell::pin_button(std::string_view id) const {
  auto cache = tab_lists_.find(state_.workspace_id);
  if (cache == tab_lists_.end()) return nullptr;
  auto button = cache->second.pin_buttons.find(std::string(id));
  return button == cache->second.pin_buttons.end() ? nullptr : button->second.get();
}
views::View* WorkspaceShell::tab_button(size_t index) const {
  auto cache = tab_lists_.find(state_.workspace_id);
  if (cache == tab_lists_.end()) return nullptr;
  auto button = cache->second.tab_buttons.find(index);
  return button == cache->second.tab_buttons.end() ? nullptr : button->second.get();
}

views::View* WorkspaceShell::workspace_button(std::string_view id) const {
  auto it = workspace_buttons_.find(std::string(id));
  return it == workspace_buttons_.end() ? nullptr : it->second.get();
}

void WorkspaceShell::ReconcileChromeNodes() {
  if(!chrome_node_graph_)chrome_node_graph_=std::make_shared<SidebarNodeGraphState>();
  auto units=[](const std::u16string& text){rust::Vec<uint16_t> result;for(char16_t unit:text)result.push_back(unit);return result;};
  rust::Vec<sidebar::WorkspaceSceneInput> items;items.reserve(state_.workspaces.size());
  for(const auto& workspace:state_.workspaces)items.push_back({workspace.id,units(base::UTF8ToUTF16(workspace.name)),units(workspace_emoji(workspace.id)),state_.sleeping_workspaces.contains(workspace.id),workspace.id==state_.workspace_id,state_.configuration_ready});
  CHECK(chrome_node_graph_->graph->reconcile_chrome(std::move(items)).admitted)<<"Invalid window chrome scene projection";
}
void WorkspaceShell::RebuildWorkspaceDots() {
  ReconcileChromeNodes();
  if(chrome_node_graph_){
    const auto scene=chrome_node_graph_->graph->workspace_scene();CHECK_NE(scene.root,0u);
    std::map<uint64_t,views::View*> existing;std::set<uint64_t> wanted;
    for(const auto& child:scene.children){CHECK_EQ(child.parent,scene.root);wanted.insert(child.id);}
    std::vector<views::View*> removed;
    // Inspect actual children, never stale cached pointers to detached adapters.
    for(const auto& child:workspace_dots_->children()){auto* adapter=views::AsViewClass<RustActionView>(child.get());const auto id=adapter?adapter->LogicalNodeId():0;if(id&&wanted.contains(id)&&adapter->BelongsTo(*chrome_node_graph_))existing.emplace(id,child.get());else removed.push_back(child.get());}
    workspace_buttons_.clear();
    for(auto* child:removed)workspace_dots_->RemoveChildViewT(child);
    size_t index=0;
    auto text=[](const rust::Vec<uint16_t>& units){return std::u16string(units.begin(),units.end());};
    for(const auto& child:scene.children){const std::string key(child.key);auto found=existing.find(child.id);views::View* button=nullptr;
      if(found!=existing.end())button=found->second;
      else button=workspace_dots_->AddChildView(CreateSceneAction(base::BindRepeating(callbacks_.select_workspace,key),text(child.name),GlyphButton::Glyph::kDot,nullptr,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),true,{chrome_node_graph_,3,{},key}));
      if(child.cancel_input)CancelActionInput(button);
      workspace_dots_->ReorderChildView(button,index++);button->SetTooltipText(text(child.name));button->GetViewAccessibility().SetName(text(child.name));SetActionLabel(button,text(child.label));SetActionColor(button,workspace_color(key));if(auto* action=views::AsViewClass<RustActionView>(button)){std::u16string pill;for(const auto& w:state_.workspaces)if(w.id==key&&workspace_names_)pill=base::UTF8ToUTF16(w.name);action->SetPillName(std::move(pill));}SetActionOn(button,child.active);button->SetEnabled(child.enabled);
      // Rust may update input enablement before the native setter sees a delta.
      // AX also follows native ancestor enablement, not just the local property.
      if(button->GetEnabledInViewsSubtree())button->GetViewAccessibility().SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);else button->GetViewAccessibility().RemoveDefaultActionVerb();
      // Rust updated input visuals before native projection; invalidate the
      // canvas fallback even when SetOn/CancelInput now see no state delta.
      button->SchedulePaint();button->set_context_menu_controller(this);workspace_buttons_[key]=button;
    }
    ScheduleMetalSidebar();return;
  }
  workspace_buttons_.clear();
  workspace_dots_->RemoveAllChildViews();
  for (const auto& workspace : state_.workspaces) {
    auto* button = workspace_dots_->AddChildView(CreateSceneAction(
        base::BindRepeating(callbacks_.select_workspace, workspace.id),
        state_.sleeping_workspaces.contains(workspace.id) ? TF(u"Przestrzeń: {} · uśpiona", {base::UTF8ToUTF16(workspace.name)}) : TF(u"Przestrzeń: {}", {base::UTF8ToUTF16(workspace.name)}), GlyphButton::Glyph::kDot,nullptr,
        base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {return shell && shell->metal_sidebar_visible();},weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),true,{chrome_node_graph_,3,{},workspace.id}));
    SetActionLabel(button,workspace_emoji(workspace.id));
    SetActionColor(button,workspace_color(workspace.id));
    if(auto* action=views::AsViewClass<RustActionView>(button))action->SetPillName(workspace_names_?base::UTF8ToUTF16(workspace.name):std::u16string());
    SetActionOn(button,workspace.id == state_.workspace_id);
    button->set_context_menu_controller(this);
    workspace_buttons_[workspace.id] = button;
  }
  ScheduleMetalSidebar();
}

namespace {
constexpr int kSetProfile = 1;
constexpr int kNewProfile = 2;
constexpr int kEditProfile = 3;
constexpr int kWorkspaceSettings = 4;
constexpr int kOpenSplit = 5;
constexpr int kCloseSplit = 6;
constexpr int kMoveLeft = 7;
constexpr int kMoveRight = 8;
constexpr int kToggleVertical = 9;
constexpr int kCloseTab = 10;
constexpr int kSeparateSplit = 11;
constexpr int kCloseSplitTabs = 12;
constexpr int kLinkPeek = 20;
constexpr int kLinkNewTab = 21;
constexpr int kLinkSplit = 22;
constexpr int kRuleHere = 23;
constexpr int kRemoveRule = 24;
constexpr int kRouteSubmenu = 25;
constexpr int kRouteBase = 1000;
constexpr int kNewFolder = 36;
constexpr int kRenameFolder = 37;
constexpr int kDeleteFolder = 38;
constexpr int kRemoveWorkspace = 30;
constexpr int kColorMenu = 31;
constexpr int kEmojiMenu = 32;
constexpr int kCustomColor = 33;
constexpr int kCustomEmoji = 34;
constexpr int kClearEmoji = 35;
constexpr int kColorBase = 40;
constexpr int kEmojiBase = 50;
constexpr int kProfileCommandBase = 100;
// Rust plans use the established application command IDs; fail compilation if
// the native IDs change without updating the Rust UI contract.
static_assert(kOpenSplit==5&&kCloseSplit==6&&kMoveLeft==7&&kMoveRight==8&&kToggleVertical==9&&kCloseTab==10&&kSeparateSplit==11&&kCloseSplitTabs==12&&kLinkPeek==20&&kRuleHere==23&&kRemoveRule==24&&kNewFolder==36);
// Snapshot for the Rust workspace menu, captured when the menu is built/queried.
sidebar::WorkspaceMenuState WorkspaceMenuStateFor(const ShellState& state,const std::string& workspace,const std::vector<ProfileInfo>& profiles,SkColor color,const std::u16string& emoji) {
  sidebar::WorkspaceMenuState out{};out.ready=state.configuration_ready;out.busy=state.profiles_busy;out.workspace_count=state.workspaces.size();out.color=color;out.emoji=base::UTF16ToUTF8(emoji);
  for(const auto& item:state.workspaces)if(item.id==workspace){out.workspace=workspace;out.profile=item.profile_id;}
  for(const auto& profile:profiles)out.profiles.push_back({profile.id,profile.name});
  return out;
}
// Snapshot for the Rust link/route menu: labels, enabled/checked and routing.
sidebar::LinkMenuState LinkMenuStateFor(const ShellState& state,const GURL& url,const std::string& source,const std::vector<Workspace>& routes) {
  sidebar::LinkMenuState out{};out.ready=state.configuration_ready;out.host=std::string(url.host());out.source=source;
  for(const auto& rule:state.link_rules)if(rule.host==url.host()){out.has_rule=true;out.rule_workspace=rule.workspace_id;break;}
  for(const auto& workspace:routes){std::u16string profile;for(const auto& candidate:state.profiles)if(candidate.id==workspace.profile_id)profile=base::UTF8ToUTF16(candidate.name);
    out.routes.push_back({workspace.id,base::UTF16ToUTF8(base::UTF8ToUTF16(workspace.name)+u" · "+profile)});}
  return out;
}
sidebar::TabMenuState RowMenuState(const ShellState& state,std::optional<size_t> tab,bool valid,bool http) {
  sidebar::TabMenuState out{};out.ready=state.configuration_ready;out.busy=state.profiles_busy;out.valid=valid;out.tab=tab.value_or(0);out.has_tab=tab.has_value();
  out.split=state.split;for(size_t member:state.split_tabs)out.members.push_back(member);
  out.vertical=state.split_vertical;out.tab_count=state.tabs.size();out.http=http;return out;
}
}

bool WorkspaceShell::peek_toolbar_metal_visible_for_testing() const{
  if(auto* toolbar=views::AsViewClass<ScenePeekToolbar>(peek_toolbar_))return toolbar->metal_visible();
  return false;
}
bool WorkspaceShell::peek_toolbar_contains_text_for_testing(std::string_view text) const{
  if(auto* toolbar=views::AsViewClass<ScenePeekToolbar>(peek_toolbar_))return toolbar->contains_text(text);
  return false;
}
gfx::Rect WorkspaceShell::peek_toolbar_scene_bounds_for_testing() const{
  if(auto* toolbar=views::AsViewClass<ScenePeekToolbar>(peek_toolbar_))return toolbar->scene_bounds();
  return {};
}
void WorkspaceShell::SetPeekTitle(std::u16string title) {
  auto* toolbar=views::AsViewClass<ScenePeekToolbar>(peek_toolbar_);toolbar->SetTitle(std::move(title));
}
void WorkspaceShell::ShowPeek(std::u16string title) {
  CloseOverlays();
  SetPeekTitle(std::move(title));
  PositionPanels();
  peek_widget_->Show(); peek_widget_->Activate();
}
void WorkspaceShell::HidePeek() { if (peek_widget_) peek_widget_->Hide(); }
void WorkspaceShell::BuildLinkMenu(GURL url, std::string source_workspace) {
  link_menu_runner_.reset(); link_menu_.reset(); route_menu_.reset();
  context_url_ = std::move(url); link_source_workspace_ = std::move(source_workspace);
  route_workspaces_ = state_.workspaces;
  route_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  if (metal_sidebar_) {
    link_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (const auto& entry : sidebar::link_context_menu(LinkMenuStateFor(state_, context_url_, link_source_workspace_, route_workspaces_))) {
      const auto label = base::UTF8ToUTF16(std::string(entry.label));
      switch (entry.kind) {
        case 1: link_menu_->AddSeparator(ui::NORMAL_SEPARATOR); break;
        case 2: link_menu_->AddSubMenu(entry.command, label, route_menu_.get()); break;
        case 3: route_menu_->AddRadioItem(entry.command, label, 2); break;
        default: link_menu_->AddItem(entry.command, label);
      }
    }
    return;
  }
  for (size_t i = 0; i < route_workspaces_.size(); ++i) {
    std::u16string profile;
    for (const auto& candidate : state_.profiles)
      if (candidate.id == route_workspaces_[i].profile_id) profile = base::UTF8ToUTF16(candidate.name);
    route_menu_->AddRadioItem(kRouteBase + i,
        base::UTF8ToUTF16(route_workspaces_[i].name) + u" · " + profile, 2);
  }
  link_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  link_menu_->AddItem(kLinkPeek, T(u"Otwórz w Peek"));
  link_menu_->AddItem(kLinkNewTab, T(u"Otwórz w nowej karcie"));
  link_menu_->AddItem(kLinkSplit, T(u"Otwórz obok w Split View"));
  link_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  link_menu_->AddItem(kRuleHere, T(u"Zawsze otwieraj tę domenę tutaj"));
  link_menu_->AddSubMenu(kRouteSubmenu, TF(u"Otwieraj {} w…", {base::UTF8ToUTF16(context_url_.host())}), route_menu_.get());
  link_menu_->AddItem(kRemoveRule, T(u"Usuń regułę dla tej domeny"));
}
void WorkspaceShell::ShowLinkMenu(GURL url, std::string source_workspace, gfx::Point point) {
  BuildLinkMenu(std::move(url), std::move(source_workspace));
  link_menu_runner_ = std::make_unique<views::MenuRunner>(link_menu_.get(), views::MenuRunner::CONTEXT_MENU);
  link_menu_runner_->RunMenuAt(widget_, nullptr, gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void WorkspaceShell::BuildWorkspaceMenu(std::string id) {
  workspace_menu_runner_.reset();
  workspace_menu_.reset();
  profile_menu_.reset();
  context_tab_.reset();context_tab_identity_={};
  context_pin_.clear();
  context_workspace_id_ = std::move(id);
  menu_profiles_ = state_.profiles;
  profile_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  if (metal_sidebar_) {
    color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    emoji_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    const auto entries = sidebar::workspace_context_menu(RustWorkspaceMenuState());
    // Fill submenus first; their headers in the main menu reference them.
    for (uint32_t pass : {1u, 0u}) for (const auto& entry : entries) {
      if ((entry.group == 0) != (pass == 0)) continue;
      auto* model = entry.group == 1 ? profile_menu_.get() : entry.group == 2 ? color_menu_.get() : entry.group == 3 ? emoji_menu_.get() : workspace_menu_.get();
      const auto label = base::UTF8ToUTF16(std::string(entry.label));
      switch (entry.kind) {
        case 1: model->AddSeparator(ui::NORMAL_SEPARATOR); break;
        case 2: model->AddSubMenu(entry.command, label, entry.command == kSetProfile ? profile_menu_.get() : entry.command == kColorMenu ? color_menu_.get() : emoji_menu_.get()); break;
        case 3: model->AddRadioItem(entry.command, label, 1); break;
        case 4: model->AddCheckItem(entry.command, label); break;
        default: model->AddItem(entry.command, label);
      }
    }
    return;
  }
  for (size_t i = 0; i < menu_profiles_.size(); ++i)
    profile_menu_->AddRadioItem(kProfileCommandBase + i, base::UTF8ToUTF16(menu_profiles_[i].name), 1);
  profile_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  profile_menu_->AddItem(kEditProfile, T(u"Edytuj profile…"));
  profile_menu_->AddItem(kNewProfile, T(u"Dodaj nowy profil…"));
  workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  workspace_menu_->AddSubMenu(kSetProfile, T(u"Profil przeglądania"), profile_menu_.get());
  color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::array<std::u16string, kWorkspacePresetCount> color_names = {T(u"Fiolet"), T(u"Koral"), T(u"Niebo"), T(u"Bursztyn"), T(u"Róż"), T(u"Mięta"), T(u"Zieleń"), T(u"Grafit")};
  for (size_t i = 0; i < kWorkspacePresetCount; ++i)
    color_menu_->AddCheckItem(kColorBase + i, color_names[i]);
  color_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  color_menu_->AddItem(kCustomColor, T(u"Własny kolor…"));
  emoji_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < kWorkspaceEmojis.size(); ++i)
    emoji_menu_->AddCheckItem(kEmojiBase + i, kWorkspaceEmojis[i]);
  emoji_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  emoji_menu_->AddItem(kCustomEmoji, T(u"Własna ikonka…"));
  emoji_menu_->AddCheckItem(kClearEmoji, T(u"Bez ikonki (kropka)"));
  workspace_menu_->AddSubMenu(kColorMenu, T(u"Kolor przestrzeni"), color_menu_.get());
  workspace_menu_->AddSubMenu(kEmojiMenu, T(u"Ikonka przestrzeni"), emoji_menu_.get());
  workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  workspace_menu_->AddItem(kWorkspaceSettings, T(u"Nazwa i ustawienia…"));
  workspace_menu_->AddItem(kRemoveWorkspace, T(u"Usuń przestrzeń"));
}

void WorkspaceShell::BuildFolderMenu(std::string parent) {
  context_tab_.reset();context_tab_identity_={};context_pin_.clear();
  workspace_menu_runner_.reset();
  workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  folder_parent_id_ = std::move(parent);
  context_workspace_id_ = state_.workspace_id;
  if (metal_sidebar_) {
    for (const auto& entry : sidebar::folder_context_menu(RustFolderMenuState())) {
      if (entry.command == 0) workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      else workspace_menu_->AddItem(entry.command, base::UTF8ToUTF16(std::string(entry.label)));
    }
    return;
  }
  workspace_menu_->AddItem(kNewFolder, folder_parent_id_.empty() ? T(u"Nowy folder…") : T(u"Nowy podfolder…"));
  if (!folder_parent_id_.empty()) {
    workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    workspace_menu_->AddItem(kRenameFolder, T(u"Zmień nazwę…"));
    workspace_menu_->AddItem(kDeleteFolder, T(u"Usuń folder"));
  }
}

views::View* WorkspaceShell::folder_form_for_testing() const {
  return folder_dialog_?FindFolderForm(folder_dialog_->GetContentsView()):nullptr;
}
EditorControl* WorkspaceShell::folder_editor_for_testing() const {
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form?form->field():nullptr;
}
bool WorkspaceShell::folder_form_metal_visible() const {
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form&&form->metal_visible();
}
bool WorkspaceShell::folder_form_contains_scene_text(std::string_view text) const {
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form&&form->contains_scene_text(text);
}
std::u16string WorkspaceShell::folder_form_error_for_testing() const {
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form?form->error_text():std::u16string();
}
void WorkspaceShell::RequestNewFolder(std::string workspace, std::string parent) {
  RequestFolderName(std::move(workspace), std::move(parent), {});
}
void WorkspaceShell::RequestRenameFolder(std::string workspace, std::string folder) {
  RequestFolderName(std::move(workspace), {}, std::move(folder));
}
void WorkspaceShell::RequestFolderName(std::string workspace, std::string parent, std::string renamed) {
  if (folder_dialog_) { folder_dialog_->Activate(); return; }
  if (!state_.configuration_ready || state_.profiles_busy || !callbacks_.create_folder) return;
  std::u16string initial_name = T(u"Nowy folder");
  if (!renamed.empty()) {
    auto item = std::find_if(state_.pins.begin(), state_.pins.end(), [&](const auto& item) { return item.id == renamed && item.folder; });
    if (state_.workspace_id != workspace || item == state_.pins.end()) return;
    initial_name = base::UTF8ToUTF16(item->title);
  }
  if(metal_sidebar_) {
    auto contents=std::make_unique<FolderFormView>(workspace,parent,renamed,initial_name,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,FolderFormView* form){
      if(!shell)return true;const auto alive=form->weak();auto request=form->Prepare(shell->state_.workspace_id,shell->state_.configuration_ready,shell->state_.profiles_busy,shell->state_.pins);
      if(request.action==sidebar::FolderAction::None)return false;
      const auto target=std::string(request.workspace),parent=std::string(request.parent),folder=std::string(request.folder),name=std::string(request.name);
      const bool collapsed=request.expand_parent&&shell->sidebar_tree_.Collapsed(target,parent);
      if(request.expand_parent)shell->sidebar_tree_.SetCollapsed(target,parent,false);
      const bool ok=request.action==sidebar::FolderAction::Create?shell->callbacks_.create_folder.Run(target,parent,name):shell->callbacks_.rename_folder.Run(target,folder,name);
      if(!shell||!alive)return true;
      if(!ok&&request.expand_parent)shell->sidebar_tree_.SetCollapsed(target,parent,collapsed);
      form->Finish(ok);if(ok)shell->folder_dialog_.reset();return ok;
    },weak_factory_.GetWeakPtr()));
    const auto title=contents->title(),accept=contents->accept_label(),cancel=contents->cancel_label();const auto weak=contents->weak();auto* input=contents->field();
    auto dialog=std::make_unique<SceneModalDialog>(std::move(contents),true);dialog->SetTitle(title);dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,accept);dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,cancel);dialog->SetInitiallyFocusedView(input->view());
    dialog->SetAcceptCallbackWithClose(base::BindRepeating([](base::WeakPtr<FolderFormView> form){return !form||form->Accept();},weak));dialog->SetCancelCallback(base::BindOnce([](base::WeakPtr<FolderFormView> form){if(form)form->Cancel();},weak));
    rust_folder_dialog_delegate_=std::move(dialog);auto* prompt=views::DialogDelegate::CreateDialogWidget(rust_folder_dialog_delegate_.get(),widget_->GetNativeWindow(),widget_->GetNativeView());rust_folder_dialog_.reset(prompt);
    prompt->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,views::Widget::ClosedReason){if(shell){shell->folder_dialog_.reset();shell->rust_folder_dialog_.reset();shell->rust_folder_dialog_delegate_.reset();}},weak_factory_.GetWeakPtr()));
    prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
    InstallWorkspaceKeyboardRouting(prompt,true);
#endif
    PreparePanelText(prompt->GetRootView());folder_dialog_=prompt->GetWeakPtr();prompt->Show();input->SelectRange(gfx::Range(0,input->GetText().size()));return;
  }
  ui::DialogModel::Builder builder;
  auto* model = builder.model();
  auto weak = weak_factory_.GetWeakPtr();
  auto dialog_model = builder.SetTitle(!renamed.empty() ? T(u"Zmień nazwę folderu") : parent.empty() ? T(u"Nowy folder") : T(u"Nowy podfolder"))
      .AddTextfield(kFolderNameField, T(u"Nazwa folderu"), initial_name)
      .SetInitiallyFocusedField(kFolderNameField)
      .AddOkButton(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, ui::DialogModel* model,
                                        std::string workspace, std::string parent, std::string renamed) {
        if (!shell) return true;
        const auto name = std::u16string(base::TrimWhitespace(model->GetTextfieldByUniqueId(kFolderNameField)->text(), base::TRIM_ALL));
        if (name.empty()) return false;
        const bool was_collapsed = !parent.empty() && shell->sidebar_tree_.Collapsed(workspace, parent);
        if (!parent.empty()) shell->sidebar_tree_.SetCollapsed(workspace, parent, false);
        const bool ok = renamed.empty() ? shell->callbacks_.create_folder.Run(workspace, parent, base::UTF16ToUTF8(name))
            : shell->callbacks_.rename_folder.Run(workspace, renamed, base::UTF16ToUTF8(name));
        if (!ok && !parent.empty()) shell->sidebar_tree_.SetCollapsed(workspace, parent, was_collapsed);
        if (ok) shell->folder_dialog_.reset();
        return ok;
      }, weak, base::Unretained(model), workspace, parent, renamed), ui::DialogModel::Button::Params().SetLabel(renamed.empty() ? T(u"Dodaj folder") : T(u"Zapisz nazwę")))
      .AddCancelButton(base::DoNothing(), ui::DialogModel::Button::Params().SetLabel(T(u"Anuluj")))
      .Build();
  auto* prompt = views::DialogDelegate::CreateDialogWidget(
      views::BubbleDialogModelHost::CreateModal(std::move(dialog_model), ui::mojom::ModalType::kWindow),
      widget_->GetNativeWindow(), widget_->GetNativeView());
  prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(prompt,true);
#endif
  PreparePanelText(prompt->GetRootView());
  folder_dialog_ = prompt->GetWeakPtr();
  prompt->Show();
}

bool WorkspaceShell::ShowRustFolderNotice(std::string workspace,std::string folder,std::string title,bool error_only) {
  if(!metal_sidebar_)return false;
  auto content=std::make_unique<FolderNoticeView>(workspace,folder,title,error_only,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,FolderNoticeView* notice){
    if(!shell)return true;const auto alive=notice->weak();auto request=notice->Prepare(shell->state_.workspace_id,shell->state_.configuration_ready,shell->state_.profiles_busy,shell->state_.pins);
    if(request.action==sidebar::FolderNoticeAction::None)return false;
    const bool ok=request.action==sidebar::FolderNoticeAction::Acknowledge||shell->callbacks_.delete_folder.Run(std::string(request.workspace),std::string(request.folder),true);
    if(!shell||!alive)return true;notice->Finish(ok);if(ok)shell->folder_dialog_.reset();return ok;
  },weak_factory_.GetWeakPtr()));
  const auto copy=content->copy();const auto weak=content->weak();
  auto dialog=std::make_unique<SceneModalDialog>(std::move(content),false);dialog->SetTitle(base::UTF8ToUTF16(std::string(copy.title)));dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|(!copy.cancel.empty()?static_cast<int>(ui::mojom::DialogButton::kCancel):0));dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,base::UTF8ToUTF16(std::string(copy.accept)));dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,base::UTF8ToUTF16(std::string(copy.cancel)));
  dialog->SetAcceptCallbackWithClose(base::BindRepeating([](base::WeakPtr<FolderNoticeView> notice){return !notice||notice->Accept();},weak));if(!copy.cancel.empty())dialog->SetCancelCallback(base::BindOnce([](base::WeakPtr<FolderNoticeView> notice){if(notice)notice->Cancel();},weak));if(copy.default_cancel)dialog->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));
  rust_folder_dialog_delegate_=std::move(dialog);auto* prompt=views::DialogDelegate::CreateDialogWidget(rust_folder_dialog_delegate_.get(),widget_->GetNativeWindow(),widget_->GetNativeView());rust_folder_dialog_.reset(prompt);
  prompt->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,views::Widget::ClosedReason){if(shell){shell->folder_dialog_.reset();shell->rust_folder_dialog_.reset();shell->rust_folder_dialog_delegate_.reset();}},weak_factory_.GetWeakPtr()));
  prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(prompt,true);
#endif
  PreparePanelText(prompt->GetRootView());folder_dialog_=prompt->GetWeakPtr();prompt->Show();return true;
}
views::View* WorkspaceShell::folder_notice_for_testing() const {
  return folder_dialog_?FindFolderNotice(folder_dialog_->GetContentsView()):nullptr;
}
bool WorkspaceShell::folder_notice_metal_visible() const {
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice&&notice->metal_visible();
}
bool WorkspaceShell::folder_notice_contains_scene_text(std::string_view text) const {
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice&&notice->contains_scene_text(text);
}
std::u16string WorkspaceShell::folder_notice_error_for_testing() const {
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice?notice->error_text():std::u16string();
}
size_t WorkspaceShell::folder_notice_line_count_for_testing() const {
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice?notice->body_line_count():0;
}
// Removing a folder can be undone, so it happens at once with a toast (design:
// Dialog is only for what cannot be undone).
void WorkspaceShell::RequestDeleteFolder(std::string workspace, std::string folder) {
  auto item = std::find_if(state_.pins.begin(), state_.pins.end(), [&](const auto& item) { return item.id == folder && item.folder; });
  if (state_.workspace_id != workspace || item == state_.pins.end() || state_.profiles_busy || !state_.configuration_ready) return;
  const auto title = base::UTF8ToUTF16(item->title);
  auto previous = state_.pins;
  if (!callbacks_.delete_folder.Run(workspace, folder, true)) return;
  ShowToast(T(u"Usunięto folder "), title, base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, std::string workspace, std::vector<PinnedItem> pins) {
    if (shell) shell->callbacks_.restore_pins.Run(std::move(workspace), std::move(pins));
  }, weak_factory_.GetWeakPtr(), workspace, std::move(previous)));
}

void WorkspaceShell::ShowFolderSaveError() {
  if(!widget_)return;
  if(ShowRustFolderNotice({},{},{},true))return;
  auto model = ui::DialogModel::Builder().SetTitle(T(u"Nie udało się zapisać folderu"))
      .AddParagraph(ui::DialogModelLabel(T(u"Sprawdź wolne miejsce na dysku i spróbuj ponownie.")))
      .AddOkButton(base::DoNothing(), ui::DialogModel::Button::Params().SetLabel(u"OK")).Build();
  auto* prompt = views::DialogDelegate::CreateDialogWidget(
      views::BubbleDialogModelHost::CreateModal(std::move(model), ui::mojom::ModalType::kWindow),
      widget_->GetNativeWindow(), widget_->GetNativeView());
  prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(prompt,true);
#endif
  PreparePanelText(prompt->GetRootView());
  folder_dialog_ = prompt->GetWeakPtr();
  prompt->Show();
}

void WorkspaceShell::ShowContextMenuForViewImpl(views::View* source, const gfx::Point& point,
                                               ui::mojom::MenuSourceType source_type) {
  context_tab_.reset(); context_pin_.clear();context_tab_identity_={};
  if (source == url_pill_) {
    if (state_.url.SchemeIsHTTPOrHTTPS()) ShowLinkMenu(state_.url, state_.workspace_id, point);
    return;
  }
  if (auto cache = tab_lists_.find(state_.workspace_id); cache != tab_lists_.end()) {
    for (const auto& [index, button] : cache->second.tab_buttons) if (button == source) context_tab_ = index;
    for (const auto& [id, button] : cache->second.pin_buttons) if (button == source) context_pin_ = id;
  }
  if (!context_pin_.empty()) for (const auto& pin : state_.pins) {
    if (pin.id == context_pin_ && pin.folder) {
      BuildFolderMenu(pin.id);
      workspace_menu_runner_ = std::make_unique<views::MenuRunner>(workspace_menu_.get(), views::MenuRunner::CONTEXT_MENU);
      workspace_menu_runner_->RunMenuAt(widget_, nullptr, gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft, source_type);
      return;
    }
  }
  if (context_tab_ || !context_pin_.empty()) {
    ShowTabMenu(point, source_type);
    return;
  }
  for (const auto& [id, button] : workspace_buttons_) {
    if (button != source) continue;
    BuildWorkspaceMenu(id);
    workspace_menu_runner_ = std::make_unique<views::MenuRunner>(workspace_menu_.get(), views::MenuRunner::CONTEXT_MENU);
    workspace_menu_runner_->RunMenuAt(widget_, nullptr, gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft, source_type);
    return;
  }
  if (source == sidebar_root_ || source == tab_list_ || sidebar_root_->Contains(source)) {
    BuildFolderMenu();
    workspace_menu_runner_ = std::make_unique<views::MenuRunner>(workspace_menu_.get(), views::MenuRunner::CONTEXT_MENU);
    workspace_menu_runner_->RunMenuAt(widget_, nullptr, gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft, source_type);
  }
}

void WorkspaceShell::ShowSplitPaneMenu(size_t pane, gfx::Point point) {
  if (!state_.split_visible || pane >= state_.split_tabs.size()) return;
  context_tab_ = state_.split_tabs[pane]; context_pin_.clear();
  ShowTabMenu(point, ui::mojom::MenuSourceType::kMouse);
}

void WorkspaceShell::CloseSplitPane(size_t pane) {
  if (!state_.split_visible || pane >= state_.split_tabs.size()) return;
  std::array<uint64_t,2> identity{};
  for(const auto& tab:state_.tabs)if(tab.index==state_.split_tabs[pane]){identity=tab.identity;break;}
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell,std::string workspace,std::array<uint64_t,2> identity){
        if(!shell||shell->state_.workspace_id!=workspace||identity==std::array<uint64_t,2>{})return;
        for(const auto& tab:shell->state_.tabs)if(tab.identity==identity){shell->callbacks_.close_tab.Run(tab.index);return;}
      },weak_factory_.GetWeakPtr(),state_.workspace_id,identity));
}

views::View* WorkspaceShell::split_pane_button(size_t pane, bool close) const {
  auto* bar = static_cast<PaneHost*>(content_host_.get())->bar(pane);
  if (!bar) return nullptr;
  return close ? bar->close() : bar->menu();
}

void WorkspaceShell::BuildTabMenu(std::optional<size_t> target_tab,std::string target_pin) {
  context_tab_=target_tab;context_pin_=std::move(target_pin);context_tab_identity_={};
  if(target_tab)for(const auto& row:state_.tabs)if(row.index==*target_tab){context_tab_identity_=row.identity;break;}
    workspace_menu_runner_.reset();
    workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
    GURL url;
    if (context_tab_) for (const auto& tab : state_.tabs) if (tab.index == *context_tab_) url = tab.url;
    if (!context_pin_.empty()) for (const auto& pin : state_.pins) if (pin.id == context_pin_) url = pin.url;
    context_url_ = url; link_source_workspace_ = state_.workspace_id;
    folder_parent_id_.clear();context_workspace_id_=state_.workspace_id;
    if(metal_sidebar_) {
      const auto target=ResolveSidebarTarget(context_workspace_id_,context_tab_identity_,context_pin_);
      const auto plan=sidebar::tab_context_menu(RowMenuState(state_,target.tab,target.valid,url.SchemeIsHTTPOrHTTPS()));
      for(const auto& entry:plan){if(entry.command==0)workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);else workspace_menu_->AddItem(entry.command,base::UTF8ToUTF16(std::string(entry.label)));}
      return;
    }
    workspace_menu_->AddItem(kNewFolder, T(u"Nowy folder…"));
    folder_parent_id_.clear();
    context_workspace_id_ = state_.workspace_id;
    workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    workspace_menu_->AddItem(kLinkPeek, T(u"Otwórz w Peek"));
    const bool paired = context_tab_ && state_.split &&
        std::find(state_.split_tabs.begin(), state_.split_tabs.end(), *context_tab_) != state_.split_tabs.end();
    if (paired) {
      const bool vertical = state_.split_vertical;
      workspace_menu_->AddItem(kMoveLeft, vertical ? T(u"Przesuń w górę") : T(u"Przesuń w lewo"));
      workspace_menu_->AddItem(kMoveRight, vertical ? T(u"Przesuń w dół") : T(u"Przesuń w prawo"));
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kToggleVertical, vertical ? T(u"Podział poziomy (obok siebie)")
                                                         : T(u"Podział pionowy (jedna nad drugą)"));
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kSeparateSplit, T(u"Rozdziel"));
      if (state_.split_tabs.size() > 2) workspace_menu_->AddItem(kCloseSplit, T(u"Wyjmij kartę ze splitu"));
      workspace_menu_->AddItem(kCloseTab, T(u"Zamknij kartę"));
      workspace_menu_->AddItem(kCloseSplitTabs, T(u"Zamknij wszystkie karty w splicie"));
    } else {
      workspace_menu_->AddItem(kOpenSplit, T(u"Otwórz obok w Split View"));
    }
    if (url.SchemeIsHTTPOrHTTPS()) {
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kRuleHere, T(u"Zawsze otwieraj tę domenę tutaj"));
      workspace_menu_->AddItem(kRemoveRule, T(u"Usuń regułę dla tej domeny"));
    }
}
void WorkspaceShell::ShowTabMenu(gfx::Point point,ui::mojom::MenuSourceType source_type) {
  BuildTabMenu(context_tab_,context_pin_);
  workspace_menu_runner_=std::make_unique<views::MenuRunner>(workspace_menu_.get(),views::MenuRunner::CONTEXT_MENU);
  workspace_menu_runner_->RunMenuAt(widget_,nullptr,gfx::Rect(point,gfx::Size()),views::MenuAnchorPosition::kTopLeft,source_type);
}

sidebar::FolderMenuState WorkspaceShell::RustFolderMenuState() const {
  sidebar::FolderMenuState out{};out.ready=state_.configuration_ready;out.busy=state_.profiles_busy;out.folder=folder_parent_id_;
  for(const auto& item:state_.workspaces)if(item.id==context_workspace_id_)out.workspace=context_workspace_id_;
  return out;
}
sidebar::WorkspaceMenuState WorkspaceShell::RustWorkspaceMenuState() const {
  return WorkspaceMenuStateFor(state_, context_workspace_id_, menu_profiles_, workspace_color(context_workspace_id_), workspace_emoji(context_workspace_id_));
}
bool WorkspaceShell::IsCommandIdChecked(int command_id) const {
  if (metal_sidebar_ && sidebar::workspace_menu_command(command_id, menu_profiles_.size()))
    return sidebar::workspace_menu_checked(command_id, RustWorkspaceMenuState());
  if (command_id >= kColorBase && command_id < kColorBase + static_cast<int>(kWorkspacePresetCount))
    return sidebar::design_same_workspace_color(workspace_color(context_workspace_id_), WorkspacePreset(command_id - kColorBase));
  if (command_id >= kEmojiBase && command_id < kEmojiBase + static_cast<int>(kWorkspaceEmojis.size()))
    return workspace_emoji(context_workspace_id_) == kWorkspaceEmojis[command_id - kEmojiBase];
  if (command_id == kClearEmoji) return workspace_emoji(context_workspace_id_).empty();
  if (metal_sidebar_ && command_id >= kRouteBase && command_id < kRouteBase + static_cast<int>(route_workspaces_.size()))
    return sidebar::link_menu_checked(command_id, LinkMenuStateFor(state_, context_url_, link_source_workspace_, route_workspaces_));
  if (command_id >= kRouteBase && command_id < kRouteBase + static_cast<int>(route_workspaces_.size()))
    for (const auto& rule : state_.link_rules) if (rule.host == context_url_.host())
      return rule.workspace_id == route_workspaces_[command_id - kRouteBase].id;
  if (command_id < kProfileCommandBase || command_id >= kProfileCommandBase + static_cast<int>(menu_profiles_.size())) return false;
  for (const auto& workspace : state_.workspaces)
    if (workspace.id == context_workspace_id_) return workspace.profile_id == menu_profiles_[command_id - kProfileCommandBase].id;
  return false;
}
bool WorkspaceShell::IsCommandIdEnabled(int command_id) const {
  if(metal_sidebar_&&command_id>=kOpenSplit&&command_id<=kCloseSplitTabs)return RustTabCommandEnabled(command_id);
  if(metal_sidebar_&&sidebar::workspace_menu_command(command_id,menu_profiles_.size()))return sidebar::workspace_menu_enabled(command_id,RustWorkspaceMenuState());
  if(metal_sidebar_&&command_id>=kNewFolder&&command_id<=kDeleteFolder)return sidebar::folder_menu_enabled(command_id,RustFolderMenuState());
  if(metal_sidebar_&&((command_id>=kLinkPeek&&command_id<=kRouteSubmenu)||(command_id>=kRouteBase&&command_id<kRouteBase+static_cast<int>(route_workspaces_.size()))))
    return sidebar::link_menu_enabled(command_id,LinkMenuStateFor(state_,context_url_,link_source_workspace_,route_workspaces_));
  if (command_id == kNewFolder || command_id == kRenameFolder || command_id == kDeleteFolder) return state_.configuration_ready && !state_.profiles_busy;
  if (command_id == kRemoveWorkspace) return state_.configuration_ready && !state_.profiles_busy && state_.workspaces.size() > 1;
  // Left/top half can only move right/down, and vice versa.
  if (command_id == kMoveLeft || command_id == kMoveRight) {
    if (!state_.configuration_ready || !context_tab_) return false;
    const auto found = std::find(state_.split_tabs.begin(), state_.split_tabs.end(), *context_tab_);
    if (found == state_.split_tabs.end()) return false;
    return command_id == kMoveLeft ? found != state_.split_tabs.begin() : found + 1 != state_.split_tabs.end();
  }
  if (command_id == kCloseTab) return state_.tabs.size() > 1;
  if (command_id == kRemoveRule) return state_.configuration_ready &&
      std::any_of(state_.link_rules.begin(), state_.link_rules.end(), [&](const auto& rule) { return rule.host == context_url_.host(); });
  return state_.configuration_ready;
}
WorkspaceShell::ResolvedRowTarget WorkspaceShell::ResolveSidebarTarget(std::string_view workspace,std::array<uint64_t,2> identity,std::string_view pin) const {
  std::vector<sidebar::MenuTab> tabs;tabs.reserve(state_.tabs.size());for(const auto& tab:state_.tabs)tabs.push_back({tab.index,tab.identity[0],tab.identity[1]});
  std::vector<sidebar::TreeNode> pins;pins.reserve(state_.pins.size());for(const auto& row:state_.pins)pins.push_back({row.id,row.parent_id,row.folder});
  const auto target=sidebar::resolve_row_target(rust::Str(workspace.data(),workspace.size()),state_.workspace_id,identity[0],identity[1],rust::Str(pin.data(),pin.size()),rust::Slice<const sidebar::MenuTab>(tabs.data(),tabs.size()),rust::Slice<const sidebar::TreeNode>(pins.data(),pins.size()));
  return {target.valid,target.has_tab?std::optional<size_t>(target.tab):std::nullopt,std::string(target.pin)};
}
std::optional<WorkspaceShell::SidebarReorder> WorkspaceShell::SidebarReorderAt(gfx::Point point) const {
  if(!sidebar_visible()||!sidebar_root_||!sidebar_root_->GetBoundsInScreen().Contains(point))return std::nullopt;
  const auto found=tab_lists_.find(state_.workspace_id);
  if(found==tab_lists_.end()||!found->second.node_graph)return std::nullopt;
  const auto& cache=found->second;
  // Rows in visual order with what a drop before or after each one means.
  struct Meta {bool pinned;std::string pin,parent;bool folder,expanded;size_t before_tab,after_tab;gfx::Rect bounds;};
  std::vector<Meta> metas;std::vector<sidebar::ReorderRow> rows;
  auto add=[&](views::View* view,Meta meta){
    if(!view||!view->IsDrawn())return;meta.bounds=view->GetBoundsInScreen();
    rows.push_back({{meta.bounds.x(),meta.bounds.y(),meta.bounds.width(),meta.bounds.height()},meta.pinned,meta.folder});metas.push_back(std::move(meta));
  };
  const SidebarTabSlot* pinned_pair=nullptr;
  for(const auto& slot:cache.tab_plan.slots)if(slot.pair&&slot.pin!=SidebarTree::kRoot)pinned_pair=&slot;
  for(const auto& entry:cache.node_graph->graph->pin_tree().entries){
    if(!entry.visible||entry.index>=state_.pins.size())continue;
    const auto& pin=state_.pins[entry.index];
    const auto button=cache.pin_buttons.find(pin.id);
    views::View* view=button==cache.pin_buttons.end()?nullptr:button->second.get();
    if(pinned_pair&&pinned_pair->pin==entry.index)view=cache.pin_pair;
    add(view,{true,pin.id,pin.parent_id,pin.folder,pin.folder&&!entry.collapsed,0,0,{}});
  }
  for(const auto& slot:cache.tab_plan.slots){
    if(slot.pair&&slot.pin!=SidebarTree::kRoot)continue;
    size_t first=cache.tabs[slot.first].index,last=first;
    for(size_t member:slot.members){first=std::min(first,cache.tabs[member].index);last=std::max(last,cache.tabs[member].index);}
    views::View* view=nullptr;
    if(slot.pair)view=cache.root_pair;
    else if(const auto button=cache.tab_buttons.find(cache.tabs[slot.first].index);button!=cache.tab_buttons.end())view=button->second.get();
    add(view,{false,{},{},false,false,first,last+1,{}});
  }
  if(rows.empty())return std::nullopt;
  const auto target=sidebar::sidebar_reorder_target(rust::Slice<const sidebar::ReorderRow>(rows.data(),rows.size()),point.y());
  if(!target.valid)return std::nullopt;
  SidebarReorder out;out.pinned=target.pinned;
  if(target.row>=metas.size()) {
    // No pins yet: the new pinned section opens above the first tab.
    const auto& first=metas.front().bounds;out.indicator=gfx::Rect(first.x(),first.y()-4,first.width(),2);
    return out;
  }
  const auto& meta=metas[target.row];const auto& r=meta.bounds;
  out.indicator=gfx::Rect(r.x(),target.after?r.bottom():r.y()-2,r.width(),2);
  if(!meta.pinned){out.before_tab=target.after?meta.after_tab:meta.before_tab;return out;}
  if(target.into||(target.after&&meta.expanded)) {
    // Into a folder: dropped on its row, or just below an open folder's header.
    out.parent=meta.pin;
    if(target.into){out.into_folder=true;out.indicator=r;}
    else for(const auto& pin:state_.pins)if(pin.parent_id==meta.pin){out.before_pin=pin.id;break;}
    return out;
  }
  out.parent=meta.parent;
  if(!target.after){out.before_pin=meta.pin;return out;}
  bool next=false;
  for(const auto& pin:state_.pins){if(pin.parent_id!=meta.parent)continue;if(next){out.before_pin=pin.id;break;}next=pin.id==meta.pin;}
  return out;
}
namespace {
class SidebarDragOverlay final : public views::View {
  METADATA_HEADER(SidebarDragOverlay, views::View)
 public:
  explicit SidebarDragOverlay(base::RepeatingCallback<void(gfx::Canvas*,views::View*)> paint):paint_(std::move(paint)) {
    GetViewAccessibility().SetIsIgnored(true);SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey,true);
    // Its own layer stacks it above the scrolling tab list, which has one too.
    SetPaintToLayer();layer()->SetFillsBoundsOpaquely(false);
  }
  void OnPaint(gfx::Canvas* canvas) override{paint_.Run(canvas,this);}
 private:
  base::RepeatingCallback<void(gfx::Canvas*,views::View*)> paint_;
};
BEGIN_METADATA(SidebarDragOverlay)
END_METADATA
}  // namespace
void WorkspaceShell::HandleSidebarDrag(views::View* row,gfx::Point grab,std::optional<gfx::Point> point) {
  sidebar_reorder_=point?SidebarReorderAt(*point):std::nullopt;
  drag_row_.SetView(point?row:nullptr);drag_grab_=grab;drag_point_=point;
  ScheduleMetalSidebar();
  if(!sidebar_root_)return;
  if(!drag_overlay_)drag_overlay_=sidebar_root_->AddChildView(std::make_unique<SidebarDragOverlay>(
      base::BindRepeating(&WorkspaceShell::PaintSidebarDrag,base::Unretained(this))));
  // Above every row, including children added since the last drag.
  sidebar_root_->ReorderChildView(drag_overlay_,sidebar_root_->children().size());
  drag_overlay_->SetBoundsRect(sidebar_root_->GetLocalBounds());
  drag_overlay_->SetVisible(point.has_value());
  drag_overlay_->SchedulePaint();
}
void WorkspaceShell::PaintSidebarDrag(gfx::Canvas* canvas,views::View* overlay) {
  auto* row=views::AsViewClass<RustRowView>(drag_row_.view());
  if(!row||!drag_point_||metal_sidebar_visible())return;
  auto local=[&](gfx::Rect screen){gfx::Point origin=screen.origin();views::View::ConvertPointFromScreen(overlay,&origin);return gfx::Rect(origin,screen.size());};
  const SkColor tint=FrameTint(workspace_color(state_.workspace_id),root_);
  // The row stays in place as a faded placeholder.
  FillRound(canvas,local(row->GetBoundsInScreen()),10,SkColorSetA(pinned_?tint:Mix(tint,Tone(root_,kCard),70),0xB3));
  if(sidebar_reorder_) {
    gfx::ScopedCanvas clipped(canvas);canvas->ClipRect(local(tab_list_->parent()->GetBoundsInScreen()));
    const SkColor accent=workspace_color(state_.workspace_id);
    if(sidebar_reorder_->into_folder)FillRound(canvas,local(sidebar_reorder_->indicator),10,SkColorSetA(accent,0x40));
    else FillRound(canvas,local(sidebar_reorder_->indicator),1,accent);
  }
  // A lifted copy of the row follows the pointer.
  gfx::Point origin=*drag_point_-drag_grab_.OffsetFromOrigin();views::View::ConvertPointFromScreen(overlay,&origin);
  const gfx::Rect ghost(origin,row->size());
  FillRound(canvas,ghost+gfx::Vector2d(1,3),10,SkColorSetA(SK_ColorBLACK,IsDark(root_)?0x59:0x1F));
  auto scene=row->SidebarScene(gfx::RectF(ghost),gfx::RectF(overlay->GetLocalBounds()));
  if(!scene.valid)return;
  auto& item=scene.item;item.fill=SkColorSetA(Tone(root_,kCard),0xF2);item.active=true;item.hovered=false;item.focused=false;item.close_visible=false;
  PaintSceneRecipe(canvas,item,Font(0,gfx::Font::Weight::MEDIUM));
}
void WorkspaceShell::HandleSidebarDrop(std::string workspace,std::optional<size_t> tab,std::array<uint64_t,2> identity,std::string pin,gfx::Point point) {
  if(workspace!=state_.workspace_id)return;
  if(auto reorder=SidebarReorderAt(point)) {
    sidebar_reorder_.reset();ScheduleMetalSidebar();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,std::string workspace,std::array<uint64_t,2> identity,std::string pin,SidebarReorder reorder){
      if(!shell||shell->state_.workspace_id!=workspace||!shell->state_.configuration_ready||shell->state_.profiles_busy)return;
      const auto target=shell->ResolveSidebarTarget(workspace,identity,pin);
      if(!target.valid)return;
      if(target.tab) {
        // Crossing into the pinned section pins the tab.
        if(reorder.pinned)shell->callbacks_.pin_tab.Run(*target.tab,reorder.parent,reorder.before_pin);
        else shell->callbacks_.move_tab.Run(*target.tab,reorder.before_tab);
      } else if(!target.pin.empty()) {
        if(!reorder.pinned)shell->callbacks_.unpin.Run(target.pin,reorder.before_tab);
        else if(reorder.before_pin!=target.pin&&reorder.parent!=target.pin)shell->callbacks_.move_pin.Run(target.pin,reorder.parent,reorder.before_pin);
      }
    },weak_factory_.GetWeakPtr(),std::move(workspace),identity,std::move(pin),std::move(*reorder)));
    return;
  }
  const auto bounds=content_host_->GetBoundsInScreen();
  if(metal_sidebar_) {
    const auto target=ResolveSidebarTarget(workspace,identity,pin);
    const bool allowed=target.valid&&state_.configuration_ready&&!state_.profiles_busy;
    if(sidebar::sidebar_drop_accepts({bounds.x(),bounds.y(),bounds.width(),bounds.height()},point.x(),point.y(),allowed))QueueSplit(target.tab,target.pin);
    return;
  }
  if(bounds.Contains(point)&&point.x()>=bounds.CenterPoint().x())QueueSplit(tab,std::move(pin));
}
bool WorkspaceShell::RustTabCommandEnabled(int command) const {
  const auto target=ResolveSidebarTarget(context_workspace_id_,context_tab_identity_,context_pin_);
  return sidebar::tab_menu_enabled(command,RowMenuState(state_,target.tab,target.valid,context_url_.SchemeIsHTTPOrHTTPS()));
}
void WorkspaceShell::QueueTabCommand(int command) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,int command,std::string workspace,std::array<uint64_t,2> identity,std::string pin){
    if(!shell)return;const auto target=shell->ResolveSidebarTarget(workspace,identity,pin);
    const auto action=sidebar::tab_menu_action(command,RowMenuState(shell->state_,target.tab,target.valid,shell->context_url_.SchemeIsHTTPOrHTTPS()));
    switch(action) {
      case sidebar::SidebarAction::OpenSplit:if(target.tab)shell->callbacks_.split_tab.Run(*target.tab);else shell->callbacks_.split_pin.Run(target.pin);break;
      case sidebar::SidebarAction::CloseSplit:shell->callbacks_.leave_split.Run(*target.tab);break;
      case sidebar::SidebarAction::Separate:shell->callbacks_.unsplit.Run(*target.tab);break;
      case sidebar::SidebarAction::CloseGroup:shell->callbacks_.close_split_tabs.Run(*target.tab);break;
      case sidebar::SidebarAction::MoveEarlier:case sidebar::SidebarAction::MoveLater:shell->callbacks_.move_split_pane.Run(*target.tab,action==sidebar::SidebarAction::MoveLater);break;
      case sidebar::SidebarAction::ToggleOrientation:shell->callbacks_.split_vertical.Run(!shell->state_.split_vertical);break;
      case sidebar::SidebarAction::CloseTab:shell->callbacks_.close_tab.Run(*target.tab);break;
      default:break;
    }
  },weak_factory_.GetWeakPtr(),command,context_workspace_id_,context_tab_identity_,context_pin_));
}
void WorkspaceShell::QueueSplit(std::optional<size_t> tab, std::string pin) {
  std::array<uint64_t,2> identity{};if(tab)for(const auto& row:state_.tabs)if(row.index==*tab){identity=row.identity;break;}
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, std::string workspace, std::optional<size_t> tab, std::string pin,std::array<uint64_t,2> identity) {
        if (!shell || shell->state_.workspace_id != workspace) return;
        if(shell->metal_sidebar_) {
          const auto target=shell->ResolveSidebarTarget(workspace,identity,pin);
          if(!target.valid||!shell->state_.configuration_ready||shell->state_.profiles_busy)return;
          if(target.tab)shell->callbacks_.split_tab.Run(*target.tab);else shell->callbacks_.split_pin.Run(target.pin);return;
        }
        if (tab) shell->callbacks_.split_tab.Run(*tab);
        else if (!pin.empty()) shell->callbacks_.split_pin.Run(std::move(pin));
      }, weak_factory_.GetWeakPtr(), state_.workspace_id, tab, std::move(pin),std::move(identity)));
}

void WorkspaceShell::ExecuteCommand(int command_id, int) {
  if(metal_sidebar_&&command_id>=kOpenSplit&&command_id<=kCloseSplitTabs){QueueTabCommand(command_id);return;}
  if(metal_sidebar_&&command_id>=kNewFolder&&command_id<=kDeleteFolder){
    // Rust revalidates readiness and captures the workspace/folder target.
    const auto action=sidebar::folder_menu_action(command_id,RustFolderMenuState());using C=sidebar::FolderMenuCommand;
    if(action.command==C::None)return;
    const auto request=action.command==C::Create?&WorkspaceShell::RequestNewFolder:action.command==C::Rename?&WorkspaceShell::RequestRenameFolder:&WorkspaceShell::RequestDeleteFolder;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(request,weak_factory_.GetWeakPtr(),std::string(action.workspace),std::string(action.folder)));
    return;
  }
  if (command_id == kRenameFolder || command_id == kDeleteFolder) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(command_id == kRenameFolder ? &WorkspaceShell::RequestRenameFolder : &WorkspaceShell::RequestDeleteFolder,
            weak_factory_.GetWeakPtr(), context_workspace_id_, folder_parent_id_));
    return;
  }
  if (command_id == kNewFolder) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(&WorkspaceShell::RequestNewFolder, weak_factory_.GetWeakPtr(), context_workspace_id_, folder_parent_id_));
    return;
  }
  if (metal_sidebar_ && ((command_id >= kLinkPeek && command_id <= kRemoveRule) ||
      (command_id >= kRouteBase && command_id < kRouteBase + static_cast<int>(route_workspaces_.size())))) {
    // Rust revalidates readiness/rule state and resolves the target workspace.
    const auto action = sidebar::link_menu_action(command_id, LinkMenuStateFor(state_, context_url_, link_source_workspace_, route_workspaces_));
    if (action.command == sidebar::LinkMenuCommand::None) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, sidebar::LinkMenuCommand command, GURL url, std::string workspace) {
          if (!shell) return;
          using C = sidebar::LinkMenuCommand;
          if (command == C::Peek) shell->callbacks_.peek.Run(url, workspace);
          else if (command == C::NewTab || command == C::Split) shell->callbacks_.open_link.Run(url, workspace, true, command == C::Split);
          else if (command == C::SetRule || command == C::RemoveRule) shell->callbacks_.set_link_rule.Run(url, workspace);
        }, weak_factory_.GetWeakPtr(), action.command, context_url_, std::string(action.workspace)));
    return;
  }
  if ((command_id >= kLinkPeek && command_id <= kRemoveRule) ||
      (command_id >= kRouteBase && command_id < kRouteBase + static_cast<int>(route_workspaces_.size()))) {
    const auto target = command_id >= kRouteBase ? route_workspaces_[command_id - kRouteBase].id : link_source_workspace_;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, int command, GURL url, std::string source, std::string target) {
          if (!shell) return;
          if (command == kLinkPeek) shell->callbacks_.peek.Run(url, source);
          else if (command == kLinkNewTab || command == kLinkSplit)
            shell->callbacks_.open_link.Run(url, source, true, command == kLinkSplit);
          else shell->callbacks_.set_link_rule.Run(url, command == kRemoveRule ? std::string() : target);
        }, weak_factory_.GetWeakPtr(), command_id, context_url_, link_source_workspace_, target));
    return;
  }
  if (command_id == kOpenSplit) {
    QueueSplit(context_tab_, context_pin_);
    return;
  }
  if (command_id == kMoveLeft || command_id == kMoveRight || command_id == kToggleVertical || command_id == kCloseTab) {
    if (!context_tab_) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, std::string workspace, int command, size_t tab) {
          if (!shell || shell->state_.workspace_id != workspace) return;
          if (command == kToggleVertical) shell->callbacks_.split_vertical.Run(!shell->state_.split_vertical);
          else if (command == kCloseTab) shell->callbacks_.close_tab.Run(tab);
          else shell->callbacks_.move_split_pane.Run(tab, command == kMoveRight);
        }, weak_factory_.GetWeakPtr(), state_.workspace_id, command_id, *context_tab_));
    return;
  }
  if (command_id == kCloseSplit || command_id == kSeparateSplit || command_id == kCloseSplitTabs) {
    if (!context_tab_) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, std::string workspace, int command, size_t tab) {
          if (!shell || shell->state_.workspace_id != workspace) return;
          if (command == kSeparateSplit) shell->callbacks_.unsplit.Run(tab);
          else if (command == kCloseSplitTabs) shell->callbacks_.close_split_tabs.Run(tab);
          else shell->callbacks_.leave_split.Run(tab);
        }, weak_factory_.GetWeakPtr(), state_.workspace_id, command_id, *context_tab_));
    return;
  }
  if (metal_sidebar_ && sidebar::workspace_menu_command(command_id, menu_profiles_.size())) {
    const auto action = sidebar::workspace_menu_action(command_id, RustWorkspaceMenuState());
    if (action.command == sidebar::WorkspaceMenuCommand::None) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, sidebar::WorkspaceMenuCommand command, std::string target, std::string profile, SkColor color, std::u16string emoji) {
          if (!shell) return;
          using C = sidebar::WorkspaceMenuCommand;
          switch (command) {
            case C::AssignProfile: shell->callbacks_.assign_profile.Run(target, profile); break;
            case C::NewProfile: shell->OpenProfiles({}, target); break;
            case C::EditProfile: shell->OpenProfiles(profile); break;
            case C::Remove: shell->ConfirmRemoveWorkspace(target); break;
            case C::Color: shell->SetWorkspaceColor(target, color); break;
            case C::Emoji: case C::ClearEmoji: shell->SetWorkspaceEmoji(target, emoji); break;
            case C::Settings: case C::CustomColor: case C::CustomEmoji:
              shell->callbacks_.select_workspace.Run(target);
              shell->OpenWorkspaceSettings();
              if (command == C::CustomColor) shell->color_wheel_[0]->RequestFocus();
              if (command == C::CustomEmoji) shell->emoji_input_->RequestFocus();
              break;
            default: break;
          }
        }, weak_factory_.GetWeakPtr(), action.command, std::string(action.workspace), std::string(action.profile), static_cast<SkColor>(action.color), base::UTF8ToUTF16(std::string(action.emoji))));
    return;
  }
  const auto target = context_workspace_id_;
  std::string profile;
  for (const auto& workspace : state_.workspaces) if (workspace.id == target) profile = workspace.profile_id;
  if (command_id >= kProfileCommandBase && command_id < kProfileCommandBase + static_cast<int>(menu_profiles_.size()))
    profile = menu_profiles_[command_id - kProfileCommandBase].id;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, int command, std::string target, std::string profile) {
        if (!shell) return;
        if (command >= kProfileCommandBase) shell->callbacks_.assign_profile.Run(target, profile);
        else if (command == kNewProfile) shell->OpenProfiles({}, target);
        else if (command == kEditProfile) shell->OpenProfiles(profile);
        else if (command == kRemoveWorkspace) shell->ConfirmRemoveWorkspace(target);
        else if (command >= kColorBase && command < kColorBase + static_cast<int>(kWorkspacePresetCount))
          shell->SetWorkspaceColor(target, WorkspacePreset(command - kColorBase));
        else if (command >= kEmojiBase && command < kEmojiBase + static_cast<int>(kWorkspaceEmojis.size()))
          shell->SetWorkspaceEmoji(target, kWorkspaceEmojis[command - kEmojiBase]);
        else if (command == kClearEmoji) shell->SetWorkspaceEmoji(target, u"");
        else if (command == kWorkspaceSettings || command == kCustomColor || command == kCustomEmoji) {
          shell->callbacks_.select_workspace.Run(target);
          shell->OpenWorkspaceSettings();
          if (command == kCustomColor) shell->color_wheel_[0]->RequestFocus();
          if (command == kCustomEmoji) shell->emoji_input_->RequestFocus();
        }
      }, weak_factory_.GetWeakPtr(), command_id, target, profile));
}

void WorkspaceShell::RebuildSuggestions() {
  auto* palette=static_cast<PaletteView*>(palette_root_.get());
  std::optional<Suggestion> selected;
  if(palette->moved()&&palette->selected()<suggestions_.size()&&suggestions_text_==address_->GetText())
    selected=suggestions_[palette->selected()];
  suggestions_.clear();
  suggestions_text_ = address_->GetText();
  const auto text = base::UTF16ToUTF8(base::TrimWhitespace(address_->GetText(), base::TRIM_ALL));
  const bool commands_only = command_palette_ && text.starts_with(">");
  std::optional<size_t> primary;
  std::optional<Suggestion> primary_item;  // Found again below: rotation and commands move it.
  if (metal_sidebar_) {
    // Native services resolve input and supply results/commands; Rust orders them.
    sidebar::PalettePlanInput input{};input.text=text;input.command_palette=command_palette_;input.results=search_results_.size();
    std::u16string provider_name;GURL url;
    if (!text.empty() && !commands_only) url = ResolveInput(text, &provider_name);
    input.provider=base::UTF16ToUTF8(provider_name);input.resolved=url.is_valid()?url.spec():text;
    for (const auto& provider : tools_.providers) input.keywords.push_back(provider.keyword);
    std::vector<Suggestion> commands;
    if (command_palette_ && callbacks_.commands) commands = callbacks_.commands.Run(base::UTF8ToUTF16(text));
    input.commands=commands.size();
    const auto plan = sidebar::palette_plan(input);
    for (const auto& slot : plan.slots) {
      switch (slot.source) {
        case sidebar::PaletteSource::Primary: primary=suggestions_.size();suggestions_.push_back({base::UTF8ToUTF16(std::string(plan.primary_title)),base::UTF8ToUTF16(std::string(plan.primary_detail)),url,std::nullopt});break;
        case sidebar::PaletteSource::Result: suggestions_.push_back(search_results_.at(slot.index));break;
        default: suggestions_.push_back(commands.at(slot.index));break;
      }
    }
  } else
  {
  if (!text.empty() && !commands_only) {
    std::u16string provider_name;
    const GURL url = ResolveInput(text, &provider_name);
    const bool search = !provider_name.empty();
    const std::u16string search_title = TF(u"Szukaj w {}", {provider_name});
    suggestions_.push_back({search ? search_title : T(u"Otwórz"),
                            search ? base::UTF8ToUTF16(text) : base::UTF8ToUTF16(url.is_valid() ? url.spec() : text),
                            url, std::nullopt});
    primary_item = suggestions_.back();
  }
  if (!commands_only) suggestions_.insert(suggestions_.end(), search_results_.begin(), search_results_.end());
  const auto keyword_end = text.find_first_of(" \t");
  const bool keyword_search = keyword_end != std::string::npos && std::any_of(tools_.providers.begin(), tools_.providers.end(),
      [&](const auto& provider) { return provider.keyword == base::ToLowerASCII(text.substr(0, keyword_end)); });
  if (command_palette_ && !commands_only && !keyword_search && !text.empty() && !search_results_.empty())
    std::rotate(suggestions_.begin(), suggestions_.begin() + 1, suggestions_.end());
  if (command_palette_ && callbacks_.commands) {
    auto commands = callbacks_.commands.Run(base::UTF8ToUTF16(text));
    // Explicit provider prefixes always retain their search action first.
    const auto position = keyword_search && !suggestions_.empty() ? suggestions_.begin() + 1 : suggestions_.begin();
    suggestions_.insert(position, commands.begin(), commands.end());
  }
  if (primary_item) primary = std::find(suggestions_.begin(), suggestions_.end(), *primary_item) - suggestions_.begin();
  }
  // Results come in groups (design: CommandPalette): Akcje, Karty, Przypięte,
  // Przestrzenie, Historia, each under its eyebrow. The resolved search/open row
  // keeps its place and has no group.
  std::vector<std::u16string> headers(suggestions_.size());
  {
    auto rank=[](const Suggestion& s){return !s.command_id.empty()?0:s.tab?1:!s.pin_id.empty()?2:s.workspace_only?3:4;};
    std::vector<size_t> slots;std::vector<Suggestion> grouped;
    for(size_t i=0;i<suggestions_.size();++i)if(i!=primary){slots.push_back(i);grouped.push_back(suggestions_[i]);}
    std::stable_sort(grouped.begin(),grouped.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});
    const std::array<std::u16string,5> names={T(u"Akcje"),T(u"Karty"),T(u"Przypięte"),T(u"Przestrzenie"),T(u"Historia")};
    int previous=-1;
    for(size_t k=0;k<slots.size();++k){suggestions_[slots[k]]=grouped[k];const int r=rank(grouped[k]);if(r!=previous)headers[slots[k]]=base::i18n::ToUpper(names[r]);previous=r;}
  }
  size_t preserved=0;bool found_selected=false;
  if(selected){auto found=std::find(suggestions_.begin(),suggestions_.end(),*selected);if(found!=suggestions_.end()){preserved=found-suggestions_.begin();found_selected=true;}}
  palette->Reconcile(suggestions_.size(),preserved,found_selected);
  sidebar::PaletteScene scene{};std::map<uint64_t,RustRowView*> retained_rows;
  if(palette->scene()){
    palette_node_graph_=palette->node_scope();CHECK(palette_node_graph_);
    auto units=[](const std::u16string& text){rust::Vec<uint16_t> result;for(char16_t unit:text)result.push_back(unit);return result;};
    rust::Vec<sidebar::PaletteSceneInput> items;items.reserve(suggestions_.size());
    std::map<std::string,size_t> occurrences;
    for(const auto& item:suggestions_){
      // Frame the target projection, not its current list index or display text.
      // Native browser services remain authoritative for executing that target.
      base::ListValue identity;identity.Append(state_.profile_id);identity.Append(state_.workspace_id);identity.Append(base::UTF16ToUTF8(suggestions_text_));identity.Append(command_palette_);identity.Append(new_tab_);
      identity.Append(item.url.spec());identity.Append(item.tab?base::NumberToString(*item.tab):std::string());identity.Append(item.workspace_id);identity.Append(item.pin_id);identity.Append(item.workspace_only);identity.Append(item.command_id);identity.Append(item.target_workspace_id);identity.Append(item.command_page_id);
      std::string target;CHECK(base::JSONWriter::Write(identity,&target));identity.Append(base::NumberToString(occurrences[target]++));
      std::string name;CHECK(base::JSONWriter::Write(identity,&name));const uint32_t icon=!item.command_id.empty()?2:(item.tab||item.workspace_only||!item.pin_id.empty()||item.title==T(u"Otwórz"))?1:0;items.push_back({std::move(name),units(item.title),units(item.detail),icon});
    }
    CHECK(palette_node_graph_->graph->reconcile_palette(std::move(items)).admitted)<<"Invalid palette scene projection";scene=palette_node_graph_->graph->palette_scene();CHECK_NE(scene.root,0u);
    std::set<uint64_t> wanted;for(const auto& child:scene.children){CHECK_EQ(child.parent,scene.root);wanted.insert(child.id);}
    std::vector<views::View*> removed;for(const auto& child:suggestion_list_->children()){auto* row=views::AsViewClass<RustRowView>(child.get());const auto id=row?row->LogicalNodeId():0;if(id&&wanted.contains(id)&&row->BelongsTo(*palette_node_graph_))retained_rows.emplace(id,row);else removed.push_back(child.get());}
    for(auto* child:removed)suggestion_list_->RemoveChildViewT(child);
  }else
  suggestion_list_->RemoveAllChildViews();
  for (size_t i = 0; i < suggestions_.size(); ++i) {
    if(palette->scene()){
      const auto& child=scene.children[i];auto found=retained_rows.find(child.id);RustRowView* adapter=nullptr;
      if(found!=retained_rows.end())adapter=found->second;
      else{
        RowRecipe::Spec spec;spec.chip=true;spec.sidebar_row=metal_palette_!=nullptr;spec.height=child.height;spec.title=std::u16string(child.title.begin(),child.title.end());spec.detail=std::u16string(child.detail.begin(),child.detail.end());spec.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak_factory_.GetWeakPtr());
        auto callback=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::weak_ptr<SidebarNodeGraphState> owner,std::string key,uint64_t id){const auto scope=owner.lock();if(!shell||!scope||shell->palette_node_graph_!=scope||!shell->address_visible())return;const auto target=scope->graph->palette_target(key,id);if(target.valid)shell->RunSuggestion(target.index);},weak_factory_.GetWeakPtr(),std::weak_ptr<SidebarNodeGraphState>(palette_node_graph_),std::string(child.key),child.id);
        adapter=views::AsViewClass<RustRowView>(suggestion_list_->AddChildView(CreateSceneRow(std::move(callback),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{palette_node_graph_,6,{},std::string(child.key)})));
      }
      CHECK(adapter);adapter->ProjectPalette(child);adapter->SetPaletteHeader(headers[i],child.height);suggestion_list_->ReorderChildView(adapter,i);continue;
    }
    const auto& item = suggestions_[i];
    RowRecipe::Spec spec;
    spec.title = item.title;
    spec.detail = item.detail;
    spec.chip = true;
    spec.header = headers[i];
    spec.height = 36 + (spec.header.empty() ? 0 : kPaletteHeader);
    spec.highlighted=i==palette->selected();
    spec.sidebar_row=metal_palette_!=nullptr;
    spec.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak_factory_.GetWeakPtr());
    spec.icon = !item.command_id.empty() ? &vector_icons::kSettingsIcon
              : (item.tab || item.workspace_only || !item.pin_id.empty()) ? &vector_icons::kArrowForwardIcon
              : item.title == T(u"Otwórz") ? &vector_icons::kArrowForwardIcon
                                        : &vector_icons::kSearchIcon;
    suggestion_list_->AddChildView(CreateSceneRow(
        base::BindRepeating(&WorkspaceShell::RunSuggestion,weak_factory_.GetWeakPtr(),i),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{}));
  }
  suggestion_list_->SetVisible(!suggestions_.empty());
  suggestion_scroll_->view()->SetVisible(!suggestions_.empty());
  PositionPanels();
  address_widget_->LayoutRootViewIfNecessary();
  if (!suggestions_.empty()) suggestion_list_->children()[palette->selected()]->ScrollViewToVisible();
  ScheduleMetalPalette();
}

void WorkspaceShell::StopWorkspaceAnimation() {
  ScheduleMetalSidebar();
  ++workspace_animation_generation_;
  workspace_animation_pending_ = false;
  if(workspace_switch_overlay_){auto* overlay=workspace_switch_overlay_.get();workspace_switch_overlay_=nullptr;sidebar_root_->RemoveChildViewT(overlay);if(tab_list_)tab_list_->SchedulePaint();}
  if (tab_scroll_ && tab_scroll_->view()->layer()) {
    tab_scroll_->view()->layer()->GetAnimator()->StopAnimating();
    tab_scroll_->view()->layer()->SetTransform(gfx::Transform());
  }
}
void WorkspaceShell::AnimateWorkspaceSwitch(std::string_view previous) {
  StopWorkspaceAnimation();
  if (swipe_committing_ && state_.workspace_id == swipe_commit_target_) return;
  CancelWorkspaceSwipe();
  int old_index = -1, new_index = -1;
  for (size_t i = 0; i < state_.workspaces.size(); ++i) {
    if (state_.workspaces[i].id == previous) old_index = i;
    if (state_.workspaces[i].id == state_.workspace_id) new_index = i;
  }
  const auto motion=sidebar::workspace_switch_motion(old_index,new_index,sidebar_visible(),workspace_animations_,gfx::Animation::PrefersReducedMotion());
  if(metal_sidebar_){if(!motion.enabled)return;}
  // Prepare the new viewport before starting the clock. Otherwise its first
  // layout/raster can consume much of a short animation and skip its opening.
  sidebar_widget_->LayoutRootViewIfNecessary();
  if (metal_sidebar_) {
    metal_sidebar_->Hide();HideMetalSidebarGrip();
    tab_list_->SchedulePaint();  // Refresh cached native contents before animating.
    new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint();
  }
  if(metal_sidebar_) {
    auto* viewport=tab_list_->parent();
    const auto frame=views::View::ConvertRectToTarget(viewport,sidebar_root_,viewport->GetLocalBounds());
    const auto tint=FrameTint(workspace_color(state_.workspace_id),root_);
    auto preview=std::make_unique<WorkspaceSwitchPreview>(CaptureWorkspaceScene(state_.workspace_id,frame.size(),tab_scroll_->visible_rect().y(),true),pinned_?tint:Mix(tint,Tone(root_,kCard),70));
    auto* canvas=preview.get();workspace_switch_overlay_=sidebar_root_->AddChildView(std::move(preview));canvas->SetBoundsRect(frame);
    workspace_animation_pending_=true;const auto generation=workspace_animation_generation_;
    auto presented=base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,size_t generation,unsigned duration){
      if(!shell||generation!=shell->workspace_animation_generation_||!shell->workspace_switch_overlay_)return;
      shell->workspace_animation_pending_=false;
      if(!shell->sidebar_visible()||!shell->workspace_animations_||gfx::Animation::PrefersReducedMotion()){shell->StopWorkspaceAnimation();return;}
      static_cast<WorkspaceSwitchPreview*>(shell->workspace_switch_overlay_.get())->Animate(base::Milliseconds(duration),base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,size_t generation){
        if(shell&&generation==shell->workspace_animation_generation_)shell->StopWorkspaceAnimation();
      },shell,generation));
    },weak_factory_.GetWeakPtr(),generation,motion.duration_ms);
    if(canvas->Prepare(motion.offset,std::move(presented))){tab_list_->SchedulePaint();return;}
    // Missing GPU resources/drawable: discard the entire overlay and run the
    // existing native animation below. Any late presentation is generation stale.
    StopWorkspaceAnimation();
  }
  auto* compositor=sidebar_widget_->GetCompositor();
  if(!compositor)return;
  tab_scroll_->view()->SetPaintToLayer();
  tab_scroll_->view()->layer()->SetFillsBoundsOpaquely(false);
  gfx::Transform offset; offset.Translate(new_index > old_index ? 32 : -32, 0);
  tab_scroll_->view()->layer()->SetTransform(offset);
  workspace_animation_pending_ = true;
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, size_t generation, const viz::FrameTimingDetails&) {
        if (!shell || generation != shell->workspace_animation_generation_) return;
        shell->workspace_animation_pending_ = false;
        if (!shell->sidebar_visible() || !shell->workspace_animations_ || gfx::Animation::PrefersReducedMotion()) {
          shell->StopWorkspaceAnimation();
          return;
        }
        auto* layer = shell->tab_scroll_->view()->layer();
        ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
        settings.SetTransitionDuration(base::Milliseconds(180));
        settings.SetTweenType(gfx::Tween::EASE_OUT);
        settings.CacheRenderSurface();
        if (shell->metal_sidebar_) {
          // Scoped settings detach and activate this self-owned observer after
          // all implicit sequences are registered, including zero-duration ones.
          settings.AddObserver(new ui::ClosureAnimationObserver(base::BindOnce(
              &WorkspaceShell::ScheduleMetalSidebar, shell)));
        }
        layer->SetTransform(gfx::Transform());
      }, weak_factory_.GetWeakPtr(), workspace_animation_generation_));
  compositor->ScheduleDraw();
}
bool WorkspaceShell::workspace_switch_metal_visible() const {
  return workspace_switch_overlay_&&static_cast<WorkspaceSwitchPreview*>(workspace_switch_overlay_.get())->visible();
}
bool WorkspaceShell::workspace_switch_contains_scene_text(std::string_view text) const {
  return workspace_switch_overlay_&&static_cast<WorkspaceSwitchPreview*>(workspace_switch_overlay_.get())->contains_text(text);
}
bool WorkspaceShell::workspace_animation_running() const {
  return workspace_animation_pending_ || workspace_switch_overlay_ ||
      (tab_scroll_ && tab_scroll_->view()->layer() && tab_scroll_->view()->layer()->GetAnimator()->is_animating());
}

void WorkspaceShell::ApplyTheme(bool refresh_controls) {
  if (!widget_ || applying_theme_) return;
  applying_theme_ = true;
  std::optional<ui::ColorProviderKey::ColorMode> mode;
  if (theme_mode_ != ThemeMode::kSystem) mode = theme_mode_ == ThemeMode::kDark
      ? ui::ColorProviderKey::ColorMode::kDark : ui::ColorProviderKey::ColorMode::kLight;
  for (auto* panel : {widget_.get(), sidebar_widget_.get(), address_widget_.get(), settings_widget_.get(),
       profiles_widget_.get(), import_widget_.get(), peek_widget_.get(), general_widget_.get(), tools_widget_.get()}) {
    if (!panel) continue;
    if (panel->color_mode_override() != mode) {
      panel->SetColorModeOverride(mode);
      PreparePanelText(panel->GetRootView());
    }
#if BUILDFLAG(IS_MAC)
    SetWorkspaceNativeAppearance(panel, mode ? std::optional<bool>(*mode == ui::ColorProviderKey::ColorMode::kDark) : std::nullopt);
#endif
  }
  for (auto* surface : {settings_root_.get(), profiles_root_.get(), import_root_.get(),
       peek_widget_ ? peek_widget_->GetContentsView() : nullptr, general_root_.get(), palette_root_.get(), tools_root_.get()}) {
    if (!surface) continue;
    const int radius = surface == palette_root_ ? 20 : 12;  // radius-lg / radius-md
    if(surface==palette_root_&&static_cast<PaletteView*>(surface)->scene()){surface->SetBackground(nullptr);surface->SetBorder(views::CreateEmptyBorder(1));}
    else {surface->SetBackground(views::CreateRoundedRectBackground(Tone(surface,kCard),radius));
      if((surface==profiles_root_&&profile_form_)||(surface==settings_root_&&workspace_form_)||(surface==import_root_&&import_form_))surface->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,radius,Hairline(surface)),gfx::Insets(20)));
      else surface->SetBorder(views::CreateRoundedRectBorder(1,radius,Hairline(surface)));}
    surface->SchedulePaint();
  }
  for (auto* field : {snapshot_name_.get(), timeline_filter_.get(), shortcut_chord_.get(), provider_name_.get(), provider_keyword_.get(), provider_url_.get(),history_search_.get()})
    if (field) field->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_, kChip));
  content_host_->SetBackground(views::CreateRoundedRectBackground(Tone(root_, kCard), 10));
  if (general_scroll_) general_scroll_->SetViewportBackground(Tone(root_, kCard));
  if (preferences_search_) preferences_search_->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_, kChip));
  for (const auto& [key, field] : preference_textfields_) field->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_, kChip));
  if (site_origin_) site_origin_->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_, kChip));
  if (address_) address_->SetPresentation(17,0,false,Tone(root_,kCard));
  for(auto* editor:{emoji_input_.get()})if(editor)
    if(auto* native=views::AsViewClass<views::Textfield>(editor->view())){
      native->SetBackgroundColor(Tone(root_, kChip));
      native->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,7,Hairline(root_)),gfx::Insets::VH(4,8)));
    }

  if(suggestion_list_) {
    auto border=views::CreateSolidSidedBorder(gfx::Insets::TLBR(1,0,0,0),Hairline(root_));
    if(metal_palette_)border=views::CreatePaddedBorder(std::move(border),gfx::Insets(8));
    suggestion_list_->SetBorder(std::move(border));
  }
  ScheduleMetalPalette();
  if (refresh_controls) RefreshGeneralControls();
  applying_theme_ = false;
}
void WorkspaceShell::OnNativeThemeUpdated(ui::NativeTheme*) {
  if (theme_mode_ == ThemeMode::kSystem && general_widget_) ApplyAppearance();
}
void WorkspaceShell::ScheduleToolsChrome(){
  SyncSceneRowInput();
  if(tools_chrome_)tools_chrome_->Schedule();
}
bool WorkspaceShell::ToolsChromeVisible() const{
  return tools_chrome_&&tools_chrome_->visible();
}
views::View* WorkspaceShell::AddToolsCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline){
  if(tools_chrome_)return parent->AddChildView(std::make_unique<PreferenceCopy>(std::move(text),tone,delta,static_cast<gfx::Font::Weight>(weight),multiline,base::BindRepeating(&WorkspaceShell::ScheduleToolsChrome,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->ToolsChromeVisible();},weak_factory_.GetWeakPtr())));
  auto* label=Label(parent,std::move(text),tone,Font(delta,static_cast<gfx::Font::Weight>(weight)));label->SetMultiLine(multiline);return label;
}
bool WorkspaceShell::tools_chrome_contains_text_for_testing(std::string_view text) const{
  return tools_chrome_&&tools_chrome_->contains_text(text);
}
bool WorkspaceShell::tools_chrome_excludes_for_testing(views::View* view) const{
  return tools_chrome_&&tools_chrome_->excludes(view);
}
bool WorkspaceShell::tools_chrome_order_correct_for_testing() const{
  return tools_chrome_&&tools_chrome_->order_correct();
}
std::string WorkspaceShell::tools_chrome_debug_for_testing() const{
  return tools_chrome_?tools_chrome_->Debug():"no renderer";
}
void WorkspaceShell::SchedulePreferenceChrome(){
  if(preferences_chrome_)preferences_chrome_->Schedule();
}
bool WorkspaceShell::PreferenceChromeVisible() const{
  return preferences_chrome_&&preferences_chrome_->visible();
}
views::View* WorkspaceShell::AddPreferenceCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline){
  if(preferences_chrome_)return parent->AddChildView(std::make_unique<PreferenceCopy>(std::move(text),tone,delta,static_cast<gfx::Font::Weight>(weight),multiline,base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak_factory_.GetWeakPtr())));
  auto* label=Label(parent,std::move(text),tone,Font(delta,static_cast<gfx::Font::Weight>(weight)));label->SetMultiLine(multiline);return label;
}
void WorkspaceShell::SetPreferenceCopyTone(views::View* view,SkColor tone){
  if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->SetTone(tone);return;}
  static_cast<ThemedLabel*>(view)->SetTone(tone);
}
bool WorkspaceShell::preference_chrome_contains_text_for_testing(std::string_view text) const{
  return preferences_chrome_&&preferences_chrome_->contains_text(text);
}
std::string WorkspaceShell::preference_chrome_debug_for_testing() const{
  return preferences_chrome_?preferences_chrome_->Debug():"no renderer";
}
bool WorkspaceShell::preference_chrome_order_correct_for_testing() const{
  return preferences_chrome_&&preferences_chrome_->order_correct();
}
bool WorkspaceShell::preference_chrome_excludes_for_testing(views::View* view) const{
  return preferences_chrome_&&preferences_chrome_->excludes(view);
}
size_t WorkspaceShell::preference_category_for_testing() const{
  return preferences_form_?preferences_form_->form->preference_current():0;
}
views::View* WorkspaceShell::preference_navigation_for_testing(size_t category) const{return category<preference_sections_.size()?preference_sections_[category].navigation.get():nullptr;}
size_t WorkspaceShell::preference_navigation_frame_count_for_testing() const{
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group?group->frame_count():0;
}
bool WorkspaceShell::preference_navigation_selected_for_testing(size_t category) const{
  auto* control=preference_navigation_for_testing(category);if(!control)return false;
  if(auto* button=views::AsViewClass<ScenePreferenceButton>(control))return button->selected();
  return RowSpec(control).active;
}
bool WorkspaceShell::preference_navigation_metal_visible_for_testing() const{
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group&&group->metal_visible();
}
bool WorkspaceShell::preference_navigation_contains_text_for_testing(std::string_view text) const{
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group&&group->contains_text(text);
}
bool WorkspaceShell::preference_close_metal_visible_for_testing() const{
  auto* group=preferences_close_?views::AsViewClass<ScenePreferenceControls>(preferences_close_->parent()):nullptr;return group&&group->metal_visible();
}
bool WorkspaceShell::preference_action_metal_visible_for_testing(views::View* button) const {
  auto* group=button?views::AsViewClass<SceneActionGroup>(button->parent()):nullptr;return group&&group->metal_visible();
}
bool WorkspaceShell::preference_actions_contains_scene_text_for_testing(std::string_view text) const {
  if(!general_widget_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,general_widget_->GetContentsView());
}
views::View* WorkspaceShell::preference_switch_for_testing(std::string_view key) const {
  if(key=="sidebar")return sidebar_check_;
  if(key=="animations")return animations_check_;
  if(key=="workspace_names")return workspace_names_check_;
  if(key=="folders_collapsed")return folders_collapsed_check_;
  const auto it=preference_toggles_.find(std::string(key));
  return it==preference_toggles_.end()?nullptr:it->second.get();
}
bool WorkspaceShell::preference_switch_checked_for_testing(std::string_view key) const {
  auto* control=preference_switch_for_testing(key);return control&&ToggleOn(control);
}
bool WorkspaceShell::preference_switch_metal_visible_for_testing(std::string_view key) const {
  auto* scene=views::AsViewClass<SceneToggle>(preference_switch_for_testing(key));return scene&&scene->metal_visible();
}
bool WorkspaceShell::preference_switch_timer_running_for_testing(std::string_view key) const {
  auto* scene=views::AsViewClass<SceneToggle>(preference_switch_for_testing(key));return scene&&scene->timer_running_for_testing();
}
views::View* WorkspaceShell::clear_checkbox_for_testing(size_t index) const {
  const std::array<views::View*,3> choices={clear_history_,clear_cookies_,clear_cache_};
  return index<choices.size()?choices[index]:nullptr;
}
bool WorkspaceShell::clear_choices_metal_visible_for_testing() const {
  auto* group=clear_history_?views::AsViewClass<SceneCheckboxGroup>(clear_history_->parent()):nullptr;
  return group&&group->metal_visible();
}
views::View* WorkspaceShell::preference_choice_for_testing(std::string_view key) const {
  if(key=="theme")return theme_picker_;if(key=="search")return search_engine_picker_;
  if(key=="category")return preferences_category_picker_;if(key=="site_type")return site_type_;
  if(key=="site_policy")return site_policy_;if(key=="clear_period")return clear_data_period_;
  const auto it=preference_pickers_.find(std::string(key));return it==preference_pickers_.end()?nullptr:it->second.get();
}
std::optional<size_t> WorkspaceShell::preference_choice_selection_for_testing(std::string_view key) const {
  auto* picker=preference_choice_for_testing(key);return picker?PickerIndex(picker):std::nullopt;
}
size_t WorkspaceShell::preference_choice_count_for_testing(std::string_view key) const {
  auto* picker=preference_choice_for_testing(key);return picker?PickerCount(picker):0;
}
std::u16string WorkspaceShell::preference_choice_label_for_testing(std::string_view key,size_t index) const {
  auto* picker=preference_choice_for_testing(key);if(!picker||index>=PickerCount(picker))return {};
  auto* scene=views::AsViewClass<SceneSelect>(picker);return scene->LabelAt(index);
}
bool WorkspaceShell::preference_choice_metal_visible_for_testing(std::string_view key) const {
  auto* scene=views::AsViewClass<SceneSelect>(preference_choice_for_testing(key));return scene&&scene->metal_visible();
}
gfx::Rect WorkspaceShell::preference_choice_metal_bounds_for_testing(std::string_view key) const {
  auto* scene=views::AsViewClass<SceneSelect>(preference_choice_for_testing(key));if(scene)return scene->metal_bounds_for_testing();
  return {};
}
void WorkspaceShell::ActivatePreferenceChoiceForTesting(std::string_view key,size_t index){
  auto* picker=preference_choice_for_testing(key);if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
  auto* scene=views::AsViewClass<SceneSelect>(picker);scene->SelectForTesting(index);
}
void WorkspaceShell::RefreshFormViewports(){
  auto refresh=[&](auto&& self,views::View* view)->void{
    if(auto* paragraph=views::AsViewClass<SceneParagraph>(view))paragraph->OnVisibleBoundsChanged();
    if(auto* row=views::AsViewClass<AppearanceRow>(view))row->SyncViewport();
    for(const auto& child:view->children())self(self,child.get());};
  for(auto* root:{settings_root_.get(),import_root_.get()})if(root){RefreshChoiceViewports(root);refresh(refresh,root);root->SchedulePaint();}
}
void WorkspaceShell::RefreshPreferenceViewport(){
  SchedulePreferenceChrome();
  if(preferences_body_)RefreshChoiceViewports(preferences_body_);
  for(const auto& [key,field]:preference_textfields_)field->view()->SchedulePaint();
  if(site_origin_)site_origin_->view()->SchedulePaint();
}
void WorkspaceShell::BuildRustPreferenceRows(const sidebar::PreferenceCatalogue& catalogue) {
  using Kind=sidebar::PreferenceRowKind;
  using Action=sidebar::PreferenceActionKind;
  auto weak=weak_factory_.GetWeakPtr();
  auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};
  auto strings=[&](const auto& input){std::vector<std::u16string> output;for(const auto& value:input)output.push_back(text(value));return output;};
  auto copy=[&](views::View* parent,std::u16string value,SkColor tone,int delta,gfx::Font::Weight weight,bool multiline){return AddPreferenceCopy(parent,std::move(value),tone,delta,static_cast<int>(weight),multiline);};
  auto make_row=[&](const sidebar::PreferenceSpec& spec){
    CHECK_LT(spec.category,preference_sections_.size());
    auto* row=preference_sections_[spec.category].view->AddChildView(std::make_unique<PreferenceSurface>(false,base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak)));
    auto* labels=row->AddChildView(std::make_unique<views::View>());SetSidebarPanelLayout(labels,true,gfx::Insets(),5);
    copy(labels,text(spec.title),kInk,0,gfx::Font::Weight::MEDIUM,true);copy(labels,text(spec.detail),kMuted,-1,gfx::Font::Weight::NORMAL,true);SetSidebarPanelLayout(row,false,gfx::Insets(16),20,labels,true);
    preference_sections_[spec.category].rows.push_back({row,base::i18n::ToLower(text(catalogue.categories[spec.category])+u" "+text(spec.title)+u" "+text(spec.detail))});return row;
  };
  for(const auto& spec:catalogue.rows){
    auto* row=make_row(spec);const auto title=text(spec.title);const std::string key(spec.key);
    if(spec.kind==Kind::Theme||spec.kind==Kind::Search||spec.kind==Kind::Picker||spec.kind==Kind::Language){
      auto* control=ChoicePicker(row,title,strings(spec.choices));
      if(spec.kind==Kind::Language){language_picker_=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetUiLanguageChoice(PickerIndex(shell->language_picker_).value_or(0));},weak));}
      else if(spec.kind==Kind::Theme){theme_picker_=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetThemeMode(static_cast<ThemeMode>(PickerIndex(shell->theme_picker_).value_or(0)));},weak));}
      else if(spec.kind==Kind::Search){search_engine_picker_=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell)return;const size_t index=PickerIndex(shell->search_engine_picker_).value_or(0);if(index<shell->tools_.providers.size())shell->SetDefaultSearchProvider(shell->tools_.providers[index].keyword);},weak));}
      else{preference_pickers_[key]=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell){auto index=PickerIndex(shell->preference_pickers_.at(key));if(index)shell->WritePreference(key,static_cast<int>(*index));}},weak,key));}
      continue;
    }
    if(spec.kind==Kind::Pinned||spec.kind==Kind::Animations||spec.kind==Kind::WorkspaceNames||spec.kind==Kind::FoldersCollapsed||spec.kind==Kind::Toggle){
      auto* control=PreferenceToggle(row,title);
      if(spec.kind==Kind::Pinned){sidebar_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetPinned(ToggleOn(shell->sidebar_check_));},weak));}
      else if(spec.kind==Kind::Animations){animations_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetWorkspaceAnimations(ToggleOn(shell->animations_check_));},weak));}
      else if(spec.kind==Kind::FoldersCollapsed){folders_collapsed_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetPinnedFoldersCollapsed(ToggleOn(shell->folders_collapsed_check_));},weak));}
      else if(spec.kind==Kind::WorkspaceNames){workspace_names_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetWorkspaceNames(ToggleOn(shell->workspace_names_check_));},weak));}
      else{preference_toggles_[key]=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell)shell->WritePreference(key,ToggleOn(shell->preference_toggles_.at(key)));},weak,key));}
      continue;
    }
    if(spec.kind==Kind::Text){
      auto* field=AddEditor(row->children().front());field->GetViewAccessibility().SetName(title);field->SetPlaceholderText(text(spec.placeholder));
      field->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell)shell->PreferenceTextEdited(key);},weak,key),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){return shell&&shell->HandlePreferenceEditorKey(field,event);},weak,base::Unretained(field)));field->view()->SetPreferredSize(gfx::Size(240,36));preference_textfields_[key]=field;
      auto* save=PreferenceAction(row,{},text(spec.button));BindFormCommand(save,sidebar::FormCommand::PreferenceText,key);save->SetTooltipText(text(spec.button_ax));save->GetViewAccessibility().SetName(text(spec.button_ax));preference_save_buttons_[key]=save;continue;
    }
    switch(spec.kind){
      case Kind::Action:{
        using Command=sidebar::FormCommand;Command command=Command::None;
        switch(spec.action){
          case Action::History:command=Command::History;break;
          case Action::Workspace:command=Command::Workspace;break;
          case Action::Passwords:command=Command::Passwords;break;
          case Action::Profiles:command=Command::Profiles;break;
          case Action::Import:command=Command::Import;break;
          case Action::Proxy:command=Command::Proxy;break;
          case Action::Certificates:command=Command::Certificates;break;
          case Action::Providers:command=Command::Providers;break;
          case Action::Shortcuts:command=Command::Shortcuts;break;
          case Action::TimeMachine:command=Command::TimeMachine;break;
          default:CHECK(false);
        }
        auto* button=PreferenceAction(row,{},text(spec.button));BindFormCommand(button,command);button->SetTooltipText(text(spec.button_ax));button->GetViewAccessibility().SetName(text(spec.button_ax));break;
      }
      case Kind::Directory:
        download_directory_button_=PreferenceAction(row,{},text(spec.button));BindFormCommand(download_directory_button_,sidebar::FormCommand::PreferenceDirectory);download_directory_button_->SetTooltipText(text(spec.button_ax));download_directory_button_->GetViewAccessibility().SetName(text(spec.button_ax));download_directory_label_=copy(row->children().front().get(),u"",kMuted,-1,gfx::Font::Weight::NORMAL,true);break;
      case Kind::SiteEditor:{
        const auto& site=catalogue.site;auto* fields=row->children().front().get();site_origin_=AddEditor(fields);site_origin_->GetViewAccessibility().SetName(text(site.origin_ax));site_origin_->SetPlaceholderText(text(site.placeholder));site_origin_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){return shell&&shell->HandlePreferenceEditorKey(field,event);},weak,base::Unretained(site_origin_.get())));
        site_type_=ChoicePicker(fields,text(site.type_ax),strings(site.types));site_policy_=ChoicePicker(fields,text(site.policy_ax),strings(sidebar::preference_site_policies(0)));
        SetPickerCallback(site_type_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell)return;std::vector<std::u16string> choices;for(const auto& choice:sidebar::preference_site_policies(PickerIndex(shell->site_type_).value_or(0)))choices.push_back(base::UTF8ToUTF16(std::string(choice)));ReplacePickerLabels(shell->site_policy_,choices);},weak));
        BindFormCommand(PreferenceAction(row,{},text(site.save)),sidebar::FormCommand::PreferenceSite);break;
      }
      case Kind::SiteRules:
        site_rules_card_=row;site_rules_list_=row->children().front()->AddChildView(std::make_unique<views::View>());SetSidebarPanelLayout(site_rules_list_,true,gfx::Insets(),8);break;
      case Kind::ClearData:{
        const auto& clear=catalogue.clear;auto* fields=row->children().front().get();clear_data_period_=ChoicePicker(fields,text(clear.period_ax),strings(clear.periods));auto* checks=fields->AddChildView(std::make_unique<SceneCheckboxGroup>(8));CHECK_EQ(clear.checks.size(),3u);
        clear_history_=AddImportCheck(checks,text(clear.checks[0]),{});clear_cookies_=AddImportCheck(checks,text(clear.checks[1]),{});clear_cache_=AddImportCheck(checks,text(clear.checks[2]),{});SetCheckValue(clear_cache_,true);
        clear_data_button_=PreferenceAction(row,{},text(clear.action));BindFormCommand(clear_data_button_,sidebar::FormCommand::PreferenceClear);break;
      }
      case Kind::ShortcutHint:row->SetTooltipText(text(spec.tooltip));break;
      default:CHECK(false);
    }
  }
}
void WorkspaceShell::RefreshGeneralControls() {
  if (!general_widget_) return;
  bool profile_changed=false;
  if(preferences_form_)profile_changed=preferences_form_->form->preference_profile(state_.profile_id);
  if(profile_changed){CancelChoiceInteractions(general_widget_->GetContentsView());
    CancelSceneActions(general_widget_->GetContentsView());
  }
  SyncPickerIndex(theme_picker_,static_cast<size_t>(theme_mode_));
  std::vector<std::pair<std::string,std::u16string>> provider_choices;
  size_t provider_index=0;
  const std::string implicit=search_engine_==SearchEngine::kGoogle?"g":search_engine_==SearchEngine::kBing?"b":"d";
  const std::string selected=tools_.default_keyword.empty()?implicit:tools_.default_keyword;
  for(size_t i=0;i<tools_.providers.size();++i){
    provider_choices.emplace_back(tools_.providers[i].keyword,base::UTF8ToUTF16(tools_.providers[i].name));
    if(tools_.providers[i].keyword==selected)provider_index=i;
  }
  ReplacePickerOptions(search_engine_picker_,provider_choices);
  SyncPickerIndex(search_engine_picker_,provider_choices.empty()?std::nullopt:std::optional<size_t>(provider_index));
  SetToggleOn(sidebar_check_,pinned_);
  SetToggleOn(animations_check_,workspace_animations_);
  SetToggleOn(workspace_names_check_,workspace_names_);
  SetToggleOn(folders_collapsed_check_,folders_collapsed_);
  {const auto stored=StoredUiLanguage();SyncPickerIndex(language_picker_,stored=="pl"?1:stored=="en"?2:0);}
  if (profile_changed) {
    site_origin_->SetText(u"");
    ResetPreferenceRuleLimit();
    RefreshSitePreferences();
  }
  // Rust owns availability/managed copy; Views keeps its original strings.
  const bool rust_copy =
      bool(preferences_form_);
  auto control_view = [&](bool toggle, bool present, bool editable, std::u16string native) {
    if (rust_copy) return base::UTF8ToUTF16(std::string(sidebar::preference_control_view(toggle ? sidebar::PreferenceControlKind::Toggle : sidebar::PreferenceControlKind::Text, present, editable).tooltip));
    return native;
  };
  if (rust_copy) SetPanelText(preferences_profile_,base::UTF8ToUTF16(std::string(sidebar::preferences_profile_banner(base::UTF16ToUTF8(state_.profile_name)))));
  else
  SetPanelText(preferences_profile_,TF(u"Profil: {} · Ustawienia wspólne dla aplikacji są oznaczone w opisach.", {state_.profile_name}));
  const auto values = callbacks_.read_preferences ? callbacks_.read_preferences.Run() : BrowserPreferences();
  SetPanelText(download_directory_label_,callbacks_.download_directory ? callbacks_.download_directory.Run() : u"");
  for (const auto& [key, field] : preference_textfields_) {
    auto it = values.find(key);
    const bool enabled = it != values.end() && it->second.editable;
    field->view()->SetEnabled(enabled);
    preference_save_buttons_.at(key)->SetEnabled(enabled);
    if (SyncPreferenceText(key,it!=values.end(),enabled,field->IsIMEComposing(),profile_changed)) {
      field->SetText(base::UTF8ToUTF16(it->second.text));
    }
    field->view()->SetTooltipText(control_view(false, it != values.end(), enabled, it == values.end() ? T(u"Ustawienie jest niedostępne") : !enabled ? T(u"To ustawienie jest zarządzane") : T(u"Zapisz przyciskiem lub klawiszem Enter")));
  }
  auto directory = values.find("download_directory");
  download_directory_button_->SetEnabled(directory != values.end() && directory->second.editable);
  for (const auto& [key, control] : preference_toggles_) {
    auto it = values.find(key);
    control->SetEnabled(it != values.end() && it->second.editable);
    if (it != values.end()) SetToggleOn(control,it->second.value != 0);
    control->SetTooltipText(control_view(true, it != values.end(), it != values.end() && it->second.editable, it == values.end() ? T(u"Profil nie jest jeszcze gotowy") :
        !it->second.editable ? T(u"To ustawienie jest zarządzane") : T(u"Dotyczy aktywnego profilu")));
  }
  for (const auto& [key, control] : preference_pickers_) {
    auto it = values.find(key);
    control->SetEnabled(it != values.end() && it->second.editable);
    if (it != values.end()) SyncPickerIndex(control,it->second.value < 0 ? std::nullopt : std::optional<size_t>(it->second.value));
  }
}
void WorkspaceShell::SelectPreferenceCategory(size_t category) {
  if (category >= preference_sections_.size()) return;
  if(preferences_form_){if(!preferences_form_->form->preference_select(category))return;}
  SyncPickerIndex(preferences_category_picker_,category);
  preferences_search_->SetText(u"");
  FilterPreferences();
}
void WorkspaceShell::FilterPreferences() {
  if (site_rules_list_) RefreshSitePreferences(false);
  const auto query = base::i18n::ToLower(base::TrimWhitespace(preferences_search_->GetText(), base::TRIM_ALL));
  if(!preferences_form_)return;
  SyncPickerIndex(preferences_category_picker_,preferences_form_->form->preference_current());
  std::vector<sidebar::PreferenceRow> rows;
  for(size_t i=0;i<preference_sections_.size();++i)for(const auto& row:preference_sections_[i].rows){sidebar::PreferenceRow input{};input.category=i;for(auto unit:row.keywords)input.keywords.push_back(unit);rows.push_back(std::move(input));}
  rust::Vec<uint16_t> normalized;for(auto unit:query)normalized.push_back(unit);
  auto visible=preferences_form_->form->preference_filter(std::move(normalized),rust::Slice<const sidebar::PreferenceRow>(rows.data(),rows.size()));
  size_t cursor=0;for(size_t i=0;i<preference_sections_.size();++i){auto& section=preference_sections_[i];for(auto& row:section.rows)row.view->SetVisible(visible.rows[cursor++]);section.view->SetVisible(visible.sections[i]);SetPreferenceNavigationSelected(section.navigation,visible.navigation[i]);}
  preferences_empty_->SetVisible(!visible.any);preferences_body_->InvalidateLayout();general_scroll_->ScrollTo(0);
}
void WorkspaceShell::SetBrowsingDataBusy(bool busy) {
  browsing_data_busy_ = busy;
  if (history_clear_) {
    const auto presentation=ToolsClearControls(busy);
    history_clear_->SetEnabled(presentation.enabled);
    SetActionText(history_clear_,presentation.label);
  }
  if (!clear_data_button_) return;
  clear_data_button_->SetEnabled(!busy);
  SetActionText(clear_data_button_,busy ? T(u"Czyszczenie…") : T(u"Wyczyść…"));
}
void WorkspaceShell::ReportPreferenceOutcome(int outcome, const std::string& key, bool ok, std::u16string native) {
  bool error = !ok;
  if (preferences_form_) {
    const auto status = preferences_form_->form->preference_outcome(sidebar::PreferenceOutcome{static_cast<uint8_t>(outcome)}, key, ok);
    native = base::UTF8ToUTF16(std::string(status.text)); error = status.error;
  }
  if (outcome != 4) { SetPreferencesStatus(std::move(native), error); return; }
  if (!general_status_) return;
  SetPanelText(general_status_, native); SetPreferenceCopyTone(general_status_, error ? kError : kMuted);
}
void WorkspaceShell::SetPreferencesStatus(std::u16string text, bool error) {
  if (history_visible()) SetPanelText(tools_status_,text);
  if (!general_status_) return;
  SetPanelText(general_status_,text);SetPreferenceCopyTone(general_status_,error?kError:kMuted);
}
void WorkspaceShell::PreferenceTextEdited(const std::string& key){
  if(preferences_form_){preferences_form_->form->preference_edited(key);return;}
  preference_text_dirty_.insert(key);
}
void WorkspaceShell::PreferenceTextSaved(const std::string& key,bool ok){
  if(preferences_form_){preferences_form_->form->preference_saved(key,ok);return;}
  if(ok)preference_text_dirty_.erase(key);
}
bool WorkspaceShell::SyncPreferenceText(const std::string& key,bool present,bool editable,bool composing,bool profile_changed){
  if(preferences_form_)return preferences_form_->form->preference_sync_text(key,present,editable,composing,profile_changed);
  const bool replace=present&&((!preference_text_dirty_.contains(key)&&!composing)||!editable||profile_changed);
  if(replace)preference_text_dirty_.erase(key);return replace;
}
void WorkspaceShell::ResetPreferenceRuleLimit(){
  if(preferences_form_){preferences_form_->form->preference_reset_rules();return;}
  site_rules_limit_=50;
}
void WorkspaceShell::MorePreferenceRules(){
  if(preferences_form_){preferences_form_->form->preference_more_rules();return;}
  site_rules_limit_+=50;
}
size_t WorkspaceShell::PreferenceRuleLimit() const{
  if(preferences_form_)return preferences_form_->form->preference_rule_limit();
  return site_rules_limit_;
}
void WorkspaceShell::SaveTextPreference(std::string key) {
  auto it = preference_textfields_.find(key);
  if (it == preference_textfields_.end()) return;
  const bool enabled=it->second->view()->GetEnabledInViewsSubtree();
  const bool composing=it->second->IsIMEComposing();
  if(preferences_form_){if(!preferences_form_->form->preference_can_save(enabled,composing))return;}
  const bool ok = callbacks_.write_text_preference && callbacks_.write_text_preference.Run(key, base::UTF16ToUTF8(it->second->GetText()));
  PreferenceTextSaved(key,ok);
  RefreshGeneralControls();
  ReportPreferenceOutcome(0, key, ok, ok ? T(u"Zapisano. Witryna może wymagać odświeżenia, aby zastosować zmianę.") :
      key == "accept_languages" ? T(u"Podaj poprawne kody języków oddzielone przecinkami, np. pl, en-US, en.") :
      key == "dns_templates" ? T(u"Podaj poprawny szablon DNS-over-HTTPS. W trybie ścisłym adres nie może być pusty.") :
      T(u"Nie udało się zapisać. Podaj nazwę rodziny czcionki (do 128 znaków). Ustawienie może być zarządzane."));
}
void WorkspaceShell::SaveSitePreference() {
  if(!site_origin_||site_origin_->IsIMEComposing())return;
  const auto type = PickerIndex(site_type_).value_or(kSitePreferenceKeys.size());
  const auto policy = PickerIndex(site_policy_).value_or(0);
  if (type >= kSitePreferenceKeys.size()) return;
  const bool ok = callbacks_.write_site_preference && callbacks_.write_site_preference.Run(
      base::UTF16ToUTF8(site_origin_->GetText()), kSitePreferenceKeys[type], static_cast<int>(policy));
  if (ok) { RefreshSitePreferences(); FilterPreferences(); }
  ReportPreferenceOutcome(1, "", ok, ok ? T(u"Zapisano wyjątek w tym profilu. Odśwież witrynę, aby zastosować zmianę.") :
      T(u"Nie zapisano wyjątku. Podaj adres HTTP(S) bez ścieżki i danych logowania. Sprawdź wybraną politykę; reguły zarządzane nie mogą być zmieniane."));
}
void WorkspaceShell::PostRemoveSiteRule(SitePreferenceRule rule,std::string profile_id){
  // Retain the click's exact backend value and profile through the deferred task.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,SitePreferenceRule rule,std::string profile_id){if(!shell||shell->state_.profile_id!=profile_id)return;const bool ok=shell->callbacks_.remove_site_preference&&shell->callbacks_.remove_site_preference.Run(std::move(rule));shell->RefreshSitePreferences();shell->FilterPreferences();shell->ReportPreferenceOutcome(2,"",ok,ok?T(u"Usunięto wyjątek. Witryna wróci do ustawień domyślnych."):T(u"Nie udało się usunąć wyjątku. Reguła może być zarządzana."));},weak_factory_.GetWeakPtr(),std::move(rule),std::move(profile_id)));
}
void WorkspaceShell::RefreshSitePreferences(bool reload) {
  if(!site_rules_list_)return;
  if(reload)site_preference_rules_=callbacks_.read_site_preferences?callbacks_.read_site_preferences.Run():std::vector<SitePreferenceRule>();
  const auto query=base::i18n::ToLower(base::TrimWhitespace(preferences_search_->GetText(),base::TRIM_ALL));
  std::u16string keywords=T(u"uprawnienia stron zapisane wyjątki witryny reguły domyślne zarządzane");const bool show_all=query.empty()||keywords.find(query)!=std::u16string::npos;
  struct Projection {size_t source;std::u16string detail;std::string key;};std::vector<Projection> visible;size_t matching=0;
  [[maybe_unused]] std::map<std::string,size_t> occurrences;
  for(size_t source=0;source<site_preference_rules_.size();++source){const auto& rule=site_preference_rules_[source];auto it=std::find(kSitePreferenceKeys.begin(),kSitePreferenceKeys.end(),rule.key);const auto name=it==kSitePreferenceKeys.end()?base::UTF8ToUTF16(rule.key):T(kSitePreferenceNames[it-kSitePreferenceKeys.begin()]);const auto policy=rule.setting==1?T(u"Zezwalaj"):rule.setting==2?T(u"Blokuj"):rule.setting==3?T(u"Pytaj"):T(u"Inna polityka");
    auto detail=base::UTF8ToUTF16(rule.primary)+u" · "+name+u" · "+policy;if(rule.secondary!="*")detail+=u" · "+TF(u"Osadzenie: {}",{base::UTF8ToUTF16(rule.secondary)});if(!rule.editable)detail+=u" · "+T(u"Zarządzane");
    if(preferences_form_)detail=base::UTF8ToUTF16(std::string(sidebar::site_rule_detail(rule.primary,base::UTF16ToUTF8(name),rule.setting,rule.secondary,rule.editable)));
    std::string key;
    if(workspace_form_){base::ListValue identity;identity.Append(state_.profile_id);identity.Append(rule.key);identity.Append(rule.primary);identity.Append(rule.secondary);std::string target;CHECK(base::JSONWriter::Write(identity,&target));identity.Append(base::NumberToString(occurrences[target]++));CHECK(base::JSONWriter::Write(identity,&key));}
    const auto searchable=base::i18n::ToLower(detail);keywords+=u" "+searchable;if(!show_all&&searchable.find(query)==std::u16string::npos)continue;++matching;if(visible.size()<PreferenceRuleLimit())visible.push_back({source,std::move(detail),std::move(key)});
  }
  if(workspace_form_){
    if(!site_rule_node_graph_)site_rule_node_graph_=std::make_shared<SidebarNodeGraphState>();rust::Vec<sidebar::SiteRuleSceneInput> items;items.reserve(visible.size());for(const auto& row:visible){rust::Vec<uint16_t> detail;for(char16_t unit:row.detail)detail.push_back(unit);items.push_back({row.key,std::move(detail),site_preference_rules_[row.source].editable,row.source});}
    CHECK(site_rule_node_graph_->graph->reconcile_site_rules(std::move(items)).admitted)<<"Invalid site-rule scene projection";const auto scene=site_rule_node_graph_->graph->site_rule_scene();std::set<uint64_t> wanted;for(const auto& child:scene.children){CHECK_EQ(child.parent,scene.root);wanted.insert(child.id);}
    std::map<uint64_t,SiteRuleSceneRow*> retained;std::vector<views::View*> removed;for(const auto& child:site_rules_list_->children()){auto* row=views::AsViewClass<SiteRuleSceneRow>(child.get());const auto id=row?row->NodeId():0;if(id&&wanted.contains(id)&&row->BelongsTo(*site_rule_node_graph_))retained.emplace(id,row);else removed.push_back(child.get());}for(auto* child:removed)site_rules_list_->RemoveChildViewT(child);
    for(size_t i=0;i<scene.children.size();++i){const auto& child=scene.children[i];auto found=retained.find(child.id);SiteRuleSceneRow* row=nullptr;
      if(found!=retained.end())row=found->second;
      else{row=site_rules_list_->AddChildView(std::make_unique<SiteRuleSceneRow>(site_rule_node_graph_->graph->acquire({13,0,0,std::string(child.key)})));auto* label=AddPreferenceCopy(row,std::u16string(child.detail.begin(),child.detail.end()),kSubtle,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);SetSidebarPanelLayout(row,false,gfx::Insets::VH(4,0),12,label,true);
        auto callback=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::weak_ptr<SidebarNodeGraphState> owner,base::WeakPtr<SiteRuleSceneRow> row,std::string key,uint64_t id,std::string profile){const auto scope=owner.lock();if(!shell||!scope||!row||shell->site_rule_node_graph_!=scope||shell->state_.profile_id!=profile||!shell->general_widget_||!shell->general_widget_->IsVisible()||row->parent()!=shell->site_rules_list_||row->NodeId()!=id||!row->BelongsTo(*scope))return;const auto target=scope->graph->site_rule_target(key,id);if(target.valid&&target.index<shell->site_preference_rules_.size())shell->PostRemoveSiteRule(shell->site_preference_rules_[target.index],std::move(profile));},weak_factory_.GetWeakPtr(),std::weak_ptr<SidebarNodeGraphState>(site_rule_node_graph_),row->GetWeakPtr(),std::string(child.key),child.id,state_.profile_id);
        auto* remove=PreferenceAction(row,std::move(callback),T(u"Usuń"));row->SetParts(label,remove);
      }row->Project(child);site_rules_list_->ReorderChildView(row,i);
    }
  }else
  {site_rules_list_->RemoveAllChildViews();for(const auto& projection:visible){const auto& rule=site_preference_rules_[projection.source];auto* row=site_rules_list_->AddChildView(std::make_unique<views::View>());auto* label=AddPreferenceCopy(row,projection.detail,kSubtle,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);SetSidebarPanelLayout(row,false,gfx::Insets::VH(4,0),12,label,true);auto* remove=PreferenceAction(row,base::BindRepeating(&WorkspaceShell::PostRemoveSiteRule,weak_factory_.GetWeakPtr(),rule,state_.profile_id),T(u"Usuń"));remove->SetEnabled(rule.editable);remove->GetViewAccessibility().SetName(TF(u"Usuń wyjątek: {}",{projection.detail}));}}
  if(!matching)AddPreferenceCopy(site_rules_list_,
      preferences_form_?base::UTF8ToUTF16(std::string(sidebar::site_rules_empty(!site_preference_rules_.empty()))):
      std::u16string(site_preference_rules_.empty()?T(u"Nie masz jeszcze zapisanych wyjątków."):T(u"Brak wyjątków pasujących do wyszukiwania.")),kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);
  else if(matching>visible.size()){
    if(workspace_form_){BindFormCommand(PreferenceAction(site_rules_list_,{},T(u"Pokaż kolejne wyjątki…"),true),sidebar::FormCommand::PreferenceMore);}
  }
  for(auto& row:preference_sections_[3].rows)if(row.view==site_rules_card_)row.keywords=keywords;site_rules_list_->InvalidateLayout();
}

void WorkspaceShell::WritePreference(std::string key, int value) {
  const bool ok = callbacks_.write_preference && callbacks_.write_preference.Run(key, value);
  RefreshGeneralControls();
  ReportPreferenceOutcome(3, key, ok, ok ? (key == "hardware_acceleration" ? T(u"Zapisano. Uruchom ponownie Zitter, aby zmienić akcelerację sprzętową.") : T(u"Zapisano. Jeśli strona nie zastosowała zmiany, odśwież ją.")) :
      key == "dns_mode" ? T(u"Nie zapisano trybu DNS. Tryb ścisły wymaga poprawnego adresu DNS-over-HTTPS; ustawienie może też być zarządzane.") :
      T(u"Nie udało się zmienić ustawienia. Profil może być zarządzany lub niedostępny."));
}
void WorkspaceShell::OpenGeneralSettings() {
  if (!widget_ || !widget_->IsVisible()) return;
  CloseOverlays(true);
  RefreshGeneralControls(); RefreshSitePreferences(); FilterPreferences(); PositionPanels();
  general_widget_->Show(); general_widget_->Activate(); preferences_search_->RequestFocus();
}
void WorkspaceShell::SetThemeMode(ThemeMode mode) {
  if (mode < ThemeMode::kSystem || mode > ThemeMode::kDark) return;
  theme_mode_ = mode; general_dirty_.insert("theme");
  ApplyAppearance(); SaveGeneralPatch(mode, std::nullopt, std::nullopt);
}
void WorkspaceShell::SetSearchEngine(SearchEngine engine) {
  if (engine < SearchEngine::kDuckDuckGo || engine > SearchEngine::kBing) return;
  search_engine_ = engine;
  tools_.default_keyword.clear(); engines_dirty_ = true; SaveTools(); general_dirty_.insert("search");
  RefreshGeneralControls();
  if (address_visible()) RebuildSuggestions();
  SaveGeneralPatch(std::nullopt, engine, std::nullopt);
}
void WorkspaceShell::SetWorkspaceAnimations(bool enabled) {
  workspace_animations_ = enabled; general_dirty_.insert("animations");
  if (!enabled) StopWorkspaceAnimation();
  RefreshGeneralControls(); SaveGeneralPatch(std::nullopt, std::nullopt, enabled);
}
void WorkspaceShell::SetWorkspaceNames(bool shown) {
  workspace_names_ = shown; general_dirty_.insert("workspace_names");
  RebuildWorkspaceDots(); ScheduleMetalSidebar();
  RefreshGeneralControls(); SaveGeneralPatch(std::nullopt, std::nullopt, std::nullopt, shown);
}
// The language is fixed for a run (its text is built once); the choice is
// stored for the next start.
void WorkspaceShell::SetUiLanguageChoice(size_t index) {
  if (index > 2) return;
  StoreUiLanguage(index == 1 ? "pl" : index == 2 ? "en" : "system");
  ReportPreferenceOutcome(4, "", true, T(u"Język zmieni się po ponownym uruchomieniu Zitter."));
}
void WorkspaceShell::SetPinnedFoldersCollapsed(bool collapsed) {
  ApplyPinnedFoldersCollapsed(collapsed);
  RefreshGeneralControls(); SaveGeneralPatch(std::nullopt, std::nullopt, std::nullopt, std::nullopt, collapsed);
}
void WorkspaceShell::ApplyPinnedFoldersCollapsed(bool collapsed) {
  folders_collapsed_ = collapsed; general_dirty_.insert("folders_collapsed");
  sidebar_tree_.SetDefaultCollapsed(collapsed); revealed_for_pin_.clear();
  // Every cached pin tree re-plans with the new default on its next build.
  for (auto& [id, cache] : tab_lists_) cache.pins.clear();
  if (tab_list_) { RebuildTabs("", false); ScheduleMetalSidebar(); }
}
void WorkspaceShell::SaveGeneralPatch(std::optional<ThemeMode> theme, std::optional<SearchEngine> search,
    std::optional<bool> animations, std::optional<bool> workspace_names, std::optional<bool> folders_collapsed) {
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, std::nullopt, theme, search, animations, std::nullopt, std::nullopt, workspace_names, folders_collapsed),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        shell->ReportPreferenceOutcome(4, "", ok, ok ? T(u"Przełączniki zapisują się automatycznie. Pola tekstowe zatwierdź przyciskiem Zapisz lub Enter.") : T(u"Nie udało się zapisać ustawień. Spróbuj ponownie."));
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceShell::ApplyAppearance(bool refresh_theme) {
  ScheduleMetalHeader();ScheduleMetalSplit();
  if (!widget_) return;
  const auto color = workspace_color(state_.workspace_id);
  // Every role of the palette follows the workspace color, so a new color
  // re-applies the theme, not only the frame.
  const bool repaint = !sidebar::design_same_workspace_color(WindowWorkspaceColor(root_), color);
  LastWorkspaceColor() = color;
  for (const auto* window : {widget_.get(), sidebar_widget_.get(), address_widget_.get()})
    if (window) WindowWorkspaceColors()[window->GetTopLevelWidget()] = color;
  if (refresh_theme) ApplyTheme();
  else if (repaint) ApplyTheme(/*refresh_controls=*/false);
  if (repaint && sidebar_root_) sidebar_root_->SchedulePaint();
  root_->SetBackground(views::CreateSolidBackground(FrameVoid(color, root_)));
  const auto tint = FrameTint(color, root_);
  root_->SchedulePaint();
  if (pinned_) {
    sidebar_root_->SetBackground(views::CreateSolidBackground(tint));
    sidebar_root_->SetBorder(nullptr);
  } else {
    sidebar_root_->SetBackground(views::CreateRoundedRectBackground(Mix(tint, Tone(root_, kCard), 70), 12));
    sidebar_root_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, 12, Hairline(root_)), gfx::Insets(6)));
  }
  static_cast<EdgeView*>(edge_.get())->color = Palette(root_).accent;
  edge_->SchedulePaint();
  size_t color_index=0;
  for (auto& child : color_presets_->children())
    if (auto* swatch=child.get();IsGlyphAction(swatch)) SetActionOn(swatch,sidebar::appearance_selected(false,color_index++,color,{}));
  for(auto& wheel:color_wheel_)if(auto* control=views::AsViewClass<ColorWheelControl>(wheel.get()))control->Refresh();
  if(settings_icon_)settings_icon_->SchedulePaint();
  const auto emoji = workspace_emoji(state_.workspace_id);
  if (!emoji_input_->HasFocus()) emoji_input_->SetText(emoji);
  size_t emoji_index=0;
  for (auto& child : emoji_input_->view()->parent()->children()) {
    if (auto* pick=child.get();IsGlyphAction(pick)) {
      if(workspace_form_){SetActionOn(pick,sidebar::appearance_selected(true,emoji_index++,color,base::UTF16ToUTF8(emoji)));continue;}
      SetActionOn(pick,!emoji.empty()&&pick->GetTooltipText()==TF(u"Emoji {}",{emoji}));
    }
  }
  auto* pin = pin_button_.get();
  SetActionOn(pin,pinned_);
  pin->SetTooltipText(pinned_ ? T(u"Odepnij panel (auto-ukrywanie)") : T(u"Przypnij panel"));
  static_cast<ShellRoot*>(root_.get())->pinned = pinned_;
  static_cast<ShellRoot*>(root_.get())->sidebar_width = sidebar_width_;
  root_->InvalidateLayout();
  // Workspace color changes do not resize the frame or hidden settings panels.
  if (refresh_theme) PositionPanels();
}

namespace {
// Moving an NSWindow is costly even when the frame is unchanged.
void SetWidgetBounds(views::Widget* widget, const gfx::Rect& bounds) {
  if (widget->GetWindowBoundsInScreen() != bounds) widget->SetBounds(bounds);
}
}  // namespace
void WorkspaceShell::TremorAddress(){if(url_pill_)TremorRow(url_pill_);}
void WorkspaceShell::ShowToast(std::u16string lead,std::u16string noun,base::OnceClosure undo,std::u16string action){
  if(!toast_widget_)return;
  auto* toast=static_cast<ToastView*>(toast_.get());toast->SetMessage(std::move(lead),std::move(noun));toast->SetAction(action.empty()?T(u"Cofnij"):std::move(action));toast_undo_=std::move(undo);
  PositionToast();toast_widget_->ShowInactive();
  toast_timer_.Start(FROM_HERE,base::Seconds(6),base::BindOnce(&WorkspaceShell::HideToast,weak_factory_.GetWeakPtr()));
  // It lands with one tremor (`zt-tremor`: 1 px shakes in four steps over 180 ms).
  toast_tremor_step_=0;if(!gfx::Animation::PrefersReducedMotion())toast_tremor_timer_.Start(FROM_HERE,base::Milliseconds(45),base::BindRepeating(&WorkspaceShell::TremorToast,base::Unretained(this)));
}
void WorkspaceShell::TremorToast(){
  static constexpr std::array<int,4> kOffsets={-1,1,-1,0};
  PositionToast();if(!toast_widget_||toast_tremor_step_>=static_cast<int>(kOffsets.size())){toast_tremor_timer_.Stop();return;}
  auto bounds=toast_widget_->GetWindowBoundsInScreen();bounds.Offset(kOffsets[toast_tremor_step_++],0);toast_widget_->SetBounds(bounds);
}
void WorkspaceShell::HideToast(){toast_timer_.Stop();toast_tremor_timer_.Stop();toast_undo_.Reset();if(toast_widget_)toast_widget_->Hide();}
void WorkspaceShell::UndoToast(){auto undo=std::move(toast_undo_);HideToast();if(undo)std::move(undo).Run();}
std::u16string WorkspaceShell::toast_text_for_testing() const{return toast_widget_&&toast_widget_->IsVisible()?static_cast<ToastView*>(toast_.get())->text():std::u16string();}
// Bottom-centre of the page area, 20 pt above the window edge.
void WorkspaceShell::PositionToast(){
  if(!toast_widget_)return;
  const auto bounds=widget_->GetClientAreaBoundsInScreen();
  const int left=pinned_&&sidebar_visible()?sidebar_widget_->GetWindowBoundsInScreen().right():bounds.x();
  const auto size=toast_->GetPreferredSize();const int width=std::min(size.width(),std::max(200,bounds.right()-left-24));
  SetWidgetBounds(toast_widget_.get(),gfx::Rect((left+bounds.right()-width)/2,bounds.bottom()-20-size.height(),width,size.height()));
}
void WorkspaceShell::PositionPanels() {
  if (!sidebar_widget_ || !address_widget_ || !settings_widget_) return;
  const auto bounds = widget_->GetClientAreaBoundsInScreen();
  if (tools_widget_) {
    const int w = std::max(300, std::min(660, bounds.width() - 24));
    const int h = std::max(240, std::min(680, bounds.height() - 24));
    SetWidgetBounds(tools_widget_.get(),gfx::Rect(bounds.CenterPoint().x() - w / 2, bounds.CenterPoint().y() - h / 2, w, h));
  }
  const int width = SidebarColumn(sidebar_width_, bounds.width());
  auto sidebar_bounds=gfx::Rect(bounds.x()+kGap,bounds.y()+kShellHeaderHeight,width,std::max(200,bounds.height()-kShellHeaderHeight-kGap));
  if(metal_sidebar_){const auto g=sidebar::shell_layout(bounds.width(),bounds.height(),sidebar_width_,pinned_);sidebar_bounds=gfx::Rect(bounds.x()+g.sidebar.x,bounds.y()+g.sidebar.y,g.sidebar.width,g.sidebar.height);}
  SetWidgetBounds(sidebar_widget_.get(),sidebar_bounds);
  if(scrim_widget_)SetWidgetBounds(scrim_widget_.get(),bounds);
  if(toast_widget_&&toast_widget_->IsVisible())PositionToast();
  if (general_widget_) {
    const int panel_width = std::max(320, std::min(960, bounds.width() - 24));
    const int panel_height = std::max(240, std::min(760, bounds.height() - 24));
    const bool sidebar = panel_width >= 760 && panel_height >= 720;
    preferences_navigation_->SetVisible(sidebar);
    preferences_category_picker_->SetVisible(!sidebar);
    SetWidgetBounds(general_widget_.get(),gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 8, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  const int settings_width = std::min(440, bounds.width() - 24);
  const int settings_height = std::min(settings_root_->GetHeightForWidth(settings_width), std::max(120, bounds.height() - 24));
  SetWidgetBounds(settings_widget_.get(),gfx::Rect(bounds.CenterPoint().x() - settings_width / 2,
      std::max(bounds.y() + 12, bounds.CenterPoint().y() - settings_height / 2),
      settings_width, settings_height));
  if (profiles_widget_) {
    const int panel_width = std::min(420, bounds.width() - 24);
    const int panel_height = profiles_root_->GetHeightForWidth(panel_width);
    SetWidgetBounds(profiles_widget_.get(),gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 12, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  if (import_widget_) {
    const int panel_width = std::min(520, bounds.width() - 24);
    const int panel_height = std::min(import_root_->GetHeightForWidth(panel_width), bounds.height() - 24);
    SetWidgetBounds(import_widget_.get(),gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 12, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  if (peek_widget_) {
    auto area = content_host_->GetBoundsInScreen();
    if (area.width() < 600) {
      area = bounds;
      area.Inset(gfx::Insets::TLBR(kShellHeaderHeight, 8, 8, 8));
    }
    const int margin = std::clamp(area.width() / 16, 16, 64);
    SetWidgetBounds(peek_widget_.get(),gfx::Rect(area.x() + margin, area.y() + 20,
        std::max(240, area.width() - 2 * margin), std::max(280, area.height() - 40)));
  }
  int palette_width=std::min(640,std::max(120,bounds.width()-48));
  if(metal_palette_)palette_width=sidebar::palette_width(bounds.width());
  const auto content = content_host_->GetBoundsInScreen();
  int results_height=std::max(90,std::min(300,content.height()-230));
  if(metal_palette_)results_height=sidebar::palette_results_height(content.height());
  suggestion_scroll_->SetHeightRange(0,results_height);
  const int height = palette_root_->GetHeightForWidth(palette_width);
  auto palette_bounds=gfx::Rect(content.CenterPoint().x()-palette_width/2,content.y()+std::min(132,content.height()/6),palette_width,height);
  if(metal_palette_){const auto r=sidebar::palette_frame({content.x(),content.y(),content.width(),content.height()},palette_width,height);palette_bounds=gfx::Rect(r.x,r.y,r.width,r.height);}
  SetWidgetBounds(address_widget_.get(),palette_bounds);
}

std::string WorkspaceShell::ShortcutFor(const ui::Accelerator& accelerator) const {
  for (const auto& shortcut : tools_.shortcuts) if (shortcut.accelerator.key_code() != ui::VKEY_UNKNOWN && shortcut.accelerator == accelerator) return shortcut.id;
#if BUILDFLAG(IS_MAC)
  constexpr int primary = ui::EF_COMMAND_DOWN;
#else
  constexpr int primary = ui::EF_CONTROL_DOWN;
#endif
  if (accelerator.modifiers() == (primary | ui::EF_ALT_DOWN) && (accelerator.key_code() == ui::VKEY_1 || accelerator.key_code() == ui::VKEY_2)) {
    const auto* id = accelerator.key_code() == ui::VKEY_1 ? "space1" : "space2";
    const auto defaults = ArcShortcuts();
    auto current = std::find_if(tools_.shortcuts.begin(), tools_.shortcuts.end(), [&](const auto& item) { return item.id == id; });
    auto original = std::find_if(defaults.begin(), defaults.end(), [&](const auto& item) { return item.id == id; });
    if (current != tools_.shortcuts.end() && current->accelerator == original->accelerator) return id;
  }
  return {};
}
void WorkspaceShell::RefreshKeyRegistrations() {
  if (!accelerator_target_ || !widget_) return;
  if (new_tab_button_) for (const auto& shortcut : tools_.shortcuts) if (shortcut.id == "new_tab")
    SetRowDetail(new_tab_button_.get(),base::UTF8ToUTF16(DisplayShortcut(shortcut.accelerator)));
#if BUILDFLAG(IS_MAC)
  UpdateWorkspaceMenuShortcuts(tools_.shortcuts);
#endif
  for (auto* panel : {widget_.get(), sidebar_widget_.get(), address_widget_.get(), settings_widget_.get(), profiles_widget_.get(), import_widget_.get(), peek_widget_.get(), general_widget_.get(), tools_widget_.get()}) {
    if (!panel) continue;
    panel->GetFocusManager()->UnregisterAccelerators(accelerator_target_);
    RegisterKeys(panel, accelerator_target_, tools_, panel != widget_, false);
    if (ToolsRecording() && panel == tools_widget_.get()) {
      for (int code = 1; code < 256; ++code) for (int bits = 1; bits < 16; ++bits) {
        const int mods = ((bits & 1) ? ui::EF_COMMAND_DOWN : 0) | ((bits & 2) ? ui::EF_CONTROL_DOWN : 0) |
            ((bits & 4) ? ui::EF_ALT_DOWN : 0) | ((bits & 8) ? ui::EF_SHIFT_DOWN : 0);
        ui::Accelerator chord(static_cast<ui::KeyboardCode>(code), mods);
        if (ValidShortcut(chord) && ShortcutFor(chord).empty())
          panel->GetFocusManager()->RegisterAccelerator(chord, ui::AcceleratorManager::kHighPriority, accelerator_target_);
      }
    }
  }
}
bool WorkspaceShell::SetShortcut(std::string id, std::string chord) {
  auto parsed = ParseShortcut(chord);
  if (!parsed) return false;
  auto found = std::find_if(tools_.shortcuts.begin(), tools_.shortcuts.end(), [&](const auto& item) { return item.id == id; });
  if (found == tools_.shortcuts.end()) return false;
  if (parsed->key_code() != ui::VKEY_UNKNOWN) for (const auto& item : tools_.shortcuts)
    if (item.id != id && item.accelerator == *parsed) return false;
  found->accelerator = *parsed; keys_dirty_ = true;
  RefreshKeyRegistrations(); SaveTools(); return true;
}
bool WorkspaceShell::CaptureShortcut(const ui::Accelerator& accelerator) {
  if (!ToolsRecording() || !tools_widget_ || !tools_widget_->IsVisible()) return false;
  if (tools_form_) {
    const auto capture = accelerator.key_code() == ui::VKEY_ESCAPE ? sidebar::ToolsCapture::Escape :
        !ValidShortcut(accelerator) || accelerator.key_code() == ui::VKEY_UNKNOWN ? sidebar::ToolsCapture::Invalid : sidebar::ToolsCapture::Valid;
    const auto result = tools_form_->form->tools_capture(capture);
    if (!result.admitted) return false;
    if (result.accept_chord && shortcut_chord_) shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(accelerator)));
    if (result.stop_recording) {
      SetShortcutRecording(false);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&WorkspaceShell::RefreshKeyRegistrations, weak_factory_.GetWeakPtr()));
    }
    SetPanelText(tools_status_, base::UTF8ToUTF16(std::string(result.status))); return true;
  }
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    SetShortcutRecording(false);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&WorkspaceShell::RefreshKeyRegistrations, weak_factory_.GetWeakPtr()));
    SetPanelText(tools_status_,T(u"Nagrywanie anulowane.")); return true;
  }
  if (!ValidShortcut(accelerator) || accelerator.key_code() == ui::VKEY_UNKNOWN) {
    SetPanelText(tools_status_,T(u"Użyj Cmd / Ctrl / Alt i klawisza. Skróty edycji tekstu oraz systemowe są zarezerwowane.")); return true;
  }
  shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(accelerator)));
  SetShortcutRecording(false);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&WorkspaceShell::RefreshKeyRegistrations, weak_factory_.GetWeakPtr()));
  SetPanelText(tools_status_,T(u"Kombinacja przechwycona. Wybierz Zapisz skrót.")); return true;
}
bool WorkspaceShell::UpsertSearchProvider(SearchProvider provider, std::string previous) {
  if (!ValidSearchProvider(provider)) return false;
  for (const auto& item : tools_.providers) if (item.keyword == provider.keyword && item.keyword != previous) return false;
  auto found = std::find_if(tools_.providers.begin(), tools_.providers.end(), [&](const auto& item) { return item.keyword == previous; });
  if (found != tools_.providers.end()) { if (tools_.default_keyword == previous) tools_.default_keyword = provider.keyword; *found = std::move(provider); }
  else { if (!previous.empty() || tools_.providers.size() >= 50) return false; tools_.providers.push_back(std::move(provider)); }
  engines_dirty_ = true; RefreshGeneralControls(); SaveTools(); return true;
}
bool WorkspaceShell::RemoveSearchProvider(std::string keyword) {
  if (tools_.providers.size() <= 1) return false;
  auto found = std::find_if(tools_.providers.begin(), tools_.providers.end(), [&](const auto& item) { return item.keyword == keyword; });
  if (found == tools_.providers.end()) return false;
  const std::string implicit = search_engine_ == SearchEngine::kGoogle ? "g" : search_engine_ == SearchEngine::kBing ? "b" : "d";
  tools_.providers.erase(found);
  if (tools_.default_keyword == keyword || (tools_.default_keyword.empty() && keyword == implicit)) tools_.default_keyword = tools_.providers.front().keyword;
  engines_dirty_ = true; RefreshGeneralControls(); SaveTools(); return true;
}
bool WorkspaceShell::SetDefaultSearchProvider(std::string keyword) {
  if (std::none_of(tools_.providers.begin(), tools_.providers.end(), [&](const auto& item) { return item.keyword == keyword; })) return false;
  tools_.default_keyword = std::move(keyword); engines_dirty_ = true; RefreshGeneralControls(); SaveTools(); return true;
}
GURL WorkspaceShell::ResolveInput(std::string text, std::u16string* provider_name) const {
  if (provider_name) provider_name->clear();
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  const auto separator = text.find_first_of(" \t");
  if (separator != std::string::npos) {
    const auto keyword = base::ToLowerASCII(text.substr(0, separator));
    const std::string query(base::TrimWhitespaceASCII(std::string_view(text).substr(separator + 1), base::TRIM_ALL));
    if (!query.empty()) for (const auto& provider : tools_.providers) if (provider.keyword == keyword) {
      if (provider_name) *provider_name = base::UTF8ToUTF16(provider.name);
      return SearchDestination(provider, query);
    }
  }
  const GURL resolved = ResolveTyped(text, search_engine_);
  GURL direct(text);
  const bool query = text.find_first_of(" \t") != std::string::npos || (!direct.has_scheme() && text.find('.') == std::string::npos && !text.starts_with("localhost") && !text.starts_with("[::1]"));
  if (query) {
    for (const auto& provider : tools_.providers) if (provider.keyword == tools_.default_keyword) {
      if (provider_name) *provider_name = base::UTF8ToUTF16(provider.name);
      return SearchDestination(provider, text);
    }
    if (provider_name) *provider_name = search_engine_ == SearchEngine::kGoogle ? u"Google" : search_engine_ == SearchEngine::kBing ? u"Bing" : u"DuckDuckGo";
  }
  return resolved;
}
void WorkspaceShell::SaveTools() {
  // One sequenced file contains both sections; startup dirty guards prevent a
  // delayed read from rolling back edits made immediately after launch.
  std::string json; if (!base::JSONWriter::Write(tools_.ToValue(), &json)) return;
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, std::string data, bool keys, bool engines) {
    std::string existing; base::ReadFileToString(path, &existing);
    auto original = base::JSONReader::ReadDict(existing, base::JSON_PARSE_RFC);
    auto patch = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    base::DictValue merged = original ? std::move(*original) : base::DictValue();
    if (keys) merged.Set("shortcuts", patch->FindList("shortcuts")->Clone());
    if (engines) { merged.Set("providers", patch->FindList("providers")->Clone()); merged.Set("default", *patch->FindString("default")); }
    std::string output;
    return base::JSONWriter::Write(merged, &output) && base::ImportantFileWriter::WriteFileAtomically(path, output);
  }, settings_path_.DirName().AppendASCII("ZitterTools.json"), std::move(json), keys_dirty_, engines_dirty_), base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
    if (!shell) return;
    --shell->pending_writes_; if (!ok && shell->tools_status_) SetPanelText(shell->tools_status_,shell->ToolsStatusText(2));
  }, weak_factory_.GetWeakPtr()));
}
int WorkspaceShell::ToolsPage() const {
  return tools_form_?static_cast<int>(tools_form_->form->tools_page()):0;
}
std::u16string WorkspaceShell::ToolsStatusText(int status) const {
  return base::UTF8ToUTF16(std::string(sidebar::tools_status(sidebar::ToolsStatus{static_cast<uint8_t>(status)})));
}
bool WorkspaceShell::ToolsRecording() const {
  return tools_form_&&tools_form_->form->tools_recording();
}
bool WorkspaceShell::history_loaded() const {
  return tools_form_&&tools_form_->form->tools_history_loaded();
}
std::u16string WorkspaceShell::ToolsHistoryQuery() const {
  if(!tools_form_)return {};
  const auto units=tools_form_->form->tools_history_query();return std::u16string(units.begin(),units.end());
}
std::u16string WorkspaceShell::ToolsTimelineQuery() const {
  if(!tools_form_)return {};
  const auto units=tools_form_->form->tools_timeline_query();return std::u16string(units.begin(),units.end());
}
void WorkspaceShell::ResetToolsForm() {
  if(tools_form_)tools_form_->form->tools_reset();
}
void WorkspaceShell::OpenTools(int page) {
  if (!widget_ || !tools_widget_) return;
  CloseOverlays(true);
  if(tools_form_)tools_form_->form->tools_select(std::clamp(page,0,3));
  RefreshTools(); RefreshKeyRegistrations(); PositionPanels(); ApplyTheme();
  tools_widget_->LayoutRootViewIfNecessary();
  tools_widget_->Show(); tools_widget_->Activate(); tools_widget_->ClearNativeFocus();
  if (history_search_) { history_search_->RequestFocus(); RefreshHistory(); }
  else if (snapshot_name_) snapshot_name_->RequestFocus();
  else if (tools_picker_) tools_picker_->RequestFocus();
}
bool WorkspaceShell::history_visible() const {
  return ToolsPage() == 3 && tools_widget_ && tools_widget_->IsVisible();
}
void WorkspaceShell::RefreshHistory(bool more) {
  history_timer_.Stop();
  if (!history_visible() || !history_search_ || !callbacks_.query_history) return;
  const size_t limit=tools_form_->form->tools_history_request(more);
  history_more_->SetEnabled(false);
  SetPanelText(tools_status_,ToolsStatusText(0));
  callbacks_.query_history.Run(ToolsHistoryQuery(),limit);
}
void WorkspaceShell::SetHistoryResults(std::u16string query, std::string profile_id, size_t limit,
    std::vector<BrowsingHistoryEntry> entries, bool more, bool error) {
  if (!history_visible() || !history_search_ || profile_id != state_.profile_id) return;
  if(!tools_form_||!tools_form_->form->tools_history_accept(rust::Slice<const uint16_t>(reinterpret_cast<const uint16_t*>(query.data()),query.size()),limit))return;
  history_entries_ = std::move(entries);
  sidebar::HistoryScene scene{};std::map<uint64_t,RustRowView*> retained_rows;
  {
    if(!history_node_graph_)history_node_graph_=std::make_shared<SidebarNodeGraphState>();
    auto units=[](const std::u16string& text){rust::Vec<uint16_t> result;for(char16_t unit:text)result.push_back(unit);return result;};
    rust::Vec<sidebar::HistorySceneInput> items;items.reserve(history_entries_.size());std::map<std::pair<std::string,int64_t>,size_t> occurrences;
    for(const auto& entry:history_entries_){
      const auto record=std::make_pair(entry.url.spec(),entry.visited.ToInternalValue());
      // The profile/URL/exact-visit/duplicate key stays opaque UI identity;
      // native profile/query/limit and command admission remain authoritative.
      base::ListValue identity;identity.Append(profile_id);identity.Append(record.first);identity.Append(base::NumberToString(record.second));identity.Append(base::NumberToString(occurrences[record]++));std::string key;CHECK(base::JSONWriter::Write(identity,&key));
      auto url=base::UTF8ToUTF16(entry.url.spec());items.push_back({std::move(key),units(entry.title.empty()?url:entry.title),units(base::TimeFormatShortDateAndTime(entry.visited)+u" · "+url),units(url)});
    }
    CHECK(history_node_graph_->graph->reconcile_history(std::move(items)).admitted)<<"Invalid history scene projection";scene=history_node_graph_->graph->history_scene();CHECK_NE(scene.root,0u);
    std::set<uint64_t> wanted;for(const auto& child:scene.children){CHECK_EQ(child.parent,scene.root);wanted.insert(child.id);}
    std::vector<views::View*> removed;
    for(const auto& child:history_list_->children()){auto* row=views::AsViewClass<RustRowView>(child.get());const auto id=row?row->LogicalNodeId():0;if(id&&wanted.contains(id)&&row->BelongsTo(*history_node_graph_))retained_rows.emplace(id,row);else removed.push_back(child.get());}
    for(auto* child:removed)history_list_->RemoveChildViewT(child);
  }
  std::u16string empty_text,status_text;bool more_visible,more_enabled;
  {const auto view=tools_form_->form->tools_history_view(history_entries_.size(),more,error);empty_text=base::UTF8ToUTF16(std::string(view.empty));status_text=base::UTF8ToUTF16(std::string(view.status));more_visible=view.more_visible;more_enabled=view.more_enabled;}
  if(history_entries_.empty())AddToolsCopy(history_list_,empty_text,kMuted,0,static_cast<int>(gfx::Font::Weight::NORMAL),true);
  [[maybe_unused]] size_t history_node_index=0;
  for (const auto& entry : history_entries_) {
    {const auto& child=scene.children[history_node_index];const auto found=retained_rows.find(child.id);if(found!=retained_rows.end()){auto* adapter=found->second;adapter->ProjectHistory(child);history_list_->ReorderChildView(adapter,history_node_index++);continue;}}
    RowRecipe::Spec row;
    {const auto& child=scene.children[history_node_index];row.title=std::u16string(child.title.begin(),child.title.end());row.detail=std::u16string(child.detail.begin(),child.detail.end());row.height=child.height;}
    // Retained keys keep the same profile/URL callback; avoid rebinding and
    // cancelling input when an identical result batch is projected.
    auto callback=[&]{return base::BindRepeating(
        [](base::WeakPtr<WorkspaceShell> shell, std::string profile_id, GURL url) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
              [](base::WeakPtr<WorkspaceShell> shell, std::string profile_id, GURL url) {
                if (!shell || shell->state_.profile_id != profile_id) return;
                if (shell->callbacks_.navigate.Run(url, true)) { shell->CloseOverlays(); shell->RestorePageFocus(); }
              }, shell, profile_id, url));
        }, weak_factory_.GetWeakPtr(), profile_id, entry.url);};
    views::View* button;
    {
      row.sidebar_row=true;row.history=true;
      row.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->ToolsChromeVisible();},weak_factory_.GetWeakPtr());
      const auto& child=scene.children[history_node_index];auto* adapter=views::AsViewClass<RustRowView>(history_list_->AddChildView(CreateSceneRow(callback(),std::move(row),base::BindRepeating(&WorkspaceShell::ScheduleToolsChrome,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr()),{history_node_graph_,5,{},std::string(child.key)})));
      CHECK(adapter);adapter->ProjectHistory(child);history_list_->ReorderChildView(adapter,history_node_index);button=adapter;
    }
    ++history_node_index;
    if(!tools_form_)
    {button->SetTooltipText(base::UTF8ToUTF16(entry.url.spec()));button->GetViewAccessibility().SetDescription(base::TimeFormatShortDateAndTime(entry.visited)+u" · "+base::UTF8ToUTF16(entry.url.spec()));}
  }
  SetHistoryMoreVisible(more_visible);
  history_more_->SetEnabled(more_enabled);
  SetPanelText(tools_status_,status_text);
  PreparePanelText(tools_widget_->GetRootView());
  history_list_->InvalidateLayout();
  tools_widget_->LayoutRootViewIfNecessary();
  ScheduleToolsChrome();
}
views::View* WorkspaceShell::tools_choice_for_testing(std::string_view key) const{
  if(key=="selection")return tools_picker_;if(key=="closed")return closed_picker_;if(key=="history_period")return history_period_;return nullptr;
}
size_t WorkspaceShell::tools_choice_count_for_testing(std::string_view key) const{auto* picker=tools_choice_for_testing(key);return picker?PickerCount(picker):0;}
std::optional<size_t> WorkspaceShell::tools_choice_selection_for_testing(std::string_view key) const{auto* picker=tools_choice_for_testing(key);return picker?PickerIndex(picker):std::nullopt;}
std::u16string WorkspaceShell::tools_choice_label_for_testing(std::string_view key,size_t index) const{
  auto* picker=tools_choice_for_testing(key);if(!picker||index>=PickerCount(picker))return {};
  auto* scene=views::AsViewClass<SceneSelect>(picker);return scene->LabelAt(index);
}
bool WorkspaceShell::tools_choice_metal_visible_for_testing(std::string_view key) const{
  auto* scene=views::AsViewClass<SceneSelect>(tools_choice_for_testing(key));return scene&&scene->metal_visible();
}
gfx::Rect WorkspaceShell::tools_choice_metal_bounds_for_testing(std::string_view key) const{
  auto* scene=views::AsViewClass<SceneSelect>(tools_choice_for_testing(key));if(scene)return scene->metal_bounds_for_testing();
  return {};
}
void WorkspaceShell::ActivateToolsChoiceForTesting(std::string_view key,size_t index){
  auto* picker=tools_choice_for_testing(key);if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
  auto* scene=views::AsViewClass<SceneSelect>(picker);scene->SelectForTesting(index);
}
bool WorkspaceShell::tools_action_metal_visible_for_testing(views::View* button) const{return preference_action_metal_visible_for_testing(button);}
gfx::Rect WorkspaceShell::tools_action_metal_bounds_for_testing(views::View* button) const{
  auto* group=button?views::AsViewClass<SceneActionGroup>(button->parent()):nullptr;return group?group->metal_bounds_for_testing():gfx::Rect();
}
bool WorkspaceShell::tools_actions_contains_scene_text_for_testing(std::string_view text) const{
  if(!tools_body_)return false;
  auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,tools_body_);
}
size_t WorkspaceShell::tools_action_groups_for_testing() const{
  if(!tools_body_)return 0;
  auto count=[&](auto&& self,views::View* view)->size_t{if(views::AsViewClass<SceneActionGroup>(view))return 1;size_t n=0;for(const auto& child:view->children())n+=self(self,child.get());return n;};return count(count,tools_body_);
}
bool WorkspaceShell::tools_close_metal_visible_for_testing() const{
  auto* group=tools_close_?views::AsViewClass<ScenePreferenceControls>(tools_close_->parent()):nullptr;return group&&group->metal_visible();
}
void WorkspaceShell::SetHistoryMoreVisible(bool visible){
  history_more_->SetVisible(visible);
  if(auto* group=views::AsViewClass<SceneActionGroup>(history_more_->parent()))group->SetVisible(visible);
}
void WorkspaceShell::RefreshToolsViewport(){ScheduleToolsChrome();if(tools_body_){RefreshChoiceViewports(tools_body_);
  SyncSceneRowInput();
}}
void WorkspaceShell::RefreshTimelineItems() {
  if (ToolsPage() != 0 || !tools_picker_) return;
  const auto state = callbacks_.time_machine ? callbacks_.time_machine.Run() : TimeMachineState();
  if(!tools_form_)return;
  // The Rust projection owns filtering, display copy and the key-to-ID map.
  auto fill=[&](const auto& items,views::View* picker,bool closed){
    auto* scene=views::AsViewClass<SceneSelect>(picker);if(!scene)return;
    auto units=[](const std::u16string& text){rust::Vec<uint16_t> value;for(char16_t unit:text)value.push_back(unit);return value;};
    rust::Vec<sidebar::ToolsTimelineItem> metadata;
    for(const auto& item:items)metadata.push_back({item.id,units(item.title),units(item.detail),units(base::i18n::ToLower(item.title+u" "+item.detail))});
    auto projection=tools_form_->form->tools_timeline_project(closed,std::move(metadata));
    std::vector<std::pair<std::string,std::u16string>> choices;choices.reserve(projection.choices.size());
    for(const auto& choice:projection.choices)choices.emplace_back(std::string(choice.key),std::u16string(choice.label.begin(),choice.label.end()));
    scene->SetOptions(choices);scene->SetSelectedIndex(0);scene->SetEnabled(projection.enabled);
  };
  fill(state.snapshots,tools_picker_,false);fill(state.closed,closed_picker_,true);
}
void WorkspaceShell::BuildRustToolsForm(){
  using Kind=sidebar::ToolsNodeKind;using Role=sidebar::ToolsNodeRole;
  CHECK(tools_form_);auto weak=weak_factory_.GetWeakPtr();
  auto units=[](const std::u16string& text){rust::Vec<uint16_t> value;value.reserve(text.size());for(char16_t unit:text)value.push_back(unit);return value;};
  auto text=[](const auto& units){return std::u16string(units.begin(),units.end());};
  std::u16string context;rust::Vec<sidebar::ToolsSleepingCopy> sleeping;
  if(ToolsPage()==0){context=state_.workspace_name;const auto state=callbacks_.time_machine?callbacks_.time_machine.Run():TimeMachineState();for(const auto& item:state.sleeping)sleeping.push_back({units(item.title),units(item.detail)});}
  if(ToolsPage()==3)for(const auto& profile:state_.profiles)if(profile.id==state_.profile_id)context=base::UTF8ToUTF16(profile.name);
  const auto plan=tools_form_->form->tools_form_plan(base::UTF16ToUTF8(context),browsing_data_busy_,std::move(sleeping));SetPanelText(tools_title_,text(plan.title));SetPanelText(tools_status_,text(plan.status));
  struct Host {raw_ptr<views::View> view=nullptr;bool row=false;raw_ptr<views::View> flex=nullptr;int spacing=0;};
  std::vector<Host> hosts{{tools_body_,false,nullptr,0}};bool timeline=false;
  auto form_picker=[&](views::View* parent,std::u16string name,const sidebar::ToolsFormChoices& projection)->views::View*{
    auto* control=parent->AddChildView(MakeParentSceneControl<SceneSelect>(parent,std::vector<std::u16string>(),std::move(name),base::RepeatingClosure()));std::vector<std::pair<std::string,std::u16string>> choices;choices.reserve(projection.choices.size());for(const auto& choice:projection.choices)choices.emplace_back(std::string(choice.key),text(choice.label));control->SetOptions(choices);control->SetSelectedIndex(0);control->SetEnabled(projection.enabled);return control;
  };
  for(const auto& node:plan.nodes){auto* parent=hosts.back().view.get();const auto label=text(node.text);views::View* control=nullptr;
    switch(node.kind){
      case Kind::BeginRow:{auto* row=parent->AddChildView(std::make_unique<views::View>());hosts.push_back({row,true,nullptr,node.spacing});continue;}
      case Kind::EndRow:{CHECK_GT(hosts.size(),1u);auto host=hosts.back();CHECK(host.row);SetSidebarPanelLayout(host.view,false,gfx::Insets(),host.spacing,host.flex);hosts.pop_back();continue;}
      case Kind::BeginActions:hosts.push_back({ActionGroup(parent,true,node.spacing),false,nullptr,node.spacing});continue;
      case Kind::EndActions:CHECK_GT(hosts.size(),1u);CHECK(!hosts.back().row);hosts.pop_back();continue;
      case Kind::Hint:control=AddToolsCopy(parent,label,kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);break;
      case Kind::Heading:control=AddToolsCopy(parent,label,kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);break;
      case Kind::Editor:{
        auto* field=AddEditor(parent);field->GetViewAccessibility().SetName(label);field->SetPlaceholderText(label);field->view()->SetPreferredSize(gfx::Size(200,36));field->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_,kChip));field->SetCallbacks(base::BindRepeating(&WorkspaceShell::ToolsEditorEdited,weak,base::Unretained(field)),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* editor,const ui::KeyEvent& event){return shell&&shell->HandleToolsEditorKey(editor,event);},weak,base::Unretained(field)));control=field->view();
        switch(node.role){case Role::SnapshotName:snapshot_name_=field;break;case Role::TimelineFilter:timeline_filter_=field;break;case Role::ShortcutChord:shortcut_chord_=field;break;case Role::ProviderName:provider_name_=field;break;case Role::ProviderKeyword:provider_keyword_=field;break;case Role::ProviderUrl:provider_url_=field;break;case Role::HistoryQuery:history_search_=field;break;default:CHECK(false);}
        break;
      }
      case Kind::Choice:{
        if(node.role==Role::ShortcutChoice){rust::Vec<sidebar::ToolsShortcutItem> metadata;for(const auto& item:tools_.shortcuts)metadata.push_back({item.id,units(item.title),units(base::UTF8ToUTF16(FormatShortcut(item.accelerator)))});const auto projection=tools_form_->form->tools_shortcut_choices(std::move(metadata));control=form_picker(parent,label,projection);}
        else if(node.role==Role::ProviderChoice){rust::Vec<sidebar::ToolsProviderItem> metadata;for(const auto& item:tools_.providers)metadata.push_back({item.keyword,units(base::UTF8ToUTF16(item.name)),units(base::UTF8ToUTF16(item.keyword)),units(base::UTF8ToUTF16(item.url)),item.keyword==tools_.default_keyword});const auto projection=tools_form_->form->tools_provider_choices(std::move(metadata));control=form_picker(parent,label,projection);}
        else{std::vector<std::u16string> choices;for(const auto& value:node.choices)choices.push_back(base::UTF8ToUTF16(std::string(value)));control=ChoicePicker(parent,label,std::move(choices));SetPickerIndex(control,0);}
        switch(node.role){case Role::SnapshotChoice:tools_picker_=control;timeline=true;break;case Role::ClosedChoice:closed_picker_=control;break;case Role::ShortcutChoice:case Role::ProviderChoice:tools_picker_=control;SetPickerCallback(control,base::BindRepeating(&WorkspaceShell::RefreshToolsChoiceFields,weak));break;case Role::HistoryPeriod:history_period_=control;break;default:CHECK(false);}
        break;
      }
      case Kind::Action:{
        views::View* button;
        if(node.role==Role::HistoryClear){button=PreferenceAction(parent,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell&&shell->history_period_&&shell->callbacks_.clear_browsing_data)shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->history_period_).value_or(0)),true,false,false});},weak),label);history_clear_=button;}
        else if(node.role==Role::HistoryMore){button=PreferenceAction(parent,{},label,true);BindFormCommand(button,sidebar::FormCommand::HistoryMore);history_more_=button;}
        else button=AddAction(parent,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,int action){if(shell)shell->PostToolsAction(action);},weak,node.action),label);
        control=button;break;
      }
      case Kind::Rows:control=parent->AddChildView(std::make_unique<views::View>());control->SetLayoutManager(CreateSidebarColumnLayout(node.spacing));CHECK(node.role==Role::HistoryRows);history_list_=control;break;
      default:CHECK(false);
    }
    CHECK(control);control->SetEnabled(control->GetEnabled()&&node.enabled);if(node.role==Role::HistoryMore)SetHistoryMoreVisible(node.visible);else control->SetVisible(node.visible);if(node.flex)hosts.back().flex=control;
  }
  CHECK_EQ(hosts.size(),1u);RefreshToolsChoiceFields();if(timeline)RefreshTimelineItems();
}
void WorkspaceShell::RefreshTools() {
  ++tools_generation_;
  if(tools_root_)CancelChoiceInteractions(tools_root_);
  SetShortcutRecording(false);
  for(auto* field:{snapshot_name_.get(),timeline_filter_.get(),shortcut_chord_.get(),provider_name_.get(),provider_keyword_.get(),provider_url_.get(),history_search_.get()})
    if(field)field->SetCallbacks({},{});
  ResetToolsForm();
  history_timer_.Stop();
  history_search_ = nullptr; history_period_ = nullptr; history_list_ = nullptr;
  history_more_ = nullptr; history_clear_ = nullptr; history_entries_.clear();history_node_graph_.reset();
  snapshot_name_ = nullptr; timeline_filter_ = nullptr; shortcut_chord_ = nullptr;
  provider_name_ = nullptr; provider_keyword_ = nullptr; provider_url_ = nullptr; tools_picker_ = nullptr; closed_picker_ = nullptr;
  tools_body_->RemoveAllChildViews();
  if(tools_form_){BuildRustToolsForm();PreparePanelText(tools_widget_->GetRootView());tools_body_->InvalidateLayout();if(history_visible())RefreshHistory();return;}
  std::u16string context=ToolsPage()==0?state_.workspace_name:std::u16string();
  if(ToolsPage()==3)for(const auto& profile:state_.profiles)if(profile.id==state_.profile_id)context=base::UTF8ToUTF16(profile.name);
  std::u16string form_title,form_intro,form_tail,form_status;
  std::vector<std::u16string> fields,pickers,headings,actions,form_choices;
  if(workspace_form_){
    auto data=sidebar::tools_copy(ToolsPage(),base::UTF16ToUTF8(context));
    auto strings=[](const auto& input){std::vector<std::u16string> out;for(const auto& value:input)out.push_back(base::UTF8ToUTF16(std::string(value)));return out;};
    form_title=base::UTF8ToUTF16(std::string(data.title));form_intro=base::UTF8ToUTF16(std::string(data.intro));form_tail=base::UTF8ToUTF16(std::string(data.tail));form_status=base::UTF8ToUTF16(std::string(data.initial_status));
    fields=strings(data.fields);pickers=strings(data.pickers);headings=strings(data.headings);actions=strings(data.actions);form_choices=strings(data.choices);
  }
  auto text=[&](std::vector<std::u16string>& data,size_t index,std::u16string_view native){return workspace_form_?std::move(data.at(index)):std::u16string(native);};
  SetPanelText(tools_status_,workspace_form_?form_status:T(u"Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie."));
  auto weak = weak_factory_.GetWeakPtr();
  auto hint = [&](std::u16string text) {return AddToolsCopy(tools_body_,std::move(text),kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);};
  auto input = [&](std::u16string name) {
    auto* field = AddEditor(tools_body_);
    field->GetViewAccessibility().SetName(name);field->SetPlaceholderText(name);
    field->view()->SetPreferredSize(gfx::Size(200,36));
    field->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_,kChip));
    field->SetCallbacks(base::BindRepeating(&WorkspaceShell::ToolsEditorEdited,weak,base::Unretained(field)),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* editor,const ui::KeyEvent& event){
        return shell&&shell->HandleToolsEditorKey(editor,event);
      },weak,base::Unretained(field)));
    return field;
  };
  auto action_parent=[&]()->views::View*{
    if(workspace_form_){
      // Batch adjacent actions while preserving the native body sequence/gaps.
      if(!tools_body_->children().empty())if(auto* group=views::AsViewClass<SceneActionGroup>(tools_body_->children().back()))return group;
      return ActionGroup(tools_body_,true,10);
    }
    return tools_body_;
  };
  auto button = [&](std::u16string_view native, int action) {
    const size_t index=ToolsPage()==0?action-1:ToolsPage()==1?action-10:action-20;
    auto title=text(actions,index,native);
    return AddAction(action_parent(),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, int action) {
      if (shell) shell->PostToolsAction(action);
    }, weak, action),std::move(title));
  };
  auto picker = [&](std::u16string name, std::vector<std::u16string> values, views::View* parent=nullptr)->views::View* {
    if(!parent)parent=tools_body_;
    if(values.empty())values.emplace_back(T(u"Brak pozycji"));
    auto* control=ChoicePicker(parent,std::move(name),std::move(values));SetPickerIndex(control,0);return control;
  };
  auto units=[](const std::u16string& text){rust::Vec<uint16_t> value;value.reserve(text.size());for(char16_t unit:text)value.push_back(unit);return value;};
  auto form_picker=[&](std::u16string name,const sidebar::ToolsFormChoices& projection)->views::View*{
    auto* control=tools_body_->AddChildView(MakeParentSceneControl<SceneSelect>(tools_body_.get(),std::vector<std::u16string>(),std::move(name),base::RepeatingClosure()));
    std::vector<std::pair<std::string,std::u16string>> choices;choices.reserve(projection.choices.size());for(const auto& choice:projection.choices)choices.emplace_back(std::string(choice.key),std::u16string(choice.label.begin(),choice.label.end()));
    control->SetOptions(choices);control->SetSelectedIndex(0);control->SetEnabled(projection.enabled);return control;
  };
  if (ToolsPage() == 0) {
    SetPanelText(tools_title_,workspace_form_?form_title:u"Time Machine · " + state_.workspace_name);
    hint(workspace_form_?form_intro:T(u"Snapshot zapisuje karty, foldery i Split View w tej przestrzeni. Przywrócenie przeładuje strony; formularze i historia Wstecz nie są częścią snapshotu. Poprzedni układ otrzyma punkt przywracania."));
    snapshot_name_ = input(text(fields,0,T(u"Nazwa snapshotu, np. Research przed deployem")));
    button(T(u"Zapisz snapshot"), 1);
    timeline_filter_ = input(text(fields,1,T(u"Szukaj snapshotów i zamkniętych kart")));
    AddToolsCopy(tools_body_,text(headings,0,T(u"Snapshoty · 30 własnych + 30 automatycznych")),kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
    tools_picker_ = picker(text(pickers,0,T(u"Zapisane snapshoty")), {});
    button(T(u"Przywróć wybrany układ"), 2); button(T(u"Usuń wybrany snapshot"), 3);
    AddToolsCopy(tools_body_,text(headings,1,T(u"Ostatnio zamknięte · do 100 kart")),kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
    closed_picker_ = picker(text(pickers,1,T(u"Zamknięte karty")), {});
    button(T(u"Przywróć wybraną kartę"), 4);
    hint(workspace_form_?form_tail:T(u"Uśpienie zwalnia strony tej przestrzeni z pamięci i przełącza do innej. Kliknięcie jej kropki wznowi układ. Niezapisane formularze zostaną utracone."));
    button(T(u"Uśpij tę przestrzeń"), 5);
    const auto timeline = callbacks_.time_machine ? callbacks_.time_machine.Run() : TimeMachineState();
    for (const auto& item : timeline.sleeping) hint(item.title + u" · " + item.detail);
    RefreshTimelineItems();
  } else if (ToolsPage() == 1) {
    SetPanelText(tools_title_,workspace_form_?form_title:T(u"Skróty klawiszowe"));
    hint(workspace_form_?form_intro:T(u"Schemat inspirowany Arc. Wybierz akcję, nagraj kombinację lub wpisz np. Cmd+Shift+J. Puste pole wyłącza skrót. Escape, skróty edycji tekstu i systemowe pozostają zarezerwowane."));
    if(tools_form_){
      rust::Vec<sidebar::ToolsShortcutItem> metadata;for(const auto& shortcut:tools_.shortcuts)metadata.push_back({shortcut.id,units(shortcut.title),units(base::UTF8ToUTF16(FormatShortcut(shortcut.accelerator)))});
      const auto projection=tools_form_->form->tools_shortcut_choices(std::move(metadata));tools_picker_=form_picker(text(pickers,0,T(u"Akcja skrótu")),projection);
      shortcut_chord_=input(text(fields,0,T(u"Kombinacja, np. Cmd+Shift+J")));RefreshToolsChoiceFields();
    }else
    {
    std::vector<std::u16string> choices;
    for (const auto& shortcut : tools_.shortcuts) choices.push_back(shortcut.title + u" — " + base::UTF8ToUTF16(FormatShortcut(shortcut.accelerator)));
    tools_picker_ = picker(text(pickers,0,T(u"Akcja skrótu")), choices);
    shortcut_chord_ = input(text(fields,0,T(u"Kombinacja, np. Cmd+Shift+J")));
    shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(tools_.shortcuts.front().accelerator)));
    }
    SetPickerCallback(tools_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (!shell || !shell->shortcut_chord_) return;
      if(shell->tools_form_){shell->RefreshToolsChoiceFields();return;}
      const size_t i = PickerIndex(shell->tools_picker_).value_or(0);
      if (i < shell->tools_.shortcuts.size()) shell->shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(shell->tools_.shortcuts[i].accelerator)));
      shell->SetShortcutRecording(false);
      shell->RefreshKeyRegistrations();
    }, weak));
    button(T(u"Nagraj kombinację…"), 10); button(T(u"Zapisz skrót"), 11);
    button(T(u"Wyłącz wybrany skrót"), 12); button(T(u"Przywróć schemat Arc"), 13);
  } else if (ToolsPage() == 2) {
    SetPanelText(tools_title_,workspace_form_?form_title:T(u"Wyszukiwarki i prefiksy"));
    hint(workspace_form_?form_intro:T(u"W pasku adresu wpisz prefiks i zapytanie, np. g koty lub w Warszawa. Szablon musi być adresem HTTP(S) z jednym znacznikiem {query}. Zapytania są kodowane automatycznie."));
    if(tools_form_){
      rust::Vec<sidebar::ToolsProviderItem> metadata;for(const auto& provider:tools_.providers)metadata.push_back({provider.keyword,units(base::UTF8ToUTF16(provider.name)),units(base::UTF8ToUTF16(provider.keyword)),units(base::UTF8ToUTF16(provider.url)),provider.keyword==tools_.default_keyword});
      const auto projection=tools_form_->form->tools_provider_choices(std::move(metadata));tools_picker_=form_picker(text(pickers,0,T(u"Wyszukiwarka do edycji")),projection);
      provider_name_=input(text(fields,0,T(u"Nazwa wyszukiwarki")));provider_keyword_=input(text(fields,1,T(u"Prefiks (a–z, 0–9)")));provider_url_=input(text(fields,2,T(u"Adres z {query}")));RefreshToolsChoiceFields();
    }else
    {
    std::vector<std::u16string> provider_choices{workspace_form_?std::move(form_choices.at(0)):T(u"Dodaj nową wyszukiwarkę…")};
    for (const auto& provider : tools_.providers) {const auto choice = base::UTF8ToUTF16(provider.keyword + " · " + provider.name); provider_choices.push_back(provider.keyword == tools_.default_keyword ? TF(u"{} · domyślna", {choice}) : choice);}
    tools_picker_ = picker(text(pickers,0,T(u"Wyszukiwarka do edycji")), provider_choices);
    provider_name_ = input(text(fields,0,T(u"Nazwa wyszukiwarki"))); provider_keyword_ = input(text(fields,1,T(u"Prefiks (a–z, 0–9)"))); provider_url_ = input(text(fields,2,T(u"Adres z {query}")));
    }
    SetPickerCallback(tools_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (!shell || !shell->provider_name_) return;
      if(shell->tools_form_){shell->RefreshToolsChoiceFields();return;}
      const size_t i = PickerIndex(shell->tools_picker_).value_or(0);
      SearchProvider provider;
      if (i > 0 && i <= shell->tools_.providers.size()) provider = shell->tools_.providers[i - 1];
      shell->provider_name_->SetText(base::UTF8ToUTF16(provider.name)); shell->provider_keyword_->SetText(base::UTF8ToUTF16(provider.keyword)); shell->provider_url_->SetText(base::UTF8ToUTF16(provider.url));
    }, weak));
    button(T(u"Zapisz wyszukiwarkę"), 20); button(T(u"Ustaw jako domyślną"), 21); button(T(u"Usuń wyszukiwarkę"), 22);
  }
  if (ToolsPage() == 3) {
    SetPanelText(tools_title_,workspace_form_?form_title:T(u"Historia przeglądania"));
    hint(workspace_form_?form_intro:TF(u"Profil: {}",{context}));
    history_search_ = input(text(fields,0,T(u"Szukaj w historii")));
    auto* clear_row = tools_body_->AddChildView(std::make_unique<views::View>());
    history_period_=picker(text(pickers,0,T(u"Zakres czyszczenia historii")),workspace_form_?std::move(form_choices):std::vector<std::u16string>{T(u"Ostatnia godzina"),T(u"Ostatnie 24 godziny"),T(u"Ostatnie 7 dni"),T(u"Cały okres")},clear_row);
    const auto clear_presentation=ToolsClearControls(browsing_data_busy_);
    history_clear_ = PreferenceAction(clear_row,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (shell && shell->history_period_ && shell->callbacks_.clear_browsing_data)
        shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->history_period_).value_or(0)), true, false, false});
    }, weak), clear_presentation.label);
    SetSidebarPanelLayout(clear_row,false,gfx::Insets(),8,history_period_);
    history_list_ = tools_body_->AddChildView(std::make_unique<views::View>());
    if(tools_form_)history_list_->SetLayoutManager(CreateSidebarColumnLayout(4));
    history_more_ = PreferenceAction(tools_body_,base::BindRepeating(&WorkspaceShell::RefreshHistory, weak, true), text(actions,1,T(u"Pokaż więcej")),true);
    SetHistoryMoreVisible(false);
    SetPanelText(tools_status_,workspace_form_?form_status:T(u"Odczytywanie historii…"));
    history_clear_->SetEnabled(clear_presentation.enabled);
  }
  PreparePanelText(tools_widget_->GetRootView()); tools_body_->InvalidateLayout();
  if (history_visible()) RefreshHistory();
}
void WorkspaceShell::RefreshToolsChoiceFields(){
  if(!tools_form_)return;auto* scene=views::AsViewClass<SceneSelect>(tools_picker_);if(!scene)return;
  const auto draft=tools_form_->form->tools_choice_select(scene->SelectedKey());if(!draft.valid)return;
  auto text=[](const auto& units){return std::u16string(units.begin(),units.end());};
  if(draft.page==1&&shortcut_chord_)shortcut_chord_->SetText(text(draft.chord));
  if(draft.page==2&&provider_name_&&provider_keyword_&&provider_url_){provider_name_->SetText(text(draft.name));provider_keyword_->SetText(text(draft.keyword));provider_url_->SetText(text(draft.url));}
  if(draft.stop_recording){SetShortcutRecording(false);RefreshKeyRegistrations();}
}
void WorkspaceShell::PostToolsAction(int action) {
  std::optional<std::string> choice_key;
  if(tools_form_&&(action==2||action==3||action==4||action==11||action==12||(action>=20&&action<=22))){
    auto* picker=action==4?closed_picker_.get():tools_picker_.get();auto* scene=views::AsViewClass<SceneSelect>(picker);
    choice_key=scene?scene->SelectedKey():std::string();
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell,uint64_t generation,std::string workspace,std::string profile,int action,std::optional<std::string> key){
        if(shell&&shell->tools_generation_==generation&&shell->state_.workspace_id==workspace&&shell->state_.profile_id==profile)
          shell->ToolsAction(action,std::move(key));
      },weak_factory_.GetWeakPtr(),tools_generation_,state_.workspace_id,state_.profile_id,action,std::move(choice_key)));
}
void WorkspaceShell::SetShortcutRecording(bool recording) {
  if(recording&&shortcut_chord_)shortcut_chord_->ClearCompositionText();
  if(tools_form_)tools_form_->form->tools_record(recording);
  if(shortcut_chord_)shortcut_chord_->SetReadOnly(recording);
}
void WorkspaceShell::ToolsAction(int action,std::optional<std::string> choice_key) {
  if (!tools_widget_ || !tools_widget_->IsVisible() || !tools_form_) return;
  std::u16string record_status;
  bool valid_form_choice=false;std::string snapshot_target,closed_target,form_target;
  {
    if(ToolsPage()==0){
      auto target=[&](views::View* picker,bool closed){auto* scene=views::AsViewClass<SceneSelect>(picker);return scene?std::string(tools_form_->form->tools_timeline_target(closed,choice_key?*choice_key:scene->SelectedKey())):std::string();};
      snapshot_target=target(tools_picker_,false);closed_target=target(closed_picker_,true);
    }
    if(ToolsPage()==1||ToolsPage()==2){
      if(auto* scene=views::AsViewClass<SceneSelect>(tools_picker_)){
        const auto draft=tools_form_->form->tools_choice_draft(choice_key?*choice_key:scene->SelectedKey());valid_form_choice=draft.valid;form_target=std::string(draft.target);
      }
    }
    sidebar::ToolsActionContext context{};
    context.snapshot_field=bool(snapshot_name_);context.shortcut_field=bool(shortcut_chord_);context.provider_field=bool(provider_name_);
    context.snapshot_composing=snapshot_name_&&snapshot_name_->IsIMEComposing();context.shortcut_composing=shortcut_chord_&&shortcut_chord_->IsIMEComposing();
    for(auto* field:{provider_name_.get(),provider_keyword_.get(),provider_url_.get()})if(field&&field->IsIMEComposing())context.provider_composing=true;
    context.snapshot_selected=!snapshot_target.empty();context.closed_selected=!closed_target.empty();context.shortcut_selected=ToolsPage()==1&&valid_form_choice;context.provider_selected=ToolsPage()==2&&valid_form_choice;
    const auto plan=tools_form_->form->tools_dispatch(action,context);if(!plan.admitted)return;
    if(plan.sync_recording){SetShortcutRecording(plan.recording);RefreshKeyRegistrations();}
    action=plan.action;record_status=base::UTF8ToUTF16(std::string(plan.record_status));
  }
  bool ok = false;
  if (action == 1 && snapshot_name_) {
    ok = callbacks_.save_snapshot && callbacks_.save_snapshot.Run(base::UTF16ToUTF8(snapshot_name_->GetText()), false);
  } else if ((action == 2 || action == 3) && !snapshot_target.empty()) {
    const auto& id=snapshot_target;
    ok = action == 2 ? callbacks_.restore_snapshot.Run(id) : callbacks_.delete_snapshot.Run(id);
  } else if (action == 4 && closed_picker_) {
    if(!closed_target.empty())ok=callbacks_.reopen_closed.Run(closed_target);
  } else if (action == 5) {
    const std::string id = state_.workspace_id;
    ok = callbacks_.sleep_workspace && callbacks_.sleep_workspace.Run(id);
    if (ok) return;
  } else if (action == 10 && shortcut_chord_) {
    shortcut_chord_->RequestFocus();
    SetPanelText(tools_status_,record_status); return;
  } else if ((action == 11 || action == 12) && shortcut_chord_ && valid_form_choice) {
    ok = SetShortcut(form_target, action == 12 ? "" : base::UTF16ToUTF8(shortcut_chord_->GetText()));
  } else if (action == 13) {
    tools_.shortcuts = ArcShortcuts(); keys_dirty_ = true; RefreshKeyRegistrations(); SaveTools(); ok = true;
  } else if (action >= 20 && action <= 22 && provider_name_) {
    const std::string& previous=form_target;
    if (action == 20) ok = UpsertSearchProvider({base::UTF16ToUTF8(provider_name_->GetText()), base::UTF16ToUTF8(provider_keyword_->GetText()), base::UTF16ToUTF8(provider_url_->GetText())}, previous);
    if (action == 21) ok = SetDefaultSearchProvider(previous);
    if (action == 22) ok = RemoveSearchProvider(previous);
  }
  auto outcome=tools_form_->form->tools_outcome(ok,false);
  if(outcome.refresh){RefreshTools();if(callbacks_.operation_pending&&callbacks_.operation_pending.Run())outcome=tools_form_->form->tools_outcome(true,true);}
  SetPanelText(tools_status_,base::UTF8ToUTF16(std::string(outcome.status)));
}

void WorkspaceShell::SetOperationStatus(OperationStatusEvent event) {
  SetOperationStatus(base::UTF8ToUTF16(std::string(sidebar::operation_status_copy(sidebar::OperationStatusEvent{static_cast<uint8_t>(event)}))));
}
void WorkspaceShell::SetOperationStatus(std::u16string text) {
  if (tools_widget_ && tools_widget_->IsVisible()) { RefreshTools(); SetPanelText(tools_status_,text); }
  if (profiles_widget_ && profiles_widget_->IsVisible()) SetProfileStatus(text);
}
void WorkspaceShell::OpenAddress(bool new_tab) {
  command_palette_ = false;
  address_->SetPlaceholderText(T(u"Szukaj lub wpisz adres"));
  address_->GetViewAccessibility().SetName(T(u"Szukaj lub wpisz adres"));
  new_tab_ = new_tab;
  static_cast<PaletteView*>(palette_root_.get())->Reset();
  search_results_.clear();
  search_query_.clear();
  search_timer_.Stop();
  if (!pinned_) sidebar_widget_->Hide();
  address_error_->SetVisible(false);
  const bool blank = new_tab || state_.url.path() == "/start.html";
  address_->SetText(blank ? u"" : base::UTF8ToUTF16(state_.url.spec()));
  RebuildSuggestions();
  if (url_pill_) SetRowEditing(url_pill_, true);
  address_widget_->Show();
  address_widget_->Activate();
  address_widget_->ClearNativeFocus();
  address_->RequestFocus();
  address_->SelectRange(gfx::Range(0,address_->GetText().size()));
  RequestSearch();
}
void WorkspaceShell::OpenCommandPalette() {
  OpenAddress(false);
  if (url_pill_) SetRowEditing(url_pill_, false);  // The Command Center is not an address edit.
  // The Command Center sits on the dimmed window (design: CommandPalette).
  if (scrim_widget_) {
    PositionPanels(); scrim_widget_->ShowInactive();
    if (sidebar_visible()) scrim_widget_->StackAboveWidget(sidebar_widget_.get());
    address_widget_->StackAboveWidget(scrim_widget_.get());
  }
  command_palette_ = true;
  address_->SetPlaceholderText(T(u"Szukaj lub wpisz > i polecenie"));
  address_->GetViewAccessibility().SetName(T(u"Command Center — karty, historia i polecenia"));
  address_->SetText(u"");
  search_results_.clear();
  RebuildSuggestions();
  RequestSearch();
}
void WorkspaceShell::ShowSidebar() {
  if (!widget_ || !widget_->IsVisible()) return;
  address_widget_->Hide();
  PositionPanels();
  sidebar_widget_->ShowInactive();
  sidebar_widget_->StackAboveWidget(widget_);
  ScheduleMetalSidebar();
}
void WorkspaceShell::ToggleSidebar() {
  if (sidebar_visible()) {
    if (pinned_) SetPinned(false);
    sidebar_widget_->Hide();
  } else ShowSidebar();
}
bool WorkspaceShell::CloseOverlays(bool keep_sidebar) {
  if (!keep_sidebar) CancelWorkspaceSwipe();
  const bool visible = address_visible() || settings_visible() ||
      (profiles_widget_ && profiles_widget_->IsVisible()) || import_visible() ||
      (general_widget_ && general_widget_->IsVisible()) || (tools_widget_ && tools_widget_->IsVisible()) || (!keep_sidebar && !pinned_ && sidebar_visible());
  if (tools_widget_) tools_widget_->Hide();
  SetShortcutRecording(false);
  if (general_widget_) general_widget_->Hide();
  if (profiles_widget_) profiles_widget_->Hide();
  if (import_widget_) import_widget_->Hide();
  address_widget_->Hide();
  settings_widget_->Hide();
  import_preview_timer_.Stop();
  if(import_form_)import_form_->form->cancel_scan();
  history_timer_.Stop();
  if (!keep_sidebar && !pinned_) sidebar_widget_->Hide();
  return visible;
}
bool WorkspaceShell::address_visible() const { return address_widget_ && address_widget_->IsVisible(); }
bool WorkspaceShell::settings_visible() const { return settings_widget_ && settings_widget_->IsVisible(); }
void WorkspaceShell::OpenWorkspaceSettings() {
  if (!widget_ || !widget_->IsVisible()) return;
  if (general_widget_) general_widget_->Hide();
  address_widget_->Hide();
  if (profiles_widget_) profiles_widget_->Hide();
  RefreshWorkspaceSettings();
  PositionPanels();
  settings_widget_->Show();
  settings_widget_->Activate();
}
bool WorkspaceShell::sidebar_visible() const { return sidebar_widget_ && sidebar_widget_->IsVisible(); }
void WorkspaceShell::RestorePageFocus() {
  if ((general_widget_ && general_widget_->IsVisible()) || (tools_widget_ && tools_widget_->IsVisible())) return;
  widget_->Activate();
  if (callbacks_.focus_pane) callbacks_.focus_pane.Run(state_.active_pane);
}
void WorkspaceShell::SetPinned(bool pinned) {
  pinned_ = pinned;
  pin_dirty_ = true;
  RefreshGeneralControls();
  ApplyAppearance();
  if (pinned_) ShowSidebar();
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, pinned_, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) shell->ShowWorkspaceAppearanceError(0);
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::ResizeSidebar(int amount, bool done) {
  if (!widget_) return;
  if (!sidebar_resizing_) { sidebar_resize_start_ = sidebar_width_; sidebar_resizing_ = true; }
  const int width=sidebar::sidebar_resize_width(sidebar_resize_start_,amount);
  if(done)sidebar_resizing_=false;
  ApplySidebarWidth(width,done);
}
void WorkspaceShell::ApplySidebarWidth(int width,bool done) {
  if(!widget_)return;
  sidebar_width_=width;
  sidebar_width_dirty_ = true;
  static_cast<ShellRoot*>(root_.get())->sidebar_width = sidebar_width_;
  root_->InvalidateLayout();
  PositionPanels();
  if (!done) return;
  sidebar_resizing_ = false;
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, std::nullopt, std::nullopt, std::nullopt,
                     std::nullopt, std::optional<int>(sidebar_width_), std::nullopt, std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool) {
        if (shell) --shell->pending_writes_;
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::SetWorkspaceColor(std::string_view id, SkColor color) {
  const std::string key(id);
  colors_[key] = color | 0xFF000000;
  color_dirty_.insert(key);
  color_error_->SetVisible(false);
  ApplyAppearance();
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::optional<std::string>(key), colors_[key], std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) shell->ShowWorkspaceAppearanceError(1);
      }, weak_factory_.GetWeakPtr()));
}
std::u16string WorkspaceShell::workspace_emoji(std::string_view id) const {
  auto it = emoji_.find(std::string(id));
  return it == emoji_.end() ? std::u16string() : it->second;
}
void WorkspaceShell::RefreshWorkspaceHeading() {
  if (swipe_overlay_ && workspace_swipe_->tracking() && !swipe_workspace_ids_.empty()) {
    const auto index = workspace_swipe_->target(state_.workspaces.size());
    const auto& workspace = state_.workspaces[index];
    const auto emoji = workspace_emoji(workspace.id);
    static_cast<WorkspaceHeading*>(workspace_name_.get())->SetText((emoji.empty() ? u"" : emoji + u"  ") + base::UTF8ToUTF16(workspace.name));
    return;
  }
  const auto emoji = workspace_emoji(state_.workspace_id);
  static_cast<WorkspaceHeading*>(workspace_name_.get())->SetText(emoji.empty() ? state_.workspace_name : emoji + u"  " + state_.workspace_name);
}
void WorkspaceShell::SetWorkspaceEmoji(std::string_view id, std::u16string text) {
  // Keep a single grapheme: a typed "🚀abc" or a pasted sentence stores 🚀.
  std::u16string emoji;
  const auto trimmed = std::u16string(base::TrimWhitespace(text, base::TRIM_ALL));
  base::i18n::BreakIterator graphemes(trimmed, base::i18n::BreakIterator::BREAK_CHARACTER);
  if (graphemes.Init() && graphemes.Advance()) emoji = std::u16string(graphemes.GetString());
  const std::string key(id);
  if (workspace_emoji(key) == emoji) {
    if (key == state_.workspace_id && emoji_input_ && emoji_input_->GetText() != emoji && !text.empty()) emoji_input_->SetText(emoji);
    return;
  }
  if (emoji.empty()) emoji_.erase(key); else emoji_[key] = emoji;
  emoji_dirty_.insert(key);
  RebuildWorkspaceDots();
  RefreshWorkspaceHeading();
  ApplyAppearance();
  if (key == state_.workspace_id && emoji_input_ && emoji_input_->GetText() != emoji) emoji_input_->SetText(emoji);
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, std::nullopt, std::nullopt, std::nullopt,
                     std::nullopt, std::nullopt,
                     std::optional<std::pair<std::string, std::string>>({key, base::UTF16ToUTF8(emoji)}), std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) shell->ShowWorkspaceAppearanceError(2);
      }, weak_factory_.GetWeakPtr()));
}
SkColor WorkspaceShell::workspace_color(std::string_view id) const {
  auto it = colors_.find(std::string(id));
  return it == colors_.end() ? kDefaultWorkspaceColor : it->second;
}
void WorkspaceShell::ToggleWorkspaceCreation() {
  if(creating_workspace_){RefreshWorkspaceSettings();PositionPanels();return;}
  if(workspace_form_)workspace_form_->form->begin_create(state_.workspace_id);
  creating_workspace_=true;workspace_appearance_->SetVisible(false);workspace_more_->SetVisible(false);configuration_error_->SetVisible(false);
  SetPanelText(settings_title_,T(u"Nowa przestrzeń"));workspace_input_->SetText(u"");workspace_input_->RequestFocus();
  SetActionText(edit_workspace_,T(u"Utwórz przestrzeń"));SetActionText(add_workspace_,T(u"Anuluj"));SetCreateAction(edit_workspace_,true);
  RefreshRustWorkspacePresentation();PositionPanels();
}
void WorkspaceShell::RefreshRustWorkspacePresentation() {
  if(!workspace_form_)return;
  const auto v=workspace_form_->form->view(base::UTF16ToUTF8(state_.workspace_name),base::UTF16ToUTF8(state_.profile_name),state_.configuration_ready,state_.profiles_busy,state_.workspaces.size());
  creating_workspace_=v.creating;SetPanelText(settings_title_,base::UTF8ToUTF16(std::string(v.title)));SetPanelText(profile_name_,base::UTF8ToUTF16(std::string(v.description)));
  SetActionText(edit_workspace_,base::UTF8ToUTF16(std::string(v.save_label)));SetActionText(add_workspace_,base::UTF8ToUTF16(std::string(v.secondary_label)));SetCreateAction(edit_workspace_,v.creating);
  workspace_appearance_->SetVisible(v.appearance);workspace_more_->SetVisible(v.appearance);edit_workspace_->SetEnabled(v.enabled);add_workspace_->SetEnabled(v.enabled);profile_picker_->SetEnabled(v.enabled);remove_workspace_->SetEnabled(v.remove_enabled);
  SetPanelText(configuration_error_,base::UTF8ToUTF16(std::string(v.error)));configuration_error_->SetVisible(!v.error.empty());
}
void WorkspaceShell::ShowWorkspaceAppearanceError(unsigned kind) {
  const std::array<std::u16string,4> messages={T(u"Nie udało się zapisać ustawień"),T(u"Nie udało się zapisać koloru"),T(u"Nie udało się zapisać emoji"),T(u"Podaj kolor #RRGGBB")};
  std::u16string text(messages[std::min<size_t>(kind,3)]);
  if(workspace_form_)text=base::UTF8ToUTF16(std::string(sidebar::workspace_appearance_error(kind)));
  SetPanelText(color_error_,std::move(text));color_error_->SetVisible(true);PositionPanels();
}
bool WorkspaceShell::workspace_panel_contains_scene_text(std::string_view text) const {
  if(!settings_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* picker=views::AsViewClass<SceneSelect>(view))if(picker->contains_text(text))return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,settings_root_);
}
void WorkspaceShell::RefreshWorkspaceSettings() {
  if(workspace_form_)workspace_form_->form->reset(state_.workspace_id);
  creating_workspace_ = false;
  workspace_appearance_->SetVisible(true);workspace_more_->SetVisible(true);
  SetPanelText(settings_title_,state_.workspace_name);
  SetActionText(edit_workspace_,T(u"Zapisz nazwę i profil"));SetCreateAction(edit_workspace_,false);
  SetActionText(add_workspace_,T(u"Nowa przestrzeń…"));
  workspace_input_->SetText(state_.workspace_name);
  for (size_t i = 0; i < state_.profiles.size(); ++i)
    if (state_.profiles[i].id == state_.profile_id) SetPickerIndex(profile_picker_,i);
  RefreshRustWorkspacePresentation();
}
void WorkspaceShell::SetConfigurationError(bool error) {
  if(workspace_form_){workspace_form_->form->set_configuration_error(error);RefreshRustWorkspacePresentation();PositionPanels();return;}
  SetPanelText(configuration_error_,error ? T(u"Nie udało się odczytać lub zapisać przestrzeni.") : u"");
  configuration_error_->SetVisible(error);
  PositionPanels();
}
// Removing a workspace can be undone (its tabs reload from their URLs), so it
// happens at once with a toast instead of a confirmation dialog.
void WorkspaceShell::ConfirmRemoveWorkspace(std::string id) {
  if (!widget_ || !state_.configuration_ready || state_.profiles_busy || state_.workspaces.size() < 2) return;
  const auto target = std::ranges::find(state_.workspaces, id, &Workspace::id);
  if (target == state_.workspaces.end()) return;
  const auto name = base::UTF8ToUTF16(target->name);
  pending_removal_.reset();
  const bool ok = callbacks_.remove_workspace.Run(id);
  SetConfigurationError(!ok);
  if (!ok) return;
  // Pages with unsaved changes may ask first; the toast follows the removal.
  if (std::ranges::any_of(state_.workspaces, [&](const auto& w) { return w.id == id; })) { pending_removal_.emplace(id, name); return; }
  ShowWorkspaceRemovedToast(std::move(id), name);
}
void WorkspaceShell::ShowWorkspaceRemovedToast(std::string id, std::u16string name) {
  ShowToast(T(u"Usunięto przestrzeń "), std::move(name), base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, std::string id) {
    if (shell) shell->SetConfigurationError(!shell->callbacks_.restore_workspace.Run(id));
  }, weak_factory_.GetWeakPtr(), std::move(id)));
}

// Rust owns each form member's semantic command and key. Native dispatch keeps
// service authorization, stores, composition and the existing form coordinators.
void WorkspaceShell::BindFormCommand(views::View* view,sidebar::FormCommand command,std::string key){
  auto* control=views::AsViewClass<RustActionView>(view);CHECK(control);
  control->SetFormCommand(command,key,base::BindRepeating(&WorkspaceShell::DispatchFormCommand,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::BindAppearancePreset(views::View* view,bool emoji,size_t index){
  auto* control=views::AsViewClass<RustActionView>(view);CHECK(control);
  control->SetAppearancePreset(emoji,index,base::BindRepeating(&WorkspaceShell::DispatchFormCommand,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::BindPreferenceCategory(views::View* view,size_t category){
  auto* control=views::AsViewClass<RustActionView>(view);CHECK(control);CHECK(preferences_form_);
  control->SetPreferenceCategory(*preferences_form_->form,category,base::BindRepeating(&WorkspaceShell::DispatchFormCommand,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::DispatchFormCommand(views::View* source,const sidebar::FormCommandBinding& binding){
  using C=sidebar::FormCommand;const auto command=binding.command;
  const views::Widget* panel=nullptr;
  switch(binding.panel){
    case sidebar::FormPanel::Workspace:panel=settings_widget_.get();break;
    case sidebar::FormPanel::Profiles:panel=profiles_widget_.get();break;
    case sidebar::FormPanel::Import:panel=import_widget_.get();break;
    case sidebar::FormPanel::Preferences:panel=general_widget_.get();break;
    case sidebar::FormPanel::Tools:panel=tools_widget_.get();break;
    default:return;
  }
  if(!source||!panel||!panel->IsVisible()||!SceneIsVisible(source)||!source->GetEnabledInViewsSubtree()||source->GetWidget()!=panel||!panel->GetContentsView()||!panel->GetContentsView()->Contains(source))return;
  switch(command){
    case C::PreferenceCategory:SelectPreferenceCategory(binding.index);break;
    case C::PreferenceMore:{
      // Rebuilding the list destroys this adapter. The Rust ticket holds only
      // a weak record and rejects cancellation, retirement or a new input lease.
      auto* action=views::AsViewClass<RustActionView>(source);if(!action)return;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,std::string profile,rust::Box<sidebar::FormCommandTicket> ticket){
        if(!shell||shell->state_.profile_id!=profile||!shell->general_widget_||!shell->general_widget_->IsVisible())return;
        const auto command=ticket->take_command();if(command.command!=sidebar::FormCommand::PreferenceMore||command.panel!=sidebar::FormPanel::Preferences)return;
        shell->MorePreferenceRules();shell->RefreshSitePreferences(false);
      },weak_factory_.GetWeakPtr(),state_.profile_id,action->DeferFormCommand()));break;
    }
    case C::WorkspaceDismiss:settings_widget_->Hide();RestorePageFocus();break;
    case C::PreferencesDismiss:CloseOverlays();RestorePageFocus();break;
    case C::ToolsDismiss:SetShortcutRecording(false);tools_widget_->Hide();RestorePageFocus();break;
    case C::HistoryMore:if(history_more_==source)RefreshHistory(true);break;
    case C::ImportDismiss:{if(import_applying_)return;const auto alive=weak_factory_.GetWeakPtr();callbacks_.dismiss_import.Run();if(!alive)return;CloseOverlays();RestorePageFocus();break;}
    case C::WorkspaceColorPreset:case C::WorkspaceEmojiPreset:{
      const auto target=sidebar::appearance_command_target(command,binding.index);if(!target.valid)return;
      if(command==C::WorkspaceColorPreset)SetWorkspaceColor(state_.workspace_id,target.color);
      else SetWorkspaceEmoji(state_.workspace_id,base::UTF8ToUTF16(std::string(target.emoji)));
      break;
    }
    case C::WorkspaceSave:SubmitWorkspace(creating_workspace_);break;
    case C::WorkspaceToggle:ToggleWorkspaceCreation();break;
    case C::WorkspaceRemove:ConfirmRemoveWorkspace(state_.workspace_id);break;
    case C::ProfileCreate:SubmitProfile();break;
    case C::ProfileRename:RenameSelectedProfile();break;
    case C::ProfileDelete:DeleteSelectedProfile();break;
    case C::ProfileBack:case C::Workspace:OpenWorkspaceSettings();break;
    case C::ImportApply:SubmitImport();break;
    case C::PreferenceText:SaveTextPreference(std::string(binding.key));break;
    case C::PreferenceSite:SaveSitePreference();break;
    case C::PreferenceClear:if(callbacks_.clear_browsing_data)callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(clear_data_period_).value_or(0)),CheckValue(clear_history_),CheckValue(clear_cookies_),CheckValue(clear_cache_)});break;
    case C::PreferenceDirectory:if(callbacks_.choose_download_directory)callbacks_.choose_download_directory.Run();break;
    case C::History:OpenTools(3);break;
    case C::Passwords:CloseOverlays();callbacks_.passwords.Run();break;
    case C::WorkspaceProfiles:case C::Profiles:OpenProfiles({},{});break;
    case C::WorkspaceImport:case C::Import:OpenImport();break;
    case C::Proxy:case C::Certificates:if(callbacks_.system_settings)callbacks_.system_settings.Run(command==C::Certificates);break;
    case C::Providers:OpenTools(2);break;
    case C::Shortcuts:OpenTools(1);break;
    case C::TimeMachine:OpenTools(0);break;
    default:break;
  }
}
void WorkspaceShell::SubmitWorkspace(bool add) {
  if(workspace_form_){
    const auto index=PickerIndex(profile_picker_);const auto profile=index&&*index<state_.profiles.size()?state_.profiles[*index].id:std::string();
    const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
    const auto request=workspace_form_->form->submit(base::UTF16ToUTF8(workspace_input_->GetText()),profile,state_.workspace_id,state_.configuration_ready,state_.profiles_busy,workspace_input_->IsIMEComposing(),rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));
    if(request.action==sidebar::WorkspaceFormAction::None){RefreshRustWorkspacePresentation();PositionPanels();return;}
    const auto alive=weak_factory_.GetWeakPtr();
    const bool accepted=request.action==sidebar::WorkspaceFormAction::Create?callbacks_.add_workspace.Run(std::string(request.name),std::string(request.profile)):callbacks_.edit_workspace.Run(std::string(request.name),std::string(request.profile));
    if(!alive||!workspace_form_)return;
    workspace_form_->form->finish_attempt(accepted);SetConfigurationError(!accepted);
    if(accepted)RefreshWorkspaceSettings();else RefreshRustWorkspacePresentation();return;
  }
  std::string name;
  base::TrimWhitespaceASCII(base::UTF16ToUTF8(workspace_input_->GetText()), base::TRIM_ALL, &name);
  if (name.empty()) {
    SetPanelText(configuration_error_,T(u"Podaj nazwę przestrzeni."));
    configuration_error_->SetVisible(true);
    PositionPanels();
    return;
  }
  const auto index = PickerIndex(profile_picker_);
  if (!index || *index >= state_.profiles.size()) return;
  const std::string profile = state_.profiles[*index].id;
  const bool ok = add ? callbacks_.add_workspace.Run(name, profile) : callbacks_.edit_workspace.Run(name, profile);
  SetConfigurationError(!ok);
  if (ok) RefreshWorkspaceSettings();
}
void WorkspaceShell::OpenImport() {
  if (!import_widget_) return;
  CloseOverlays();
  PositionPanels();
  import_widget_->Show();
  import_widget_->Activate();
  if (!import_applying_) ImportSelectionChanged();
}
bool WorkspaceShell::import_visible() const { return import_widget_ && import_widget_->IsVisible(); }
BrowserImportOptions WorkspaceShell::import_options() const {
  BrowserImportOptions options;
  options.browser = static_cast<ImportBrowser>(ImportSourceIndex(import_browser_picker_));
  options.separate_profiles = CheckValue(import_profiles_);
  options.bookmarks = CheckValue(import_bookmarks_);
  options.history = CheckValue(import_history_);
  options.cookies = CheckValue(import_cookies_);
  return options;
}
void WorkspaceShell::SelectImportBrowserForTesting(size_t index) {
  if(!import_browser_picker_||!import_browser_picker_->GetEnabledInViewsSubtree()||!SceneIsVisible(import_browser_picker_))return;
  views::AsViewClass<SceneSelect>(import_browser_picker_)->SelectForTesting(index);
}
bool WorkspaceShell::import_source_metal_visible() const {
  auto* source=views::AsViewClass<SceneSelect>(import_browser_picker_);return source&&source->metal_visible();
}
void WorkspaceShell::SetImportOptionCheckedForTesting(size_t option,bool checked) {
  const std::array<views::View*,4> controls={import_profiles_,import_bookmarks_,import_history_,import_cookies_};if(option<controls.size()&&controls[option])SetCheckValue(controls[option],checked);
}
bool WorkspaceShell::import_close_metal_visible_for_testing() const {
  auto* group=dismiss_import_?views::AsViewClass<ScenePreferenceControls>(dismiss_import_->parent()):nullptr;return group&&group->metal_visible();
}
bool WorkspaceShell::import_choices_metal_visible() const {
  auto* group=import_profiles_?views::AsViewClass<SceneCheckboxGroup>(import_profiles_->parent()):nullptr;return group&&group->metal_visible();
}
void WorkspaceShell::BeginImportPreview(const BrowserImportOptions& options) {
  if(import_form_){import_preview_timer_.Stop();import_form_->form->begin_preview(ImportUiSelection(options));CancelSceneActions(import_root_);}
}
void WorkspaceShell::RefreshRustImportControls() {
  if(!import_form_||!import_status_)return;const auto v=import_form_->form->view(state_.configuration_ready,state_.profiles_busy);
  SetPanelText(import_status_,base::UTF8ToUTF16(std::string(v.message)));import_status_->SetVisible(!v.message.empty());SetCheckText(import_bookmarks_,base::UTF8ToUTF16(std::string(v.bookmarks_label)));
  import_browser_picker_->SetEnabled(v.options_enabled);for(auto* control:{import_profiles_.get(),import_bookmarks_.get(),import_history_.get(),import_cookies_.get()})control->SetEnabled(v.options_enabled);
  dismiss_import_->SetEnabled(v.close_enabled);apply_import_->SetEnabled(v.apply_enabled);import_applying_=v.applying;import_busy_=v.busy;import_can_apply_=v.can_apply;
}
void WorkspaceShell::DispatchImportPreview(uint64_t epoch) {
  if(!import_form_)return;const auto request=import_form_->form->take_scan(epoch,import_visible(),ImportUiSelection(import_options()),state_.configuration_ready,state_.profiles_busy);
  RefreshRustImportControls();PositionPanels();
  if(request.action==sidebar::ImportAction::Rescan){ImportSelectionChanged();return;}
  if(request.action==sidebar::ImportAction::Preview)callbacks_.preview_import.Run(ImportServiceOptions(request.selection));
}
void WorkspaceShell::SubmitImport() {
  if(import_form_){const auto request=import_form_->form->request_apply(ImportUiSelection(import_options()),state_.configuration_ready,state_.profiles_busy,import_visible());
    if(request.action==sidebar::ImportAction::Rescan){ImportSelectionChanged();return;}
    RefreshRustImportControls();PositionPanels();if(request.action!=sidebar::ImportAction::Apply)return;
    const auto alive=weak_factory_.GetWeakPtr();const bool accepted=callbacks_.apply_import.Run();if(!alive||!import_form_)return;import_form_->form->finish_apply(accepted);RefreshRustImportControls();PositionPanels();return;
  }
  callbacks_.apply_import.Run();
}
bool WorkspaceShell::import_panel_contains_scene_text(std::string_view text) const {
  if(!import_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* source=views::AsViewClass<SceneSelect>(view))if(source->contains_text(text))return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;if(auto* choices=views::AsViewClass<SceneCheckboxGroup>(view))if(choices->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,import_root_);
}
void WorkspaceShell::ImportSelectionChanged() {
  if(import_form_){import_preview_timer_.Stop();const auto request=import_form_->form->select(ImportUiSelection(import_options()));RefreshRustImportControls();PositionPanels();if(request.action!=sidebar::ImportAction::Preview)return;
    CancelSceneActions(import_root_);CancelSceneCheckboxes(import_root_);callbacks_.invalidate_import.Run();
    import_preview_timer_.Start(FROM_HERE,base::Milliseconds(200),base::BindOnce(&WorkspaceShell::DispatchImportPreview,weak_factory_.GetWeakPtr(),request.epoch));return;
  }
  if (import_applying_) return;
  const bool arc = import_options().browser == ImportBrowser::kArc;
  SetCheckText(import_bookmarks_,arc ? T(u"Przypięte karty, foldery i Ulubione") : T(u"Zakładki i foldery"));
  callbacks_.invalidate_import.Run();
  SetImportStatus(T(u"Odczytywanie danych…"), false, true);
  import_preview_timer_.Start(FROM_HERE, base::Milliseconds(200), base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell) {
        if (shell && shell->import_visible()) shell->callbacks_.preview_import.Run(shell->import_options());
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::SetImportStatus(const ImportStatusReport& report, bool can_import, bool busy, bool applying) {
  using Stage = ImportStatusReport::Stage;
  if (import_form_) {
    sidebar::ImportStatusReport rust{};
    rust.stage = sidebar::ImportStage{static_cast<uint8_t>(report.stage)};rust.browser = base::UTF16ToUTF8(report.browser);rust.error = report.error;
    rust.profiles = report.profiles;rust.spaces = report.spaces;rust.folders = report.folders;rust.tabs = report.tabs;rust.skipped = report.skipped;
    rust.ok = report.ok;rust.added_profiles = report.added_profiles;rust.added_spaces = report.added_spaces;
    rust.browsing_data = report.browsing_data;rust.history = report.history;rust.cookies = report.cookies;rust.data_skipped = report.data_skipped;rust.data_failed = report.data_failed;
    SetImportStatus(base::UTF8ToUTF16(std::string(sidebar::import_status_copy(rust))), can_import, busy, applying);
    return;
  }
  std::u16string text;
  switch (report.stage) {
    case Stage::kReading: SetImportStatus(TF(u"Odczytywanie danych z {}…", {report.browser}), can_import, busy, applying); return;
    case Stage::kImporting: text = T(u"Importowanie wybranych danych…"); break;
    case Stage::kSavingBrowsingData: text = T(u"Zapisywanie historii i ciasteczek…"); break;
    case Stage::kPreview:
      text = report.error.empty() ? TF(u"Do importu: {} profili, {} przestrzeni, {} folderów i {} kart lub zakładek.", {base::NumberToString16(report.profiles), base::NumberToString16(report.spaces),
          base::NumberToString16(report.folders), base::NumberToString16(report.tabs)}) : base::UTF8ToUTF16(report.error);
      if (report.skipped) text += u" " + TF(u"Pominięte elementy: {}.", {base::NumberToString16(report.skipped)});
      break;
    case Stage::kFinished:
      text = report.ok ? TF(u"Import zakończony. Dodano {} profili i {} przestrzeni. Możesz wybrać je na liście przestrzeni.", {base::NumberToString16(report.added_profiles), base::NumberToString16(report.added_spaces)})
          : T(u"Import się nie powiódł. Dotychczasowe przestrzenie pozostały bez zmian. Spróbuj ponownie.");
      if (report.browsing_data) {
        text += u" " + TF(u"Przeniesiono {} stron historii i {} ciasteczek.", {base::NumberToString16(report.history), base::NumberToString16(report.cookies)});
        if (report.data_skipped) text += u" " + TF(u"Pominięto: {}.", {base::NumberToString16(report.data_skipped)});
        if (report.data_failed) text += u" " + T(u"Część danych nie została zapisana. Ponów import.");
      }
      break;
  }
  SetImportStatus(std::move(text), can_import, busy, applying);
}
void WorkspaceShell::SetImportStatus(std::u16string message, bool can_import, bool busy, bool applying) {
  import_preview_timer_.Stop();if(!import_status_)return;
  if(import_form_){import_form_->form->set_status(base::UTF16ToUTF8(message),can_import,busy,applying);RefreshRustImportControls();PositionPanels();return;}
  SetPanelText(import_status_,message);
  import_status_->SetVisible(!message.empty());
  // The source and options remain editable while scanning. Only applying an
  // import locks them; replies from earlier scans cannot enable Apply again.
  import_applying_ = applying;
  import_browser_picker_->SetEnabled(!applying);
  import_profiles_->SetEnabled(!applying);
  import_bookmarks_->SetEnabled(!applying);
  import_history_->SetEnabled(!applying);
  import_cookies_->SetEnabled(!applying);
  dismiss_import_->SetEnabled(!applying);
  import_can_apply_ = can_import;
  import_busy_ = busy;
  apply_import_->SetEnabled(can_import && !busy && state_.configuration_ready && !state_.profiles_busy);
  PositionPanels();
}
std::optional<size_t> WorkspaceShell::profile_picker_selection(bool managed) const {
  if(!managed)return PickerIndex(profile_picker_.get());
  const std::string id=profile_form_?std::string(profile_form_->form->selection()):std::string();
  for(size_t i=0;i<state_.profiles.size();++i)if(state_.profiles[i].id==id)return i;
  return std::nullopt;
}
size_t WorkspaceShell::profile_picker_count() const {return PickerCount(profile_picker_);}
void WorkspaceShell::SetProfilePickerSelectionForTesting(bool managed,size_t index){
  if(managed){if(index<state_.profiles.size())SelectManagedProfile(state_.profiles[index].id);return;}
  if(profile_picker_&&index<PickerCount(profile_picker_))SetPickerIndex(profile_picker_,index);
}
void WorkspaceShell::ActivateProfilePickerSelectionForTesting(bool managed,size_t index){
  if(managed){if(index<state_.profiles.size()&&profile_list_&&SceneIsVisible(profile_list_)&&profile_list_->GetEnabledInViewsSubtree())SelectManagedProfile(state_.profiles[index].id);return;}
  auto* picker=profile_picker_.get();if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SelectForTesting(index);return;}
  SetPickerIndex(picker,index);
}
bool WorkspaceShell::profile_picker_metal_visible(bool managed) const {
  if(managed)return profile_list_&&SceneIsVisible(profile_list_);
  auto* scene=views::AsViewClass<SceneSelect>(profile_picker_.get());return scene&&scene->metal_visible();
}
namespace {
SkColor ProfileAvatarColor(std::string_view id){return kMonogramColors[base::PersistentHash(id)%kMonogramColors.size()];}
RustRowView* ProfileRowView(views::View* line){return line&&!line->children().empty()?views::AsViewClass<RustRowView>(line->children().front().get()):nullptr;}
}  // namespace
void WorkspaceShell::RebuildProfileRows(){
  if(!profile_list_||!profile_form_)return;
  CancelSceneActions(profiles_root_);
  for(auto& row:profile_rows_)profile_list_->RemoveChildViewT(row.get());
  profile_rows_.clear();profile_row_ids_.clear();
  const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
  const auto rows=sidebar::profile_rows(rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()),state_.profile_id);
  const auto weak=weak_factory_.GetWeakPtr();
  for(const auto& r:rows){
    const std::string id(r.id);auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};
    auto* line=profile_list_->AddChildView(std::make_unique<views::View>());
    RowRecipe::Spec spec;spec.title=text(r.name);spec.detail=text(r.detail);spec.monogram=text(r.initial);spec.monogram_color=ProfileAvatarColor(id);spec.stacked=true;spec.height=48;
    auto* row=line->AddChildView(CreateSceneRow(base::BindRepeating(&WorkspaceShell::SelectManagedProfile,weak,id),std::move(spec),{},{}));
    line->AddChildView(CreateSceneAction(base::BindRepeating(&WorkspaceShell::StartProfileRename,weak,id),text(r.rename_ax),ControlGlyph::kVector,&vector_icons::kEditIcon,{},{}));
    line->AddChildView(CreateSceneAction(base::BindRepeating(&WorkspaceShell::RequestProfileDelete,weak,id),text(r.remove_ax),ControlGlyph::kVector,&views::kDeleteIcon,{},{}));
    SetSidebarPanelLayout(line,false,gfx::Insets(),2,row,true);
    profile_rows_.push_back(line);profile_row_ids_.push_back(id);
  }
  RefreshProfileControls();
}
// Rename and delete strips and the row status sit beneath the selected row.
void WorkspaceShell::LayoutProfileStrips(){
  if(!profile_list_||!profile_form_)return;
  const std::string selected(profile_form_->form->selection());const std::string confirming(CurrentProfileControls().confirming);
  const bool renaming=!renaming_profile_id_.empty()&&renaming_profile_id_==selected;const bool confirm=!confirming.empty()&&confirming==selected;
  const bool status=profile_status_->GetVisible();
  size_t index=0;bool placed=false;
  for(size_t i=0;i<profile_rows_.size();++i){
    auto* line=profile_rows_[i].get();profile_list_->ReorderChildView(line,index++);const bool here=profile_row_ids_[i]==selected;
    if(auto* row=ProfileRowView(line))row->SetActive(here);
    if(!here)continue;placed=true;
    if(renaming)profile_list_->ReorderChildView(profile_rename_strip_,index++);
    if(confirm)profile_list_->ReorderChildView(profile_confirm_strip_,index++);
    if(status)profile_list_->ReorderChildView(profile_status_,index++);
  }
  if(!placed&&status)profile_list_->ReorderChildView(profile_status_,index++);
  profile_rename_strip_->SetVisible(renaming);profile_confirm_strip_->SetVisible(confirm);
  if(confirm)for(const auto& p:state_.profiles)if(p.id==selected)SetPanelText(profile_confirm_text_,base::UTF8ToUTF16(std::string(sidebar::profile_confirm_copy(p.name))));
  profile_list_->InvalidateLayout();PositionPanels();
}
void WorkspaceShell::SelectManagedProfile(std::string profile_id){
  if(!profile_form_)return;
  CancelSceneActions(profiles_root_);new_profile_workspace_id_.clear();
  if(renaming_profile_id_!=profile_id)renaming_profile_id_.clear();
  profile_form_->form->select(profile_id);last_profile_action_=1;
  SetProfileStatus(u"");RefreshProfileControls();
}
void WorkspaceShell::StartProfileRename(std::string profile_id){
  SelectManagedProfile(profile_id);
  if(!CurrentProfileControls().rename)return;
  for(const auto& p:state_.profiles)if(p.id==profile_id){const auto name=base::UTF8ToUTF16(p.name);profile_rename_input_->SetText(name);profile_rename_input_->SelectRange(gfx::Range(0,name.size()));}
  renaming_profile_id_=std::move(profile_id);LayoutProfileStrips();profile_rename_input_->RequestFocus();
}
void WorkspaceShell::RequestProfileDelete(std::string profile_id){
  SelectManagedProfile(profile_id);SubmitRustProfileAction(2);
}
void WorkspaceShell::CancelProfileRow(){
  if(!profile_form_)return;
  CancelSceneActions(profiles_root_);renaming_profile_id_.clear();profile_form_->form->cancel();
  SetProfileStatus(u"");RefreshProfileControls();
}
void WorkspaceShell::OpenEmojiPicker(){
  if(!emoji_input_||!settings_widget_||!settings_widget_->IsVisible())return;
  // Start empty so the picked emoji is the whole value the commit applies.
  emoji_input_->SetText(u"");emoji_input_->RequestFocus();
#if BUILDFLAG(IS_MAC)
  ShowCharacterPalette();
#endif
}
bool WorkspaceShell::HandleProfileEditorKey(bool rename,const ui::KeyEvent& event){
  auto* field=rename?profile_rename_input_.get():profile_name_input_.get();
  if(!field||event.type()!=ui::EventType::kKeyPressed||field->IsIMEComposing())return false;
  if(event.key_code()==ui::VKEY_RETURN){if(rename)RenameSelectedProfile();else SubmitProfile();return true;}
  if(rename&&event.key_code()==ui::VKEY_ESCAPE){CancelProfileRow();return true;}
  return false;
}
void WorkspaceShell::OpenProfiles(std::string profile_id, std::string workspace_id) {
  if (!profiles_widget_) return;
  if(profile_form_)CancelSceneActions(profiles_root_);
  CloseOverlays();
  new_profile_workspace_id_ = std::move(workspace_id);
  renaming_profile_id_.clear();
  profile_name_input_->SetText(u"");
  std::string selected=profile_id;
  if(selected.empty()&&profile_form_)selected=std::string(profile_form_->form->selection());
  if(selected.empty())selected=state_.profile_id;
  if(profile_form_)profile_form_->form->open(selected,new_profile_workspace_id_);
  last_profile_action_=new_profile_workspace_id_.empty()?1:0;
  RebuildProfileRows();
  PositionPanels();
  profiles_widget_->Show();
  profiles_widget_->Activate();
  if(!profile_id.empty())StartProfileRename(profile_id);
  else if(!new_profile_workspace_id_.empty())profile_name_input_->RequestFocus();
}
void WorkspaceShell::ReportClearData(ClearDataEvent event, const std::u16string& profile) {
  if (metal_sidebar_) {
    const auto status = sidebar::clear_data_status(sidebar::ClearDataEvent{static_cast<uint8_t>(event)}, base::UTF16ToUTF8(profile));
    SetPreferencesStatus(base::UTF8ToUTF16(std::string(status.text)), status.error);
    return;
  }
  switch (event) {
    case ClearDataEvent::kBusy: SetPreferencesStatus(T(u"Poczekaj na zakończenie bieżącej operacji."), true); break;
    case ClearDataEvent::kNothingSelected: SetPreferencesStatus(T(u"Wybierz co najmniej jeden rodzaj danych i zakres czasu."), true); break;
    case ClearDataEvent::kHistoryForbidden: SetPreferencesStatus(T(u"Zasady tego profilu nie pozwalają usuwać historii."), true); break;
    case ClearDataEvent::kClearing: SetPreferencesStatus(TF(u"Czyszczenie danych profilu „{}”…", {profile})); break;
    case ClearDataEvent::kCleared: SetPreferencesStatus(TF(u"Wyczyszczono dane profilu „{}”.", {profile}), false); break;
    case ClearDataEvent::kFailed: SetPreferencesStatus(T(u"Nie udało się usunąć wszystkich wybranych danych. Spróbuj ponownie."), true); break;
  }
}
WorkspaceShell::ClearDataCopy WorkspaceShell::ClearDataConfirmCopy(const std::u16string& profile, int period, bool history, bool cookies, bool cache) const {
  ClearDataCopy out;
  if (metal_sidebar_) {
    const auto copy = sidebar::clear_data_confirm(base::UTF16ToUTF8(profile), period, history, cookies, cache);
    out.valid = copy.valid; out.title = base::UTF8ToUTF16(std::string(copy.title)); out.description = base::UTF8ToUTF16(std::string(copy.description));
    out.confirm = base::UTF8ToUTF16(std::string(copy.confirm)); out.cancel = base::UTF8ToUTF16(std::string(copy.cancel));
    return out;
  }
  if (period < 0 || period > 3 || (!history && !cookies && !cache)) return out;
  const std::array<std::u16string, 4> periods = {T(u"Ostatnia godzina"), T(u"Ostatnie 24 godziny"), T(u"Ostatnie 7 dni"), T(u"Cały okres")};
  out.description = TF(u"Profil: {}\nZakres: {}\n\nZostaną usunięte:", {profile, periods[period]});
  if (history) out.description += u"\n• " + T(u"Historia przeglądania");
  if (cookies) out.description += u"\n• " + T(u"Cookies i dane witryn — może to wylogować Cię z serwisów");
  if (cache) out.description += u"\n• " + T(u"Pamięć podręczna");
  out.description += u"\n\n" + T(u"Tej operacji nie można cofnąć. Hasła, przypięte karty i pobrane pliki pozostaną zachowane.");
  out.valid = true; out.title = T(u"Wyczyścić dane przeglądania?"); out.confirm = T(u"Wyczyść dane"); out.cancel = T(u"Anuluj");
  return out;
}
void WorkspaceShell::SetProfileStatus(ProfileStatusEvent event) {
  const std::array<std::u16string, 11> kViewsCopy = {
      T(u"Profil o tej nazwie już istnieje."),
      T(u"Tworzenie profilu…"),
      T(u"Nie udało się utworzyć profilu. Spróbuj ponownie."),
      T(u"Profil utworzony. Możesz przypisać go do przestrzeni."),
      T(u"Nie udało się zapisać profilu. Profil nie został dodany. Spróbuj ponownie."),
      T(u"Nazwa profilu zapisana."),
      T(u"Nie udało się zapisać nazwy profilu."),
      T(u"Usuwanie profilu…"),
      T(u"Profil usunięty. Dane przeglądania zostaną wyczyszczone."),
      T(u"Nie udało się zapisać zmian. Profil nie został usunięty."),
      T(u"Nie udało się wczytać profili. Uruchom aplikację ponownie.")};
  const auto index = static_cast<size_t>(event);
  std::u16string text(kViewsCopy.at(index));
  if (profile_form_) text = base::UTF8ToUTF16(std::string(sidebar::profile_status_copy(sidebar::ProfileStatusEvent{static_cast<uint8_t>(index)})));
  SetProfileStatus(std::move(text));
}
void WorkspaceShell::SetProfileStatus(std::u16string message) {
  if(!profile_status_)return;
  if(profile_form_)profile_form_->form->set_status(base::UTF16ToUTF8(message));
  // A new profile reports under its field; row actions report under their row.
  auto* active=last_profile_action_==0?profile_create_status_.get():profile_status_.get();
  auto* idle=active==profile_status_.get()?profile_create_status_.get():profile_status_.get();
  SetPanelText(active,message);active->SetVisible(!message.empty());
  SetPanelText(idle,u"");idle->SetVisible(false);
  LayoutProfileStrips();
}
sidebar::ProfileControls WorkspaceShell::CurrentProfileControls(){
  const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
  return profile_form_->form->controls(state_.configuration_ready,state_.profiles_busy,rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));
}
void WorkspaceShell::RefreshProfileControls() {
  if(!profile_form_||!profile_list_)return;
  const auto controls=CurrentProfileControls();
  create_profile_->SetEnabled(controls.create);rename_profile_->SetEnabled(controls.rename);delete_profile_->SetEnabled(controls.edit);
  for(size_t i=0;i<profile_rows_.size();++i){
    auto* line=profile_rows_[i].get();if(line->children().size()<3)continue;
    bool used=false;for(const auto& w:state_.workspaces)used=used||w.profile_id==profile_row_ids_[i];
    line->children()[0]->SetEnabled(controls.edit);line->children()[1]->SetEnabled(controls.rename);line->children()[2]->SetEnabled(controls.edit);
    // A profile still in use keeps its trash enabled (it explains why) but dimmed.
    SetActionColor(line->children()[2].get(),used?kSubtle:kMuted);SetActionColor(line->children()[1].get(),kMuted);
  }
  if(!controls.rename)renaming_profile_id_.clear();
  SetProfileStatus(base::UTF8ToUTF16(std::string(controls.status)));
}
bool WorkspaceShell::SubmitRustProfileAction(unsigned action) {
  if(!profile_form_)return false;
  auto* field=action==1?profile_rename_input_.get():profile_name_input_.get();
  if(field->IsIMEComposing())return true;
  last_profile_action_=action;
  if(action==1&&renaming_profile_id_.empty())return true;
  const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
  const auto request=profile_form_->form->request(action,action==2?std::string():base::UTF16ToUTF8(field->GetText()),state_.configuration_ready,state_.profiles_busy,rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));
  if(request.action==sidebar::ProfileAction::None){const auto status=base::UTF8ToUTF16(std::string(profile_form_->form->status()));SetProfileStatus(status);RefreshProfileControls();return true;}
  if(request.action==sidebar::ProfileAction::ConfirmDelete){SetProfileStatus(u"");RefreshProfileControls();return true;}
  const auto alive=weak_factory_.GetWeakPtr();bool accepted=false;
  switch(request.action){
    case sidebar::ProfileAction::Create:accepted=callbacks_.create_profile.Run(std::string(request.name),std::string(request.workspace));break;
    case sidebar::ProfileAction::Rename:accepted=callbacks_.rename_profile.Run(std::string(request.id),std::string(request.name));break;
    case sidebar::ProfileAction::Delete:accepted=callbacks_.remove_profile.Run(std::string(request.id));break;
    default:break;
  }
  if(!alive||!profile_form_)return true;profile_form_->form->finish_attempt(accepted);
  if(accepted&&request.action==sidebar::ProfileAction::Create)profile_name_input_->SetText(u"");
  if(accepted&&request.action==sidebar::ProfileAction::Rename)renaming_profile_id_.clear();
  const auto status=base::UTF8ToUTF16(std::string(profile_form_->form->status()));SetProfileStatus(status);RefreshProfileControls();return true;
}
bool WorkspaceShell::profile_panel_contains_scene_text(std::string_view text) const {
  if(!profiles_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* row=views::AsViewClass<RustRowView>(view))if(base::UTF16ToUTF8(row->preview_spec().title)==text||base::UTF16ToUTF8(row->preview_spec().detail)==text)return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,profiles_root_);
}
void WorkspaceShell::SubmitProfile() {SubmitRustProfileAction(0);}
void WorkspaceShell::RenameSelectedProfile() {SubmitRustProfileAction(1);}
void WorkspaceShell::DeleteSelectedProfile() {SubmitRustProfileAction(2);}
bool WorkspaceShell::appearance_metal_visible(bool emoji) const {
  auto* view=emoji?(emoji_input_?emoji_input_->view()->parent():nullptr):color_presets_.get();auto* row=views::AsViewClass<AppearanceRow>(view);return row&&row->metal_visible();
}
bool WorkspaceShell::appearance_contains_scene_text(std::string_view text) const {
  auto* row=emoji_input_?views::AsViewClass<AppearanceRow>(emoji_input_->view()->parent()):nullptr;return row&&row->contains_text(text);
}
gfx::Rect WorkspaceShell::appearance_scene_bounds_for_testing(bool emoji) const {
  auto* view=emoji?(emoji_input_?emoji_input_->view()->parent():nullptr):color_presets_.get();auto* row=views::AsViewClass<AppearanceRow>(view);return row?row->scene_bounds():gfx::Rect();
}
bool WorkspaceShell::form_actions_contains_scene_text(bool profiles,std::string_view text) const {
  auto* root=profiles?profiles_root_.get():settings_root_.get();if(!root)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,root);
}
size_t WorkspaceShell::form_action_canvases_visible_for_testing(bool profiles) const {
  auto* root=profiles?profiles_root_.get():settings_root_.get();if(!root)return 0;auto count=[&](auto&& self,views::View* view)->size_t{size_t n=0;if(auto* group=views::AsViewClass<SceneActionGroup>(view))n+=group->metal_visible();for(const auto& child:view->children())n+=self(self,child.get());return n;};return count(count,root);
}
bool WorkspaceShell::HandleAppearanceEditorKey(bool emoji,const ui::KeyEvent& event) {
  auto* field=emoji?emoji_input_.get():nullptr;
  if(!field||event.type()!=ui::EventType::kKeyPressed||field->IsIMEComposing())return false;
  if(event.key_code()==ui::VKEY_ESCAPE){CloseOverlays();RestorePageFocus();return true;}
  if(event.key_code()!=ui::VKEY_RETURN)return false;
  SetWorkspaceEmoji(state_.workspace_id,std::u16string(field->GetText()));
  return true;
}
void WorkspaceShell::SetPaletteErrorText(std::u16string text) {
  if(!address_error_)return;
  if(palette_root_&&static_cast<PaletteView*>(palette_root_.get())->scene()){static_cast<PaletteText*>(address_error_.get())->SetText(std::move(text));return;}
  static_cast<views::Label*>(address_error_.get())->SetText(text);
}
void WorkspaceShell::ShowAddressError() {
  SetPaletteErrorText(T(u"Wpisz adres strony HTTP lub HTTPS"));
  address_error_->SetVisible(true);
  PositionPanels();
}
void WorkspaceShell::RunSuggestion(size_t index) {
  if (index >= suggestions_.size()) return;
  const auto item = suggestions_[index];
  if (!item.command_id.empty()) {
    CloseOverlays(true);
    RestorePageFocus();
    // Panel-opening and tab mutations must outlive the row callback.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, PaletteResult command) {
          if (shell && shell->callbacks_.activate_result) shell->callbacks_.activate_result.Run(std::move(command));
        }, weak_factory_.GetWeakPtr(), item));
    return;
  }
  if (!item.workspace_id.empty()) {
    callbacks_.activate_result.Run(item);
    return;
  }
  if (item.tab) {
    callbacks_.select_tab.Run(*item.tab);
    return;
  }
  if (item.url.is_valid() && callbacks_.navigate.Run(item.url, new_tab_)) {
    CloseOverlays();
    RestorePageFocus();
  } else {
    ShowAddressError();
  }
}
void WorkspaceShell::ToolsEditorEdited(EditorControl* sender) {
  if(!tools_widget_||!tools_widget_->IsVisible()||sender->IsIMEComposing())return;
  if(sender==history_search_){
    const auto query=base::TrimWhitespace(sender->GetText(),base::TRIM_ALL);
    if(tools_form_){rust::Vec<uint16_t> units;for(char16_t unit:query)units.push_back(unit);tools_form_->form->tools_history_edit(std::move(units));}
    history_entries_.clear();
    if(history_node_graph_)history_node_graph_->graph->reconcile({});
    if(history_list_)history_list_->RemoveAllChildViews();
    SetPanelText(tools_status_,ToolsStatusText(1));
    if(history_more_)history_more_->SetEnabled(false);
    history_timer_.Start(FROM_HERE,base::Milliseconds(150),base::BindOnce(&WorkspaceShell::RefreshHistory,weak_factory_.GetWeakPtr(),false));
  }else if(sender==timeline_filter_){
    const auto query=base::i18n::ToLower(sender->GetText());
    if(tools_form_){rust::Vec<uint16_t> units;for(char16_t unit:query)units.push_back(unit);tools_form_->form->tools_timeline_edit(std::move(units));}
    RefreshTimelineItems();
  }
}
void WorkspaceShell::AddressEdited() {
  if(!address_visible()||address_->IsIMEComposing())return;
  address_error_->SetVisible(false);
  static_cast<PaletteView*>(palette_root_.get())->Reset();
  search_results_.clear();RebuildSuggestions();RequestSearch();
}

void WorkspaceShell::RequestSearch() {
  if (!callbacks_.search) return;
  search_query_ = base::TrimWhitespace(address_->GetText(), base::TRIM_ALL);
  SetPaletteLoading(!search_query_.empty());
  search_timer_.Start(FROM_HERE, base::Milliseconds(120), base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, std::u16string query) {
        if (shell && shell->address_visible()&&!shell->address_->IsIMEComposing()&&base::TrimWhitespace(shell->address_->GetText(),base::TRIM_ALL)==query) shell->callbacks_.search.Run(query);
      }, weak_factory_.GetWeakPtr(), search_query_));
}
void WorkspaceShell::SetPaletteLoading(bool loading) {
  if (auto* icon = views::AsViewClass<SceneSearchIcon>(palette_chrome_views_[0].get())) icon->SetLoading(loading);
}
bool WorkspaceShell::palette_loading_for_testing() const {
  auto* icon = views::AsViewClass<SceneSearchIcon>(palette_chrome_views_[0].get());
  return icon && icon->loading();
}
void WorkspaceShell::SetSearchResults(std::u16string query, std::string profile_id, std::vector<PaletteResult> results, bool complete) {
  if (!address_visible() || address_->IsIMEComposing() || profile_id != state_.profile_id ||
      base::TrimWhitespace(address_->GetText(), base::TRIM_ALL) != query) return;
  if (complete) SetPaletteLoading(false);
  search_results_ = std::move(results);
  RebuildSuggestions();
}
bool WorkspaceShell::HandleToolsEditorKey(EditorControl* sender,const ui::KeyEvent& event) {
  if(!tools_widget_||!tools_widget_->IsVisible()||!sender->view()->GetEnabledInViewsSubtree()||event.type()!=ui::EventType::kKeyPressed)return false;
  if(sender==shortcut_chord_&&ToolsRecording())
    return CaptureShortcut(ui::Accelerator(event.key_code(),ui::Accelerator::MaskOutKeyEventFlags(event.flags())));
  if(sender->IsIMEComposing())return false;
  if(event.key_code()==ui::VKEY_ESCAPE){CloseOverlays();RestorePageFocus();return true;}
  if(event.key_code()==ui::VKEY_RETURN){
    const int action=sender==snapshot_name_?1:sender==shortcut_chord_?11:
        (sender==provider_name_||sender==provider_keyword_||sender==provider_url_)?20:0;
    if(action){PostToolsAction(action);return true;}
  }
  return false;
}
bool WorkspaceShell::HandlePreferenceEditorKey(EditorControl* sender,const ui::KeyEvent& event) {
  if(!general_widget_->IsVisible()||!sender->view()->GetEnabledInViewsSubtree()||
      sender->IsIMEComposing()||event.type()!=ui::EventType::kKeyPressed)return false;
  if(event.key_code()==ui::VKEY_ESCAPE){CloseOverlays();RestorePageFocus();return true;}
  if(event.key_code()!=ui::VKEY_RETURN)return false;
  for(const auto& [key,field]:preference_textfields_)if(sender==field){SaveTextPreference(key);return true;}
  if(sender==site_origin_){SaveSitePreference();return true;}
  return false;
}
bool WorkspaceShell::HandleAddressKey(const ui::KeyEvent& event) {
  if(!address_visible()||event.type()!=ui::EventType::kKeyPressed||address_->IsIMEComposing())return false;
  if(event.key_code()==ui::VKEY_ESCAPE){CloseOverlays();RestorePageFocus();return true;}
  if (!suggestions_.empty() &&
      (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP)) {
    static_cast<PaletteView*>(palette_root_.get())->Step(suggestions_.size(),event.key_code()==ui::VKEY_UP);
    RebuildSuggestions();
    return true;
  }
  if (event.key_code() != ui::VKEY_RETURN) return false;
  // SetText() does not notify the controller; resolve what is typed now.
  if (address_->GetText() != suggestions_text_) RebuildSuggestions();
  if (base::TrimWhitespace(address_->GetText(), base::TRIM_ALL).empty() && suggestions_.empty())
    return true;
  if (suggestions_.empty()) {
    if (command_palette_ && base::TrimWhitespace(address_->GetText(), base::TRIM_ALL).starts_with(u">")) {
      SetPaletteErrorText(T(u"Brak pasujących poleceń.")); address_error_->SetVisible(true); PositionPanels();
    } else ShowAddressError();
    return true;
  }
  RunSuggestion(static_cast<PaletteView*>(palette_root_.get())->selected());
  return true;
}
void WorkspaceShell::OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect&) {
  // Screen-space GPU clipping belongs to the captured viewport. Reflow restores
  // live content instead of leaving an overlay at old window/sidebar bounds.
  if(workspace_switch_overlay_&&(widget==widget_||widget==sidebar_widget_.get()))StopWorkspaceAnimation();
  if(widget==address_widget_.get())ScheduleMetalPalette();
  if (widget == widget_) {PositionPanels();ScheduleMetalHeader();ScheduleMetalSplit();}
}
void WorkspaceShell::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  if (active) return;
  if (widget == address_widget_.get()) address_widget_->Hide();
  if (widget == settings_widget_.get()) settings_widget_->Hide();
  if(widget==sidebar_widget_.get()) {
    bool hide=!pinned_&&!settings_visible();
    if(metal_sidebar_)hide=sidebar::sidebar_auto_hide(pinned_,settings_visible(),false,false);
    if(hide)sidebar_widget_->Hide();
  }
}
void WorkspaceShell::OnWidgetVisibilityChanged(views::Widget* widget, bool visible) {
  if(widget==address_widget_.get())ScheduleMetalPalette();
  if(widget==address_widget_.get()&&!visible&&scrim_widget_)scrim_widget_->Hide();
  if(widget==address_widget_.get()&&!visible)SetPaletteLoading(false);
  if(widget==address_widget_.get()&&!visible&&url_pill_){SetRowEditing(url_pill_,false);ScheduleMetalSidebar();}
  if (widget==widget_) {ScheduleMetalHeader();ScheduleMetalSplit();}
  if (widget == sidebar_widget_.get() && !visible) { CancelWorkspaceSwipe(); StopWorkspaceAnimation(); }
  if (widget == widget_ && !visible) {
    HidePeek();
    address_widget_->Hide(); sidebar_widget_->Hide(); settings_widget_->Hide();
    if (profiles_widget_) profiles_widget_->Hide();
    if (import_widget_) import_widget_->Hide();
    if (general_widget_) general_widget_->Hide();
    if (tools_widget_) tools_widget_->Hide();
  } else if (widget == widget_ && visible && pinned_) ShowSidebar();
}
}
