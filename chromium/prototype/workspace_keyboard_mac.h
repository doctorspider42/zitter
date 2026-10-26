#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_KEYBOARD_MAC_H_

#include "chromium/prototype/browser_tools.h"
#include "base/functional/callback.h"

namespace views { class Widget; }

namespace zitter::prototype {
// Route standalone window text/keyups through NSWindow's responder chain and
// connect command equivalents to priority accelerators. Panels dispatch their
// equivalents explicitly; the main window keeps AppKit menu dispatch. The
// native window bridge owns the delegate.
// Observe a real Cocoa menu in its tracking runloop; inspect then close it.
bool RunWithNativeMenuInspectionForTesting(base::OnceClosure open,base::RepeatingClosure inspect);
void UpdateWorkspaceMenuShortcuts(const std::vector<Shortcut>& shortcuts);
void InstallWorkspaceKeyboardRouting(views::Widget* widget, bool panel = false);
}

#endif
