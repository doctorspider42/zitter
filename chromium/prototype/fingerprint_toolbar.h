// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_TOOLBAR_H_
#define ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_TOOLBAR_H_

#include <memory>
#include <string>

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

// Hosts supply the active profile and page. The shared toolbar owns the
// popover, and the host's view hierarchy owns its button.
class FingerprintToolbar {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    ~Callbacks();

    base::RepeatingCallback<sidebar::DesignPalette()> palette;
    base::RepeatingCallback<bool()> enabled;
    base::RepeatingCallback<std::u16string()> site_code;
    base::RepeatingClosure before_open;
    base::RepeatingClosure rotate;
    base::RepeatingClosure toggle_protection;
    base::RepeatingClosure open_settings;
  };

  explicit FingerprintToolbar(Callbacks callbacks);
  ~FingerprintToolbar();

  std::unique_ptr<views::View> CreateButton(bool private_mode);
  void UpdateButton(bool private_mode);
  void Toggle();
  void Close();
  void Refresh();
  views::Widget* widget() const { return widget_.get(); }

  std::u16string site_code_for_testing() const;
  views::View* rotate_button_for_testing() const;
  views::View* protection_button_for_testing() const;
  void RunCommandForTesting(int command);

 private:
  class Panel;

  void Rotate();
  void ToggleProtection();
  void OpenSettings();
  void DestroyPanel();

  const Callbacks callbacks_;
  bool private_mode_ = false;
  views::ViewTracker button_;
  std::unique_ptr<Panel> panel_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtrFactory<FingerprintToolbar> weak_factory_{this};
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_TOOLBAR_H_
