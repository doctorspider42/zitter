#include "chromium/prototype/browser_tools.h"

#include <set>
#include <algorithm>
#include <array>
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "ui/events/event_constants.h"
namespace zitter::prototype {
namespace {
constexpr int Primary() {
#if BUILDFLAG(IS_MAC)
  return ui::EF_COMMAND_DOWN;
#else
  return ui::EF_CONTROL_DOWN;
#endif
}
constexpr int kModifiers = ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN;
struct KeyName { const char* name; ui::KeyboardCode code; };
constexpr std::array kKeys = {
    KeyName{"Tab", ui::VKEY_TAB}, KeyName{"Left", ui::VKEY_LEFT}, KeyName{"Right", ui::VKEY_RIGHT},
    KeyName{"Up", ui::VKEY_UP}, KeyName{"Down", ui::VKEY_DOWN}, KeyName{"Space", ui::VKEY_SPACE},
    KeyName{",", ui::VKEY_OEM_COMMA}, KeyName{"[", ui::VKEY_OEM_4}, KeyName{"]", ui::VKEY_OEM_6},
    KeyName{"=", ui::VKEY_OEM_PLUS}, KeyName{"-", ui::VKEY_OEM_MINUS}, KeyName{"/", ui::VKEY_OEM_2}};
}
std::vector<SearchProvider> DefaultSearchProviders() {
  return {{"DuckDuckGo", "d", "https://duckduckgo.com/?q={query}"},
          {"Google", "g", "https://www.google.com/search?q={query}"},
          {"Wikipedia", "w", "https://pl.wikipedia.org/w/index.php?search={query}"},
          {"YouTube", "yt", "https://www.youtube.com/results?search_query={query}"},
          {"GitHub", "gh", "https://github.com/search?q={query}"},
          {"Bing", "b", "https://www.bing.com/search?q={query}"}};
}
std::vector<Shortcut> ArcShortcuts() {
  std::vector<Shortcut> result;
  auto add = [&](std::string id, std::u16string title, ui::KeyboardCode key, int modifiers) {
    result.push_back({std::move(id), std::move(title), ui::Accelerator(key, modifiers)});
  };
  const int p = Primary();
  add("address", T(u"Adres i wyszukiwanie"), ui::VKEY_L, p);
  add("new_tab", T(u"Nowa karta"), ui::VKEY_T, p);
  add("palette", T(u"Command Center"), ui::VKEY_K, p);
  add("close_tab", T(u"Zamknij kartę / Peek"), ui::VKEY_W, p);
  add("close_other_tabs", T(u"Zamknij pozostałe karty"), ui::VKEY_K, p | ui::EF_SHIFT_DOWN);
  add("reopen", T(u"Przywróć zamkniętą kartę"), ui::VKEY_T, p | ui::EF_SHIFT_DOWN);
  add("sidebar", T(u"Pokaż / ukryj panel"), ui::VKEY_S, p);
  add("reload", T(u"Odśwież stronę"), ui::VKEY_R, p);
#if BUILDFLAG(IS_WIN)
  // Design: Teksty i skróty. Ctrl+Y is Redo on Windows.
  add("history", T(u"Historia przeglądania"), ui::VKEY_H, p);
#else
  add("history", T(u"Historia przeglądania"), ui::VKEY_Y, p);
#endif
  add("downloads", T(u"Pobrania"), ui::VKEY_J, p);
  add("find", T(u"Znajdź na stronie"), ui::VKEY_F, p);
  add("print", T(u"Drukuj"), ui::VKEY_P, p);
  add("print_system", T(u"Drukuj w oknie systemowym"), ui::VKEY_P, p | ui::EF_SHIFT_DOWN);
  add("extensions", T(u"Rozszerzenia"), ui::VKEY_E, p | ui::EF_SHIFT_DOWN);
  add("settings", T(u"Preferencje"), ui::VKEY_OEM_COMMA, p);
  add("next_space", T(u"Następna przestrzeń"), ui::VKEY_RIGHT, p | ui::EF_ALT_DOWN);
  add("previous_space", T(u"Poprzednia przestrzeń"), ui::VKEY_LEFT, p | ui::EF_ALT_DOWN);
  add("next_tab", T(u"Następna karta"), ui::VKEY_TAB, ui::EF_CONTROL_DOWN);
  add("previous_tab", T(u"Poprzednia karta"), ui::VKEY_TAB, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("split", T(u"Dodaj / zamknij Split View"), ui::VKEY_OEM_PLUS, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("close_split", T(u"Zamknij Split View"), ui::VKEY_OEM_MINUS, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("left_pane", T(u"Aktywuj lewą stronę podziału"), ui::VKEY_1, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("right_pane", T(u"Aktywuj prawą stronę podziału"), ui::VKEY_2, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("third_pane", T(u"Aktywuj trzeci panel podziału"), ui::VKEY_3, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("fourth_pane", T(u"Aktywuj czwarty panel podziału"), ui::VKEY_4, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  add("back", T(u"Wstecz"), ui::VKEY_LEFT, p);
  add("forward", T(u"Dalej"), ui::VKEY_RIGHT, p);
#if BUILDFLAG(IS_WIN)
  add("timeline", T(u"Time Machine przestrzeni"), ui::VKEY_H, p | ui::EF_SHIFT_DOWN);
#else
  add("timeline", T(u"Time Machine przestrzeni"), ui::VKEY_Y, p | ui::EF_SHIFT_DOWN);
#endif
  add("snapshot", T(u"Zapisz snapshot przestrzeni"), ui::VKEY_S, p | ui::EF_SHIFT_DOWN);
  for (int i = 0; i < 9; ++i) {
    add("space" + std::to_string(i + 1), TF(u"Przestrzeń {}", {base::UTF8ToUTF16(std::to_string(i + 1))}),
#if BUILDFLAG(IS_WIN)
        // Alt+N: Ctrl+Alt is AltGr in Polish and other layouts.
        static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), ui::EF_ALT_DOWN);
#else
        static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), ui::EF_CONTROL_DOWN | (p == ui::EF_CONTROL_DOWN ? ui::EF_ALT_DOWN : 0));
#endif
  }
  for (int i = 0; i < 9; ++i)
    add("tab" + std::to_string(i + 1), TF(u"Przejdź do karty {}", {base::UTF8ToUTF16(std::to_string(i + 1))}), static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), p);
  return result;
}
bool ValidSearchProvider(const SearchProvider& provider) {
  if (provider.name.empty() || provider.name.size() > 80 || provider.keyword.empty() || provider.keyword.size() > 16 || provider.url.size() > 4096) return false;
  for (char c : provider.keyword) if (!base::IsAsciiLower(c) && !base::IsAsciiDigit(c)) return false;
  const auto marker = provider.url.find("{query}");
  if (marker == std::string::npos || provider.url.find("{query}", marker + 7) != std::string::npos) return false;
  std::string test = provider.url; test.replace(marker, 7, "zitter");
  GURL url(test);
  // Query substitution must never change the origin, credentials or scheme.
  const auto authority_end = provider.url.find('/', provider.url.find("://") + 3);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password() &&
      authority_end != std::string::npos && marker >= authority_end;
}
GURL SearchDestination(const SearchProvider& provider, std::string query) {
  if (!ValidSearchProvider(provider)) return GURL();
  std::string url = provider.url;
  const auto marker = url.find("{query}");
  const auto question = url.find('?');
  url.replace(marker, 7, base::EscapeQueryParamValue(query, question != std::string::npos && question < marker));
  return GURL(url);
}
std::string FormatShortcut(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_UNKNOWN) return "";
  std::string result;
  const int mods = accelerator.modifiers();
  if (mods & ui::EF_COMMAND_DOWN) result += "Cmd+";
  if (mods & ui::EF_CONTROL_DOWN) result += "Ctrl+";
  if (mods & ui::EF_ALT_DOWN) result += "Alt+";
  if (mods & ui::EF_SHIFT_DOWN) result += "Shift+";
  const auto key = accelerator.key_code();
  if ((key >= ui::VKEY_A && key <= ui::VKEY_Z) || (key >= ui::VKEY_0 && key <= ui::VKEY_9)) result += static_cast<char>(key);
  else for (const auto& entry : kKeys) if (entry.code == key) { result += entry.name; break; }
  return result;
}
// The chord as the interface shows it in `Kbd`: macOS symbols in their system
// order (⌃⌥⇧⌘), named keys as symbols or lowercase words. Elsewhere keys are
// words joined by "+" (design: Teksty i skróty), arrows stay arrows.
std::string DisplayShortcut(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_UNKNOWN) return "";
#if !BUILDFLAG(IS_MAC)
  if (accelerator.key_code() == ui::VKEY_LEFT || accelerator.key_code() == ui::VKEY_RIGHT ||
      accelerator.key_code() == ui::VKEY_UP || accelerator.key_code() == ui::VKEY_DOWN) {
    std::string result = FormatShortcut(ui::Accelerator(ui::VKEY_A, accelerator.modifiers()));
    result.pop_back();
    const auto key = accelerator.key_code();
    return result + (key == ui::VKEY_LEFT ? "←" : key == ui::VKEY_RIGHT ? "→" : key == ui::VKEY_UP ? "↑" : "↓");
  }
  return FormatShortcut(accelerator);
#else
  std::string result;
  const int mods = accelerator.modifiers();
  if (mods & ui::EF_CONTROL_DOWN) result += "⌃";
  if (mods & ui::EF_ALT_DOWN) result += "⌥";
  if (mods & ui::EF_SHIFT_DOWN) result += "⇧";
  if (mods & ui::EF_COMMAND_DOWN) result += "⌘";
  const auto key = accelerator.key_code();
  if ((key >= ui::VKEY_A && key <= ui::VKEY_Z) || (key >= ui::VKEY_0 && key <= ui::VKEY_9)) { result += static_cast<char>(key); return result; }
  switch (key) {
    case ui::VKEY_TAB: return result + "tab";
    case ui::VKEY_LEFT: return result + "←";
    case ui::VKEY_RIGHT: return result + "→";
    case ui::VKEY_UP: return result + "↑";
    case ui::VKEY_DOWN: return result + "↓";
    case ui::VKEY_SPACE: return result + "space";
    default: break;
  }
  for (const auto& entry : kKeys) if (entry.code == key) { result += entry.name; break; }
  return result;
#endif
}
std::optional<ui::Accelerator> ParseShortcut(std::string text) {
  if (text.empty()) return ui::Accelerator(ui::VKEY_UNKNOWN, 0);
  const auto parts = base::SplitString(text, "+", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  int modifiers = 0;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const auto mod = base::ToLowerASCII(parts[i]);
    if (mod == "cmd" || mod == "command") modifiers |= ui::EF_COMMAND_DOWN;
    else if (mod == "ctrl" || mod == "control") modifiers |= ui::EF_CONTROL_DOWN;
    else if (mod == "alt" || mod == "option") modifiers |= ui::EF_ALT_DOWN;
    else if (mod == "shift") modifiers |= ui::EF_SHIFT_DOWN;
    else return std::nullopt;
  }
  const auto key_text = base::ToUpperASCII(parts.back());
  ui::KeyboardCode key = ui::VKEY_UNKNOWN;
  if (key_text.size() == 1 && (base::IsAsciiAlpha(key_text[0]) || base::IsAsciiDigit(key_text[0]))) key = static_cast<ui::KeyboardCode>(key_text[0]);
  else for (const auto& entry : kKeys) if (base::EqualsCaseInsensitiveASCII(entry.name, key_text)) key = entry.code;
  ui::Accelerator result(key, modifiers);
  return ValidShortcut(result) && key != ui::VKEY_UNKNOWN ? std::optional(result) : std::nullopt;
}
bool ValidShortcut(const ui::Accelerator& accelerator) {
  const int mods = accelerator.modifiers();
  const auto key = accelerator.key_code();
  if (key == ui::VKEY_UNKNOWN) return mods == 0;
  if ((mods & ~kModifiers) || !(mods & (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN))) return false;
  // Text editing everywhere; Quit, Hide and Minimize only on macOS, where Windows
  // uses Ctrl+H for History.
  if (mods == Primary() && (key == ui::VKEY_A || key == ui::VKEY_C || key == ui::VKEY_V || key == ui::VKEY_X || key == ui::VKEY_Z)) return false;
#if BUILDFLAG(IS_MAC)
  if (mods == Primary() && (key == ui::VKEY_Q || key == ui::VKEY_H || key == ui::VKEY_M)) return false;
#endif
  if ((mods == (Primary() | ui::EF_SHIFT_DOWN) && key == ui::VKEY_Z) ||
      (mods == (Primary() | ui::EF_ALT_DOWN) && (key == ui::VKEY_1 || key == ui::VKEY_2))) return false;
  return (key >= ui::VKEY_A && key <= ui::VKEY_Z) || (key >= ui::VKEY_0 && key <= ui::VKEY_9) ||
      std::any_of(kKeys.begin(), kKeys.end(), [&](const auto& item) { return item.code == key; });
}
BrowserTools::BrowserTools() : providers(DefaultSearchProviders()), shortcuts(ArcShortcuts()) {}
BrowserTools::~BrowserTools() = default;
BrowserTools::BrowserTools(const BrowserTools&) = default;
BrowserTools& BrowserTools::operator=(const BrowserTools&) = default;
BrowserTools::BrowserTools(BrowserTools&&) = default;
BrowserTools& BrowserTools::operator=(BrowserTools&&) = default;
base::DictValue BrowserTools::ToValue() const {
  base::DictValue dict; base::ListValue engines, keys;
  for (const auto& provider : providers) { base::DictValue entry; entry.Set("name", provider.name); entry.Set("keyword", provider.keyword); entry.Set("url", provider.url); engines.Append(std::move(entry)); }
  for (const auto& shortcut : shortcuts) { base::DictValue entry; entry.Set("id", shortcut.id); entry.Set("chord", FormatShortcut(shortcut.accelerator)); keys.Append(std::move(entry)); }
  dict.Set("providers", std::move(engines)); dict.Set("shortcuts", std::move(keys)); dict.Set("default", default_keyword); return dict;
}
BrowserTools BrowserTools::FromValue(const base::DictValue& dict) {
  BrowserTools result;
  if (const auto* list = dict.FindList("providers"); list && !list->empty() && list->size() <= 50) {
    std::vector<SearchProvider> valid;
    for (const auto& item : *list) {
      const auto* entry = item.GetIfDict(); if (!entry) continue;
      const auto* name = entry->FindString("name"); const auto* keyword = entry->FindString("keyword"); const auto* url = entry->FindString("url");
      if (!name || !keyword || !url) continue;
      SearchProvider provider{*name, *keyword, *url};
      if (ValidSearchProvider(provider) && std::none_of(valid.begin(), valid.end(), [&](const auto& other) { return other.keyword == provider.keyword; })) valid.push_back(provider);
    }
    if (!valid.empty()) result.providers = std::move(valid);
  }
  if (const auto* key = dict.FindString("default"); key && std::any_of(result.providers.begin(), result.providers.end(), [&](const auto& provider) { return provider.keyword == *key; })) result.default_keyword = *key;
  if (const auto* list = dict.FindList("shortcuts"); list && list->size() <= 100) {
    // Apply atomically: swaps are valid, duplicates and malformed files fall back.
    auto proposed = result.shortcuts; bool valid = true; std::set<std::string> saved;
    for (const auto& item : *list) {
      const auto* entry = item.GetIfDict(); if (!entry) { valid = false; break; }
      const auto* id = entry->FindString("id"); const auto* chord = entry->FindString("chord");
      if (!id || !chord) { valid = false; break; }
      saved.insert(*id);
      auto parsed = ParseShortcut(*chord); if (!parsed) { valid = false; break; }
      auto found = std::find_if(proposed.begin(), proposed.end(), [&](const auto& shortcut) { return shortcut.id == *id; });
      if (found != proposed.end()) found->accelerator = *parsed;
    }
    // A new default must never invalidate an older user's custom bindings.
    for (const std::string added : {"downloads", "find", "print", "print_system"}) {
      if (saved.contains(added)) continue;
      auto shortcut = std::find_if(proposed.begin(), proposed.end(), [&](const auto& item) { return item.id == added; });
      if (shortcut != proposed.end() && std::any_of(proposed.begin(), proposed.end(), [&](const auto& other) {
        return other.id != added && other.accelerator == shortcut->accelerator;
      })) shortcut->accelerator = ui::Accelerator();
    }
    for (size_t i = 0; i < proposed.size(); ++i) for (size_t j = 0; j < i; ++j)
      if (proposed[i].accelerator.key_code() != ui::VKEY_UNKNOWN && proposed[i].accelerator == proposed[j].accelerator) valid = false;
    if (valid) result.shortcuts = std::move(proposed);
  }
  return result;
}
}  // namespace zitter::prototype
