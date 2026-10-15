#include "chromium/prototype/workspace_shell.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "components/vector_icons/vector_icons.h"
#include "components/url_formatter/url_fixer.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider_key.h"
#include "ui/events/event.h"
#include "ui/display/screen.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

namespace zitter::prototype {
namespace {
constexpr SkColor kBackground = 0xFF151619;
constexpr SkColor kText = 0xFFEEEEF1;
constexpr SkColor kMuted = 0xFFA2A2AE;
constexpr int kSidebarWidth = 248;

int ColorIndex(std::string_view id) { return id == "personal" ? 1 : 0; }
std::string Hex(SkColor color) { return base::StringPrintf("#%06X", color & 0xFFFFFF); }
std::optional<SkColor> ParseColor(std::string_view text) {
  unsigned value;
  if (text.size() != 7 || text[0] != '#' ||
      !base::HexStringToUInt(text.substr(1), &value)) return std::nullopt;
  return value | 0xFF000000;
}
SkColor Tint(SkColor color, int percent) {
  return SkColorSetRGB((29 * (100 - percent) + SkColorGetR(color) * percent) / 100,
                       (30 * (100 - percent) + SkColorGetG(color) * percent) / 100,
                       (35 * (100 - percent) + SkColorGetB(color) * percent) / 100);
}
struct Appearance {
  std::array<SkColor, 2> colors = {0xFF9E91DC, 0xFF84B6A4};
  bool pinned = false;
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
  result.pinned = dict->FindBool("sidebar_pinned").value_or(false);
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

class EdgeView final : public views::View {
  METADATA_HEADER(EdgeView, views::View)
 public:
  explicit EdgeView(base::RepeatingClosure show) : show_(std::move(show)) {}
  void OnMouseEntered(const ui::MouseEvent&) override { show_.Run(); }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(gfx::Rect(1, std::max(0, height() / 2 - 18), 3, 36), color);
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

class ShellRoot final : public views::View {
  METADATA_HEADER(ShellRoot, views::View)
 public:
  ShellRoot() = default;
  gfx::Size GetMinimumSize() const override { return gfx::Size(460, 560); }
  void Layout(PassKey) override {
    const int side = pinned ? std::min(kSidebarWidth + 16, width() / 2) : 0;
    header->SetBounds(12, 0, std::max(0, width() - 24), 38);
    content->SetBounds(side + 8, 38, std::max(0, width() - side - 8), std::max(0, height() - 38));
    edge->SetBounds(0, 38, 8, std::max(0, height() - 38));
    LayoutSuperclass<views::View>(this);
  }
  raw_ptr<views::View> header = nullptr;
  raw_ptr<views::View> content = nullptr;
  raw_ptr<views::View> edge = nullptr;
  bool pinned = false;
};
BEGIN_METADATA(ShellRoot)
END_METADATA

views::MdTextButton* Button(views::View* parent, std::u16string label,
                          base::RepeatingClosure action) {
  auto* button = parent->AddChildView(std::make_unique<views::MdTextButton>(action, label));
  button->SetEnabledTextColors(kText);
  button->SetBgColorOverrideDeprecated(0xFF24252B);
  button->SetStrokeColorOverrideDeprecated(SK_ColorTRANSPARENT);
  button->SetCornerRadius(7);
  button->SetCustomPadding(gfx::Insets::VH(8, 12));
  return button;
}
views::Label* Label(views::View* parent, std::u16string text, SkColor color = kMuted) {
  auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
  label->SetEnabledColor(color);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}
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
}

WorkspaceShell::WorkspaceShell(base::FilePath path,
    base::RepeatingCallback<void(std::string)> select_workspace,
    base::RepeatingCallback<bool(const GURL&, bool)> navigate,
    base::RepeatingCallback<void(size_t)> select_tab)
    : settings_path_(std::move(path)), select_workspace_(std::move(select_workspace)),
      navigate_(std::move(navigate)), select_tab_(std::move(select_tab)),
      io_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}
WorkspaceShell::~WorkspaceShell() { Detach(); }

std::unique_ptr<views::View> WorkspaceShell::Build() {
  auto root = std::make_unique<ShellRoot>();
  root_ = root.get();
  root->SetBackground(views::CreateSolidBackground(kBackground));
  auto* header = root->AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  auto weak = weak_factory_.GetWeakPtr();
  // A compact icon-like label avoids any dependency on Chrome's browser frame.
  auto* menu = Button(header, u"☰", base::BindRepeating(&WorkspaceShell::ToggleSidebar, weak));
  menu->SetMinSize(gfx::Size(32, 30));
  menu->SetCustomPadding(gfx::Insets::VH(4, 8));
  menu->SetTooltipText(u"Karty i workspace");
  auto search = views::ImageButton::CreateIconButton(
      base::BindRepeating(&WorkspaceShell::OpenAddress, weak, false),
      vector_icons::kSearchIcon, u"Szukaj lub wpisz adres");
  header->AddChildView(std::move(search));
  content_host_ = root->AddChildView(std::make_unique<views::View>());
  content_host_->SetLayoutManager(std::make_unique<views::FillLayout>());
  edge_ = root->AddChildView(std::make_unique<EdgeView>(
      base::BindRepeating(&WorkspaceShell::ShowSidebar, weak)));
  root->header = header;
  root->content = content_host_;
  root->edge = edge_;
  return root;
}

void WorkspaceShell::Attach(views::Widget* widget, ui::AcceleratorTarget* target) {
  widget_ = widget;
  widget_->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kDark);
  widget_->AddObserver(this);
  RegisterKeys(widget_, target);
  auto weak = weak_factory_.GetWeakPtr();
  auto sidebar = std::make_unique<SidebarView>(base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell) {
        if (shell && !shell->pinned_ && !shell->color_input_->HasFocus()) {
          shell->sidebar_widget_->Hide();
          shell->RestorePageFocus();
        }
      }, weak));
  sidebar_root_ = sidebar.get();
  auto* layout = sidebar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(14), 10));
  workspace_name_ = Label(sidebar.get(), u"Praca", kText);
  profile_name_ = Label(sidebar.get(), u"Profil firmowy");
