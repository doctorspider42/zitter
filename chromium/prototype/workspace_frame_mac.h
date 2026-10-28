#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_MAC_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_MAC_H_

#include <optional>

namespace gfx {
class Rect;
}

namespace views {
class NativeWidget;
class Widget;
}

namespace zitter::prototype {
// A titled, resizable NSWindow whose content extends under a transparent
// titlebar. Owned by its NSWindow, like any NativeWidgetMac.
void SetWorkspaceNativeAppearance(views::Widget* widget, std::optional<bool> dark);
views::NativeWidget* CreateWorkspaceNativeWidget(views::Widget* widget);
// Centers the native traffic lights |center_y| points below the window top
// and keeps them there across AppKit titlebar relayouts. Returns their union
// in window coordinates (top-left origin), or an empty rect when the window
// shows none (full screen, panels).
gfx::Rect AlignWorkspaceWindowButtons(views::Widget* widget, int center_y);
}  // namespace zitter::prototype

#endif
