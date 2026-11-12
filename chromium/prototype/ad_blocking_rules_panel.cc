// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/ad_blocking_rules_panel.h"

#include <algorithm>
#include <map>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chromium/prototype/adblock_browser_support.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::prototype {
namespace {
// Text styles (PanelScene): 3 title, 5 body, 6 label, 8 url (mono).
constexpr uint32_t kTitle = 3, kBody = 5, kLabel = 6, kUrl = 8;
using Tone = PanelScene::Tone;
std::u16string U(const std::string& text) { return base::UTF8ToUTF16(text); }
}  // namespace

// Text mode: every edit is checked again.
class RulesTextWatcher final : public views::TextfieldController {
 public:
  explicit RulesTextWatcher(base::RepeatingClosure changed) : changed_(std::move(changed)) {}
  ~RulesTextWatcher() override = default;
  void ContentsChanged(views::Textfield*, const std::u16string&) override { changed_.Run(); }
 private:
  base::RepeatingClosure changed_;
};

AdBlockingRulesPanel::AdBlockingRulesPanel() = default;
AdBlockingRulesPanel::~AdBlockingRulesPanel() { Close(); }

void AdBlockingRulesPanel::Close() {
  weak_factory_.InvalidateWeakPtrs();
  try_action_ = nullptr;
  text_ = nullptr; add_field_ = nullptr; body_ = nullptr; count_ = nullptr; footer_ = nullptr;
  list_mode_ = nullptr; text_mode_ = nullptr; add_error_ = nullptr; text_status_ = nullptr; save_ = nullptr;
  scene_.reset(); widget_.reset(); delegate_.reset(); watcher_.reset(); profile_.clear(); callbacks_ = {};
}

views::View* AdBlockingRulesPanel::MakeLine(views::View* parent, uint32_t style, int tone) {
  auto* line = parent->AddChildView(scene_->MakeText(u"", false, false, 0));
  scene_->SetTextStyle(line, style, static_cast<Tone>(tone));
  return line;
}

void AdBlockingRulesPanel::Show(std::string profile, views::Widget* parent,
                                Callbacks callbacks) {
  if (profile.empty() || !parent) return;
  if (widget_ && (widget_->IsClosed() || profile_ != profile)) Close();
  callbacks_ = std::move(callbacks);
  if (widget_) { widget_->Show(); widget_->Activate(); return; }
  profile_ = std::move(profile);
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->SetTitle(T(u"Twoje reguły"));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetModalType(ui::mojom::ModalType::kWindow);
  delegate->set_use_custom_frame(false);
  auto content = std::make_unique<views::View>();
  scene_ = CreatePanelScene(content.get());
  auto weak = weak_factory_.GetWeakPtr();
  // The title, the count, then the two views of the same rules.
  auto* heading = content->AddChildView(std::make_unique<views::View>());
  auto* titles = heading->AddChildView(std::make_unique<views::View>());
  scene_->SetText(MakeLine(titles, kTitle, static_cast<int>(Tone::kInk)), T(u"Twoje reguły"));
  count_ = MakeLine(titles, kLabel, static_cast<int>(Tone::kMuted));
  SetSidebarPanelLayout(titles, true, gfx::Insets(), 2);
  auto* modes = heading->AddChildView(scene_->MakeActions(4));
  list_mode_ = scene_->AddAction(modes, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) { if (panel) panel->SetMode(Mode::kList); }, weak), T(u"Lista"));
  text_mode_ = scene_->AddAction(modes, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) { if (panel) panel->SetMode(Mode::kText); }, weak), T(u"Tekst"));
  for (auto* tab : {list_mode_.get(), text_mode_.get()}) tab->GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  scene_->AddIconAction(heading, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) {
    if (panel && panel->widget_) panel->widget_->Close(); }, weak), T(u"Zamknij"), &vector_icons::kCloseIcon);
  SetSidebarPanelLayout(heading, false, gfx::Insets(), 12, titles, true);
  body_ = content->AddChildView(std::make_unique<views::View>());
  footer_ = content->AddChildView(std::make_unique<views::View>());
  SetSidebarPanelLayout(content.get(), true, gfx::Insets::TLBR(18, 22, 14, 22), 14, body_, false);
  content->SetPreferredSize(gfx::Size(760, 700));
  delegate->SetContentsView(std::move(content));
  delegate_ = std::move(delegate);
  auto* widget = views::DialogDelegate::CreateDialogWidget(delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView());
  widget_.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<AdBlockingRulesPanel> panel, views::Widget::ClosedReason) {
    if (panel) panel->Close(); }, weak));
  widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget, true);
