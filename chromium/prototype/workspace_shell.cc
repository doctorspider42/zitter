#include "chromium/prototype/workspace_shell.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/i18n/case_conversion.h"
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
#include "components/url_formatter/url_fixer.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider_key.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
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
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

namespace zitter::prototype {
namespace {
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
constexpr int kGap = 8;
#if BUILDFLAG(IS_MAC)
constexpr int kTrafficLightInset = 78;
#else
constexpr int kTrafficLightInset = 8;
#endif
constexpr std::array<SkColor, 7> kMonogramColors = {
    0xFF4F46E5, 0xFF2F6F5E, 0xFFB4532A, 0xFF3A6EA5,
    0xFF7A5C2E, 0xFF6B4FA0, 0xFF2E7D4F};

SkColor Hairline() { return SkColorSetA(kInk, 0x17); }
SkColor HoverWash() { return SkColorSetA(kInk, 0x0D); }
SkColor Mix(SkColor a, SkColor b, int percent_a) {
  auto channel = [&](U8CPU x, U8CPU y) { return (x * percent_a + y * (100 - percent_a)) / 100; };
  return SkColorSetRGB(channel(SkColorGetR(a), SkColorGetR(b)),
                       channel(SkColorGetG(a), SkColorGetG(b)),
                       channel(SkColorGetB(a), SkColorGetB(b)));
}
// The workspace accent tints the whole window frame, Arc-style.
SkColor FrameTint(SkColor accent) { return Mix(accent, kFrameBase, 14); }

int ColorIndex(std::string_view id) { return id == "personal" ? 1 : 0; }
std::string Hex(SkColor color) { return base::StringPrintf("#%06X", color & 0xFFFFFF); }
std::optional<SkColor> ParseColor(std::string_view text) {
  unsigned value;
  if (text.size() != 7 || text[0] != '#' ||
      !base::HexStringToUInt(text.substr(1), &value)) return std::nullopt;
  return value | 0xFF000000;
}
struct Appearance {
  std::array<SkColor, 2> colors = {0xFF9E91DC, 0xFF84B6A4};
  // Tabs live in a visible sidebar by default; auto-hide is opt-in.
  bool pinned = true;
};
Appearance ReadAppearance(const base::FilePath& path) {
  Appearance result;
  std::string json;
  if (!base::ReadFileToString(path, &json)) return result;
  auto dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) return result;
  for (auto id : {"work", "personal"}) {
    if (const auto* value = dict->FindString(id)) {
      if (auto color = ParseColor(*value)) result.colors[ColorIndex(id)] = *color;
    }
  }
  result.pinned = dict->FindBool("sidebar_pinned").value_or(true);
  return result;
}
bool SavePatch(const base::FilePath& path, std::optional<int> index,
               SkColor color, std::optional<bool> pinned) {
  auto appearance = ReadAppearance(path);
  if (index) appearance.colors[*index] = color;
  if (pinned) appearance.pinned = *pinned;
  base::DictValue dict;
  dict.Set("work", Hex(appearance.colors[0]));
  dict.Set("personal", Hex(appearance.colors[1]));
  dict.Set("sidebar_pinned", appearance.pinned);
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
views::Label* Label(views::View* parent, std::u16string text, SkColor color,
                    gfx::FontList font = gfx::FontList()) {
  auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
  label->SetEnabledColor(color);
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

// 32×32 icon button with a soft hover wash, used for chrome-level commands.
class GlyphButton final : public views::Button {
  METADATA_HEADER(GlyphButton, views::Button)
 public:
  enum class Glyph { kVector, kSidebar, kDot, kSwatch };
  GlyphButton(PressedCallback callback, std::u16string name, Glyph glyph,
              const gfx::VectorIcon* icon = nullptr)
      : views::Button(std::move(callback)), glyph_(glyph), icon_(icon) {
    SetTooltipText(name);
    GetViewAccessibility().SetName(name);
  }
  void set_color(SkColor color) { color_ = color; SchedulePaint(); }
  void set_on(bool on) { on_ = on; SchedulePaint(); }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return glyph_ == Glyph::kDot ? gfx::Size(24, 28) : gfx::Size(32, 32);
  }
  void StateChanged(ButtonState) override { SchedulePaint(); }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto bounds = GetLocalBounds();
    if (glyph_ != Glyph::kDot && glyph_ != Glyph::kSwatch && GetEnabled() &&
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)) {
      FillRound(canvas, bounds, 8, HoverWash());
    }
    const SkColor ink = GetEnabled() ? color_ : kDisabled;
    const auto center = bounds.CenterPoint();
    switch (glyph_) {
      case Glyph::kVector:
        PaintIcon(canvas, *icon_, bounds, 16, ink);
        break;
      case Glyph::kSidebar: {
        const gfx::Rect frame(center.x() - 8, center.y() - 7, 16, 14);
        StrokeRound(canvas, frame, 3, ink);
        canvas->FillRect(gfx::Rect(frame.x() + 5, frame.y(), 1, frame.height()), ink);
        if (on_) canvas->FillRect(gfx::Rect(frame.x() + 1, frame.y() + 1, 4, frame.height() - 2),
                                  SkColorSetA(ink, 0x55));
        break;
      }
      case Glyph::kDot: {
        const gfx::Rect dot(center.x() - 4, center.y() - 4, 8, 8);
        if (on_) FillRound(canvas, dot, 4, kInk);
        else StrokeRound(canvas, dot, 4, GetState() == STATE_HOVERED ? kInk : kMuted);
        break;
      }
      case Glyph::kSwatch: {
        const gfx::Rect ring(center.x() - 10, center.y() - 10, 20, 20);
        if (on_ || GetState() == STATE_HOVERED) StrokeRound(canvas, ring, 10, on_ ? kInk : kMuted);
        FillRound(canvas, gfx::Rect(center.x() - 7, center.y() - 7, 14, 14), 7, color_);
        break;
      }
    }
  }
 private:
  Glyph glyph_;
  raw_ptr<const gfx::VectorIcon> icon_;
  SkColor color_ = kSubtle;
  bool on_ = false;
};
BEGIN_METADATA(GlyphButton)
END_METADATA