#if BUILDFLAG(IS_MAC)
  const auto* new_tab_label = u"+  Nowa karta  ⌘T";
#else
  const auto* new_tab_label = u"+  Nowa karta  Ctrl+T";
#endif
  Button(sidebar.get(), new_tab_label,
         base::BindRepeating(&WorkspaceShell::OpenAddress, weak, true));
  tab_list_ = sidebar->AddChildView(std::make_unique<views::View>());
  tab_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 5));
  auto* spacer = sidebar->AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(spacer, 1);
  auto* workspaces = sidebar->AddChildView(std::make_unique<views::View>());
  workspaces->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  workspace_buttons_[0] = Button(workspaces, u"Praca", base::BindRepeating(select_workspace_, "work"));
  workspace_buttons_[1] = Button(workspaces, u"Prywatne", base::BindRepeating(select_workspace_, "personal"));
  Label(sidebar.get(), u"Kolor workspace");
  auto* presets = sidebar->AddChildView(std::make_unique<views::View>());
  presets->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  for (SkColor color : {0xFF9E91DCu, 0xFF84B6A4u, 0xFFD6A07Bu, 0xFF7FAACBu}) {
    auto* button = Button(presets, u"●", base::BindRepeating(
        [](base::WeakPtr<WorkspaceShell> shell, SkColor value) {
          if (shell) shell->SetWorkspaceColor(shell->active_id_, value);
        }, weak, color));
    button->SetMinSize(gfx::Size(36, 30));
    button->SetEnabledTextColors(color);
    button->SetCustomPadding(gfx::Insets::VH(4, 8));
    button->SetTooltipText(base::UTF8ToUTF16(Hex(color)));
  }
  auto* color_row = sidebar->AddChildView(std::make_unique<views::View>());
  auto* color_layout = color_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  color_input_ = color_row->AddChildView(std::make_unique<views::Textfield>());
  color_input_->GetViewAccessibility().SetName(u"Kolor workspace #RRGGBB");
  color_input_->SetBackgroundColor(0xFF24252B);
  color_input_->SetPlaceholderText(u"#RRGGBB");
  color_input_->set_controller(this);
  color_layout->SetFlexForView(color_input_, 1);
  Button(color_row, u"Zapisz", base::BindRepeating(&WorkspaceShell::ApplyHexColor, weak));
  color_error_ = Label(sidebar.get(), u"");
  color_error_->SetVisible(false);
  pin_button_ = Button(sidebar.get(), u"Przypnij panel", base::BindRepeating(
      [](base::WeakPtr<WorkspaceShell> shell) { if (shell) shell->SetPinned(!shell->pinned_); }, weak));

  auto palette = std::make_unique<views::View>();
  auto* palette_layout = palette->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(14), 4));
  palette->SetBackground(views::CreateRoundedRectBackground(0xFF24252B, 12));
  palette->SetBorder(views::CreateRoundedRectBorder(1, 12, 0xFF484950));
  address_ = palette->AddChildView(std::make_unique<views::Textfield>());
  address_->GetViewAccessibility().SetName(u"Szukaj lub wpisz adres");
  address_->SetPlaceholderText(u"Szukaj lub wpisz adres…");
  address_->SetBackgroundColor(0xFF24252B);
  address_->SetBorder(views::CreateEmptyBorder(0));
  address_->set_controller(this);
  palette_layout->SetFlexForView(address_, 1);
  address_error_ = Label(palette.get(), u"");
  address_error_->SetVisible(false);
  auto make_panel = [&](std::unique_ptr<views::View> contents,
                        std::unique_ptr<views::WidgetDelegate>& delegate,
                        std::unique_ptr<views::Widget>& panel, std::u16string title) {
    delegate = std::make_unique<views::WidgetDelegate>();
    delegate->SetTitle(title);
    delegate->SetContentsView(std::move(contents));
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                     views::Widget::InitParams::TYPE_BUBBLE);
    params.delegate = delegate.get();
    params.SetParent(widget_);
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.rounded_corners = gfx::RoundedCornersF(12);
    panel = std::make_unique<views::Widget>();
    panel->Init(std::move(params));
    panel->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kDark);
    panel->AddObserver(this);
    RegisterKeys(panel.get(), target, true);
  };
  make_panel(std::move(sidebar), sidebar_delegate_, sidebar_widget_, u"Zitter — Panel");
  make_panel(std::move(palette), address_delegate_, address_widget_, u"Zitter — Pasek adresu");
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
        if (shell->pinned_) shell->ShowSidebar();
      }, weak));
}