#endif
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(760, area.width()), std::min(700, area.height()));
  bounds.set_origin(parent->GetWindowBoundsInScreen().CenterPoint() - gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
  bounds.AdjustToFit(area);
  widget->SetBounds(bounds);
  mode_ = Mode::kList;
  Rebuild();
  widget->Show();
  scene_->Schedule();
}

std::vector<AdBlockingRulesPanel::Entry> AdBlockingRulesPanel::Entries() const {
  std::vector<Entry> entries;
  std::string note;
  for (const auto& line : callbacks_.read.Run()) {
    // A note heads the rules under it ("Asystent: wp.pl, 2026-11-05").
    if (line.starts_with("!")) { note = std::string(base::TrimWhitespaceASCII(line.substr(1), base::TRIM_ALL)); continue; }
    entries.push_back({line, note, AdblockRuleSite(line), callbacks_.stale && callbacks_.stale.Run(line)});
  }
  return entries;
}

std::vector<std::string> AdBlockingRulesPanel::listed_for_testing() const {
  std::vector<std::string> listed;
  for (const auto& entry : Entries()) if (site_.empty() || entry.site == site_) listed.push_back(entry.rule);
  return listed;
}
std::vector<std::string> AdBlockingRulesPanel::sites_for_testing() const {
  std::vector<std::string> sites;
  for (const auto& entry : Entries()) if (!std::ranges::contains(sites, entry.site)) sites.push_back(entry.site);
  std::sort(sites.begin(), sites.end());
  return sites;
}

void AdBlockingRulesPanel::Refresh() { if (widget_ && mode_ == Mode::kList) Rebuild(); }
void AdBlockingRulesPanel::SetMode(Mode mode) { if (mode_ == mode) return; mode_ = mode; Rebuild(); }
void AdBlockingRulesPanel::SelectSite(std::string site) { site_ = std::move(site); Rebuild(); }

void AdBlockingRulesPanel::Rebuild() {
  if (!body_ || !scene_) return;
  add_field_ = nullptr; text_ = nullptr; add_error_ = nullptr; text_status_ = nullptr; save_ = nullptr;
  body_->RemoveAllChildViews();
  footer_->RemoveAllChildViews();
  const auto entries = Entries();
  std::map<std::string, size_t> per_site;
  for (const auto& entry : entries) ++per_site[entry.site];
  scene_->SetText(count_, TF(u"Reguły: {} · witryny: {} · działają tylko w tym profilu",
                             {base::NumberToString16(entries.size()), base::NumberToString16(per_site.size())}));
  scene_->SetActionKind(list_mode_, mode_ == Mode::kList ? 7 : 6);
  scene_->SetActionKind(text_mode_, mode_ == Mode::kText ? 7 : 6);
  list_mode_->GetViewAccessibility().SetIsSelected(mode_ == Mode::kList);
  text_mode_->GetViewAccessibility().SetIsSelected(mode_ == Mode::kText);
  if (mode_ == Mode::kList) BuildList(); else BuildText();
  body_->InvalidateLayout();
  if (widget_) widget_->GetContentsView()->InvalidateLayout();
  scene_->Schedule();
}