// A sidebar tab, a "New tab" action or a palette suggestion.
class RowButton final : public views::Button {
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
    int height = 34;
    PressedCallback close;
  };
  RowButton(PressedCallback callback, Spec spec)
      : views::Button(std::move(callback)), spec_(std::move(spec)) {
    GetViewAccessibility().SetName(spec_.title);
    SetTooltipText(spec_.title);
    title_ = Label(this, spec_.title, kInk,
                   Font(0, spec_.active || spec_.chip ? gfx::Font::Weight::MEDIUM
                                                      : gfx::Font::Weight::NORMAL));
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetCanProcessEventsWithinSubtree(false);
    if (!spec_.detail.empty()) {
      detail_ = Label(this, spec_.detail, kMuted, Font(-1));
      detail_->SetElideBehavior(gfx::ELIDE_MIDDLE);
      detail_->SetCanProcessEventsWithinSubtree(false);
    }
    if (spec_.close) {
      close_ = AddChildView(std::make_unique<GlyphButton>(
          std::move(spec_.close), u"Zamknij kartę", GlyphButton::Glyph::kVector,
          &vector_icons::kCloseSmallIcon));
      close_->SetVisible(spec_.active);
    }
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(200, spec_.height);
  }
  void StateChanged(ButtonState) override {
    if (close_) close_->SetVisible(spec_.active || GetState() == STATE_HOVERED);
    SchedulePaint();
  }
  void Layout(PassKey) override {
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
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto bounds = GetLocalBounds();
    if (spec_.active) {
      FillRound(canvas, bounds, 8, kCard);
      StrokeRound(canvas, bounds, 8, SkColorSetA(kInk, 0x0F));
    } else if (spec_.highlighted) {
      FillRound(canvas, bounds, 9, kAccentWash);
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      FillRound(canvas, bounds, 8, HoverWash());
    }
    if (spec_.chip) {
      const gfx::Rect chip(12, (height() - 28) / 2, 28, 28);
      FillRound(canvas, chip, 7, kChip);
      if (spec_.icon) PaintIcon(canvas, *spec_.icon, chip, 15, kSubtle);
    } else if (spec_.icon) {
      PaintIcon(canvas, *spec_.icon, gfx::Rect(10, 0, 16, height()), 16, kSubtle);
    } else {
      const gfx::Rect mono(10, (height() - 16) / 2, 16, 16);
      FillRound(canvas, mono, 4, spec_.monogram_color);
      canvas->DrawStringRectWithFlags(spec_.monogram, Font(-4, gfx::Font::Weight::SEMIBOLD),
                                      SK_ColorWHITE, mono, gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }
 private:
  Spec spec_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> detail_ = nullptr;
  raw_ptr<GlyphButton> close_ = nullptr;
};
BEGIN_METADATA(RowButton)
END_METADATA

// The address "pill" at the top of the sidebar; opens the command palette.
class UrlPill final : public views::Button {
  METADATA_HEADER(UrlPill, views::Button)
 public:
  explicit UrlPill(PressedCallback callback) : views::Button(std::move(callback)) {
    GetViewAccessibility().SetName(u"Adres strony");
    SetTooltipText(u"Szukaj lub wpisz adres");
    host_ = Label(this, u"", kInk, Font(0, gfx::Font::Weight::MEDIUM));
    path_ = Label(this, u"", kMuted);
    path_->SetElideBehavior(gfx::ELIDE_TAIL);
    host_->SetCanProcessEventsWithinSubtree(false);
    path_->SetCanProcessEventsWithinSubtree(false);
  }
  views::Label* host() const { return host_; }
  views::Label* path() const { return path_; }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(200, 40);
  }
  void StateChanged(ButtonState) override { SchedulePaint(); }
  void Layout(PassKey) override {
    const int lead = 34, available = std::max(0, width() - lead - 12);
    const int host_width = std::min(host_->GetPreferredSize().width(), available);
    host_->SetBounds(lead, 0, host_width, height());
    path_->SetBounds(lead + host_width, 0, available - host_width, height());
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto bounds = GetLocalBounds();
    FillRound(canvas, bounds, 10, SkColorSetA(kCard, GetState() == STATE_HOVERED ? 0xD9 : 0x99));
    StrokeRound(canvas, bounds, 10, SkColorSetA(kInk, 0x0D));
    PaintIcon(canvas, secure ? vector_icons::kLockIcon : vector_icons::kSearchIcon,
              gfx::Rect(10, 0, 16, height()), 14, kMuted);
  }
  bool secure = false;
 private:
  raw_ptr<views::Label> host_ = nullptr;
  raw_ptr<views::Label> path_ = nullptr;
};
BEGIN_METADATA(UrlPill)
END_METADATA

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

