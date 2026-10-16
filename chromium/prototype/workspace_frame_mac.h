#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_MAC_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_FRAME_MAC_H_

namespace views {
class NativeWidget;
class Widget;
}

namespace zitter::prototype {
// A titled, resizable NSWindow whose content extends under a transparent
// titlebar; the native traffic lights stay in the standard 28 px titlebar.
// Owned by its NSWindow, like any NativeWidgetMac.
views::NativeWidget* CreateWorkspaceNativeWidget(views::Widget* widget);
}  // namespace zitter::prototype

#endif
