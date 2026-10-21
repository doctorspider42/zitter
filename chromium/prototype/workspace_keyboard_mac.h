#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_

#include "chromium/prototype/browser_tools.h"

namespace views { class Widget; }

namespace zitter::prototype {
// Connect the standalone Views window's Cocoa key equivalents to its own
// high-priority accelerators. The native window bridge owns the delegate.
void UpdateWorkspaceMenuShortcuts(const std::vector<Shortcut>& shortcuts);
void InstallWorkspaceKeyboardRouting(views::Widget* widget, bool panel = false);
}

#endif