class SidebarView final : public views::View {
  METADATA_HEADER(SidebarView, views::View)
 public:
  explicit SidebarView(base::RepeatingClosure leave) : leave_(std::move(leave)) {
    SetNotifyEnterExitOnChild(true);
  }
  void OnMouseExited(const ui::MouseEvent&) override {
    if (!GetBoundsInScreen().Contains(display::Screen::Get()->GetCursorScreenPoint())) leave_.Run();
  }
 private:
  base::RepeatingClosure leave_;
};
BEGIN_METADATA(SidebarView)
END_METADATA

// Frame: tinted ground, header row (traffic lights + page commands) and the
// page as a rounded white card. Pinned, the sidebar column owns the header.
class ShellRoot final : public views::View {
  METADATA_HEADER(ShellRoot, views::View)
 public:
  ShellRoot() = default;
  gfx::Size GetMinimumSize() const override { return gfx::Size(560, 560); }
  void Layout(PassKey) override {
    const int column = pinned ? std::min(kSidebarWidth, width() / 2) + kGap : 0;
    const int header_width = header->GetPreferredSize().width();
    // Centered on the 28 px macOS titlebar line, next to the traffic lights.
    header->SetBounds(kTrafficLightInset, -2, header_width, 32);
    const int top = pinned ? kGap : kShellHeaderHeight;
    content->SetBounds(column + kGap, top, std::max(0, width() - column - 2 * kGap),
                       std::max(0, height() - top - kGap));
    edge->SetBounds(0, kShellHeaderHeight, kGap, std::max(0, height() - kShellHeaderHeight));
    edge->SetVisible(!pinned);
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

void RegisterKeys(views::Widget* widget, ui::AcceleratorTarget* target, bool panel = false) {
#if BUILDFLAG(IS_MAC)
  constexpr int primary = ui::EF_COMMAND_DOWN;
#else
  constexpr int primary = ui::EF_CONTROL_DOWN;
#endif
  for (auto key : {ui::VKEY_1, ui::VKEY_2})
    widget->GetFocusManager()->RegisterAccelerator(ui::Accelerator(key, primary | ui::EF_ALT_DOWN),
        ui::AcceleratorManager::kHighPriority, target);
  for (auto key : {ui::VKEY_L, ui::VKEY_T})
    widget->GetFocusManager()->RegisterAccelerator(ui::Accelerator(key, primary),
        ui::AcceleratorManager::kHighPriority, target);
  widget->GetFocusManager()->RegisterAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, 0),
      ui::AcceleratorManager::kHighPriority, target);
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget, panel);
#endif
}

// Typed text → destination, or an invalid GURL for anything but HTTP(S).
GURL ResolveTyped(const std::string& text) {
  GURL url(text);
  const bool bare_address = text.find('.') != std::string::npos ||
                            text.starts_with("localhost") ||
                            text.starts_with("[::1]");
  if ((!url.has_scheme() && !bare_address) || text.find(' ') != std::string::npos) {
    url = GURL("https://duckduckgo.com/?q=" + base::EscapeQueryParamValue(text, true));
  } else if (!url.has_scheme() || text.starts_with("localhost:")) {
    url = url_formatter::FixupURL(text);
  }
  return url.SchemeIsHTTPOrHTTPS() ? url : GURL();
}
}  // namespace

WorkspaceShell::Callbacks::Callbacks() = default;
WorkspaceShell::Callbacks::Callbacks(Callbacks&&) = default;
WorkspaceShell::Callbacks& WorkspaceShell::Callbacks::operator=(Callbacks&&) = default;
WorkspaceShell::Callbacks::~Callbacks() = default;

