#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_TOOLS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_TOOLS_H_
#include <optional>
#include <string>
#include <vector>
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
std::vector<SearchProvider> DefaultSearchProviders();
std::vector<Shortcut> ArcShortcuts();
bool ValidSearchProvider(const SearchProvider& provider);
std::optional<ui::Accelerator> ParseShortcut(std::string text);
std::string FormatShortcut(const ui::Accelerator& accelerator);
std::string DisplayShortcut(const ui::Accelerator& accelerator);
bool ValidShortcut(const ui::Accelerator& accelerator);
GURL SearchDestination(const SearchProvider& provider, std::string query);
struct BrowserTools {
  BrowserTools();
  BrowserTools(const BrowserTools&);
  BrowserTools& operator=(const BrowserTools&);
  BrowserTools(BrowserTools&&);
  BrowserTools& operator=(BrowserTools&&);
  ~BrowserTools();
  std::vector<SearchProvider> providers;
  std::vector<Shortcut> shortcuts;
  std::string default_keyword;
  base::DictValue ToValue() const;
  static BrowserTools FromValue(const base::DictValue& dict);
};
struct TimeMachineItem {
  std::string id;
  std::u16string title;
  std::u16string detail;
};
struct TimeMachineState {
  std::vector<TimeMachineItem> snapshots, closed;
  std::vector<TimeMachineItem> sleeping;
};
}  // namespace zitter::prototype
#endif