void AdBlockingRulesPanel::BuildList() {
  auto weak = weak_factory_.GetWeakPtr();
  const auto entries = Entries();
  std::map<std::string, size_t> per_site;
  for (const auto& entry : entries) ++per_site[entry.site];
  if (!site_.empty() && !per_site.contains(site_)) site_.clear();
  // Sites on the left, "Wszystkie" first; the chosen one is the chosen chip.
  auto* sites = body_->AddChildView(std::make_unique<views::View>());
  auto add_site = [&](std::string site, std::u16string label, size_t count) {
    auto* group = sites->AddChildView(scene_->MakeActions(0));
    auto* chip = scene_->AddAction(group, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel, std::string site) {
      if (panel) panel->SelectSite(site); }, weak, site), label + u"  " + base::NumberToString16(count));
    scene_->SetActionKind(chip, site == site_ ? 7 : 6);
    chip->GetViewAccessibility().SetIsSelected(site == site_);
  };
  add_site(std::string(), T(u"Wszystkie"), entries.size());
  for (const auto& [site, count] : per_site) add_site(site, site.empty() ? T(u"Ogólne") : U(site), count);
  SetSidebarPanelLayout(sites, true, gfx::Insets(), 4);
  sites->SetPreferredSize(gfx::Size(200, sites->GetPreferredSize().height()));

  // The chosen site's rules, then the field to add one.
  auto* column = body_->AddChildView(std::make_unique<views::View>());
  PanelViewport* viewport = nullptr;
  auto scroll_host = CreatePanelViewport(&viewport, scene_->ViewportChangedCallback(), {80, 470, false, true, true});
  auto* scroll = column->AddChildView(std::move(scroll_host));
  auto rules = std::make_unique<views::View>();
  size_t shown = 0;
  for (const auto& entry : entries) {
    if (!site_.empty() && entry.site != site_) continue;
    ++shown;
    auto* row = rules->AddChildView(std::make_unique<views::View>());
    auto* text = row->AddChildView(std::make_unique<views::View>());
    auto* code = text->AddChildView(scene_->MakeText(U(entry.rule), true, false, 0));
    scene_->SetTextStyle(code, kUrl, Tone::kInk);
    if (entry.stale) {
      auto* stale = MakeLine(text, kLabel, static_cast<int>(Tone::kWarn));
      scene_->SetText(stale, T(u"Nic nie ukrywa od kilku wizyt"));
    }
    if (!entry.note.empty()) scene_->SetText(MakeLine(text, kLabel, static_cast<int>(Tone::kMuted)), U(entry.note));
    SetSidebarPanelLayout(text, true, gfx::Insets(), 3);
    auto* actions = row->AddChildView(std::make_unique<views::View>());
    if (entry.stale && callbacks_.repair) {
      auto* group = actions->AddChildView(scene_->MakeActions(0));
      auto* fix = scene_->AddAction(group, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel, std::string rule) {
        if (panel && panel->callbacks_.repair) panel->callbacks_.repair.Run(rule); }, weak, entry.rule), T(u"Napraw z asystentem"));
      scene_->SetActionKind(fix, 3);
    }
    scene_->AddIconAction(actions, base::BindRepeating([](std::string rule) {
      ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(base::UTF8ToUTF16(rule)); }, entry.rule),
      TF(u"Kopiuj regułę {}", {U(entry.rule)}), &vector_icons::kContentCopyIcon);
    scene_->AddIconAction(actions, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel, std::string rule) {
      if (panel && panel->callbacks_.remove) panel->callbacks_.remove.Run(std::move(rule)); }, weak, entry.rule),
      TF(u"Usuń regułę {}", {U(entry.rule)}), &vector_icons::kCloseIcon);
    SetSidebarPanelLayout(actions, false, gfx::Insets(), 2, nullptr, true);
    SetSidebarPanelLayout(row, false, gfx::Insets::VH(8, 10), 10, text, false);
  }
  if (!shown) {
    auto* empty = rules->AddChildView(scene_->MakeText(
        T(u"Brak reguł. Dodaj regułę poniżej albo poproś asystenta o zablokowanie reklam na stronie."), true, false, 0));
    scene_->SetTextStyle(empty, kBody, Tone::kMuted);
  }
  SetSidebarPanelLayout(rules.get(), true, gfx::Insets(), 2);
  viewport->SetPanelContents(std::move(rules));

  auto* add = column->AddChildView(std::make_unique<views::View>());
  auto* field_row = add->AddChildView(std::make_unique<views::View>());
  add_field_ = scene_->AddSearch(field_row, site_.empty() ? std::u16string(u"example.com##.reklama") : U(site_ + "##.reklama"), T(u"Nowa reguła blokowania"));
  add_field_->view()->SetPreferredSize(gfx::Size(360, 34));
  auto submit = base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) {
    if (!panel || !panel->add_field_) return;
    const auto rule = base::UTF16ToUTF8(base::TrimWhitespace(std::u16string(panel->add_field_->GetText()), base::TRIM_ALL));
    if (rule.empty()) return;
    const auto error = panel->Add(rule);
    if (!error.empty() && panel->add_error_) { panel->scene_->SetText(panel->add_error_, U(error)); panel->add_error_->SetVisible(true); }
  }, weak);
  add_field_->SetCallbacks({}, base::BindRepeating([](base::RepeatingClosure submit, const ui::KeyEvent& event) {
    if (event.type() != ui::EventType::kKeyPressed || event.key_code() != ui::VKEY_RETURN) return false;
    submit.Run(); return true; }, submit));
  auto* add_actions = field_row->AddChildView(scene_->MakeActions(0));
  try_action_ = nullptr;
  if (callbacks_.try_rule) {
    try_action_ = scene_->AddAction(
        add_actions, base::BindRepeating(&AdBlockingRulesPanel::TryRule, weak),
        T(u"Wypróbuj na karcie"));
    scene_->SetActionKind(try_action_, 3);
  }
  scene_->SetActionKind(scene_->AddAction(add_actions, submit, T(u"Dodaj")), 0);
  SetSidebarPanelLayout(field_row, false, gfx::Insets(), 8, add_field_->view(), true);
  add_error_ = MakeLine(add, kLabel, static_cast<int>(Tone::kDanger));
  add_error_->SetVisible(false);
  scene_->SetText(MakeLine(add, kLabel, static_cast<int>(Tone::kMuted)),
                  T(u"Składnia Adblock Plus i uBlock: witryna##selektor, ||host^, +js(). Albo poproś asystenta."));
  SetSidebarPanelLayout(add, true, gfx::Insets::TLBR(10, 0, 0, 0), 6);
  SetSidebarPanelLayout(column, true, gfx::Insets(), 6, scroll, false);
  SetSidebarPanelLayout(body_, false, gfx::Insets(), 16, column, false);

  scene_->SetText(MakeLine(footer_, kLabel, static_cast<int>(Tone::kMuted)), T(u"Zmiany działają po przeładowaniu strony."));
  auto* done = footer_->AddChildView(scene_->MakeActions(0));
  scene_->SetActionKind(scene_->AddAction(done, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) {
    if (panel && panel->widget_) panel->widget_->Close(); }, weak), T(u"Gotowe")), 0);
  SetSidebarPanelLayout(footer_, false, gfx::Insets(), 12, footer_->children().front(), true);
}