WorkspaceShell::WorkspaceShell(base::FilePath path, Callbacks callbacks)
    : settings_path_(std::move(path)), callbacks_(std::move(callbacks)),
      io_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  state_.workspace_id = "work";
}
WorkspaceShell::~WorkspaceShell() { Detach(); }

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
  root->SetBackground(views::CreateSolidBackground(FrameTint(colors_[0])));
  auto* header = root->AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto weak = weak_factory_.GetWeakPtr();
  header->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating(&WorkspaceShell::ToggleSidebar, weak), u"Pokaż lub ukryj karty",
      GlyphButton::Glyph::kSidebar));
  const std::array<std::pair<const gfx::VectorIcon*, std::u16string>, 3> commands = {{
      {&vector_icons::kArrowBackIcon, u"Wstecz"},
      {&vector_icons::kArrowForwardIcon, u"Dalej"},
      {&vector_icons::kReloadCustomIcon, u"Odśwież"}}};
  for (size_t i = 0; i < commands.size(); ++i) {
    nav_buttons_[i] = header->AddChildView(std::make_unique<GlyphButton>(
        base::BindRepeating(callbacks_.page_command, static_cast<PageCommand>(i)),
        commands[i].second, GlyphButton::Glyph::kVector, commands[i].first));
  }
  content_host_ = root->AddChildView(std::make_unique<views::View>());
  content_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  content_host_->SetBackground(views::CreateRoundedRectBackground(kCard, 10));
  edge_ = root->AddChildView(std::make_unique<EdgeView>(
      base::BindRepeating(&WorkspaceShell::ShowSidebar, weak)));
  root->header = header;
  root->content = content_host_;
  root->edge = edge_;
  return root;
}

void WorkspaceShell::Attach(views::Widget* widget, ui::AcceleratorTarget* target) {
  widget_ = widget;
  widget_->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kLight);
  widget_->AddObserver(this);
  RegisterKeys(widget_, target);
  auto weak = weak_factory_.GetWeakPtr();

  // Sidebar: address pill, workspace, tabs; workspace switch and color below.
  auto sidebar = std::make_unique<SidebarView>(base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell) {
        if (shell && !shell->pinned_ && !shell->settings_visible()) {
          shell->sidebar_widget_->Hide();
          shell->RestorePageFocus();
        }
      }, weak));
  sidebar_root_ = sidebar.get();
  auto* layout = sidebar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(2, 4, 8, 4), 2));
  auto* pill = sidebar->AddChildView(std::make_unique<UrlPill>(
      base::BindRepeating(&WorkspaceShell::OpenAddress, weak, false)));
  url_pill_ = pill;
  url_host_ = pill->host();
  url_path_ = pill->path();
  Spacer(sidebar.get(), 12);
  auto* heading = sidebar->AddChildView(std::make_unique<views::View>());
  heading->SetLayoutManager(std::make_unique<views::FillLayout>());
  heading->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 8)));
  heading->SetPreferredSize(gfx::Size(200, 24));
  workspace_name_ = Label(heading, u"Praca", kSubtle, Font(-1, gfx::Font::Weight::SEMIBOLD));
#if BUILDFLAG(IS_MAC)
  const auto* new_tab_hint = u"⌘T";
#else
  const auto* new_tab_hint = u"Ctrl+T";
