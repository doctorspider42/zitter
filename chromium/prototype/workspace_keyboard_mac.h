#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_

namespace views { class Widget; }

namespace zitter::prototype {
// Connect the standalone Views window's Cocoa key equivalents to its own
// high-priority accelerators. The native window bridge owns the delegate.
void InstallWorkspaceKeyboardRouting(views::Widget* widget, bool panel = false);
}

#endif
