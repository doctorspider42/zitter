// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_AD_BLOCKING_RULES_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_AD_BLOCKING_RULES_PANEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Profile;
namespace views { class DialogDelegate; class Textarea; class View; class Widget; }

namespace zitter::prototype {
class EditorControl;
class PanelScene;
class PanelViewport;
class RulesTextWatcher;

// The profile's own ad blocking rules (design: Zitter — Ad blocking
// preferences, "Twoje reguły"): a dialog over Settings. The list view groups
// the rules by site, with each rule's note, copy and remove, a field to add
// one and, for a rule that stopped working, a way to have the assistant fix
// it. The text view is the whole list as one text, checked line by line
// before it replaces the rules.
class AdBlockingRulesPanel {
 public:
  enum class Mode { kList, kText };
  // Asks the assistant to fix `rule` on its site (the shield's repair).
  using Repair = base::RepeatingCallback<void(std::string rule)>;
  AdBlockingRulesPanel();
  ~AdBlockingRulesPanel();
  void Show(Profile* profile, views::Widget* parent, Repair repair);
  void Close();
  // The rules changed elsewhere (Settings, the assistant): show them again.
  void Refresh();
  views::Widget* widget() const { return widget_.get(); }
  void SetMode(Mode mode);
  // "" shows every site.
  void SelectSite(std::string site);
  // Testing.
  Mode mode() const { return mode_; }
  std::vector<std::string> listed_for_testing() const;
  std::vector<std::string> sites_for_testing() const;
  std::string AddForTesting(const std::string& rule);
  void SetTextForTesting(const std::u16string& text);
  size_t text_errors_for_testing() const { return text_errors_; }
  bool SaveTextForTesting();

 private:
  struct Entry { std::string rule, note, site; bool stale = false; };
  std::vector<Entry> Entries() const;
  void Rebuild();
  void BuildList();
  void BuildText();
  void CheckText();
  bool SaveText();
  std::string Add(const std::string& rule);
  views::View* MakeLine(views::View* parent, uint32_t style, int tone);

  raw_ptr<Profile> profile_ = nullptr;
  Repair repair_;
  Mode mode_ = Mode::kList;
  std::string site_;
  size_t text_errors_ = 0;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<views::View> body_ = nullptr, count_ = nullptr, footer_ = nullptr;
  raw_ptr<views::View> list_mode_ = nullptr, text_mode_ = nullptr, add_error_ = nullptr, text_status_ = nullptr, save_ = nullptr;
  raw_ptr<EditorControl> add_field_ = nullptr;
  raw_ptr<views::Textarea> text_ = nullptr;
  std::unique_ptr<RulesTextWatcher> watcher_;
  base::WeakPtrFactory<AdBlockingRulesPanel> weak_factory_{this};
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_AD_BLOCKING_RULES_PANEL_H_