void AdBlockingRulesPanel::TryRule() {
  if (!add_field_ || !callbacks_.try_rule)
    return;
  const auto rule = base::UTF16ToUTF8(
      base::TrimWhitespace(add_field_->GetText(), base::TRIM_ALL));
  callbacks_.try_rule.Run(
      rule, base::BindOnce([](base::WeakPtr<AdBlockingRulesPanel> panel,
                              bool success) {
        if (!panel || !panel->widget_ || panel->widget_->IsClosed())
          return false;
        if (success) {
          panel->widget_->Close();
        } else if (panel->add_error_) {
          panel->scene_->SetText(panel->add_error_,
              T(u"Nie można wypróbować tej reguły na bieżącej stronie."));
          panel->add_error_->SetVisible(true);
        }
        return true;
      }, weak_factory_.GetWeakPtr()));
}

views::View* AdBlockingRulesPanel::PrepareTrialForTesting(
    const std::u16string& rule) {
  if (add_field_ && try_action_)
    add_field_->SetText(rule);
  return try_action_;
}

std::string AdBlockingRulesPanel::Add(const std::string& rule) {
  if (rule.size() > 1000) return base::UTF16ToUTF8(T(u"Reguła jest za długa."));
  if (auto problem = AdblockRuleError(rule); !problem.empty())
    return base::UTF16ToUTF8(TF(u"Tej reguły nie da się użyć: {}", {U(problem)}));
  if (auto error = callbacks_.add.Run(rule); !error.empty()) return error;
  if (add_field_) add_field_->SetText(u"");
  return {};
}
std::string AdBlockingRulesPanel::AddForTesting(const std::string& rule) { return Add(rule); }