#endif
  RowButton::Spec new_tab;
  new_tab.title = u"Nowa karta";
  new_tab.detail = new_tab_hint;
  new_tab.icon = &vector_icons::kAdd2Icon;
  sidebar->AddChildView(std::make_unique<RowButton>(
      base::BindRepeating(&WorkspaceShell::OpenAddress, weak, true), std::move(new_tab)));
  tab_list_ = sidebar->AddChildView(std::make_unique<views::View>());
  tab_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  auto* spacer = sidebar->AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(spacer, 1);

  // Workspace settings: profile and color live here, not in the sidebar.
  auto settings = std::make_unique<views::View>();
  settings_root_ = settings.get();
  settings->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 6));
  settings->SetBackground(views::CreateRoundedRectBackground(kCard, 12));
  settings->SetBorder(views::CreateRoundedRectBorder(1, 12, Hairline()));
  settings_title_ = Label(settings.get(), u"Praca", kInk, Font(2, gfx::Font::Weight::SEMIBOLD));
  profile_name_ = Label(settings.get(), u"", kMuted, Font(-1));
  Spacer(settings.get(), 6);
  Label(settings.get(), u"Kolor przestrzeni", kSubtle, Font(-1, gfx::Font::Weight::MEDIUM));
  auto* color_row = settings->AddChildView(std::make_unique<views::View>());
  auto* color_layout = color_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  color_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  for (SkColor color : {0xFF9E91DCu, 0xFF84B6A4u, 0xFFD6A07Bu, 0xFF7FAACBu}) {
    auto* swatch = color_row->AddChildView(std::make_unique<GlyphButton>(
        base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell, SkColor value) {
          if (shell) shell->SetWorkspaceColor(shell->state_.workspace_id, value);
        }, weak, color), base::UTF8ToUTF16(Hex(color)), GlyphButton::Glyph::kSwatch));
    swatch->set_color(color);
  }
  Spacer(color_row, 1)->SetPreferredSize(gfx::Size(8, 1));
  color_input_ = color_row->AddChildView(std::make_unique<views::Textfield>());
  color_input_->GetViewAccessibility().SetName(u"Kolor przestrzeni #RRGGBB");
  color_input_->SetPlaceholderText(u"#RRGGBB");
  color_input_->SetFontList(Font(-1));
  color_input_->SetBackgroundColor(kChip);
  color_input_->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(1, 7, Hairline()), gfx::Insets::VH(4, 8)));
  color_input_->set_controller(this);
  color_layout->SetFlexForView(color_input_, 1);
  color_error_ = Label(settings.get(), u"", kError, Font(-2));
  color_error_->SetVisible(false);

  auto* footer = sidebar->AddChildView(std::make_unique<views::View>());
  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  footer_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  pin_button_ = footer->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating([](base::WeakPtr<WorkspaceShell> shell) {
        if (shell) shell->SetPinned(!shell->pinned_);
      }, weak), u"Przypnij panel", GlyphButton::Glyph::kSidebar));
  footer_layout->SetFlexForView(Spacer(footer, 1), 1);
  const std::array<std::pair<const char*, std::u16string>, 2> spaces = {{
      {"work", u"Przestrzeń: Praca"}, {"personal", u"Przestrzeń: Prywatne"}}};
  for (size_t i = 0; i < spaces.size(); ++i) {
    workspace_buttons_[i] = footer->AddChildView(std::make_unique<GlyphButton>(
        base::BindRepeating(callbacks_.select_workspace, std::string(spaces[i].first)),
        spaces[i].second, GlyphButton::Glyph::kDot));
  }
  footer_layout->SetFlexForView(Spacer(footer, 1), 1);
  footer->AddChildView(std::make_unique<GlyphButton>(
      base::BindRepeating(&WorkspaceShell::OpenWorkspaceSettings, weak),
      u"Ustawienia przestrzeni", GlyphButton::Glyph::kVector, &vector_icons::kSettingsIcon));

  // Command palette: input, suggestions, keyboard hints.
  auto palette = std::make_unique<views::View>();
  palette_root_ = palette.get();
  palette->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  palette->SetBackground(views::CreateRoundedRectBackground(kCard, 14));
  palette->SetBorder(views::CreateRoundedRectBorder(1, 14, Hairline()));
  auto* input_row = palette->AddChildView(std::make_unique<views::View>());
  auto* input_layout = input_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 18), 12));
  input_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  input_row->SetPreferredSize(gfx::Size(560, 56));
  auto* search_icon = input_row->AddChildView(std::make_unique<GlyphButton>(
      views::Button::PressedCallback(), u"Szukaj", GlyphButton::Glyph::kVector,
      &vector_icons::kSearchIcon));
  search_icon->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  search_icon->SetCanProcessEventsWithinSubtree(false);
  address_ = input_row->AddChildView(std::make_unique<views::Textfield>());
  address_->GetViewAccessibility().SetName(u"Szukaj lub wpisz adres");
  address_->SetPlaceholderText(u"Szukaj lub wpisz adres…");
  address_->SetFontList(Font(3));
  address_->SetBackgroundColor(kCard);
  address_->SetBorder(views::CreateEmptyBorder(0));
  address_->set_controller(this);
  input_layout->SetFlexForView(address_, 1);
  Label(input_row, u"esc", kMuted, Font(-2))
      ->SetBorder(views::CreateRoundedRectBorder(1, 5, Hairline()));
  address_error_ = Label(palette.get(), u"", kError, Font(-1));
  address_error_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 18)));
  address_error_->SetVisible(false);
  suggestion_list_ = palette->AddChildView(std::make_unique<views::View>());
  suggestion_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 2));
  suggestion_list_->SetBorder(views::CreateSolidSidedBorder(gfx::Insets::TLBR(1, 0, 0, 0), Hairline()));
#if BUILDFLAG(IS_MAC)
  auto* hints = Label(palette.get(), u"↑↓ wybierz     ↵ otwórz     ⌘T nowa karta", kMuted, Font(-2));
#else
  auto* hints = Label(palette.get(), u"↑↓ wybierz     ↵ otwórz     Ctrl+T nowa karta", kMuted, Font(-2));
#endif
  hints->SetBorder(views::CreatePaddedBorder(
      views::CreateSolidSidedBorder(gfx::Insets::TLBR(1, 0, 0, 0), Hairline()),
      gfx::Insets::TLBR(10, 18, 10, 18)));

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
    panel->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kLight);
    panel->AddObserver(this);
    RegisterKeys(panel.get(), target, true);
  };
  make_panel(std::move(sidebar), sidebar_delegate_, sidebar_widget_, u"Zitter — Karty", false);
  make_panel(std::move(palette), address_delegate_, address_widget_, u"Zitter — Paleta poleceń");
  make_panel(std::move(settings), settings_delegate_, settings_widget_, u"Zitter — Ustawienia przestrzeni");
  PositionPanels();
  ApplyAppearance();
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(ReadAppearance, settings_path_),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, Appearance appearance) {
        if (!shell) return;
        for (int i = 0; i < 2; ++i)
          if (!shell->color_dirty_[i]) shell->colors_[i] = appearance.colors[i];
        if (!shell->pin_dirty_) shell->pinned_ = appearance.pinned;
        shell->appearance_ready_ = true;
        shell->ApplyAppearance();
        // The pinned sidebar already appeared with the window; reorder panels
        // only if the saved state differs, never as a side effect of loading.
        if (shell->pinned_ && !shell->sidebar_visible()) shell->ShowSidebar();
        if (!shell->pinned_ && shell->sidebar_visible()) shell->sidebar_widget_->Hide();
      }, weak));
}

