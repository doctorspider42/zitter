// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_UI_AUTOMATION_H_
#define ZITTER_CONTENT_UI_AUTOMATION_H_

#include <memory>

#include "base/files/file_path.h"

namespace zitter::content_embedder {

// Development switch: --zitter-automation opens a control socket at
// <workspace root>/automation.sock. A private temporary probe workspace
// enables a Unix socket on macOS or a per-user named pipe on Windows.
inline constexpr char kUiAutomation[] = "zitter-automation";

// Lets agents drive the app without the system cursor: clicks, hover, drags,
// scrolls, keys and text go straight to the app's widgets and web pages, and
// screenshots come from the window's compositor, so they work with the app in
// the background or covered by other windows. Commands are JSON lines; see
// .claude/skills/verify-ui/scripts/ui.py for the client.
class UiAutomation {
 public:
  virtual ~UiAutomation() = default;
  // Null when the socket can't be opened.
  static std::unique_ptr<UiAutomation> Start(const base::FilePath& socket, bool front);
};

// Start the main window without activation during background automation.
// --zitter-automation=front lets the app activate as usual.
bool UiAutomationBlocksActivation();

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_UI_AUTOMATION_H_
