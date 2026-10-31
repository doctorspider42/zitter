// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/workspace_frame_win.h"

#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>

#include <set>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/no_destructor.h"
#include "base/win/registry.h"
#include "base/win/windows_version.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"
#include "ui/views/view_tracker.h"
#include "ui/views/win/hwnd_util.h"

namespace zitter::prototype {
namespace {
// DWMWA_USE_IMMERSIVE_DARK_MODE; Windows 10 20H1 and later.
constexpr DWORD kImmersiveDarkMode = 20;

// Snap Layouts on Windows 10, for checking the non-client path there: it
// shows no flyout, but hover, press and click run as on Windows 11.
constexpr char kForceSnapLayoutsSwitch[] = "zitter-force-snap-layouts";

bool UseSnapLayouts() {
  static const bool use = base::win::GetVersion() >= base::win::Version::WIN11 ||
                          base::CommandLine::ForCurrentProcess()->HasSwitch(kForceSnapLayoutsSwitch);
  return use;
}

class WorkspaceTreeHost;
// The workspace windows' hosts, so a widget's host is known to be one.
std::set<WorkspaceTreeHost*>& Hosts() {
  static base::NoDestructor<std::set<WorkspaceTreeHost*>> hosts;
  return *hosts;
}

class WorkspaceTreeHost final : public views::DesktopWindowTreeHostWin {
 public:
  WorkspaceTreeHost(views::internal::NativeWidgetDelegate* widget, views::DesktopNativeWidgetAura* native_widget)
      : views::DesktopWindowTreeHostWin(widget, native_widget) {
    Hosts().insert(this);
  }
  ~WorkspaceTreeHost() override { Hosts().erase(this); }

  void set_maximize_button(views::Button* button) { maximize_.SetView(button); }
  void set_command_handler(base::RepeatingCallback<void(std::string)> handler) {
    command_handler_ = std::move(handler);
    if (command_handler_) ::SetPropW(GetHWND(), kWorkspaceCommandProperty, reinterpret_cast<HANDLE>(1));
    else ::RemovePropW(GetHWND(), kWorkspaceCommandProperty);
  }

 protected:
  bool GetClientAreaInsets(gfx::Insets* insets, int frame_thickness) const override {
    if (GetWidget()->IsFullscreen()) {
      *insets = gfx::Insets();
      return true;
    }
    // Any non-client area at the top makes Windows draw its own titlebar. A
    // maximized window is outdented by the frame thickness on every side, so
    // the top inset keeps the client area on its monitor.
    const int top = GetWidget()->IsMaximized() ? frame_thickness : 0;
    *insets = gfx::Insets::TLBR(top, frame_thickness, frame_thickness, frame_thickness);
    return true;
  }
  bool GetDwmFrameInsetsInPixels(gfx::Insets* insets) const override {
    // No glass inside the client area: the header paints its own ground.
    *insets = gfx::Insets();
    return true;
  }
  bool PreHandleMSG(UINT message, WPARAM w_param, LPARAM l_param, LRESULT* result) override {
    // As Chrome's frame: save the session before Windows logs off or shuts down.
    if (message == WM_ENDSESSION && w_param) chrome::SessionEnding();
    if (HandleSnapLayouts(message, w_param, l_param, result)) return true;
    if (message == WM_COPYDATA && command_handler_) {
      const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(l_param);
      if (data && data->dwData == kWorkspaceCommandMessage && data->lpData && data->cbData <= 4096) {
        // Data the sender's copy owns only for this call.
        std::string command(static_cast<const char*>(data->lpData), data->cbData);
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(command_handler_, std::move(command)));
        *result = TRUE;
        return true;
      }
    }
    return views::DesktopWindowTreeHostWin::PreHandleMSG(message, w_param, l_param, result);
  }