void WorkspaceShell::Detach() {
  weak_factory_.InvalidateWeakPtrs();
  if (!widget_) return;
  widget_->RemoveObserver(this);
  for (auto* panel : {sidebar_widget_.get(), address_widget_.get(), settings_widget_.get()})
    panel->RemoveObserver(this);
  address_->set_controller(nullptr);
  color_input_->set_controller(nullptr);
  address_ = nullptr;
  color_input_ = nullptr;
  sidebar_root_ = nullptr;
  palette_root_ = nullptr;
  tab_list_ = nullptr;
  suggestion_list_ = nullptr;
  url_pill_ = nullptr;
  url_host_ = nullptr;
  url_path_ = nullptr;
  workspace_name_ = nullptr;
  profile_name_ = nullptr;
  settings_title_ = nullptr;
  settings_root_ = nullptr;
  pin_button_ = nullptr;
  nav_buttons_.fill(nullptr);
  workspace_buttons_.fill(nullptr);
  address_error_ = nullptr;
  color_error_ = nullptr;
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
  state_ = std::move(state);
  if (!widget_) return;
  workspace_name_->SetText(state_.workspace_name);
  settings_title_->SetText(state_.workspace_name);
  profile_name_->SetText(u"Chromium: " + state_.profile_name);
  const bool blank = !state_.url.is_valid() || state_.url.path() == "/start.html";
  url_host_->SetText(blank ? u"Szukaj lub wpisz adres" : HostLabel(state_.url));
  url_host_->SetEnabledColor(blank ? kMuted : kInk);
  const std::string path = state_.url.path() == "/" ? std::string() : std::string(state_.url.path());
  url_path_->SetText(blank ? u"" : base::UTF8ToUTF16(path));
  static_cast<UrlPill*>(url_pill_.get())->secure = !blank && state_.url.SchemeIs("https");
  url_pill_->InvalidateLayout();
  url_pill_->SchedulePaint();
  nav_buttons_[0]->SetEnabled(state_.can_go_back);
  nav_buttons_[1]->SetEnabled(state_.can_go_forward);
  ApplyAppearance();
}

void WorkspaceShell::RebuildTabs() {
  tab_list_->RemoveAllChildViews();
  for (size_t i = 0; i < state_.tabs.size(); ++i) {
    const auto& tab = state_.tabs[i];
    RowButton::Spec spec;
    spec.title = tab.title;
    spec.active = i == state_.active_tab;
    auto host = HostLabel(tab.url);
    spec.monogram = base::i18n::ToUpper((host.empty() ? tab.title : host).substr(0, 1));
    spec.monogram_color = kMonogramColors[base::FastHash(base::UTF16ToUTF8(host)) %
                                          kMonogramColors.size()];
    if (state_.tabs.size() > 1) spec.close = base::BindRepeating(callbacks_.close_tab, i);
    tab_list_->AddChildView(std::make_unique<RowButton>(
        base::BindRepeating(callbacks_.select_tab, i), std::move(spec)));
  }
  sidebar_root_->InvalidateLayout();
}

void WorkspaceShell::RebuildSuggestions() {
  suggestions_.clear();
  suggestions_text_ = address_->GetText();
  const auto text = base::UTF16ToUTF8(base::TrimWhitespace(address_->GetText(), base::TRIM_ALL));
  if (!text.empty()) {
    const GURL url = ResolveTyped(text);
    const bool search = url.is_valid() && url.host() == "duckduckgo.com" &&
                        GURL(text).host() != "duckduckgo.com";
    suggestions_.push_back({search ? u"Szukaj w DuckDuckGo" : u"Otwórz",
                            search ? base::UTF8ToUTF16(text) : base::UTF8ToUTF16(url.is_valid() ? url.spec() : text),
                            url, std::nullopt});
    const auto needle = base::i18n::ToLower(base::UTF8ToUTF16(text));
    for (size_t i = 0; i < state_.tabs.size() && suggestions_.size() < 5; ++i) {
      const auto& tab = state_.tabs[i];
      if (i == state_.active_tab) continue;
      if (base::i18n::ToLower(tab.title).find(needle) == std::u16string::npos &&
          base::i18n::ToLower(base::UTF8ToUTF16(tab.url.spec())).find(needle) == std::u16string::npos)
        continue;
      suggestions_.push_back({tab.title, u"Przełącz na kartę", tab.url, i});
    }
  } else {
    for (size_t i = 0; i < state_.tabs.size() && suggestions_.size() < 5; ++i) {
      if (i == state_.active_tab) continue;
      suggestions_.push_back({state_.tabs[i].title, u"Przełącz na kartę", state_.tabs[i].url, i});
    }
  }
  selected_suggestion_ = std::min(selected_suggestion_, suggestions_.empty() ? 0 : suggestions_.size() - 1);
  suggestion_list_->RemoveAllChildViews();
  for (size_t i = 0; i < suggestions_.size(); ++i) {
    const auto& item = suggestions_[i];
    RowButton::Spec spec;
    spec.title = item.title;
    spec.detail = item.detail;
    spec.chip = true;
    spec.height = 44;
    spec.highlighted = i == selected_suggestion_;
    spec.icon = item.tab ? &vector_icons::kArrowForwardIcon
              : item.title == u"Otwórz" ? &vector_icons::kArrowForwardIcon
                                        : &vector_icons::kSearchIcon;
    suggestion_list_->AddChildView(std::make_unique<RowButton>(
        base::BindRepeating(&WorkspaceShell::RunSuggestion, weak_factory_.GetWeakPtr(), i),
        std::move(spec)));
  }
  suggestion_list_->SetVisible(!suggestions_.empty());
  PositionPanels();
}

