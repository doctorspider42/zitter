#include "chromium/prototype/passwords_panel.h"

#include <algorithm>
#include <map>
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "components/device_reauth/device_authenticator.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_form_converters.h"
#include "components/password_manager/core/browser/ui/weak_check_utility.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/origin.h"

namespace zitter::prototype {
namespace {
using password_manager::StoredCredential;
class PasswordsBodyView final : public views::View {
  METADATA_HEADER(PasswordsBodyView, views::View)
 public:
  void ContentsChanged() { InvalidateLayout(); PreferredSizeChanged(); }
};
BEGIN_METADATA(PasswordsBodyView)
END_METADATA
// A site row: a click (or Enter) opens its page. The scene paints the text.
class PasswordRowView final : public views::View {
  METADATA_HEADER(PasswordRowView, views::View)
 public:
  explicit PasswordRowView(base::RepeatingClosure pressed) : pressed_(std::move(pressed)) {
    SetFocusBehavior(pressed_ ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
    if (pressed_) GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override { return pressed_ && event.IsLeftMouseButton(); }
  void OnMouseReleased(const ui::MouseEvent& event) override { if (pressed_ && event.IsLeftMouseButton() && HitTestPoint(event.location())) pressed_.Run(); }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (!pressed_ || (event.key_code() != ui::VKEY_RETURN && event.key_code() != ui::VKEY_SPACE)) return false;
    pressed_.Run(); return true;
  }
 private:
  base::RepeatingClosure pressed_;
};
BEGIN_METADATA(PasswordRowView)
END_METADATA
// The site a credential belongs to, as people read it: „github.com”.
std::u16string Site(const StoredCredential& credential) {
  GURL url(credential.signon_realm);
  if (!url.is_valid()) url = credential.url;
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) host = host.substr(4);
  return base::UTF8ToUTF16(host.empty() ? credential.signon_realm : host);
}
bool Leaked(const StoredCredential& credential) {
  return credential.password_issues.contains(password_manager::InsecureType::kLeaked) ||
         credential.password_issues.contains(password_manager::InsecureType::kPhished);
}
}  // namespace

PasswordsPanel::PasswordsPanel() = default;
PasswordsPanel::~PasswordsPanel() { CloseUI(); }
void PasswordsPanel::CloseUI() {
  weak_factory_.InvalidateWeakPtrs(); cancelable_task_tracker()->TryCancelAll();
  if (store_) store_->RemoveObserver(this);
  store_.reset(); authenticator_.reset(); credentials_.clear(); blocked_.clear(); loaded_ = false; revealed_ = false; editing_ = false; detail_.reset();
  fields_.clear(); scene_.reset(); body_ = nullptr; count_ = nullptr; add_ = nullptr; search_row_ = nullptr; search_ = nullptr; tabs_ = {}; scroll_ = nullptr;
  widget_.reset(); delegate_.reset(); profile_ = nullptr;
}
views::View* PasswordsPanel::MakeLine(views::View* parent, uint32_t style, int tone) {
  auto* line = parent->AddChildView(scene_->MakeText(u"", false, false, 0));
  scene_->SetTextStyle(line, style, static_cast<PanelScene::Tone>(tone)); return line;
}
std::vector<std::u16string> PasswordsPanel::listed_for_testing() const {
  std::vector<std::u16string> sites; for (size_t i : Listed()) sites.push_back(Site(credentials_[i])); return sites;
}
int PasswordsPanel::issues_for_testing(int kind) const {
  int n = 0;
  for (const auto& credential : credentials_) { const auto issues = IssuesOf(credential); n += kind == 0 ? issues.leaked : kind == 1 ? issues.reused : issues.weak; }
  return n;
}

void PasswordsPanel::Show(Profile* profile, views::Widget* parent) {
  if (!profile || !parent) return;
  if (widget_ && (widget_->IsClosed() || profile_ != profile)) CloseUI();
  if (widget_) { widget_->Show(); widget_->Activate(); return; }
  profile_ = profile;
  store_ = ProfilePasswordStoreFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (store_) store_->AddObserver(this);
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->SetTitle(TF(u"Hasła · {}", {BrowsingProfileName(profile)}));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetCanResize(true);
  delegate->set_use_custom_frame(false);
  delegate->set_draggable(true);
  auto content = std::make_unique<views::View>();
  scene_ = CreatePanelScene(content.get());
  auto weak = weak_factory_.GetWeakPtr();
  // `zt-pagetitle`: italic „Hasła”, the count, then adding one.
  auto* heading = content->AddChildView(std::make_unique<views::View>());
  auto* title = MakeLine(heading, 2, static_cast<int>(PanelScene::Tone::kInk)); scene_->SetText(title, T(u"Hasła"));
  count_ = MakeLine(heading, 8, static_cast<int>(PanelScene::Tone::kMuted));
  auto* actions = heading->AddChildView(scene_->MakeActions(4));
  add_ = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->SetPage(Page::kAdd); }, weak), T(u"Dodaj hasło"));
  scene_->SetActionKind(add_, 2);
  SetSidebarPanelLayout(heading, false, gfx::Insets(), 8, count_, true);
  // Passwords, the checkup and the never-saved sites; then importing.
  auto* nav = content->AddChildView(std::make_unique<views::View>());
  auto* pages = nav->AddChildView(scene_->MakeActions(6));
  for (size_t i = 0; i < tabs_.size(); ++i) {
    tabs_[i] = scene_->AddAction(pages, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, Page page) { if (panel) panel->SetPage(page); },
                                                            weak, static_cast<Page>(i)), u"");
    tabs_[i]->GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  }
  auto* tools = nav->AddChildView(scene_->MakeActions(4));
  auto* import = scene_->AddAction(tools, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel && panel->import_) panel->import_.Run(); }, weak), T(u"Importuj…"));
  scene_->SetActionKind(import, 3);
  SetSidebarPanelLayout(nav, false, gfx::Insets(), 8, pages, true);
  search_row_ = content->AddChildView(std::make_unique<views::View>());
  search_ = scene_->AddSearch(search_row_, T(u"Szukaj witryny lub loginu"), T(u"Szukaj haseł"));
  search_->SetCallbacks(base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) {
    if (panel && panel->search_ && !panel->search_->IsIMEComposing()) panel->SetQuery(std::u16string(panel->search_->GetText())); }, weak),
      base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, const ui::KeyEvent& event) {
        if (!panel || event.type() != ui::EventType::kKeyPressed || event.key_code() != ui::VKEY_ESCAPE || panel->query_.empty()) return false;
        panel->search_->SetText(u""); panel->SetQuery(u""); return true; }, weak));
  SetSidebarPanelLayout(search_row_, true, gfx::Insets(), 0);
  PanelViewport* viewport = nullptr;
  auto scroll_host = CreatePanelViewport(&viewport, scene_->ViewportChangedCallback(), {100, 560, false, true, true}); scroll_ = viewport;
  auto* scroll = content->AddChildView(std::move(scroll_host));
  auto body = std::make_unique<PasswordsBodyView>();
  SetSidebarPanelLayout(body.get(), true, gfx::Insets(), 2);
  body_ = body.get(); viewport->SetPanelContents(std::move(body));
  SetSidebarPanelLayout(content.get(), true, gfx::Insets::TLBR(16, 16, 12, 16), 10, scroll);
  content->SetPreferredSize(gfx::Size(720, 700));
  delegate->SetContentsView(std::move(content));
  delegate_ = std::move(delegate);
  auto* widget = views::DialogDelegate::CreateDialogWidget(delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView());
  widget_.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<PasswordsPanel> panel, views::Widget::ClosedReason) { if (panel) panel->CloseUI(); }, weak));
  widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget, true);
