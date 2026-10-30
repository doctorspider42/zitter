// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/workspace_frame_win.h"

#include <windows.h>

#include <dwmapi.h>

#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"
#include "ui/views/win/hwnd_util.h"

namespace zitter::prototype {
namespace {
// DWMWA_USE_IMMERSIVE_DARK_MODE; Windows 10 20H1 and later.
constexpr DWORD kImmersiveDarkMode = 20;

class WorkspaceTreeHost final : public views::DesktopWindowTreeHostWin {
 public:
  using views::DesktopWindowTreeHostWin::DesktopWindowTreeHostWin;

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
};
}  // namespace

void ConfigureWorkspaceNativeWidget(views::Widget* widget, views::Widget::InitParams& params) {
  auto* native_widget = new views::DesktopNativeWidgetAura(widget);
  params.native_widget = native_widget;
  params.desktop_window_tree_host = new WorkspaceTreeHost(widget, native_widget);
}

void SetWorkspaceNativeAppearance(views::Widget* widget, std::optional<bool> dark) {
  if (!widget || !widget->GetNativeWindow()) return;
  HWND hwnd = views::HWNDForWidget(widget);
  if (!hwnd) return;
  BOOL value = dark.value_or(false);
  ::DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &value, sizeof(value));
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