 private:
  views::Button* maximize() { return views::AsViewClass<views::Button>(maximize_.view()); }
  bool OverMaximize(LPARAM l_param) {
    views::Button* button = maximize();
    if (!button || !button->GetVisible() || !button->GetWidget()) return false;
    // Signed: a second monitor can sit left of or above the first.
    const gfx::PointF pixels(static_cast<short>(LOWORD(l_param)), static_cast<short>(HIWORD(l_param)));
    const gfx::Point screen = gfx::ToRoundedPoint(display::win::GetScreenWin()->ScreenToDIPPoint(pixels));
    return button->GetBoundsInScreen().Contains(screen);
  }
  void SetMaximizeState(views::Button::ButtonState state) {
    if (views::Button* button = maximize(); button && button->GetState() != state) button->SetState(state);
  }
  // Windows 11 shows Snap Layouts over HTMAXBUTTON. The button then gets
  // non-client messages; handling them here keeps DefWindowProc from drawing
  // the classic caption button over it.
  bool HandleSnapLayouts(UINT message, WPARAM w_param, LPARAM l_param, LRESULT* result) {
    views::Button* button = maximize();
    if (!button || !UseSnapLayouts()) return false;
    switch (message) {
      case WM_NCHITTEST:
        if (!OverMaximize(l_param)) return false;
        *result = HTMAXBUTTON;
        return true;
      case WM_NCMOUSEMOVE:
        if (w_param != HTMAXBUTTON) {
          SetMaximizeState(views::Button::STATE_NORMAL);
          return false;
        }
        if (button->GetState() != views::Button::STATE_PRESSED) SetMaximizeState(views::Button::STATE_HOVERED);
        if (!tracking_) {
          TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, GetHWND(), 0};
          tracking_ = ::TrackMouseEvent(&track);
        }
        *result = 0;
        return true;
      case WM_NCMOUSELEAVE:
        tracking_ = false;
        SetMaximizeState(views::Button::STATE_NORMAL);
        return false;
      case WM_NCLBUTTONDOWN:
      case WM_NCLBUTTONDBLCLK:
        if (w_param != HTMAXBUTTON) return false;
        SetMaximizeState(views::Button::STATE_PRESSED);
        *result = 0;
        return true;
      case WM_NCLBUTTONUP:
        if (w_param != HTMAXBUTTON) {
          SetMaximizeState(views::Button::STATE_NORMAL);
          return false;
        }
        if (button->GetState() == views::Button::STATE_PRESSED) {
          SetMaximizeState(views::Button::STATE_HOVERED);
          views::Widget* widget = button->GetWidget();
          if (widget->IsMaximized()) widget->Restore();
          else widget->Maximize();
        }
        *result = 0;
        return true;
      default:
        return false;
    }
  }

  // Clears itself when the button goes away.
  views::ViewTracker maximize_;
  base::RepeatingCallback<void(std::string)> command_handler_;
  bool tracking_ = false;
};
// Clicks stay with the panel, activation with the window behind it.
class NonActivatingPanelHost final : public views::DesktopWindowTreeHostWin {
 public:
  using views::DesktopWindowTreeHostWin::DesktopWindowTreeHostWin;

 protected:
  bool PreHandleMSG(UINT message, WPARAM w_param, LPARAM l_param, LRESULT* result) override {
    if (message == WM_MOUSEACTIVATE) {
      *result = MA_NOACTIVATE;
      return true;
    }
    return views::DesktopWindowTreeHostWin::PreHandleMSG(message, w_param, l_param, result);
  }
};
}  // namespace

void ConfigureWorkspaceNativeWidget(views::Widget* widget, views::Widget::InitParams& params) {
  auto* native_widget = new views::DesktopNativeWidgetAura(widget);
  params.native_widget = native_widget;
  params.desktop_window_tree_host = new WorkspaceTreeHost(widget, native_widget);
}

void ConfigureNonActivatingPanel(views::Widget* widget, views::Widget::InitParams& params) {
  auto* native_widget = new views::DesktopNativeWidgetAura(widget);
  params.native_widget = native_widget;
  params.desktop_window_tree_host = new NonActivatingPanelHost(widget, native_widget);
}

void SetWorkspaceNativeAppearance(views::Widget* widget, std::optional<bool> dark) {
  if (!widget || !widget->GetNativeWindow()) return;
  HWND hwnd = views::HWNDForWidget(widget);
  if (!hwnd) return;
  BOOL value = dark.value_or(false);
  ::DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &value, sizeof(value));
}

void SetWorkspaceMaximizeButton(views::Widget* widget, views::Button* button) {
  if (!widget || !widget->GetNativeWindow()) return;
  // Only a workspace window's own host (ConfigureWorkspaceNativeWidget).
  const aura::WindowTreeHost* host = widget->GetNativeWindow()->GetHost();
  for (WorkspaceTreeHost* workspace : Hosts())
    if (static_cast<const aura::WindowTreeHost*>(workspace) == host) workspace->set_maximize_button(button);
}

bool IsZitterDefaultBrowser() {
  base::win::RegKey choice(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice", KEY_READ);
  std::wstring prog_id;
  return choice.Valid() && choice.ReadValue(L"ProgId", &prog_id) == ERROR_SUCCESS && prog_id == L"ZitterHTML";
}

void OpenDefaultAppsSettings() {
  ::ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=Zitter", nullptr, nullptr, SW_SHOWNORMAL);
}

void SetWorkspaceCommandHandler(views::Widget* widget, base::RepeatingCallback<void(std::string)> handler) {
  if (!widget || !widget->GetNativeWindow()) return;
  const aura::WindowTreeHost* host = widget->GetNativeWindow()->GetHost();
  for (WorkspaceTreeHost* workspace : Hosts())
    if (static_cast<const aura::WindowTreeHost*>(workspace) == host) workspace->set_command_handler(handler);
}

int WorkspaceResizeBorderThickness(const views::Widget* widget) {
  if (!widget || widget->IsMaximized() || widget->IsFullscreen()) return 0;
  // SM_CYSIZEFRAME plus padding, in DIPs, as the system frame uses.
  HWND hwnd = views::HWNDForWidget(widget);
  const int pixels = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, ::GetDpiForWindow(hwnd)) +
                     ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, ::GetDpiForWindow(hwnd));
  return display::win::GetScreenWin()->ScreenToDIPSize(hwnd, gfx::Size(0, pixels)).height();
}
}  // namespace zitter::prototype