#endif
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(parent->GetNativeWindow()).work_area();
  gfx::Rect bounds(0, 0, std::min(760, area.width()), std::min(760, area.height()));
  bounds.set_origin(parent->GetWindowBoundsInScreen().CenterPoint() - gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
  bounds.AdjustToFit(area); widget->SetBounds(bounds);
  page_ = Page::kList; Rebuild(); widget->Show(); scene_->Schedule();
  Load();
}
void PasswordsPanel::Load() { if (store_) store_->GetAllLogins(weak_factory_.GetWeakPtr()); }
void PasswordsPanel::OnGetPasswordStoreResultsOrErrorFrom(password_manager::PasswordStoreInterface*,
    base::expected<std::vector<StoredCredential>, password_manager::PasswordStoreBackendError> results) {
  loaded_ = true; credentials_.clear(); blocked_.clear();
  if (results.has_value())
    for (auto& credential : results.value()) {
      if (credential.blocked_by_user) blocked_.push_back(std::move(credential));
      else if (credential.scheme == password_manager::PasswordForm::Scheme::kHtml || credential.scheme == password_manager::PasswordForm::Scheme::kBasic) credentials_.push_back(std::move(credential));
    }
  // Sites alphabetically, then logins (design: lista witryn).
  auto order = [](const auto& a, const auto& b) { const auto sa = Site(a), sb = Site(b); return sa != sb ? sa < sb : a.username_value < b.username_value; };
  std::sort(credentials_.begin(), credentials_.end(), order); std::sort(blocked_.begin(), blocked_.end(), order);
  if (detail_ && *detail_ >= credentials_.size()) { detail_.reset(); if (page_ == Page::kDetail) page_ = Page::kList; }
  Rebuild();
}
void PasswordsPanel::OnLoginsChanged(password_manager::PasswordStoreInterface*, const password_manager::PasswordStoreChangeList&) { Load(); }
void PasswordsPanel::OnLoginsRetained(password_manager::PasswordStoreInterface*, const std::vector<StoredCredential>&) { Load(); }