void WorkspaceShell::Detach() {
  weak_factory_.InvalidateWeakPtrs();
  if (!widget_) return;
  widget_->RemoveObserver(this);
  for (auto* panel : {sidebar_widget_.get(), address_widget_.get()}) panel->RemoveObserver(this);
  address_->set_controller(nullptr);
  color_input_->set_controller(nullptr);
  address_ = nullptr;
  color_input_ = nullptr;
  sidebar_root_ = nullptr;
  tab_list_ = nullptr;
  workspace_name_ = nullptr;
  profile_name_ = nullptr;
  pin_button_ = nullptr;
  workspace_buttons_.fill(nullptr);
  address_error_ = nullptr;
  color_error_ = nullptr;
  address_widget_.reset();
  sidebar_widget_.reset();
  address_delegate_.reset();
  sidebar_delegate_.reset();
  widget_ = nullptr;
  root_ = nullptr;
  content_host_ = nullptr;
  edge_ = nullptr;
}

void WorkspaceShell::Update(std::string id, std::u16string name, std::u16string profile,
    GURL url, std::vector<std::u16string> tabs, size_t active_tab) {
  active_id_ = std::move(id);
  current_url_ = std::move(url);
  tabs_ = std::move(tabs);
  active_tab_ = active_tab;
  workspace_name_->SetText(name);
  profile_name_->SetText(profile);
  ApplyAppearance();
}
void WorkspaceShell::RebuildTabs() {
  tab_list_->RemoveAllChildViews();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    auto* button = Button(tab_list_, tabs_[i], base::BindRepeating(select_tab_, i));
    button->SetBgColorOverrideDeprecated(i == active_tab_ ? Tint(colors_[ColorIndex(active_id_)], 22) : Tint(colors_[ColorIndex(active_id_)], 12));
  }
}
void WorkspaceShell::ApplyAppearance() {
  if (!widget_) return;
  auto color = colors_[ColorIndex(active_id_)];
  sidebar_root_->SetBackground(views::CreateRoundedRectBackground(Tint(color, 12), 12));
  sidebar_root_->SetBorder(views::CreateRoundedRectBorder(1, 12, 0xFF393940));
  static_cast<EdgeView*>(edge_.get())->color = color;
  edge_->SchedulePaint();
  color_input_->SetText(base::UTF8ToUTF16(Hex(color)));
  pin_button_->SetText(pinned_ ? u"Odepnij panel" : u"Przypnij panel");
  for (int i = 0; i < 2; ++i) workspace_buttons_[i]->SetBgColorOverrideDeprecated(
      i == ColorIndex(active_id_) ? Tint(colors_[i], 28) : Tint(color, 12));
  static_cast<ShellRoot*>(root_.get())->pinned = pinned_;
  root_->InvalidateLayout();
  RebuildTabs();
}
void WorkspaceShell::PositionPanels() {
  if (!sidebar_widget_ || !address_widget_) return;
  const auto bounds = widget_->GetClientAreaBoundsInScreen();
  const int width = std::min(kSidebarWidth, std::max(120, bounds.width() - 16));
  sidebar_widget_->SetBounds(gfx::Rect(bounds.x() + 8, bounds.y() + 38, width,
                                      std::max(200, bounds.height() - 46)));
  const int palette_width = std::min(560, std::max(120, bounds.width() - 48));
  address_widget_->SetBounds(gfx::Rect(bounds.CenterPoint().x() - palette_width / 2,
      bounds.y() + bounds.height() / 4, palette_width, address_error_->GetVisible() ? 88 : 60));
}
void WorkspaceShell::OpenAddress(bool new_tab) {
  new_tab_ = new_tab;
  if (!pinned_) sidebar_widget_->Hide();
  address_error_->SetVisible(false);
  address_->SetText(new_tab || current_url_.path() == "/start.html" ? u"" :
                    base::UTF8ToUTF16(current_url_.spec()));
  PositionPanels();
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
  const bool visible = address_visible() || (!pinned_ && sidebar_visible());
  address_widget_->Hide();
  if (!pinned_) sidebar_widget_->Hide();
  return visible;
}
bool WorkspaceShell::address_visible() const { return address_widget_ && address_widget_->IsVisible(); }
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
  if (auto color = ParseColor(base::UTF16ToUTF8(color_input_->GetText()))) SetWorkspaceColor(active_id_, *color);
  else { color_error_->SetText(u"Podaj kolor #RRGGBB"); color_error_->SetVisible(true); }
}
bool WorkspaceShell::HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed) return false;
  if (event.key_code() == ui::VKEY_ESCAPE) { CloseOverlays(); RestorePageFocus(); return true; }
  if (event.key_code() != ui::VKEY_RETURN) return false;
  if (sender == color_input_) { ApplyHexColor(); return true; }
  auto text = base::UTF16ToUTF8(address_->GetText());
  if (text.empty()) return true;
  GURL url(text);
  const bool bare_address = text.find('.') != std::string::npos ||
                            text.starts_with("localhost") ||
                            text.starts_with("[::1]");
  if ((!url.has_scheme() && !bare_address) || text.find(' ') != std::string::npos) {
    url = GURL("https://duckduckgo.com/?q=" +
               base::EscapeQueryParamValue(text, true));
  } else if (!url.has_scheme() || text.starts_with("localhost:")) {
    url = url_formatter::FixupURL(text);
  }
  if (!url.SchemeIsHTTPOrHTTPS()) url = GURL();
  if (url.is_valid() && navigate_.Run(url, new_tab_)) {
    CloseOverlays(); RestorePageFocus();
  } else {
    address_error_->SetText(u"Wpisz adres strony HTTP lub HTTPS");
    address_error_->SetVisible(true);
    PositionPanels();
  }
  return true;
}
void WorkspaceShell::OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect&) {
  if (widget == widget_) PositionPanels();
}
void WorkspaceShell::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  if (active) return;
  if (widget == address_widget_.get()) address_widget_->Hide();
  if (widget == sidebar_widget_.get() && !pinned_) sidebar_widget_->Hide();
}
void WorkspaceShell::OnWidgetVisibilityChanged(views::Widget* widget, bool visible) {
  if (widget == widget_ && !visible) {
    address_widget_->Hide(); sidebar_widget_->Hide();
  } else if (widget == widget_ && visible && pinned_) ShowSidebar();
}
}
