#include "base/i18n/number_formatting.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/mouse_constants.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view_tracker.h"
#include "ui/views/window/dialog_client_view.h"
#include "chromium/prototype/page_dialogs.h"
#include "base/i18n/time_formatting.h"
#include "chromium/prototype/workspace_shell.h"

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

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/workspace_frame_mac.h"
#endif

namespace zitter::prototype {
class ImportFormState {
 public:
#if BUILDFLAG(IS_MAC)
  rust::Box<sidebar::ImportForm> form=sidebar::new_import_form();
#endif
};
class WorkspaceFormState {
 public:
#if BUILDFLAG(IS_MAC)
  rust::Box<sidebar::WorkspaceForm> form=sidebar::new_workspace_form();
#endif
};
class ToolsFormState {
 public:
#if BUILDFLAG(IS_MAC)
  rust::Box<sidebar::ToolsForm> form=sidebar::new_tools_form();
#endif
};
class PreferencesFormState {
 public:
#if BUILDFLAG(IS_MAC)
  explicit PreferencesFormState(size_t categories):form(sidebar::new_preferences_form(categories)){}
  rust::Box<sidebar::PreferencesForm> form;
#else
  explicit PreferencesFormState(size_t){}
#endif
};
class ProfileFormState {
 public:
#if BUILDFLAG(IS_MAC)
  rust::Box<sidebar::ProfileForm> form=sidebar::new_profile_form();
#endif
};
namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFolderNameField);
// Light "frame + card" palette from the Zitter design canvas.
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
constexpr std::array<SkColor, 4> kWorkspaceColors = {
    0xFF9E91DCu, 0xFF84B6A4u, 0xFFD6A07Bu, 0xFF7FAACBu};
constexpr std::array<const char16_t*, 6> kWorkspaceEmojis = {
    u"💼", u"🏠", u"🚀", u"🎨", u"📚", u"🎮"};
constexpr int kGap = 8;
constexpr int kPaneBarHeight = 30;
#if BUILDFLAG(IS_MAC)
constexpr int kTrafficLightInset = 78;
#else
constexpr int kTrafficLightInset = 8;
#endif
constexpr std::array<SkColor, 7> kMonogramColors = {
    0xFF4F46E5, 0xFF2F6F5E, 0xFFB4532A, 0xFF3A6EA5,
    0xFF7A5C2E, 0xFF6B4FA0, 0xFF2E7D4F};

bool IsDark(const views::View* view) {
  return view && view->GetWidget() && view->GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark;
}
SkColor Tone(const views::View* view, SkColor light) {
  if (!IsDark(view)) return light;
  switch (light) {
    case kInk: return 0xFFF0EEF5;
    case kSubtle: return 0xFFD0CCD9;
    case kMuted: return 0xFFAAA5B8;
    case kDisabled: return 0xFF686373;
    case kCard: return 0xFF24212D;
    case kAccentWash: return 0xFF393249;
    case kChip: return 0xFF302C3B;
    case kFrameBase: return 0xFF19171F;
    case kError: return 0xFFFFB4AB;
    default: return light;
  }
}
SkColor Hairline(const views::View* view) { return SkColorSetA(Tone(view, kInk), IsDark(view) ? 0x30 : 0x17); }
SkColor HoverWash(const views::View* view) { return SkColorSetA(Tone(view, kInk), 0x0D); }
SkColor Mix(SkColor a, SkColor b, int percent_a) {
  auto channel = [&](U8CPU x, U8CPU y) { return (x * percent_a + y * (100 - percent_a)) / 100; };
  return SkColorSetRGB(channel(SkColorGetR(a), SkColorGetR(b)),
                       channel(SkColorGetG(a), SkColorGetG(b)),
                       channel(SkColorGetB(a), SkColorGetB(b)));
}
// The workspace accent tints the whole window frame, Arc-style.
SkColor FrameTint(SkColor accent, const views::View* view) { return Mix(accent, Tone(view, kFrameBase), IsDark(view) ? 10 : 14); }

std::string Hex(SkColor color) { return base::StringPrintf("#%06X", color & 0xFFFFFF); }
std::optional<SkColor> ParseColor(std::string_view text) {
  unsigned value;
  if (text.size() != 7 || text[0] != '#' ||
      !base::HexStringToUInt(text.substr(1), &value)) return std::nullopt;
  return value | 0xFF000000;
}
struct Appearance {
  std::map<std::string, SkColor> colors = {{"work", 0xFF9E91DC}, {"personal", 0xFF84B6A4}};
  // Tabs live in a visible sidebar by default; auto-hide is opt-in.
  bool pinned = true;
  ThemeMode theme = ThemeMode::kSystem;
  SearchEngine search = SearchEngine::kDuckDuckGo;
  bool animations = true;
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
               std::optional<std::pair<std::string, std::string>> emoji) {
  auto appearance = ReadAppearance(path);
  if (id) appearance.colors[*id] = color;
  if (pinned) appearance.pinned = *pinned;
  if (theme) appearance.theme = *theme;
  if (search) appearance.search = *search;
  if (animations) appearance.animations = *animations;
  if (sidebar_width) appearance.sidebar_width = *sidebar_width;
  if (emoji && emoji->second.empty()) appearance.emoji.erase(emoji->first);
  else if (emoji) appearance.emoji[emoji->first] = emoji->second;
  base::DictValue dict;
  for (const auto& [key, value] : appearance.colors) dict.Set(key, Hex(value));
  dict.Set("sidebar_pinned", appearance.pinned);
  dict.Set("theme", appearance.theme == ThemeMode::kDark ? "dark" : appearance.theme == ThemeMode::kLight ? "light" : "system");
  dict.Set("search_engine", appearance.search == SearchEngine::kGoogle ? "google" : appearance.search == SearchEngine::kBing ? "bing" : "duckduckgo");
  dict.Set("workspace_animations", appearance.animations);
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
  explicit PreferenceSurface(bool navigation=false,bool scene=false,base::RepeatingClosure changed={},base::RepeatingCallback<bool()> visible={}):navigation_(navigation),scene_(scene),changed_(std::move(changed)),visible_(std::move(visible)){}
  bool scene() const{return scene_;}
#if BUILDFLAG(IS_MAC)
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const{sidebar::Item item{};item.kind=24;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.fill=Tone(this,navigation_?kFrameBase:kCard);item.ink=Hairline(this);item.active=navigation_;item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;}
#endif
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return scene_;}
  void OnVisibleBoundsChanged() override{Notify();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Notify();}
  void AddedToWidget() override{views::View::AddedToWidget();Notify();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);Notify();}
  void OnPaint(gfx::Canvas* canvas) override{views::View::OnPaint(canvas);
#if BUILDFLAG(IS_MAC)
    if(scene_&&(navigation_||!visible_||!visible_.Run())){auto item=Scene(gfx::RectF(GetLocalBounds()),gfx::RectF(GetLocalBounds()));auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));PaintEditorSceneFallback(canvas,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(ops.data(),ops.size())));}
#endif
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if(scene_){SetBackground(nullptr);SetBorder(views::CreateEmptyBorder(navigation_?gfx::Insets():gfx::Insets(1)));Notify();}
    else {SetBackground(views::CreateRoundedRectBackground(Tone(this,navigation_?kFrameBase:kCard),12));if(!navigation_)SetBorder(views::CreateRoundedRectBorder(1,12,Hairline(this)));}
  }
 private:
  void Notify(){if(changed_)changed_.Run();}
  bool navigation_;bool scene_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;
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
    const SkColor ink = view->GetEnabled() ? Tone(view, color) : Tone(view, kDisabled);
    const auto center = bounds.CenterPoint();
    switch (glyph) {
      case ControlGlyph::kVector:
        PaintIcon(canvas, *icon, bounds, 16, ink);
        break;
      case ControlGlyph::kSidebar: {
        const gfx::Rect frame(center.x() - 8, center.y() - 7, 16, 14);
        StrokeRound(canvas, frame, 3, ink);
        canvas->FillRect(gfx::Rect(frame.x() + 5, frame.y(), 1, frame.height()), ink);
        if (on) canvas->FillRect(gfx::Rect(frame.x() + 1, frame.y() + 1, 4, frame.height() - 2),
                                  SkColorSetA(ink, 0x55));
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

// Footer controls retain one native event/focus/AX node, while Rust owns
// their pointer/key state and Metal owns their steady-state painting.
class SceneGlyphButton : public GlyphButton {
  METADATA_HEADER(SceneGlyphButton,GlyphButton)
 public:
  SceneGlyphButton(PressedCallback callback, std::u16string name, Glyph glyph,
      const gfx::VectorIcon* icon, base::RepeatingCallback<bool()> scene_visible,base::RepeatingClosure painted={})
      : GlyphButton(std::move(callback),std::move(name),glyph,icon),scene_visible_(std::move(scene_visible)),painted_(std::move(painted)) {
#if BUILDFLAG(IS_MAC)
    if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")
      rust_pointer_.emplace(sidebar::new_row_pointer());
#endif
  }
  void OnDidSchedulePaint(const gfx::Rect& rect) override {GlyphButton::OnDidSchedulePaint(rect);if(painted_)painted_.Run();}
  void OnFocus() override {GlyphButton::OnFocus();SchedulePaint();}
  bool OnMousePressed(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) {
      const auto result = RustPointerEvent(sidebar::PointerPhase::Press, event);
      if (GetEnabledInViewsSubtree()) RequestFocusFromEvent();
      return ApplyRustInput(result, event);
    }
#endif
    return views::Button::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Drag, event), event);
#endif
    return views::Button::OnMouseDragged(event);
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Release, event), event); return; }
#endif
    views::Button::OnMouseReleased(event);
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Enter, event), event); return; }
#endif
    views::Button::OnMouseEntered(event);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Exit, event), event); return; }
#endif
    views::Button::OnMouseExited(event);
  }
  void OnMouseMoved(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Move, event), event); return; }
#endif
    views::Button::OnMouseMoved(event);
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput((*rust_pointer_)->key_event(true, RustKeyPolicy(event), GetEnabledInViewsSubtree()), event);
#endif
    return views::Button::OnKeyPressed(event);
  }
  bool OnKeyReleased(const ui::KeyEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput((*rust_pointer_)->key_event(false, RustKeyPolicy(event), GetEnabledInViewsSubtree()), event);
#endif
    return views::Button::OnKeyReleased(event);
  }
  void OnBlur() override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) (*rust_pointer_)->cancel();
#endif
    views::Button::OnBlur();SchedulePaint();
  }
  void OnMouseCaptureLost() override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) (*rust_pointer_)->cancel();
#endif
    views::Button::OnMouseCaptureLost();
  }
  void StateChanged(ButtonState state) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_ && GetState()==STATE_DISABLED) (*rust_pointer_)->cancel();
#endif
    GlyphButton::StateChanged(state);
  }
  void PaintChildren(const views::PaintInfo& info) override {
    if (scene_visible_ && scene_visible_.Run()) return;
    views::Button::PaintChildren(info);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (scene_visible_ && scene_visible_.Run()) return;
    GlyphButton::PaintButtonContents(canvas);
  }
 protected:
  void NotifyClick(const ui::Event& event) override {if(GetEnabledInViewsSubtree()&&SceneIsVisible(this))GlyphButton::NotifyClick(event);}
 private:
#if BUILDFLAG(IS_MAC)
  sidebar::RowInputResult RustPointerEvent(sidebar::PointerPhase phase, const ui::MouseEvent& event) {
    return (*rust_pointer_)->pointer_event(phase, event.x(), event.y(),
        phase != sidebar::PointerPhase::Exit && HitTestPoint(event.location()),
        GetEnabledInViewsSubtree(), event.IsLeftMouseButton(), false);
  }
  uint32_t RustKeyPolicy(const ui::KeyEvent& event) {
    const uint32_t key = event.key_code() == ui::VKEY_SPACE ? 1 :
        event.key_code() == ui::VKEY_RETURN ? 2 : 0;
    return sidebar::key_policy(key, true);  // This adapter is macOS-only.
  }
  bool ApplyRustInput(const sidebar::RowInputResult& result, const ui::Event& event) {
    switch (result.visual) {
      case sidebar::RowVisual::Normal: SetState(STATE_NORMAL); break;
      case sidebar::RowVisual::Hovered: SetState(STATE_HOVERED); break;
      case sidebar::RowVisual::Pressed: SetState(STATE_PRESSED); break;
      case sidebar::RowVisual::Disabled: SetState(STATE_DISABLED); break;
    }
    const bool handled = result.handled;
    // Actions may delete this row or its owning palette. Do not touch members
    // after calling a controller, including the native accessibility action.
    switch (result.action) {
      case sidebar::RowAction::Activate: NotifyClick(event); return handled;
      case sidebar::RowAction::Drop: return handled;
      case sidebar::RowAction::Cancel: OnClickCanceled(event); return handled;
      case sidebar::RowAction::None: return handled;
    }
    return handled;
  }
  std::optional<rust::Box<sidebar::RowPointer>> rust_pointer_;
#endif
  base::RepeatingCallback<bool()> scene_visible_;
  base::RepeatingClosure painted_;
};

BEGIN_METADATA(SceneGlyphButton)
END_METADATA

#if BUILDFLAG(IS_MAC)
sidebar::Item GlyphSceneData(views::View* view,ControlGlyph glyph,const gfx::VectorIcon* icon,bool on,const std::u16string& label,SkColor color,bool hovered,bool focused,gfx::RectF rect,gfx::RectF clip){
  sidebar::Item item{}; item.kind=7;
  item.x=rect.x(); item.y=rect.y(); item.width=rect.width(); item.height=rect.height();
  item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
  item.glyph=glyph==GlyphButton::Glyph::kSidebar ? 0 : glyph==GlyphButton::Glyph::kDot ? 1 : glyph==GlyphButton::Glyph::kMore ? 6
      : icon==&vector_icons::kArrowBackIcon ? 3 : icon==&vector_icons::kArrowForwardIcon ? 4
      : icon==&vector_icons::kReloadCustomIcon ? 5 : icon==&vector_icons::kCloseSmallIcon ? 7 : icon==&vector_icons::kSearchIcon ? 8 : 2;
  item.active=on; item.focused=focused;
  item.hovered=hovered;
  item.ink=Tone(view,!view->GetEnabledInViewsSubtree() ? kDisabled : item.glyph==1 ? item.active || item.hovered ? kInk : kMuted : kSubtle);
  item.monogram=base::UTF16ToUTF8(label); item.font_size=Font(3).GetFontSize();
  item.fill=item.glyph==1 ? item.active && !item.monogram.empty() ? SkColorSetA(Tone(view,kInk),0x1f) : item.hovered ? HoverWash(view) : SK_ColorTRANSPARENT
      : item.hovered ? HoverWash(view) : SK_ColorTRANSPARENT;
  if(glyph==GlyphButton::Glyph::kSwatch){item.kind=18;item.badge=color;item.ink=Tone(view,!view->GetEnabledInViewsSubtree()?kDisabled:item.active?kInk:kMuted);}
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
      auto text=base::UTF8ToUTF16(std::string(op.text));auto text_font=font;
      if(styled_text){text_font=font.DeriveWithSizeDelta(std::max(1,static_cast<int>(op.font_size))-font.GetFontSize());text=gfx::ElideText(text,text_font,op.width,op.middle?gfx::ELIDE_MIDDLE:gfx::ELIDE_TAIL);}
      canvas->DrawStringRectWithFlags(text,text_font,op.color,gfx::Rect(op.x,op.y,op.width,op.height),op.center?gfx::Canvas::TEXT_ALIGN_CENTER:gfx::Canvas::TEXT_ALIGN_LEFT);
    }
    else {cc::PaintFlags flags;flags.setAntiAlias(true);flags.setColor(op.color);if(op.stroke>0){flags.setStyle(cc::PaintFlags::kStroke_Style);flags.setStrokeWidth(op.stroke);}canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);}
  }
}
#endif

#if BUILDFLAG(IS_MAC)
// A decorative image has no action controller, capture, focus ring or input state.
class SceneSearchIcon final : public views::View {
  METADATA_HEADER(SceneSearchIcon,views::View)
 public:
  explicit SceneSearchIcon(base::RepeatingCallback<bool()> visible):visible_(std::move(visible)) {
    SetTooltipText(u"Szukaj");GetViewAccessibility().SetName(u"Szukaj");GetViewAccessibility().SetRole(ax::mojom::Role::kImage);
    SetFocusBehavior(FocusBehavior::NEVER);SetCanProcessEventsWithinSubtree(false);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {const auto r=sidebar::action_preferred_size(false,false);return gfx::Size(r.width,r.height);}
  sidebar::Item SceneItem(gfx::RectF bounds,gfx::RectF clip) {return GlyphSceneData(this,ControlGlyph::kVector,&vector_icons::kSearchIcon,false,{},kSubtle,false,false,bounds,clip);}
  void OnPaint(gfx::Canvas* canvas) override {if(visible_&&visible_.Run())return;PaintGlyphControl(this,ControlGlyph::kVector,&vector_icons::kSearchIcon,kSubtle,false,{},views::Button::STATE_NORMAL,canvas);}
 private:
  base::RepeatingCallback<bool()> visible_;
};
BEGIN_METADATA(SceneSearchIcon)
END_METADATA

// Event/focus/AX delivery remains native; state and activation live in Rust.
class RustActionView : public views::View {
  METADATA_HEADER(RustActionView,views::View)
 public:
  RustActionView(views::Button::PressedCallback callback,std::u16string name,ControlGlyph glyph,const gfx::VectorIcon* icon,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,bool native_focus_ring)
      :callback_(std::move(callback)),glyph_(glyph),icon_(icon),visible_(std::move(visible)),changed_(std::move(changed)){
    SetTooltipText(name);auto& ax=GetViewAccessibility();ax.SetName(name);ax.SetRole(ax::mojom::Role::kButton);ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);SetFocusBehavior(views::PlatformStyle::kDefaultFocusBehavior);if(native_focus_ring)views::FocusRing::Install(this);
  }
  void OnTooltipTextChanged(const std::u16string& previous) override{views::View::OnTooltipTextChanged(previous);auto& ax=GetViewAccessibility();if((ax.GetCachedName().empty()||ax.GetCachedName()==previous)&&!GetTooltipText().empty())ax.SetName(GetTooltipText());}
  sidebar::ActionSnapshot Snapshot() const{return state_->action_snapshot();}
  void SetActivationOnPress(bool value){if(state_->action_on_press(value))SchedulePaint();}
  void SetDefault(bool value){if(!state_->action_default(value))return;GetViewAccessibility().SetIsDefault(value);const ui::Accelerator enter(ui::VKEY_RETURN,ui::EF_NONE);if(value)AddAccelerator(enter);else RemoveAccelerator(enter);}
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override{return sidebar::key_policy(event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0,true)!=0;}
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override{SyncEnabled();const auto before=Snapshot().visual;const bool pending=Pending();return Apply(state_->action_accelerator(accelerator.key_code()==ui::VKEY_RETURN?2:0,accelerator.modifiers()!=ui::EF_NONE,SceneIsVisible(this)),accelerator.ToKeyEvent(),before,pending);}
  void SetColor(SkColor color){if(color_==color)return;color_=color;SchedulePaint();}
  void SetCallback(views::Button::PressedCallback callback){Cancel();callback_=std::move(callback);}
  void CancelInput(){Cancel();}
  bool Pending() const{return state_->action_pending();}
  void SetOn(bool on){if(state_->action_on(on))SchedulePaint();}
  void SetLabel(std::u16string label){if(label_==label)return;label_=std::move(label);PreferredSizeChanged();SchedulePaint();}
  void SyncViewportVisibility(){if(!Interactive())Cancel();}
  bool HandleAccessibleAction(const ui::AXActionData& action) override{if(action.action==ax::mojom::Action::kDoDefault&&SceneIsVisible(this)&&GetEnabledInViewsSubtree())ScrollViewToVisible();return views::View::HandleAccessibleAction(action);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{const auto r=sidebar::action_preferred_size(glyph_==ControlGlyph::kDot,!label_.empty());return gfx::Size(r.width,r.height);}
  sidebar::Item SceneItem(gfx::RectF rect,gfx::RectF clip){SyncEnabled();const auto s=state_->action_snapshot();return GlyphSceneData(this,glyph_,icon_,s.on,label_,color_,s.enabled&&(s.visual==sidebar::RowVisual::Hovered||s.visual==sidebar::RowVisual::Pressed),s.focused,rect,clip);}
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
  void OnPaint(gfx::Canvas* canvas) override{if(visible_&&visible_.Run())return;SyncEnabled();const auto v=state_->action_snapshot();const auto paint=v.visual==sidebar::RowVisual::Disabled?views::Button::STATE_DISABLED:v.visual==sidebar::RowVisual::Pressed?views::Button::STATE_PRESSED:v.visual==sidebar::RowVisual::Hovered?views::Button::STATE_HOVERED:views::Button::STATE_NORMAL;PaintGlyphControl(this,glyph_,icon_,color_,v.on,label_,paint,canvas);}
 protected:
  virtual ax::mojom::DefaultActionVerb ActionVerb() const{return ax::mojom::DefaultActionVerb::kPress;}
  virtual void ActivateAction(const ui::Event& event){const auto id=GetProperty(views::kElementIdentifierKey);if(id){views::ViewTracker alive(this);views::ElementTrackerViews::GetInstance()->NotifyViewActivated(id,this);if(!alive.view())return;}if(callback_)callback_.Run(event);}
 private:
  void SyncEnabled(){const bool enabled=GetEnabledInViewsSubtree();if(!state_->action_enabled(enabled))return;auto& ax=GetViewAccessibility();if(enabled)ax.SetDefaultActionVerb(ActionVerb());else ax.RemoveDefaultActionVerb();}
  bool Interactive() const{return SceneIsVisible(this)&&!GetVisibleBounds().IsEmpty();}
  void Cancel(){if(state_->action_cancel())SchedulePaint();}
  void WindowChanged(){Cancel();state_->action_focus(HasFocus());SchedulePaint();}
  bool Pointer(sidebar::PointerPhase phase,const ui::MouseEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;const bool pending=state_->action_pending();return Apply(state_->action_pointer(phase,event.x(),event.y(),phase!=sidebar::PointerPhase::Exit&&HitTestPoint(event.location())&&GetVisibleBounds().Contains(event.location()),event.IsLeftMouseButton(),Interactive()),event,before,pending);}
  bool Key(bool pressed,const ui::KeyEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;const bool pending=state_->action_pending();const uint32_t key=event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0;return Apply(state_->action_key(pressed,key,true,Interactive()),event,before,pending);}
  bool Apply(const sidebar::RowInputResult& result,const ui::Event& event,sidebar::RowVisual before,bool pending){const bool handled=result.handled;if(result.visual!=before)SchedulePaint();else if(pending!=state_->action_pending()&&changed_)changed_.Run();if(result.action==sidebar::RowAction::Activate){ActivateAction(event);return handled;}return handled;}
  SkColor color_=kSubtle;std::u16string label_;views::Button::PressedCallback callback_;ControlGlyph glyph_;raw_ptr<const gfx::VectorIcon> icon_;base::RepeatingCallback<bool()> visible_;base::RepeatingClosure changed_;rust::Box<sidebar::ActionControl> state_=sidebar::new_action_control();SceneWindowObserver window_{base::BindRepeating(&RustActionView::WindowChanged,base::Unretained(this))};base::CallbackListSubscription enabled_changed_{AddEnabledInViewsSubtreeChangedCallback(base::BindRepeating(&RustActionView::EnabledChanged,base::Unretained(this)))};
};
BEGIN_METADATA(RustActionView)
END_METADATA
#endif
std::unique_ptr<views::View> CreateSceneAction(views::Button::PressedCallback callback,std::u16string name,ControlGlyph glyph,const gfx::VectorIcon* icon,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,bool native_focus_ring=true){
#if BUILDFLAG(IS_MAC)
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")return std::make_unique<RustActionView>(std::move(callback),std::move(name),glyph,icon,std::move(visible),std::move(changed),native_focus_ring);
#endif
  auto view=std::make_unique<SceneGlyphButton>(std::move(callback),std::move(name),glyph,icon,std::move(visible),std::move(changed));if(!native_focus_ring){view->SetInstallFocusRingOnFocus(false);view->SetHasInkDropActionOnClick(false);}return view;
}
void SetActionColor(views::View* view,SkColor color){
#if BUILDFLAG(IS_MAC)
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetColor(color);return;}
#endif
  static_cast<GlyphButton*>(view)->set_color(color);
}
bool IsGlyphAction(views::View* view){
#if BUILDFLAG(IS_MAC)
  if(views::AsViewClass<RustActionView>(view))return true;
#endif
  return views::AsViewClass<GlyphButton>(view)!=nullptr;
}

void SetActionCallback(views::View* view,views::Button::PressedCallback callback){
#if BUILDFLAG(IS_MAC)
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetCallback(std::move(callback));return;}
#endif
  static_cast<views::Button*>(view)->SetCallback(std::move(callback));
}
void CancelActionInput(views::View* view){
#if BUILDFLAG(IS_MAC)
  if(auto* control=views::AsViewClass<RustActionView>(view))control->CancelInput();
#endif
}
void SetActionOn(views::View* view,bool on){
#if BUILDFLAG(IS_MAC)
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetOn(on);return;}
#endif
  static_cast<GlyphButton*>(view)->set_on(on);
}
void SetActionLabel(views::View* view,std::u16string label){
#if BUILDFLAG(IS_MAC)
  if(auto* control=views::AsViewClass<RustActionView>(view)){control->SetLabel(std::move(label));return;}
#endif
  static_cast<GlyphButton*>(view)->set_label(std::move(label));
}
#if BUILDFLAG(IS_MAC)
sidebar::Item ActionSceneItem(views::View* view,gfx::RectF rect,gfx::RectF clip){if(auto* control=views::AsViewClass<RustActionView>(view))return control->SceneItem(rect,clip);return GlyphSceneItem(static_cast<GlyphButton*>(view),rect,clip);}
#endif

#if BUILDFLAG(IS_MAC)
// Rust lays out preset controls and the native editor separately. The canvas
// covers only preset bounds; fallback consumes the same Rust scene recipe.
class AppearanceRow final : public views::View {
  METADATA_HEADER(AppearanceRow,views::View)
 public:
  void SyncWindowVisibility(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  explicit AppearanceRow(bool emoji):emoji_(emoji) {}
  views::View* AddPicker(views::Button::PressedCallback callback,std::u16string label,GlyphButton::Glyph glyph) {
    return AddChildView(CreateSceneAction(std::move(callback),std::move(label),glyph,nullptr,
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
  size_t Count() const{return emoji_?6:4;}
  sidebar::ColumnLayout Geometry(int width,int height) const {const auto field=children().size()==Count()+2?children().back()->GetPreferredSize():gfx::Size();return sidebar::appearance_row_layout(width,height,field.width(),field.height(),emoji_);}
  gfx::Rect Controls() const {return children().size()==Count()+2?gfx::Rect(0,0,children()[Count()-1]->bounds().right(),height()):gfx::Rect();}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {
    std::vector<sidebar::Item> items;if(children().size()!=Count()+2)return items;
    for(size_t i=0;i<Count();++i){auto* button=children()[i].get();auto bounds=gfx::RectF(button->bounds());bounds.Offset(dx,dy);auto child_clip=bounds;child_clip.Intersect(clip);items.push_back(ActionSceneItem(button,bounds,child_clip));}return items;
  }
  void Schedule(){SyncViewport();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&AppearanceRow::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();auto local=GetVisibleBounds();local.Intersect(Controls());
    if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();frame_={};if(was)SchedulePaint();return;}
    if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;
    frame_=GetBoundsInScreen();frame_.Offset(local.x(),local.y());frame_.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame_.size())));
    surface_->Update(GetWidget()->GetNativeWindow(),frame_,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();
  }
  bool emoji_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;gfx::Rect frame_;SceneWindowObserver window_visibility_{base::BindRepeating(&AppearanceRow::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<AppearanceRow> weak_factory_{this};
};
BEGIN_METADATA(AppearanceRow)
END_METADATA
#endif
views::View* AppearancePicker(views::View* parent,views::Button::PressedCallback callback,std::u16string label,GlyphButton::Glyph glyph) {
#if BUILDFLAG(IS_MAC)
  if(auto* row=views::AsViewClass<AppearanceRow>(parent))return row->AddPicker(std::move(callback),std::move(label),glyph);
#endif
  return parent->AddChildView(std::make_unique<GlyphButton>(std::move(callback),std::move(label),glyph));
}

#if BUILDFLAG(IS_MAC)
class ScenePreferenceButton final : public RustActionView {
  METADATA_HEADER(ScenePreferenceButton,RustActionView)
 public:
  ScenePreferenceButton(views::Button::PressedCallback callback,std::u16string text,size_t category,bool dismiss,base::RepeatingClosure painted)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false),text_(std::move(text)),category_(category),dismiss_(dismiss){}
  void SetSelected(bool selected){SetOn(selected);}
  bool selected() const{return Snapshot().on;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return dismiss_?gfx::Size(32,32):gfx::Size(200,40);}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const{
    const auto state=Snapshot();sidebar::Item item{};item.kind=dismiss_?23:22;item.glyph=category_;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.title=base::UTF16ToUTF8(text_);item.active=state.on;item.focused=state.focused;item.tile=base::i18n::IsRTL();item.font_size=Font(0).GetFontSize();item.ink=Tone(this,state.enabled?kInk:kDisabled);item.fill=state.on?Tone(this,kCard):state.enabled&&(state.visual==sidebar::RowVisual::Hovered||state.visual==sidebar::RowVisual::Pressed)?HoverWash(this):SK_ColorTRANSPARENT;item.detail_ink=SkColorSetA(Tone(this,kInk),0x0f);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;
  }
 private:
  std::u16string text_;size_t category_;bool dismiss_;
};
BEGIN_METADATA(ScenePreferenceButton)
END_METADATA
class ScenePreferenceControls final : public views::View {
  METADATA_HEADER(ScenePreferenceControls,views::View)
 public:
  explicit ScenePreferenceControls(bool dismiss):dismiss_(dismiss){}
  views::View* AddControl(views::Button::PressedCallback callback,std::u16string title,size_t category){return AddChildView(std::make_unique<ScenePreferenceButton>(std::move(callback),std::move(title),category,dismiss_,base::BindRepeating(&ScenePreferenceControls::Schedule,weak_factory_.GetWeakPtr())));}
  size_t frame_count() const{return surface_?surface_->frame_count():0;}
  void SyncInput(){for(const auto& child:children())static_cast<ScenePreferenceButton*>(child.get())->SyncViewportVisibility();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  void CancelInput(){for(const auto& child:children())child->OnMouseCaptureLost();}
  void SyncWindowVisibility(){if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{if(GetVisibleBounds().IsEmpty())CancelInput();Schedule();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{auto g=Geometry(!dismiss_&&available.width().is_bounded()?available.width().value():-1,-1);return gfx::Size(g.width,g.height);}
  void Layout(PassKey) override{auto g=Geometry(width(),height());for(size_t i=0;i<children().size();++i){auto r=g.children[i];children()[i]->SetBounds(r.x,r.y,r.width,r.height);}Schedule();}
  void AddedToWidget() override{views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override{window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void OnBoundsChanged(const gfx::Rect& previous) override{views::View::OnBoundsChanged(previous);Schedule();}
  void OnThemeChanged() override{views::View::OnThemeChanged();SchedulePaint();Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);Schedule();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);SyncWindowVisibility();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,Font(0,item.active?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL));Schedule();}
 private:
  sidebar::ColumnLayout Geometry(int width,int height) const{std::vector<sidebar::ColumnChild> measured;for(const auto& child:children()){auto size=child->GetPreferredSize();measured.push_back({size.width(),size.height(),0,0,0,0});}return sidebar::action_group_layout(width,height,!dismiss_,dismiss_?0:6,rust::Slice<const sidebar::ColumnChild>(measured.data(),measured.size()));}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const{std::vector<sidebar::Item> items;for(const auto& child:children()){auto bounds=gfx::RectF(child->bounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);items.push_back(static_cast<ScenePreferenceButton*>(child.get())->Scene(bounds,local));}return items;}
  void Schedule(){SyncInput();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&ScenePreferenceControls::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,dismiss_?kCard:kFrameBase));if(was!=metal_visible())SchedulePaint();}
  bool dismiss_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&ScenePreferenceControls::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<ScenePreferenceControls> weak_factory_{this};
};
BEGIN_METADATA(ScenePreferenceControls)
END_METADATA
#endif

#if BUILDFLAG(IS_MAC)
class SceneTextButton final : public RustActionView {
  METADATA_HEADER(SceneTextButton,RustActionView)
 public:
  SceneTextButton(views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style,base::RepeatingClosure painted)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false),text_(std::move(text)),style_(style) {}
  void UseNativeMinimum(){native_minimum_=true;}
  gfx::Size GetMinimumSize() const override{if(!native_minimum_)return RustActionView::GetMinimumSize();const auto font=TextFont();return gfx::Size(views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_DIALOG_BUTTON_MINIMUM_WIDTH),std::max(0,views::LayoutProvider::GetControlHeightForFont(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY,font)-std::max(font.GetHeight(),views::TypographyProvider::Get().GetLineHeight(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY))));}
  void SetText(std::u16string text){if(text==text_)return;OnMouseCaptureLost();text_=std::move(text);GetViewAccessibility().SetName(text_);SetTooltipText(text_);PreferredSizeChanged();SchedulePaint();}
  static gfx::FontList TextFont(){return views::TypographyProvider::Get().GetFont(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {const auto g=Metrics(-1,-1);return gfx::Size(g.width,g.height);}
  sidebar::Item Scene(gfx::RectF bounds,gfx::RectF clip) const {
    const auto* provider=GetColorProvider();const auto state=Snapshot();const bool enabled=state.enabled;const auto font=TextFont();const bool prominent=style_==ui::ButtonStyle::kProminent;
    sidebar::Item item{};item.kind=19;item.x=bounds.x();item.y=bounds.y();item.width=bounds.width();item.height=bounds.height();item.title=base::UTF16ToUTF8(text_);item.font_size=font.GetFontSize();item.active=font.GetFontWeight()>=gfx::Font::Weight::MEDIUM;
    item.title_width=views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_BUTTON_HORIZONTAL_PADDING);item.focused=state.focused;item.hovered=enabled&&(state.visual==sidebar::RowVisual::Hovered||state.visual==sidebar::RowVisual::Pressed);
    item.ink=provider->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_BUTTON_MD,!enabled?views::style::STYLE_DISABLED:prominent?views::style::STYLE_DIALOG_BUTTON_DEFAULT:views::style::STYLE_PRIMARY));
    item.fill=style_==ui::ButtonStyle::kText?SK_ColorTRANSPARENT:provider->GetColor(prominent?!enabled?ui::kColorButtonBackgroundProminentDisabled:state.focused?ui::kColorButtonBackgroundProminentFocused:ui::kColorButtonBackgroundProminent:ui::kColorButtonBackground);
    if(state.visual==sidebar::RowVisual::Pressed)item.fill=GetNativeTheme()->GetSystemButtonPressedColor(item.fill);
    item.badge=style_==ui::ButtonStyle::kDefault?provider->GetColor(enabled?ui::kColorButtonBorder:ui::kColorButtonBorderDisabled):SK_ColorTRANSPARENT;
    item.detail_ink=provider->GetColor(prominent?ui::kColorSysStateHoverOnProminent:ui::kColorSysStateHoverOnSubtle);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;
  }
 private:
  sidebar::ActionButtonLayout Metrics(int width,int height) const {const auto font=TextFont();return sidebar::action_button_layout(width,height,gfx::GetStringWidth(text_,font),views::LayoutProvider::GetControlHeightForFont(views::style::CONTEXT_BUTTON_MD,views::style::STYLE_PRIMARY,font),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_BUTTON_HORIZONTAL_PADDING),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_DIALOG_BUTTON_MINIMUM_WIDTH));}
  std::u16string text_;ui::ButtonStyle style_;bool native_minimum_=false;
};
BEGIN_METADATA(SceneTextButton)
END_METADATA
class SceneActionGroup final : public views::View {
  METADATA_HEADER(SceneActionGroup,views::View)
 public:
  void SyncWindowVisibility(){CancelInput();if(!SceneIsVisible(this)&&surface_)surface_->Hide();Schedule();}

  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{for(const auto& child:children())if(!child->GetVisible()||child->GetVisibleBounds().IsEmpty())child->OnMouseCaptureLost();Schedule();}
  SceneActionGroup(bool vertical,int spacing,bool intrinsic=false,bool dialog=false):vertical_(vertical),spacing_(spacing),intrinsic_(intrinsic),dialog_(dialog) {}
  void Share(base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed){shared_visible_=std::move(visible);shared_changed_=std::move(changed);SetSidebarPanelLayout(this,false,gfx::Insets(),spacing_);}
  bool shared() const{return bool(shared_visible_);}
  void Append(std::vector<sidebar::Item>& items,gfx::Point origin,gfx::Rect clip) const{auto next=Items(origin.x(),origin.y(),gfx::RectF(clip));items.insert(items.end(),std::make_move_iterator(next.begin()),std::make_move_iterator(next.end()));}
  views::View* AddAction(views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style){auto* button=AddChildView(std::make_unique<SceneTextButton>(std::move(callback),std::move(text),style,base::BindRepeating(&SceneActionGroup::Schedule,weak_factory_.GetWeakPtr())));if(shared())button->UseNativeMinimum();return button;}
  bool metal_visible() const{return shared()?shared_visible_.Run():surface_&&surface_->visible();}
  gfx::Rect metal_bounds_for_testing() const{return surface_?surface_->screen_bounds_for_testing():gfx::Rect();}
  bool contains_text(std::string_view text) const{return surface_&&surface_->visible()&&surface_->contains_text_for_testing(text);}
  void CancelInput(){for(const auto& child:children())child->OnMouseCaptureLost();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override {if(shared())return views::View::CalculatePreferredSize(available);const auto g=Geometry(!intrinsic_&&available.width().is_bounded()?available.width().value():-1,-1);return gfx::Size(g.width,g.height);}
  void Layout(PassKey) override {if(shared()){LayoutSuperclass<views::View>(this);Schedule();return;}const auto g=Geometry(width(),height());size_t i=0;for(const auto& child:children())if(child->GetVisible()){const auto& r=g.children[i++];child->SetBounds(r.x,r.y,r.width,r.height);}Schedule();}
  void OnBoundsChanged(const gfx::Rect& previous) override {views::View::OnBoundsChanged(previous);Schedule();}
  void AddedToWidget() override {views::View::AddedToWidget();window_visibility_.Observe(GetWidget());Schedule();}
  void RemovedFromWidget() override {window_visibility_.Observe(nullptr);surface_.reset();views::View::RemovedFromWidget();}
  void OnThemeChanged() override {views::View::OnThemeChanged();PreferredSizeChanged();Schedule();}
  void VisibilityChanged(views::View* view,bool visible) override {views::View::VisibilityChanged(view,visible);if(!SceneIsVisible(this)){CancelInput();if(surface_)surface_->Hide();}Schedule();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {views::View::OnDidSchedulePaint(rect);Schedule();}
  void OnPaint(gfx::Canvas* canvas) override {if(!metal_visible())for(const auto& item:Items(0,0,gfx::RectF(GetLocalBounds())))PaintSceneRecipe(canvas,item,SceneTextButton::TextFont());Schedule();}
 private:
  sidebar::ColumnLayout Geometry(int width,int height) const {std::vector<sidebar::ColumnChild> measured;for(const auto& child:children())if(child->GetVisible()){const auto size=child->GetPreferredSize();measured.push_back({size.width(),size.height(),0,0,0,0});}if(dialog_){auto* provider=views::LayoutProvider::Get();const auto insets=provider->GetInsetsMetric(views::INSETS_DIALOG_BUTTON_ROW);return sidebar::dialog_actions_layout(width,height,spacing_,provider->GetDistanceMetric(views::DISTANCE_BUTTON_MAX_LINKABLE_WIDTH),insets.top(),insets.left(),insets.bottom(),insets.right(),rust::Slice<const sidebar::ColumnChild>(measured.data(),measured.size()));}return sidebar::action_group_layout(width,height,vertical_,spacing_,rust::Slice<const sidebar::ColumnChild>(measured.data(),measured.size()));}
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {std::vector<sidebar::Item> out;for(const auto& child:children())if(child->GetVisible()){auto bounds=gfx::RectF(child->bounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);out.push_back(static_cast<SceneTextButton*>(child.get())->Scene(bounds,local));}return out;}
  void Schedule(){for(const auto& child:children())static_cast<SceneTextButton*>(child.get())->SyncViewportVisibility();if(shared()){shared_changed_.Run();return;}if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneActionGroup::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,dialog_?GetColorProvider()->GetColor(ui::kColorDialogBackground):Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  base::RepeatingCallback<bool()> shared_visible_;base::RepeatingClosure shared_changed_;
  bool vertical_;int spacing_;bool intrinsic_;bool dialog_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneActionGroup::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneActionGroup> weak_factory_{this};
};
BEGIN_METADATA(SceneActionGroup)
END_METADATA
void CancelSceneActions(views::View* root){if(!root)return;if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->CancelInput();return;}for(const auto& child:root->children())CancelSceneActions(child.get());}
#endif
#if BUILDFLAG(IS_MAC)
class SceneToggle final : public RustActionView {
  METADATA_HEADER(SceneToggle,RustActionView)
 public:
  explicit SceneToggle(std::u16string name):RustActionView({},std::move(name),ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),{},false),state_(sidebar::new_toggle_state()){
    GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);RefreshAX();
  }
  bool On() const{return state_->toggle_checked();}
  void SetOn(bool value){state_->toggle_set(value);RefreshAX();Schedule();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool timer_running_for_testing() const{return timer_.IsRunning();}
  void CancelInteraction(){OnMouseCaptureLost();state_->toggle_finish();timer_.Stop();Schedule();}
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
  void OnDidSchedulePaint(const gfx::Rect& rect) override{RustActionView::OnDidSchedulePaint(rect);const auto input=Snapshot();state_->toggle_hover(input.enabled&&(input.visual==sidebar::RowVisual::Hovered||input.visual==sidebar::RowVisual::Pressed),Now(),gfx::Animation::ShouldRenderRichAnimation());Schedule();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible()){auto scene=Scene();PaintEditorSceneFallback(canvas,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())));}Schedule();}
 protected:
  void ActivateAction(const ui::Event& event) override{if(!state_->toggle_flip(GetEnabledInViewsSubtree()&&SceneIsVisible(this),Now(),gfx::Animation::ShouldRenderRichAnimation()))return;RefreshAX();Schedule();RustActionView::ActivateAction(event);}
 private:
  static uint64_t Now(){return (base::TimeTicks::Now()-base::TimeTicks()).InMilliseconds();}
  void RefreshAX(){GetViewAccessibility().SetCheckedState(On()?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);}
  rust::Vec<sidebar::Primitive> Scene() const{
    const auto* colors=GetColorProvider();const auto input=Snapshot();const bool enabled=input.enabled;sidebar::ToggleStyle s{};s.width=width();s.height=height();s.checked=On();s.pressed=input.visual==sidebar::RowVisual::Pressed;s.focused=input.focused;s.rtl=base::i18n::IsRTL();s.progress=state_->toggle_progress(Now());s.hover=state_->toggle_hover_progress(Now());
    s.track_on=SkColorSetRGB(110,86,207);s.track_off=colors->GetColor(enabled?ui::kColorToggleButtonTrackOff:ui::kColorToggleButtonTrackOffDisabled);s.thumb_on=colors->GetColor(enabled?ui::kColorToggleButtonThumbOn:ui::kColorToggleButtonThumbOnDisabled);s.thumb_off=colors->GetColor(enabled?ui::kColorToggleButtonThumbOff:ui::kColorToggleButtonThumbOffDisabled);s.thumb_hover=colors->GetColor(ui::kColorToggleButtonThumbOnHover);s.halo=colors->GetColor(s.pressed?ui::kColorToggleButtonPressed:ui::kColorToggleButtonHover);s.focus=colors->GetColor(ui::kColorFocusableBorderFocused);s.border=colors->GetColor(ui::kColorToggleButtonShadow);s.clip={0,0,float(width()),float(height())};return sidebar::toggle_scene(s);
  }
  void Schedule(){SyncViewportVisibility();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneToggle::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void Tick(){if(!metal_visible())SchedulePaint();Schedule();}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){timer_.Stop();if(surface_)surface_->Hide();return;}auto scene=Scene();for(auto& op:scene){op.x-=local.x();op.y-=local.y();op.clip={0,0,float(local.width()),float(local.height())};}if(!surface_)surface_=CreateSidebarMetal();const bool was=metal_visible();if(surface_){auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());surface_->UpdateScene(GetWidget()->GetNativeWindow(),frame,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())),Tone(this,kCard));}if(was!=metal_visible())SchedulePaint();if(state_->toggle_animating(Now())){if(!timer_.IsRunning())timer_.Start(FROM_HERE,base::Milliseconds(16),this,&SceneToggle::Tick);}else timer_.Stop();}
  rust::Box<sidebar::ToggleState> state_;base::RepeatingTimer timer_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneToggle::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneToggle> weak_factory_{this};
};
BEGIN_METADATA(SceneToggle)
END_METADATA
#endif
bool ToggleOn(views::View* control){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneToggle>(control))return scene->On();
#endif
  return static_cast<views::ToggleButton*>(control)->GetIsOn();
}
void SetToggleOn(views::View* control,bool value){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneToggle>(control)){scene->SetOn(value);return;}
#endif
  static_cast<views::ToggleButton*>(control)->SetIsOn(value);
}
views::View* PreferenceToggle(views::View* parent,std::u16string name,bool scene){
#if BUILDFLAG(IS_MAC)
  if(scene)return parent->AddChildView(std::make_unique<SceneToggle>(std::move(name)));
#endif
  auto* toggle=parent->AddChildView(std::make_unique<views::ToggleButton>());toggle->GetViewAccessibility().SetName(name);toggle->SetTrackOnColor(SkColorSetRGB(110,86,207));return toggle;
}

#if BUILDFLAG(IS_MAC)
class SceneCheckbox final : public RustActionView {
  METADATA_HEADER(SceneCheckbox,RustActionView)
 public:
  SceneCheckbox(views::Button::PressedCallback callback,std::u16string text,base::RepeatingClosure painted)
      :RustActionView(std::move(callback),text,ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),std::move(painted),false),state_(sidebar::new_checkbox_state(false)),text_(std::move(text)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kCheckBox);RefreshAX();
  }
  bool Checked() const{return state_->checked();}
  void SetChecked(bool checked){if(!state_->set_checked(checked))return;RefreshAX();SchedulePaint();}
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
  void ActivateAction(const ui::Event& event) override {if(!state_->toggle(GetEnabledInViewsSubtree()&&SceneIsVisible(this)))return;RefreshAX();SchedulePaint();RustActionView::ActivateAction(event);}
 private:
  void RefreshAX(){if(GetEnabledInViewsSubtree())GetViewAccessibility().SetDefaultActionVerb(ActionVerb());else GetViewAccessibility().RemoveDefaultActionVerb();GetViewAccessibility().SetCheckedState(Checked()?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);}
  sidebar::CheckboxLayout Geometry(int requested) const {
    const auto font=TextFont();const auto padding=views::LayoutProvider::Get()->GetInsetsMetric(views::INSETS_CHECKBOX_RADIO_BUTTON);const int width=requested<0?gfx::GetStringWidth(text_,font)+padding.width()+24:requested;const int label_width=std::max(0,width-padding.width()-24);
    if(measured_width_!=label_width||measured_font_!=font.GetFontSize()){lines_=ShapeSidebarParagraph(base::UTF16ToUTF8(text_),font.GetFontSize(),label_width,font.GetFontWeight()>=gfx::Font::Weight::MEDIUM);measured_width_=label_width;measured_font_=font.GetFontSize();}
    return sidebar::checkbox_layout(width,font.GetHeight(),lines_.size(),padding.left(),padding.top(),padding.right(),padding.bottom(),base::i18n::IsRTL());
  }
  rust::Box<sidebar::CheckboxState> state_;std::u16string text_;mutable std::vector<std::string> lines_;mutable int measured_width_=-1,measured_font_=0;
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
  views::View* AddChoice(views::Button::PressedCallback callback,std::u16string text){return AddChildView(std::make_unique<SceneCheckbox>(std::move(callback),std::move(text),base::BindRepeating(&SceneCheckboxGroup::Schedule,weak_factory_.GetWeakPtr())));}
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
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {std::vector<sidebar::Item> out;for(const auto& child:children())if(child->GetVisible()){auto bounds=gfx::RectF(child->bounds());bounds.Offset(dx,dy);auto local=bounds;local.Intersect(clip);auto items=static_cast<SceneCheckbox*>(child.get())->Scene(bounds,local);for(auto& item:items)out.push_back(std::move(item));}return out;}
  void Schedule(){for(const auto& child:children())static_cast<SceneCheckbox*>(child.get())->SyncViewportVisibility();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneCheckboxGroup::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneCheckboxGroup::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneCheckboxGroup> weak_factory_{this};
};
BEGIN_METADATA(SceneCheckboxGroup)
END_METADATA
void CancelSceneCheckboxes(views::View* root){if(!root)return;if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->CancelInput();return;}for(const auto& child:root->children())CancelSceneCheckboxes(child.get());}
#endif
#if BUILDFLAG(IS_MAC)
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

  SceneSelect(std::vector<std::u16string> labels,std::u16string name,base::RepeatingClosure changed)
      :RustActionView({},std::move(name),ControlGlyph::kMore,nullptr,base::BindRepeating([]{return true;}),{},false),labels_(std::move(labels)),state_(MakeState(labels_)),changed_(std::move(changed)),input_(this,base::BindRepeating(&SceneSelect::Input,base::Unretained(this)),base::BindRepeating(&SceneSelect::ResetPrefix,base::Unretained(this))) {
    SetActivationOnPress(true);GetViewAccessibility().SetRole(ax::mojom::Role::kComboBoxSelect);GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);GetViewAccessibility().SetDefaultActionVerb(ActionVerb());RefreshAX();
  }
  ~SceneSelect() override {if(GetInputMethod())GetInputMethod()->DetachTextInputClient(&input_);}
  std::optional<size_t> Selected() const {return state_->has_selection()?std::optional<size_t>(state_->selected()):std::nullopt;}
  std::string SelectedKey() const{return std::string(state_->selected_key());}
  size_t Count() const {return labels_.size();}
  std::u16string LabelAt(size_t index) const{return index<labels_.size()?labels_[index]:std::u16string();}
  void SetSelectedIndex(std::optional<size_t> index){if(index&&*index>=Count())return;OnMouseCaptureLost();CancelMenu();if(index)state_->select_index(*index);else state_->clear_selection();RefreshAX();Schedule();}
  void SetChangedCallback(base::RepeatingClosure changed){changed_=std::move(changed);}
  void CancelInteraction(){OnMouseCaptureLost();CancelMenu();Schedule();}
  void SetOptions(const std::vector<std::pair<std::string,std::u16string>>& choices){
    bool same=choices.size()==labels_.size();
    for(size_t i=0;same&&i<choices.size();++i)same=choices[i].first==std::string(state_->option_key(i))&&choices[i].second==labels_[i];
    if(same)return;
    OnMouseCaptureLost();CancelMenu();rust::Vec<sidebar::SelectOption> options;std::vector<std::u16string> labels;
    for(const auto& [key,label]:choices){options.push_back({key,base::UTF16ToUTF8(label),base::UTF16ToUTF8(base::i18n::ToLower(label))});labels.push_back(label);}
    if(!state_->replace_options(std::move(options),false,false))return;
    labels_=std::move(labels);RefreshAX();PreferredSizeChanged();Schedule();
  }
  void SetProfiles(const std::vector<ProfileInfo>& profiles,bool fallback_last,bool select_last){
    OnMouseCaptureLost();CancelMenu();rust::Vec<sidebar::SelectOption> options;std::vector<std::u16string> labels;
    for(const auto& profile:profiles){auto label=base::UTF8ToUTF16(profile.name);options.push_back({profile.id,profile.name,base::UTF16ToUTF8(base::i18n::ToLower(label))});labels.push_back(std::move(label));}
    if(!state_->replace_options(std::move(options),fallback_last,select_last))return;
    labels_=std::move(labels);RefreshAX();PreferredSizeChanged();Schedule();
  }
  void SelectForTesting(size_t index){if(index>=Count()||!GetEnabledInViewsSubtree()||!SceneIsVisible(this))return;CancelMenu();const auto token=state_->open(true);if(!state_->choose(token,index,true))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  bool metal_visible() const{return surface_&&surface_->visible();}
  bool contains_text(std::string_view text) const{return metal_visible()&&surface_->contains_text_for_testing(text);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {int widest=0;for(const auto& text:labels_)widest=std::max(widest,gfx::GetStringWidth(text,TextFont()));const auto g=sidebar::select_layout(-1,-1,widest,views::LayoutProvider::GetControlHeightForFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY,TextFont()),false);return gfx::Size(g.width,g.height);}
  bool OnKeyPressed(const ui::KeyEvent& event) override {if(!GetEnabledInViewsSubtree())return false;if(!sidebar::select_menu_key(event.key_code()))return false;ShowMenu(ui::mojom::MenuSourceType::kKeyboard);return true;}
  bool OnKeyReleased(const ui::KeyEvent&) override {return false;}
  void OnFocus() override {RustActionView::OnFocus();if(GetInputMethod())GetInputMethod()->SetFocusedTextInputClient(&input_);}
  void OnBlur() override {if(GetInputMethod())GetInputMethod()->DetachTextInputClient(&input_);ResetPrefix();RustActionView::OnBlur();}
  bool HandleAccessibleAction(const ui::AXActionData& action) override {if(action.action==ax::mojom::Action::kDoDefault&&GetEnabledInViewsSubtree()){ShowMenu(ui::mojom::MenuSourceType::kKeyboard);return true;}return RustActionView::HandleAccessibleAction(action);}
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
  void ActivateAction(const ui::Event& event) override {if(GetEnabledInViewsSubtree()&&SceneIsVisible(this))ShowMenu(ui::GetMenuSourceTypeForEvent(event));}
 private:
  static rust::Box<sidebar::SelectState> MakeState(const std::vector<std::u16string>& labels){rust::Vec<sidebar::SelectOption> options;for(size_t i=0;i<labels.size();++i)options.push_back({base::NumberToString(i),base::UTF16ToUTF8(labels[i]),base::UTF16ToUTF8(base::i18n::ToLower(labels[i]))});return sidebar::new_select_state(std::move(options));}
  static gfx::FontList TextFont(){return views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY);}
  void ResetPrefix(){state_->reset_prefix();}
  void Input(std::u16string text){if(!SceneIsVisible(this)||!HasFocus())return;const auto lower=base::UTF16ToUTF8(base::i18n::ToLower(text));if(!state_->input(lower,(base::TimeTicks::Now()-base::TimeTicks()).InMilliseconds(),GetEnabledInViewsSubtree()))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  void RefreshAX(){const auto selected=Selected();GetViewAccessibility().SetValue(selected?labels_[*selected]:std::u16string());if(state_->expanded())GetViewAccessibility().SetIsExpanded();else GetViewAccessibility().SetIsCollapsed();}
  static uint64_t Now(){return (base::TimeTicks::Now()-base::TimeTicks()).InMicroseconds();}
  void CancelMenu(){state_->dismiss_popup(Now(),bool(runner_));ResetPrefix();RefreshAX();auto old=std::move(runner_);old.reset();}
  void Closed(uint64_t token){if(!state_->close_menu_at(token,Now()))return;runner_.reset();RefreshAX();Schedule();}
  bool CanChoose(uint64_t token,const std::string& key) const {return state_->can_choose(token,key,GetEnabledInViewsSubtree()&&SceneIsVisible(this));}
  void Choose(uint64_t token,const std::string& key){if(!state_->choose_key(token,key,GetEnabledInViewsSubtree()&&SceneIsVisible(this)))return;RefreshAX();Schedule();if(changed_)changed_.Run();}
  void ShowMenu(ui::mojom::MenuSourceType source){
    if(!GetWidget()||!SceneIsVisible(this)||!GetEnabledInViewsSubtree())return;if(GetVisibleBounds().IsEmpty())ScrollViewToVisible();if(GetVisibleBounds().IsEmpty())return;CancelMenu();const auto token=source==ui::mojom::MenuSourceType::kKeyboard?state_->open(true):state_->open_pointer(true,Now(),views::kMinimumTimeBetweenButtonClicks.InMicroseconds());if(!token)return;
    std::vector<std::pair<std::string,std::u16string>> items;for(size_t i=0;i<labels_.size();++i)items.emplace_back(std::string(state_->option_key(i)),labels_[i]);
    const auto weak=weak_factory_.GetWeakPtr();menu_=std::make_unique<SceneSelectMenu>(std::move(items),std::string(state_->selected_key()),token,base::BindRepeating([](base::WeakPtr<SceneSelect> owner,uint64_t epoch,const std::string& key){return owner&&owner->CanChoose(epoch,key);},weak),base::BindRepeating(&SceneSelect::Choose,weak));
    runner_=std::make_unique<views::MenuRunner>(menu_.get(),views::MenuRunner::COMBOBOX,base::BindRepeating(&SceneSelect::Closed,weak,token));RefreshAX();Schedule();if(runner_&&state_->expanded())runner_->RunMenuAt(GetWidget(),nullptr,GetBoundsInScreen(),views::MenuAnchorPosition::kTopLeft,source);
  }
  sidebar::Item Scene(float dx,float dy,gfx::RectF clip) const {sidebar::Item item{};item.kind=21;item.x=dx;item.y=dy;item.width=width();item.height=height();if(const auto selected=Selected())item.title=base::UTF16ToUTF8(labels_[*selected]);const auto* colors=GetColorProvider();const auto input=Snapshot();const bool enabled=input.enabled;item.font_size=TextFont().GetFontSize();item.active=TextFont().GetFontWeight()>=gfx::Font::Weight::MEDIUM;item.tile=base::i18n::IsRTL();item.focused=input.focused;item.hovered=enabled&&(input.visual==sidebar::RowVisual::Hovered||state_->expanded());item.ink=colors->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_LABEL,enabled?views::style::STYLE_PRIMARY:views::style::STYLE_DISABLED));item.fill=colors->GetColor(enabled?ui::kColorComboboxBackground:ui::kColorComboboxBackgroundDisabled);item.badge=colors->GetColor(input.focused?ui::kColorFocusableBorderFocused:enabled?ui::kColorButtonBorder:ui::kColorButtonBorderDisabled);item.detail_ink=colors->GetColor(ui::kColorSysStateHoverOnSubtle);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};return item;}
  void Schedule(){SyncViewportVisibility();if((runner_||state_->expanded())&&(!SceneIsVisible(this)||GetVisibleBounds().IsEmpty()))CancelMenu();if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneSelect::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=metal_visible();const auto local=GetVisibleBounds();if(!SceneIsVisible(this)||local.IsEmpty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());std::vector<sidebar::Item> items;items.push_back(Scene(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size()))));surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=metal_visible())SchedulePaint();}
  std::vector<std::u16string> labels_;rust::Box<sidebar::SelectState> state_;base::RepeatingClosure changed_;SceneSelectInput input_;std::unique_ptr<SceneSelectMenu> menu_;std::unique_ptr<views::MenuRunner> runner_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneSelect::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneSelect> weak_factory_{this};
};
BEGIN_METADATA(SceneSelect)
END_METADATA
// Chromium paints a nonempty model with no selection, and its prefix service
// supports it, but Combobox::OnKeyPressed CHECKs. Preserve the blank value and
// delegate to the same native input/menu services instead of inventing a preset.
class PreferenceCombobox final : public views::Combobox {
  METADATA_HEADER(PreferenceCombobox,views::Combobox)
 public:
  using views::Combobox::Combobox;
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if(!GetSelectedIndex()&&GetModel()->GetItemCount()){
      if(event.key_code()==ui::VKEY_DOWN||event.key_code()==ui::VKEY_UP||event.key_code()==ui::VKEY_SPACE||event.key_code()==ui::VKEY_HOME||event.key_code()==ui::VKEY_END){
        ui::AXActionData action;action.action=ax::mojom::Action::kDoDefault;
        return HandleAccessibleAction(action);
      }
      return false;
    }
    return views::Combobox::OnKeyPressed(event);
  }
};
BEGIN_METADATA(PreferenceCombobox)
END_METADATA
#endif
size_t ImportSourceIndex(views::View* view){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(view))return scene->Selected().value_or(0);
#endif
  return static_cast<views::Combobox*>(view)->GetSelectedIndex().value_or(0);
}
std::optional<size_t> PickerIndex(views::View* picker){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker))return scene->Selected();
#endif
  return static_cast<views::Combobox*>(picker)->GetSelectedIndex();
}
size_t PickerCount(views::View* picker){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker))return scene->Count();
#endif
  return static_cast<views::Combobox*>(picker)->GetModel()->GetItemCount();
}
void SetPickerIndex(views::View* picker,std::optional<size_t> index){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SetSelectedIndex(index);return;}
#endif
  static_cast<views::Combobox*>(picker)->SetSelectedIndex(index);
}
// Programmatic refresh does not steal an unchanged popup/IME interaction.
void SyncPickerIndex(views::View* picker,std::optional<size_t> index){if(PickerIndex(picker)!=index)SetPickerIndex(picker,index);}
void SetPickerCallback(views::View* picker,base::RepeatingClosure callback){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SetChangedCallback(std::move(callback));return;}
#endif
  static_cast<views::Combobox*>(picker)->SetCallback(std::move(callback));
}
views::View* ChoicePicker(views::View* parent,std::u16string name,std::vector<std::u16string> labels,bool scene){
#if BUILDFLAG(IS_MAC)
  if(scene)return parent->AddChildView(std::make_unique<SceneSelect>(std::move(labels),std::move(name),base::RepeatingClosure()));
#endif
  std::vector<ui::SimpleComboboxModel::Item> items;for(const auto& label:labels)items.emplace_back(label);
#if BUILDFLAG(IS_MAC)
  using NativeChoice=PreferenceCombobox;
#else
  using NativeChoice=views::Combobox;
#endif
  auto* picker=parent->AddChildView(std::make_unique<NativeChoice>(std::make_unique<ui::SimpleComboboxModel>(std::move(items))));
  picker->SetTooltipTextAndAccessibleName(name);return picker;
}
void ReplacePickerOptions(views::View* picker,const std::vector<std::pair<std::string,std::u16string>>& choices){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SetOptions(choices);return;}
#endif
  auto* native=static_cast<views::Combobox*>(picker);bool same=native->GetModel()->GetItemCount()==choices.size();
  for(size_t i=0;same&&i<choices.size();++i)same=native->GetModel()->GetItemAt(i)==choices[i].second;
  if(same)return;
  std::vector<ui::SimpleComboboxModel::Item> labels;for(const auto& choice:choices)labels.emplace_back(choice.second);
  native->SetSelectedIndex(std::nullopt);native->SetOwnedModel(std::make_unique<ui::SimpleComboboxModel>(std::move(labels)));
}
void ReplacePickerLabels(views::View* picker,const std::vector<std::u16string>& labels){
  std::vector<std::pair<std::string,std::u16string>> choices;
  for(size_t i=0;i<labels.size();++i)choices.emplace_back(base::NumberToString(i),labels[i]);
  ReplacePickerOptions(picker,choices);
  SetPickerIndex(picker,0); // Native site-policy model replacement starts at Default.
}
void RefreshChoiceViewports(views::View* root){
#if BUILDFLAG(IS_MAC)
  if(auto* group=views::AsViewClass<ScenePreferenceControls>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* scene=views::AsViewClass<SceneToggle>(root)){scene->OnVisibleBoundsChanged();return;}
  if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->OnVisibleBoundsChanged();return;}
  if(auto* scene=views::AsViewClass<SceneSelect>(root)){scene->OnVisibleBoundsChanged();return;}
  for(const auto& child:root->children())RefreshChoiceViewports(child.get());
#endif
}
void CancelChoiceInteractions(views::View* root){
#if BUILDFLAG(IS_MAC)
  if(auto* group=views::AsViewClass<ScenePreferenceControls>(root)){group->CancelInput();return;}
  if(auto* group=views::AsViewClass<SceneActionGroup>(root)){group->CancelInput();return;}
  if(auto* scene=views::AsViewClass<SceneToggle>(root)){scene->CancelInteraction();return;}
  if(auto* group=views::AsViewClass<SceneCheckboxGroup>(root)){group->CancelInput();return;}
  if(auto* scene=views::AsViewClass<SceneSelect>(root)){scene->CancelInteraction();return;}
  for(const auto& child:root->children())CancelChoiceInteractions(child.get());
#endif
}
views::View* ProfilePicker(views::View* parent,std::u16string name,bool scene,base::RepeatingClosure changed){
#if BUILDFLAG(IS_MAC)
  if(scene)return parent->AddChildView(std::make_unique<SceneSelect>(std::vector<std::u16string>(),std::move(name),std::move(changed)));
#endif
  auto* picker=parent->AddChildView(std::make_unique<views::Combobox>());picker->SetTooltipTextAndAccessibleName(name);picker->SetCallback(std::move(changed));return picker;
}
void ReplaceProfilePickerOptions(views::View* picker,const std::vector<ProfileInfo>& profiles,bool fallback_last,bool select_last){
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SetProfiles(profiles,fallback_last,select_last);return;}
#endif
  std::vector<ui::SimpleComboboxModel::Item> labels;for(const auto& profile:profiles)labels.emplace_back(base::UTF8ToUTF16(profile.name));static_cast<views::Combobox*>(picker)->SetOwnedModel(std::make_unique<ui::SimpleComboboxModel>(std::move(labels)));
}
views::View* AddImportCheck(views::View* parent,std::u16string text,views::Button::PressedCallback callback) {
#if BUILDFLAG(IS_MAC)
  if(auto* group=views::AsViewClass<SceneCheckboxGroup>(parent))return group->AddChoice(std::move(callback),std::move(text));
#endif
  return parent->AddChildView(std::make_unique<views::Checkbox>(std::move(text),std::move(callback)));
}
bool CheckValue(views::View* button) {
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneCheckbox>(button))return scene->Checked();
#endif
  return static_cast<views::Checkbox*>(button)->GetChecked();
}
void SetCheckValue(views::View* button,bool checked) {
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneCheckbox>(button)){scene->SetChecked(checked);return;}
#endif
  static_cast<views::Checkbox*>(button)->SetChecked(checked);
}
void SetCheckText(views::View* button,std::u16string text) {
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneCheckbox>(button)){scene->SetText(std::move(text));return;}
#endif
  static_cast<views::Checkbox*>(button)->SetText(text);
}

struct ToolsClearPresentation {std::u16string label;bool enabled;};
ToolsClearPresentation ToolsClearControls(bool scene,bool busy){
#if BUILDFLAG(IS_MAC)
  if(scene){const auto value=sidebar::tools_clear_controls(busy);return {base::UTF8ToUTF16(std::string(value.label)),value.enabled};}
#endif
  return {busy?u"Czyszczenie…":u"Wyczyść historię…",!busy};
}

views::View* ActionGroup(views::View* parent,bool scene,bool vertical,int spacing) {
#if BUILDFLAG(IS_MAC)
  if(scene)return parent->AddChildView(std::make_unique<SceneActionGroup>(vertical,spacing));
#endif
  auto* group=parent->AddChildView(std::make_unique<views::View>());group->SetLayoutManager(std::make_unique<views::BoxLayout>(vertical?views::BoxLayout::Orientation::kVertical:views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),spacing));return group;
}
views::View* AddAction(views::View* parent,views::Button::PressedCallback callback,std::u16string text,ui::ButtonStyle style=ui::ButtonStyle::kDefault) {
#if BUILDFLAG(IS_MAC)
  if(auto* group=views::AsViewClass<SceneActionGroup>(parent))return group->AddAction(std::move(callback),std::move(text),style);
#endif
  auto* button=parent->AddChildView(std::make_unique<views::MdTextButton>(std::move(callback),std::move(text)));button->SetStyle(style);return button;
}
void SetActionText(views::View* button,std::u16string text) {
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneTextButton>(button)){scene->SetText(std::move(text));return;}
#endif
  static_cast<views::MdTextButton*>(button)->SetText(text);
}

// Preserve the native baseline hierarchy; scene buttons share the existing
// action-group recipe and retain one native event/focus/accessibility adapter.
views::View* PreferenceAction(views::View* parent,bool scene,views::Button::PressedCallback callback,std::u16string text,bool stretch=false) {
#if BUILDFLAG(IS_MAC)
  // A button in a horizontal preference row keeps its intrinsic width, even
  // when Views asks for its height at the full row width. Vertical list actions
  // retain the original stretch behavior.
  if(scene)parent=parent->AddChildView(std::make_unique<SceneActionGroup>(stretch,0,!stretch));
#endif
  return AddAction(parent,std::move(callback),std::move(text));
}

// Editing/IME remains a platform service; result selection is scene state.
class PaletteView final : public views::View {
  METADATA_HEADER(PaletteView,views::View)
 public:
  explicit PaletteView(base::RepeatingClosure painted):painted_(std::move(painted)) {
#if BUILDFLAG(IS_MAC)
    if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")selection_.emplace(sidebar::new_palette_selection());
#endif
  }
  void SetEditor(views::View* editor){editor_=editor;}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {
    views::View::OnDidSchedulePaint(rect);
    // Native caret/preedit repainting does not change a GPU scene. Text/data
    // changes schedule explicitly; retain other invalidations (e.g. scrollbar).
    if(editor_&&scene()&&views::View::ConvertRectToTarget(editor_,this,editor_->GetLocalBounds()).Contains(rect))return;
    painted_.Run();
  }
  bool scene() const {
#if BUILDFLAG(IS_MAC)
    return selection_.has_value();
#else
    return false;
#endif
  }
  void OnPaint(gfx::Canvas* canvas) override {
#if BUILDFLAG(IS_MAC)
    if(scene()){sidebar::Item frame{};frame.kind=16;frame.width=width();frame.height=height();frame.fill=Tone(this,kCard);frame.badge=Hairline(this);frame.clip={0,0,float(width()),float(height())};PaintSceneRecipe(canvas,frame,Font(-2));return;}
#endif
    views::View::OnPaint(canvas);
  }
  void Reset() {
#if BUILDFLAG(IS_MAC)
    if(selection_){(*selection_)->reset();return;}
#endif
    index_=0;moved_=false;
  }
  size_t selected() const {
#if BUILDFLAG(IS_MAC)
    if(selection_)return (*selection_)->selected();
#endif
    return index_;
  }
  bool moved() const {
#if BUILDFLAG(IS_MAC)
    if(selection_)return (*selection_)->moved();
#endif
    return moved_;
  }
  void Step(size_t count,bool backwards) {
#if BUILDFLAG(IS_MAC)
    if(selection_){static_cast<void>((*selection_)->step(count,backwards));return;}
#endif
    if(count){moved_=true;index_=(index_+(backwards?count-1:1))%count;}
  }
  void Reconcile(size_t count,size_t preserved,bool found) {
#if BUILDFLAG(IS_MAC)
    if(selection_){(*selection_)->reconcile(count,preserved,found);return;}
#endif
    if(found)index_=preserved;index_=std::min(index_,count?count-1:0);
  }
 private:
#if BUILDFLAG(IS_MAC)
  std::optional<rust::Box<sidebar::PaletteSelection>> selection_;
#endif
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
class RowButton : public views::Button {
  METADATA_HEADER(RowButton, views::Button)
 public:
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
    int height = 34;
    PressedCallback close;
    base::RepeatingCallback<void(gfx::Point)> drop;
  };
  RowButton(PressedCallback callback, Spec spec)
      : views::Button(std::move(callback)), spec_(std::move(spec)) {
    // The close affordance is a descendant: entering it must retain row hover.
    SetNotifyEnterExitOnChild(true);
#if BUILDFLAG(IS_MAC)
    if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal")
      rust_pointer_.emplace(sidebar::new_row_pointer());
    scene_text_ = rust_pointer_.has_value() && spec_.sidebar_row;
#endif
    GetViewAccessibility().SetName(spec_.title);
    if(scene_text_&&spec_.chip)GetViewAccessibility().SetDescription(spec_.detail);
    SetTooltipText(spec_.title);
    if (!scene_text_) {
      title_ = Label(this, spec_.title, kInk,
                     Font(0, spec_.active || spec_.chip || spec_.address ? gfx::Font::Weight::MEDIUM
                                                        : gfx::Font::Weight::NORMAL));
      title_->SetElideBehavior(gfx::ELIDE_TAIL);
      title_->SetCanProcessEventsWithinSubtree(false);
    }
    if (!scene_text_ && (spec_.address || !spec_.detail.empty())) {
      detail_ = Label(this, spec_.detail, kMuted, Font(spec_.address ? 0 : -1));
      detail_->SetElideBehavior(spec_.address ? gfx::ELIDE_TAIL : gfx::ELIDE_MIDDLE);
      detail_->SetCanProcessEventsWithinSubtree(false);
    }
    if (spec_.close) {
      close_ = AddChildView(std::make_unique<SceneGlyphButton>(
          std::move(spec_.close), u"Zamknij kartę", GlyphButton::Glyph::kVector,
          &vector_icons::kCloseSmallIcon,base::RepeatingCallback<bool()>()));
      close_->SetVisible(spec_.active && !spec_.tile);
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) {
      const auto result = RustPointerEvent(sidebar::PointerPhase::Press, event);
      if (GetEnabledInViewsSubtree()) RequestFocusFromEvent();
      return ApplyRustInput(result, event);
    }
#endif
    drag_start_ = event.location(); dragging_ = false;
    return views::Button::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Drag, event), event);
#endif
    if (spec_.drop && (event.location() - drag_start_).LengthSquared() > 64) dragging_ = true;
    return views::Button::OnMouseDragged(event);
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Release, event), event); return; }
#endif
    if (dragging_ && spec_.drop) {
      SetState(STATE_NORMAL);
      auto callback = spec_.drop;
      callback.Run(display::Screen::Get()->GetCursorScreenPoint());
      return;
    }
    views::Button::OnMouseReleased(event);
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Enter, event), event); return; }
#endif
    views::Button::OnMouseEntered(event);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Exit, event), event); return; }
#endif
    views::Button::OnMouseExited(event);
  }
  void OnMouseMoved(const ui::MouseEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) { ApplyRustInput(RustPointerEvent(sidebar::PointerPhase::Move, event), event); return; }
#endif
    views::Button::OnMouseMoved(event);
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput((*rust_pointer_)->key_event(true, RustKeyPolicy(event), GetEnabledInViewsSubtree()), event);
#endif
    return views::Button::OnKeyPressed(event);
  }
  bool OnKeyReleased(const ui::KeyEvent& event) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) return ApplyRustInput((*rust_pointer_)->key_event(false, RustKeyPolicy(event), GetEnabledInViewsSubtree()), event);
#endif
    return views::Button::OnKeyReleased(event);
  }
  void OnBlur() override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) (*rust_pointer_)->cancel();
#endif
    views::Button::OnBlur();
  }
  void OnMouseCaptureLost() override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) (*rust_pointer_)->cancel();
#endif
    dragging_ = false;
    views::Button::OnMouseCaptureLost();
  }
  void SetActive(bool active) {
    if (spec_.active == active) return;
    spec_.active = active;scene_title_width_cache_.reset();
    if (title_) title_->SetFontList(Font(0, active ? gfx::Font::Weight::MEDIUM : gfx::Font::Weight::NORMAL));
    UpdateCloseVisibility();
    SchedulePaint();
  }
  void SetDetail(std::u16string detail) {
    if (spec_.detail == detail) return;
    spec_.detail = std::move(detail);
    if (detail_) detail_->SetText(spec_.detail);
    InvalidateLayout(); SchedulePaint();
  }
  int scene_title_width() const {
    if(!spec_.address&&!spec_.chip&&spec_.detail.empty())return 0;
    if(!scene_title_width_cache_)scene_title_width_cache_=gfx::GetStringWidth(spec_.title,
        Font(0,spec_.active||spec_.chip||spec_.address?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL))+2;
    return *scene_title_width_cache_;
  }
  void SetTitle(std::u16string title) {
    if (spec_.title == title) return;
    spec_.title = std::move(title);scene_title_width_cache_.reset();
    if (title_) title_->SetText(spec_.title);
    else SchedulePaint();
    GetViewAccessibility().SetName(spec_.title);
    SetTooltipText(spec_.title);
  }
  void SetMonogram(std::u16string monogram, SkColor color) {
    if (spec_.monogram == monogram && spec_.monogram_color == color) return;
    spec_.monogram = std::move(monogram);
    spec_.monogram_color = color;
    SchedulePaint();
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(200, spec_.height);
  }
  void StateChanged(ButtonState) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_ && GetState() == STATE_DISABLED) (*rust_pointer_)->cancel();
#endif
    // Half-width split tiles keep their title room; close appears on hover.
    UpdateCloseVisibility();
    SchedulePaint();
  }
  void Layout(PassKey) override {
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) {
      const auto geometry = SceneGeometry();
      auto rect = [](const sidebar::Rect& r) { return gfx::Rect(r.x, r.y, r.width, r.height); };
      if (title_) title_->SetBoundsRect(rect(geometry.title));
      if (detail_) detail_->SetBoundsRect(rect(geometry.detail));
      if (close_) close_->SetBoundsRect(rect(geometry.close));
      return;
    }
#endif
    if (spec_.address) {
      const int available = std::max(0, width() - 46);
      const int host_width = std::min(title_->GetPreferredSize().width(), available);
      title_->SetBounds(34, 0, host_width, height());
      if (detail_) detail_->SetBounds(34 + host_width, 0, available - host_width, height());
      return;
    }
    const int lead = spec_.chip ? 52 : 36;
    const int trail = close_ ? 32 : 10;
    const int width = std::max(0, this->width() - lead - trail);
    if (detail_) {
      const int title_width = std::min(title_->GetPreferredSize().width(), width * 3 / 5);
      title_->SetBounds(lead, 0, title_width, height());
      detail_->SetBounds(lead + title_width + 10, 0,
                         std::max(0, width - title_width - 10), height());
    } else {
      title_->SetBounds(lead, 0, width, height());
    }
    if (close_) close_->SetBounds(this->width() - 30, (height() - 24) / 2, 24, 24);
  }
  void SetAddress(std::u16string host, std::u16string path, bool blank, bool secure) {
    SetTitle(std::move(host)); SetDetail(std::move(path));
    spec_.blank = blank; spec_.secure = secure;
    GetViewAccessibility().SetDescription(spec_.title + spec_.detail);
    if (title_) static_cast<ThemedLabel*>(title_.get())->SetTone(blank ? kMuted : kInk);
    GetViewAccessibility().SetName(u"Adres strony");
    SetTooltipText(u"Szukaj lub wpisz adres");
    InvalidateLayout(); SchedulePaint();
  }
#if BUILDFLAG(IS_MAC)
  sidebar::RowLayout SceneGeometry() const {
    if (spec_.address) return sidebar::address_layout(width(), height(), scene_title_width());
    return sidebar::row_layout(width(), height(),
        title_ ? title_->GetPreferredSize().width() : scene_title_width(),
        !spec_.detail.empty(), spec_.chip, close_ != nullptr);
  }
#endif
  void OnThemeChanged() override{views::Button::OnThemeChanged();scene_title_width_cache_.reset();}
  bool has_close_button() const { return close_ != nullptr; }
  bool close_button_visible() const { return close_ && close_->GetVisible(); }
  const Spec& preview_spec() const { return spec_; }
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
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (scene_text_ && spec_.scene_visible && spec_.scene_visible.Run()) return;
    PaintDecorations(canvas, GetLocalBounds(), spec_, this, GetState() == STATE_HOVERED || GetState() == STATE_PRESSED);
#if BUILDFLAG(IS_MAC)
    if (scene_text_) {
      // Used only for GPU fallback and native animation snapshots. Steady Metal
      // frames skip this subtree entirely; no hidden Label measures the title.
      const auto geometry = SceneGeometry();
      const auto& r = geometry.title;
      canvas->DrawStringRectWithFlags(spec_.title,
          Font(0, spec_.active || spec_.chip || spec_.address ? gfx::Font::Weight::MEDIUM : gfx::Font::Weight::NORMAL),
          Tone(this, spec_.blank ? kMuted : kInk), gfx::Rect(r.x, r.y, r.width, r.height), gfx::Canvas::TEXT_ALIGN_LEFT);
      if (!spec_.detail.empty()) {
        const auto& d = geometry.detail;
        canvas->DrawStringRectWithFlags(gfx::ElideText(spec_.detail, Font(spec_.address ? 0 : -1), d.width, spec_.address ? gfx::ELIDE_TAIL : gfx::ELIDE_MIDDLE),
            Font(spec_.address ? 0 : -1), Tone(this, kMuted), gfx::Rect(d.x, d.y, d.width, d.height), gfx::Canvas::TEXT_ALIGN_LEFT);
      }
    }
#endif
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
      const gfx::Rect mono(10, (bounds.height() - 16) / 2, 16, 16);
      FillRound(canvas, mono, 4, spec.monogram_color);
      canvas->DrawStringRectWithFlags(spec.monogram, Font(-4, gfx::Font::Weight::SEMIBOLD),
                                      SK_ColorWHITE, mono, gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }
 private:
#if BUILDFLAG(IS_MAC)
  sidebar::RowInputResult RustPointerEvent(sidebar::PointerPhase phase, const ui::MouseEvent& event) {
    return (*rust_pointer_)->pointer_event(phase, event.x(), event.y(),
        phase != sidebar::PointerPhase::Exit && HitTestPoint(event.location()),
        GetEnabledInViewsSubtree(), event.IsLeftMouseButton(), !spec_.drop.is_null());
  }
  uint32_t RustKeyPolicy(const ui::KeyEvent& event) {
    const uint32_t key = event.key_code() == ui::VKEY_SPACE ? 1 :
        event.key_code() == ui::VKEY_RETURN ? 2 : 0;
    return sidebar::key_policy(key, true);  // This adapter is macOS-only.
  }
  bool ApplyRustInput(const sidebar::RowInputResult& result, const ui::Event& event) {
    switch (result.visual) {
      case sidebar::RowVisual::Normal: SetState(STATE_NORMAL); break;
      case sidebar::RowVisual::Hovered: SetState(STATE_HOVERED); break;
      case sidebar::RowVisual::Pressed: SetState(STATE_PRESSED); break;
      case sidebar::RowVisual::Disabled: SetState(STATE_DISABLED); break;
    }
    const bool handled = result.handled;
    // Actions may delete this row or its owning palette. Do not touch members
    // after calling a controller, including the native accessibility action.
    switch (result.action) {
      case sidebar::RowAction::Activate: NotifyClick(event); return handled;
      case sidebar::RowAction::Drop: {
        auto callback = spec_.drop;
        if (callback) callback.Run(display::Screen::Get()->GetCursorScreenPoint());
        return handled;
      }
      case sidebar::RowAction::Cancel: OnClickCanceled(event); return handled;
      case sidebar::RowAction::None: return handled;
    }
    return handled;
  }
#endif
  void UpdateCloseVisibility() {
    if (!close_) return;
    const bool hovered = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
#if BUILDFLAG(IS_MAC)
    if (rust_pointer_) {
      close_->SetVisible(sidebar::close_visible(spec_.active, spec_.tile, hovered));
      return;
    }
#endif
    close_->SetVisible((spec_.active && !spec_.tile) || hovered);
  }
#if BUILDFLAG(IS_MAC)
  std::optional<rust::Box<sidebar::RowPointer>> rust_pointer_;
#endif
  bool scene_text_ = false;
  mutable std::optional<int> scene_title_width_cache_;
  gfx::Point drag_start_;
  bool dragging_ = false;
  Spec spec_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> detail_ = nullptr;
  raw_ptr<GlyphButton> close_ = nullptr;
};
BEGIN_METADATA(RowButton)
END_METADATA
#if BUILDFLAG(IS_MAC)
// Input, enabled/focus/visual state and drag admission live in Rust. This node
// adapts native events, accessibility, capture, window visibility and font paint.
class RustRowView final : public views::View {
  METADATA_HEADER(RustRowView,views::View)
 public:
  using Spec=RowButton::Spec;
  using Watch=base::RepeatingCallback<void(views::View*,base::RepeatingClosure)>;
  RustRowView(views::Button::PressedCallback callback,Spec spec,base::RepeatingClosure changed,Watch watch)
      :spec_(std::move(spec)),callback_(std::move(callback)),changed_(std::move(changed)),watch_(std::move(watch)){
    SetNotifyEnterExitOnChild(true);SetTooltipText(spec_.title);auto& ax=GetViewAccessibility();ax.SetName(spec_.title);ax.SetRole(ax::mojom::Role::kButton);ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);SetFocusBehavior(views::PlatformStyle::kDefaultFocusBehavior);if(!spec_.history)views::FocusRing::Install(this);state_->action_on(spec_.active);if(spec_.chip)ax.SetDescription(spec_.detail);
    if(spec_.close){close_=AddChildView(CreateSceneAction(std::move(spec_.close),u"Zamknij kartę",ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,{},base::BindRepeating(&RustRowView::ChildChanged,weak_factory_.GetWeakPtr())));UpdateClose();}
  }
  ~RustRowView() override{if(watch_)watch_.Run(this,{});}
  const Spec& preview_spec() const{return spec_;}
  void SetCallback(views::Button::PressedCallback callback){CancelInput();callback_=std::move(callback);}
  void SetActive(bool active){if(!state_->action_on(active))return;spec_.active=active;measured_.reset();UpdateClose();SchedulePaint();}
  void SetTitle(std::u16string title){if(spec_.title==title)return;spec_.title=std::move(title);measured_.reset();SetTooltipText(spec_.title);GetViewAccessibility().SetName(spec_.title);SchedulePaint();}
  void SetDetail(std::u16string detail){if(spec_.detail==detail)return;spec_.detail=std::move(detail);InvalidateLayout();SchedulePaint();}
  void SetAddress(std::u16string host,std::u16string path,bool blank,bool secure){SetTitle(std::move(host));SetDetail(std::move(path));spec_.blank=blank;spec_.secure=secure;GetViewAccessibility().SetDescription(spec_.title+spec_.detail);GetViewAccessibility().SetName(u"Adres strony");SetTooltipText(u"Szukaj lub wpisz adres");InvalidateLayout();SchedulePaint();}
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
    item.fill=item.hovered?HoverWash(this):SK_ColorTRANSPARENT;item.focused=HasFocus();return item;
  }
  void SetMonogram(std::u16string monogram,SkColor color){if(spec_.monogram==monogram&&spec_.monogram_color==color)return;spec_.monogram=std::move(monogram);spec_.monogram_color=color;SchedulePaint();}
  int scene_title_width() const{if(!spec_.address&&!spec_.chip&&spec_.detail.empty())return 0;if(!measured_)measured_=gfx::GetStringWidth(spec_.title,Font(0,spec_.active||spec_.chip||spec_.address?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL))+2;return *measured_;}
  views::Button::ButtonState GetState() const{const auto v=state_->action_snapshot().visual;return v==sidebar::RowVisual::Disabled?views::Button::STATE_DISABLED:v==sidebar::RowVisual::Pressed?views::Button::STATE_PRESSED:v==sidebar::RowVisual::Hovered?views::Button::STATE_HOVERED:views::Button::STATE_NORMAL;}
  bool has_close_button() const{return close_!=nullptr;}
  bool close_button_visible() const{return close_&&close_->GetVisible();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(200,spec_.height);}
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
  void OnMouseCaptureLost() override{CancelInput();}
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
    if(spec_.scene_visible&&spec_.scene_visible.Run())return;SyncEnabled();if(spec_.history){const auto bounds=gfx::RectF(GetLocalBounds());PaintSceneRecipe(canvas,Scene(bounds,bounds),Font(0),true);return;}const auto v=state_->action_snapshot().visual;RowButton::PaintDecorations(canvas,GetLocalBounds(),spec_,this,v==sidebar::RowVisual::Hovered||v==sidebar::RowVisual::Pressed);
    const auto geometry=Geometry();const auto& r=geometry.title;canvas->DrawStringRectWithFlags(spec_.title,Font(0,spec_.active||spec_.chip||spec_.address?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL),Tone(this,spec_.blank?kMuted:kInk),gfx::Rect(r.x,r.y,r.width,r.height),gfx::Canvas::TEXT_ALIGN_LEFT);
    if(!spec_.detail.empty()){const auto& d=geometry.detail;canvas->DrawStringRectWithFlags(gfx::ElideText(spec_.detail,Font(spec_.address?0:-1),d.width,spec_.address?gfx::ELIDE_TAIL:gfx::ELIDE_MIDDLE),Font(spec_.address?0:-1),Tone(this,kMuted),gfx::Rect(d.x,d.y,d.width,d.height),gfx::Canvas::TEXT_ALIGN_LEFT);}
  }
 private:
  sidebar::RowLayout Geometry() const{if(spec_.address)return sidebar::address_layout(width(),height(),scene_title_width());return sidebar::row_layout(width(),height(),scene_title_width(),!spec_.detail.empty(),spec_.chip,close_!=nullptr);}
  bool Interactive() const{return SceneIsVisible(this)&&!GetVisibleBounds().IsEmpty();}
  void SyncEnabled(){if(!state_->action_enabled(GetEnabledInViewsSubtree()))return;auto& ax=GetViewAccessibility();if(GetEnabledInViewsSubtree())ax.SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);else ax.RemoveDefaultActionVerb();UpdateClose();UpdateWatch();SchedulePaint();}
  void EnabledChanged(){SyncEnabled();}
  void WindowChanged(){CancelInput();state_->action_focus(HasFocus());SchedulePaint();}
  void UpdateClose(){if(close_){const auto v=state_->action_snapshot();close_->SetVisible(sidebar::close_visible(v.on,spec_.tile,v.visual==sidebar::RowVisual::Hovered||v.visual==sidebar::RowVisual::Pressed));}}
  void ChildChanged(){UpdateWatch();if(changed_)changed_.Run();}
  void UpdateWatch(){if(!watch_)return;bool pending=state_->action_pending();if(close_)if(auto* action=views::AsViewClass<RustActionView>(close_.get()))pending=pending||action->Pending();watch_.Run(this,pending?base::BindRepeating(&RustRowView::SyncViewport,weak_factory_.GetWeakPtr()):base::RepeatingClosure());}
  bool Pointer(sidebar::PointerPhase phase,const ui::MouseEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;return Apply(state_->action_row_pointer(phase,event.x(),event.y(),phase!=sidebar::PointerPhase::Exit&&HitTestPoint(event.location())&&GetVisibleBounds().Contains(event.location()),event.IsLeftMouseButton(),Interactive(),!spec_.drop.is_null()),event,before);}
  bool Key(bool pressed,const ui::KeyEvent& event){SyncEnabled();const auto before=state_->action_snapshot().visual;return Apply(state_->action_key(pressed,event.key_code()==ui::VKEY_SPACE?1:event.key_code()==ui::VKEY_RETURN?2:0,true,Interactive()),event,before);}
  bool Apply(const sidebar::RowInputResult& result,const ui::Event& event,sidebar::RowVisual before){UpdateClose();UpdateWatch();if(result.visual!=before)SchedulePaint();const bool handled=result.handled;if(result.action==sidebar::RowAction::Activate&&callback_){callback_.Run(event);return handled;}if(result.action==sidebar::RowAction::Drop&&spec_.drop){auto drop=spec_.drop;drop.Run(display::Screen::Get()->GetCursorScreenPoint());return handled;}return handled;}
  Spec spec_;views::Button::PressedCallback callback_;base::RepeatingClosure changed_;Watch watch_;raw_ptr<views::View> close_=nullptr;mutable std::optional<int> measured_;rust::Box<sidebar::ActionControl> state_=sidebar::new_action_control();SceneWindowObserver window_{base::BindRepeating(&RustRowView::WindowChanged,base::Unretained(this))};base::CallbackListSubscription enabled_changed_{AddEnabledInViewsSubtreeChangedCallback(base::BindRepeating(&RustRowView::EnabledChanged,base::Unretained(this)))};base::WeakPtrFactory<RustRowView> weak_factory_{this};
};
BEGIN_METADATA(RustRowView)
END_METADATA
#endif
std::unique_ptr<views::View> CreateSceneRow(views::Button::PressedCallback callback,RowButton::Spec spec,base::RepeatingClosure changed,base::RepeatingCallback<void(views::View*,base::RepeatingClosure)> watch){
#if BUILDFLAG(IS_MAC)
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")return std::make_unique<RustRowView>(std::move(callback),std::move(spec),std::move(changed),std::move(watch));
#endif
  return std::make_unique<RowButton>(std::move(callback),std::move(spec));
}
template<typename Callback> decltype(auto) VisitRow(views::View* view,Callback callback){
#if BUILDFLAG(IS_MAC)
  if(auto* row=views::AsViewClass<RustRowView>(view))return callback(row);
#endif
  return callback(static_cast<RowButton*>(view));
}
bool IsRow(views::View* view){
#if BUILDFLAG(IS_MAC)
  if(views::AsViewClass<RustRowView>(view))return true;
#endif
  return views::AsViewClass<RowButton>(view)!=nullptr;
}
const RowButton::Spec& RowSpec(views::View* view){return VisitRow(view,[](auto* row)->const RowButton::Spec&{return row->preview_spec();});}
void SetRowActive(views::View* view,bool value){VisitRow(view,[value](auto* row){row->SetActive(value);});}
void SetRowTitle(views::View* view,std::u16string value){VisitRow(view,[&value](auto* row){row->SetTitle(std::move(value));});}
void SetRowDetail(views::View* view,std::u16string value){VisitRow(view,[&value](auto* row){row->SetDetail(std::move(value));});}
void SetRowMonogram(views::View* view,std::u16string value,SkColor color){VisitRow(view,[&value,color](auto* row){row->SetMonogram(std::move(value),color);});}
void SetRowCallback(views::View* view,views::Button::PressedCallback value){VisitRow(view,[&value](auto* row){row->SetCallback(std::move(value));});}
bool RowHovered(views::View* view){return VisitRow(view,[](auto* row){return row->GetState()==views::Button::STATE_HOVERED||row->GetState()==views::Button::STATE_PRESSED;});}
int RowTitleWidth(views::View* view){return VisitRow(view,[](auto* row){return row->scene_title_width();});}
bool RowHasClose(views::View* view){return VisitRow(view,[](auto* row){return row->has_close_button();});}
bool RowCloseVisible(views::View* view){return VisitRow(view,[](auto* row){return row->close_button_visible();});}
void SetRowAddress(views::View* view,std::u16string host,std::u16string path,bool blank,bool secure){VisitRow(view,[&](auto* row){row->SetAddress(std::move(host),std::move(path),blank,secure);});}
void SetPreferenceNavigationSelected(views::View* control,bool selected){
#if BUILDFLAG(IS_MAC)
  if(auto* button=views::AsViewClass<ScenePreferenceButton>(control)){button->SetSelected(selected);return;}
#endif
  static_cast<RowButton*>(control)->SetActive(selected);
}


// One compact sidebar item containing the two pages in visual pane order.
class SplitPair final : public views::View {
  METADATA_HEADER(SplitPair, views::View)
 public:
  SplitPair() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Split View — para kart");
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
std::unique_ptr<views::View> CreateAddressRow(views::Button::PressedCallback callback,base::RepeatingCallback<bool()> visible,base::RepeatingClosure changed,base::RepeatingCallback<void(views::View*,base::RepeatingClosure)> watch){
  RowButton::Spec spec;spec.address=true;spec.sidebar_row=true;spec.height=40;spec.scene_visible=std::move(visible);
  auto row=CreateSceneRow(std::move(callback),std::move(spec),std::move(changed),std::move(watch));row->GetViewAccessibility().SetName(u"Adres strony");row->SetTooltipText(u"Szukaj lub wpisz adres");return row;
}

// Scene-owned text with one native static-text accessibility node. The Views
// backend retains its ordinary Label; Metal never constructs a hidden Label.
class WorkspaceHeading final : public views::View {
  METADATA_HEADER(WorkspaceHeading, views::View)
 public:
  explicit WorkspaceHeading(base::RepeatingCallback<bool()> visible) : visible_(std::move(visible)) {
    SetPreferredSize(gfx::Size(200, 24));
#if BUILDFLAG(IS_MAC)
    scene_ = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal";
#endif
    if (scene_) GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
    else {
      SetLayoutManager(std::make_unique<views::FillLayout>());
      SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 8)));
      label_ = Label(this, u"", kSubtle, Font(-1, gfx::Font::Weight::SEMIBOLD));
    }
    SetText(u"Praca");
  }
  void SetText(std::u16string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    if (label_) label_->SetText(text_);
    else GetViewAccessibility().SetName(text_);
    SchedulePaint();
  }
  const std::u16string& text() const { return text_; }
  void OnPaint(gfx::Canvas* canvas) override {
    if (!scene_ || (visible_ && visible_.Run())) return;
    auto bounds = GetLocalBounds(); bounds.Inset(gfx::Insets::VH(0, 8));
    canvas->DrawStringRectWithFlags(text_, Font(-1, gfx::Font::Weight::SEMIBOLD),
        Tone(this, kSubtle), bounds, gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:
  bool scene_ = false;
  std::u16string text_;
  base::RepeatingCallback<bool()> visible_;
  raw_ptr<views::Label> label_ = nullptr;
};
BEGIN_METADATA(WorkspaceHeading)
END_METADATA

#if BUILDFLAG(IS_MAC)
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
#if BUILDFLAG(IS_MAC)
    const auto g=sidebar::palette_text_layout(kind_,-1,-1,text_width(),font_.GetHeight());return gfx::Size(g.width,g.height);
#else
    return views::View::CalculatePreferredSize(available);
#endif
  }
  gfx::Rect text_bounds() const {
#if BUILDFLAG(IS_MAC)
    const auto g=sidebar::palette_text_layout(kind_,width(),height(),text_width(),font_.GetHeight());const auto& r=g.children[0];return gfx::Rect(r.x,r.y,r.width,r.height);
#else
    return GetLocalBounds();
#endif
  }
#if BUILDFLAG(IS_MAC)
  sidebar::Item SceneItem(gfx::RectF rect,gfx::RectF clip) const {
    sidebar::Item item{};item.kind=15;item.glyph=kind_;item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
    item.title=base::UTF16ToUTF8(text_);item.title_width=text_width();item.font_size=font_.GetFontSize();item.font_height=font_.GetHeight();item.ink=Tone(this,kind_==2?kError:kMuted);item.badge=Hairline(this);return item;
  }
#endif
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_.reset();PreferredSizeChanged();painted_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void OnPaint(gfx::Canvas* canvas) override {
#if BUILDFLAG(IS_MAC)
    if(kind_==1){sidebar::Item line{};line.kind=14;line.width=width();line.height=1;line.fill=Hairline(this);line.clip={0,0,float(width()),float(height())};PaintSceneRecipe(canvas,line,font_);}
    if(visible_&&visible_.Run())return;
    const auto bounds=gfx::RectF(GetLocalBounds());PaintSceneRecipe(canvas,SceneItem(bounds,bounds),font_);
#endif
  }
 private:
  std::u16string text_;uint32_t kind_;gfx::FontList font_;
  base::RepeatingClosure painted_;base::RepeatingCallback<bool()> visible_;
  mutable std::optional<int> measured_;
};
BEGIN_METADATA(PaletteText)
END_METADATA
#endif

#if BUILDFLAG(IS_MAC)
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
    auto* provider=views::LayoutProvider::Get();set_fixed_width(provider->GetDistanceMetric(views::DISTANCE_MODAL_DIALOG_PREFERRED_WIDTH));
    const auto sides=provider->GetInsetsMetric(views::INSETS_DIALOG);
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
    field_=AddEditor(this,true);field_->GetViewAccessibility().SetName(base::UTF8ToUTF16(std::string(copy_.label)));
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
    for(size_t i=0;i<parts_.size();++i)if(parts_[i]->GetVisible()&&(!surfaces_[i]||!surfaces_[i]->visible()))PaintSceneRecipe(canvas,Item(i,gfx::RectF(parts_[i]->bounds())),Font(-1));
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
#endif

#if BUILDFLAG(IS_MAC)
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
      const auto clip=gfx::RectF(parts_[i]->bounds());const auto items=Items(i,g,0,0,clip);
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
class WorkspaceNoticeView final : public NoticeBodyView {
  METADATA_HEADER(WorkspaceNoticeView,NoticeBodyView)
 public:
  WorkspaceNoticeView(const std::string& workspace,const std::string& title,base::RepeatingCallback<bool(WorkspaceNoticeView*)> submit)
      :WorkspaceNoticeView(sidebar::new_workspace_notice(workspace,title),std::move(submit)) {}
  base::WeakPtr<WorkspaceNoticeView> weak(){return weak_factory_.GetWeakPtr();}
  bool Accept(){return submit_.Run(this);}
  void Cancel(){notice_->cancel();}
  sidebar::WorkspaceNoticeRequest Prepare(bool ready,bool busy,const std::vector<Workspace>& workspaces) {
    std::vector<sidebar::ProfileUse> uses;uses.reserve(workspaces.size());for(const auto& w:workspaces)uses.push_back({w.id,w.name,w.profile_id});
    auto request=notice_->submit(ready,busy,rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));SetError(base::UTF8ToUTF16(std::string(notice_->error())));return request;
  }
  void Finish(bool accepted){notice_->finish_attempt(accepted);SetError(base::UTF8ToUTF16(std::string(notice_->error())));}
 private:
  WorkspaceNoticeView(rust::Box<sidebar::WorkspaceNotice> notice,base::RepeatingCallback<bool(WorkspaceNoticeView*)> submit)
      :NoticeBodyView(notice->copy()),notice_(std::move(notice)),submit_(std::move(submit)) {}
  rust::Box<sidebar::WorkspaceNotice> notice_;base::RepeatingCallback<bool(WorkspaceNoticeView*)> submit_;base::WeakPtrFactory<WorkspaceNoticeView> weak_factory_{this};
};
BEGIN_METADATA(WorkspaceNoticeView)
END_METADATA
WorkspaceNoticeView* FindWorkspaceNotice(views::View* view) {
  if(auto* notice=views::AsViewClass<WorkspaceNoticeView>(view))return notice;
  for(const auto& child:view->children())if(auto* notice=FindWorkspaceNotice(child.get()))return notice;return nullptr;
}
FolderNoticeView* FindFolderNotice(views::View* view) {
  if(auto* notice=views::AsViewClass<FolderNoticeView>(view))return notice;
  for(const auto& child:view->children())if(auto* notice=FindFolderNotice(child.get()))return notice;return nullptr;
}
#endif

#if BUILDFLAG(IS_MAC)
class PreferenceCopy final : public views::View {
  METADATA_HEADER(PreferenceCopy,views::View)
 public:
  PreferenceCopy(std::u16string text,SkColor tone,int delta,gfx::Font::Weight weight,bool multiline,base::RepeatingClosure changed,base::RepeatingCallback<bool()> visible,bool native_style=false,bool center=false,int fixed_width=0)
      :text_(std::move(text)),tone_(tone),delta_(delta),weight_(weight),multiline_(multiline),native_style_(native_style),center_(center),fixed_width_(fixed_width),changed_(std::move(changed)),visible_(std::move(visible)){GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);SetCanProcessEventsWithinSubtree(false);}
  const std::u16string& text() const{return text_;}
  gfx::FontList font() const{return native_style_?views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,views::style::STYLE_PRIMARY):Font(delta_,weight_);}
  SkColor Ink() const{return native_style_?GetColorProvider()->GetColor(views::TypographyProvider::Get().GetColorId(views::style::CONTEXT_LABEL,GetEnabledInViewsSubtree()?views::style::STYLE_PRIMARY:views::style::STYLE_DISABLED)):Tone(this,GetEnabledInViewsSubtree()?tone_:kDisabled);}
  void SetText(std::u16string text){if(text==text_)return;text_=std::move(text);GetViewAccessibility().SetName(text_);measured_width_=-1;natural_width_=-1;PreferredSizeChanged();SchedulePaint();Notify();}
  void SetTone(SkColor tone){if(tone_==tone)return;tone_=tone;SchedulePaint();Notify();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{if(multiline_&&fixed_width_>0&&!text_.empty()){const auto g=Geometry(fixed_width_);return gfx::Size(fixed_width_,g.height);}const int natural=NaturalWidth();const int width=available.width().is_bounded()?std::min(natural,available.width().value()):natural;auto g=Geometry(width);return gfx::Size(width,g.height);}
  void Append(std::vector<sidebar::Item>& items,gfx::Point origin,gfx::Rect clip) const{auto g=Geometry(width());for(size_t i=0;i<lines_.size();++i){auto r=g.children[i];gfx::Rect box(origin.x()+r.x,origin.y()+r.y,r.width,r.height);auto visible=box;visible.Intersect(clip);if(visible.IsEmpty()||lines_[i].empty())continue;sidebar::Item item{};item.kind=17;item.x=box.x();item.y=box.y();item.width=box.width();item.height=box.height();item.title=lines_[i];item.ink=Ink();item.glyph=center_?1:0;item.font_size=font().GetFontSize();item.font_weight=static_cast<int>(native_style_?font().GetFontWeight():weight_);item.active=item.font_weight>=static_cast<int>(gfx::Font::Weight::MEDIUM);item.clip={float(visible.x()),float(visible.y()),float(visible.width()),float(visible.height())};items.push_back(std::move(item));}}
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
  std::u16string text_;SkColor tone_;int delta_;gfx::Font::Weight weight_;bool multiline_;bool native_style_,center_;int fixed_width_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;mutable int measured_width_=-1,measured_font_=0,natural_width_=-1;mutable std::vector<std::string> lines_;mutable std::unique_ptr<gfx::RenderText> wrapped_;
};
BEGIN_METADATA(PreferenceCopy)
END_METADATA
class SceneProgress final : public views::View {
  METADATA_HEADER(SceneProgress,views::View)
 public:
  SceneProgress(base::RepeatingClosure changed,base::RepeatingCallback<bool()> visible):changed_(std::move(changed)),visible_(std::move(visible)){GetViewAccessibility().SetRole(ax::mojom::Role::kProgressIndicator);SetCanProcessEventsWithinSubtree(false);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(1,5);}
  void SetValue(double value){const double next=!std::isfinite(value)||value<0||value>1?-1:value;if(next==value_)return;value_=next;if(value_<0)started_=base::TimeTicks::Now();AX();SchedulePaint();changed_.Run();}
  bool moving() const{return value_<0;}
  sidebar::Item Scene(gfx::Rect box,gfx::Rect clip) const{sidebar::Item item{};item.kind=26;item.x=box.x();item.y=box.y();item.width=box.width();item.height=box.height();item.title_width=value_;item.detail_font_size=started_.is_null()?0:std::fmod((base::TimeTicks::Now()-started_).InSecondsF(),2.0)/2.0;item.tile=base::i18n::IsRTL();item.ink=GetColorProvider()->GetColor(ui::kColorProgressBar);item.fill=GetColorProvider()->GetColor(ui::kColorProgressBarBackground);item.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};return item;}
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
  double value_=0;int announced_=-1;base::TimeTicks started_;base::RepeatingClosure changed_;base::RepeatingCallback<bool()> visible_;
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
  void Collect(views::View* view,std::vector<sidebar::Item>& backgrounds,std::vector<sidebar::Item>& text,std::vector<sidebar::FloatRect>& holes){if(!SceneIsVisible(view))return;auto clip=Projected(view);if(clip.IsEmpty())return;auto box=view->GetBoundsInScreen();box.Offset(-root_->GetBoundsInScreen().x(),-root_->GetBoundsInScreen().y());if(auto* progress=views::AsViewClass<SceneProgress>(view)){text.push_back(progress->Scene(box,clip));moving_|=progress->moving();return;}if(auto* actions=views::AsViewClass<SceneActionGroup>(view);actions&&actions->shared()){actions->Append(text,box.origin(),clip);return;}if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->Append(text,box.origin(),clip);return;}if(auto* row=views::AsViewClass<RustRowView>(view);row&&row->history()){text.push_back(row->Scene(gfx::RectF(box),gfx::RectF(clip)));return;}if(auto* surface=views::AsViewClass<PreferenceSurface>(view))if(surface->scene())backgrounds.push_back(surface->Scene(gfx::RectF(box),gfx::RectF(clip)));if(views::AsViewClass<SceneActionGroup>(view)||views::AsViewClass<SceneCheckboxGroup>(view)||views::AsViewClass<ScenePreferenceControls>(view)||views::AsViewClass<SceneSelect>(view)||views::AsViewClass<SceneToggle>(view)||views::AsViewClass<views::Button>(view)||view->GetViewAccessibility().GetCachedRole()==ax::mojom::Role::kTextField){holes.push_back({float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())});return;}for(const auto& child:view->children())Collect(child.get(),backgrounds,text,holes);}
  void Update(){pending_=false;auto* widget=root_->GetWidget();if(!widget)return;if(observation_.GetSource()!=widget){observation_.Reset();observation_.Observe(widget);}widget->LayoutRootViewIfNecessary();const bool was=visible();if(!SceneIsVisible(root_)){timer_.Stop();if(surface_)surface_->Hide();return;}moving_=false;std::vector<sidebar::Item> backgrounds,text;std::vector<sidebar::FloatRect> holes;Collect(root_,backgrounds,text,holes);backgrounds.insert(backgrounds.end(),std::make_move_iterator(text.begin()),std::make_move_iterator(text.end()));last_ops_=sidebar::preference_chrome_scene(rust::Slice<const sidebar::Item>(backgrounds.data(),backgrounds.size()),rust::Slice<const sidebar::FloatRect>(holes.data(),holes.size()));if(!surface_)surface_=CreateSidebarMetal();if(surface_)surface_->UpdateUnderlay(widget->GetNativeWindow(),root_->GetBoundsInScreen(),UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(last_ops_.data(),last_ops_.size())));if(was!=visible())root_->SchedulePaint();if(moving_){if(!timer_.IsRunning())timer_.Start(FROM_HERE,base::Milliseconds(16),base::BindRepeating(&PreferenceChromeRenderer::Tick,base::Unretained(this)));}else timer_.Stop();}
  void Tick(){if(!visible())root_->SchedulePaint();Schedule();}
  base::RepeatingTimer timer_;bool moving_=false;
  raw_ptr<views::View> root_;bool pending_=false;std::unique_ptr<SidebarMetal> surface_;rust::Vec<sidebar::Primitive> last_ops_;base::ScopedObservation<views::Widget,views::WidgetObserver> observation_{this};base::WeakPtrFactory<PreferenceChromeRenderer> weak_factory_{this};
};

class PanelSceneImpl final : public PanelScene {
 public:
  explicit PanelSceneImpl(views::View* root):root_(root),renderer_(root){}
  std::unique_ptr<views::View> MakeText(std::u16string text,bool multiline,bool center,int fixed_width) override{
    return std::make_unique<PreferenceCopy>(std::move(text),kInk,0,gfx::Font::Weight::NORMAL,multiline,base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()),true,center,fixed_width);
  }
  std::unique_ptr<views::View> MakeSelect(std::vector<std::u16string> labels,std::u16string name) override{return std::make_unique<SceneSelect>(std::move(labels),std::move(name),base::RepeatingClosure());}
  void SetText(views::View* view,std::u16string text) override{static_cast<PreferenceCopy*>(view)->SetText(std::move(text));}
  std::unique_ptr<views::View> MakeActions(int spacing) override{auto group=std::make_unique<SceneActionGroup>(false,spacing);group->Share(base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()),base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()));return group;}
  views::View* AddAction(views::View* group,base::RepeatingClosure callback,std::u16string text) override{return static_cast<SceneActionGroup*>(group)->AddAction(std::move(callback),std::move(text),ui::ButtonStyle::kDefault);}
  void SetActionText(views::View* button,std::u16string text) override{static_cast<SceneTextButton*>(button)->SetText(std::move(text));}
  std::unique_ptr<views::View> MakeProgress() override{return std::make_unique<SceneProgress>(base::BindRepeating(&PanelSceneImpl::Schedule,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<PanelSceneImpl> self){return self&&self->visible();},weak_factory_.GetWeakPtr()));}
  void SetProgress(views::View* view,double value) override{static_cast<SceneProgress*>(view)->SetValue(value);}
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
#endif

#if BUILDFLAG(IS_MAC)
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
    const int width=std::max(0,requested);const auto font=Font(delta_,bold_?gfx::Font::Weight::MEDIUM:gfx::Font::Weight::NORMAL);
    if(width!=measured_width_||font.GetFontSize()!=measured_font_){lines_=ShapeSidebarParagraph(base::UTF16ToUTF8(text_),font.GetFontSize(),width,bold_);measured_width_=width;measured_font_=font.GetFontSize();}
    return sidebar::paragraph_layout(width,font.GetHeight(),lines_.size());
  }
  std::vector<sidebar::Item> Items(float dx,float dy,gfx::RectF clip) const {
    const auto g=Geometry(width());std::vector<sidebar::Item> items;items.reserve(g.children.size());
    for(size_t i=0;i<g.children.size();++i){const auto& r=g.children[i];sidebar::Item item{};item.kind=17;item.x=r.x+dx;item.y=r.y+dy;item.width=r.width;item.height=r.height;item.title=lines_[i];item.font_size=Font(delta_).GetFontSize();item.active=bold_;item.ink=Tone(this,ink_);item.clip={clip.x(),clip.y(),clip.width(),clip.height()};items.push_back(std::move(item));}return items;
  }
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneParagraph::UpdateMetal,weak_factory_.GetWeakPtr()));}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();const bool was=surface_&&surface_->visible();const auto local=GetVisibleBounds();
    if(!SceneIsVisible(this)||local.IsEmpty()||text_.empty()){if(surface_)surface_->Hide();if(was)SchedulePaint();return;}
    if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;
    auto frame=GetBoundsInScreen();frame.Offset(local.x(),local.y());frame.set_size(local.size());const auto items=Items(-local.x(),-local.y(),gfx::RectF(gfx::Rect(frame.size())));
    surface_->Update(GetWidget()->GetNativeWindow(),frame,items,Tone(this,kCard));if(was!=surface_->visible())SchedulePaint();
  }
  std::u16string text_;int delta_;bool bold_;SkColor ink_;mutable int measured_width_=-1,measured_font_=0;mutable std::vector<std::string> lines_;
  std::unique_ptr<SidebarMetal> surface_;bool pending_=false;SceneWindowObserver window_visibility_{base::BindRepeating(&SceneParagraph::SyncWindowVisibility,base::Unretained(this))};base::WeakPtrFactory<SceneParagraph> weak_factory_{this};
};
BEGIN_METADATA(SceneParagraph)
END_METADATA
#endif
void SetPanelText(views::View* view,std::u16string text) {
#if BUILDFLAG(IS_MAC)
  if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->SetText(std::move(text));return;}
  if(auto* scene=views::AsViewClass<SceneParagraph>(view)){scene->SetText(std::move(text));return;}
#endif
  static_cast<views::Label*>(view)->SetText(text);
}
#if BUILDFLAG(IS_MAC)
sidebar::ImportSelection ImportUiSelection(const BrowserImportOptions& o){return {static_cast<uint32_t>(o.browser),o.separate_profiles,o.bookmarks,o.history,o.cookies};}
BrowserImportOptions ImportServiceOptions(const sidebar::ImportSelection& s){BrowserImportOptions o;o.browser=static_cast<ImportBrowser>(s.browser);o.separate_profiles=s.separate_profiles;o.bookmarks=s.bookmarks;o.history=s.history;o.cookies=s.cookies;return o;}
std::vector<sidebar::ProfileOption> ProfileOptions(const ShellState& state){std::vector<sidebar::ProfileOption> out;out.reserve(state.profiles.size());for(const auto& p:state.profiles)out.push_back({p.id,p.name});return out;}
std::vector<sidebar::ProfileUse> ProfileUses(const ShellState& state){std::vector<sidebar::ProfileUse> out;out.reserve(state.workspaces.size());for(const auto& w:state.workspaces)out.push_back({w.id,w.name,w.profile_id});return out;}
#endif

class EdgeView final : public views::View {
  METADATA_HEADER(EdgeView, views::View)
 public:
  explicit EdgeView(base::RepeatingClosure show) : show_(std::move(show)) {}
  void OnMouseEntered(const ui::MouseEvent&) override { show_.Run(); }
  void OnPaint(gfx::Canvas* canvas) override {
    FillRound(canvas, gfx::Rect(2, std::max(0, height() / 2 - 18), 3, 36), 2, color);
  }
  SkColor color = 0xFF9E91DC;
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
class NativeSidebarGrip final : public views::ResizeArea, public views::ResizeAreaDelegate,public ResizeHandle {
  METADATA_HEADER(NativeSidebarGrip, views::ResizeArea)
 public:
  NativeSidebarGrip(base::RepeatingCallback<void(int,bool)> resized,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      :views::ResizeArea(this),resized_(std::move(resized)),painted_(std::move(painted)),visible_(std::move(visible)) {
    GetViewAccessibility().SetName(u"Zmień szerokość panelu bocznego");SetProperty(views::kViewIgnoredByLayoutKey,true);
  }
  views::View* view() override{return this;}
  bool active() const override{return active_;} bool hovered() const override{return hovered_;}
  void OnResize(int amount,bool done) override{active_=!done;SchedulePaint();resized_.Run(amount,done);}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void OnMouseEntered(const ui::MouseEvent&) override{hovered_=true;SchedulePaint();}
  void OnMouseExited(const ui::MouseEvent&) override{hovered_=false;SchedulePaint();}
  bool OnMousePressed(const ui::MouseEvent& event) override {
    return ResizeArea::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    return ResizeArea::OnMouseDragged(event);
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    ResizeArea::OnMouseReleased(event);
  }
  void OnMouseCaptureLost() override {
    ResizeArea::OnMouseCaptureLost();
  }
  void VisibilityChanged(views::View* view,bool visible) override {
    ResizeArea::VisibilityChanged(view,visible);
  }
  void OnGestureEvent(ui::GestureEvent* event) override {
    ResizeArea::OnGestureEvent(event);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if(visible_&&visible_.Run())return;
    if(!hovered_&&!active_)return;
    FillRound(canvas,gfx::Rect(width()-3,8,2,std::max(0,height()-16)),1,SkColorSetA(Tone(this,kInk),active_?0x40:0x26));
  }
 private:
  base::RepeatingCallback<void(int,bool)> resized_;
  base::RepeatingClosure painted_;
  base::RepeatingCallback<bool()> visible_;
  bool hovered_=false,active_=false;
};
BEGIN_METADATA(NativeSidebarGrip)
END_METADATA

#if BUILDFLAG(IS_MAC)
class RustResizeHandle final : public views::View,public ResizeHandle {
  METADATA_HEADER(RustResizeHandle,views::View)
 public:
  RustResizeHandle(base::RepeatingCallback<int()> width,base::RepeatingCallback<void(int,bool)> applied,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      :width_(std::move(width)),applied_(std::move(applied)),painted_(std::move(painted)),visible_(std::move(visible)){width_drag_.emplace(sidebar::new_sidebar_drag());Init();GetViewAccessibility().SetName(u"Zmień szerokość panelu bocznego");SetProperty(views::kViewIgnoredByLayoutKey,true);}
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
#endif
std::unique_ptr<views::View> CreateSidebarGrip(ResizeHandle** handle,base::RepeatingCallback<void(int,bool)> resized,base::RepeatingCallback<int()> width,base::RepeatingCallback<void(int,bool)> applied,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible){
#if BUILDFLAG(IS_MAC)
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal"){auto view=std::make_unique<RustResizeHandle>(std::move(width),std::move(applied),std::move(painted),std::move(visible));*handle=view.get();return view;}
#endif
  auto view=std::make_unique<NativeSidebarGrip>(std::move(resized),std::move(painted),std::move(visible));*handle=view.get();return view;
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
#if BUILDFLAG(IS_MAC)
      if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal") {const auto r=sidebar::sidebar_grip_layout(width(),height());grip->SetBounds(r.x,r.y,r.width,r.height);return;}
#endif
      grip->SetBounds(width()-6,0,6,height());
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
#if BUILDFLAG(IS_MAC)
  void SetSceneCapture(base::RepeatingCallback<std::vector<sidebar::Item>(int)> capture,unsigned background) {
    scene_capture_=std::move(capture);background_=background;
  }
#endif
  void Move(double position, int count, bool settle) {
    const auto plan=SidebarSwipeLayout(position,count,width());
    const int center=plan.center;
    center_ = center;
#if BUILDFLAG(IS_MAC)
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
#endif
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
#if BUILDFLAG(IS_MAC)
    if(metal_owned_)return surfaces_.size();
#endif
    return pages_.size();
  }
  bool metal_owned() const {
#if BUILDFLAG(IS_MAC)
    return metal_owned_;
#else
    return false;
#endif
  }
  size_t native_capture_count() const{return native_capture_count_;}
  size_t metal_frame_count() const {
    size_t count=0;
#if BUILDFLAG(IS_MAC)
    for(const auto& [index,surface]:surfaces_)if(surface)count+=surface->frame_count();
#endif
    return count;
  }
  bool contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
    if(metal_owned_)for(const auto& [index,surface]:surfaces_)if(surface&&surface->visible()&&surface->contains_text_for_testing(text))return true;
#endif
    return false;
  }
  int center() const { return center_; }
 private:
  int center_ = -1;
  size_t native_capture_count_=0;
#if BUILDFLAG(IS_MAC)
  base::RepeatingCallback<std::vector<sidebar::Item>(int)> scene_capture_;
  std::map<int,std::vector<sidebar::Item>> scenes_;
  std::map<int,std::unique_ptr<SidebarMetal>> surfaces_;
  gfx::Size scene_size_;
  unsigned background_=0;
  bool metal_owned_=false;
#endif
  base::RepeatingCallback<cc::PaintRecord(int)> capture_;
  std::map<int, std::unique_ptr<SwipePage>> pages_;
  std::map<int, cc::PaintRecord> records_;
};
BEGIN_METADATA(WorkspaceSwipePreview)
END_METADATA

#if BUILDFLAG(IS_MAC)
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
#endif

// Split View divider with a visible grip so the pane boundary reads as draggable.
class NativeSplitDivider final : public views::ResizeArea,public ResizeHandle {
  METADATA_HEADER(NativeSplitDivider, views::ResizeArea)
 public:
  NativeSplitDivider(views::ResizeAreaDelegate* delegate,base::RepeatingClosure painted):ResizeArea(delegate),painted_(std::move(painted)){}
  views::View* view() override{return this;}
  void SetVertical(bool vertical) override{set_axis(vertical?Axis::kVertical:Axis::kHorizontal);}
  bool hovered() const override {return hovered_;}
  void OnDidSchedulePaint(const gfx::Rect& rect) override {views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void OnMouseEntered(const ui::MouseEvent&) override { hovered_ = true; SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent&) override { hovered_ = false; SchedulePaint(); }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    return ResizeArea::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    return ResizeArea::OnMouseDragged(event);
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    ResizeArea::OnMouseReleased(event);
  }
  void OnMouseCaptureLost() override {
    ResizeArea::OnMouseCaptureLost();
  }
  void OnGestureEvent(ui::GestureEvent* event) override {
    ResizeArea::OnGestureEvent(event);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    views::ResizeArea::OnPaint(canvas);
    const bool across = width() > height();
    const int grip = std::min(40, (across ? width() : height()) / 4);
    const gfx::Rect bar = across ? gfx::Rect(width() / 2 - grip / 2, height() / 2 - 2, grip, 4)
                                 : gfx::Rect(width() / 2 - 2, height() / 2 - grip / 2, 4, grip);
    FillRound(canvas, bar, 2, Tone(this, hovered_ ? kSubtle : kDisabled));
  }
 private:
  bool hovered_ = false;
  base::RepeatingClosure painted_;
};
BEGIN_METADATA(NativeSplitDivider)
END_METADATA

std::unique_ptr<views::View> CreateSplitDivider(ResizeHandle** handle,views::ResizeAreaDelegate* delegate,base::RepeatingCallback<double()> ratio,base::RepeatingCallback<int()> extent,base::RepeatingCallback<void(double,bool)> changed,base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible){
#if BUILDFLAG(IS_MAC)
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal"){auto view=std::make_unique<RustResizeHandle>(std::move(ratio),std::move(extent),std::move(changed),std::move(painted),std::move(visible));*handle=view.get();return view;}
#endif
  auto view=std::make_unique<NativeSplitDivider>(delegate,std::move(painted));*handle=view.get();return view;
}

// Slim header above each half of a split: page title, pane menu and close.
class PaneBar final : public views::View {
  METADATA_HEADER(PaneBar, views::View)
 public:
  PaneBar(views::Button::PressedCallback menu, views::Button::PressedCallback close,
      base::RepeatingClosure painted,base::RepeatingCallback<bool()> visible)
      : painted_(std::move(painted)),visible_(std::move(visible)) {
#if BUILDFLAG(IS_MAC)
    scene_=base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal";
#endif
    if(!scene_) {
      auto* layout=SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets::TLBR(0,12,0,4),0));
      layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      title_=Label(this,u"",kMuted,Font(-1));title_->SetElideBehavior(gfx::ELIDE_TAIL);title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);layout->SetFlexForView(title_,1);
    } else GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    menu_=AddChildView(CreateSceneAction(std::move(menu),u"Opcje Split View",ControlGlyph::kMore,nullptr,visible_,painted_));
    close_=AddChildView(CreateSceneAction(std::move(close),u"Zamknij kartę w Split View",ControlGlyph::kVector,&vector_icons::kCloseSmallIcon,visible_,painted_));
  }
  void CancelInput(){CancelActionInput(menu_);CancelActionInput(close_);}
  // A bar survives tab replacement and index reuse. Revoke pending input when
  // its domain target changes; title/focus updates keep an ordinary click alive.
  void SetTarget(const std::string& workspace,std::optional<size_t> index,
                 std::array<uint64_t,2> identity,bool visible){
    if(workspace_==workspace&&index_==index&&identity_==identity&&target_visible_==visible)return;
    CancelInput();workspace_=workspace;index_=index;identity_=identity;target_visible_=visible;
  }
  void SetTitle(std::u16string title,bool active) {
    if(text_==title && active_==active)return;
    if(text_!=title)measured_.reset();text_=std::move(title);active_=active;
    if(title_){title_->SetText(text_);static_cast<ThemedLabel*>(title_.get())->SetTone(active?kInk:kMuted);}
    else GetViewAccessibility().SetName(text_);
    SchedulePaint();painted_.Run();
  }
  const std::u16string& text() const{return text_;}
  bool active() const{return active_;}
  int text_width() const{if(!measured_)measured_=gfx::GetStringWidth(text_,Font(-1))+2;return *measured_;}
  void OnThemeChanged() override{views::View::OnThemeChanged();measured_.reset();painted_.Run();}
  void OnDidSchedulePaint(const gfx::Rect& rect) override{views::View::OnDidSchedulePaint(rect);painted_.Run();}
  void Layout(PassKey) override {
#if BUILDFLAG(IS_MAC)
    if(scene_){const auto g=sidebar::pane_bar_layout(width(),height());menu_->SetBounds(g.menu.x,g.menu.y,g.menu.width,g.menu.height);close_->SetBounds(g.close.x,g.close.y,g.close.width,g.close.height);return;}
#endif
    LayoutSuperclass<views::View>(this);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if(!scene_ || (visible_ && visible_.Run()))return;
#if BUILDFLAG(IS_MAC)
    const auto g=sidebar::pane_bar_layout(width(),height());
    canvas->DrawStringRectWithFlags(text_,Font(-1),Tone(this,active_?kInk:kMuted),gfx::Rect(g.title.x,g.title.y,g.title.width,g.title.height),gfx::Canvas::TEXT_ALIGN_LEFT);
#endif
  }
  views::View* menu() const { return menu_; }
  views::View* close() const { return close_; }
 private:
  bool scene_=false,active_=false;
  std::u16string text_;
  mutable std::optional<int> measured_;
  base::RepeatingClosure painted_;
  base::RepeatingCallback<bool()> visible_;
  raw_ptr<views::Label> title_ = nullptr;
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
// two selected pages. Native ResizeArea supplies capture and resize cursors.
class PaneHost final : public views::View, public views::ResizeAreaDelegate {
  METADATA_HEADER(PaneHost, views::View)
 public:
  PaneHost(base::RepeatingCallback<void(double, bool)> changed,
           base::RepeatingCallback<void(bool, views::View*)> menu,
           base::RepeatingCallback<void(bool)> close,base::RepeatingClosure painted,
           base::RepeatingCallback<bool(size_t)> visible)
      : painted_(std::move(painted)),changed_(std::move(changed)) {
#if BUILDFLAG(IS_MAC)
    rust_=base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal";
#endif
    ResizeHandle* divider_handle=nullptr;
    divider_=AddChildView(CreateSplitDivider(&divider_handle,this,
        base::BindRepeating([](PaneHost* host){return host->ratio_;},base::Unretained(this)),
        base::BindRepeating([](PaneHost* host){return host->vertical_?host->height():host->width();},base::Unretained(this)),
        base::BindRepeating(&PaneHost::ApplyRustRatio,base::Unretained(this)),painted_,base::BindRepeating(visible,size_t{2})));
    divider_handle_=divider_handle;
    if(!rust_)divider_->SetBackground(views::CreateSolidBackground(kChip));
    divider_->GetViewAccessibility().SetName(u"Zmień szerokość paneli");
    divider_->SetVisible(false);
    for (bool right : {false, true}) {
      auto* bar = AddChildView(std::make_unique<PaneBar>(views::Button::PressedCallback(),
                                                         base::BindRepeating(close, right),painted_,base::BindRepeating(visible,size_t{right})));
      SetActionCallback(bar->menu(),base::BindRepeating(menu,right,base::Unretained(bar->menu())));
      bar->SetVisible(false);
      bars_[right] = bar;
    }
  }
  PaneBar* bar(bool right) const { return bars_[right]; }
  views::View* divider() const{return divider_;}
  ResizeHandle* divider_handle() const{return divider_handle_;}
  void SetPanes(views::View* left, views::View* right, double ratio, bool vertical) {
    if (left_ == left && right_ == right && ratio_ == ratio &&
        vertical_ == vertical) return;
    if(left_!=left || right_!=right || vertical_!=vertical){divider_handle_->CancelDrag();for(auto& bar:bars_)bar->CancelInput();}
    left_ = left; right_ = right; ratio_ = ratio; vertical_ = vertical;
    divider_handle_->SetVertical(vertical);
    divider_->SetVisible(right != nullptr);
    for (auto& bar : bars_) bar->SetVisible(right != nullptr);
    InvalidateLayout();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if(!rust_)divider_->SetBackground(views::CreateSolidBackground(Tone(this, kChip)));
    painted_.Run();
  }
  void Layout(PassKey) override {
#if BUILDFLAG(IS_MAC)
    if(rust_) {
      const auto g=sidebar::pane_host_layout(width(),height(),ratio_,vertical_,right_!=nullptr);
      auto set=[](views::View* view,const sidebar::Rect& r){if(view)view->SetBounds(r.x,r.y,r.width,r.height);};
      set(left_,g.first);set(right_,g.second);set(bars_[0],g.first_bar);set(bars_[1],g.second_bar);set(divider_,g.divider);
      LayoutSuperclass<views::View>(this);painted_.Run();return;
    }
#endif
    constexpr int offset = 0;
    if (!right_) {
      if (left_) left_->SetBounds(offset, 0, width(), height());
      LayoutSuperclass<views::View>(this);
      return;
    }
    // Each half: a header bar, then the page below it.
    auto place = [&](views::View* page, PaneBar* bar, gfx::Rect area) {
      bar->SetBoundsRect(gfx::Rect(area.x(), area.y(), area.width(), std::min(kPaneBarHeight, area.height())));
      area.Inset(gfx::Insets::TLBR(std::min(kPaneBarHeight, area.height()), 0, 0, 0));
      if (page) page->SetBoundsRect(area);
    };
    if (vertical_) {
      // Top/bottom panes; the workspace slide still moves horizontally.
      const int available = std::max(0, height() - 8);
      const int top_height = std::clamp(static_cast<int>(available * ratio_), 0, available);
      place(left_, bars_[0], gfx::Rect(offset, 0, width(), top_height));
      divider_->SetBounds(offset, top_height, width(), 8);
      place(right_, bars_[1], gfx::Rect(offset, top_height + 8, width(), available - top_height));
    } else {
      const int available = std::max(0, width() - 8);
      const int left_width = std::clamp(static_cast<int>(available * ratio_), 0, available);
      place(left_, bars_[0], gfx::Rect(offset, 0, left_width, height()));
      divider_->SetBounds(left_width + offset, 0, 8, height());
      place(right_, bars_[1], gfx::Rect(left_width + 8 + offset, 0, available - left_width, height()));
    }
    LayoutSuperclass<views::View>(this);
  }
  void OnResize(int amount, bool done) override {
    if (!resizing_) { initial_ratio_ = ratio_; resizing_ = true; }
    const int extent = vertical_ ? height() : width();
    ratio_ = std::clamp(initial_ratio_ + static_cast<double>(amount) / std::max(1, extent - 8), 0.2, 0.8);
    InvalidateLayout();
    changed_.Run(ratio_, done);
    if (done) resizing_ = false;
  }
 private:
  void ApplyRustRatio(double ratio,bool done){ratio_=ratio;InvalidateLayout();painted_.Run();changed_.Run(ratio,done);}
  bool rust_=false;
  base::RepeatingClosure painted_;
  raw_ptr<views::View> left_ = nullptr;
  raw_ptr<views::View> right_ = nullptr;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<ResizeHandle> divider_handle_=nullptr;
  std::array<raw_ptr<PaneBar>, 2> bars_ = {};
  base::RepeatingCallback<void(double, bool)> changed_;
  double ratio_ = 0.5;
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
    // Centered on the 28 px macOS titlebar line, next to the traffic lights.
    header->SetBounds(kTrafficLightInset, -2, header_width, 32);
    const int top = kShellHeaderHeight;
    content->SetBounds(column + kGap, top, std::max(0, width() - column - 2 * kGap),
                       std::max(0, height() - top - kGap));
    edge->SetBounds(0, kShellHeaderHeight, kGap, std::max(0, height() - kShellHeaderHeight));
    edge->SetVisible(!pinned);
#if BUILDFLAG(IS_MAC)
    if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal") {
      const auto g=sidebar::shell_layout(width(),height(),sidebar_width,pinned);
      content->SetBounds(g.content.x,g.content.y,g.content.width,g.content.height);
      edge->SetBounds(g.edge.x,g.edge.y,g.edge.width,g.edge.height);
    }
#endif
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
#if BUILDFLAG(IS_MAC)
  if(auto* action=views::AsViewClass<RustActionView>(view))return action->Snapshot().visual==sidebar::RowVisual::Pressed;
#endif
  auto* button=views::AsViewClass<views::Button>(view);return button&&button->GetState()==views::Button::STATE_PRESSED;
}
bool PageDialogActionsVisibleForTesting(views::View* actions,std::string_view text){
#if BUILDFLAG(IS_MAC)
  auto* group=views::AsViewClass<SceneActionGroup>(actions);return group&&group->contains_text(text);
#else
  return false;
#endif
}
std::unique_ptr<views::View> CreatePageDialogActions(views::Widget* widget){
#if BUILDFLAG(IS_MAC)
  auto* delegate=widget->widget_delegate()->AsDialogDelegate();
  auto group=std::make_unique<SceneActionGroup>(false,views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL),true,true);
  auto add=[&](ui::mojom::DialogButton type){
    if(!(delegate->buttons()&static_cast<int>(type)))return;
    auto* button=static_cast<SceneTextButton*>(group->AddAction({},delegate->GetDialogButtonLabel(type),delegate->GetDialogButtonStyle(type)));
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
#else
  return nullptr;
#endif
}

std::unique_ptr<views::DialogDelegate> CreateSceneModalDialog(std::unique_ptr<views::View> body,bool control,bool modal){
#if BUILDFLAG(IS_MAC)
  return std::make_unique<SceneModalDialog>(std::move(body),control,modal);
#else
  return nullptr;
#endif
}
std::unique_ptr<PanelScene> CreatePanelScene(views::View* root){
#if BUILDFLAG(IS_MAC)
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")return std::make_unique<PanelSceneImpl>(root);
#endif
  return nullptr;
}
std::unique_ptr<PageDialogScene> CreatePageDialogScene(views::View* root){return CreatePanelScene(root);}
#if BUILDFLAG(IS_MAC)
class PreferenceChromeState final : public PreferenceChromeRenderer {public:using PreferenceChromeRenderer::PreferenceChromeRenderer;};
#else
class PreferenceChromeState {};
#endif

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
  auto root = std::make_unique<ShellRoot>();
  root_ = root.get();
  root->SetBackground(views::CreateSolidBackground(FrameTint(workspace_color("work"), root_)));
  auto weak = weak_factory_.GetWeakPtr();
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal") metal_header_=CreateSidebarMetal();
#endif
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
      base::BindRepeating(&WorkspaceShell::ToggleSidebar,weak),u"Pokaż lub ukryj karty",
      GlyphButton::Glyph::kSidebar,nullptr,header_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalHeader,weak)));
  const std::array<std::pair<const gfx::VectorIcon*, std::u16string>, 3> commands = {{
      {&vector_icons::kArrowBackIcon, u"Wstecz"},
      {&vector_icons::kArrowForwardIcon, u"Dalej"},
      {&vector_icons::kReloadCustomIcon, u"Odśwież"}}};
  for (size_t i = 0; i < commands.size(); ++i) {
    nav_buttons_[i] = header->AddChildView(CreateSceneAction(
        base::BindRepeating(callbacks_.page_command, static_cast<PageCommand>(i)),
        commands[i].second, GlyphButton::Glyph::kVector, commands[i].first,header_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalHeader,weak)));
  }
  content_host_ = root->AddChildView(std::make_unique<PaneHost>(callbacks_.split_ratio,
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, bool right, views::View* anchor) {
        if (shell) shell->ShowSplitPaneMenu(right, anchor->GetBoundsInScreen().bottom_left());
      }, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, bool right) {
        if (shell) shell->CloseSplitPane(right);
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

void WorkspaceShell::SetPanes(views::View* left, views::View* right, double ratio, bool vertical) {
  static_cast<PaneHost*>(content_host_.get())->SetPanes(left, right, ratio, vertical);
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
#if BUILDFLAG(IS_MAC)
        if(shell->metal_sidebar_)hide=sidebar::sidebar_auto_hide(shell->pinned_,blocked,inside,dragging);
#endif
        if(hide) {
          shell->sidebar_widget_->Hide();
          shell->RestorePageFocus();
        }
      }, weak));
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal") {
    metal_sidebar_ = CreateSidebarMetal();
    sidebar->painted = base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar, weak);
  }
#endif
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
  auto* layout = sidebar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(2, 4, 8, 4), 2));
  auto* pill = sidebar->AddChildView(CreateAddressRow(
      base::BindRepeating(&WorkspaceShell::OpenAddress, weak, false),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        return shell && shell->metal_sidebar_visible();
      },weak),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak)));
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
  RowButton::Spec new_tab;
  new_tab.title = u"Nowa karta";
  new_tab.detail = new_tab_hint;
  new_tab.sidebar_row = true;
  new_tab.scene_visible = base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    return shell && shell->metal_sidebar_visible();
  }, weak);
  new_tab.icon = &vector_icons::kAdd2Icon;
  new_tab_button_ = sidebar->AddChildView(CreateSceneRow(
      base::BindRepeating(&WorkspaceShell::OpenAddress,weak,true),std::move(new_tab),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak)));
  std::unique_ptr<views::ScrollBar> vertical_bar;
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)vertical_bar=CreateSidebarScrollBar(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_sidebar_visible();},weak));
#endif
  PanelViewport* tab_viewport=nullptr;
  auto viewport_host=CreatePanelViewport(&tab_viewport,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),{0,std::numeric_limits<int>::max(),false},std::move(vertical_bar));tab_scroll_=tab_viewport;
  auto* scroll=sidebar->AddChildView(std::move(viewport_host));scroll->set_context_menu_controller(this);scroll->SetPreferredSize(gfx::Size(200,0));
  auto list = std::make_unique<SidebarListView>(
      base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        return shell && (shell->metal_sidebar_visible()||shell->workspace_swipe_metal_visible()||shell->workspace_switch_metal_visible());
      }, weak));
  list->SetLayoutManager(CreateSidebarColumnLayout(2));
  tab_list_=list.get();tab_scroll_->SetPanelContents(std::move(list));
  tab_list_->set_context_menu_controller(this);
  layout->SetFlexForView(scroll, 1);

  // Workspace settings: profile and color live here, not in the sidebar.
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_){workspace_form_=std::make_unique<WorkspaceFormState>();tools_form_=std::make_unique<ToolsFormState>();}
#endif
  std::array<std::u16string,17> workspace_copy={u"Nazwa przestrzeni",u"Np. Projekt lub Klient",u"Profil przeglądania",u"Profil przestrzeni",u"Wspólny profil = wspólne logowanie i cookies.\nZmiana profilu zamknie karty tej przestrzeni.",u"Zapisz poniżej nazwę i profil. Wygląd zapisuje się od razu.",u"Zamknij ustawienia przestrzeni",u"Wygląd · zapis automatyczny",u"Kolor przestrzeni",u"Kolor przestrzeni #RRGGBB",u"Emoji przestrzeni",u"Emoji przestrzeni",u"Inne… (⌃⌘Spacja)",u"Więcej opcji",u"Zarządzaj profilami",u"Importuj z przeglądarki…",u"Usuń przestrzeń…"};
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){const auto c=sidebar::workspace_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};workspace_copy={text(c.name_label),text(c.placeholder),text(c.profile_label),text(c.profile_ax),text(c.profile_hint),text(c.save_hint),text(c.close),text(c.appearance),text(c.color),text(c.color_ax),text(c.emoji),text(c.emoji_ax),text(c.emoji_placeholder),text(c.more),text(c.manage),text(c.import_label),text(c.remove_label)};}
#endif
  auto settings_text=[&](views::View* parent,std::u16string text,SkColor ink,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false)->views::View* {
#if BUILDFLAG(IS_MAC)
    if(workspace_form_)return parent->AddChildView(std::make_unique<SceneParagraph>(std::move(text),delta,weight!=gfx::Font::Weight::NORMAL,ink));
#endif
    auto* label=Label(parent,std::move(text),ink,Font(delta,weight));label->SetMultiLine(multiline);return label;
  };
  auto settings = std::make_unique<views::View>();
  settings_root_ = settings.get();
  settings->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 8));
  settings->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  settings->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto* settings_header = settings->AddChildView(std::make_unique<views::View>());
  auto* settings_header_layout = settings_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  settings_title_ = settings_text(settings_header,u"Praca",kInk,2,gfx::Font::Weight::SEMIBOLD);
  settings_header_layout->SetFlexForView(settings_title_, 1);
  settings_header->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) { shell->settings_widget_->Hide(); shell->RestorePageFocus(); }
      }, weak), workspace_copy[6], GlyphButton::Glyph::kVector, &vector_icons::kCloseSmallIcon));
  profile_name_ = settings_text(settings.get(),u"",kMuted,-1);
  settings_text(settings.get(),workspace_copy[0],kSubtle,-1);
  workspace_input_ = AddEditor(settings.get(),bool(workspace_form_));
  workspace_input_->GetViewAccessibility().SetName(workspace_copy[0]);
  workspace_input_->SetPlaceholderText(workspace_copy[1]);
  settings_text(settings.get(),workspace_copy[2],kSubtle,-1);
  profile_picker_=ProfilePicker(settings.get(),workspace_copy[3],bool(workspace_form_),base::BindRepeating([]{}));
  settings_text(settings.get(),workspace_copy[4],kMuted,-2,gfx::Font::Weight::NORMAL,true);
  settings_text(settings.get(),workspace_copy[5],kMuted,-2,gfx::Font::Weight::NORMAL,true);
  auto* actions=ActionGroup(settings.get(),bool(workspace_form_),false,8);
  edit_workspace_ = AddAction(actions,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->SubmitWorkspace(shell->creating_workspace_);
      }, weak),u"Zapisz nazwę i profil",ui::ButtonStyle::kProminent);
  add_workspace_ = AddAction(actions,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if(shell)shell->ToggleWorkspaceCreation();
      }, weak),u"Nowa przestrzeń…");
  configuration_error_ = settings_text(settings.get(),u"",kError,-2,gfx::Font::Weight::NORMAL,true);
  configuration_error_->SetVisible(false);
  auto* appearance = settings->AddChildView(std::make_unique<views::View>());
  workspace_appearance_ = appearance;
  appearance->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  Spacer(appearance, 8);
  settings_text(appearance,workspace_copy[7],kInk,0,gfx::Font::Weight::SEMIBOLD);
  settings_text(appearance,workspace_copy[8],kSubtle,-1,gfx::Font::Weight::MEDIUM);
  auto appearance_row=[&](bool emoji)->views::View* {
#if BUILDFLAG(IS_MAC)
    if(workspace_form_)return appearance->AddChildView(std::make_unique<AppearanceRow>(emoji));
#endif
    return appearance->AddChildView(std::make_unique<views::View>());
  };
  auto* color_row=appearance_row(false);views::BoxLayout* color_layout=nullptr;
  if(!workspace_form_){color_layout=color_row->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),0));color_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);}
  std::vector<std::pair<SkColor,std::u16string>> colors;for(const auto c:kWorkspaceColors)colors.emplace_back(c,base::UTF8ToUTF16(Hex(c)));
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){colors.clear();for(const auto& p:sidebar::appearance_presets(false))colors.emplace_back(p.color,base::UTF8ToUTF16(std::string(p.label)));}
#endif
  for (const auto& [color,label] : colors) {
    auto* swatch = AppearancePicker(color_row,
        base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, SkColor value) {
          if (shell) shell->SetWorkspaceColor(shell->state_.workspace_id, value);
        }, weak, color), label, GlyphButton::Glyph::kSwatch);
    SetActionColor(swatch,color);
  }
  Spacer(color_row, 1)->SetPreferredSize(gfx::Size(8, 1));
  color_input_ = AddEditor(color_row,bool(workspace_form_));
  color_input_->GetViewAccessibility().SetName(workspace_copy[9]);
  color_input_->SetPlaceholderText(u"#RRGGBB");
  if(auto* native=views::AsViewClass<views::Textfield>(color_input_->view())){
    native->SetFontList(Font(-1));
  native->SetBackgroundColor(kChip);
  native->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(1, 7, Hairline(root_)), gfx::Insets::VH(4, 8)));
  }
  if(color_layout)color_layout->SetFlexForView(color_input_->view(), 1);
  color_error_ = settings_text(appearance,u"",kError,-2,gfx::Font::Weight::NORMAL,true);
  color_error_->SetVisible(false);
  Spacer(appearance, 6);
  settings_text(appearance,workspace_copy[10],kSubtle,-1,gfx::Font::Weight::MEDIUM);
  auto* emoji_row=appearance_row(true);views::BoxLayout* emoji_layout=nullptr;
  if(!workspace_form_){emoji_layout=emoji_row->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),0));emoji_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);}
  std::vector<std::pair<std::u16string,std::u16string>> emojis;for(const auto* e:kWorkspaceEmojis)emojis.emplace_back(e,u"Emoji "+std::u16string(e));
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){emojis.clear();for(const auto& p:sidebar::appearance_presets(true))emojis.emplace_back(base::UTF8ToUTF16(std::string(p.emoji)),base::UTF8ToUTF16(std::string(p.label)));}
#endif
  for (const auto& [emoji,label] : emojis) {
    auto* pick = AppearancePicker(emoji_row,
        base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, std::u16string value) {
          if (shell) shell->SetWorkspaceEmoji(shell->state_.workspace_id, value);
        }, weak, std::u16string(emoji)), label, GlyphButton::Glyph::kDot);
    SetActionLabel(pick,emoji);
  }
  Spacer(emoji_row, 1)->SetPreferredSize(gfx::Size(8, 1));
  emoji_input_ = AddEditor(emoji_row,bool(workspace_form_));
  emoji_input_->GetViewAccessibility().SetName(workspace_copy[11]);
  emoji_input_->SetPlaceholderText(workspace_copy[12]);
  if(auto* native=views::AsViewClass<views::Textfield>(emoji_input_->view())){
    native->SetFontList(Font(-1));
  native->SetBackgroundColor(kChip);
  native->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(1, 7, Hairline(root_)), gfx::Insets::VH(4, 8)));
  }
  if(emoji_layout)emoji_layout->SetFlexForView(emoji_input_->view(), 1);

  color_input_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleAppearanceEditorKey(false,event);},weak));
  emoji_input_->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(shell&&shell->emoji_input_&&!shell->emoji_input_->IsIMEComposing())
      shell->SetWorkspaceEmoji(shell->state_.workspace_id,std::u16string(shell->emoji_input_->GetText()));
  },weak),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleAppearanceEditorKey(true,event);},weak));

  Spacer(appearance, 8);
  settings_text(appearance,workspace_copy[13],kInk,0,gfx::Font::Weight::SEMIBOLD);
  auto* more_actions=ActionGroup(appearance,bool(workspace_form_),true,8);
  AddAction(more_actions,
      base::BindRepeating(&WorkspaceShell::OpenProfiles, weak, std::string(), std::string()), workspace_copy[14],ui::ButtonStyle::kText);
  AddAction(more_actions,
      base::BindRepeating(&WorkspaceShell::OpenImport, weak), workspace_copy[15],ui::ButtonStyle::kText);
  remove_workspace_ = AddAction(more_actions,
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->ConfirmRemoveWorkspace(shell->state_.workspace_id);
      }, weak), workspace_copy[16],ui::ButtonStyle::kText);

  if(workspace_form_){settings->SetLayoutManager(CreateSidebarColumnLayout(8));settings->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(20)));appearance->SetLayoutManager(CreateSidebarColumnLayout(8));}

  auto* footer = sidebar->AddChildView(std::make_unique<views::View>());
  sidebar_footer_=footer;
  auto footer_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    return shell && shell->metal_sidebar_visible();
  },weak);
  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  footer_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  pin_button_ = footer->AddChildView(CreateSceneAction(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->SetPinned(!shell->pinned_);
      }, weak), u"Przypnij panel", GlyphButton::Glyph::kSidebar,nullptr,footer_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak)));
  Spacer(footer, 1);
  std::unique_ptr<views::ScrollBar> workspace_bar;
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)workspace_bar=CreateSidebarScrollBar(footer_visible,true);
#endif
  PanelViewport* workspace_viewport=nullptr;
  auto* dot_scroll=footer->AddChildView(CreatePanelViewport(&workspace_viewport,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak),{-1,-1,metal_sidebar_!=nullptr,false,false,false,true},std::move(workspace_bar)));
  workspace_scroll_=workspace_viewport;
  dot_scroll->SetPreferredSize(gfx::Size(0,32));
  auto dots = std::make_unique<views::View>();
  dots->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  workspace_dots_=dots.get();
  workspace_scroll_->SetPanelContents(std::move(dots));
#if BUILDFLAG(IS_MAC)
  if (metal_sidebar_) {
    workspace_dots_->SetLayoutManager(CreateSidebarDotsLayout());

  }
#endif
  footer_layout->SetFlexForView(dot_scroll, 1);
  Spacer(footer, 1);
  workspace_settings_button_=footer->AddChildView(CreateSceneAction(
      base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings, weak),
      u"Ustawienia przestrzeni", GlyphButton::Glyph::kVector, &vector_icons::kTuneIcon,footer_visible,base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak)));
#if BUILDFLAG(IS_MAC)
  if (metal_sidebar_) footer->SetLayoutManager(CreateSidebarFooterLayout());
#endif

  // Command palette: input, suggestions, keyboard hints.
  auto palette=std::make_unique<PaletteView>(base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak));
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)metal_palette_=CreateSidebarMetal();
#endif
  palette_root_ = palette.get();
  palette->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  if(palette->scene())palette->SetBorder(views::CreateEmptyBorder(1));
  else {palette->SetBackground(views::CreateRoundedRectBackground(kCard,14));palette->SetBorder(views::CreateRoundedRectBorder(1,14,Hairline(root_)));}
#if BUILDFLAG(IS_MAC)
  auto chrome_visible=[weak](size_t index){return base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,size_t index){return shell&&shell->metal_palette_chrome_visible(index);},weak,index);};
  auto chrome_painted=base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak);
#endif
  auto palette_text=[&](views::View* parent,std::u16string text,uint32_t kind,[[maybe_unused]] size_t index)->views::View* {
#if BUILDFLAG(IS_MAC)
    if(palette->scene())return parent->AddChildView(std::make_unique<PaletteText>(std::move(text),kind,chrome_painted,chrome_visible(index)));
#endif
    auto* label=Label(parent,std::move(text),kind==2?kError:kMuted,Font(kind==2?-1:-2));
    if(kind==0)label->SetBorder(views::CreateRoundedRectBorder(1,5,Hairline(root_)));
    else if(kind==1)label->SetBorder(views::CreatePaddedBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1,0,0,0),Hairline(root_)),gfx::Insets::TLBR(10,18,10,18)));
    else label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8,18)));
    return label;
  };
  auto* input_row = palette->AddChildView(std::make_unique<views::View>());
  auto* input_layout = input_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 18), 12));
  input_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  input_row->SetPreferredSize(gfx::Size(560, 56));
  std::unique_ptr<views::View> search_control;
#if BUILDFLAG(IS_MAC)
  if(palette->scene())search_control=std::make_unique<SceneSearchIcon>(chrome_visible(0));
  else
#endif
    search_control=std::make_unique<GlyphButton>(views::Button::PressedCallback(),u"Szukaj",GlyphButton::Glyph::kVector,&vector_icons::kSearchIcon);
  auto* search_icon=input_row->AddChildView(std::move(search_control));
  palette_chrome_views_[0]=search_icon;
  search_icon->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  search_icon->SetCanProcessEventsWithinSubtree(false);
  address_ = AddEditor(input_row,palette->scene());
  address_->GetViewAccessibility().SetName(u"Szukaj lub wpisz adres");
  address_->SetPlaceholderText(u"Szukaj lub wpisz adres…");
  address_->SetPresentation(Font(3).GetFontSize(),0,false,Tone(root_,kCard));
  address_->SetCallbacks(base::BindRepeating(&WorkspaceShell::AddressEdited,weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,const ui::KeyEvent& event){return shell&&shell->HandleAddressKey(event);},weak));
  palette->SetEditor(address_->view());input_layout->SetFlexForView(address_->view(),1);
  palette_chrome_views_[1]=palette_text(input_row,u"esc",0,1);
  if(metal_palette_){palette->SetLayoutManager(CreateSidebarColumnLayout());input_row->SetLayoutManager(CreatePaletteInputLayout());}
  address_error_=palette_text(palette.get(),u"",2,3);palette_chrome_views_[3]=address_error_;
  address_error_->SetVisible(false);
  std::unique_ptr<views::ScrollBar> palette_bar;
#if BUILDFLAG(IS_MAC)
  if(metal_palette_)palette_bar=CreateSidebarScrollBar(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak));
#endif
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
#if BUILDFLAG(IS_MAC)
  if(metal_palette_) {
    suggestion_list_->SetLayoutManager(CreateSidebarColumnLayout(2));
    suggestion_list_->SetBorder(views::CreatePaddedBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1,0,0,0),Hairline(root_)),gfx::Insets(8)));
  }
#endif
  palette_chrome_views_[2]=palette_text(palette.get(),u"↑↓ wybierz     ↵ wykonaj / otwórz     > polecenia",1,2);

#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)profile_form_=std::make_unique<ProfileFormState>();
#endif
  std::array<std::u16string,12> profile_copy={u"Profile przeglądania",u"Każdy profil ma osobne logowania i dane stron.\nMożesz przypisać go do kilku przestrzeni.",u"Nazwa profilu",u"Nazwa nowego profilu",u"Np. Klient, Projekt, Konto testowe",u"Istniejące profile",u"Zarządzany profil",u"Utwórz profil",u"Zapisz nazwę profilu",u"Usuń profil i jego dane",u"Potwierdź usunięcie profilu i danych",u"Wróć do przestrzeni"};
#if BUILDFLAG(IS_MAC)
  if(profile_form_){const auto c=sidebar::profile_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};profile_copy={text(c.title),text(c.hint),text(c.name_label),text(c.name_ax),text(c.placeholder),text(c.existing),text(c.picker_ax),text(c.create),text(c.rename),text(c.remove_label),text(c.confirm_delete),text(c.back)};}
#endif
  auto profiles_panel = std::make_unique<views::View>();
  profiles_root_ = profiles_panel.get();
  profiles_panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 10));
  profiles_panel->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  profiles_panel->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto profile_text=[&](std::u16string text,SkColor ink,int delta,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false)->views::View* {
#if BUILDFLAG(IS_MAC)
    if(profile_form_)return profiles_panel->AddChildView(std::make_unique<SceneParagraph>(std::move(text),delta,weight!=gfx::Font::Weight::NORMAL,ink));
#endif
    auto* label=Label(profiles_panel.get(),std::move(text),ink,Font(delta,weight));label->SetMultiLine(multiline);return label;
  };
  profile_text(profile_copy[0],kInk,3,gfx::Font::Weight::SEMIBOLD);
  profile_text(profile_copy[1],kMuted,-1,gfx::Font::Weight::NORMAL,true);
  profile_text(profile_copy[2],kSubtle,-1);
  profile_name_input_ = AddEditor(profiles_panel.get(),bool(profile_form_));
  profile_name_input_->GetViewAccessibility().SetName(profile_copy[3]);
  profile_name_input_->SetPlaceholderText(profile_copy[4]);
  auto* profile_create_actions=ActionGroup(profiles_panel.get(),bool(profile_form_),true,10);
  create_profile_ = AddAction(profile_create_actions,
      base::BindRepeating(&WorkspaceShell::SubmitProfile, weak), profile_copy[7]);
  rename_profile_ = AddAction(profile_create_actions,
      base::BindRepeating(&WorkspaceShell::RenameSelectedProfile, weak), profile_copy[8]);
  profile_text(profile_copy[5],kSubtle,-1);
  managed_profile_picker_=ProfilePicker(profiles_panel.get(),profile_copy[6],bool(profile_form_),base::BindRepeating(&WorkspaceShell::ManagedProfileSelectionChanged,weak));
  profile_status_=profile_text(u"",kSubtle,-1,gfx::Font::Weight::NORMAL,true);
  auto* profile_delete_actions=ActionGroup(profiles_panel.get(),bool(profile_form_),true,10);
  delete_profile_ = AddAction(profile_delete_actions,
      base::BindRepeating(&WorkspaceShell::DeleteSelectedProfile, weak), profile_copy[9]);
  AddAction(profile_delete_actions,base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings, weak), profile_copy[11]);

  if(profile_form_){profiles_panel->SetLayoutManager(CreateSidebarColumnLayout(10));profiles_panel->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(20)));}

#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)import_form_=std::make_unique<ImportFormState>();
#endif
  std::array<std::u16string,10> import_copy={u"Przenieś się do Zittera",u"Zamknij import",u"Wybierz przeglądarkę i dane, które chcesz przenieść.",u"Przeglądarka źródłowa",u"Przeglądarka do importu",u"Co chcesz przenieść?",u"Oddzielne profile przeglądania",u"Historia przeglądania",u"Ciasteczka i sesje witryn",u"Importuj"};
  std::vector<std::u16string> import_browsers={u"Arc",u"Google Chrome",u"Microsoft Edge",u"Brave"};
#if BUILDFLAG(IS_MAC)
  if(import_form_){const auto c=sidebar::import_copy();auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};import_copy={text(c.title),text(c.dismiss),text(c.intro),text(c.source_label),text(c.source_ax),text(c.heading),text(c.profiles),text(c.history),text(c.cookies),text(c.apply_label)};import_browsers.clear();for(const auto& b:c.browsers)import_browsers.push_back(text(b));}
#endif
  auto import_text=[&](views::View* parent,std::u16string text,SkColor ink,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false)->views::View* {
#if BUILDFLAG(IS_MAC)
    if(import_form_)return parent->AddChildView(std::make_unique<SceneParagraph>(std::move(text),delta,weight!=gfx::Font::Weight::NORMAL,ink));
#endif
    auto* label=Label(parent,std::move(text),ink,Font(delta,weight));label->SetMultiLine(multiline);return label;
  };
  auto import_panel = std::make_unique<views::View>();
  import_root_ = import_panel.get();
  import_panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 12));
  import_panel->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  import_panel->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto* import_header = import_panel->AddChildView(std::make_unique<views::View>());
  auto* import_header_layout = import_header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  auto* import_title = import_text(import_header,import_copy[0],kInk,4,gfx::Font::Weight::SEMIBOLD);
  import_header_layout->SetFlexForView(import_title, 1);
  auto dismiss_import = base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (!shell || shell->import_applying_) return;
    shell->callbacks_.dismiss_import.Run();
    shell->CloseOverlays();
    shell->RestorePageFocus();
  }, weak);
#if BUILDFLAG(IS_MAC)
  if(import_form_){auto* controls=import_header->AddChildView(std::make_unique<ScenePreferenceControls>(true));dismiss_import_=controls->AddControl(dismiss_import,import_copy[1],0);}else
#endif
  dismiss_import_=import_header->AddChildView(std::make_unique<GlyphButton>(dismiss_import,import_copy[1],GlyphButton::Glyph::kVector,&vector_icons::kCloseSmallIcon));
  import_text(import_panel.get(),import_copy[2],kMuted,-1,gfx::Font::Weight::NORMAL,true);
  import_text(import_panel.get(),import_copy[3],kSubtle,-1);
  auto source_changed=base::BindRepeating(&WorkspaceShell::ImportSelectionChanged,weak);
#if BUILDFLAG(IS_MAC)
  if(import_form_)import_browser_picker_=import_panel->AddChildView(std::make_unique<SceneSelect>(import_browsers,import_copy[4],source_changed));else
#endif
  {
    std::vector<ui::SimpleComboboxModel::Item> items;for(const auto& b:import_browsers)items.emplace_back(b);
    auto* picker=import_panel->AddChildView(std::make_unique<views::Combobox>(std::make_unique<ui::SimpleComboboxModel>(items)));picker->SetTooltipTextAndAccessibleName(import_copy[4]);picker->SetSelectedIndex(0);picker->SetCallback(source_changed);import_browser_picker_=picker;
  }
  Spacer(import_panel.get(), 8);
  import_text(import_panel.get(),import_copy[5],kInk,0,gfx::Font::Weight::SEMIBOLD);
  auto selection_changed = base::BindRepeating(&WorkspaceShell::ImportSelectionChanged, weak);
  views::View* import_choices=import_panel.get();
#if BUILDFLAG(IS_MAC)
  if(import_form_)import_choices=import_panel->AddChildView(std::make_unique<SceneCheckboxGroup>(12));
#endif
  import_profiles_ = AddImportCheck(import_choices,import_copy[6],selection_changed);
  import_bookmarks_ = AddImportCheck(import_choices,u"Przypięte karty, foldery i Ulubione",selection_changed);
  for (auto* checkbox : {import_profiles_.get(), import_bookmarks_.get()}) {
    SetCheckValue(checkbox,true);
    if(auto* native=views::AsViewClass<views::Checkbox>(checkbox))native->SetMultiLine(true);
  }
  import_history_ = AddImportCheck(import_choices,import_copy[7],selection_changed);
  import_cookies_ = AddImportCheck(import_choices,import_copy[8],selection_changed);
  Spacer(import_panel.get(), 8);
  import_status_ = import_text(import_panel.get(),u"",kSubtle,-1,gfx::Font::Weight::NORMAL,true);
  import_status_->SetVisible(false);
  auto* import_actions=ActionGroup(import_panel.get(),bool(import_form_),true,0);
  apply_import_ = AddAction(import_actions,base::BindRepeating(&WorkspaceShell::SubmitImport,weak),import_copy[9]);
  apply_import_->SetEnabled(false);
#if BUILDFLAG(IS_MAC)
  if(import_form_){const auto d=sidebar::import_defaults();SetCheckValue(import_profiles_,d.separate_profiles);SetCheckValue(import_bookmarks_,d.bookmarks);SetCheckValue(import_history_,d.history);SetCheckValue(import_cookies_,d.cookies);import_panel->SetLayoutManager(CreateSidebarColumnLayout(12));import_panel->SetBorder(views::CreatePaddedBorder(views::CreateRoundedRectBorder(1,12,Hairline(root_)),gfx::Insets(20)));}
#endif


  auto general = std::make_unique<views::View>();
  general_root_ = general.get();
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)preferences_chrome_=std::make_unique<PreferenceChromeState>(general_root_);
#endif
  auto preference_chrome_painted=base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak);
  auto preference_chrome_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak);
  auto copy=[&](views::View* parent,std::u16string text,SkColor tone,int delta=0,gfx::Font::Weight weight=gfx::Font::Weight::NORMAL,bool multiline=false){return AddPreferenceCopy(parent,std::move(text),tone,delta,static_cast<int>(weight),multiline);};
  auto* navigation = general->AddChildView(std::make_unique<PreferenceSurface>(true,bool(workspace_form_),preference_chrome_painted,preference_chrome_visible));
  preferences_navigation_ = navigation;
  navigation->SetPreferredSize(gfx::Size(212, 1));
  copy(navigation, u"zitter", kInk, 8, gfx::Font::Weight::BOLD);
  copy(navigation, u"TWOJA PRZEGLĄDARKA", kMuted, -3, gfx::Font::Weight::MEDIUM);
  Spacer(navigation, 20);
  auto* main = general->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(general.get(),false,gfx::Insets(8),8,main);
  auto* preferences_heading = main->AddChildView(std::make_unique<views::View>());
  auto* heading_text = preferences_heading->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(heading_text,true,gfx::Insets(),5);
  copy(heading_text, u"Preferencje", kInk, 9, gfx::Font::Weight::SEMIBOLD);
  copy(heading_text, u"Twoja przestrzeń. Twoje zasady.", kMuted, 0);
  SetSidebarPanelLayout(preferences_heading,false,gfx::Insets(),12,heading_text);
  auto dismiss=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell){shell->CloseOverlays();shell->RestorePageFocus();}},weak);
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){auto* controls=preferences_heading->AddChildView(std::make_unique<ScenePreferenceControls>(true));preferences_close_=controls->AddControl(dismiss,u"Zamknij preferencje (Escape)",0);}
  else
#endif
  preferences_close_=preferences_heading->AddChildView(std::make_unique<GlyphButton>(dismiss,u"Zamknij preferencje (Escape)",GlyphButton::Glyph::kVector,&vector_icons::kCloseIcon));
  preferences_search_ = AddEditor(main,bool(workspace_form_));
  preferences_search_->SetPlaceholderText(u"Szukaj ustawień…");
  preferences_search_->GetViewAccessibility().SetName(u"Szukaj we wszystkich ustawieniach");
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
      u"Wygląd", u"Wyszukiwanie", u"Prywatność", u"Uprawnienia stron", u"Treści i czytelność", u"Hasła i profile", u"Pobieranie", u"Języki i pisownia", u"Dane przeglądania", u"Zaawansowane"};
  std::vector<std::u16string> descriptions = {
      u"Dopasuj Zitter do swojego rytmu.",
      u"Wybierz, gdzie zaczyna się każde wyszukiwanie.",
      u"Zdecyduj, jakie ślady zostawiasz w sieci.",
      u"Ustal domyślny dostęp witryn do urządzenia.",
      u"Wygodne czytanie i kontrola zawartości stron.",
      u"Oddzielne logowania, wspólne możliwości.", u"Wybierz miejsce dla pobieranych plików.",
      u"Wybierz języki witryn i pomoc podczas pisania.",
      u"Kontroluj dane zapisane w bieżącym profilu.",
      u"Dostępność, połączenia i ustawienia silnika Chromium."};
#if BUILDFLAG(IS_MAC)
  std::optional<sidebar::PreferenceCatalogue> preference_catalogue;
  if(workspace_form_){
    preference_catalogue=sidebar::preference_catalogue(true);
    auto strings=[](const auto& input){std::vector<std::u16string> output;for(const auto& value:input)output.push_back(base::UTF8ToUTF16(std::string(value)));return output;};
    categories=strings(preference_catalogue->categories);descriptions=strings(preference_catalogue->descriptions);
  }
#endif
  preferences_category_picker_=ChoicePicker(main,u"Kategoria preferencji",categories,bool(workspace_form_));
  preferences_category_picker_->SetVisible(false);
  SetPickerCallback(preferences_category_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if(shell)shell->SelectPreferenceCategory(PickerIndex(shell->preferences_category_picker_).value_or(0));
  },weak));
  views::View* category_controls=navigation;
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){
    // This UI-only controller retains category selection across view rebuilds.
    if(!preferences_form_)preferences_form_=std::make_unique<PreferencesFormState>(categories.size());
    category_controls=navigation->AddChildView(std::make_unique<ScenePreferenceControls>(false));
  }
#endif
  for (size_t i = 0; i < categories.size(); ++i) {
    const std::array icons = {&vector_icons::kSettingsIcon, &vector_icons::kSearchIcon,
        &vector_icons::kShieldIcon, &vector_icons::kTuneIcon, &vector_icons::kGlobeIcon,
        &vector_icons::kLockIcon, &vector_icons::kDownloadIcon, &vector_icons::kGlobeIcon,
        &vector_icons::kShieldIcon, &vector_icons::kTuneIcon};
    RowButton::Spec spec;
    spec.title = categories[i]; spec.icon = icons[i]; spec.height = 40;
    views::View* button=nullptr;
#if BUILDFLAG(IS_MAC)
    if(auto* controls=views::AsViewClass<ScenePreferenceControls>(category_controls))button=controls->AddControl(base::BindRepeating(&WorkspaceShell::SelectPreferenceCategory,weak,i),categories[i],i);
    else
#endif
    button=category_controls->AddChildView(std::make_unique<RowButton>(base::BindRepeating(&WorkspaceShell::SelectPreferenceCategory,weak,i),std::move(spec)));
    auto* section = body->AddChildView(std::make_unique<views::View>());
    SetSidebarPanelLayout(section,true,gfx::Insets(),12);
    copy(section, categories[i],kInk,4,gfx::Font::Weight::SEMIBOLD);
    copy(section,descriptions[i],kMuted,-1,gfx::Font::Weight::NORMAL,true);
    preference_sections_.push_back({section, button, {}});
  }
  auto* nav_space = Spacer(navigation, 1);
  SetSidebarPanelLayout(navigation,true,gfx::Insets(16),6,nav_space);
  copy(navigation, u"ZITTER / PREFERENCJE", kMuted, -3);
#if BUILDFLAG(IS_MAC)
  if(preference_catalogue)BuildRustPreferenceRows(*preference_catalogue);
  else
#endif
  {
  auto make_row = [&](size_t category, std::u16string title, std::u16string detail) {
    auto* row = preference_sections_[category].view->AddChildView(std::make_unique<PreferenceSurface>(false,bool(workspace_form_),preference_chrome_painted,preference_chrome_visible));
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(16), 20));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* text = row->AddChildView(std::make_unique<views::View>());
    text->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(), 5));
    copy(text,title,kInk,0,gfx::Font::Weight::MEDIUM,true);
    copy(text,detail,kMuted,-1,gfx::Font::Weight::NORMAL,true);
    layout->SetFlexForView(text, 1);
    preference_sections_[category].rows.push_back({row, base::i18n::ToLower(categories[category] + u" " + title + u" " + detail)});
    return row;
  };
  auto picker = [&](views::View* row,std::u16string name,std::vector<std::u16string> choices){
    return ChoicePicker(row,std::move(name),std::move(choices),bool(workspace_form_));
  };
  auto toggle = [&](views::View* row,std::u16string name){return PreferenceToggle(row,std::move(name),bool(workspace_form_));};
  theme_picker_ = picker(make_row(0, u"Motyw aplikacji", u"Kolory interfejsu i stron obsługujących motyw systemowy."),
      u"Motyw aplikacji", {u"Systemowy", u"Jasny", u"Ciemny"});
  SetPickerCallback(theme_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (shell) shell->SetThemeMode(static_cast<ThemeMode>(PickerIndex(shell->theme_picker_).value_or(0)));
  }, weak));
  sidebar_check_ = toggle(make_row(0, u"Zawsze widoczny panel kart", u"Przypnij karty i przestrzenie przy lewej krawędzi."), u"Zawsze widoczny panel kart");
  SetActionCallback(sidebar_check_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (shell) shell->SetPinned(ToggleOn(shell->sidebar_check_));
  }, weak));
  animations_check_ = toggle(make_row(0, u"Animacje przestrzeni", u"Płynne przejścia. Systemowe ograniczenie ruchu ma pierwszeństwo."), u"Animacje przestrzeni");
  SetActionCallback(animations_check_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (shell) shell->SetWorkspaceAnimations(ToggleOn(shell->animations_check_));
  }, weak));
  search_engine_picker_ = picker(make_row(1, u"Domyślna wyszukiwarka", u"Używana, gdy wpiszesz zapytanie w pasku adresu."),
      u"Domyślna wyszukiwarka", {u"DuckDuckGo", u"Google", u"Bing"});
  SetPickerCallback(search_engine_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (!shell) return;
    const size_t index = PickerIndex(shell->search_engine_picker_).value_or(0);
    if (index < shell->tools_.providers.size()) shell->SetDefaultSearchProvider(shell->tools_.providers[index].keyword);
  }, weak));
  auto add_toggle = [&](size_t category, std::string key, std::u16string title, std::u16string detail) {
    auto* control = toggle(make_row(category, title, detail), title);
    preference_toggles_[key] = control;
    SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, std::string key) {
      if (shell) shell->WritePreference(key, ToggleOn(shell->preference_toggles_.at(key)));
    }, weak, key));
  };
  auto add_text = [&](size_t category, std::string key, std::u16string title, std::u16string detail, std::u16string placeholder) {
    auto* row = make_row(category, title, detail);
    auto* field = AddEditor(row->children().front(),bool(workspace_form_));
    field->GetViewAccessibility().SetName(title);
    field->SetPlaceholderText(placeholder);
    field->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){
      if(shell) shell->PreferenceTextEdited(key);
    },weak,key),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){
    return shell&&shell->HandlePreferenceEditorKey(field,event);
  },weak,base::Unretained(field)));
    field->view()->SetPreferredSize(gfx::Size(240, 36));
    preference_textfields_[key] = field;
    auto* save = PreferenceAction(row,bool(workspace_form_),
        base::BindRepeating(&WorkspaceShell::SaveTextPreference, weak, key), u"Zapisz");
    save->SetTooltipText(u"Zapisz: " + title);
    save->GetViewAccessibility().SetName(u"Zapisz: " + title);
    preference_save_buttons_[key] = save;
  };
  add_toggle(2, "referrers", u"Przekazuj adres strony odsyłającej", u"Nagłówek Referer pomaga witrynie rozpoznać, skąd pochodzi wejście. Wyłączenie może zmienić działanie niektórych serwisów.");
  add_toggle(2, "https_only", u"Zawsze używaj HTTPS", u"Wymagaj bezpiecznego połączenia. Witryny bez HTTPS mogą wyświetlić ostrzeżenie.");
  add_toggle(2, "third_party_cookies", u"Blokuj cookies innych firm", u"Ogranicz śledzenie między witrynami. Niektóre logowania mogą wymagać cookies.");
  add_toggle(2, "do_not_track", u"Wysyłaj Do Not Track", u"Poproś witryny o nieśledzenie. Respektowanie prośby zależy od witryny.");
  add_toggle(2, "preconnect", u"Wstępne połączenia ze stronami", u"Przygotowuj połączenie podczas wpisywania adresu. Może użyć sieci, nawet jeśli nie otworzysz strony. Ustawienie dotyczy tego profilu.");
  add_toggle(2, "link_auditing", u"Raporty kliknięć w linki", u"Zezwalaj na wysyłanie sygnałów ping po kliknięciu odnośnika.");
  const std::u16string permission_hint = u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo.";
  add_toggle(3, "camera", u"Kamera", permission_hint);
  add_toggle(3, "microphone", u"Mikrofon", permission_hint);
  add_toggle(3, "location", u"Lokalizacja", permission_hint);
  add_toggle(3, "notifications", u"Powiadomienia", permission_hint);
  add_toggle(3, "clipboard", u"Odczyt schowka", permission_hint);
  auto add_picker = [&](size_t category, std::string key, std::u16string title, std::u16string detail, std::vector<std::u16string> choices) {
    auto* control = picker(make_row(category, title, detail), title, choices);
    preference_pickers_[key] = control;
    SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, std::string key) {
      if (shell) {
        auto index = PickerIndex(shell->preference_pickers_.at(key));
        if (index) shell->WritePreference(key, static_cast<int>(*index));
      }
    }, weak, key));
  };
  add_picker(4, "zoom", u"Domyślne powiększenie", u"Skala stron bez własnego, zapisanego powiększenia.", {u"75%", u"90%", u"100%", u"110%", u"125%", u"150%", u"175%", u"200%"});
  add_picker(4, "font_size", u"Rozmiar tekstu", u"Domyślna wielkość czcionki na stronach.", {u"12 px", u"14 px", u"16 px", u"18 px", u"20 px", u"24 px"});
  add_picker(4, "minimum_font_size", u"Minimalny rozmiar tekstu", u"Zapobiegaj wyświetlaniu zbyt drobnych liter.", {u"Bez limitu", u"10 px", u"12 px", u"14 px", u"16 px", u"18 px"});
  add_picker(4, "fixed_font_size", u"Rozmiar tekstu o stałej szerokości", u"Domyślna wielkość tekstu w blokach kodu i elementach monospace.", {u"12 px", u"14 px", u"16 px", u"18 px", u"20 px", u"24 px"});
  add_text(4, "font_standard", u"Czcionka standardowa", u"Nazwa rodziny czcionki używanej, gdy witryna nie wybierze własnej.", u"Np. Times New Roman");
  add_text(4, "font_serif", u"Czcionka szeryfowa", u"Rodzina dla tekstu serif. Musi być zainstalowana w systemie.", u"Np. Georgia");
  add_text(4, "font_sans", u"Czcionka bezszeryfowa", u"Rodzina dla tekstu sans-serif. Musi być zainstalowana w systemie.", u"Np. Arial");
  add_text(4, "font_fixed", u"Czcionka o stałej szerokości", u"Rodzina monospace, używana m.in. w blokach kodu.", u"Np. Menlo");
  add_toggle(4, "javascript", u"JavaScript", u"Skrypty potrzebne do działania większości aplikacji internetowych.");
  add_toggle(4, "images", u"Wyświetlaj obrazy", u"Pozwól witrynom ładować zdjęcia i grafiki.");
  add_toggle(4, "popups", u"Wyskakujące okna", u"Pozwól stronom otwierać dodatkowe okna. Domyślnie zablokowane.");
  add_toggle(4, "sound", u"Dźwięk na stronach", u"Domyślna zgoda na odtwarzanie dźwięku.");
  add_toggle(5, "save_passwords", u"Proponuj zapisywanie haseł", u"Zapisuj dane logowania w menedżerze haseł tego profilu.");
  add_toggle(5, "auto_signin", u"Automatyczne logowanie", u"Korzystaj z zapisanych danych logowania, gdy witryna na to pozwala.");
  auto action = [&](size_t category, std::u16string title, std::u16string detail, base::RepeatingClosure callback) {
    auto* row = make_row(category, title, detail);
    auto* button = PreferenceAction(row,bool(workspace_form_),std::move(callback),u"Otwórz →");
    button->SetTooltipText(title);
    button->GetViewAccessibility().SetName(title);
  };
  action(2, u"Historia przeglądania", u"Wyszukuj odwiedzone strony i czyść historię tego profilu.", base::BindRepeating(&WorkspaceShell::OpenTools, weak, 3));
  action(0, u"Kolor i nazwa przestrzeni", u"Własny akcent kolorystyczny i przypisanie profilu.", base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings, weak));
  action(5, u"Zapisane hasła", u"Przeglądaj i usuwaj loginy aktywnego profilu.", base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell) { if (shell) { shell->CloseOverlays(); shell->callbacks_.passwords.Run(); } }, weak));
  action(5, u"Profile przeglądania", u"Oddziel cookies, historię i dane logowania między profilami.", base::BindRepeating(&WorkspaceShell::OpenProfiles, weak, std::string(), std::string()));
  action(5, u"Import z przeglądarki", u"Przenieś profile, przestrzenie, foldery i zakładki.", base::BindRepeating(&WorkspaceShell::OpenImport, weak));
  add_toggle(6, "download_prompt", u"Pytaj, gdzie zapisać każdy plik", u"Wybieraj miejsce zapisu przy każdym pobieraniu.");
  auto* directory_row = make_row(6, u"Folder pobierania", u"Domyślne miejsce zapisu plików w tym profilu.");
  download_directory_button_ = PreferenceAction(directory_row,bool(workspace_form_),
      callbacks_.choose_download_directory,u"Zmień…");
  download_directory_button_->SetTooltipText(u"Zmień folder pobierania");
  download_directory_button_->GetViewAccessibility().SetName(u"Zmień folder pobierania");
  download_directory_label_=copy(directory_row->children().front().get(),u"",kMuted,-1,gfx::Font::Weight::NORMAL,true);
  add_text(7, "accept_languages", u"Preferowane języki witryn", u"Kody języków w kolejności preferencji, oddzielone przecinkami. Np. pl, en-US, en. Zmiana dotyczy treści witryn; interfejs Zitter pozostaje po polsku.", u"pl, en-US, en");
  add_toggle(7, "spellcheck", u"Sprawdzaj pisownię", u"Podkreślaj błędy w polach tekstowych na stronach. Na macOS słowniki wybiera system.");
  add_toggle(7, "online_spellcheck", u"Rozszerzone sprawdzanie pisowni", u"Wysyłaj wpisywany tekst do usługi Google w celu sprawdzania pisowni. Wymaga włączonego sprawdzania pisowni.");
  add_toggle(9, "caret_browsing", u"Nawigacja kursorem tekstowym", u"Poruszaj się po tekstach witryn za pomocą klawiatury i zaznaczaj je bez myszy.");
  add_toggle(9, "hardware_acceleration", u"Akceleracja sprzętowa", u"Używaj procesora graficznego do renderowania. Ustawienie wspólne dla całej aplikacji; działa po ponownym uruchomieniu Zitter.");
  add_picker(9, "dns_mode", u"Bezpieczny DNS", u"Wspólne dla całej aplikacji. Tryb automatyczny może używać DNS systemowego. Tryb ścisły wymaga wcześniej zapisanego adresu DNS-over-HTTPS.", {u"Systemowy", u"Automatyczny", u"Ścisły (DoH)"});
  add_text(9, "dns_templates", u"Adres serwera DNS-over-HTTPS", u"Wspólny dla całej aplikacji. Wpisz szablon HTTPS, np. https://dns.google/dns-query{?dns}. Najpierw zapisz adres, następnie włącz tryb ścisły.", u"https://…/dns-query{?dns}");
  action(9, u"Ustawienia proxy systemowego", u"Zitter korzysta z konfiguracji proxy systemu operacyjnego.", base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (shell && shell->callbacks_.system_settings) shell->callbacks_.system_settings.Run(false);
  }, weak));
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  action(9, u"Certyfikaty systemowe", u"Otwórz systemowy magazyn certyfikatów i ustawienia zaufania.", base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (shell && shell->callbacks_.system_settings) shell->callbacks_.system_settings.Run(true);
  }, weak));
#endif
  auto* site_editor = make_row(3, u"Wyjątek dla witryny", u"Wpisz pełny adres witryny bez ścieżki, np. https://example.com. Wyjątek zastępuje politykę domyślną w tym profilu.");
  auto* site_fields = site_editor->children().front().get();
  site_origin_ = AddEditor(site_fields,bool(workspace_form_));
  site_origin_->GetViewAccessibility().SetName(u"Adres witryny dla wyjątku");
  site_origin_->SetPlaceholderText(u"https://example.com");
  site_origin_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){
    return shell&&shell->HandlePreferenceEditorKey(field,event);
  },weak,base::Unretained(site_origin_.get())));
  std::vector<std::u16string> site_names(kSitePreferenceNames.begin(), kSitePreferenceNames.end());
  site_type_ = picker(site_fields, u"Rodzaj uprawnienia dla witryny", site_names);
  site_policy_ = picker(site_fields, u"Polityka dla witryny", {u"Użyj domyślnej", u"Zezwalaj", u"Blokuj"});
  SetPickerCallback(site_type_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (!shell) return;
    std::vector<std::u16string> choices={u"Użyj domyślnej",u"Zezwalaj",u"Blokuj"};
    if(PickerIndex(shell->site_type_).value_or(0)>=4)choices.emplace_back(u"Pytaj");
    ReplacePickerLabels(shell->site_policy_,choices);
  }, weak));
  PreferenceAction(site_editor,bool(workspace_form_),base::BindRepeating(&WorkspaceShell::SaveSitePreference,weak),u"Zapisz wyjątek");
  site_rules_card_ = make_row(3, u"Zapisane wyjątki", u"Własne reguły możesz usunąć, aby wrócić do ustawień domyślnych. Reguły zarządzane pozostają tylko do odczytu.");
  site_rules_list_ = site_rules_card_->children().front()->AddChildView(std::make_unique<views::View>());
  site_rules_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  auto* clear_row = make_row(8, u"Wyczyść dane przeglądania", u"Dotyczy wyłącznie aktywnego profilu. Pobrane pliki, zapisane hasła i przypięte karty pozostaną zachowane. Przed usunięciem zobaczysz potwierdzenie.");
  auto* clear_fields = clear_row->children().front().get();
  clear_data_period_ = picker(clear_fields, u"Zakres czasu czyszczenia danych", {u"Ostatnia godzina", u"Ostatnie 24 godziny", u"Ostatnie 7 dni", u"Cały okres"});
  auto* clear_choices=clear_fields;
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)clear_choices=clear_fields->AddChildView(std::make_unique<SceneCheckboxGroup>(8));
#endif
  clear_history_=AddImportCheck(clear_choices,u"Historia przeglądania",{});
  clear_cookies_=AddImportCheck(clear_choices,u"Cookies i dane witryn (może wylogować)",{});
  clear_cache_=AddImportCheck(clear_choices,u"Pamięć podręczna",{});
  for(auto* checkbox:{clear_history_.get(),clear_cookies_.get(),clear_cache_.get()})
    if(auto* native=views::AsViewClass<views::Checkbox>(checkbox))native->SetMultiLine(true);
  SetCheckValue(clear_cache_,true);
  clear_data_button_ = PreferenceAction(clear_row,bool(workspace_form_),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
    if (!shell || !shell->callbacks_.clear_browsing_data) return;
    shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->clear_data_period_).value_or(0)),
        CheckValue(shell->clear_history_), CheckValue(shell->clear_cookies_), CheckValue(shell->clear_cache_)});
  }, weak), u"Wyczyść…");
  action(1, u"Wyszukiwarki i prefiksy", u"Własne wyszukiwarki: g zapytanie, w hasło, yt film…", base::BindRepeating(&WorkspaceShell::OpenTools, weak, 2));
  action(1, u"Skróty klawiszowe", u"Schemat Arc, własne kombinacje i wykrywanie konfliktów.", base::BindRepeating(&WorkspaceShell::OpenTools, weak, 1));
  action(0, u"Time Machine przestrzeni", u"Snapshoty, zamknięte karty i usypianie całych projektów.", base::BindRepeating(&WorkspaceShell::OpenTools, weak, 0));
  auto* shortcuts = make_row(1, u"Szybciej z klawiaturą", u"⌘ L  adres i wyszukiwanie\n⌘ K  karty, przestrzenie i historia\n⌘ T  nowa karta     ⌘ ,  preferencje");
  shortcuts->SetTooltipText(u"Skróty klawiaturowe Zitter");
  }
  preferences_empty_ = copy(body.get(), u"Brak pasujących ustawień. Spróbuj innego hasła.", kMuted,0,gfx::Font::Weight::NORMAL,true);
  general_scroll_->SetPanelContents(std::move(body));
  auto* main_scroll=main->AddChildView(std::move(general_scroll));
  SetSidebarPanelLayout(main,true,gfx::Insets::VH(16,20),12,main_scroll);
  general_status_ = copy(main, u"Przełączniki zapisują się automatycznie. Pola tekstowe zatwierdź przyciskiem Zapisz lub Enter.", kMuted,-2,gfx::Font::Weight::NORMAL,true);
  FilterPreferences();
  auto peek = std::make_unique<views::View>();
  auto* peek_layout = peek->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 8));
  peek->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  peek->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline(root_)));
  auto* peek_toolbar = peek->AddChildView(std::make_unique<views::View>());
  auto* toolbar_layout = peek_toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  toolbar_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  peek_toolbar->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating(callbacks_.page_command, PageCommand::kBack), u"Wstecz", GlyphButton::Glyph::kVector, &vector_icons::kArrowBackIcon));
  peek_toolbar->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating(callbacks_.page_command, PageCommand::kForward), u"Dalej", GlyphButton::Glyph::kVector, &vector_icons::kArrowForwardIcon));
  peek_title_ = Label(peek_toolbar, u"Peek", kInk, Font(-1, gfx::Font::Weight::SEMIBOLD));
  peek_title_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  toolbar_layout->SetFlexForView(peek_title_, 1);
  for (bool split : {false, true}) peek_toolbar->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, bool split) {
        if (!shell) return;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool split) {
              if (shell) shell->callbacks_.promote_peek.Run(split);
            }, shell, split));
      }, weak, split), split ? u"Split View" : u"Otwórz jako kartę"));
  peek_toolbar->AddChildView(std::make_unique<GlyphButton>(base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell) {
        if (!shell) return;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce([](base::WeakPtr<WorkspaceShell> shell) {
              if (shell) shell->callbacks_.close_peek.Run();
            }, shell));
      }, weak), u"Zamknij podgląd (Escape)", GlyphButton::Glyph::kVector, &vector_icons::kCloseIcon));
  peek_host_ = peek->AddChildView(std::make_unique<views::View>());
  peek_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  peek_layout->SetFlexForView(peek_host_, 1);

  auto tools_panel = std::make_unique<views::View>();
  tools_root_ = tools_panel.get();
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)tools_chrome_=std::make_unique<PreferenceChromeState>(tools_root_);
#endif
  auto* tools_heading = tools_panel->AddChildView(std::make_unique<views::View>());
  tools_title_ = AddToolsCopy(tools_heading,u"Time Machine",kInk,5,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
  auto dismiss_tools=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){
    if(shell){shell->SetShortcutRecording(false);shell->tools_widget_->Hide();shell->RestorePageFocus();}
  },weak);
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){auto* controls=tools_heading->AddChildView(std::make_unique<ScenePreferenceControls>(true));tools_close_=controls->AddControl(dismiss_tools,u"Zamknij narzędzia",0);}
  else
#endif
  tools_close_=tools_heading->AddChildView(std::make_unique<GlyphButton>(dismiss_tools,u"Zamknij narzędzia",GlyphButton::Glyph::kVector,&vector_icons::kCloseIcon));
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
  make_panel(std::move(sidebar), sidebar_delegate_, sidebar_widget_, u"Zitter — Karty", false);
  make_panel(std::move(palette), address_delegate_, address_widget_, u"Zitter — Paleta poleceń");
  PanelViewport* settings_viewport=nullptr;
  auto settings_scroll=CreatePanelViewport(&settings_viewport,base::BindRepeating(&WorkspaceShell::RefreshFormViewports,weak),{0,10000,false});
  settings_scroll_=settings_viewport;settings_viewport->SetPanelContents(std::move(settings));
  make_panel(std::move(settings_scroll), settings_delegate_, settings_widget_, u"Zitter — Ustawienia przestrzeni");
  make_panel(std::move(profiles_panel), profiles_delegate_, profiles_widget_, u"Zitter — Profile");
  PanelViewport* import_viewport=nullptr;
  auto import_scroll=CreatePanelViewport(&import_viewport,base::BindRepeating(&WorkspaceShell::RefreshFormViewports,weak),{0,10000,false});
  import_scroll_=import_viewport;import_viewport->SetPanelContents(std::move(import_panel));
  make_panel(std::move(import_scroll), import_delegate_, import_widget_, u"Zitter — Import danych");
  make_panel(std::move(peek), peek_delegate_, peek_widget_, u"Zitter — Peek");
  make_panel(std::move(general), general_delegate_, general_widget_, u"Zitter — Ustawienia");
  make_panel(std::move(tools_panel), tools_delegate_, tools_widget_, u"Zitter — Narzędzia");
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
  remove_workspace_dialog_.reset();remove_workspace_dialog_delegate_.reset();
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
  color_input_->SetCallbacks({},{});
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
  managed_profile_picker_ = nullptr;
  profile_name_input_ = nullptr;
  create_profile_ = nullptr;
  rename_profile_ = nullptr;
  delete_profile_ = nullptr;
  profile_status_ = nullptr;profile_form_.reset();
  address_ = nullptr;
  color_input_ = nullptr;
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
  settings_title_ = nullptr;
  workspace_appearance_ = nullptr;
  settings_scroll_ = nullptr;
  settings_root_ = nullptr;
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
#if BUILDFLAG(IS_MAC)
  if(preferences_form_)preferences_form_->form->preference_detach();
#endif
  preference_textfields_.clear(); preference_save_buttons_.clear(); preference_text_dirty_.clear();
  site_origin_ = nullptr; site_type_ = nullptr; site_policy_ = nullptr; site_rules_list_ = nullptr; site_rules_card_ = nullptr;
  clear_data_period_ = nullptr; clear_history_ = nullptr; clear_cookies_ = nullptr; clear_cache_ = nullptr; clear_data_button_ = nullptr;
  site_preference_rules_.clear(); preferences_profile_id_.clear();
  preference_sections_.clear(); preference_toggles_.clear(); preference_pickers_.clear();
  download_directory_label_ = nullptr; download_directory_button_ = nullptr;
  preferences_navigation_ = nullptr; preferences_category_picker_ = nullptr;preferences_close_=nullptr;
  preferences_search_ = nullptr; preferences_profile_ = nullptr; preferences_empty_ = nullptr; preferences_body_ = nullptr;
  general_root_ = nullptr; general_scroll_ = nullptr; general_status_ = nullptr;
  theme_picker_ = nullptr; search_engine_picker_ = nullptr;
  sidebar_check_ = nullptr; animations_check_ = nullptr;
  tools_root_ = nullptr; tools_body_ = nullptr; tools_close_=nullptr; tools_title_ = nullptr; tools_status_ = nullptr;
  tools_scroll_ = nullptr; tools_picker_ = nullptr; closed_picker_ = nullptr;
  snapshot_name_ = nullptr; timeline_filter_ = nullptr; shortcut_chord_ = nullptr;
  provider_name_ = nullptr; provider_keyword_ = nullptr; provider_url_ = nullptr;
  history_search_ = nullptr; history_period_ = nullptr; history_list_ = nullptr;
  history_more_ = nullptr; history_clear_ = nullptr;
  tools_widget_.reset(); tools_delegate_.reset();
  general_widget_.reset(); general_delegate_.reset();
  peek_host_ = nullptr; peek_title_ = nullptr;
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
  const auto managed_index = PickerIndex(managed_profile_picker_);
  const std::string managed_id = managed_index && *managed_index < state_.profiles.size()
      ? state_.profiles[*managed_index].id : std::string();
  const bool added_profile = state.profiles.size() > state_.profiles.size();
  const bool profiles_changed = state.profiles != state_.profiles;
  const auto previous_workspace = state_.workspace_id;
  if (swipe_overlay_ && (changed || (state.workspace_id != previous_workspace &&
      (!swipe_committing_ || state.workspace_id != swipe_commit_target_)))) CancelWorkspaceSwipe();
  const bool selected = state.workspace_id != previous_workspace;
  const bool history_profile_changed = state.profile_id != state_.profile_id;
  state_ = std::move(state);
  if (!widget_) return;
#if BUILDFLAG(IS_MAC)
  if(selected&&workspace_form_){for(auto* field:{color_input_.get(),emoji_input_.get()})if(auto* row=views::AsViewClass<AppearanceRow>(field->view()->parent()))row->CancelInput();CancelSceneActions(settings_root_);CancelSceneActions(profiles_root_);}
  if(profiles_changed&&profile_form_)CancelSceneActions(profiles_root_);
#endif
  if((selected||history_profile_changed)&&tools_root_)CancelChoiceInteractions(tools_root_);
  if (history_profile_changed && history_visible()) RefreshTools();
  if (changed || profiles_changed) RebuildWorkspaceDots();
  for (const auto& [id, button] : workspace_buttons_) {
    SetActionOn(button,id == state_.workspace_id);
    button->SetEnabled(state_.configuration_ready);
  }
  if (profiles_changed) {
    ReplaceProfilePickerOptions(profile_picker_,state_.profiles,false,false);
    ReplaceProfilePickerOptions(managed_profile_picker_,state_.profiles,true,added_profile);
#if BUILDFLAG(IS_MAC)
    if(!profile_form_)
#endif
    {
    if (!state_.profiles.empty()) SetPickerIndex(managed_profile_picker_,state_.profiles.size() - 1);
    if (!added_profile) {
      for (size_t i = 0; i < state_.profiles.size(); ++i)
        if (state_.profiles[i].id == managed_id) SetPickerIndex(managed_profile_picker_,i);
    }
    }
    confirm_profile_deletion_ = false;
#if BUILDFLAG(IS_MAC)
    if(profile_form_){const auto profiles=ProfileOptions(state_);profile_form_->form->reconcile(rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),added_profile);const std::string id(profile_form_->form->selection());for(size_t i=0;i<state_.profiles.size();++i)if(state_.profiles[i].id==id)SetPickerIndex(managed_profile_picker_,i);}
#endif
  }
#if BUILDFLAG(IS_MAC)
  if(import_form_){RefreshRustImportControls();if(ready_changed&&import_visible()&&import_form_->form->should_resume(state_.configuration_ready,state_.profiles_busy))ImportSelectionChanged();}
  else
#endif
  apply_import_->SetEnabled(import_can_apply_ && !import_busy_ && state_.configuration_ready && !state_.profiles_busy);
  profile_picker_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  if (changed || profiles_changed || ready_changed) RefreshProfileControls();
  add_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  edit_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy);
  remove_workspace_->SetEnabled(state_.configuration_ready && !state_.profiles_busy && state_.workspaces.size() > 1);
  if ((changed || selected || profiles_changed) && settings_visible()) RefreshWorkspaceSettings();
  if (general_widget_->IsVisible()) RefreshGeneralControls();
  RefreshWorkspaceHeading();
  SetPanelText(settings_title_,creating_workspace_ ? u"Nowa przestrzeń" : state_.workspace_name);
  SetPanelText(profile_name_,u"Chromium: " + state_.profile_name);
  RefreshRustWorkspacePresentation();
  const bool blank = !state_.url.is_valid() || state_.url.path() == "/start.html";
  const std::string path = state_.url.path() == "/" ? std::string() : std::string(state_.url.path());
  SetRowAddress(url_pill_.get(),
      blank ? u"Szukaj lub wpisz adres" : HostLabel(state_.url),
      blank ? u"" : base::UTF8ToUTF16(path), blank, !blank && state_.url.SchemeIs("https"));
  for (bool right : {false, true}) {
    std::u16string title;
    std::array<uint64_t,2> identity{};
    for (const auto& tab : state_.tabs) if (state_.split_tabs[right] == tab.index){title=tab.title;identity=tab.identity;}
    auto* bar=static_cast<PaneHost*>(content_host_.get())->bar(right);
    bar->SetTarget(state_.workspace_id,state_.split_tabs[right],identity,state_.split_visible);
    bar->SetTitle(title,state_.right_active==right);
  }
  nav_buttons_[0]->SetEnabled(state_.can_go_back);
  nav_buttons_[1]->SetEnabled(state_.can_go_forward);
  if (selected) ApplyAppearance(false);
  RebuildTabs(previous_workspace, changed);
}

void WorkspaceShell::RebuildTabs(std::string_view previous_workspace, bool catalogue_changed) {
  if (state_.workspace_id.empty()) return;
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
  if (!cache.root) {
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(CreateSidebarColumnLayout());
    cache.root = root.get();
    root->set_context_menu_controller(this);
    cache.pins_root = root->AddChildView(std::make_unique<views::View>());
    cache.pins_root->SetLayoutManager(CreateSidebarColumnLayout(2));
    cache.tabs_root = root->AddChildView(std::make_unique<views::View>());
    cache.tabs_root->SetLayoutManager(CreateSidebarColumnLayout(2));
    // Attach a completed tree only once, instead of hundreds of live AX/layout mutations.
    cache.root->SetVisible(false);
    tab_list_->AddChildView(std::move(root));
  }
  const bool pins_changed = cache.pins != state_.pins;
  if (pins_changed) {
    ++pinned_list_build_count_;
    cache.pin_buttons.clear();
    cache.pins_root->RemoveAllChildViews();
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(CreateSidebarColumnLayout(2));
    if (!state_.pins.empty()) {
      Label(contents.get(), u"PRZYPIĘTE", kMuted, Font(-2));
      AppendPinnedItems(contents.get(), state_.pins, cache, state_.workspace_id);
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
  // Titles/URLs can change several times during a single navigation. Keep the
  // native rows, callbacks, hover/focus and accessibility nodes in that case.
  const bool same_structure = !pins_changed && cache.tabs.size() == state_.tabs.size() &&
      cache.split_tabs == state_.split_tabs &&
      std::equal(cache.tabs.begin(), cache.tabs.end(), state_.tabs.begin(),
          [](const ShellTab& a, const ShellTab& b) {
            return a.index == b.index && a.pin_id == b.pin_id && a.identity == b.identity;
          });
  if (!same_structure) {
    if (cache.pin_pair) {
      cache.pin_pair->parent()->RemoveChildViewT(cache.pin_pair.get());
      cache.pin_pair = nullptr;
    }
    cache.tab_buttons.clear();
    cache.tabs_root->RemoveAllChildViews();
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(CreateSidebarColumnLayout(2));
    auto add_tab = [&](views::View* parent, const ShellTab& tab, bool tile = false) {
      RowButton::Spec spec;
      spec.title = tab.title;
      spec.sidebar_row = true;
      spec.tile = tile;
      if (tile) spec.height = 38;
      auto host = HostLabel(tab.url);
      spec.monogram = base::i18n::ToUpper((host.empty() ? tab.title : host).substr(0, 1));
      spec.monogram_color = kMonogramColors[base::FastHash(base::UTF16ToUTF8(host)) % kMonogramColors.size()];
      spec.drop=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string workspace,size_t index,std::array<uint64_t,2> identity,gfx::Point point){
        if(shell)shell->HandleSidebarDrop(std::move(workspace),index,std::move(identity),{},point);
      },weak_factory_.GetWeakPtr(),state_.workspace_id,tab.index,tab.identity);
      if (state_.tabs.size() > 1) spec.close = base::BindRepeating(callbacks_.close_tab, tab.index);
      cache.tab_buttons[tab.index] = parent->AddChildView(CreateSceneRow(
          base::BindRepeating(callbacks_.select_tab,tab.index),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr())));
      cache.tab_buttons[tab.index]->set_context_menu_controller(this);
      return cache.tab_buttons[tab.index].get();
    };
    std::vector<SidebarTreeNode> pin_nodes;
    std::vector<SidebarTabNode> tab_nodes;
    pin_nodes.reserve(state_.pins.size()); tab_nodes.reserve(state_.tabs.size());
    for (const auto& pin : state_.pins) pin_nodes.push_back({pin.id, pin.parent_id, pin.folder});
    for (const auto& tab : state_.tabs) tab_nodes.push_back({tab.index, tab.pin_id});
    cache.tab_plan = sidebar_tree_.Tabs(state_.workspace_id, pin_nodes, tab_nodes,
        state_.split_tabs[0].value_or(SidebarTree::kRoot),
        state_.split_tabs[1].value_or(SidebarTree::kRoot), state_.split);
    for (const auto& slot : cache.tab_plan.slots) {
      if (!slot.pair) { add_tab(contents.get(), state_.tabs[slot.first]); continue; }
      SplitPair* pair = nullptr;
      if (slot.pin != SidebarTree::kRoot) {
        auto* pin_row = cache.pin_buttons.at(state_.pins[slot.pin].id)->parent();
        auto* group = pin_row->parent();
        pair = group->AddChildViewAt(std::make_unique<SplitPair>(), *group->GetIndexOf(pin_row));
        pair->SetProperty(views::kMarginsKey, pin_row->GetInsets());
        cache.pin_pair = pair;
      } else {
        pair = contents->AddChildView(std::make_unique<SplitPair>());
      }
      add_tab(pair, state_.tabs[slot.first], true);
      add_tab(pair, state_.tabs[slot.second], true);
    }
    cache.tabs_root->AddChildView(std::move(contents));
    cache.tabs = state_.tabs;
    cache.split_tabs = state_.split_tabs;
  } else if (cache.tabs != state_.tabs) {
    for (size_t i = 0; i < state_.tabs.size(); ++i) {
      const auto& tab = state_.tabs[i];
      const auto& previous = cache.tabs[i];
      auto found = cache.tab_buttons.find(tab.index);
      if (found == cache.tab_buttons.end()) continue;
      auto* row = found->second.get();
      if (tab.title != previous.title) SetRowTitle(row,tab.title);
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
      cache.pin_buttons.at(state_.pins[pin.index].id)->parent()->SetVisible(pin.visible);
    }
  }
  for (const auto& [index, button] : cache.tab_buttons)
    SetRowActive(button.get(),index==state_.active_tab);
  if (previous_workspace != state_.workspace_id) {
    if (auto old = tab_lists_.find(std::string(previous_workspace)); old != tab_lists_.end()) old->second.root->SetVisible(false);
  }
  cache.root->SetVisible(true);
  // Install controllers only for new trees. A normal switch must not walk all
  // cached workspaces and their label/layout descendants again.
  if (catalogue_changed) ConfigureSidebarContextMenus(sidebar_root_);
  else if (!same_structure) ConfigureSidebarContextMenus(cache.root);
  // Model metadata can change while native paints are locked, clipped or
  // suppressed. The Rust scene must not depend on a subsequent native paint.
  ScheduleMetalSidebar();
}

size_t WorkspaceShell::CycleSidebarTab(size_t current, size_t count, bool backwards) const {
#if BUILDFLAG(IS_MAC)
  if (metal_sidebar_) return sidebar::cycle_tab(current, count, backwards);
#endif
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
views::View* WorkspaceShell::header_action_for_testing(size_t index) const{return header_view_&&index<header_view_->children().size()?header_view_->children()[index].get():nullptr;}
views::View* WorkspaceShell::sidebar_grip_for_testing() const{return sidebar_root_?static_cast<SidebarView*>(sidebar_root_.get())->grip.get():nullptr;}
views::View* WorkspaceShell::split_divider_for_testing() const{return content_host_?static_cast<PaneHost*>(content_host_.get())->divider():nullptr;}
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
#if BUILDFLAG(IS_MAC)
  if(!content_host_ || !widget_ || (!metal_header_ && !metal_sidebar_))return;
  widget_->LayoutRootViewIfNecessary();
  auto* host=static_cast<PaneHost*>(content_host_.get());
  const std::array<views::View*,3> views{host->bar(false),host->bar(true),host->divider()};
  for(size_t index=0;index<views.size();++index) {
    auto* view=views[index];auto& surface=metal_split_[index];const bool was=surface && surface->visible();
    const auto visible=view->GetVisibleBounds();
    if(!widget_->IsVisible() || !view->IsDrawn() || visible.IsEmpty()) {
      if(surface)surface->Hide();if(was)view->SchedulePaint();continue;
    }
    if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
    auto bounds=view->GetBoundsInScreen();bounds.Offset(visible.x(),visible.y());bounds.set_size(visible.size());
    const sidebar::FloatRect clip{0,0,float(bounds.width()),float(bounds.height())};
    auto rect=gfx::RectF(view->GetBoundsInScreen());rect.Offset(-bounds.x(),-bounds.y());
    std::vector<sidebar::Item> items;
    if(index<2) {
      auto card=gfx::RectF(host->GetBoundsInScreen());card.Offset(-bounds.x(),-bounds.y());
      sidebar::Item background{};background.kind=10;background.fill=Tone(host,kCard);background.clip=clip;
      background.x=card.x();background.y=card.y();background.width=card.width();background.height=card.height();items.push_back(std::move(background));
      auto* bar=static_cast<PaneBar*>(view);
      sidebar::Item title{};title.kind=8;title.x=rect.x();title.y=rect.y();title.width=rect.width();title.height=rect.height();title.clip=clip;
      title.title=base::UTF16ToUTF8(bar->text());title.title_width=bar->text_width();title.ink=Tone(bar,bar->active()?kInk:kMuted);title.font_size=Font(-1).GetFontSize();items.push_back(std::move(title));
      for(auto* button:{bar->menu(),bar->close()}) {
        auto box=gfx::RectF(button->GetBoundsInScreen());box.Offset(-bounds.x(),-bounds.y());
        items.push_back(ActionSceneItem(button,box,gfx::RectF(gfx::Rect(bounds.size()))));
      }
    } else {
      sidebar::Item item{};item.kind=9;item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip=clip;
      item.ink=Tone(view,host->divider_handle()->hovered()?kSubtle:kDisabled);items.push_back(std::move(item));
    }
    surface->Update(widget_->GetNativeWindow(),bounds,items,index<2 ? FrameTint(workspace_color(state_.workspace_id),root_) : Tone(view,kChip));
    if(was!=surface->visible())view->SchedulePaint();
  }
#endif
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
#if BUILDFLAG(IS_MAC)
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
  metal_header_->Update(widget_->GetNativeWindow(),bounds,items,FrameTint(workspace_color(state_.workspace_id),root_));
  if (was_visible!=metal_header_->visible()) header_view_->SchedulePaint();
#endif
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
#if BUILDFLAG(IS_MAC)
  if(!address_widget_||!metal_palette_)return;
  if(address_visible())address_widget_->LayoutRootViewIfNecessary();
  for(size_t i=0;i<palette_chrome_views_.size();++i) {
    auto* view=palette_chrome_views_[i].get();auto& surface=metal_palette_chrome_[i];const bool was=surface&&surface->visible();
    if(!view||!address_visible()||!view->IsDrawn()){if(surface)surface->Hide();if(was&&view)view->SchedulePaint();continue;}
    auto local=view->GetVisibleBounds();if(i==2)local.Intersect(static_cast<PaletteText*>(view)->text_bounds());
    if(local.IsEmpty()){if(surface)surface->Hide();if(was)view->SchedulePaint();continue;}
    if(!surface)surface=CreateSidebarMetal();if(!surface)continue;
    auto bounds=view->GetBoundsInScreen();bounds.Offset(local.x(),local.y());bounds.set_size(local.size());
    auto full=gfx::RectF(view->GetBoundsInScreen());full.Offset(-bounds.x(),-bounds.y());const auto clip=gfx::RectF(gfx::Rect(bounds.size()));
    std::vector<sidebar::Item> items;
    if(i==0)items.push_back(static_cast<SceneSearchIcon*>(view)->SceneItem(full,clip));
    else items.push_back(static_cast<PaletteText*>(view)->SceneItem(full,clip));
    surface->Update(address_widget_->GetNativeWindow(),bounds,items,Tone(palette_root_,kCard));if(was!=surface->visible())view->SchedulePaint();
  }
#endif
}
void WorkspaceShell::ScheduleMetalPalette() {
  SyncSceneRowInput();
  if(!metal_palette_||metal_palette_pending_)return;
  metal_palette_pending_=true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&WorkspaceShell::UpdateMetalPalette,weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalPalette() {
  metal_palette_pending_=false;
#if BUILDFLAG(IS_MAC)
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
    const bool hovered=RowHovered(row);
    sidebar::Item item{};item.kind=12;item.x=rect.x();item.y=rect.y();item.width=rect.width();item.height=rect.height();item.clip={clip.x(),clip.y(),clip.width(),clip.height()};
    item.title=base::UTF16ToUTF8(spec.title);item.detail=base::UTF16ToUTF8(spec.detail);item.title_width=RowTitleWidth(row);
    item.ink=Tone(row,kInk);item.detail_ink=Tone(row,kMuted);item.badge=Tone(row,kChip);item.font_size=Font(0).GetFontSize();item.detail_font_size=Font(-1).GetFontSize();
    item.active=spec.highlighted;item.hovered=hovered;item.focused=row->HasFocus();item.fill=spec.highlighted?Tone(row,kAccentWash):hovered?HoverWash(row):SK_ColorTRANSPARENT;
    items.push_back(std::move(item));
    sidebar::Item icon{};icon.kind=13;icon.x=rect.x();icon.y=rect.y();icon.width=rect.width();icon.height=rect.height();icon.clip={clip.x(),clip.y(),clip.width(),clip.height()};icon.ink=Tone(row,kSubtle);
    icon.glyph=spec.icon==&vector_icons::kSettingsIcon?2:spec.icon==&vector_icons::kArrowForwardIcon?1:0;items.push_back(std::move(icon));
  }
  if(scroller.visible){auto rect=scroller.thumb;rect.Offset(-bounds.x(),-bounds.y());auto clip=scroller.bounds;clip.Offset(-bounds.x(),-bounds.y());sidebar::Item thumb{};thumb.kind=6;thumb.x=rect.x();thumb.y=rect.y();thumb.width=rect.width();thumb.height=rect.height();thumb.fill=scroller.color;thumb.clip={float(clip.x()),float(clip.y()),float(clip.width()),float(clip.height())};items.push_back(std::move(thumb));}
  metal_palette_->Update(address_widget_->GetNativeWindow(),bounds,items,Tone(palette_root_,kCard));
  if(was_visible!=metal_palette_->visible()){suggestion_list_->SchedulePaint();suggestion_scroll_->vertical_scroll_bar()->SchedulePaint();}
#endif
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
#if BUILDFLAG(IS_MAC)
  // Layer transforms do not deliver descendant visibility callbacks. Retire a
  // clipped action synchronously, including while a GPU update is already queued.
  if(tab_scroll_&&tab_scroll_->rust_owned()){
  for(auto* view:{pin_button_.get(),workspace_settings_button_.get()})if(view)if(auto* control=views::AsViewClass<RustActionView>(view))control->SyncViewportVisibility();
  for(const auto& [id,view]:workspace_buttons_)if(auto* control=views::AsViewClass<RustActionView>(view.get()))control->SyncViewportVisibility();
  }
#endif
  ScheduleMetalHeader();
  if (!metal_sidebar_ || metal_sidebar_pending_) return;
  metal_sidebar_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&WorkspaceShell::UpdateMetalSidebar, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::UpdateMetalSidebar() {
  metal_sidebar_pending_ = false;
#if BUILDFLAG(IS_MAC)
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
    item.kind=spec.address ? 4 : 3; item.title=base::UTF16ToUTF8(spec.title); item.detail=base::UTF16ToUTF8(spec.detail);
    item.title_width=RowTitleWidth(row); item.font_size=Font(0).GetFontSize();
    item.detail_font_size=Font(spec.address ? 0 : -1).GetFontSize(); item.detail_ink=Tone(row,kMuted);
    item.ink=Tone(row,row->GetEnabledInViewsSubtree() && !spec.blank ? kInk : kMuted); item.badge=Tone(row,spec.address ? kMuted : kSubtle);
    item.active=spec.secure;
    item.hovered=RowHovered(row);
    item.fill=spec.address ? SkColorSetA(Tone(row,kCard), item.hovered ? 0xD9 : 0x99) : item.hovered ? HoverWash(row) : SK_ColorTRANSPARENT; item.focused=row->HasFocus();
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
  std::vector<SidebarViewportNode> dot_nodes{{SidebarTree::kRoot,gfx::Rect(workspace_dots_->size()),true}};
  for (const auto& child : workspace_dots_->children()) dot_nodes.push_back({0,child->GetMirroredBounds(),child->GetVisible()});
  const gfx::PointF dot_requested(static_cast<float>(workspace_scroll_->offset()),0);
  const auto dot_projection=metal_sidebar_->Project(workspace_dots_->size(),dot_viewport->size(),dot_requested,dot_nodes);
  if (dot_projection.offset!=dot_requested) {
    workspace_scroll_->ScrollTo(dot_projection.offset.x()); ScheduleMetalSidebar(); return;
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
  if (!was_visible && metal_sidebar_->visible()) { new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint(); }
  // A failed drawable must expose freshly painted fallback content, even if
  // the last native paint was skipped while Metal still had a visible frame.
  if (was_visible && !metal_sidebar_->visible()) { tab_list_->SchedulePaint(); new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint(); }
#endif
}

void WorkspaceShell::ConfigureSidebarContextMenus(views::View* view) {
  ++sidebar_context_visit_count_;
  if (!view->context_menu_controller()) view->set_context_menu_controller(this);
  if (views::AsViewClass<views::Button>(view)) return;
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
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(CreateSidebarColumnLayout());
    root->SetVisible(false); root->set_context_menu_controller(this); cache.root = root.get();
    cache.pins_root = root->AddChildView(std::make_unique<views::View>());
    cache.pins_root->SetLayoutManager(CreateSidebarColumnLayout(2));
    cache.tabs_root = root->AddChildView(std::make_unique<views::View>());
    cache.tabs_root->SetLayoutManager(CreateSidebarColumnLayout(2));
    auto contents = std::make_unique<views::View>();
    contents->SetLayoutManager(CreateSidebarColumnLayout(2));
    Label(contents.get(), u"PRZYPIĘTE", kMuted, Font(-2));
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
#if BUILDFLAG(IS_MAC)
std::vector<sidebar::Item> WorkspaceShell::CaptureWorkspaceScene(std::string_view id,gfx::Size size,int offset,bool source) {
  auto* shell=this;
  const auto found=shell->tab_lists_.find(std::string(id));const TabListCache* cache=found==shell->tab_lists_.end()?nullptr:&found->second;
  const std::vector<PinnedItem>* pins=cache?&cache->pins:nullptr;
  if(!pins){auto queued=std::find_if(shell->sidebar_warm_queue_.begin(),shell->sidebar_warm_queue_.end(),[&](const auto& entry){return entry.first==id;});if(queued!=shell->sidebar_warm_queue_.end())pins=&queued->second;}
  std::vector<sidebar::Item> data;std::vector<sidebar::PreviewRow> rows;
  auto make_item=[&](const RowButton::Spec& spec){
    sidebar::Item item{};item.kind=0;item.title=base::UTF16ToUTF8(spec.title);item.monogram=base::UTF16ToUTF8(spec.icon?u"📁":spec.monogram);
    item.ink=Tone(shell->root_,kInk);item.badge=spec.icon?SK_ColorTRANSPARENT:spec.monogram_color;
    item.fill=spec.tile?SkColorSetA(Tone(shell->root_,kCard),spec.active?0xFF:0x59):spec.active?Tone(shell->root_,kCard):spec.highlighted?Tone(shell->root_,kAccentWash):SK_ColorTRANSPARENT;
    item.active=spec.active;item.tile=spec.tile;item.close=bool(spec.close);item.close_visible=spec.close&&spec.active&&!spec.tile;item.font_size=Font(0).GetFontSize();return item;
  };
  auto gap=[&](int height){rows.push_back({0,3,0,height});};
  auto row=[&](const RowButton::Spec& spec,int depth){rows.push_back({data.size(),0,depth,34});data.push_back(make_item(spec));};
  auto pair=[&](const SidebarTabSlot& slot,int depth){
    // Tab plans contain both identities; resolve before publishing a group.
    const auto first=cache->tab_buttons.find(cache->tabs[slot.first].index),second=cache->tab_buttons.find(cache->tabs[slot.second].index);
    if(first==cache->tab_buttons.end()||second==cache->tab_buttons.end())return;
    rows.push_back({data.size(),2,depth,46});sidebar::Item group{};group.kind=2;group.fill=SkColorSetA(Tone(shell->root_,kCard),0x33);data.push_back(std::move(group));
    data.push_back(make_item(RowSpec(first->second.get())));data.push_back(make_item(RowSpec(second->second.get())));
  };
  const SidebarTabSlot* pair_slot=nullptr;if(cache)for(const auto& slot:cache->tab_plan.slots)if(slot.pair){pair_slot=&slot;break;}
  std::vector<bool> visible(pins?pins->size():0,true);if(cache)for(const auto& pin:cache->tab_plan.pins)visible[pin.index]=pin.visible;
  if(pins&&!pins->empty()) {
    rows.push_back({data.size(),1,0,Font(-2).GetHeight()});sidebar::Item label{};label.kind=1;label.title="PRZYPIĘTE";label.ink=Tone(shell->root_,kMuted);label.font_size=Font(-2).GetFontSize();data.push_back(std::move(label));
    std::vector<SidebarTreeNode> nodes;for(const auto& pin:*pins)nodes.push_back({pin.id,pin.parent_id,pin.folder});
    const auto plan=shell->sidebar_tree_.Plan(id,nodes);std::vector<int> groups;
    for(const auto& entry:plan) {
      if(!entry.visible)continue;
      while(!groups.empty()&&groups.back()>=entry.depth){gap(2);groups.pop_back();}
      const auto& pin=(*pins)[entry.index];auto button=cache?cache->pin_buttons.find(pin.id):std::map<std::string,raw_ptr<views::View>>::const_iterator();
      if(cache&&button!=cache->pin_buttons.end()) {
        if(pair_slot&&pair_slot->pin==entry.index)pair(*pair_slot,entry.depth);
        else if(visible[entry.index])row(RowSpec(button->second.get()),entry.depth);
      } else {
        RowButton::Spec spec;spec.title=base::UTF8ToUTF16(pin.title);
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
    auto button=cache->tab_buttons.find(cache->tabs[slot.first].index);if(button!=cache->tab_buttons.end())row(RowSpec(button->second.get()),0);
  }
  if(!cache||(cache->tabs.empty()&&!source)){RowButton::Spec blank;blank.title=u"Nowa karta";blank.monogram=u"N";row(blank,0);}
  const auto plan=sidebar::workspace_preview_layout(size.width(),size.height(),offset,rust::Slice<const sidebar::PreviewRow>(rows.data(),rows.size()));
  std::vector<sidebar::Item> items;items.reserve(plan.size());
  for(const auto& placement:plan){auto item=std::move(data[placement.index]);const auto& r=placement.bounds;const auto& c=placement.clip;item.x=r.x;item.y=r.y;item.width=r.width;item.height=r.height;item.clip={float(c.x),float(c.y),float(c.width),float(c.height)};items.push_back(std::move(item));}
  return items;
}
#endif
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
        auto row = [&](const RowButton::Spec& spec, int indent, int row_height) {
          if (y + row_height > 0 && y < size.height())
            RowButton::PaintPreview(canvas, gfx::Rect(indent, y, std::max(0, size.width() - indent), row_height), spec, shell->root_);
          y += row_height + 2;
        };
        const SidebarTabSlot* pair_slot = nullptr;
        if (cache) for (const auto& slot : cache->tab_plan.slots) if (slot.pair) {pair_slot = &slot; break;}
        std::vector<bool> pin_visibility(pins ? pins->size() : 0, true);
        if (cache) for (const auto& pin : cache->tab_plan.pins) pin_visibility[pin.index] = pin.visible;
        auto pair = [&](const SidebarTabSlot& slot, int indent) {
          const int half = std::max(0, size.width() - indent - 12) / 2;
          if (y + 46 > 0 && y < size.height())
            FillRound(canvas, gfx::Rect(indent, y, size.width() - indent, 46), 12, SkColorSetA(Tone(shell->root_, kCard), 0x33));
          int x = indent + 4;
          for (size_t position : {slot.first, slot.second}) {
            const auto& tab = cache->tabs[position];
            auto button = cache->tab_buttons.find(tab.index);
            if (button == cache->tab_buttons.end()) continue;
            if (y + 38 > 0 && y < size.height())
              RowButton::PaintPreview(canvas, gfx::Rect(x, y + 4, half, 38), RowSpec(button->second.get()), shell->root_);
            x += half + 4;
          }
          y += 48;
        };
        if (pins && !pins->empty()) {
          const int label_height = Font(-2).GetHeight();
          canvas->DrawStringRectWithFlags(u"PRZYPIĘTE", Font(-2), Tone(shell->root_, kMuted),
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
                row(RowSpec(button->second.get()), indent, 34);
            } else {
              RowButton::Spec spec; spec.title = base::UTF8ToUTF16(pin->title);
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
            row(RowSpec(button->second.get()), 0, 34);
        }
        if (!cache || (cache->tabs.empty() && id != shell->swipe_source_)) {
          RowButton::Spec blank; blank.title = u"Nowa karta"; blank.monogram = u"N";
          row(blank, 0, 34);
        }
      }
      list->Finalize();
      return list->paint_op_buffer().DeepCopyAsRecord();
    }, weak_factory_.GetWeakPtr());
    auto preview = std::make_unique<WorkspaceSwipePreview>(std::move(capture));
#if BUILDFLAG(IS_MAC)
    if(metal_sidebar_) {
      const auto tint=FrameTint(workspace_color(swipe_source_),root_);
      preview->SetSceneCapture(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,int index)->std::vector<sidebar::Item>{
        if(!shell||index<0||index>=static_cast<int>(shell->swipe_workspace_ids_.size())||!shell->swipe_overlay_)return {};
        const auto& id=shell->swipe_workspace_ids_[index];const auto size=shell->swipe_overlay_->size();
        const bool source=id==shell->swipe_source_;
        return shell->CaptureWorkspaceScene(id,size,source?shell->tab_scroll_->visible_rect().y():0,source);
      },weak_factory_.GetWeakPtr()),pinned_?tint:Mix(tint,Tone(root_,kCard),70));
    }
#endif
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
  if (!sidebar_visible() || !state_.configuration_ready || state_.profiles_busy || state_.workspaces.size() < 2 || folder_dialog_ || remove_workspace_dialog_ ||
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

void WorkspaceShell::AppendPinnedItems(views::View* root, const std::vector<PinnedItem>& pins,
    TabListCache& cache, const std::string& workspace) {
  std::vector<SidebarTreeNode> nodes;
  nodes.reserve(pins.size());
  for (const auto& pin : pins) nodes.push_back({pin.id, pin.parent_id, pin.folder});
  const auto plan = sidebar_tree_.Plan(workspace, nodes);
  std::map<size_t, views::View*> groups;
  for (const auto& entry : plan) {
    const auto* pin = &pins[entry.index];
    auto* parent = entry.parent == SidebarTree::kRoot ? root : groups.at(entry.parent);
    RowButton::Spec spec;
    spec.title = base::UTF8ToUTF16(pin->title);
    spec.sidebar_row = true;
    if (pin->folder) {
      spec.icon = &vector_icons::kFolderFilledIcon;
      spec.title = (entry.collapsed ? u"▸ " : u"▾ ") + spec.title;
    } else {
      spec.drop=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string workspace,std::string id,gfx::Point point){
        if(shell)shell->HandleSidebarDrop(std::move(workspace),std::nullopt,{},std::move(id),point);
      },weak_factory_.GetWeakPtr(),workspace,pin->id);
      spec.monogram = base::i18n::ToUpper(spec.title.substr(0, 1));
      spec.monogram_color = kMonogramColors[base::FastHash(pin->url.host()) % kMonogramColors.size()];
    }
    auto* row = parent->AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(CreateSidebarPinnedRowLayout());
    row->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, SidebarFolderIndent(entry.depth), 0, 0)));
    auto* button = row->AddChildView(CreateSceneRow(views::Button::PressedCallback(),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr())));
    if (pin->folder) {
      auto* group = parent->AddChildView(std::make_unique<views::View>());
      group->SetLayoutManager(CreateSidebarColumnLayout(2));
      groups[entry.index] = group;
      group->SetVisible(!entry.collapsed);
      SetRowCallback(button,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, views::View* group,
                                               views::View* button, std::string workspace, std::string id, std::u16string title) {
        if (!shell) return;
        const bool collapsed = shell->sidebar_tree_.Toggle(workspace, id);
        group->SetVisible(!collapsed);
        SetRowTitle(button,(collapsed?u"▸ ":u"▾ ")+title);
        shell->ScheduleMetalSidebar();
      }, weak_factory_.GetWeakPtr(), base::Unretained(group), base::Unretained(button), workspace, pin->id, base::UTF8ToUTF16(pin->title)));
    } else {
      SetRowCallback(button,base::BindRepeating(callbacks_.open_pin, pin->id));
    }
    cache.pin_buttons[pin->id] = button;
    button->set_context_menu_controller(this);
  }
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

void WorkspaceShell::RebuildWorkspaceDots() {
  workspace_buttons_.clear();
  workspace_dots_->RemoveAllChildViews();
  for (const auto& workspace : state_.workspaces) {
    auto* button = workspace_dots_->AddChildView(CreateSceneAction(
        base::BindRepeating(callbacks_.select_workspace, workspace.id),
        u"Przestrzeń: " + base::UTF8ToUTF16(workspace.name) + (state_.sleeping_workspaces.contains(workspace.id) ? u" · uśpiona" : u""), GlyphButton::Glyph::kDot,nullptr,
        base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {return shell && shell->metal_sidebar_visible();},weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::ScheduleMetalSidebar,weak_factory_.GetWeakPtr())));
    SetActionLabel(button,workspace_emoji(workspace.id));
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
#if BUILDFLAG(IS_MAC)
// Rust plans use the established application command IDs; fail compilation if
// the native IDs change without updating the Rust UI contract.
static_assert(kOpenSplit==5&&kCloseSplit==6&&kMoveLeft==7&&kMoveRight==8&&kToggleVertical==9&&kCloseTab==10&&kLinkPeek==20&&kRuleHere==23&&kRemoveRule==24&&kNewFolder==36);
sidebar::TabMenuState RowMenuState(const ShellState& state,std::optional<size_t> tab,bool valid,bool http) {
  sidebar::TabMenuState out{};out.ready=state.configuration_ready;out.busy=state.profiles_busy;out.valid=valid;out.tab=tab.value_or(0);out.has_tab=tab.has_value();
  out.split=state.split;out.first=state.split_tabs[0].value_or(0);out.has_first=state.split_tabs[0].has_value();out.second=state.split_tabs[1].value_or(0);out.has_second=state.split_tabs[1].has_value();
  out.vertical=state.split_vertical;out.tab_count=state.tabs.size();out.http=http;return out;
}
#endif
}

void WorkspaceShell::SetPeekTitle(std::u16string title) { peek_title_->SetText(u"Peek · " + title); }
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
  for (size_t i = 0; i < route_workspaces_.size(); ++i) {
    std::u16string profile;
    for (const auto& candidate : state_.profiles)
      if (candidate.id == route_workspaces_[i].profile_id) profile = base::UTF8ToUTF16(candidate.name);
    route_menu_->AddRadioItem(kRouteBase + i,
        base::UTF8ToUTF16(route_workspaces_[i].name) + u" · " + profile, 2);
  }
  link_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  link_menu_->AddItem(kLinkPeek, u"Otwórz w Peek");
  link_menu_->AddItem(kLinkNewTab, u"Otwórz w nowej karcie");
  link_menu_->AddItem(kLinkSplit, u"Otwórz obok w Split View");
  link_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  link_menu_->AddItem(kRuleHere, u"Zawsze otwieraj tę domenę tutaj");
  link_menu_->AddSubMenu(kRouteSubmenu, u"Otwieraj " + base::UTF8ToUTF16(context_url_.host()) + u" w…", route_menu_.get());
  link_menu_->AddItem(kRemoveRule, u"Usuń regułę dla tej domeny");
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
  for (size_t i = 0; i < menu_profiles_.size(); ++i)
    profile_menu_->AddRadioItem(kProfileCommandBase + i, base::UTF8ToUTF16(menu_profiles_[i].name), 1);
  profile_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  profile_menu_->AddItem(kEditProfile, u"Edytuj profil…");
  profile_menu_->AddItem(kNewProfile, u"Dodaj nowy profil…");
  workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  workspace_menu_->AddSubMenu(kSetProfile, u"Profil przeglądania", profile_menu_.get());
  color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::array<const char16_t*, 4> color_names = {u"Lawendowy", u"Szałwiowy", u"Brzoskwiniowy", u"Niebieski"};
  for (size_t i = 0; i < kWorkspaceColors.size(); ++i)
    color_menu_->AddCheckItem(kColorBase + i, color_names[i]);
  color_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  color_menu_->AddItem(kCustomColor, u"Własny kolor…");
  emoji_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < kWorkspaceEmojis.size(); ++i)
    emoji_menu_->AddCheckItem(kEmojiBase + i, kWorkspaceEmojis[i]);
  emoji_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  emoji_menu_->AddItem(kCustomEmoji, u"Własna ikonka…");
  emoji_menu_->AddCheckItem(kClearEmoji, u"Bez ikonki (kropka)");
  workspace_menu_->AddSubMenu(kColorMenu, u"Kolor przestrzeni", color_menu_.get());
  workspace_menu_->AddSubMenu(kEmojiMenu, u"Ikonka przestrzeni", emoji_menu_.get());
  workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  workspace_menu_->AddItem(kWorkspaceSettings, u"Nazwa i ustawienia…");
  workspace_menu_->AddItem(kRemoveWorkspace, u"Usuń przestrzeń…");
}

void WorkspaceShell::BuildFolderMenu(std::string parent) {
  context_tab_.reset();context_tab_identity_={};context_pin_.clear();
  workspace_menu_runner_.reset();
  workspace_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  folder_parent_id_ = std::move(parent);
  context_workspace_id_ = state_.workspace_id;
  workspace_menu_->AddItem(kNewFolder, folder_parent_id_.empty() ? u"Nowy folder…" : u"Nowy podfolder…");
  if (!folder_parent_id_.empty()) {
    workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    workspace_menu_->AddItem(kRenameFolder, u"Zmień nazwę…");
    workspace_menu_->AddItem(kDeleteFolder, u"Usuń folder…");
  }
}

views::View* WorkspaceShell::folder_form_for_testing() const {
#if BUILDFLAG(IS_MAC)
  return folder_dialog_?FindFolderForm(folder_dialog_->GetContentsView()):nullptr;
#else
  return nullptr;
#endif
}
EditorControl* WorkspaceShell::folder_editor_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form?form->field():nullptr;
#else
  return nullptr;
#endif
}
bool WorkspaceShell::folder_form_metal_visible() const {
#if BUILDFLAG(IS_MAC)
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form&&form->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::folder_form_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form&&form->contains_scene_text(text);
#else
  return false;
#endif
}
std::u16string WorkspaceShell::folder_form_error_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* form=static_cast<FolderFormView*>(folder_form_for_testing());return form?form->error_text():std::u16string();
#else
  return {};
#endif
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
  std::u16string initial_name = u"Nowy folder";
  if (!renamed.empty()) {
    auto item = std::find_if(state_.pins.begin(), state_.pins.end(), [&](const auto& item) { return item.id == renamed && item.folder; });
    if (state_.workspace_id != workspace || item == state_.pins.end()) return;
    initial_name = base::UTF8ToUTF16(item->title);
  }
#if BUILDFLAG(IS_MAC)
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
    prompt->SetColorModeOverride(widget_->color_mode_override());InstallWorkspaceKeyboardRouting(prompt,true);PreparePanelText(prompt->GetRootView());folder_dialog_=prompt->GetWeakPtr();prompt->Show();input->SelectRange(gfx::Range(0,input->GetText().size()));return;
  }
#endif
  ui::DialogModel::Builder builder;
  auto* model = builder.model();
  auto weak = weak_factory_.GetWeakPtr();
  auto dialog_model = builder.SetTitle(!renamed.empty() ? u"Zmień nazwę folderu" : parent.empty() ? u"Nowy folder" : u"Nowy podfolder")
      .AddTextfield(kFolderNameField, u"Nazwa folderu", initial_name)
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
      }, weak, base::Unretained(model), workspace, parent, renamed), ui::DialogModel::Button::Params().SetLabel(renamed.empty() ? u"Dodaj folder" : u"Zapisz nazwę"))
      .AddCancelButton(base::DoNothing(), ui::DialogModel::Button::Params().SetLabel(u"Anuluj"))
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
#if BUILDFLAG(IS_MAC)
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
  prompt->SetColorModeOverride(widget_->color_mode_override());InstallWorkspaceKeyboardRouting(prompt,true);PreparePanelText(prompt->GetRootView());folder_dialog_=prompt->GetWeakPtr();prompt->Show();return true;
#else
  return false;
#endif
}
views::View* WorkspaceShell::folder_notice_for_testing() const {
#if BUILDFLAG(IS_MAC)
  return folder_dialog_?FindFolderNotice(folder_dialog_->GetContentsView()):nullptr;
#else
  return nullptr;
#endif
}
bool WorkspaceShell::folder_notice_metal_visible() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice&&notice->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::folder_notice_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice&&notice->contains_scene_text(text);
#else
  return false;
#endif
}
std::u16string WorkspaceShell::folder_notice_error_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice?notice->error_text():std::u16string();
#else
  return {};
#endif
}
size_t WorkspaceShell::folder_notice_line_count_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<FolderNoticeView*>(folder_notice_for_testing());return notice?notice->body_line_count():0;
#else
  return 0;
#endif
}
void WorkspaceShell::RequestDeleteFolder(std::string workspace, std::string folder) {
  if (folder_dialog_) { folder_dialog_->Activate(); return; }
  auto item = std::find_if(state_.pins.begin(), state_.pins.end(), [&](const auto& item) { return item.id == folder && item.folder; });
  if (state_.workspace_id != workspace || item == state_.pins.end() || state_.profiles_busy) return;
  if(ShowRustFolderNotice(workspace,folder,item->title,false))return;
  auto model = ui::DialogModel::Builder().SetTitle(u"Usunąć folder „" + base::UTF8ToUTF16(item->title) + u"”?")
      .AddParagraph(ui::DialogModelLabel(u"Folder i cała jego przypięta zawartość zostaną usunięte. Tej operacji nie można cofnąć. Strony już otwarte w kartach pozostaną otwarte."))
      .AddOkButton(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, std::string workspace, std::string folder) {
        if (!shell) return true;
        const bool ok = shell->callbacks_.delete_folder.Run(workspace, folder, true);
        if (ok) shell->folder_dialog_.reset();
        return ok;
      }, weak_factory_.GetWeakPtr(), workspace, folder), ui::DialogModel::Button::Params().SetLabel(u"Usuń folder"))
      .AddCancelButton(base::DoNothing(), ui::DialogModel::Button::Params().SetLabel(u"Anuluj"))
      .OverrideDefaultButton(ui::mojom::DialogButton::kCancel).Build();
  auto* prompt = views::DialogDelegate::CreateDialogWidget(
      views::BubbleDialogModelHost::CreateModal(std::move(model), ui::mojom::ModalType::kWindow), widget_->GetNativeWindow(), widget_->GetNativeView());
  prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(prompt,true);
#endif
  PreparePanelText(prompt->GetRootView());
  folder_dialog_ = prompt->GetWeakPtr(); prompt->Show();
}

void WorkspaceShell::ShowFolderSaveError() {
  if(!widget_)return;
  if(ShowRustFolderNotice({},{},{},true))return;
  auto model = ui::DialogModel::Builder().SetTitle(u"Nie udało się zapisać folderu")
      .AddParagraph(ui::DialogModelLabel(u"Sprawdź wolne miejsce na dysku i spróbuj ponownie."))
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

void WorkspaceShell::ShowSplitPaneMenu(bool right, gfx::Point point) {
  if (!state_.split_visible || !state_.split_tabs[right]) return;
  context_tab_ = state_.split_tabs[right]; context_pin_.clear();
  ShowTabMenu(point, ui::mojom::MenuSourceType::kMouse);
}

void WorkspaceShell::CloseSplitPane(bool right) {
  if (!state_.split_visible || !state_.split_tabs[right]) return;
  std::array<uint64_t,2> identity{};
  for(const auto& tab:state_.tabs)if(tab.index==*state_.split_tabs[right]){identity=tab.identity;break;}
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell,std::string workspace,std::array<uint64_t,2> identity){
        if(!shell||shell->state_.workspace_id!=workspace||identity==std::array<uint64_t,2>{})return;
        for(const auto& tab:shell->state_.tabs)if(tab.identity==identity){shell->callbacks_.close_tab.Run(tab.index);return;}
      },weak_factory_.GetWeakPtr(),state_.workspace_id,identity));
}

views::View* WorkspaceShell::split_pane_button(bool right, bool close) const {
  auto* bar = static_cast<PaneHost*>(content_host_.get())->bar(right);
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
#if BUILDFLAG(IS_MAC)
    if(metal_sidebar_) {
      const auto target=ResolveSidebarTarget(context_workspace_id_,context_tab_identity_,context_pin_);
      const auto plan=sidebar::tab_context_menu(RowMenuState(state_,target.tab,target.valid,url.SchemeIsHTTPOrHTTPS()));
      for(const auto& entry:plan){if(entry.command==0)workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);else workspace_menu_->AddItem(entry.command,base::UTF8ToUTF16(std::string(entry.label)));}
      return;
    }
#endif
    workspace_menu_->AddItem(kNewFolder, u"Nowy folder…");
    folder_parent_id_.clear();
    context_workspace_id_ = state_.workspace_id;
    workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    workspace_menu_->AddItem(kLinkPeek, u"Otwórz w Peek");
    const bool paired = context_tab_ && state_.split &&
        (state_.split_tabs[0] == context_tab_ || state_.split_tabs[1] == context_tab_);
    if (paired) {
      const bool vertical = state_.split_vertical;
      workspace_menu_->AddItem(kMoveLeft, vertical ? u"Przesuń w górę" : u"Przesuń w lewo");
      workspace_menu_->AddItem(kMoveRight, vertical ? u"Przesuń w dół" : u"Przesuń w prawo");
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kToggleVertical, vertical ? u"Podział poziomy (obok siebie)"
                                                         : u"Podział pionowy (jedna nad drugą)");
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kCloseSplit, u"Wyjmij kartę ze splitu");
      workspace_menu_->AddItem(kCloseTab, u"Zamknij kartę");
    } else {
      workspace_menu_->AddItem(kOpenSplit, u"Otwórz obok w Split View");
    }
    if (url.SchemeIsHTTPOrHTTPS()) {
      workspace_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      workspace_menu_->AddItem(kRuleHere, u"Zawsze otwieraj tę domenę tutaj");
      workspace_menu_->AddItem(kRemoveRule, u"Usuń regułę dla tej domeny");
    }
}
void WorkspaceShell::ShowTabMenu(gfx::Point point,ui::mojom::MenuSourceType source_type) {
  BuildTabMenu(context_tab_,context_pin_);
  workspace_menu_runner_=std::make_unique<views::MenuRunner>(workspace_menu_.get(),views::MenuRunner::CONTEXT_MENU);
  workspace_menu_runner_->RunMenuAt(widget_,nullptr,gfx::Rect(point,gfx::Size()),views::MenuAnchorPosition::kTopLeft,source_type);
}

bool WorkspaceShell::IsCommandIdChecked(int command_id) const {
  if (command_id >= kColorBase && command_id < kColorBase + static_cast<int>(kWorkspaceColors.size()))
    return workspace_color(context_workspace_id_) == kWorkspaceColors[command_id - kColorBase];
  if (command_id >= kEmojiBase && command_id < kEmojiBase + static_cast<int>(kWorkspaceEmojis.size()))
    return workspace_emoji(context_workspace_id_) == kWorkspaceEmojis[command_id - kEmojiBase];
  if (command_id == kClearEmoji) return workspace_emoji(context_workspace_id_).empty();
  if (command_id >= kRouteBase && command_id < kRouteBase + static_cast<int>(route_workspaces_.size()))
    for (const auto& rule : state_.link_rules) if (rule.host == context_url_.host())
      return rule.workspace_id == route_workspaces_[command_id - kRouteBase].id;
  if (command_id < kProfileCommandBase || command_id >= kProfileCommandBase + static_cast<int>(menu_profiles_.size())) return false;
  for (const auto& workspace : state_.workspaces)
    if (workspace.id == context_workspace_id_) return workspace.profile_id == menu_profiles_[command_id - kProfileCommandBase].id;
  return false;
}
bool WorkspaceShell::IsCommandIdEnabled(int command_id) const {
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_&&command_id>=kOpenSplit&&command_id<=kCloseTab)return RustTabCommandEnabled(command_id);
#endif
  if (command_id == kNewFolder || command_id == kRenameFolder || command_id == kDeleteFolder) return state_.configuration_ready && !state_.profiles_busy;
  if (command_id == kRemoveWorkspace) return state_.configuration_ready && !state_.profiles_busy && state_.workspaces.size() > 1;
  // Left/top half can only move right/down, and vice versa.
  if (command_id == kMoveLeft) return state_.configuration_ready && context_tab_ && state_.split_tabs[1] == context_tab_;
  if (command_id == kMoveRight) return state_.configuration_ready && context_tab_ && state_.split_tabs[0] == context_tab_;
  if (command_id == kCloseTab) return state_.tabs.size() > 1;
  if (command_id == kRemoveRule) return state_.configuration_ready &&
      std::any_of(state_.link_rules.begin(), state_.link_rules.end(), [&](const auto& rule) { return rule.host == context_url_.host(); });
  return state_.configuration_ready;
}
WorkspaceShell::ResolvedRowTarget WorkspaceShell::ResolveSidebarTarget(std::string_view workspace,std::array<uint64_t,2> identity,std::string_view pin) const {
#if BUILDFLAG(IS_MAC)
  std::vector<sidebar::MenuTab> tabs;tabs.reserve(state_.tabs.size());for(const auto& tab:state_.tabs)tabs.push_back({tab.index,tab.identity[0],tab.identity[1]});
  std::vector<sidebar::TreeNode> pins;pins.reserve(state_.pins.size());for(const auto& row:state_.pins)pins.push_back({row.id,row.parent_id,row.folder});
  const auto target=sidebar::resolve_row_target(rust::Str(workspace.data(),workspace.size()),state_.workspace_id,identity[0],identity[1],rust::Str(pin.data(),pin.size()),rust::Slice<const sidebar::MenuTab>(tabs.data(),tabs.size()),rust::Slice<const sidebar::TreeNode>(pins.data(),pins.size()));
  return {target.valid,target.has_tab?std::optional<size_t>(target.tab):std::nullopt,std::string(target.pin)};
#else
  return {false,std::nullopt,{}};
#endif
}
void WorkspaceShell::HandleSidebarDrop(std::string workspace,std::optional<size_t> tab,std::array<uint64_t,2> identity,std::string pin,gfx::Point point) {
  if(workspace!=state_.workspace_id)return;
  const auto bounds=content_host_->GetBoundsInScreen();
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_) {
    const auto target=ResolveSidebarTarget(workspace,identity,pin);
    const bool allowed=target.valid&&state_.configuration_ready&&!state_.profiles_busy;
    if(sidebar::sidebar_drop_accepts({bounds.x(),bounds.y(),bounds.width(),bounds.height()},point.x(),point.y(),allowed))QueueSplit(target.tab,target.pin);
    return;
  }
#endif
  if(bounds.Contains(point)&&point.x()>=bounds.CenterPoint().x())QueueSplit(tab,std::move(pin));
}
bool WorkspaceShell::RustTabCommandEnabled(int command) const {
#if BUILDFLAG(IS_MAC)
  const auto target=ResolveSidebarTarget(context_workspace_id_,context_tab_identity_,context_pin_);
  return sidebar::tab_menu_enabled(command,RowMenuState(state_,target.tab,target.valid,context_url_.SchemeIsHTTPOrHTTPS()));
#else
  return false;
#endif
}
void WorkspaceShell::QueueTabCommand(int command) {
#if BUILDFLAG(IS_MAC)
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,int command,std::string workspace,std::array<uint64_t,2> identity,std::string pin){
    if(!shell)return;const auto target=shell->ResolveSidebarTarget(workspace,identity,pin);
    const auto action=sidebar::tab_menu_action(command,RowMenuState(shell->state_,target.tab,target.valid,shell->context_url_.SchemeIsHTTPOrHTTPS()));
    switch(action) {
      case sidebar::SidebarAction::OpenSplit:if(target.tab)shell->callbacks_.split_tab.Run(*target.tab);else shell->callbacks_.split_pin.Run(target.pin);break;
      case sidebar::SidebarAction::CloseSplit:shell->callbacks_.unsplit.Run(*target.tab);break;
      case sidebar::SidebarAction::MoveFirst:case sidebar::SidebarAction::MoveSecond:shell->callbacks_.swap_split.Run();break;
      case sidebar::SidebarAction::ToggleOrientation:shell->callbacks_.split_vertical.Run(!shell->state_.split_vertical);break;
      case sidebar::SidebarAction::CloseTab:shell->callbacks_.close_tab.Run(*target.tab);break;
      default:break;
    }
  },weak_factory_.GetWeakPtr(),command,context_workspace_id_,context_tab_identity_,context_pin_));
#endif
}
void WorkspaceShell::QueueSplit(std::optional<size_t> tab, std::string pin) {
  std::array<uint64_t,2> identity{};if(tab)for(const auto& row:state_.tabs)if(row.index==*tab){identity=row.identity;break;}
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, std::string workspace, std::optional<size_t> tab, std::string pin,std::array<uint64_t,2> identity) {
        if (!shell || shell->state_.workspace_id != workspace) return;
#if BUILDFLAG(IS_MAC)
        if(shell->metal_sidebar_) {
          const auto target=shell->ResolveSidebarTarget(workspace,identity,pin);
          if(!target.valid||!shell->state_.configuration_ready||shell->state_.profiles_busy)return;
          if(target.tab)shell->callbacks_.split_tab.Run(*target.tab);else shell->callbacks_.split_pin.Run(target.pin);return;
        }
#endif
        if (tab) shell->callbacks_.split_tab.Run(*tab);
        else if (!pin.empty()) shell->callbacks_.split_pin.Run(std::move(pin));
      }, weak_factory_.GetWeakPtr(), state_.workspace_id, tab, std::move(pin),std::move(identity)));
}

void WorkspaceShell::ExecuteCommand(int command_id, int) {
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_&&command_id>=kOpenSplit&&command_id<=kCloseTab){QueueTabCommand(command_id);return;}
#endif
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
          else shell->callbacks_.swap_split.Run();
        }, weak_factory_.GetWeakPtr(), state_.workspace_id, command_id, *context_tab_));
    return;
  }
  if (command_id == kCloseSplit) {
    if (!context_tab_) return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<WorkspaceShell> shell, std::string workspace, size_t tab) {
          if (shell && shell->state_.workspace_id == workspace) shell->callbacks_.unsplit.Run(tab);
        }, weak_factory_.GetWeakPtr(), state_.workspace_id, *context_tab_));
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
        else if (command >= kColorBase && command < kColorBase + static_cast<int>(kWorkspaceColors.size()))
          shell->SetWorkspaceColor(target, kWorkspaceColors[command - kColorBase]);
        else if (command >= kEmojiBase && command < kEmojiBase + static_cast<int>(kWorkspaceEmojis.size()))
          shell->SetWorkspaceEmoji(target, kWorkspaceEmojis[command - kEmojiBase]);
        else if (command == kClearEmoji) shell->SetWorkspaceEmoji(target, u"");
        else if (command == kWorkspaceSettings || command == kCustomColor || command == kCustomEmoji) {
          shell->callbacks_.select_workspace.Run(target);
          shell->OpenWorkspaceSettings();
          if (command == kCustomColor) shell->color_input_->RequestFocus();
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
  if (!text.empty() && !commands_only) {
    std::u16string provider_name;
    const GURL url = ResolveInput(text, &provider_name);
    const bool search = !provider_name.empty();
    const std::u16string search_title = u"Szukaj w " + provider_name;
    suggestions_.push_back({search ? search_title : u"Otwórz",
                            search ? base::UTF8ToUTF16(text) : base::UTF8ToUTF16(url.is_valid() ? url.spec() : text),
                            url, std::nullopt});
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
  size_t preserved=0;bool found_selected=false;
  if(selected){auto found=std::find(suggestions_.begin(),suggestions_.end(),*selected);if(found!=suggestions_.end()){preserved=found-suggestions_.begin();found_selected=true;}}
  palette->Reconcile(suggestions_.size(),preserved,found_selected);
  suggestion_list_->RemoveAllChildViews();
  for (size_t i = 0; i < suggestions_.size(); ++i) {
    const auto& item = suggestions_[i];
    RowButton::Spec spec;
    spec.title = item.title;
    spec.detail = item.detail;
    spec.chip = true;
    spec.height = 44;
    spec.highlighted=i==palette->selected();
    spec.sidebar_row=metal_palette_!=nullptr;
    spec.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->metal_palette_visible();},weak_factory_.GetWeakPtr());
    spec.icon = !item.command_id.empty() ? &vector_icons::kSettingsIcon
              : (item.tab || item.workspace_only || !item.pin_id.empty()) ? &vector_icons::kArrowForwardIcon
              : item.title == u"Otwórz" ? &vector_icons::kArrowForwardIcon
                                        : &vector_icons::kSearchIcon;
    suggestion_list_->AddChildView(CreateSceneRow(
        base::BindRepeating(&WorkspaceShell::RunSuggestion,weak_factory_.GetWeakPtr(),i),std::move(spec),base::BindRepeating(&WorkspaceShell::ScheduleMetalPalette,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr())));
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
#if BUILDFLAG(IS_MAC)
  const auto motion=sidebar::workspace_switch_motion(old_index,new_index,sidebar_visible(),workspace_animations_,gfx::Animation::PrefersReducedMotion());
  if(metal_sidebar_){if(!motion.enabled)return;}
  else
#endif
  if(previous.empty()||previous==state_.workspace_id||!sidebar_visible()||!workspace_animations_||gfx::Animation::PrefersReducedMotion()||old_index<0||new_index<0)return;
  // Prepare the new viewport before starting the clock. Otherwise its first
  // layout/raster can consume much of a short animation and skip its opening.
  sidebar_widget_->LayoutRootViewIfNecessary();
  if (metal_sidebar_) {
    metal_sidebar_->Hide();HideMetalSidebarGrip();
    tab_list_->SchedulePaint();  // Refresh cached native contents before animating.
    new_tab_button_->SchedulePaint(); url_pill_->SchedulePaint(); workspace_name_->SchedulePaint(); tab_scroll_->vertical_scroll_bar()->SchedulePaint(); sidebar_footer_->SchedulePaint();
  }
#if BUILDFLAG(IS_MAC)
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
#endif
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
#if BUILDFLAG(IS_MAC)
  return workspace_switch_overlay_&&static_cast<WorkspaceSwitchPreview*>(workspace_switch_overlay_.get())->visible();
#else
  return false;
#endif
}
bool WorkspaceShell::workspace_switch_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  return workspace_switch_overlay_&&static_cast<WorkspaceSwitchPreview*>(workspace_switch_overlay_.get())->contains_text(text);
#else
  return false;
#endif
}
bool WorkspaceShell::workspace_animation_running() const {
  return workspace_animation_pending_ || workspace_switch_overlay_ ||
      (tab_scroll_ && tab_scroll_->view()->layer() && tab_scroll_->view()->layer()->GetAnimator()->is_animating());
}

void WorkspaceShell::ApplyTheme() {
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
    const int radius = surface == palette_root_ ? 14 : 12;
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
  if (address_) address_->SetPresentation(Font(3).GetFontSize(),0,false,Tone(root_,kCard));
  for(auto* editor:{color_input_.get(),emoji_input_.get()})if(editor)
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
  RefreshGeneralControls();
  applying_theme_ = false;
}
void WorkspaceShell::OnNativeThemeUpdated(ui::NativeTheme*) {
  if (theme_mode_ == ThemeMode::kSystem && general_widget_) ApplyAppearance();
}
void WorkspaceShell::ScheduleToolsChrome(){
  SyncSceneRowInput();
#if BUILDFLAG(IS_MAC)
  if(tools_chrome_)tools_chrome_->Schedule();
#endif
}
bool WorkspaceShell::ToolsChromeVisible() const{
#if BUILDFLAG(IS_MAC)
  return tools_chrome_&&tools_chrome_->visible();
#else
  return false;
#endif
}
views::View* WorkspaceShell::AddToolsCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline){
#if BUILDFLAG(IS_MAC)
  if(tools_chrome_)return parent->AddChildView(std::make_unique<PreferenceCopy>(std::move(text),tone,delta,static_cast<gfx::Font::Weight>(weight),multiline,base::BindRepeating(&WorkspaceShell::ScheduleToolsChrome,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->ToolsChromeVisible();},weak_factory_.GetWeakPtr())));
#endif
  auto* label=Label(parent,std::move(text),tone,Font(delta,static_cast<gfx::Font::Weight>(weight)));label->SetMultiLine(multiline);return label;
}
bool WorkspaceShell::tools_chrome_contains_text_for_testing(std::string_view text) const{
#if BUILDFLAG(IS_MAC)
  return tools_chrome_&&tools_chrome_->contains_text(text);
#else
  return false;
#endif
}
bool WorkspaceShell::tools_chrome_excludes_for_testing(views::View* view) const{
#if BUILDFLAG(IS_MAC)
  return tools_chrome_&&tools_chrome_->excludes(view);
#else
  return false;
#endif
}
bool WorkspaceShell::tools_chrome_order_correct_for_testing() const{
#if BUILDFLAG(IS_MAC)
  return tools_chrome_&&tools_chrome_->order_correct();
#else
  return false;
#endif
}
std::string WorkspaceShell::tools_chrome_debug_for_testing() const{
#if BUILDFLAG(IS_MAC)
  return tools_chrome_?tools_chrome_->Debug():"no renderer";
#else
  return "native";
#endif
}
void WorkspaceShell::SchedulePreferenceChrome(){
#if BUILDFLAG(IS_MAC)
  if(preferences_chrome_)preferences_chrome_->Schedule();
#endif
}
bool WorkspaceShell::PreferenceChromeVisible() const{
#if BUILDFLAG(IS_MAC)
  return preferences_chrome_&&preferences_chrome_->visible();
#else
  return false;
#endif
}
views::View* WorkspaceShell::AddPreferenceCopy(views::View* parent,std::u16string text,SkColor tone,int delta,int weight,bool multiline){
#if BUILDFLAG(IS_MAC)
  if(preferences_chrome_)return parent->AddChildView(std::make_unique<PreferenceCopy>(std::move(text),tone,delta,static_cast<gfx::Font::Weight>(weight),multiline,base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak_factory_.GetWeakPtr()),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak_factory_.GetWeakPtr())));
#endif
  auto* label=Label(parent,std::move(text),tone,Font(delta,static_cast<gfx::Font::Weight>(weight)));label->SetMultiLine(multiline);return label;
}
void WorkspaceShell::SetPreferenceCopyTone(views::View* view,SkColor tone){
#if BUILDFLAG(IS_MAC)
  if(auto* copy=views::AsViewClass<PreferenceCopy>(view)){copy->SetTone(tone);return;}
#endif
  static_cast<ThemedLabel*>(view)->SetTone(tone);
}
bool WorkspaceShell::preference_chrome_contains_text_for_testing(std::string_view text) const{
#if BUILDFLAG(IS_MAC)
  return preferences_chrome_&&preferences_chrome_->contains_text(text);
#else
  return false;
#endif
}
std::string WorkspaceShell::preference_chrome_debug_for_testing() const{
#if BUILDFLAG(IS_MAC)
  return preferences_chrome_?preferences_chrome_->Debug():"no renderer";
#else
  return "native";
#endif
}
bool WorkspaceShell::preference_chrome_order_correct_for_testing() const{
#if BUILDFLAG(IS_MAC)
  return preferences_chrome_&&preferences_chrome_->order_correct();
#else
  return false;
#endif
}
bool WorkspaceShell::preference_chrome_excludes_for_testing(views::View* view) const{
#if BUILDFLAG(IS_MAC)
  return preferences_chrome_&&preferences_chrome_->excludes(view);
#else
  return false;
#endif
}
size_t WorkspaceShell::preference_category_for_testing() const{
#if BUILDFLAG(IS_MAC)
  if(preferences_form_)return preferences_form_->form->preference_current();
#endif
  return preference_category_;
}
views::View* WorkspaceShell::preference_navigation_for_testing(size_t category) const{return category<preference_sections_.size()?preference_sections_[category].navigation.get():nullptr;}
size_t WorkspaceShell::preference_navigation_frame_count_for_testing() const{
#if BUILDFLAG(IS_MAC)
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group?group->frame_count():0;
#else
  return 0;
#endif
}
bool WorkspaceShell::preference_navigation_selected_for_testing(size_t category) const{
  auto* control=preference_navigation_for_testing(category);if(!control)return false;
#if BUILDFLAG(IS_MAC)
  if(auto* button=views::AsViewClass<ScenePreferenceButton>(control))return button->selected();
#endif
  return RowSpec(control).active;
}
bool WorkspaceShell::preference_navigation_metal_visible_for_testing() const{
#if BUILDFLAG(IS_MAC)
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::preference_navigation_contains_text_for_testing(std::string_view text) const{
#if BUILDFLAG(IS_MAC)
  auto* first=preference_navigation_for_testing(0);auto* group=first?views::AsViewClass<ScenePreferenceControls>(first->parent()):nullptr;return group&&group->contains_text(text);
#else
  return false;
#endif
}
bool WorkspaceShell::preference_close_metal_visible_for_testing() const{
#if BUILDFLAG(IS_MAC)
  auto* group=preferences_close_?views::AsViewClass<ScenePreferenceControls>(preferences_close_->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::preference_action_metal_visible_for_testing(views::View* button) const {
#if BUILDFLAG(IS_MAC)
  auto* group=button?views::AsViewClass<SceneActionGroup>(button->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::preference_actions_contains_scene_text_for_testing(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  if(!general_widget_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,general_widget_->GetContentsView());
#else
  return false;
#endif
}
views::View* WorkspaceShell::preference_switch_for_testing(std::string_view key) const {
  if(key=="sidebar")return sidebar_check_;
  if(key=="animations")return animations_check_;
  const auto it=preference_toggles_.find(std::string(key));
  return it==preference_toggles_.end()?nullptr:it->second.get();
}
bool WorkspaceShell::preference_switch_checked_for_testing(std::string_view key) const {
  auto* control=preference_switch_for_testing(key);return control&&ToggleOn(control);
}
bool WorkspaceShell::preference_switch_metal_visible_for_testing(std::string_view key) const {
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneToggle>(preference_switch_for_testing(key));return scene&&scene->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::preference_switch_timer_running_for_testing(std::string_view key) const {
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneToggle>(preference_switch_for_testing(key));return scene&&scene->timer_running_for_testing();
#else
  return false;
#endif
}
views::View* WorkspaceShell::clear_checkbox_for_testing(size_t index) const {
  const std::array<views::View*,3> choices={clear_history_,clear_cookies_,clear_cache_};
  return index<choices.size()?choices[index]:nullptr;
}
bool WorkspaceShell::clear_choices_metal_visible_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* group=clear_history_?views::AsViewClass<SceneCheckboxGroup>(clear_history_->parent()):nullptr;
  return group&&group->metal_visible();
#else
  return false;
#endif
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
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker))return scene->LabelAt(index);
#endif
  return static_cast<views::Combobox*>(picker)->GetModel()->GetItemAt(index);
}
bool WorkspaceShell::preference_choice_metal_visible_for_testing(std::string_view key) const {
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneSelect>(preference_choice_for_testing(key));return scene&&scene->metal_visible();
#else
  return false;
#endif
}
gfx::Rect WorkspaceShell::preference_choice_metal_bounds_for_testing(std::string_view key) const {
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneSelect>(preference_choice_for_testing(key));if(scene)return scene->metal_bounds_for_testing();
#endif
  return {};
}
void WorkspaceShell::ActivatePreferenceChoiceForTesting(std::string_view key,size_t index){
  auto* picker=preference_choice_for_testing(key);if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SelectForTesting(index);return;}
#endif
  static_cast<views::Combobox*>(picker)->MenuSelectionAt(index);
}
void WorkspaceShell::RefreshFormViewports(){
  auto refresh=[&](auto&& self,views::View* view)->void{
#if BUILDFLAG(IS_MAC)
    if(auto* paragraph=views::AsViewClass<SceneParagraph>(view))paragraph->OnVisibleBoundsChanged();
    if(auto* row=views::AsViewClass<AppearanceRow>(view))row->SyncViewport();
#endif
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
#if BUILDFLAG(IS_MAC)
  using Kind=sidebar::PreferenceRowKind;
  using Action=sidebar::PreferenceActionKind;
  auto weak=weak_factory_.GetWeakPtr();
  auto text=[](const rust::String& value){return base::UTF8ToUTF16(std::string(value));};
  auto strings=[&](const auto& input){std::vector<std::u16string> output;for(const auto& value:input)output.push_back(text(value));return output;};
  auto copy=[&](views::View* parent,std::u16string value,SkColor tone,int delta,gfx::Font::Weight weight,bool multiline){return AddPreferenceCopy(parent,std::move(value),tone,delta,static_cast<int>(weight),multiline);};
  auto make_row=[&](const sidebar::PreferenceSpec& spec){
    CHECK_LT(spec.category,preference_sections_.size());
    auto* row=preference_sections_[spec.category].view->AddChildView(std::make_unique<PreferenceSurface>(false,true,base::BindRepeating(&WorkspaceShell::SchedulePreferenceChrome,weak),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->PreferenceChromeVisible();},weak)));
    auto* labels=row->AddChildView(std::make_unique<views::View>());SetSidebarPanelLayout(labels,true,gfx::Insets(),5);
    copy(labels,text(spec.title),kInk,0,gfx::Font::Weight::MEDIUM,true);copy(labels,text(spec.detail),kMuted,-1,gfx::Font::Weight::NORMAL,true);SetSidebarPanelLayout(row,false,gfx::Insets(16),20,labels,true);
    preference_sections_[spec.category].rows.push_back({row,base::i18n::ToLower(text(catalogue.categories[spec.category])+u" "+text(spec.title)+u" "+text(spec.detail))});return row;
  };
  for(const auto& spec:catalogue.rows){
    auto* row=make_row(spec);const auto title=text(spec.title);const std::string key(spec.key);
    if(spec.kind==Kind::Theme||spec.kind==Kind::Search||spec.kind==Kind::Picker){
      auto* control=ChoicePicker(row,title,strings(spec.choices),true);
      if(spec.kind==Kind::Theme){theme_picker_=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetThemeMode(static_cast<ThemeMode>(PickerIndex(shell->theme_picker_).value_or(0)));},weak));}
      else if(spec.kind==Kind::Search){search_engine_picker_=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell)return;const size_t index=PickerIndex(shell->search_engine_picker_).value_or(0);if(index<shell->tools_.providers.size())shell->SetDefaultSearchProvider(shell->tools_.providers[index].keyword);},weak));}
      else{preference_pickers_[key]=control;SetPickerCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell){auto index=PickerIndex(shell->preference_pickers_.at(key));if(index)shell->WritePreference(key,static_cast<int>(*index));}},weak,key));}
      continue;
    }
    if(spec.kind==Kind::Pinned||spec.kind==Kind::Animations||spec.kind==Kind::Toggle){
      auto* control=PreferenceToggle(row,title,true);
      if(spec.kind==Kind::Pinned){sidebar_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetPinned(ToggleOn(shell->sidebar_check_));},weak));}
      else if(spec.kind==Kind::Animations){animations_check_=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell)shell->SetWorkspaceAnimations(ToggleOn(shell->animations_check_));},weak));}
      else{preference_toggles_[key]=control;SetActionCallback(control,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell)shell->WritePreference(key,ToggleOn(shell->preference_toggles_.at(key)));},weak,key));}
      continue;
    }
    if(spec.kind==Kind::Text){
      auto* field=AddEditor(row->children().front(),true);field->GetViewAccessibility().SetName(title);field->SetPlaceholderText(text(spec.placeholder));
      field->SetCallbacks(base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,std::string key){if(shell)shell->PreferenceTextEdited(key);},weak,key),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){return shell&&shell->HandlePreferenceEditorKey(field,event);},weak,base::Unretained(field)));field->view()->SetPreferredSize(gfx::Size(240,36));preference_textfields_[key]=field;
      auto* save=PreferenceAction(row,true,base::BindRepeating(&WorkspaceShell::SaveTextPreference,weak,key),text(spec.button));save->SetTooltipText(text(spec.button_ax));save->GetViewAccessibility().SetName(text(spec.button_ax));preference_save_buttons_[key]=save;continue;
    }
    switch(spec.kind){
      case Kind::Action:{
        base::RepeatingClosure callback;
        switch(spec.action){
          case Action::History:callback=base::BindRepeating(&WorkspaceShell::OpenTools,weak,3);break;
          case Action::Workspace:callback=base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings,weak);break;
          case Action::Passwords:callback=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell){shell->CloseOverlays();shell->callbacks_.passwords.Run();}},weak);break;
          case Action::Profiles:callback=base::BindRepeating(&WorkspaceShell::OpenProfiles,weak,std::string(),std::string());break;
          case Action::Import:callback=base::BindRepeating(&WorkspaceShell::OpenImport,weak);break;
          case Action::Proxy:case Action::Certificates:callback=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,bool certificates){if(shell&&shell->callbacks_.system_settings)shell->callbacks_.system_settings.Run(certificates);},weak,spec.action==Action::Certificates);break;
          case Action::Providers:callback=base::BindRepeating(&WorkspaceShell::OpenTools,weak,2);break;
          case Action::Shortcuts:callback=base::BindRepeating(&WorkspaceShell::OpenTools,weak,1);break;
          case Action::TimeMachine:callback=base::BindRepeating(&WorkspaceShell::OpenTools,weak,0);break;
          default:CHECK(false);
        }
        auto* button=PreferenceAction(row,true,std::move(callback),text(spec.button));button->SetTooltipText(text(spec.button_ax));button->GetViewAccessibility().SetName(text(spec.button_ax));break;
      }
      case Kind::Directory:
        download_directory_button_=PreferenceAction(row,true,callbacks_.choose_download_directory,text(spec.button));download_directory_button_->SetTooltipText(text(spec.button_ax));download_directory_button_->GetViewAccessibility().SetName(text(spec.button_ax));download_directory_label_=copy(row->children().front().get(),u"",kMuted,-1,gfx::Font::Weight::NORMAL,true);break;
      case Kind::SiteEditor:{
        const auto& site=catalogue.site;auto* fields=row->children().front().get();site_origin_=AddEditor(fields,true);site_origin_->GetViewAccessibility().SetName(text(site.origin_ax));site_origin_->SetPlaceholderText(text(site.placeholder));site_origin_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* field,const ui::KeyEvent& event){return shell&&shell->HandlePreferenceEditorKey(field,event);},weak,base::Unretained(site_origin_.get())));
        site_type_=ChoicePicker(fields,text(site.type_ax),strings(site.types),true);site_policy_=ChoicePicker(fields,text(site.policy_ax),strings(sidebar::preference_site_policies(0)),true);
        SetPickerCallback(site_type_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell)return;std::vector<std::u16string> choices;for(const auto& choice:sidebar::preference_site_policies(PickerIndex(shell->site_type_).value_or(0)))choices.push_back(base::UTF8ToUTF16(std::string(choice)));ReplacePickerLabels(shell->site_policy_,choices);},weak));
        PreferenceAction(row,true,base::BindRepeating(&WorkspaceShell::SaveSitePreference,weak),text(site.save));break;
      }
      case Kind::SiteRules:
        site_rules_card_=row;site_rules_list_=row->children().front()->AddChildView(std::make_unique<views::View>());SetSidebarPanelLayout(site_rules_list_,true,gfx::Insets(),8);break;
      case Kind::ClearData:{
        const auto& clear=catalogue.clear;auto* fields=row->children().front().get();clear_data_period_=ChoicePicker(fields,text(clear.period_ax),strings(clear.periods),true);auto* checks=fields->AddChildView(std::make_unique<SceneCheckboxGroup>(8));CHECK_EQ(clear.checks.size(),3u);
        clear_history_=AddImportCheck(checks,text(clear.checks[0]),{});clear_cookies_=AddImportCheck(checks,text(clear.checks[1]),{});clear_cache_=AddImportCheck(checks,text(clear.checks[2]),{});SetCheckValue(clear_cache_,true);
        clear_data_button_=PreferenceAction(row,true,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(!shell||!shell->callbacks_.clear_browsing_data)return;shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->clear_data_period_).value_or(0)),CheckValue(shell->clear_history_),CheckValue(shell->clear_cookies_),CheckValue(shell->clear_cache_)});},weak),text(clear.action));break;
      }
      case Kind::ShortcutHint:row->SetTooltipText(text(spec.tooltip));break;
      default:CHECK(false);
    }
  }
#endif
}
void WorkspaceShell::RefreshGeneralControls() {
  if (!general_widget_) return;
  bool profile_changed=false;
#if BUILDFLAG(IS_MAC)
  if(preferences_form_)profile_changed=preferences_form_->form->preference_profile(state_.profile_id);
  else
#endif
  {profile_changed=preferences_profile_id_!=state_.profile_id;if(profile_changed){preferences_profile_id_=state_.profile_id;preference_text_dirty_.clear();}}
  if(profile_changed)CancelChoiceInteractions(general_widget_->GetContentsView());
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
  if (profile_changed) {
    site_origin_->SetText(u"");
    ResetPreferenceRuleLimit();
    RefreshSitePreferences();
  }
  SetPanelText(preferences_profile_,u"Profil: " + state_.profile_name + u" · Ustawienia wspólne dla aplikacji są oznaczone w opisach.");
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
    field->view()->SetTooltipText(it == values.end() ? u"Ustawienie jest niedostępne" : !enabled ? u"To ustawienie jest zarządzane" : u"Zapisz przyciskiem lub klawiszem Enter");
  }
  auto directory = values.find("download_directory");
  download_directory_button_->SetEnabled(directory != values.end() && directory->second.editable);
  for (const auto& [key, control] : preference_toggles_) {
    auto it = values.find(key);
    control->SetEnabled(it != values.end() && it->second.editable);
    if (it != values.end()) SetToggleOn(control,it->second.value != 0);
    control->SetTooltipText(it == values.end() ? u"Profil nie jest jeszcze gotowy" :
        !it->second.editable ? u"To ustawienie jest zarządzane" : u"Dotyczy aktywnego profilu");
  }
  for (const auto& [key, control] : preference_pickers_) {
    auto it = values.find(key);
    control->SetEnabled(it != values.end() && it->second.editable);
    if (it != values.end()) SyncPickerIndex(control,it->second.value < 0 ? std::nullopt : std::optional<size_t>(it->second.value));
  }
}
void WorkspaceShell::SelectPreferenceCategory(size_t category) {
  if (category >= preference_sections_.size()) return;
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){if(!preferences_form_->form->preference_select(category))return;}
  else
#endif
  preference_category_ = category;
  SyncPickerIndex(preferences_category_picker_,category);
  preferences_search_->SetText(u"");
  FilterPreferences();
}
void WorkspaceShell::FilterPreferences() {
  if (site_rules_list_) RefreshSitePreferences(false);
  const auto query = base::i18n::ToLower(base::TrimWhitespace(preferences_search_->GetText(), base::TRIM_ALL));
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){
    SyncPickerIndex(preferences_category_picker_,preferences_form_->form->preference_current());
    std::vector<sidebar::PreferenceRow> rows;
    for(size_t i=0;i<preference_sections_.size();++i)for(const auto& row:preference_sections_[i].rows){sidebar::PreferenceRow input{};input.category=i;for(auto unit:row.keywords)input.keywords.push_back(unit);rows.push_back(std::move(input));}
    rust::Vec<uint16_t> normalized;for(auto unit:query)normalized.push_back(unit);
    auto visible=preferences_form_->form->preference_filter(std::move(normalized),rust::Slice<const sidebar::PreferenceRow>(rows.data(),rows.size()));
    size_t cursor=0;for(size_t i=0;i<preference_sections_.size();++i){auto& section=preference_sections_[i];for(auto& row:section.rows)row.view->SetVisible(visible.rows[cursor++]);section.view->SetVisible(visible.sections[i]);SetPreferenceNavigationSelected(section.navigation,visible.navigation[i]);}
    preferences_empty_->SetVisible(!visible.any);preferences_body_->InvalidateLayout();general_scroll_->ScrollTo(0);return;
  }
#endif
  bool any = false;
  for (size_t i = 0; i < preference_sections_.size(); ++i) {
    auto& section = preference_sections_[i];
    bool visible = false;
    for (auto& row : section.rows) {
      const bool match = query.empty() ? i == preference_category_ : row.keywords.find(query) != std::u16string::npos;
      row.view->SetVisible(match); visible |= match;
    }
    section.view->SetVisible(visible); any |= visible;
    SetPreferenceNavigationSelected(section.navigation,query.empty() && i == preference_category_);
  }
  preferences_empty_->SetVisible(!any);
  preferences_body_->InvalidateLayout();
  general_scroll_->ScrollTo(0);
}
void WorkspaceShell::SetBrowsingDataBusy(bool busy) {
  browsing_data_busy_ = busy;
  if (history_clear_) {
    const auto presentation=ToolsClearControls(bool(tools_form_),busy);
    history_clear_->SetEnabled(presentation.enabled);
    SetActionText(history_clear_,presentation.label);
  }
  if (!clear_data_button_) return;
  clear_data_button_->SetEnabled(!busy);
  SetActionText(clear_data_button_,busy ? u"Czyszczenie…" : u"Wyczyść…");
}
void WorkspaceShell::SetPreferencesStatus(std::u16string text, bool error) {
  if (history_visible()) SetPanelText(tools_status_,text);
  if (!general_status_) return;
  SetPanelText(general_status_,text);SetPreferenceCopyTone(general_status_,error?kError:kMuted);
}
void WorkspaceShell::PreferenceTextEdited(const std::string& key){
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){preferences_form_->form->preference_edited(key);return;}
#endif
  preference_text_dirty_.insert(key);
}
void WorkspaceShell::PreferenceTextSaved(const std::string& key,bool ok){
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){preferences_form_->form->preference_saved(key,ok);return;}
#endif
  if(ok)preference_text_dirty_.erase(key);
}
bool WorkspaceShell::SyncPreferenceText(const std::string& key,bool present,bool editable,bool composing,bool profile_changed){
#if BUILDFLAG(IS_MAC)
  if(preferences_form_)return preferences_form_->form->preference_sync_text(key,present,editable,composing,profile_changed);
#endif
  const bool replace=present&&((!preference_text_dirty_.contains(key)&&!composing)||!editable||profile_changed);
  if(replace)preference_text_dirty_.erase(key);return replace;
}
void WorkspaceShell::ResetPreferenceRuleLimit(){
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){preferences_form_->form->preference_reset_rules();return;}
#endif
  site_rules_limit_=50;
}
void WorkspaceShell::MorePreferenceRules(){
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){preferences_form_->form->preference_more_rules();return;}
#endif
  site_rules_limit_+=50;
}
size_t WorkspaceShell::PreferenceRuleLimit() const{
#if BUILDFLAG(IS_MAC)
  if(preferences_form_)return preferences_form_->form->preference_rule_limit();
#endif
  return site_rules_limit_;
}
void WorkspaceShell::SaveTextPreference(std::string key) {
  auto it = preference_textfields_.find(key);
  if (it == preference_textfields_.end()) return;
  const bool enabled=it->second->view()->GetEnabledInViewsSubtree();
  const bool composing=it->second->IsIMEComposing();
#if BUILDFLAG(IS_MAC)
  if(preferences_form_){if(!preferences_form_->form->preference_can_save(enabled,composing))return;}
  else
#endif
  if(!enabled||composing)return;
  const bool ok = callbacks_.write_text_preference && callbacks_.write_text_preference.Run(key, base::UTF16ToUTF8(it->second->GetText()));
  PreferenceTextSaved(key,ok);
  RefreshGeneralControls();
  SetPreferencesStatus(ok ? u"Zapisano. Witryna może wymagać odświeżenia, aby zastosować zmianę." :
      key == "accept_languages" ? u"Podaj poprawne kody języków oddzielone przecinkami, np. pl, en-US, en." :
      key == "dns_templates" ? u"Podaj poprawny szablon DNS-over-HTTPS. W trybie ścisłym adres nie może być pusty." :
      u"Nie udało się zapisać. Podaj nazwę rodziny czcionki (do 128 znaków). Ustawienie może być zarządzane.", !ok);
}
void WorkspaceShell::SaveSitePreference() {
  if(!site_origin_||site_origin_->IsIMEComposing())return;
  const auto type = PickerIndex(site_type_).value_or(kSitePreferenceKeys.size());
  const auto policy = PickerIndex(site_policy_).value_or(0);
  if (type >= kSitePreferenceKeys.size()) return;
  const bool ok = callbacks_.write_site_preference && callbacks_.write_site_preference.Run(
      base::UTF16ToUTF8(site_origin_->GetText()), kSitePreferenceKeys[type], static_cast<int>(policy));
  if (ok) { RefreshSitePreferences(); FilterPreferences(); }
  SetPreferencesStatus(ok ? u"Zapisano wyjątek w tym profilu. Odśwież witrynę, aby zastosować zmianę." :
      u"Nie zapisano wyjątku. Podaj adres HTTP(S) bez ścieżki i danych logowania. Sprawdź wybraną politykę; reguły zarządzane nie mogą być zmieniane.", !ok);
}
void WorkspaceShell::RefreshSitePreferences(bool reload) {
  if (!site_rules_list_) return;
  if (reload) site_preference_rules_ = callbacks_.read_site_preferences ? callbacks_.read_site_preferences.Run() : std::vector<SitePreferenceRule>();
  const auto query = base::i18n::ToLower(base::TrimWhitespace(preferences_search_->GetText(), base::TRIM_ALL));
  std::u16string keywords = u"uprawnienia stron zapisane wyjątki witryny reguły domyślne zarządzane";
  const bool show_all = query.empty() || keywords.find(query) != std::u16string::npos;
  site_rules_list_->RemoveAllChildViews();
  size_t matching = 0;
  size_t shown = 0;
  for (const auto& rule : site_preference_rules_) {
    auto it = std::find(kSitePreferenceKeys.begin(), kSitePreferenceKeys.end(), rule.key);
    const auto name = it == kSitePreferenceKeys.end() ? base::UTF8ToUTF16(rule.key) : kSitePreferenceNames[it - kSitePreferenceKeys.begin()];
    const auto* policy = rule.setting == 1 ? u"Zezwalaj" : rule.setting == 2 ? u"Blokuj" : rule.setting == 3 ? u"Pytaj" : u"Inna polityka";
    auto detail = base::UTF8ToUTF16(rule.primary) + u" · " + name + u" · " + policy;
    if (rule.secondary != "*") detail += u" · Osadzenie: " + base::UTF8ToUTF16(rule.secondary);
    if (!rule.editable) detail += u" · Zarządzane";
    auto searchable = base::i18n::ToLower(detail);
    keywords += u" " + searchable;
    if (!show_all && searchable.find(query) == std::u16string::npos) continue;
    ++matching;
    if (shown >= PreferenceRuleLimit()) continue;
    ++shown;
    auto* row = site_rules_list_->AddChildView(std::make_unique<views::View>());
    auto* label=AddPreferenceCopy(row,detail,kSubtle,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);
    SetSidebarPanelLayout(row,false,gfx::Insets::VH(4,0),12,label,true);
    auto* remove = PreferenceAction(row,bool(workspace_form_),base::BindRepeating(
        [](base::WeakPtr<WorkspaceShell> shell, SitePreferenceRule rule, std::string profile_id) {
          // Rebuild on the next UI task, after the pressed button has returned.
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
              [](base::WeakPtr<WorkspaceShell> shell, SitePreferenceRule rule, std::string profile_id) {
                if (!shell || shell->state_.profile_id != profile_id) return;
                const bool ok = shell->callbacks_.remove_site_preference && shell->callbacks_.remove_site_preference.Run(std::move(rule));
                shell->RefreshSitePreferences(); shell->FilterPreferences();
                shell->SetPreferencesStatus(ok ? u"Usunięto wyjątek. Witryna wróci do ustawień domyślnych." : u"Nie udało się usunąć wyjątku. Reguła może być zarządzana.", !ok);
              }, shell, std::move(rule), std::move(profile_id)));
        }, weak_factory_.GetWeakPtr(), rule, state_.profile_id), u"Usuń");
    remove->SetEnabled(rule.editable);
    remove->GetViewAccessibility().SetName(u"Usuń wyjątek: " + detail);
  }
  if (!matching) {
    AddPreferenceCopy(site_rules_list_,site_preference_rules_.empty()?u"Nie masz jeszcze zapisanych wyjątków.":u"Brak wyjątków pasujących do wyszukiwania.",kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);
  } else if (matching > shown) {
    PreferenceAction(site_rules_list_,bool(workspace_form_),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceShell> shell) {
        if (!shell) return;
        shell->MorePreferenceRules();
        shell->RefreshSitePreferences(false);
      }, shell));
    }, weak_factory_.GetWeakPtr()), u"Pokaż kolejne wyjątki…",true);
  }
  for (auto& row : preference_sections_[3].rows) if (row.view == site_rules_card_) row.keywords = keywords;
  site_rules_list_->InvalidateLayout();
}

void WorkspaceShell::WritePreference(std::string key, int value) {
  const bool ok = callbacks_.write_preference && callbacks_.write_preference.Run(key, value);
  RefreshGeneralControls();
  SetPreferencesStatus(ok ? (key == "hardware_acceleration" ? u"Zapisano. Uruchom ponownie Zitter, aby zmienić akcelerację sprzętową." : u"Zapisano. Jeśli strona nie zastosowała zmiany, odśwież ją.") :
      key == "dns_mode" ? u"Nie zapisano trybu DNS. Tryb ścisły wymaga poprawnego adresu DNS-over-HTTPS; ustawienie może też być zarządzane." :
      u"Nie udało się zmienić ustawienia. Profil może być zarządzany lub niedostępny.", !ok);
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
void WorkspaceShell::SaveGeneralPatch(std::optional<ThemeMode> theme, std::optional<SearchEngine> search,
    std::optional<bool> animations) {
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, std::nullopt, theme, search, animations, std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        SetPanelText(shell->general_status_,ok ? u"Przełączniki zapisują się automatycznie. Pola tekstowe zatwierdź przyciskiem Zapisz lub Enter." : u"Nie udało się zapisać ustawień. Spróbuj ponownie.");
        shell->SetPreferenceCopyTone(shell->general_status_,ok?kMuted:kError);
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceShell::ApplyAppearance(bool refresh_theme) {
  ScheduleMetalHeader();ScheduleMetalSplit();
  if (!widget_) return;
  const auto color = workspace_color(state_.workspace_id);
  if (refresh_theme) ApplyTheme();
  const auto tint = FrameTint(color, root_);
  root_->SetBackground(views::CreateSolidBackground(tint));
  root_->SchedulePaint();
  if (pinned_) {
    sidebar_root_->SetBackground(views::CreateSolidBackground(tint));
    sidebar_root_->SetBorder(nullptr);
  } else {
    sidebar_root_->SetBackground(views::CreateRoundedRectBackground(Mix(tint, Tone(root_, kCard), 70), 12));
    sidebar_root_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, 12, Hairline(root_)), gfx::Insets(6)));
  }
  static_cast<EdgeView*>(edge_.get())->color = color;
  edge_->SchedulePaint();
  color_input_->SetText(base::UTF8ToUTF16(Hex(color)));
#if BUILDFLAG(IS_MAC)
  size_t color_index=0;
#endif
  for (auto& child : color_input_->view()->parent()->children()) {
    if (auto* swatch=child.get();IsGlyphAction(swatch)) {
#if BUILDFLAG(IS_MAC)
      if(workspace_form_){SetActionOn(swatch,sidebar::appearance_selected(false,color_index++,color,{}));continue;}
#endif
      SetActionOn(swatch,swatch->GetTooltipText()==base::UTF8ToUTF16(Hex(color)));
    }
  }
  const auto emoji = workspace_emoji(state_.workspace_id);
  if (!emoji_input_->HasFocus()) emoji_input_->SetText(emoji);
#if BUILDFLAG(IS_MAC)
  size_t emoji_index=0;
#endif
  for (auto& child : emoji_input_->view()->parent()->children()) {
    if (auto* pick=child.get();IsGlyphAction(pick)) {
#if BUILDFLAG(IS_MAC)
      if(workspace_form_){SetActionOn(pick,sidebar::appearance_selected(true,emoji_index++,color,base::UTF16ToUTF8(emoji)));continue;}
#endif
      SetActionOn(pick,!emoji.empty()&&pick->GetTooltipText()==u"Emoji "+emoji);
    }
  }
  auto* pin = pin_button_.get();
  SetActionOn(pin,pinned_);
  pin->SetTooltipText(pinned_ ? u"Odepnij panel (auto-ukrywanie)" : u"Przypnij panel");
  static_cast<ShellRoot*>(root_.get())->pinned = pinned_;
  static_cast<ShellRoot*>(root_.get())->sidebar_width = sidebar_width_;
  root_->InvalidateLayout();
  // Workspace color changes do not resize the frame or hidden settings panels.
  if (refresh_theme) PositionPanels();
}

void WorkspaceShell::PositionPanels() {
  if (!sidebar_widget_ || !address_widget_ || !settings_widget_) return;
  const auto bounds = widget_->GetClientAreaBoundsInScreen();
  if (tools_widget_) {
    const int w = std::max(300, std::min(660, bounds.width() - 24));
    const int h = std::max(240, std::min(680, bounds.height() - 24));
    tools_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - w / 2, bounds.CenterPoint().y() - h / 2, w, h));
  }
  const int width = SidebarColumn(sidebar_width_, bounds.width());
  auto sidebar_bounds=gfx::Rect(bounds.x()+kGap,bounds.y()+kShellHeaderHeight,width,std::max(200,bounds.height()-kShellHeaderHeight-kGap));
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_){const auto g=sidebar::shell_layout(bounds.width(),bounds.height(),sidebar_width_,pinned_);sidebar_bounds=gfx::Rect(bounds.x()+g.sidebar.x,bounds.y()+g.sidebar.y,g.sidebar.width,g.sidebar.height);}
#endif
  sidebar_widget_->SetBounds(sidebar_bounds);
  if (general_widget_) {
    const int panel_width = std::max(320, std::min(960, bounds.width() - 24));
    const int panel_height = std::max(240, std::min(760, bounds.height() - 24));
    const bool sidebar = panel_width >= 760 && panel_height >= 720;
    preferences_navigation_->SetVisible(sidebar);
    preferences_category_picker_->SetVisible(!sidebar);
    general_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 8, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  const int settings_width = std::min(440, bounds.width() - 24);
  const int settings_height = std::min(settings_root_->GetHeightForWidth(settings_width), std::max(120, bounds.height() - 24));
  settings_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - settings_width / 2,
      std::max(bounds.y() + 12, bounds.CenterPoint().y() - settings_height / 2),
      settings_width, settings_height));
  if (profiles_widget_) {
    const int panel_width = std::min(420, bounds.width() - 24);
    const int panel_height = profiles_root_->GetHeightForWidth(panel_width);
    profiles_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 12, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  if (import_widget_) {
    const int panel_width = std::min(520, bounds.width() - 24);
    const int panel_height = std::min(import_root_->GetHeightForWidth(panel_width), bounds.height() - 24);
    import_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - panel_width / 2,
        std::max(bounds.y() + 12, bounds.CenterPoint().y() - panel_height / 2), panel_width, panel_height));
  }
  if (peek_widget_) {
    auto area = content_host_->GetBoundsInScreen();
    if (area.width() < 600) {
      area = bounds;
      area.Inset(gfx::Insets::TLBR(kShellHeaderHeight, 8, 8, 8));
    }
    const int margin = std::clamp(area.width() / 16, 16, 64);
    peek_widget_->SetBounds(gfx::Rect(area.x() + margin, area.y() + 20,
        std::max(240, area.width() - 2 * margin), std::max(280, area.height() - 40)));
  }
  int palette_width=std::min(640,std::max(120,bounds.width()-48));
#if BUILDFLAG(IS_MAC)
  if(metal_palette_)palette_width=sidebar::palette_width(bounds.width());
#endif
  const auto content = content_host_->GetBoundsInScreen();
  int results_height=std::max(90,std::min(300,content.height()-230));
#if BUILDFLAG(IS_MAC)
  if(metal_palette_)results_height=sidebar::palette_results_height(content.height());
#endif
  suggestion_scroll_->SetHeightRange(0,results_height);
  const int height = palette_root_->GetHeightForWidth(palette_width);
  auto palette_bounds=gfx::Rect(content.CenterPoint().x()-palette_width/2,content.y()+std::min(132,content.height()/6),palette_width,height);
#if BUILDFLAG(IS_MAC)
  if(metal_palette_){const auto r=sidebar::palette_frame({content.x(),content.y(),content.width(),content.height()},palette_width,height);palette_bounds=gfx::Rect(r.x,r.y,r.width,r.height);}
#endif
  address_widget_->SetBounds(palette_bounds);
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
    SetRowDetail(new_tab_button_.get(),base::UTF8ToUTF16(FormatShortcut(shortcut.accelerator)));
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
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    SetShortcutRecording(false);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&WorkspaceShell::RefreshKeyRegistrations, weak_factory_.GetWeakPtr()));
    SetPanelText(tools_status_,u"Nagrywanie anulowane."); return true;
  }
  if (!ValidShortcut(accelerator) || accelerator.key_code() == ui::VKEY_UNKNOWN) {
    SetPanelText(tools_status_,u"Użyj Cmd / Ctrl / Alt i klawisza. Skróty edycji tekstu oraz systemowe są zarezerwowane."); return true;
  }
  shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(accelerator)));
  SetShortcutRecording(false);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(&WorkspaceShell::RefreshKeyRegistrations, weak_factory_.GetWeakPtr()));
  SetPanelText(tools_status_,u"Kombinacja przechwycona. Wybierz Zapisz skrót."); return true;
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
    --shell->pending_writes_; if (!ok && shell->tools_status_) SetPanelText(shell->tools_status_,u"Nie udało się zapisać ustawień na dysku.");
  }, weak_factory_.GetWeakPtr()));
}
int WorkspaceShell::ToolsPage() const {
#if BUILDFLAG(IS_MAC)
  if(tools_form_)return static_cast<int>(tools_form_->form->tools_page());
#endif
  return tools_page_;
}
bool WorkspaceShell::ToolsRecording() const {
#if BUILDFLAG(IS_MAC)
  if(tools_form_)return tools_form_->form->tools_recording();
#endif
  return recording_shortcut_;
}
bool WorkspaceShell::history_loaded() const {
#if BUILDFLAG(IS_MAC)
  if(tools_form_)return tools_form_->form->tools_history_loaded();
#endif
  return history_loaded_;
}
std::u16string WorkspaceShell::ToolsHistoryQuery() const {
#if BUILDFLAG(IS_MAC)
  if(tools_form_){const auto units=tools_form_->form->tools_history_query();return std::u16string(units.begin(),units.end());}
#endif
  return history_query_;
}
std::u16string WorkspaceShell::ToolsTimelineQuery() const {
#if BUILDFLAG(IS_MAC)
  if(tools_form_){const auto units=tools_form_->form->tools_timeline_query();return std::u16string(units.begin(),units.end());}
#endif
  return timeline_query_;
}
void WorkspaceShell::ResetToolsForm() {
#if BUILDFLAG(IS_MAC)
  if(tools_form_){tools_form_->form->tools_reset();return;}
#endif
  history_query_.clear();timeline_query_.clear();history_loaded_=false;recording_shortcut_=false;
}
void WorkspaceShell::OpenTools(int page) {
  if (!widget_ || !tools_widget_) return;
  CloseOverlays(true);
#if BUILDFLAG(IS_MAC)
  if(tools_form_)tools_form_->form->tools_select(std::clamp(page,0,3));else
#endif
  tools_page_=std::clamp(page,0,3);
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
  size_t limit;
#if BUILDFLAG(IS_MAC)
  if(tools_form_)limit=tools_form_->form->tools_history_request(more);else
#endif
  {history_limit_=more?history_limit_+200:200;history_loaded_=false;limit=history_limit_;}
  history_more_->SetEnabled(false);
  SetPanelText(tools_status_,u"Odczytywanie historii…");
  callbacks_.query_history.Run(ToolsHistoryQuery(),limit);
}
void WorkspaceShell::SetHistoryResults(std::u16string query, std::string profile_id, size_t limit,
    std::vector<BrowsingHistoryEntry> entries, bool more, bool error) {
  if (!history_visible() || !history_search_ || profile_id != state_.profile_id) return;
#if BUILDFLAG(IS_MAC)
  if(tools_form_){if(!tools_form_->form->tools_history_accept(rust::Slice<const uint16_t>(reinterpret_cast<const uint16_t*>(query.data()),query.size()),limit))return;}else
#endif
  {if(history_query_!=query||limit!=history_limit_)return;history_loaded_=true;}
  history_entries_ = std::move(entries);
  history_list_->RemoveAllChildViews();
  std::u16string empty_text,status_text;bool more_visible,more_enabled;
#if BUILDFLAG(IS_MAC)
  if(tools_form_){const auto view=tools_form_->form->tools_history_view(history_entries_.size(),more,error);empty_text=base::UTF8ToUTF16(std::string(view.empty));status_text=base::UTF8ToUTF16(std::string(view.status));more_visible=view.more_visible;more_enabled=view.more_enabled;}else
#endif
  {empty_text=error?u"Nie udało się odczytać historii.":query.empty()?u"Brak historii przeglądania w tym profilu.":u"Brak wyników dla tego wyszukiwania.";status_text=error?u"Spróbuj ponownie otworzyć historię.":base::UTF8ToUTF16("Wyświetlono wpisów: "+std::to_string(history_entries_.size()));more_visible=more&&!error;more_enabled=!error;}
  if(history_entries_.empty())AddToolsCopy(history_list_,empty_text,kMuted,0,static_cast<int>(gfx::Font::Weight::NORMAL),true);
  for (const auto& entry : history_entries_) {
    RowButton::Spec row;
    row.title = entry.title.empty() ? base::UTF8ToUTF16(entry.url.spec()) : entry.title;
    row.detail = base::TimeFormatShortDateAndTime(entry.visited) + u" · " + base::UTF8ToUTF16(entry.url.spec());
    row.height = 60;
    auto callback=base::BindRepeating(
        [](base::WeakPtr<WorkspaceShell> shell, std::string profile_id, GURL url) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
              [](base::WeakPtr<WorkspaceShell> shell, std::string profile_id, GURL url) {
                if (!shell || shell->state_.profile_id != profile_id) return;
                if (shell->callbacks_.navigate.Run(url, true)) { shell->CloseOverlays(); shell->RestorePageFocus(); }
              }, shell, profile_id, url));
        }, weak_factory_.GetWeakPtr(), profile_id, entry.url);
    views::View* button;
#if BUILDFLAG(IS_MAC)
    if(tools_form_){
      row.sidebar_row=true;row.history=true;
      row.scene_visible=base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){return shell&&shell->ToolsChromeVisible();},weak_factory_.GetWeakPtr());
      button=history_list_->AddChildView(CreateSceneRow(std::move(callback),std::move(row),base::BindRepeating(&WorkspaceShell::ScheduleToolsChrome,weak_factory_.GetWeakPtr()),base::BindRepeating(&WorkspaceShell::WatchSceneRowInput,weak_factory_.GetWeakPtr())));
    }else
#endif
    button=history_list_->AddChildView(std::make_unique<RowButton>(std::move(callback),std::move(row)));
    button->SetTooltipText(base::UTF8ToUTF16(entry.url.spec()));
    button->GetViewAccessibility().SetDescription(base::TimeFormatShortDateAndTime(entry.visited) + u" · " + base::UTF8ToUTF16(entry.url.spec()));
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
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker))return scene->LabelAt(index);
#endif
  return static_cast<views::Combobox*>(picker)->GetModel()->GetItemAt(index);
}
bool WorkspaceShell::tools_choice_metal_visible_for_testing(std::string_view key) const{
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneSelect>(tools_choice_for_testing(key));return scene&&scene->metal_visible();
#else
  return false;
#endif
}
gfx::Rect WorkspaceShell::tools_choice_metal_bounds_for_testing(std::string_view key) const{
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneSelect>(tools_choice_for_testing(key));if(scene)return scene->metal_bounds_for_testing();
#endif
  return {};
}
void WorkspaceShell::ActivateToolsChoiceForTesting(std::string_view key,size_t index){
  auto* picker=tools_choice_for_testing(key);if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SelectForTesting(index);return;}
#endif
  static_cast<views::Combobox*>(picker)->MenuSelectionAt(index);
}
bool WorkspaceShell::tools_action_metal_visible_for_testing(views::View* button) const{return preference_action_metal_visible_for_testing(button);}
gfx::Rect WorkspaceShell::tools_action_metal_bounds_for_testing(views::View* button) const{
#if BUILDFLAG(IS_MAC)
  auto* group=button?views::AsViewClass<SceneActionGroup>(button->parent()):nullptr;return group?group->metal_bounds_for_testing():gfx::Rect();
#else
  return {};
#endif
}
bool WorkspaceShell::tools_actions_contains_scene_text_for_testing(std::string_view text) const{
#if BUILDFLAG(IS_MAC)
  if(!tools_body_)return false;
  auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,tools_body_);
#else
  return false;
#endif
}
size_t WorkspaceShell::tools_action_groups_for_testing() const{
#if BUILDFLAG(IS_MAC)
  if(!tools_body_)return 0;
  auto count=[&](auto&& self,views::View* view)->size_t{if(views::AsViewClass<SceneActionGroup>(view))return 1;size_t n=0;for(const auto& child:view->children())n+=self(self,child.get());return n;};return count(count,tools_body_);
#else
  return 0;
#endif
}
bool WorkspaceShell::tools_close_metal_visible_for_testing() const{
#if BUILDFLAG(IS_MAC)
  auto* group=tools_close_?views::AsViewClass<ScenePreferenceControls>(tools_close_->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
void WorkspaceShell::SetHistoryMoreVisible(bool visible){
  history_more_->SetVisible(visible);
#if BUILDFLAG(IS_MAC)
  if(auto* group=views::AsViewClass<SceneActionGroup>(history_more_->parent()))group->SetVisible(visible);
#endif
}
void WorkspaceShell::RefreshToolsViewport(){ScheduleToolsChrome();if(tools_body_){RefreshChoiceViewports(tools_body_);
#if BUILDFLAG(IS_MAC)
  SyncSceneRowInput();
#endif
}}
void WorkspaceShell::RefreshTimelineItems() {
  if (ToolsPage() != 0 || !tools_picker_) return;
  const auto state = callbacks_.time_machine ? callbacks_.time_machine.Run() : TimeMachineState();
#if BUILDFLAG(IS_MAC)
  if(tools_form_){
    // The Rust projection owns filtering, display copy and the current key-to-ID
    // map. Keep the parallel ID vectors only for the untouched Views baseline.
    snapshot_ids_.clear();closed_ids_.clear();
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
    fill(state.snapshots,tools_picker_,false);fill(state.closed,closed_picker_,true);return;
  }
#endif
  const auto query = ToolsTimelineQuery();
  auto fill = [&](const auto& items, views::View* picker, std::vector<std::string>& ids) {
    ids.clear(); std::vector<ui::SimpleComboboxModel::Item> labels;
    for (const auto& item : items) if (query.empty() || base::i18n::ToLower(item.title + u" " + item.detail).find(query) != std::u16string::npos) {
      ids.push_back(item.id); labels.emplace_back(item.title + u" — " + item.detail);
    }
    if (labels.empty()) labels.emplace_back(u"Brak zapisanych pozycji");
    auto* native=static_cast<views::Combobox*>(picker);
    // Chromium keeps the cached AX value if a model swap retains index zero.
    native->SetSelectedIndex(std::nullopt);
    native->SetOwnedModel(std::make_unique<ui::SimpleComboboxModel>(labels)); native->SetSelectedIndex(0); native->SetEnabled(!ids.empty());
  };
  fill(state.snapshots, tools_picker_, snapshot_ids_); fill(state.closed, closed_picker_, closed_ids_);
}
void WorkspaceShell::BuildRustToolsForm(){
#if BUILDFLAG(IS_MAC)
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
    auto* control=parent->AddChildView(std::make_unique<SceneSelect>(std::vector<std::u16string>(),std::move(name),base::RepeatingClosure()));std::vector<std::pair<std::string,std::u16string>> choices;choices.reserve(projection.choices.size());for(const auto& choice:projection.choices)choices.emplace_back(std::string(choice.key),text(choice.label));control->SetOptions(choices);control->SetSelectedIndex(0);control->SetEnabled(projection.enabled);return control;
  };
  for(const auto& node:plan.nodes){auto* parent=hosts.back().view.get();const auto label=text(node.text);views::View* control=nullptr;
    switch(node.kind){
      case Kind::BeginRow:{auto* row=parent->AddChildView(std::make_unique<views::View>());hosts.push_back({row,true,nullptr,node.spacing});continue;}
      case Kind::EndRow:{CHECK_GT(hosts.size(),1u);auto host=hosts.back();CHECK(host.row);SetSidebarPanelLayout(host.view,false,gfx::Insets(),host.spacing,host.flex);hosts.pop_back();continue;}
      case Kind::BeginActions:hosts.push_back({ActionGroup(parent,true,true,node.spacing),false,nullptr,node.spacing});continue;
      case Kind::EndActions:CHECK_GT(hosts.size(),1u);CHECK(!hosts.back().row);hosts.pop_back();continue;
      case Kind::Hint:control=AddToolsCopy(parent,label,kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);break;
      case Kind::Heading:control=AddToolsCopy(parent,label,kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);break;
      case Kind::Editor:{
        auto* field=AddEditor(parent,true);field->GetViewAccessibility().SetName(label);field->SetPlaceholderText(label);field->view()->SetPreferredSize(gfx::Size(200,36));field->SetPresentation(gfx::FontList().GetFontSize(),8,true,Tone(root_,kChip));field->SetCallbacks(base::BindRepeating(&WorkspaceShell::ToolsEditorEdited,weak,base::Unretained(field)),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,EditorControl* editor,const ui::KeyEvent& event){return shell&&shell->HandleToolsEditorKey(editor,event);},weak,base::Unretained(field)));control=field->view();
        switch(node.role){case Role::SnapshotName:snapshot_name_=field;break;case Role::TimelineFilter:timeline_filter_=field;break;case Role::ShortcutChord:shortcut_chord_=field;break;case Role::ProviderName:provider_name_=field;break;case Role::ProviderKeyword:provider_keyword_=field;break;case Role::ProviderUrl:provider_url_=field;break;case Role::HistoryQuery:history_search_=field;break;default:CHECK(false);}
        break;
      }
      case Kind::Choice:{
        if(node.role==Role::ShortcutChoice){rust::Vec<sidebar::ToolsShortcutItem> metadata;for(const auto& item:tools_.shortcuts)metadata.push_back({item.id,units(item.title),units(base::UTF8ToUTF16(FormatShortcut(item.accelerator)))});const auto projection=tools_form_->form->tools_shortcut_choices(std::move(metadata));control=form_picker(parent,label,projection);}
        else if(node.role==Role::ProviderChoice){rust::Vec<sidebar::ToolsProviderItem> metadata;for(const auto& item:tools_.providers)metadata.push_back({item.keyword,units(base::UTF8ToUTF16(item.name)),units(base::UTF8ToUTF16(item.keyword)),units(base::UTF8ToUTF16(item.url)),item.keyword==tools_.default_keyword});const auto projection=tools_form_->form->tools_provider_choices(std::move(metadata));control=form_picker(parent,label,projection);}
        else{std::vector<std::u16string> choices;for(const auto& value:node.choices)choices.push_back(base::UTF8ToUTF16(std::string(value)));control=ChoicePicker(parent,label,std::move(choices),true);SetPickerIndex(control,0);}
        switch(node.role){case Role::SnapshotChoice:tools_picker_=control;timeline=true;break;case Role::ClosedChoice:closed_picker_=control;break;case Role::ShortcutChoice:case Role::ProviderChoice:tools_picker_=control;SetPickerCallback(control,base::BindRepeating(&WorkspaceShell::RefreshToolsChoiceFields,weak));break;case Role::HistoryPeriod:history_period_=control;break;default:CHECK(false);}
        break;
      }
      case Kind::Action:{
        views::View* button;
        if(node.role==Role::HistoryClear){button=PreferenceAction(parent,true,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell){if(shell&&shell->history_period_&&shell->callbacks_.clear_browsing_data)shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->history_period_).value_or(0)),true,false,false});},weak),label);history_clear_=button;}
        else if(node.role==Role::HistoryMore){button=PreferenceAction(parent,true,base::BindRepeating(&WorkspaceShell::RefreshHistory,weak,true),label,true);history_more_=button;}
        else button=AddAction(parent,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,int action){if(shell)shell->PostToolsAction(action);},weak,node.action),label);
        control=button;break;
      }
      case Kind::Rows:control=parent->AddChildView(std::make_unique<views::View>());control->SetLayoutManager(CreateSidebarColumnLayout(node.spacing));CHECK(node.role==Role::HistoryRows);history_list_=control;break;
      default:CHECK(false);
    }
    CHECK(control);control->SetEnabled(control->GetEnabled()&&node.enabled);if(node.role==Role::HistoryMore)SetHistoryMoreVisible(node.visible);else control->SetVisible(node.visible);if(node.flex)hosts.back().flex=control;
  }
  CHECK_EQ(hosts.size(),1u);RefreshToolsChoiceFields();if(timeline)RefreshTimelineItems();
#endif
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
  history_more_ = nullptr; history_clear_ = nullptr; history_entries_.clear();
  snapshot_name_ = nullptr; timeline_filter_ = nullptr; shortcut_chord_ = nullptr;
  provider_name_ = nullptr; provider_keyword_ = nullptr; provider_url_ = nullptr; tools_picker_ = nullptr; closed_picker_ = nullptr;
  tools_body_->RemoveAllChildViews();
#if BUILDFLAG(IS_MAC)
  if(tools_form_){BuildRustToolsForm();PreparePanelText(tools_widget_->GetRootView());tools_body_->InvalidateLayout();if(history_visible())RefreshHistory();return;}
#endif
  std::u16string context=ToolsPage()==0?state_.workspace_name:std::u16string();
  if(ToolsPage()==3)for(const auto& profile:state_.profiles)if(profile.id==state_.profile_id)context=base::UTF8ToUTF16(profile.name);
  std::u16string form_title,form_intro,form_tail,form_status;
  std::vector<std::u16string> fields,pickers,headings,actions,form_choices;
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){
    auto data=sidebar::tools_copy(ToolsPage(),base::UTF16ToUTF8(context));
    auto strings=[](const auto& input){std::vector<std::u16string> out;for(const auto& value:input)out.push_back(base::UTF8ToUTF16(std::string(value)));return out;};
    form_title=base::UTF8ToUTF16(std::string(data.title));form_intro=base::UTF8ToUTF16(std::string(data.intro));form_tail=base::UTF8ToUTF16(std::string(data.tail));form_status=base::UTF8ToUTF16(std::string(data.initial_status));
    fields=strings(data.fields);pickers=strings(data.pickers);headings=strings(data.headings);actions=strings(data.actions);form_choices=strings(data.choices);
  }
#endif
  auto text=[&](std::vector<std::u16string>& data,size_t index,std::u16string_view native){return workspace_form_?std::move(data.at(index)):std::u16string(native);};
  SetPanelText(tools_status_,workspace_form_?form_status:u"Użyj przycisków poniżej pól lub Enter, aby zatwierdzić zmiany. Dane są zapisywane lokalnie.");
  auto weak = weak_factory_.GetWeakPtr();
  auto hint = [&](std::u16string text) {return AddToolsCopy(tools_body_,std::move(text),kMuted,-1,static_cast<int>(gfx::Font::Weight::NORMAL),true);};
  auto input = [&](std::u16string name) {
    auto* field = AddEditor(tools_body_,bool(workspace_form_));
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
#if BUILDFLAG(IS_MAC)
    if(workspace_form_){
      // Batch adjacent actions while preserving the native body sequence/gaps.
      if(!tools_body_->children().empty())if(auto* group=views::AsViewClass<SceneActionGroup>(tools_body_->children().back()))return group;
      return ActionGroup(tools_body_,true,true,10);
    }
#endif
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
    if(values.empty())values.emplace_back(u"Brak pozycji");
#if BUILDFLAG(IS_MAC)
    if(workspace_form_){auto* control=ChoicePicker(parent,std::move(name),std::move(values),true);SetPickerIndex(control,0);return control;}
#endif
    std::vector<ui::SimpleComboboxModel::Item> items; for (const auto& value : values) items.emplace_back(value);
    if (items.empty()) items.emplace_back(u"Brak pozycji");
    auto* control = parent->AddChildView(std::make_unique<views::Combobox>(std::make_unique<ui::SimpleComboboxModel>(items)));
    control->SetTooltipTextAndAccessibleName(name); control->SetSelectedIndex(0); return control;
  };
#if BUILDFLAG(IS_MAC)
  auto units=[](const std::u16string& text){rust::Vec<uint16_t> value;value.reserve(text.size());for(char16_t unit:text)value.push_back(unit);return value;};
  auto form_picker=[&](std::u16string name,const sidebar::ToolsFormChoices& projection)->views::View*{
    auto* control=tools_body_->AddChildView(std::make_unique<SceneSelect>(std::vector<std::u16string>(),std::move(name),base::RepeatingClosure()));
    std::vector<std::pair<std::string,std::u16string>> choices;choices.reserve(projection.choices.size());for(const auto& choice:projection.choices)choices.emplace_back(std::string(choice.key),std::u16string(choice.label.begin(),choice.label.end()));
    control->SetOptions(choices);control->SetSelectedIndex(0);control->SetEnabled(projection.enabled);return control;
  };
#endif
  if (ToolsPage() == 0) {
    SetPanelText(tools_title_,workspace_form_?form_title:u"Time Machine · " + state_.workspace_name);
    hint(workspace_form_?form_intro:u"Snapshot zapisuje karty, foldery i Split View w tej przestrzeni. Przywrócenie przeładuje strony; formularze i historia Wstecz nie są częścią snapshotu. Poprzedni układ otrzyma punkt przywracania.");
    snapshot_name_ = input(text(fields,0,u"Nazwa snapshotu, np. Research przed deployem"));
    button(u"Zapisz snapshot", 1);
    timeline_filter_ = input(text(fields,1,u"Szukaj snapshotów i zamkniętych kart"));
    AddToolsCopy(tools_body_,text(headings,0,u"Snapshoty · 30 własnych + 30 automatycznych"),kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
    tools_picker_ = picker(text(pickers,0,u"Zapisane snapshoty"), {});
    button(u"Przywróć wybrany układ", 2); button(u"Usuń wybrany snapshot", 3);
    AddToolsCopy(tools_body_,text(headings,1,u"Ostatnio zamknięte · do 100 kart"),kSubtle,0,static_cast<int>(gfx::Font::Weight::SEMIBOLD),false);
    closed_picker_ = picker(text(pickers,1,u"Zamknięte karty"), {});
    button(u"Przywróć wybraną kartę", 4);
    hint(workspace_form_?form_tail:u"Uśpienie zwalnia strony tej przestrzeni z pamięci i przełącza do innej. Kliknięcie jej kropki wznowi układ. Niezapisane formularze zostaną utracone.");
    button(u"Uśpij tę przestrzeń", 5);
    const auto timeline = callbacks_.time_machine ? callbacks_.time_machine.Run() : TimeMachineState();
    for (const auto& item : timeline.sleeping) hint(item.title + u" · " + item.detail);
    RefreshTimelineItems();
  } else if (ToolsPage() == 1) {
    SetPanelText(tools_title_,workspace_form_?form_title:u"Skróty klawiszowe");
    hint(workspace_form_?form_intro:u"Schemat inspirowany Arc. Wybierz akcję, nagraj kombinację lub wpisz np. Cmd+Shift+J. Puste pole wyłącza skrót. Escape, skróty edycji tekstu i systemowe pozostają zarezerwowane.");
#if BUILDFLAG(IS_MAC)
    if(tools_form_){
      rust::Vec<sidebar::ToolsShortcutItem> metadata;for(const auto& shortcut:tools_.shortcuts)metadata.push_back({shortcut.id,units(shortcut.title),units(base::UTF8ToUTF16(FormatShortcut(shortcut.accelerator)))});
      const auto projection=tools_form_->form->tools_shortcut_choices(std::move(metadata));tools_picker_=form_picker(text(pickers,0,u"Akcja skrótu"),projection);
      shortcut_chord_=input(text(fields,0,u"Kombinacja, np. Cmd+Shift+J"));RefreshToolsChoiceFields();
    }else
#endif
    {
    std::vector<std::u16string> choices;
    for (const auto& shortcut : tools_.shortcuts) choices.push_back(shortcut.title + u" — " + base::UTF8ToUTF16(FormatShortcut(shortcut.accelerator)));
    tools_picker_ = picker(text(pickers,0,u"Akcja skrótu"), choices);
    shortcut_chord_ = input(text(fields,0,u"Kombinacja, np. Cmd+Shift+J"));
    shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(tools_.shortcuts.front().accelerator)));
    }
    SetPickerCallback(tools_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (!shell || !shell->shortcut_chord_) return;
#if BUILDFLAG(IS_MAC)
      if(shell->tools_form_){shell->RefreshToolsChoiceFields();return;}
#endif
      const size_t i = PickerIndex(shell->tools_picker_).value_or(0);
      if (i < shell->tools_.shortcuts.size()) shell->shortcut_chord_->SetText(base::UTF8ToUTF16(FormatShortcut(shell->tools_.shortcuts[i].accelerator)));
      shell->SetShortcutRecording(false);
      shell->RefreshKeyRegistrations();
    }, weak));
    button(u"Nagraj kombinację…", 10); button(u"Zapisz skrót", 11);
    button(u"Wyłącz wybrany skrót", 12); button(u"Przywróć schemat Arc", 13);
  } else if (ToolsPage() == 2) {
    SetPanelText(tools_title_,workspace_form_?form_title:u"Wyszukiwarki i prefiksy");
    hint(workspace_form_?form_intro:u"W pasku adresu wpisz prefiks i zapytanie, np. g koty lub w Warszawa. Szablon musi być adresem HTTP(S) z jednym znacznikiem {query}. Zapytania są kodowane automatycznie.");
#if BUILDFLAG(IS_MAC)
    if(tools_form_){
      rust::Vec<sidebar::ToolsProviderItem> metadata;for(const auto& provider:tools_.providers)metadata.push_back({provider.keyword,units(base::UTF8ToUTF16(provider.name)),units(base::UTF8ToUTF16(provider.keyword)),units(base::UTF8ToUTF16(provider.url)),provider.keyword==tools_.default_keyword});
      const auto projection=tools_form_->form->tools_provider_choices(std::move(metadata));tools_picker_=form_picker(text(pickers,0,u"Wyszukiwarka do edycji"),projection);
      provider_name_=input(text(fields,0,u"Nazwa wyszukiwarki"));provider_keyword_=input(text(fields,1,u"Prefiks (a–z, 0–9)"));provider_url_=input(text(fields,2,u"Adres z {query}"));RefreshToolsChoiceFields();
    }else
#endif
    {
    std::vector<std::u16string> provider_choices{workspace_form_?std::move(form_choices.at(0)):u"Dodaj nową wyszukiwarkę…"};
    for (const auto& provider : tools_.providers) provider_choices.push_back(base::UTF8ToUTF16(provider.keyword + " · " + provider.name + (provider.keyword == tools_.default_keyword ? " · domyślna" : "")));
    tools_picker_ = picker(text(pickers,0,u"Wyszukiwarka do edycji"), provider_choices);
    provider_name_ = input(text(fields,0,u"Nazwa wyszukiwarki")); provider_keyword_ = input(text(fields,1,u"Prefiks (a–z, 0–9)")); provider_url_ = input(text(fields,2,u"Adres z {query}"));
    }
    SetPickerCallback(tools_picker_,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (!shell || !shell->provider_name_) return;
#if BUILDFLAG(IS_MAC)
      if(shell->tools_form_){shell->RefreshToolsChoiceFields();return;}
#endif
      const size_t i = PickerIndex(shell->tools_picker_).value_or(0);
      SearchProvider provider;
      if (i > 0 && i <= shell->tools_.providers.size()) provider = shell->tools_.providers[i - 1];
      shell->provider_name_->SetText(base::UTF8ToUTF16(provider.name)); shell->provider_keyword_->SetText(base::UTF8ToUTF16(provider.keyword)); shell->provider_url_->SetText(base::UTF8ToUTF16(provider.url));
    }, weak));
    button(u"Zapisz wyszukiwarkę", 20); button(u"Ustaw jako domyślną", 21); button(u"Usuń wyszukiwarkę", 22);
  }
  if (ToolsPage() == 3) {
    SetPanelText(tools_title_,workspace_form_?form_title:u"Historia przeglądania");
    hint(workspace_form_?form_intro:u"Profil: "+context);
    history_search_ = input(text(fields,0,u"Szukaj w historii"));
    auto* clear_row = tools_body_->AddChildView(std::make_unique<views::View>());
    history_period_=picker(text(pickers,0,u"Zakres czyszczenia historii"),workspace_form_?std::move(form_choices):std::vector<std::u16string>{u"Ostatnia godzina",u"Ostatnie 24 godziny",u"Ostatnie 7 dni",u"Cały okres"},clear_row);
    const auto clear_presentation=ToolsClearControls(bool(tools_form_),browsing_data_busy_);
    history_clear_ = PreferenceAction(clear_row,bool(workspace_form_),base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
      if (shell && shell->history_period_ && shell->callbacks_.clear_browsing_data)
        shell->callbacks_.clear_browsing_data.Run({static_cast<int>(PickerIndex(shell->history_period_).value_or(0)), true, false, false});
    }, weak), clear_presentation.label);
    SetSidebarPanelLayout(clear_row,false,gfx::Insets(),8,history_period_);
    history_list_ = tools_body_->AddChildView(std::make_unique<views::View>());
    if(tools_form_)history_list_->SetLayoutManager(CreateSidebarColumnLayout(4));
    else history_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    history_more_ = PreferenceAction(tools_body_,bool(workspace_form_),base::BindRepeating(&WorkspaceShell::RefreshHistory, weak, true), text(actions,1,u"Pokaż więcej"),true);
    SetHistoryMoreVisible(false);
    SetPanelText(tools_status_,workspace_form_?form_status:u"Odczytywanie historii…");
    history_clear_->SetEnabled(clear_presentation.enabled);
  }
  PreparePanelText(tools_widget_->GetRootView()); tools_body_->InvalidateLayout();
  if (history_visible()) RefreshHistory();
}
void WorkspaceShell::RefreshToolsChoiceFields(){
#if BUILDFLAG(IS_MAC)
  if(!tools_form_)return;auto* scene=views::AsViewClass<SceneSelect>(tools_picker_);if(!scene)return;
  const auto draft=tools_form_->form->tools_choice_select(scene->SelectedKey());if(!draft.valid)return;
  auto text=[](const auto& units){return std::u16string(units.begin(),units.end());};
  if(draft.page==1&&shortcut_chord_)shortcut_chord_->SetText(text(draft.chord));
  if(draft.page==2&&provider_name_&&provider_keyword_&&provider_url_){provider_name_->SetText(text(draft.name));provider_keyword_->SetText(text(draft.keyword));provider_url_->SetText(text(draft.url));}
  if(draft.stop_recording){SetShortcutRecording(false);RefreshKeyRegistrations();}
#endif
}
void WorkspaceShell::PostToolsAction(int action) {
  std::optional<std::string> choice_key;
#if BUILDFLAG(IS_MAC)
  if(tools_form_&&(action==2||action==3||action==4||action==11||action==12||(action>=20&&action<=22))){
    auto* picker=action==4?closed_picker_.get():tools_picker_.get();auto* scene=views::AsViewClass<SceneSelect>(picker);
    choice_key=scene?scene->SelectedKey():std::string();
  }
#endif
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell,uint64_t generation,std::string workspace,std::string profile,int action,std::optional<std::string> key){
        if(shell&&shell->tools_generation_==generation&&shell->state_.workspace_id==workspace&&shell->state_.profile_id==profile)
          shell->ToolsAction(action,std::move(key));
      },weak_factory_.GetWeakPtr(),tools_generation_,state_.workspace_id,state_.profile_id,action,std::move(choice_key)));
}
void WorkspaceShell::SetShortcutRecording(bool recording) {
  if(recording&&shortcut_chord_)shortcut_chord_->ClearCompositionText();
#if BUILDFLAG(IS_MAC)
  if(tools_form_)tools_form_->form->tools_record(recording);else
#endif
  recording_shortcut_=recording;
  if(shortcut_chord_)shortcut_chord_->SetReadOnly(recording);
}
void WorkspaceShell::ToolsAction(int action,std::optional<std::string> choice_key) {
  if (!tools_widget_ || !tools_widget_->IsVisible()) return;
  const size_t index = tools_picker_ ? PickerIndex(tools_picker_).value_or(0) : 0;
  std::u16string record_status;
  bool rust_timeline=false;bool valid_form_choice=false;std::string snapshot_target,closed_target,form_target;
#if BUILDFLAG(IS_MAC)
  if(tools_form_){
    rust_timeline=true;
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
  }else
#endif
  {
    if(action==1&&snapshot_name_&&snapshot_name_->IsIMEComposing())return;
    if(action>=20&&action<=22)for(auto* field:{provider_name_.get(),provider_keyword_.get(),provider_url_.get()})if(field&&field->IsIMEComposing())return;
    if((action==11||action==12)&&shortcut_chord_&&shortcut_chord_->IsIMEComposing())return;
    if(action!=10&&ToolsRecording()){SetShortcutRecording(false);RefreshKeyRegistrations();}
  }
  bool ok = false;
  if (action == 1 && snapshot_name_) {
    ok = callbacks_.save_snapshot && callbacks_.save_snapshot.Run(base::UTF16ToUTF8(snapshot_name_->GetText()), false);
  } else if ((action == 2 || action == 3) && (rust_timeline?!snapshot_target.empty():index<snapshot_ids_.size())) {
    const auto& id=rust_timeline?snapshot_target:snapshot_ids_[index];
    ok = action == 2 ? callbacks_.restore_snapshot.Run(id) : callbacks_.delete_snapshot.Run(id);
  } else if (action == 4 && closed_picker_) {
    if(rust_timeline){if(!closed_target.empty())ok=callbacks_.reopen_closed.Run(closed_target);}
    else{const size_t closed=PickerIndex(closed_picker_).value_or(0);if(closed<closed_ids_.size())ok=callbacks_.reopen_closed.Run(closed_ids_[closed]);}
  } else if (action == 5) {
    const std::string id = state_.workspace_id;
    ok = callbacks_.sleep_workspace && callbacks_.sleep_workspace.Run(id);
    if (ok) return;
  } else if (action == 10 && shortcut_chord_) {
    if(!tools_form_){SetShortcutRecording(true);RefreshKeyRegistrations();}
    shortcut_chord_->RequestFocus();
    SetPanelText(tools_status_,tools_form_?record_status:u"Naciśnij kombinację. Escape anuluje nagrywanie."); return;
  } else if ((action == 11 || action == 12) && shortcut_chord_ && (rust_timeline?valid_form_choice:index<tools_.shortcuts.size())) {
    ok = SetShortcut(rust_timeline?form_target:tools_.shortcuts[index].id, action == 12 ? "" : base::UTF16ToUTF8(shortcut_chord_->GetText()));
  } else if (action == 13) {
    tools_.shortcuts = ArcShortcuts(); keys_dirty_ = true; RefreshKeyRegistrations(); SaveTools(); ok = true;
  } else if (action >= 20 && action <= 22 && provider_name_) {
    const std::string previous=rust_timeline?form_target:(index>0&&index<=tools_.providers.size()?tools_.providers[index-1].keyword:"");
    if (action == 20) ok = UpsertSearchProvider({base::UTF16ToUTF8(provider_name_->GetText()), base::UTF16ToUTF8(provider_keyword_->GetText()), base::UTF16ToUTF8(provider_url_->GetText())}, previous);
    if (action == 21) ok = SetDefaultSearchProvider(previous);
    if (action == 22) ok = RemoveSearchProvider(previous);
  }
#if BUILDFLAG(IS_MAC)
  if(tools_form_){
    auto outcome=tools_form_->form->tools_outcome(ok,false);
    if(outcome.refresh){RefreshTools();if(callbacks_.operation_pending&&callbacks_.operation_pending.Run())outcome=tools_form_->form->tools_outcome(true,true);}
    SetPanelText(tools_status_,base::UTF8ToUTF16(std::string(outcome.status)));return;
  }
#endif
  if (ok) { RefreshTools(); SetPanelText(tools_status_,callbacks_.operation_pending && callbacks_.operation_pending.Run() ? u"Oczekiwanie na zgodę na opuszczenie strony…" : u"Gotowe. Zmiany zostały zastosowane."); }
  else SetPanelText(tools_status_,ToolsPage() == 1 ? u"Nieprawidłowa lub zajęta kombinacja. Zmień ją albo wyłącz skrót drugiej akcji." : ToolsPage() == 2 ? u"Sprawdź unikalny prefiks, nazwę i adres z {query}. Musi pozostać co najmniej jedna wyszukiwarka." : u"Nie można wykonać operacji. Podaj nazwę, wybierz pozycję zgodną z profilem lub zostaw drugą przestrzeń do przełączenia.");
}

void WorkspaceShell::SetOperationStatus(std::u16string text) {
  if (tools_widget_ && tools_widget_->IsVisible()) { RefreshTools(); SetPanelText(tools_status_,text); }
  if (profiles_widget_ && profiles_widget_->IsVisible()) SetProfileStatus(text);
}
void WorkspaceShell::OpenAddress(bool new_tab) {
  command_palette_ = false;
  address_->SetPlaceholderText(u"Szukaj lub wpisz adres…");
  address_->GetViewAccessibility().SetName(u"Szukaj lub wpisz adres");
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
  address_widget_->Show();
  address_widget_->Activate();
  address_widget_->ClearNativeFocus();
  address_->RequestFocus();
  address_->SelectRange(gfx::Range(0,address_->GetText().size()));
  RequestSearch();
}
void WorkspaceShell::OpenCommandPalette() {
  OpenAddress(false);
  command_palette_ = true;
  address_->SetPlaceholderText(u"Command Center · szukaj lub > polecenie…");
  address_->GetViewAccessibility().SetName(u"Command Center — karty, historia i polecenia");
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
#if BUILDFLAG(IS_MAC)
  if(import_form_)import_form_->form->cancel_scan();
#endif
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
  if (callbacks_.focus_pane) callbacks_.focus_pane.Run(state_.right_active);
}
void WorkspaceShell::SetPinned(bool pinned) {
  pinned_ = pinned;
  pin_dirty_ = true;
  RefreshGeneralControls();
  ApplyAppearance();
  if (pinned_) ShowSidebar();
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, pinned_, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) shell->ShowWorkspaceAppearanceError(0);
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::ResizeSidebar(int amount, bool done) {
  if (!widget_) return;
  if (!sidebar_resizing_) { sidebar_resize_start_ = sidebar_width_; sidebar_resizing_ = true; }
  int width;
#if BUILDFLAG(IS_MAC)
  if(metal_sidebar_)width=sidebar::sidebar_resize_width(sidebar_resize_start_,amount);
  else
#endif
    width=std::clamp(sidebar_resize_start_+amount,kMinSidebarWidth,kMaxSidebarWidth);
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
                     std::nullopt, std::optional<int>(sidebar_width_), std::nullopt),
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
      base::BindOnce(SavePatch, settings_path_, std::optional<std::string>(key), colors_[key], std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt),
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
                     std::optional<std::pair<std::string, std::string>>({key, base::UTF16ToUTF8(emoji)})),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) shell->ShowWorkspaceAppearanceError(2);
      }, weak_factory_.GetWeakPtr()));
}
SkColor WorkspaceShell::workspace_color(std::string_view id) const {
  auto it = colors_.find(std::string(id));
  return it == colors_.end() ? 0xFF9E91DC : it->second;
}
void WorkspaceShell::ToggleWorkspaceCreation() {
  if(creating_workspace_){RefreshWorkspaceSettings();PositionPanels();return;}
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)workspace_form_->form->begin_create(state_.workspace_id);
#endif
  creating_workspace_=true;workspace_appearance_->SetVisible(false);configuration_error_->SetVisible(false);
  SetPanelText(settings_title_,u"Nowa przestrzeń");workspace_input_->SetText(u"");workspace_input_->RequestFocus();
  SetActionText(edit_workspace_,u"Utwórz przestrzeń");SetActionText(add_workspace_,u"Anuluj");
  RefreshRustWorkspacePresentation();PositionPanels();
}
void WorkspaceShell::RefreshRustWorkspacePresentation() {
#if BUILDFLAG(IS_MAC)
  if(!workspace_form_)return;
  const auto v=workspace_form_->form->view(base::UTF16ToUTF8(state_.workspace_name),base::UTF16ToUTF8(state_.profile_name),state_.configuration_ready,state_.profiles_busy,state_.workspaces.size());
  creating_workspace_=v.creating;SetPanelText(settings_title_,base::UTF8ToUTF16(std::string(v.title)));SetPanelText(profile_name_,base::UTF8ToUTF16(std::string(v.description)));
  SetActionText(edit_workspace_,base::UTF8ToUTF16(std::string(v.save_label)));SetActionText(add_workspace_,base::UTF8ToUTF16(std::string(v.secondary_label)));
  workspace_appearance_->SetVisible(v.appearance);edit_workspace_->SetEnabled(v.enabled);add_workspace_->SetEnabled(v.enabled);profile_picker_->SetEnabled(v.enabled);remove_workspace_->SetEnabled(v.remove_enabled);
  SetPanelText(configuration_error_,base::UTF8ToUTF16(std::string(v.error)));configuration_error_->SetVisible(!v.error.empty());
#endif
}
void WorkspaceShell::ShowWorkspaceAppearanceError(unsigned kind) {
  static constexpr std::array<std::u16string_view,4> messages={u"Nie udało się zapisać ustawień",u"Nie udało się zapisać koloru",u"Nie udało się zapisać emoji",u"Podaj kolor #RRGGBB"};
  std::u16string text(messages[std::min<size_t>(kind,3)]);
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)text=base::UTF8ToUTF16(std::string(sidebar::workspace_appearance_error(kind)));
#endif
  SetPanelText(color_error_,std::move(text));color_error_->SetVisible(true);PositionPanels();
}
bool WorkspaceShell::workspace_panel_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  if(!settings_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* picker=views::AsViewClass<SceneSelect>(view))if(picker->contains_text(text))return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,settings_root_);
#else
  return false;
#endif
}
void WorkspaceShell::RefreshWorkspaceSettings() {
#if BUILDFLAG(IS_MAC)
  if(workspace_form_)workspace_form_->form->reset(state_.workspace_id);
#endif
  creating_workspace_ = false;
  workspace_appearance_->SetVisible(true);
  SetPanelText(settings_title_,state_.workspace_name);
  SetActionText(edit_workspace_,u"Zapisz nazwę i profil");
  SetActionText(add_workspace_,u"Nowa przestrzeń…");
  workspace_input_->SetText(state_.workspace_name);
  for (size_t i = 0; i < state_.profiles.size(); ++i)
    if (state_.profiles[i].id == state_.profile_id) SetPickerIndex(profile_picker_,i);
  RefreshRustWorkspacePresentation();
}
void WorkspaceShell::SetConfigurationError(bool error) {
#if BUILDFLAG(IS_MAC)
  if(workspace_form_){workspace_form_->form->set_configuration_error(error);RefreshRustWorkspacePresentation();PositionPanels();return;}
#endif
  SetPanelText(configuration_error_,error ? u"Nie udało się odczytać lub zapisać przestrzeni." : u"");
  configuration_error_->SetVisible(error);
  PositionPanels();
}
bool WorkspaceShell::ShowRustWorkspaceNotice(std::string workspace,std::string title) {
#if BUILDFLAG(IS_MAC)
  if(!metal_sidebar_)return false;
  auto content=std::make_unique<WorkspaceNoticeView>(workspace,title,base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell,WorkspaceNoticeView* notice){
    if(!shell)return true;const auto alive=notice->weak();auto request=notice->Prepare(shell->state_.configuration_ready,shell->state_.profiles_busy,shell->state_.workspaces);
    if(request.action!=sidebar::WorkspaceNoticeAction::Delete)return false;
    const bool accepted=shell->callbacks_.remove_workspace.Run(std::string(request.workspace));
    if(shell)shell->SetConfigurationError(!accepted);
    if(alive)alive->Finish(accepted);return accepted;
  },weak_factory_.GetWeakPtr()));
  const auto copy=content->copy();const auto weak=content->weak();
  auto dialog=std::make_unique<SceneModalDialog>(std::move(content),false);dialog->SetTitle(base::UTF8ToUTF16(std::string(copy.title)));dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,base::UTF8ToUTF16(std::string(copy.accept)));dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,base::UTF8ToUTF16(std::string(copy.cancel)));
  dialog->SetAcceptCallbackWithClose(base::BindRepeating([](base::WeakPtr<WorkspaceNoticeView> notice){return !notice||notice->Accept();},weak));dialog->SetCancelCallback(base::BindOnce([](base::WeakPtr<WorkspaceNoticeView> notice){if(notice)notice->Cancel();},weak));if(copy.default_cancel)dialog->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));
  remove_workspace_dialog_delegate_=std::move(dialog);auto* prompt=views::DialogDelegate::CreateDialogWidget(remove_workspace_dialog_delegate_.get(),widget_->GetNativeWindow(),widget_->GetNativeView());
  prompt->SetColorModeOverride(widget_->color_mode_override());InstallWorkspaceKeyboardRouting(prompt,true);PreparePanelText(prompt->GetRootView());
  remove_workspace_dialog_.reset(prompt);
  prompt->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,views::Widget::ClosedReason){
    if(shell){shell->remove_workspace_dialog_.reset();shell->remove_workspace_dialog_delegate_.reset();}
  },weak_factory_.GetWeakPtr()));
  prompt->Show();return true;
#else
  return false;
#endif
}
views::View* WorkspaceShell::workspace_notice_for_testing() const {
#if BUILDFLAG(IS_MAC)
  return remove_workspace_dialog_?FindWorkspaceNotice(remove_workspace_dialog_->GetContentsView()):nullptr;
#else
  return nullptr;
#endif
}
bool WorkspaceShell::workspace_notice_metal_visible() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<WorkspaceNoticeView*>(workspace_notice_for_testing());return notice&&notice->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::workspace_notice_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<WorkspaceNoticeView*>(workspace_notice_for_testing());return notice&&notice->contains_scene_text(text);
#else
  return false;
#endif
}
std::u16string WorkspaceShell::workspace_notice_error_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<WorkspaceNoticeView*>(workspace_notice_for_testing());return notice?notice->error_text():std::u16string();
#else
  return {};
#endif
}
size_t WorkspaceShell::workspace_notice_line_count_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* notice=static_cast<WorkspaceNoticeView*>(workspace_notice_for_testing());return notice?notice->body_line_count():0;
#else
  return 0;
#endif
}
void WorkspaceShell::ConfirmRemoveWorkspace(std::string id) {
  if (!widget_ || !state_.configuration_ready || state_.profiles_busy || state_.workspaces.size() < 2) return;
  if (remove_workspace_dialog_) { remove_workspace_dialog_->Activate(); return; }
  const auto target = std::ranges::find(state_.workspaces, id, &Workspace::id);
  if (target == state_.workspaces.end()) return;
  if(ShowRustWorkspaceNotice(id,target->name))return;
  auto model = ui::DialogModel::Builder()
      .SetTitle(u"Usunąć przestrzeń „" + base::UTF8ToUTF16(target->name) + u"”?")
      .AddParagraph(ui::DialogModelLabel(u"Przestrzeń, jej karty i przypięte elementy zostaną usunięte. Tej operacji nie można cofnąć.\n\nProfil przeglądania, hasła, historia i cookies pozostaną zachowane."))
      .AddOkButton(base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, std::string id) {
        if (!shell || !shell->state_.configuration_ready || shell->state_.profiles_busy || shell->state_.workspaces.size() < 2) return;
        // The target is captured when opening the prompt, independently of selection.
        if (std::ranges::none_of(shell->state_.workspaces, [&](const auto& w) { return w.id == id; })) return;
        shell->SetConfigurationError(!shell->callbacks_.remove_workspace.Run(id));
      }, weak_factory_.GetWeakPtr(), id), ui::DialogModel::Button::Params().SetLabel(u"Usuń przestrzeń"))
      .AddCancelButton(base::DoNothing(), ui::DialogModel::Button::Params().SetLabel(u"Anuluj"))
      .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
      .Build();
  auto dialog = views::BubbleDialogModelHost::CreateModal(std::move(model), ui::mojom::ModalType::kWindow);
  dialog->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* prompt = views::DialogDelegate::CreateDialogWidget(std::move(dialog), widget_->GetNativeWindow(), widget_->GetNativeView());
  prompt->SetColorModeOverride(widget_->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(prompt,true);
#endif
  PreparePanelText(prompt->GetRootView());
  remove_workspace_dialog_.reset(prompt);
  prompt->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<WorkspaceShell> shell,views::Widget::ClosedReason){
    if(shell){shell->remove_workspace_dialog_.reset();shell->remove_workspace_dialog_delegate_.reset();}
  },weak_factory_.GetWeakPtr()));
  prompt->Show();
}

void WorkspaceShell::SubmitWorkspace(bool add) {
#if BUILDFLAG(IS_MAC)
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
#endif
  std::string name;
  base::TrimWhitespaceASCII(base::UTF16ToUTF8(workspace_input_->GetText()), base::TRIM_ALL, &name);
  if (name.empty()) {
    SetPanelText(configuration_error_,u"Podaj nazwę przestrzeni.");
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
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(import_browser_picker_)){scene->SelectForTesting(index);return;}
#endif
  auto* native=static_cast<views::Combobox*>(import_browser_picker_.get());if(index>=native->GetModel()->GetItemCount())return;native->SetSelectedIndex(index);ImportSelectionChanged();
}
bool WorkspaceShell::import_source_metal_visible() const {
#if BUILDFLAG(IS_MAC)
  auto* source=views::AsViewClass<SceneSelect>(import_browser_picker_);return source&&source->metal_visible();
#else
  return false;
#endif
}
void WorkspaceShell::SetImportOptionCheckedForTesting(size_t option,bool checked) {
  const std::array<views::View*,4> controls={import_profiles_,import_bookmarks_,import_history_,import_cookies_};if(option<controls.size()&&controls[option])SetCheckValue(controls[option],checked);
}
bool WorkspaceShell::import_close_metal_visible_for_testing() const {
#if BUILDFLAG(IS_MAC)
  auto* group=dismiss_import_?views::AsViewClass<ScenePreferenceControls>(dismiss_import_->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::import_choices_metal_visible() const {
#if BUILDFLAG(IS_MAC)
  auto* group=import_profiles_?views::AsViewClass<SceneCheckboxGroup>(import_profiles_->parent()):nullptr;return group&&group->metal_visible();
#else
  return false;
#endif
}
void WorkspaceShell::BeginImportPreview(const BrowserImportOptions& options) {
#if BUILDFLAG(IS_MAC)
  if(import_form_){import_preview_timer_.Stop();import_form_->form->begin_preview(ImportUiSelection(options));CancelSceneActions(import_root_);}
#endif
}
void WorkspaceShell::RefreshRustImportControls() {
#if BUILDFLAG(IS_MAC)
  if(!import_form_||!import_status_)return;const auto v=import_form_->form->view(state_.configuration_ready,state_.profiles_busy);
  SetPanelText(import_status_,base::UTF8ToUTF16(std::string(v.message)));import_status_->SetVisible(!v.message.empty());SetCheckText(import_bookmarks_,base::UTF8ToUTF16(std::string(v.bookmarks_label)));
  import_browser_picker_->SetEnabled(v.options_enabled);for(auto* control:{import_profiles_.get(),import_bookmarks_.get(),import_history_.get(),import_cookies_.get()})control->SetEnabled(v.options_enabled);
  dismiss_import_->SetEnabled(v.close_enabled);apply_import_->SetEnabled(v.apply_enabled);import_applying_=v.applying;import_busy_=v.busy;import_can_apply_=v.can_apply;
#endif
}
void WorkspaceShell::DispatchImportPreview(uint64_t epoch) {
#if BUILDFLAG(IS_MAC)
  if(!import_form_)return;const auto request=import_form_->form->take_scan(epoch,import_visible(),ImportUiSelection(import_options()),state_.configuration_ready,state_.profiles_busy);
  RefreshRustImportControls();PositionPanels();
  if(request.action==sidebar::ImportAction::Rescan){ImportSelectionChanged();return;}
  if(request.action==sidebar::ImportAction::Preview)callbacks_.preview_import.Run(ImportServiceOptions(request.selection));
#endif
}
void WorkspaceShell::SubmitImport() {
#if BUILDFLAG(IS_MAC)
  if(import_form_){const auto request=import_form_->form->request_apply(ImportUiSelection(import_options()),state_.configuration_ready,state_.profiles_busy,import_visible());
    if(request.action==sidebar::ImportAction::Rescan){ImportSelectionChanged();return;}
    RefreshRustImportControls();PositionPanels();if(request.action!=sidebar::ImportAction::Apply)return;
    const auto alive=weak_factory_.GetWeakPtr();const bool accepted=callbacks_.apply_import.Run();if(!alive||!import_form_)return;import_form_->form->finish_apply(accepted);RefreshRustImportControls();PositionPanels();return;
  }
#endif
  callbacks_.apply_import.Run();
}
bool WorkspaceShell::import_panel_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  if(!import_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* source=views::AsViewClass<SceneSelect>(view))if(source->contains_text(text))return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;if(auto* choices=views::AsViewClass<SceneCheckboxGroup>(view))if(choices->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,import_root_);
#else
  return false;
#endif
}
void WorkspaceShell::ImportSelectionChanged() {
#if BUILDFLAG(IS_MAC)
  if(import_form_){import_preview_timer_.Stop();const auto request=import_form_->form->select(ImportUiSelection(import_options()));RefreshRustImportControls();PositionPanels();if(request.action!=sidebar::ImportAction::Preview)return;
    CancelSceneActions(import_root_);CancelSceneCheckboxes(import_root_);callbacks_.invalidate_import.Run();
    import_preview_timer_.Start(FROM_HERE,base::Milliseconds(200),base::BindOnce(&WorkspaceShell::DispatchImportPreview,weak_factory_.GetWeakPtr(),request.epoch));return;
  }
#endif
  if (import_applying_) return;
  const bool arc = import_options().browser == ImportBrowser::kArc;
  SetCheckText(import_bookmarks_,arc ? u"Przypięte karty, foldery i Ulubione" : u"Zakładki i foldery");
  callbacks_.invalidate_import.Run();
  SetImportStatus(u"Odczytywanie danych…", false, true);
  import_preview_timer_.Start(FROM_HERE, base::Milliseconds(200), base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell) {
        if (shell && shell->import_visible()) shell->callbacks_.preview_import.Run(shell->import_options());
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::SetImportStatus(std::u16string message, bool can_import, bool busy, bool applying) {
  import_preview_timer_.Stop();if(!import_status_)return;
#if BUILDFLAG(IS_MAC)
  if(import_form_){import_form_->form->set_status(base::UTF16ToUTF8(message),can_import,busy,applying);RefreshRustImportControls();PositionPanels();return;}
#endif
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
std::optional<size_t> WorkspaceShell::profile_picker_selection(bool managed) const {return PickerIndex(managed?managed_profile_picker_.get():profile_picker_.get());}
size_t WorkspaceShell::profile_picker_count() const {return PickerCount(profile_picker_);}
void WorkspaceShell::SetProfilePickerSelectionForTesting(bool managed,size_t index){auto* picker=managed?managed_profile_picker_.get():profile_picker_.get();if(picker&&index<PickerCount(picker))SetPickerIndex(picker,index);}
void WorkspaceShell::ActivateProfilePickerSelectionForTesting(bool managed,size_t index){
  auto* picker=managed?managed_profile_picker_.get():profile_picker_.get();if(!picker||index>=PickerCount(picker)||!SceneIsVisible(picker)||!picker->GetEnabledInViewsSubtree())return;
#if BUILDFLAG(IS_MAC)
  if(auto* scene=views::AsViewClass<SceneSelect>(picker)){scene->SelectForTesting(index);return;}
#endif
  SetPickerIndex(picker,index);if(managed)ManagedProfileSelectionChanged();
}
bool WorkspaceShell::profile_picker_metal_visible(bool managed) const {
#if BUILDFLAG(IS_MAC)
  auto* scene=views::AsViewClass<SceneSelect>(managed?managed_profile_picker_.get():profile_picker_.get());return scene&&scene->metal_visible();
#else
  return false;
#endif
}
void WorkspaceShell::ManagedProfileSelectionChanged(){
  confirm_profile_deletion_=false;new_profile_workspace_id_.clear();const auto selected=PickerIndex(managed_profile_picker_);
  if(selected&&*selected<state_.profiles.size())profile_name_input_->SetText(base::UTF8ToUTF16(state_.profiles[*selected].name));
#if BUILDFLAG(IS_MAC)
  if(profile_form_){CancelSceneActions(profiles_root_);const auto id=selected&&*selected<state_.profiles.size()?state_.profiles[*selected].id:std::string();profile_form_->form->select(id);}
#endif
  SetProfileStatus(u"");RefreshProfileControls();
}
void WorkspaceShell::OpenProfiles(std::string profile_id, std::string workspace_id) {
  if (!profiles_widget_) return;
#if BUILDFLAG(IS_MAC)
  if(profile_form_)CancelSceneActions(profiles_root_);
#endif
  CloseOverlays();
  confirm_profile_deletion_ = false;
  new_profile_workspace_id_ = std::move(workspace_id);
  for (size_t i = 0; i < state_.profiles.size(); ++i)
    if (state_.profiles[i].id == profile_id) SetPickerIndex(managed_profile_picker_,i);
  profile_name_input_->SetText(u"");
  for (const auto& profile : state_.profiles)
    if (profile.id == profile_id) profile_name_input_->SetText(base::UTF8ToUTF16(profile.name));
#if BUILDFLAG(IS_MAC)
  if(profile_form_){const auto selected=PickerIndex(managed_profile_picker_);const auto id=selected&&*selected<state_.profiles.size()?state_.profiles[*selected].id:std::string();profile_form_->form->open(id,new_profile_workspace_id_);}
#endif
  RefreshProfileControls();
  PositionPanels();
  profiles_widget_->Show();
  profiles_widget_->Activate();
}
void WorkspaceShell::SetProfileStatus(std::u16string message) {
  if(!profile_status_)return;
#if BUILDFLAG(IS_MAC)
  if(profile_form_)profile_form_->form->set_status(base::UTF16ToUTF8(message));
  if(auto* text=views::AsViewClass<SceneParagraph>(profile_status_))text->SetText(message);
  else
#endif
    static_cast<views::Label*>(profile_status_.get())->SetText(message);
  profile_status_->SetVisible(!message.empty());PositionPanels();
}
void WorkspaceShell::RefreshProfileControls() {
#if BUILDFLAG(IS_MAC)
  if(profile_form_){const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
    const auto controls=profile_form_->form->controls(state_.configuration_ready,state_.profiles_busy,rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));
    create_profile_->SetEnabled(controls.create);rename_profile_->SetEnabled(controls.rename);delete_profile_->SetEnabled(controls.remove);managed_profile_picker_->SetEnabled(controls.picker);
    SetActionText(delete_profile_,base::UTF8ToUTF16(std::string(controls.delete_label)));SetProfileStatus(base::UTF8ToUTF16(std::string(controls.status)));return;
  }
#endif
  const bool ready = state_.configuration_ready && !state_.profiles_busy;
  create_profile_->SetEnabled(ready);
  rename_profile_->SetEnabled(ready && new_profile_workspace_id_.empty());
  managed_profile_picker_->SetEnabled(ready);
  auto index = PickerIndex(managed_profile_picker_);
  bool used = false;
  std::u16string names;
  if (index && *index < state_.profiles.size()) {
    const auto& profile = state_.profiles[*index];
    for (const auto& workspace : state_.workspaces) {
      if (workspace.profile_id != profile.id) continue;
      used = true;
      if (!names.empty()) names += u", ";
      names += base::UTF8ToUTF16(workspace.name);
    }
  }
  delete_profile_->SetEnabled(ready && index.has_value() && !used && state_.profiles.size() > 1);
  SetActionText(delete_profile_,
      confirm_profile_deletion_ ? u"Potwierdź usunięcie profilu i danych" : u"Usuń profil i jego dane");
  if (used && !state_.profiles_busy) SetProfileStatus(u"Profil jest używany przez: " + names + u". Najpierw zmień profil tych przestrzeni.");
}
bool WorkspaceShell::SubmitRustProfileAction(unsigned action) {
#if BUILDFLAG(IS_MAC)
  if(!profile_form_)return false;
  if(profile_name_input_->IsIMEComposing())return true;
  const auto index=PickerIndex(managed_profile_picker_);const auto selected=index&&*index<state_.profiles.size()?state_.profiles[*index].id:std::string();
  if(selected!=std::string(profile_form_->form->selection()))profile_form_->form->select(selected);
  const auto profiles=ProfileOptions(state_);const auto uses=ProfileUses(state_);
  const auto request=profile_form_->form->request(action,base::UTF16ToUTF8(profile_name_input_->GetText()),state_.configuration_ready,state_.profiles_busy,rust::Slice<const sidebar::ProfileOption>(profiles.data(),profiles.size()),rust::Slice<const sidebar::ProfileUse>(uses.data(),uses.size()));
  if(request.action==sidebar::ProfileAction::None){const auto status=base::UTF8ToUTF16(std::string(profile_form_->form->status()));RefreshProfileControls();SetProfileStatus(status);return true;}
  if(request.action==sidebar::ProfileAction::ConfirmDelete){RefreshProfileControls();return true;}
  const auto alive=weak_factory_.GetWeakPtr();bool accepted=false;
  switch(request.action){
    case sidebar::ProfileAction::Create:accepted=callbacks_.create_profile.Run(std::string(request.name),std::string(request.workspace));break;
    case sidebar::ProfileAction::Rename:accepted=callbacks_.rename_profile.Run(std::string(request.id),std::string(request.name));break;
    case sidebar::ProfileAction::Delete:accepted=callbacks_.remove_profile.Run(std::string(request.id));break;
    default:break;
  }
  if(!alive||!profile_form_)return true;profile_form_->form->finish_attempt(accepted);
  if(accepted&&request.action==sidebar::ProfileAction::Create)profile_name_input_->SetText(u"");
  const auto status=base::UTF8ToUTF16(std::string(profile_form_->form->status()));RefreshProfileControls();SetProfileStatus(status);return true;
#else
  return false;
#endif
}
bool WorkspaceShell::profile_panel_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  if(!profiles_root_)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* picker=views::AsViewClass<SceneSelect>(view))if(picker->contains_text(text))return true;if(auto* node=views::AsViewClass<SceneParagraph>(view))if(node->contains_scene_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,profiles_root_);
#else
  return false;
#endif
}
void WorkspaceShell::SubmitProfile() {
  if(SubmitRustProfileAction(0))return;
  auto name = base::UTF16ToUTF8(base::TrimWhitespace(profile_name_input_->GetText(), base::TRIM_ALL));
  if (name.empty()) { SetProfileStatus(u"Podaj nazwę profilu."); return; }
  if (callbacks_.create_profile.Run(name, new_profile_workspace_id_)) profile_name_input_->SetText(u"");
}
void WorkspaceShell::RenameSelectedProfile() {
  if(SubmitRustProfileAction(1))return;
  const auto index = PickerIndex(managed_profile_picker_);
  if (!index || *index >= state_.profiles.size()) return;
  const auto name = base::UTF16ToUTF8(base::TrimWhitespace(profile_name_input_->GetText(), base::TRIM_ALL));
  if (name.empty()) { SetProfileStatus(u"Podaj nazwę profilu."); return; }
  callbacks_.rename_profile.Run(state_.profiles[*index].id, name);
}
void WorkspaceShell::DeleteSelectedProfile() {
  if(SubmitRustProfileAction(2))return;
  const auto index = PickerIndex(managed_profile_picker_);
  if (!index || *index >= state_.profiles.size()) return;
  if (!confirm_profile_deletion_) {
    confirm_profile_deletion_ = true;
    SetProfileStatus(u"Usunięte zostaną logowania, cookies i dane stron tego profilu. Tej operacji nie można cofnąć.");
    RefreshProfileControls();
    return;
  }
  confirm_profile_deletion_ = false;
  callbacks_.remove_profile.Run(state_.profiles[*index].id);
}
bool WorkspaceShell::appearance_metal_visible(bool emoji) const {
#if BUILDFLAG(IS_MAC)
  auto* field=emoji?emoji_input_.get():color_input_.get();auto* row=field?views::AsViewClass<AppearanceRow>(field->view()->parent()):nullptr;return row&&row->metal_visible();
#else
  return false;
#endif
}
bool WorkspaceShell::appearance_contains_scene_text(std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  auto* row=emoji_input_?views::AsViewClass<AppearanceRow>(emoji_input_->view()->parent()):nullptr;return row&&row->contains_text(text);
#else
  return false;
#endif
}
gfx::Rect WorkspaceShell::appearance_scene_bounds_for_testing(bool emoji) const {
#if BUILDFLAG(IS_MAC)
  auto* field=emoji?emoji_input_.get():color_input_.get();auto* row=field?views::AsViewClass<AppearanceRow>(field->view()->parent()):nullptr;return row?row->scene_bounds():gfx::Rect();
#else
  return {};
#endif
}
bool WorkspaceShell::form_actions_contains_scene_text(bool profiles,std::string_view text) const {
#if BUILDFLAG(IS_MAC)
  auto* root=profiles?profiles_root_.get():settings_root_.get();if(!root)return false;auto find=[&](auto&& self,views::View* view)->bool{if(auto* group=views::AsViewClass<SceneActionGroup>(view))if(group->contains_text(text))return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,root);
#else
  return false;
#endif
}
size_t WorkspaceShell::form_action_canvases_visible_for_testing(bool profiles) const {
#if BUILDFLAG(IS_MAC)
  auto* root=profiles?profiles_root_.get():settings_root_.get();if(!root)return 0;auto count=[&](auto&& self,views::View* view)->size_t{size_t n=0;if(auto* group=views::AsViewClass<SceneActionGroup>(view))n+=group->metal_visible();for(const auto& child:view->children())n+=self(self,child.get());return n;};return count(count,root);
#else
  return 0;
#endif
}
bool WorkspaceShell::HandleAppearanceEditorKey(bool emoji,const ui::KeyEvent& event) {
  auto* field=emoji?emoji_input_.get():color_input_.get();
  if(!field||event.type()!=ui::EventType::kKeyPressed||field->IsIMEComposing())return false;
  if(event.key_code()==ui::VKEY_ESCAPE){CloseOverlays();RestorePageFocus();return true;}
  if(event.key_code()!=ui::VKEY_RETURN)return false;
  if(emoji)SetWorkspaceEmoji(state_.workspace_id,std::u16string(field->GetText()));else ApplyHexColor();
  return true;
}
void WorkspaceShell::ApplyHexColor() {
  if (auto color = ParseColor(base::UTF16ToUTF8(color_input_->GetText())))
    SetWorkspaceColor(state_.workspace_id, *color);
  else ShowWorkspaceAppearanceError(3);
}
void WorkspaceShell::SetPaletteErrorText(std::u16string text) {
  if(!address_error_)return;
#if BUILDFLAG(IS_MAC)
  if(palette_root_&&static_cast<PaletteView*>(palette_root_.get())->scene()){static_cast<PaletteText*>(address_error_.get())->SetText(std::move(text));return;}
#endif
  static_cast<views::Label*>(address_error_.get())->SetText(text);
}
void WorkspaceShell::ShowAddressError() {
  SetPaletteErrorText(u"Wpisz adres strony HTTP lub HTTPS");
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
#if BUILDFLAG(IS_MAC)
    if(tools_form_){rust::Vec<uint16_t> units;for(char16_t unit:query)units.push_back(unit);tools_form_->form->tools_history_edit(std::move(units));}else
#endif
    {history_query_=query;history_loaded_=false;}
    history_entries_.clear();
    if(history_list_)history_list_->RemoveAllChildViews();
    SetPanelText(tools_status_,u"Wyszukiwanie historii…");
    if(history_more_)history_more_->SetEnabled(false);
    history_timer_.Start(FROM_HERE,base::Milliseconds(150),base::BindOnce(&WorkspaceShell::RefreshHistory,weak_factory_.GetWeakPtr(),false));
  }else if(sender==timeline_filter_){
    const auto query=base::i18n::ToLower(sender->GetText());
#if BUILDFLAG(IS_MAC)
    if(tools_form_){rust::Vec<uint16_t> units;for(char16_t unit:query)units.push_back(unit);tools_form_->form->tools_timeline_edit(std::move(units));}else
#endif
    timeline_query_=query;
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
  search_timer_.Start(FROM_HERE, base::Milliseconds(120), base::BindOnce(
      [](base::WeakPtr<WorkspaceShell> shell, std::u16string query) {
        if (shell && shell->address_visible()&&!shell->address_->IsIMEComposing()&&base::TrimWhitespace(shell->address_->GetText(),base::TRIM_ALL)==query) shell->callbacks_.search.Run(query);
      }, weak_factory_.GetWeakPtr(), search_query_));
}
void WorkspaceShell::SetSearchResults(std::u16string query, std::string profile_id, std::vector<PaletteResult> results) {
  if (!address_visible() || address_->IsIMEComposing() || profile_id != state_.profile_id ||
      base::TrimWhitespace(address_->GetText(), base::TRIM_ALL) != query) return;
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
      SetPaletteErrorText(u"Brak pasujących poleceń."); address_error_->SetVisible(true); PositionPanels();
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
#if BUILDFLAG(IS_MAC)
    if(metal_sidebar_)hide=sidebar::sidebar_auto_hide(pinned_,settings_visible(),false,false);
#endif
    if(hide)sidebar_widget_->Hide();
  }
}
void WorkspaceShell::OnWidgetVisibilityChanged(views::Widget* widget, bool visible) {
  if(widget==address_widget_.get())ScheduleMetalPalette();
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
