// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Local spelling on Windows through the system spell checker (ISpellChecker),
// the counterpart of NSSpellChecker in embedder_mac.mm.

#include <windows.h>

#include <spellcheck.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "ui/gfx/range/range.h"
#include "zitter_content/fingerprint_state.mojom.h"

namespace zitter::content_embedder {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<ISpellCheckerFactory> Factory() {
  ComPtr<ISpellCheckerFactory> factory;
  if (FAILED(::CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }
  return factory;
}

std::vector<std::u16string> ReadStrings(IEnumString* strings) {
  std::vector<std::u16string> values;
  if (!strings)
    return values;
  LPOLESTR value = nullptr;
  while (strings->Next(1, &value, nullptr) == S_OK && values.size() < 512) {
    values.push_back(base::WideToUTF16(value));
    ::CoTaskMemFree(value);
  }
  return values;
}

ComPtr<ISpellChecker> Checker(ISpellCheckerFactory* factory,
                              const std::u16string& language) {
  BOOL supported = FALSE;
  auto code = base::UTF16ToWide(language);
  if (FAILED(factory->IsSupported(code.c_str(), &supported)) || !supported) {
    // Profiles store short codes such as "pl"; Windows lists "pl-PL".
    ComPtr<IEnumString> languages;
    if (FAILED(factory->get_SupportedLanguages(&languages)))
      return nullptr;
    const auto prefix = language + u"-";
    for (const auto& candidate : ReadStrings(languages.Get())) {
      if (base::StartsWith(candidate, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
        code = base::UTF16ToWide(candidate);
        supported = TRUE;
        break;
      }
    }
    if (!supported)
      return nullptr;
  }
  ComPtr<ISpellChecker> checker;
  if (FAILED(factory->CreateSpellChecker(code.c_str(), &checker)))
    return nullptr;
  return checker;
}

bool Misspelled(ISpellChecker* checker, const std::wstring& word) {
  ComPtr<IEnumSpellingError> errors;
  ComPtr<ISpellingError> error;
  return SUCCEEDED(checker->Check(word.c_str(), &errors)) &&
         errors->Next(&error) == S_OK;
}

}  // namespace

std::vector<mojom::SpellingMatchPtr> CheckSystemSpelling(
    const std::u16string& input, const std::vector<std::u16string>& dictionaries,
    const std::u16string& selected) {
  const auto factory = Factory();
  if (!factory || input.empty())
    return {};
  std::vector<ComPtr<ISpellChecker>> checkers;
  for (const auto& dictionary : dictionaries) {
    if (!selected.empty() && selected != dictionary)
      continue;
    if (auto checker = Checker(factory.Get(), dictionary))
      checkers.push_back(std::move(checker));
  }
  if (checkers.empty())
    return {};
  const auto text = base::UTF16ToWide(input);
  ComPtr<IEnumSpellingError> errors;
  if (FAILED(checkers.front()->Check(text.c_str(), &errors)))
    return {};
  std::vector<mojom::SpellingMatchPtr> matches;
  ComPtr<ISpellingError> error;
  while (matches.size() < 256 && errors->Next(&error) == S_OK) {
    ULONG start = 0, length = 0;
    CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
    if (FAILED(error->get_StartIndex(&start)) || FAILED(error->get_Length(&length)) ||
        FAILED(error->get_CorrectiveAction(&action)) || !length ||
        start > text.size() || length > text.size() - start) {
      error.Reset();
      continue;
    }
    error.Reset();
    // Repeated words and auto-corrections are not spelling mistakes here.
    if (action != CORRECTIVE_ACTION_GET_SUGGESTIONS &&
        action != CORRECTIVE_ACTION_REPLACE) {
      continue;
    }
    const auto word = text.substr(start, length);
    if (std::any_of(checkers.begin() + 1, checkers.end(),
                    [&](const auto& other) { return !Misspelled(other.Get(), word); })) {
      continue;
    }
    auto match = mojom::SpellingMatch::New();
    match->offset = start;
    match->length = length;
    for (const auto& checker : checkers) {
      ComPtr<IEnumString> guesses;
      if (FAILED(checker->Suggest(word.c_str(), &guesses)))
        continue;
      for (auto& guess : ReadStrings(guesses.Get())) {
        if (!std::ranges::contains(match->suggestions, guess))
          match->suggestions.push_back(std::move(guess));
        if (match->suggestions.size() == 5)
          break;
      }
      if (match->suggestions.size() == 5)
        break;
    }
    matches.push_back(std::move(match));
  }
  return matches;
}

std::vector<gfx::Range> SystemSpellingRanges(
    std::u16string_view text, const std::vector<std::u16string>& dictionaries,
    const std::u16string& selected) {
  std::vector<gfx::Range> ranges;
  for (const auto& match : CheckSystemSpelling(std::u16string(text), dictionaries, selected))
    ranges.emplace_back(match->offset, match->offset + match->length);
  return ranges;
}

std::vector<std::pair<std::u16string, std::u16string>> SystemSpellingLanguages() {
  std::vector<std::pair<std::u16string, std::u16string>> choices;
  const auto factory = Factory();
  if (!factory)
    return choices;
  ComPtr<IEnumString> languages;
  if (FAILED(factory->get_SupportedLanguages(&languages)))
    return choices;
  const LCTYPE name_type = prototype::UiLanguageIsEnglish()
                               ? LOCALE_SENGLISHDISPLAYNAME
                               : LOCALE_SLOCALIZEDDISPLAYNAME;
  for (auto& code : ReadStrings(languages.Get())) {
    const auto wide = base::UTF16ToWide(code);
    wchar_t name[LOCALE_NAME_MAX_LENGTH * 4] = {};
    const int size = ::GetLocaleInfoEx(wide.c_str(), name_type, name,
                                       static_cast<int>(std::size(name)));
    choices.emplace_back(size > 1 ? base::WideToUTF16(std::wstring_view(name, size - 1))
                                  : code,
                         std::move(code));
  }
  std::ranges::sort(choices);
  return choices;
}

}  // namespace zitter::content_embedder
