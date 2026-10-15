#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/workspace_shell.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/navigation_entry.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

namespace zitter::prototype {
namespace {
#if BUILDFLAG(IS_MAC)
constexpr int kWorkspaceModifiers = ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN;
#else
constexpr int kWorkspaceModifiers = ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN;
#endif

std::string_view WorkspaceForShortcut(const ui::Accelerator& accelerator) {
  if (ui::Accelerator::MaskOutKeyEventFlags(accelerator.modifiers()) !=
      kWorkspaceModifiers) {
    return {};
  }
  switch (accelerator.key_code()) {
    case ui::VKEY_1: return "work";
    case ui::VKEY_2: return "personal";
    default: return {};
  }
}

// WebView owns the delegate of its WebContents. Extend that delegate rather
// than replacing it and losing WebView's sizing/focus/lifecycle behavior.
class WorkspaceWebView final : public views::WebView {
  METADATA_HEADER(WorkspaceWebView, views::WebView)

 public:
  using KeyHandler = base::RepeatingCallback<
      content::KeyboardEventProcessingResult(
          content::WebContents*, const input::NativeWebKeyboardEvent&)>;

  WorkspaceWebView(Profile* profile, KeyHandler handler, base::RepeatingClosure changed)
      : views::WebView(profile), handler_(std::move(handler)), changed_(std::move(changed)) {}
  ~WorkspaceWebView() override = default;

 private:
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    return handler_.Run(source, event);
  }

  KeyHandler handler_;
  void TitleWasSet(content::NavigationEntry*) override { changed_.Run(); }
  base::RepeatingClosure changed_;
};

BEGIN_METADATA(WorkspaceWebView)
END_METADATA
}  // namespace

WorkspaceWindow::WorkspaceWindow(Profile* work, Profile* personal)
    : controller_(model_, *this) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(work);
  CHECK(personal);
  CHECK_NE(work, personal);
  CHECK_NE(work->GetPath(), personal->GetPath());
  CHECK(!work->IsOffTheRecord());
  CHECK(!personal->IsOffTheRecord());
  CHECK(model_.Add({"work", "work-profile", "Praca"}));
  CHECK(model_.Add({"personal", "personal-profile", "Prywatne"}));
  shell_ = std::make_unique<WorkspaceShell>(
      work->GetPath().DirName().AppendASCII("ZitterAppearance.json"),
      base::BindRepeating([](WorkspaceWindow* window, std::string id) { window->Select(id); }, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::NavigateFromBar, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::SelectTab, base::Unretained(this)));
  auto root = shell_->Build();
  const std::array<Profile*, 2> profiles = {work, personal};
  for (size_t i = 0; i < hosts_.size(); ++i) {
    const auto& workspace = model_.workspaces()[i];
    auto& host = hosts_[i];
    host.workspace_id = workspace.id;
    host.profile_id = workspace.profile_id;
    host.profile = profiles[i];
    host.view = CreatePage(profiles[i]);
    host.tabs.push_back(host.view);
  }
  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetTitle(u"Zitter");
  delegate_->SetHasWindowSizeControls(true);
  delegate_->SetContentsView(std::move(root));
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate_.get();
  params.bounds = gfx::Rect(80, 80, 1200, 800);
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  shell_->Attach(widget_.get(), this);
  widget_->Show();
  CHECK(Select("work"));
}

WorkspaceWindow::Host::Host() = default;
WorkspaceWindow::Host::~Host() = default;

WorkspaceWindow::~WorkspaceWindow() {
  closing_ = true;
  shell_->Detach();
  widget_->GetFocusManager()->UnregisterAccelerators(this);
  for (auto& host : hosts_) {
    host.view = nullptr;
    host.tabs.clear();
    host.profile = nullptr;
  }
  widget_.reset();
  delegate_.reset();
  shell_.reset();
}

views::WebView* WorkspaceWindow::CreatePage(Profile* profile) {
  auto* view = shell_->content_host()->AddChildView(std::make_unique<WorkspaceWebView>(
      profile, base::BindRepeating(&WorkspaceWindow::PreHandlePageKeyEvent, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::UpdateShell, base::Unretained(this))));
  view->set_allow_accelerators(true);
  view->GetWebContents();
  view->SetVisible(false);
  view->GetWebContents()->WasHidden();
  return view;
}

bool WorkspaceWindow::Select(std::string_view id) {
  const bool selected = controller_.Activate(id);
  if (selected) {
    shell_->CloseOverlays();
    UpdateShell();
    widget_->Activate();
    contents(active_id())->Focus();
  }
  return selected;
}

void WorkspaceWindow::UpdateShell() {
  if (closing_ || !widget_) return;
  for (const auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    std::vector<std::u16string> titles;
    for (const auto& view : host.tabs) {
      auto* page = view->GetWebContents();
      auto title = page->GetTitle();
      if (title.empty()) title = base::UTF8ToUTF16(page->GetVisibleURL().host());
      titles.push_back(title.empty() ? u"Nowa karta" : title);
    }
    shell_->Update(host.workspace_id, base::UTF8ToUTF16(model_.Find(host.workspace_id)->name),
        host.workspace_id == "work" ? u"Profil firmowy" : u"Profil osobisty",
        host.view->GetWebContents()->GetVisibleURL(), std::move(titles), host.active_tab);
  }
}