void AdBlockingRulesPanel::BuildText() {
  try_action_ = nullptr;
  auto weak = weak_factory_.GetWeakPtr();
  auto* hint = body_->AddChildView(scene_->MakeText(T(u"Jedna reguła w linii. Linie z ! to notatki."), false, false, 0));
  scene_->SetTextStyle(hint, kLabel, Tone::kMuted);
  auto textarea = std::make_unique<views::Textarea>();
  textarea->SetFontList(gfx::FontList({"JetBrains Mono", "Consolas", "Menlo", "monospace"}, gfx::Font::NORMAL, 12, gfx::Font::Weight::NORMAL));
  textarea->SetText(base::UTF8ToUTF16(base::JoinString(callbacks_.read.Run(), "\n")));
  textarea->GetViewAccessibility().SetName(T(u"Reguły jako tekst"));
  watcher_ = std::make_unique<RulesTextWatcher>(base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) { if (panel) panel->CheckText(); }, weak));
  textarea->set_controller(watcher_.get());
  text_ = body_->AddChildView(std::move(textarea));
  text_status_ = body_->AddChildView(scene_->MakeText(u"", true, false, 0));
  SetSidebarPanelLayout(body_, true, gfx::Insets(), 8, text_, false);

  auto* spacer = footer_->AddChildView(std::make_unique<views::View>());
  auto* actions = footer_->AddChildView(scene_->MakeActions(8));
  scene_->SetActionKind(scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) {
    if (panel) panel->SetMode(Mode::kList); }, weak), T(u"Anuluj")), 2);
  save_ = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<AdBlockingRulesPanel> panel) {
    if (panel && panel->SaveText()) panel->SetMode(Mode::kList); }, weak), T(u"Zapisz"));
  scene_->SetActionKind(save_, 0);
  SetSidebarPanelLayout(footer_, false, gfx::Insets(), 12, spacer, true);
  CheckText();
}

void AdBlockingRulesPanel::CheckText() {
  if (!text_ || !text_status_) return;
  const auto errors = AdblockTextErrors(base::UTF16ToUTF8(text_->GetText()));
  text_errors_ = errors.size();
  std::u16string status;
  if (errors.empty()) {
    status = T(u"Każda linia jest poprawną regułą.");
  } else {
    // The first errors by line; more than that, only how many.
    for (size_t i = 0; i < std::min<size_t>(errors.size(), 4); ++i)
      // The cause only: the list of what Zitter has stays out of the way here.
      status += (i ? u"\n" : u"") + TF(u"Linia {}: {}", {base::NumberToString16(errors[i].first), U(errors[i].second.substr(0, errors[i].second.find("; ")))});
    if (errors.size() > 4) status += u"\n" + TF(u"Błędy: {} · popraw je, żeby zapisać", {base::NumberToString16(errors.size())});
  }
  scene_->SetText(text_status_, status);
  scene_->SetTextStyle(text_status_, kLabel, errors.empty() ? Tone::kMuted : Tone::kDanger);
  if (save_) save_->SetEnabled(errors.empty());
  body_->InvalidateLayout();
  scene_->Schedule();
}

bool AdBlockingRulesPanel::SaveText() {
  if (!text_ || !callbacks_.save) return false;
  CheckText();
  return text_errors_ == 0 && callbacks_.save.Run(base::UTF16ToUTF8(text_->GetText()));
}
void AdBlockingRulesPanel::SetTextForTesting(const std::u16string& text) {
  if (!text_) return;
  text_->SetText(text);
  CheckText();
}
bool AdBlockingRulesPanel::SaveTextForTesting() { return SaveText(); }

}  // namespace zitter::prototype