PasswordsPanel::Issues PasswordsPanel::IssuesOf(const StoredCredential& credential) const {
  Issues issues; issues.leaked = Leaked(credential);
  issues.weak = password_manager::IsWeak(credential.password_value).value();
  const auto value = credential.password_value.value();
  issues.reused = std::count_if(credentials_.begin(), credentials_.end(), [&](const auto& other) { return other.password_value.value() == value; }) > 1;
  return issues;
}
std::vector<size_t> PasswordsPanel::Listed() const {
  std::vector<size_t> listed;
  const auto query = base::ToLowerASCII(base::UTF16ToUTF8(query_));
  for (size_t i = 0; i < credentials_.size(); ++i) {
    if (page_ == Page::kCheckup) { const auto issues = IssuesOf(credentials_[i]); if (!issues.leaked && !issues.reused && !issues.weak) continue; }
    if (!query.empty() && base::ToLowerASCII(base::UTF16ToUTF8(Site(credentials_[i]) + u" " + credentials_[i].username_value)).find(query) == std::string::npos) continue;
    listed.push_back(i);
  }
  // The checkup puts leaked first, then reused, then weak.
  if (page_ == Page::kCheckup) std::stable_sort(listed.begin(), listed.end(), [&](size_t a, size_t b) {
    auto rank = [&](size_t i) { const auto x = IssuesOf(credentials_[i]); return x.leaked ? 0 : x.reused ? 1 : 2; }; return rank(a) < rank(b); });
  return listed;
}
void PasswordsPanel::SetPage(Page page) {
  if (page == Page::kDetail && !detail_) page = Page::kList;
  page_ = page; editing_ = false; revealed_ = false; Rebuild();
}
void PasswordsPanel::SetQuery(std::u16string query) {
  query = std::u16string(base::TrimWhitespace(query, base::TRIM_ALL));
  if (query_ == query) return;
  query_ = std::move(query); Rebuild();
}
void PasswordsPanel::OpenDetail(size_t index) { if (index >= credentials_.size()) return; detail_ = index; SetPage(Page::kDetail); }