bool WorkspaceWindow::Activate(std::string_view id,
                               std::string_view profile_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Host* selected = nullptr;
  for (auto& host : hosts_) {
    if (host.workspace_id == id && host.profile_id == profile_id) {
      selected = &host;
    }
  }
  if (!selected) {
    return false;
  }
  for (auto& host : hosts_) {
    if (&host != selected) {
      for (auto& tab : host.tabs) {
        tab->GetWebContents()->WasHidden();
        tab->SetVisible(false);
      }
    }
  }
  selected->view->SetVisible(true);
  auto* page = selected->view->GetWebContents();
  page->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  selected->view->RequestFocus();
  return true;
}

content::WebContents* WorkspaceWindow::contents(std::string_view id) const {
  for (const auto& host : hosts_) {
    if (host.workspace_id == id) {
      return host.view->GetWebContents();
    }
  }
  return nullptr;
}

std::string WorkspaceWindow::active_id() const {
  return model_.active() ? model_.active()->id : std::string();
}

void WorkspaceWindow::FocusAddressBar() {
  UpdateShell();
  shell_->OpenAddress(false);
}

size_t WorkspaceWindow::tab_count(std::string_view id) const {
  for (const auto& host : hosts_) if (host.workspace_id == id) return host.tabs.size();
  return 0;
}

void WorkspaceWindow::SelectTab(size_t index) {
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    host.view->GetWebContents()->WasHidden();
    host.view->SetVisible(false);
    host.active_tab = index;
    host.view = host.tabs[index];
    host.view->SetVisible(true);
    host.view->GetWebContents()->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
    shell_->CloseOverlays();
    UpdateShell();
    widget_->Activate();
    host.view->RequestFocus();
    return;
  }
}

bool WorkspaceWindow::NavigateFromBar(const GURL& url, bool new_tab) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) return false;
  if (new_tab) {
    for (auto& host : hosts_) {
      if (host.workspace_id != active_id()) continue;
      host.tabs.push_back(CreatePage(host.profile));
      SelectTab(host.tabs.size() - 1);
      break;
    }
  }
  auto* page = contents(active_id());
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  page->GetController().LoadURLWithParams(params);
  return true;
}

void WorkspaceWindow::Navigate(const GURL& url) { NavigateFromBar(url, false); }

bool WorkspaceWindow::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_state() != ui::Accelerator::KeyState::PRESSED) return false;
#if BUILDFLAG(IS_MAC)
  constexpr int primary = ui::EF_COMMAND_DOWN;
#else
  constexpr int primary = ui::EF_CONTROL_DOWN;
#endif
  const int modifiers = ui::Accelerator::MaskOutKeyEventFlags(accelerator.modifiers());
  if (modifiers == primary && (accelerator.key_code() == ui::VKEY_L ||
                               accelerator.key_code() == ui::VKEY_T)) {
    UpdateShell();
    shell_->OpenAddress(accelerator.key_code() == ui::VKEY_T);
    return true;
  }
  if (modifiers == 0 && accelerator.key_code() == ui::VKEY_ESCAPE) {
    if (!shell_->CloseOverlays()) return false;
    widget_->Activate();
    contents(active_id())->Focus();
    return true;
  }
  const auto workspace = WorkspaceForShortcut(accelerator);
  if (workspace.empty() ||
      accelerator.key_state() != ui::Accelerator::KeyState::PRESSED ||
      !Select(workspace)) {
    return false;
  }
  // Also track accelerators dispatched directly by Views/Cocoa, before the
  // WebContents delegate sees them, so their keyup cannot leak into the page.
  shortcut_key_down_[accelerator.key_code() == ui::VKEY_1 ? 0 : 1] = true;
  suppress_shortcut_char_ = true;
  return true;
}

content::KeyboardEventProcessingResult WorkspaceWindow::PreHandlePageKeyEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  using Result = content::KeyboardEventProcessingResult;
  using Type = blink::WebInputEvent::Type;
  const auto accelerator = ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  const int key = accelerator.key_code();
  const int index = key == ui::VKEY_1 ? 0 : key == ui::VKEY_2 ? 1 : -1;
  if (event.GetType() == Type::kKeyUp && index >= 0 &&
      shortcut_key_down_[index]) {
    shortcut_key_down_[index] = false;
    suppress_shortcut_char_ = false;
    return Result::HANDLED;
  }
  if (event.GetType() == Type::kChar && suppress_shortcut_char_) {
    return Result::HANDLED;
  }
  if (event.GetType() != Type::kRawKeyDown &&
      event.GetType() != Type::kKeyDown) {
    return Result::NOT_HANDLED;
  }
  suppress_shortcut_char_ = false;
  if (event.skip_if_unhandled || source != contents(active_id()) ||
      (WorkspaceForShortcut(accelerator).empty() &&
       !(ui::Accelerator::MaskOutKeyEventFlags(accelerator.modifiers()) ==
#if BUILDFLAG(IS_MAC)
             ui::EF_COMMAND_DOWN &&
#else
             ui::EF_CONTROL_DOWN &&
#endif
         (key == ui::VKEY_L || key == ui::VKEY_T)))) {
    return Result::NOT_HANDLED;
  }
  // Reserve only Zitter's workspace chords. Pages cannot swallow them with
  // preventDefault(), and normal typing/IME stays on Chromium's default path.
  return widget_->GetFocusManager()->ProcessAccelerator(accelerator)
             ? Result::HANDLED : Result::NOT_HANDLED;
}

bool WorkspaceWindow::CanHandleAccelerators() const {
  return widget_ && widget_->IsVisible();
}

void WorkspaceWindow::SetCloseCallback(base::OnceClosure callback) {
  widget_->MakeCloseSynchronous(base::BindOnce(
      [](base::OnceClosure quit, views::Widget::ClosedReason) {
        std::move(quit).Run();
      }, std::move(callback)));
}
}  // namespace zitter::prototype
