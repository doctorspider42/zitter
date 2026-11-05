// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_VISUALS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_VISUALS_H_
#include <memory>
#include <optional>
#include <string>
#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/range/range.h"
#include "url/gurl.h"
namespace ui { class KeyEvent; }
namespace views { class View; class Background; class Textarea; }
namespace views { class ResizeAreaDelegate; }
namespace zitter::prototype {
enum class AssistantGlyph { kSpark, kPlus, kHistory, kPin, kClose, kArrow, kStop, kAt, kSearch, kShield, kPage, kChevron, kRevoke,
                            kLayers, kCheck, kGear, kCopy, kTrash, kLock, kEye, kExpand, kGlobe, kCalendar, kRepeat, kBack };
std::unique_ptr<views::Background> CreateAssistantBackground();
std::unique_ptr<views::View> CreateAssistantIdentity();
std::unique_ptr<views::View> CreateAssistantIcon(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string name, bool primary = false);
std::unique_ptr<views::View> CreateAssistantChip(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string text);
std::unique_ptr<views::View> CreateAssistantOption(base::RepeatingClosure action, std::u16string text);
std::unique_ptr<views::View> CreateAssistantResizeGrip(views::ResizeAreaDelegate* delegate);
void UpdateAssistantButton(views::View* button, std::u16string text, bool selected = false);
// kind 0 card, 1 composer, 2 accent tint, 3 message, 4 popover (raised, with shadow).
std::unique_ptr<views::View> CreateAssistantSurface(int kind = 0);
std::unique_ptr<views::View> CreateAssistantWelcome();
// An assistant reply as Markdown: headings, paragraphs with **bold**, *italic*,
// `code` and links, bullet and numbered lists, quotes, code blocks, tables and
// rules. Links call `open`; nothing else in the text can act.
std::unique_ptr<views::View> CreateAssistantMarkdown(std::u16string text, base::RepeatingCallback<void(const GURL&)> open);
// A step of the conversation: 0 running (a spinner, the text breathing),
// 1 done (a check), 2 failed (a warning), 3 waiting for the user (a pulsing dot).
std::unique_ptr<views::View> CreateAssistantActivity(std::u16string text, int state);
// A scheduled task in the tasks view: what it does, when, with which model and
// access, how its last run went; a switch pauses it, Run starts it now.
struct AssistantTaskCard {
  std::u16string name, instruction, schedule, model, access, status;
  int state = 0;  // 0 never ran, 1 done, 2 needed the user, 3 failed, 4 paused
  bool active = true;
};
std::unique_ptr<views::View> CreateAssistantTaskCard(AssistantTaskCard card, base::RepeatingClosure open, base::RepeatingClosure toggle,
                                                     base::RepeatingClosure run, base::RepeatingClosure remove);
// The user's own message: selectable plain text.
std::unique_ptr<views::View> CreateAssistantPlainText(std::u16string text);
// The composer field: multiline, unframed, growing up to six lines, then scrolling.
// `key` sees each key press first (Enter to send); returning true consumes it.
std::unique_ptr<views::Textarea> CreateAssistantPromptArea(base::RepeatingCallback<bool(const ui::KeyEvent&)> key);
std::unique_ptr<views::View> CreateAssistantSuggestion(base::RepeatingClosure action, AssistantGlyph glyph, std::u16string title, std::u16string detail);

// The leading mark of a row or chip: a tool glyph, or a page's letter tile.
struct AssistantMark {
  std::optional<AssistantGlyph> glyph;
  std::u16string letter;
  SkColor color = SK_ColorTRANSPARENT;
};
// Uppercase group label with optional trailing text ("OTWARTE KARTY", "3 wyniki").
std::unique_ptr<views::View> CreateAssistantEyebrow(std::u16string text, std::u16string trailing = {});
struct AssistantRow {
  AssistantMark mark;
  std::u16string title, detail, badge;
  gfx::Range match;          // Highlighted part of the title.
  bool mono_detail = false;  // Addresses and model IDs.
  bool mono_title = false;
  bool checked = false;      // Current choice: a check at the end.
  bool disabled = false;
  bool danger = false;
  bool muted_mark = false;   // A switched-off state: the mark in muted ink.
  std::optional<bool> toggle;  // A trailing switch in this state; the row is the switch.
  // A trash at the row's end while it is hovered or active; Delete runs it too.
  base::RepeatingClosure remove;
  std::u16string remove_name;
};
// A popover row: hover/active fill, a mark, one or two lines.
std::unique_ptr<views::View> CreateAssistantMenuRow(base::RepeatingClosure action, AssistantRow row);
void SetAssistantMenuRowActive(views::View* row, bool active);
// Muted copy or a tinted note with a glyph (e.g. the locked model banner).
std::unique_ptr<views::View> CreateAssistantNote(std::u16string text, std::optional<AssistantGlyph> glyph = std::nullopt, bool tinted = false);
// Keyboard hints at a popover's foot: text on the left, key caps on the right.
std::unique_ptr<views::View> CreateAssistantFooter(std::u16string text, std::u16string keys);
void SetAssistantFooterText(views::View* footer, std::u16string text);
// An inserted page or tool. With `remove`, a trailing × removes it.
std::unique_ptr<views::View> CreateAssistantToken(AssistantMark mark, std::u16string text, bool tool, base::RepeatingClosure remove);
// Children wrap onto new lines (chips above the composer field).
std::unique_ptr<views::View> CreateAssistantFlow(int spacing);
// Small pill buttons: 0 primary, 1 outline, 2 quiet, 3 danger tint.
std::unique_ptr<views::View> CreateAssistantPill(base::RepeatingClosure action, std::u16string text, int kind,
                                                 std::optional<AssistantGlyph> glyph = std::nullopt, std::u16string key = {});
// A muted text button, used as a link to Settings.
std::unique_ptr<views::View> CreateAssistantLink(base::RepeatingClosure action, std::u16string text, std::optional<AssistantGlyph> glyph = std::nullopt);
// A page's element as the assistant will click it, drawn from its label only.
std::unique_ptr<views::View> CreateAssistantElementPreview(std::u16string label);
// A background task: mark, title, status (with activity dots while running).
std::unique_ptr<views::View> CreateAssistantTaskRow(AssistantMark mark, std::u16string title, std::u16string status, int state,
                                                    base::RepeatingClosure take_over, base::RepeatingClosure stop);
// Page control status capsule. With `action`, it shows the "finish in the background" button.
std::unique_ptr<views::View> CreateAssistantCapsule(std::u16string text, base::RepeatingClosure action, bool animate);
// A small consent prompt beside the clicked element.
std::unique_ptr<views::View> CreateAssistantConsentBubble(std::u16string title, std::u16string detail, base::RepeatingClosure allow,
                                                          base::RepeatingClosure skip, base::RepeatingClosure allow_all);
views::View* AssistantConsentAllowButton(views::View* bubble);
}
#endif