views::View* PasswordsPanel::AddRow(views::View* parent, std::u16string title, std::u16string meta, std::u16string initial, base::RepeatingClosure pressed) {
  auto* row = parent->AddChildView(std::make_unique<PasswordRowView>(std::move(pressed)));
  auto* tile = row->AddChildView(scene_->MakeFileTile()); scene_->SetFileTile(tile, initial);
  auto* column = row->AddChildView(std::make_unique<views::View>());
  auto* name = MakeLine(column, 4, static_cast<int>(PanelScene::Tone::kInk)); scene_->SetText(name, title);
  auto* facts = MakeLine(column, 8, static_cast<int>(PanelScene::Tone::kMuted)); scene_->SetText(facts, meta);
  SetSidebarPanelLayout(column, true, gfx::Insets(), 2);
  row->GetViewAccessibility().SetName(title + u", " + meta);
  SetSidebarPanelLayout(row, false, gfx::Insets::VH(6, 12), 12, column, true);
  return row;
}
void PasswordsPanel::Rebuild() {
  if (!body_) return;
  fields_.clear(); body_->RemoveAllChildViews();
  auto weak = weak_factory_.GetWeakPtr();
  auto empty = [&](std::u16string text) {
    auto* box = body_->AddChildView(std::make_unique<views::View>());
    auto* title = MakeLine(box, 2, static_cast<int>(PanelScene::Tone::kInk)); scene_->SetText(title, loaded_ ? T(u"Nic tu nie ma") : u"");
    box->AddChildView(scene_->MakeText(std::move(text), true, false, 0));
    SetSidebarPanelLayout(box, true, gfx::Insets::VH(48, 24), 12);
  };
  auto field = [&](views::View* parent, std::u16string placeholder, std::u16string text) {
    auto* editor = AddEditor(parent); editor->SetPlaceholderText(placeholder); editor->GetViewAccessibility().SetName(placeholder);
    editor->SetText(text); scene_->RefreshSearch(editor); editor->view()->SetPreferredSize(gfx::Size(320, 32)); fields_.push_back(editor); return editor;
  };
  switch (page_) {
    case Page::kList: case Page::kCheckup: {
      const auto listed = Listed();
      if (page_ == Page::kCheckup) {
        // Design: Kontrola haseł — the three counts, then the passwords to fix.
        auto* summary = body_->AddChildView(std::make_unique<views::View>());
        for (int kind = 0; kind < 3; ++kind) {
          auto* card = summary->AddChildView(std::make_unique<views::View>());
          auto* number = MakeLine(card, 2, static_cast<int>(kind == 0 ? PanelScene::Tone::kDanger : PanelScene::Tone::kWarn));
          scene_->SetText(number, base::NumberToString16(issues_for_testing(kind)));
          auto* label = MakeLine(card, 4, static_cast<int>(PanelScene::Tone::kInk));
          scene_->SetText(label, kind == 0 ? T(u"⚠ Wyciekłe") : kind == 1 ? T(u"↻ Użyte ponownie") : T(u"◔ Słabe"));
          SetSidebarPanelLayout(card, true, gfx::Insets::VH(8, 12), 2);
        }
        SetSidebarPanelLayout(summary, false, gfx::Insets(), 12);
        body_->AddChildView(scene_->MakeText(T(u"Wyciekłe sprawdza Chromium przy logowaniu; Zitter nie wysyła haseł do sprawdzenia."), true, false, 0));
      }
      if (listed.empty()) empty(page_ == Page::kCheckup ? T(u"Żadne hasło nie wymaga zmiany.") : query_.empty() ? T(u"Nie ma jeszcze zapisanych haseł w tym profilu.") : TF(u"Nic nie pasuje do „{}”.", {query_}));
      std::u16string letter;
      for (size_t index : listed) {
        const auto& credential = credentials_[index];
        const auto site = Site(credential);
        const auto first = site.empty() ? u"·" : base::i18n::ToUpper(site.substr(0, 1));
        if (page_ == Page::kList && first != letter) {
          letter = first; auto* label = MakeLine(body_, 7, static_cast<int>(PanelScene::Tone::kMuted)); scene_->SetText(label, letter);
          label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(12, 12, 4, 12)));
        }
        const auto issues = IssuesOf(credential);
        std::u16string meta = credential.username_value.empty() ? T(u"bez loginu") : credential.username_value;
        if (issues.leaked) meta += u" · " + T(u"⚠ Wyciekło"); else if (issues.reused) meta += u" · " + T(u"↻ Użyte ponownie"); else if (issues.weak) meta += u" · " + T(u"◔ Słabe");
        AddRow(body_, site, meta, first, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, size_t index) { if (panel) panel->OpenDetail(index); }, weak, index));
      }
      break;
    }
    case Page::kNever: {
      if (blocked_.empty()) empty(T(u"Zitter proponuje zapisanie hasła na każdej witrynie."));
      for (size_t i = 0; i < blocked_.size(); ++i) {
        const auto site = Site(blocked_[i]);
        auto* row = AddRow(body_, site, T(u"Nie proponuj zapisania hasła"), site.empty() ? u"·" : base::i18n::ToUpper(site.substr(0, 1)), {});
        auto* actions = row->AddChildView(scene_->MakeActions(0));
        auto* remove = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, size_t i) {
          if (panel && panel->store_ && i < panel->blocked_.size()) panel->store_->RemoveLogin(FROM_HERE, panel->blocked_[i]); }, weak, i), T(u"Usuń z listy"));
        scene_->SetActionKind(remove, 2);
      }
      break;
    }
    case Page::kDetail: {
      if (!detail_) break;
      const auto& credential = credentials_[*detail_];
      auto* back = body_->AddChildView(scene_->MakeActions(0));
      auto* all = scene_->AddAction(back, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->SetPage(Page::kList); }, weak), T(u"← Wszystkie hasła"));
      scene_->SetActionKind(all, 3);
      auto* site = MakeLine(body_, 3, static_cast<int>(PanelScene::Tone::kInk)); scene_->SetText(site, Site(credential));
      // Design: Szczegóły hasła — the login, the password hidden until the system says so.
      auto line = [&](std::u16string label, std::u16string value, PanelScene::Tone tone) {
        auto* row = body_->AddChildView(std::make_unique<views::View>());
        auto* name = MakeLine(row, 6, static_cast<int>(PanelScene::Tone::kMuted)); scene_->SetText(name, label);
        name->SetPreferredSize(gfx::Size(96, 18));
        auto* text = MakeLine(row, 8, static_cast<int>(tone)); scene_->SetText(text, value);
        SetSidebarPanelLayout(row, false, gfx::Insets::VH(10, 12), 12, text, true);
        return row;
      };
      auto* login = line(T(u"Login"), credential.username_value.empty() ? T(u"bez loginu") : credential.username_value, PanelScene::Tone::kInk);
      scene_->AddIconAction(login, base::BindRepeating([](std::u16string value) { ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(value); },
                                                       credential.username_value), T(u"Kopiuj login"), &vector_icons::kContentCopyIcon);
      auto* password = line(T(u"Hasło"), revealed_ ? credential.password_value.value() : std::u16string(14, u'•'), PanelScene::Tone::kInk);
      scene_->AddIconAction(password, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->RevealPassword(false); }, weak),
                            revealed_ ? T(u"Ukryj hasło") : T(u"Pokaż hasło"), revealed_ ? &vector_icons::kVisibilityOffIcon : &vector_icons::kVisibilityIcon);
      scene_->AddIconAction(password, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->RevealPassword(true); }, weak),
                            T(u"Kopiuj hasło"), &vector_icons::kContentCopyIcon);
      line(T(u"Witryna"), base::UTF8ToUTF16(credential.url.spec()), PanelScene::Tone::kMuted);
      for (const auto& note : credential.notes) if (!note.value.empty()) line(T(u"Notatka"), note.value, PanelScene::Tone::kMuted);
      if (editing_) {
        auto* edit = body_->AddChildView(std::make_unique<views::View>());
        auto* value = field(edit, T(u"Nowe hasło"), u"");
        auto* buttons = edit->AddChildView(scene_->MakeActions(8));
        auto* save = scene_->AddAction(buttons, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, EditorControl* value) {
          if (panel) panel->ChangePassword(std::u16string(value->GetText())); }, weak, value), T(u"Zapisz"));
        scene_->SetActionKind(save, 0);
        auto* cancel = scene_->AddAction(buttons, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) { panel->editing_ = false; panel->Rebuild(); } }, weak), T(u"Anuluj"));
        scene_->SetActionKind(cancel, 3);
        SetSidebarPanelLayout(edit, false, gfx::Insets::VH(4, 12), 8, nullptr, true);
      }
      auto* actions = body_->AddChildView(scene_->MakeActions(8));
      auto* change = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) {
        if (!panel) return; panel->editing_ = true; panel->Rebuild(); }, weak), T(u"Edytuj hasło"));
      scene_->SetActionKind(change, 2);
      auto* open = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, GURL url) { if (panel && panel->open_) panel->open_.Run(url); },
                                                                  weak, credential.change_password_url.is_valid() ? credential.change_password_url : credential.url), T(u"Zmień hasło na stronie"));
      scene_->SetActionKind(open, 3);
      auto* remove = scene_->AddAction(actions, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->DeleteDetail(); }, weak), T(u"Usuń…"));
      scene_->SetActionKind(remove, 4);
      break;
    }
    case Page::kAdd: {
      // Design: Dodaj hasło — the site, the login and the password.
      auto* form = body_->AddChildView(std::make_unique<views::View>());
      auto* site = field(form, T(u"Witryna, np. github.com"), u"");
      auto* user = field(form, T(u"Login"), u"");
      auto* secret = field(form, T(u"Hasło"), u"");
      auto* buttons = form->AddChildView(scene_->MakeActions(8));
      auto* save = scene_->AddAction(buttons, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel, EditorControl* site, EditorControl* user, EditorControl* secret) {
        if (panel && panel->AddPassword(std::u16string(site->GetText()), std::u16string(user->GetText()), std::u16string(secret->GetText()))) panel->SetPage(Page::kList); }, weak, site, user, secret), T(u"Zapisz"));
      scene_->SetActionKind(save, 0);
      auto* cancel = scene_->AddAction(buttons, base::BindRepeating([](base::WeakPtr<PasswordsPanel> panel) { if (panel) panel->SetPage(Page::kList); }, weak), T(u"Anuluj"));
      scene_->SetActionKind(cancel, 3);
      SetSidebarPanelLayout(form, true, gfx::Insets::VH(8, 12), 10);
      break;
    }
  }
  Refresh();
  static_cast<PasswordsBodyView*>(body_.get())->ContentsChanged(); scene_->Schedule();
}
void PasswordsPanel::Refresh() {
  scene_->RefreshSearch(search_);
  search_row_->SetVisible(page_ == Page::kList || page_ == Page::kCheckup);
  scene_->SetText(count_, loaded_ && !credentials_.empty() ? base::NumberToString16(credentials_.size()) : std::u16string());
  const int problems = issues_for_testing(0) + std::count_if(credentials_.begin(), credentials_.end(), [&](const auto& c) { const auto x = IssuesOf(c); return !x.leaked && (x.reused || x.weak); });
  const std::array<std::u16string, 3> labels = {T(u"Hasła") + u" " + base::NumberToString16(credentials_.size()), T(u"Kontrola") + (problems ? u" ⚠ " + base::NumberToString16(problems) : u""),
                                                T(u"Nigdy nie zapisuj") + u" " + base::NumberToString16(blocked_.size())};
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool on = static_cast<size_t>(page_) == i || (i == 0 && (page_ == Page::kDetail || page_ == Page::kAdd));
    scene_->SetActionText(tabs_[i], labels[i]); scene_->SetActionKind(tabs_[i], on ? 7 : 6); tabs_[i]->GetViewAccessibility().SetIsSelected(on);
  }
}
// The system asks first (Touch ID or the account password), as Chrome's manager does.
void PasswordsPanel::RevealPassword(bool copy) {
  if (!detail_) return;
  if (!copy && revealed_) { revealed_ = false; Rebuild(); return; }
  if (skip_auth_) { Authenticated(copy, true); return; }
  if (!authenticator_ && profile_ && widget_)
    authenticator_ = ChromeDeviceAuthenticatorFactory::GetForProfile(profile_, widget_->GetNativeWindow(),
        device_reauth::DeviceAuthParams(base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager));
  if (!authenticator_) return;
  authenticator_->AuthenticateWithMessage(copy ? T(u"skopiować hasło") : T(u"pokazać hasło"),
      base::BindOnce(&PasswordsPanel::Authenticated, weak_factory_.GetWeakPtr(), copy));
}
void PasswordsPanel::Authenticated(bool copy, bool success) {
  if (!success || !detail_ || *detail_ >= credentials_.size()) return;
  if (copy) { ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(credentials_[*detail_].password_value.value()); return; }
  revealed_ = true; Rebuild();
}
bool PasswordsPanel::AddPassword(std::u16string site, std::u16string username, std::u16string password) {
  if (!store_ || password.empty()) return false;
  site = std::u16string(base::TrimWhitespace(site, base::TRIM_ALL));
  GURL url(site.find(u"://") == std::u16string::npos ? u"https://" + site : site);
  // A site is one word with a dot in its host: „github.com”, not a phrase.
  if (site.empty() || std::ranges::any_of(site, [](char16_t c) { return base::IsUnicodeWhitespace(c); }) ||
      !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().find('.') == std::string::npos) return false;
  StoredCredential credential;
  credential.scheme = password_manager::PasswordForm::Scheme::kHtml;
  credential.url = url.GetWithEmptyPath();
  credential.signon_realm = url::Origin::Create(url).GetURL().spec();
  credential.username_value = std::move(username);
  credential.password_value = password_manager::PasswordString(std::move(password));
  credential.date_created = base::Time::Now();
  credential.type = password_manager::PasswordForm::Type::kManuallyAdded;
  store_->AddLogin(std::move(credential));
  return true;
}
bool PasswordsPanel::ChangePassword(std::u16string password) {
  if (!store_ || !detail_ || password.empty()) return false;
  auto credential = password_manager::CloneStoredCredential(credentials_[*detail_]);
  credential.password_value = password_manager::PasswordString(std::move(password));
  credential.date_password_modified = base::Time::Now();
  credential.password_issues.clear();
  store_->UpdateLogin(std::move(credential));
  editing_ = false; revealed_ = false; Rebuild();
  return true;
}
void PasswordsPanel::DeleteDetail() {
  if (!detail_ || !confirm_) return;
  auto credential = password_manager::CloneStoredCredential(credentials_[*detail_]);
  const auto site = Site(credential); const auto username = credential.username_value;
  confirm_.Run(widget_.get(), TF(u"Usunąć hasło do {}?", {site}), username, T(u"Tego hasła nie da się odzyskać."), T(u"Usuń hasło"),
      base::BindOnce([](base::WeakPtr<PasswordsPanel> panel, StoredCredential credential, bool yes) {
        if (!panel || !yes || !panel->store_) return;
        panel->store_->RemoveLogin(FROM_HERE, credential); panel->detail_.reset(); panel->SetPage(Page::kList);
      }, weak_factory_.GetWeakPtr(), std::move(credential)));
}
}  // namespace zitter::prototype