void WorkspaceShell::ApplyAppearance() {
  if (!widget_) return;
  const auto color = colors_[ColorIndex(state_.workspace_id)];
  const auto tint = FrameTint(color);
  root_->SetBackground(views::CreateSolidBackground(tint));
  root_->SchedulePaint();
  if (pinned_) {
    sidebar_root_->SetBackground(views::CreateSolidBackground(tint));
    sidebar_root_->SetBorder(nullptr);
  } else {
    sidebar_root_->SetBackground(views::CreateRoundedRectBackground(Mix(tint, kCard, 70), 12));
    sidebar_root_->SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, 12, Hairline()), gfx::Insets(6)));
  }
  static_cast<EdgeView*>(edge_.get())->color = color;
  edge_->SchedulePaint();
  color_input_->SetText(base::UTF8ToUTF16(Hex(color)));
  for (auto& child : color_input_->parent()->children()) {
    if (auto* swatch = views::AsViewClass<GlyphButton>(child.get()))
      swatch->set_on(swatch->GetTooltipText() == base::UTF8ToUTF16(Hex(color)));
  }
  auto* pin = static_cast<GlyphButton*>(pin_button_.get());
  pin->set_on(pinned_);
  pin->SetTooltipText(pinned_ ? u"Odepnij panel (auto-ukrywanie)" : u"Przypnij panel");
  for (int i = 0; i < 2; ++i)
    static_cast<GlyphButton*>(workspace_buttons_[i].get())->set_on(i == ColorIndex(state_.workspace_id));
  static_cast<ShellRoot*>(root_.get())->pinned = pinned_;
  root_->InvalidateLayout();
  RebuildTabs();
  PositionPanels();
}

void WorkspaceShell::PositionPanels() {
  if (!sidebar_widget_ || !address_widget_ || !settings_widget_) return;
  const auto bounds = widget_->GetClientAreaBoundsInScreen();
  const int width = std::min(kSidebarWidth, std::max(120, bounds.width() - 16));
  sidebar_widget_->SetBounds(gfx::Rect(bounds.x() + kGap, bounds.y() + kShellHeaderHeight, width,
                                      std::max(200, bounds.height() - kShellHeaderHeight - kGap)));
  const int settings_width = 300;
  settings_widget_->SetBounds(gfx::Rect(bounds.x() + kGap + width + kGap,
      bounds.bottom() - kGap - settings_root_->GetHeightForWidth(settings_width) - 40,
      settings_width, settings_root_->GetHeightForWidth(settings_width)));
  const int palette_width = std::min(640, std::max(120, bounds.width() - 48));
  const int height = palette_root_->GetHeightForWidth(palette_width);
  const auto content = content_host_->GetBoundsInScreen();
  address_widget_->SetBounds(gfx::Rect(content.CenterPoint().x() - palette_width / 2,
      content.y() + std::min(132, content.height() / 6), palette_width, height));
}

