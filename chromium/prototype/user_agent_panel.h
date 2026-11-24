// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_PANEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view_tracker.h"

namespace views {
class View;
class Widget;
}

namespace zitter::sidebar {
struct DesignPalette;
}

namespace zitter::prototype {

// What the panel shows for the active page. The host fills it from the
// profile's settings (user_agent_policy.h).
struct UserAgentPanelState {
  struct Option {
    // "auto", a built-in preset or "custom:<name>".
    std::string id;
    std::u16string name;
    // "Chrome 157", "Zitter · katalog", "Własny".
    std::u16string detail;
    // The concrete preset this option sends here, and its string.
    std::string preset;
    std::string ua;
  };

  UserAgentPanelState();
  UserAgentPanelState(const UserAgentPanelState&);
  UserAgentPanelState& operator=(const UserAgentPanelState&);
  ~UserAgentPanelState();

  // The page's host without "www."; empty for pages the setting cannot reach.
  std::u16string host;
  // The option chosen for this site: "auto" without an exception.
  std::string selected;
  // The concrete preset the open document was loaded with; empty if unknown.
  std::string applied;
  // Whether auto picks its preset from Zitter's catalogue on this site.
  bool catalog = false;
  // The profile-wide mode, by name.
  std::u16string mode;
  std::vector<Option> options;
};

// The User-Agent panel and, while pinned, its button in the extensions bar.
class UserAgentToolbar {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    ~Callbacks();

    base::RepeatingCallback<sidebar::DesignPalette()> palette;
    base::RepeatingCallback<UserAgentPanelState()> state;
    // Saves the option for the active site ("auto" removes the exception).
    base::RepeatingCallback<void(const std::string&)> choose;
    base::RepeatingClosure before_open;
    base::RepeatingClosure reload;
    base::RepeatingClosure open_settings;
  };

  explicit UserAgentToolbar(Callbacks callbacks);
  ~UserAgentToolbar();

  // The pinned button; the host's view hierarchy owns it.
  std::unique_ptr<views::View> CreateButton();
  // Shows the button's dot while the active site has an exception.
  void UpdateButton(bool exception);
  // Opens the panel below `anchor` (the puzzle or the pinned button).
  void Open(views::View* anchor);
  void Toggle();
  void Close();
  // Rebuilds an open panel from the current state.
  void Refresh();
  views::Widget* widget() const { return widget_.get(); }

  // View ids: 1 reloads, 2 opens Settings, 3 undoes, 4 toggles the details;
  // the option rows start at kOptionIdBase.
  static constexpr int kOptionIdBase = 7400;
  views::View* view_for_testing(int id) const;
  std::u16string identity_for_testing() const;

 private:
  class Panel;

  void Choose(std::string id);
  void Undo();
  void Reload();
  void OpenSettings();
  void ToggleDetails();
  void DestroyPanel();

  const Callbacks callbacks_;
  bool details_ = false;
  // The option chosen when the panel opened, for Undo.
  std::string opened_selection_;
  bool exception_ = false;
  views::ViewTracker button_;
  std::unique_ptr<Panel> panel_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtrFactory<UserAgentToolbar> weak_factory_{this};
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_PANEL_H_
