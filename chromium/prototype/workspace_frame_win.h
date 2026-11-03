// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_WIN_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_WIN_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"

#include "ui/views/controls/button/button.h"
#include "ui/views/widget/widget.h"
namespace base { class FilePath; }

namespace zitter::prototype {
// The system frame keeps its shadow, resize borders, snapping and Aero
// gestures, as Chrome's Windows frame does, while the client area extends over
// the titlebar: Zitter draws the header band and its caption buttons there.
void ConfigureWorkspaceNativeWidget(views::Widget* widget, views::Widget::InitParams& params);
// A panel that never takes activation (the sidebar, toasts): Windows would
// otherwise eat its clicks, since Views answers WM_MOUSEACTIVATE with
// MA_NOACTIVATEANDEAT for a non-activatable window with a non-client view.
void ConfigureNonActivatingPanel(views::Widget* widget, views::Widget::InitParams& params);
// Dark or light window border (Windows 11) and system menus; nullopt follows
// the system.
void SetWorkspaceNativeAppearance(views::Widget* widget, std::optional<bool> dark);
// Width of the invisible resize band the system frame would put above the
// client area; the header answers HTTOP there while the window is restored.
int WorkspaceResizeBorderThickness(const views::Widget* widget);
// The header's maximize button. On Windows 11 the frame answers HTMAXBUTTON
// over it, so hovering shows Snap Layouts, and drives its hover, press and
// click itself, since those then arrive as non-client mouse messages.
void SetWorkspaceMaximizeButton(views::Widget* widget, views::Button* button);
// Commands a second zitter.exe sends the window (jump_list_win.cc): the window
// carries this property, and the command arrives as WM_COPYDATA with this tag.
inline constexpr wchar_t kWorkspaceCommandProperty[] = L"ZitterWorkspaceCommands";
inline constexpr unsigned long kWorkspaceCommandMessage = 0x5A43;  // "ZC"
inline constexpr unsigned long kMaxWorkspaceCommandBytes = 64 * 1024;
// Whether the user chose Zitter for https (the installer's ZitterHTML).
bool IsZitterDefaultBrowser();
// Windows only lets the user choose the default browser: its Default apps
// page, Zitter's own entry where Windows 11 supports it.
void OpenDefaultAppsSettings();
void SetWorkspaceCommandHandler(views::Widget* widget, base::RepeatingCallback<void(std::string)> handler,
                               const base::FilePath& data_directory);
}  // namespace zitter::prototype

#endif