void WorkspaceShell::OpenAddress(bool new_tab) {
  new_tab_ = new_tab;
  if (!pinned_) sidebar_widget_->Hide();
  address_error_->SetVisible(false);
  selected_suggestion_ = 0;
  const bool blank = new_tab || state_.url.path() == "/start.html";
  address_->SetText(blank ? u"" : base::UTF8ToUTF16(state_.url.spec()));
  RebuildSuggestions();
  address_widget_->Show();
  address_widget_->Activate();
  address_widget_->ClearNativeFocus();
  address_->RequestFocus();
  address_->SelectAll(false);
}
void WorkspaceShell::ShowSidebar() {
  if (!widget_ || !widget_->IsVisible()) return;
  address_widget_->Hide();
  PositionPanels();
  sidebar_widget_->ShowInactive();
  sidebar_widget_->StackAboveWidget(widget_);
}
void WorkspaceShell::ToggleSidebar() {
  if (sidebar_visible()) {
    if (pinned_) SetPinned(false);
    sidebar_widget_->Hide();
  } else ShowSidebar();
}
bool WorkspaceShell::CloseOverlays() {
  const bool visible = address_visible() || settings_visible() || (!pinned_ && sidebar_visible());
  address_widget_->Hide();
  settings_widget_->Hide();
  if (!pinned_) sidebar_widget_->Hide();
  return visible;
}
bool WorkspaceShell::address_visible() const { return address_widget_ && address_widget_->IsVisible(); }
bool WorkspaceShell::settings_visible() const { return settings_widget_ && settings_widget_->IsVisible(); }
void WorkspaceShell::OpenWorkspaceSettings() {
  if (!widget_ || !widget_->IsVisible()) return;
  address_widget_->Hide();
  PositionPanels();
  settings_widget_->Show();
  settings_widget_->Activate();
}
bool WorkspaceShell::sidebar_visible() const { return sidebar_widget_ && sidebar_widget_->IsVisible(); }
void WorkspaceShell::RestorePageFocus() {
  widget_->Activate();
  if (auto* view = content_host_->children().empty() ? nullptr : content_host_->children().back().get()) {
    for (const auto& child : content_host_->children()) if (child->GetVisible()) { view = child.get(); break; }
    view->RequestFocus();
  }
}
void WorkspaceShell::SetPinned(bool pinned) {
  pinned_ = pinned;
  pin_dirty_ = true;
  ApplyAppearance();
  if (pinned_) ShowSidebar();
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, std::nullopt, 0, pinned_),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) { shell->color_error_->SetText(u"Nie udało się zapisać ustawień"); shell->color_error_->SetVisible(true); }
      }, weak_factory_.GetWeakPtr()));
}
void WorkspaceShell::SetWorkspaceColor(std::string_view id, SkColor color) {
  CHECK(id == "work" || id == "personal");
  const int index = ColorIndex(id);
  colors_[index] = color | 0xFF000000;
  color_dirty_[index] = true;
  color_error_->SetVisible(false);
  ApplyAppearance();
  ++pending_writes_;
  io_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce(SavePatch, settings_path_, index, colors_[index], std::nullopt),
      base::BindOnce([](base::WeakPtr<WorkspaceShell> shell, bool ok) {
        if (!shell) return;
        --shell->pending_writes_;
        if (!ok) { shell->color_error_->SetText(u"Nie udało się zapisać koloru"); shell->color_error_->SetVisible(true); }
      }, weak_factory_.GetWeakPtr()));
}
SkColor WorkspaceShell::workspace_color(std::string_view id) const { return colors_[ColorIndex(id)]; }
void WorkspaceShell::ApplyHexColor() {
  if (auto color = ParseColor(base::UTF16ToUTF8(color_input_->GetText())))
    SetWorkspaceColor(state_.workspace_id, *color);
  else { color_error_->SetText(u"Podaj kolor #RRGGBB"); color_error_->SetVisible(true); }
}
void WorkspaceShell::ShowAddressError() {
  address_error_->SetText(u"Wpisz adres strony HTTP lub HTTPS");
  address_error_->SetVisible(true);
  PositionPanels();
}
void WorkspaceShell::RunSuggestion(size_t index) {
  if (index >= suggestions_.size()) return;
  const auto item = suggestions_[index];
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
void WorkspaceShell::ContentsChanged(views::Textfield* sender, const std::u16string&) {
  if (sender != address_) return;
  address_error_->SetVisible(false);
  selected_suggestion_ = 0;
  RebuildSuggestions();
}
bool WorkspaceShell::HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) return false;
  if (event.key_code() == ui::VKEY_ESCAPE) { CloseOverlays(); RestorePageFocus(); return true; }
  if (sender == address_ && !suggestions_.empty() &&
      (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP)) {
    const size_t count = suggestions_.size();
    selected_suggestion_ = (selected_suggestion_ + (event.key_code() == ui::VKEY_DOWN ? 1 : count - 1)) % count;
    RebuildSuggestions();
    return true;
  }
  if (event.key_code() != ui::VKEY_RETURN) return false;
  if (sender == color_input_) { ApplyHexColor(); return true; }
  // SetText() does not notify the controller; resolve what is typed now.
  if (address_->GetText() != suggestions_text_) RebuildSuggestions();
  if (base::TrimWhitespace(address_->GetText(), base::TRIM_ALL).empty() && suggestions_.empty())
    return true;
  if (suggestions_.empty()) { ShowAddressError(); return true; }
  RunSuggestion(selected_suggestion_);
  return true;
}
void WorkspaceShell::OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect&) {
  if (widget == widget_) PositionPanels();
}
void WorkspaceShell::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  if (active) return;
  if (widget == address_widget_.get()) address_widget_->Hide();
  if (widget == settings_widget_.get()) settings_widget_->Hide();
  if (widget == sidebar_widget_.get() && !pinned_ && !settings_visible()) sidebar_widget_->Hide();
}
void WorkspaceShell::OnWidgetVisibilityChanged(views::Widget* widget, bool visible) {
  if (widget == widget_ && !visible) {
    address_widget_->Hide(); sidebar_widget_->Hide(); settings_widget_->Hide();
  } else if (widget == widget_ && visible && pinned_) ShowSidebar();
}
}
