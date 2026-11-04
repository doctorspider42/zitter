// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_VISUALS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_VISUALS_H_
#include <memory>
#include <string>
#include "base/functional/callback.h"
namespace views { class View; class Background; }
namespace zitter::prototype {
enum class AssistantGlyph { kSpark, kPlus, kHistory, kPin, kClose, kArrow, kStop, kAt, kSearch, kShield, kPage, kChevron };
std::unique_ptr<views::Background> CreateAssistantBackground();
std::unique_ptr<views::View> CreateAssistantIdentity();
std::unique_ptr<views::View> CreateAssistantIcon(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string name, bool primary = false);
std::unique_ptr<views::View> CreateAssistantChip(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string text);
void UpdateAssistantButton(views::View* button, std::u16string text, bool selected = false);
std::unique_ptr<views::View> CreateAssistantSurface(int kind = 0);
std::unique_ptr<views::View> CreateAssistantWelcome();
std::unique_ptr<views::View> CreateAssistantSuggestion(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string title, std::u16string detail);
}
#endif
