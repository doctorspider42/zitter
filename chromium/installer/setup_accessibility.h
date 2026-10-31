// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// UI Automation for the installer's drawn window: Narrator and other screen
// readers see its text, buttons, radio cards and switches as real controls.
#ifndef ZITTER_CHROMIUM_INSTALLER_SETUP_ACCESSIBILITY_H_
#define ZITTER_CHROMIUM_INSTALLER_SETUP_ACCESSIBILITY_H_

#include <windows.h>

#include <string>
#include <vector>

namespace zitter::setup {

enum class Role { kText, kButton, kRadio, kSwitch, kProgress };

// One element as last painted. |bounds| is in screen pixels.
struct AccessibleItem {
  int id = 0;
  Role role = Role::kText;
  std::wstring name;
  std::wstring help;
  std::wstring automation_id;
  RECT bounds{};
  bool on = false;  // A chosen radio card or a switch that is on.
};

class AccessibilityHost {
 public:
  virtual std::vector<AccessibleItem> AccessibleItems() const = 0;
  virtual int AccessibleFocus() const = 0;  // 0: none
  virtual void SetAccessibleFocus(int id) = 0;
  // Invoke, toggle and select. The host should post this to itself, so a
  // dialog the action opens never runs inside a screen reader's call.
  virtual void AccessibleActivate(int id) = 0;

 protected:
  ~AccessibilityHost() = default;
};

// WM_GETOBJECT for |hwnd|; |host| must outlive the window.
LRESULT AnswerGetObject(HWND hwnd, AccessibilityHost* host, WPARAM w, LPARAM l);
// Events, raised only while a client listens.
void NotifyFocusChanged(HWND hwnd, int id);
void NotifyToggled(HWND hwnd, int id, bool on);
void NotifyChildrenChanged(HWND hwnd);
// WM_DESTROY: disconnects the providers.
void DisconnectAccessibility(HWND hwnd);

}  // namespace zitter::setup

#endif
