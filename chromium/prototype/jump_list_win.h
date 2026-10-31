// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_JUMP_LIST_WIN_H_
#define ZITTER_CHROMIUM_PROTOTYPE_JUMP_LIST_WIN_H_

#include <string>
#include <vector>

namespace base {
class CommandLine;
}

namespace zitter::prototype {
// The taskbar's jump list (design: JumpList): Zitter's spaces, then the tasks
// New tab and Command Center. Each entry starts zitter.exe with
// --zitter-command, which a running Zitter receives through
// ForwardCommandToRunningZitter.
inline constexpr char kZitterCommandSwitch[] = "zitter-command";

struct JumpListSpace {
  std::string id;
  std::u16string name;
};
// Rewrites the list off the UI thread; call when the spaces change.
void UpdateJumpList(std::vector<JumpListSpace> spaces);

// In a second zitter.exe, before Chromium starts: hands --zitter-command to
// the window of a running Zitter from the same executable and returns true;
// false when none runs, and the command waits for this process's window.
bool ForwardCommandToRunningZitter(const base::CommandLine& command_line);
}  // namespace zitter::prototype

#endif
