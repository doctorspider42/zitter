#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_TOOLS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_TOOLS_H_
#include <optional>
#include <string_view>
#include <utility>
#include <string>
#include <vector>
#include "zitter/core/command_history.h"
#include "base/time/time.h"
#include "base/values.h"
#include "ui/base/accelerators/accelerator.h"
#include "url/gurl.h"
namespace zitter::prototype {
struct SearchProvider {
  std::string name, keyword, url;
  bool operator==(const SearchProvider&) const = default;
};
struct Shortcut {
  std::string id;
  std::u16string title;
  ui::Accelerator accelerator;
};
struct MouseGestureBinding {
  std::string direction, command;
  bool operator==(const MouseGestureBinding&) const = default;
};
std::vector<MouseGestureBinding> DefaultMouseGestures();
bool ValidMouseGestureCommand(std::string_view command);
// Stable category IDs, independent of the display language.
std::string ShortcutCategory(std::string_view id);
std::vector<SearchProvider> DefaultSearchProviders();
std::vector<Shortcut> ArcShortcuts();
bool ValidSearchProvider(const SearchProvider& provider);
std::optional<ui::Accelerator> ParseShortcut(std::string text);
std::string FormatShortcut(const ui::Accelerator& accelerator);
std::string DisplayShortcut(const ui::Accelerator& accelerator);
bool ValidShortcut(const ui::Accelerator& accelerator);
GURL SearchDestination(const SearchProvider& provider, std::string query);
// UI discovery only; never probes ports or changes network security.
bool IsLocalProject(const GURL& url);
struct BrowserTools {
  std::string developer_mode = "auto";
  BrowserTools();
  BrowserTools(const BrowserTools&);
  BrowserTools& operator=(const BrowserTools&);
  BrowserTools(BrowserTools&&);
  BrowserTools& operator=(BrowserTools&&);
  ~BrowserTools();
  std::vector<SearchProvider> providers;
  std::vector<Shortcut> shortcuts;
  bool mouse_gestures_enabled = false;
  std::vector<MouseGestureBinding> mouse_gestures;
  std::string MouseGestureCommand(std::string_view direction) const;
  std::string default_keyword;
  CommandHistory command_usage;
  void RecordCommandUse(std::string id, std::string target, base::Time now);
  double CommandScore(std::string_view id, std::string_view target, base::Time now) const;
  base::DictValue ToValue() const;
  static BrowserTools FromValue(const base::DictValue& dict);
};
// Minutes the local time zone is ahead of UTC at `time` (Time Machine copy).
int TimelineUtcOffsetMinutes(base::Time time);
}  // namespace zitter::prototype
#endif
