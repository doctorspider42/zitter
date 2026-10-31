// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_WIN_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_WIN_H_

#include <optional>

#include "ui/views/controls/button/button.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
// The system frame keeps its shadow, resize borders, snapping and Aero
// gestures, as Chrome's Windows frame does, while the client area extends over
// the titlebar: Zitter draws the header band and its caption buttons there.
void ConfigureWorkspaceNativeWidget(views::Widget* widget, views::Widget::InitParams& params);
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
}  // namespace zitter::prototype

#endif
