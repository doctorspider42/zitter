#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/page_media_access.h"
#include "build/build_config.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "components/sessions/content/session_tab_helper.h"
#include "ui/views/controls/webview/webview.h"
#if BUILDFLAG(IS_MAC)
#include "base/command_line.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#endif

namespace zitter::prototype {
namespace {
std::u16string Fold(std::u16string text) {
  text = base::i18n::ToLower(text);
  constexpr std::u16string_view accents = u"ąćęłńóśźż";
  constexpr std::u16string_view ascii = u"acelnoszz";
  for (auto& letter : text) {
    const auto index = accents.find(letter);
    if (index != std::u16string_view::npos) letter = ascii[index];
  }
  return text;
}
}  // namespace

std::vector<PaletteResult> WorkspaceWindow::CommandResults(std::u16string query) const {
  std::vector<PaletteResult> results;
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return results;
  const auto source = active_id();
  auto* page = contents(source);
  const bool only = query.starts_with(u">");
  if (only) query.erase(0, 1);
  query = Fold(std::u16string(base::TrimWhitespace(query, base::TRIM_ALL)));
  const auto words = base::SplitString(query, u" \t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  // Binds an entry to the current page/tab and appends its configured chord.
  auto bind = [&](std::string id, std::u16string title, std::u16string detail, std::string target) {
    PaletteResult result;
    result.title = std::move(title); result.detail = std::move(detail);
    result.workspace_id = source; result.command_id = std::move(id); result.target_workspace_id = std::move(target);
    // Bind page actions to the selected tab, so a delayed click cannot act on a replacement.
    for (const auto& host : hosts_) if (host.workspace_id == source) result.tab = host.active_tab;
    if (page) {
      result.url = page->GetVisibleURL();
      result.command_page_id = sessions::SessionTabHelper::IdForTab(page).id();
    }
    for (const auto& shortcut : shell_->browser_tools().shortcuts) if (shortcut.id == result.command_id) {
      const auto chord = FormatShortcut(shortcut.accelerator);
      if (!chord.empty()) result.detail += u" · " + base::UTF8ToUTF16(chord);
    }
    results.push_back(std::move(result));
  };
  auto has_closed = [&] {
    const auto* space_data = time_machine_.FindDict(source);
    const auto* closed = space_data ? space_data->FindList("closed") : nullptr;
    return closed && std::any_of(closed->begin(), closed->end(), [&](const auto& value) {
      const auto* entry = value.GetIfDict();
      const auto* profile = entry ? entry->FindString("profile_id") : nullptr;
      return profile && *profile == model_.active()->profile_id;
    });
  };
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal") {
    // Rust owns the catalogue, wording and matching; native code supplies facts.
    sidebar::CommandContext context{};
    context.page = page; context.http_page = page && page->GetVisibleURL().SchemeIsHTTPOrHTTPS(); context.split = split();
    context.can_back = page && page->GetController().CanGoBack(); context.can_forward = page && page->GetController().CanGoForward();
    context.tab_count = tab_count(source); context.reopen = has_closed();
    for (const auto& workspace : model_.workspaces()) {
      if (workspace.id == source) continue;
      const auto pages = WorkspacePages(workspace.id);
      context.workspaces.push_back({workspace.id, workspace.name, workspace_sleeping(workspace.id), workspace.profile_id == model_.active()->profile_id,
          std::any_of(pages.begin(), pages.end(), [](auto* page) { return PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured(); })});
    }
    std::u16string raw = only ? u">" + query : query;
    for (const auto& entry : sidebar::command_catalogue(context, base::UTF16ToUTF8(raw)))
      bind(std::string(entry.id), base::UTF8ToUTF16(std::string(entry.title)), base::UTF8ToUTF16(std::string(entry.detail)), std::string(entry.target));
    return results;
  }
#endif
  auto add = [&](std::string id, std::u16string title, std::u16string detail,
                 std::u16string aliases = {}, std::string target = {}) {
    const auto haystack = Fold(title + u" " + detail + u" " + aliases);
    if (!std::all_of(words.begin(), words.end(), [&](const auto& word) { return haystack.find(word) != std::u16string::npos; })) return;
    bind(std::move(id), std::move(title), u"Polecenie · " + detail, std::move(target));
  };
  add("new_tab", u"Nowa karta", u"Otwórz pasek nowej karty", u"new tab");
  add("save_snapshot", u"Zapisz snapshot przestrzeni", u"Szybki punkt przywracania w Time Machine", u"zapis sesji save snapshot backup");
  add("timeline", u"Time Machine", u"Snapshoty i zamknięte karty", u"przywróć restore historia sesji");
  add("settings", u"Ustawienia przeglądarki", u"Wygląd, prywatność i ustawienia profilu", u"settings preferences");
  add("shortcuts", u"Konfiguruj skróty klawiszowe", u"Nagraj lub zmień kombinacje klawiszy", u"keyboard shortcuts");
  add("engines", u"Konfiguruj wyszukiwarki", u"Prefiksy i domyślna wyszukiwarka", u"search engines");
  add("sidebar", u"Pokaż / ukryj panel boczny", u"Karty i przestrzenie", u"sidebar menu");
  add("spaces", u"Zarządzaj przestrzeniami", u"Dodaj lub edytuj space i przypisanie profilu", u"workspace spaces");
  add("profiles", u"Zarządzaj profilami", u"Dodaj profil i zarządzaj jego danymi", u"profile accounts");
  add("import", u"Importuj z przeglądarki", u"Wybierz przeglądarkę i zakres importu", u"import arc chrome edge brave zakladki profile");
  add("history", u"Historia przeglądania", u"Przeglądaj, szukaj i czyść historię profilu", u"history visited historia wyczysc");
  add("downloads", u"Pobrania", u"Postęp, pauza, wznowienie i Finder", u"downloads pobierz pliki transfer");
  if (page) {
    add("split", split() ? u"Zakończ Split View" : u"Otwórz Split View", u"Dwie karty obok siebie", u"split podziel porównaj compare");
    add("reload", u"Odśwież kartę", u"Załaduj bieżącą stronę ponownie", u"reload refresh");
    if (page->GetController().CanGoBack()) add("back", u"Wstecz", u"Poprzednia strona tej karty", u"back");
    if (page->GetController().CanGoForward()) add("forward", u"Dalej", u"Następna strona tej karty", u"forward");
    add("passwords", u"Hasła tego profilu", u"Zapisane logowania", u"passwords logins");
    if (tab_count(source) > 1) add("close_tab", u"Zamknij bieżącą kartę", u"Możesz ją odzyskać w Time Machine", u"close tab");
    if (split()) {
      add("left_pane", u"Przejdź do lewej karty Split View", u"Ustaw aktywną stronę", u"focus left pane");
      add("right_pane", u"Przejdź do prawej karty Split View", u"Ustaw aktywną stronę", u"focus right pane");
    }
  }
  if (has_closed()) add("reopen", u"Przywróć zamkniętą kartę", u"Ostatnia karta w tym profilu i space", u"reopen closed tab");
  bool awake_background = false;
  for (const auto& workspace : model_.workspaces()) {
    if (workspace.id == source) continue;
    const auto name = base::UTF8ToUTF16(workspace.name);
    const bool asleep = workspace_sleeping(workspace.id);
    add("select_space", (asleep ? u"Obudź przestrzeń: " : u"Przełącz przestrzeń: ") + name,
        asleep ? u"Przywróć zapisany układ kart" : u"Przejdź do space", u"switch space workspace wake", workspace.id);
    if (page && workspace.profile_id == model_.active()->profile_id) {
      add("move_tab", u"Przenieś kartę do: " + name, u"Ten sam profil · zachowuje formularze i historię karty", u"move tab przenies karte", workspace.id);
    } else if (page && page->GetVisibleURL().SchemeIsHTTPOrHTTPS()) {
      add("copy_tab", u"Otwórz kopię karty w: " + name, u"Inny profil · nowe logowanie i dane strony", u"copy tab skopiuj karte", workspace.id);
    }
    const auto pages = WorkspacePages(workspace.id);
    const bool capturing = std::any_of(pages.begin(), pages.end(), [](auto* page) { return PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured(); });
    awake_background |= !asleep && !capturing;
    if (!asleep && !capturing) add("sleep_space", u"Uśpij przestrzeń: " + name,
        u"Zwolnij pamięć · strony przeładują się po obudzeniu; zapisz formularze", u"sleep space memory", workspace.id);
  }
  if (awake_background) add("sleep_others", u"Uśpij pozostałe przestrzenie",
      u"Pomiń aktywne nagrywanie · inne strony przeładują się; zapisz formularze", u"sleep other spaces memory pamiec");
  // The initial view remains compact; > reveals the full action catalogue.
  if (query.empty() && !only && results.size() > 6) results.resize(6);
  return results;
}

bool WorkspaceWindow::ExecuteCommand(const PaletteResult& result) {
  if (result.workspace_id != active_id() || result.command_id.empty()) return false;
  const auto available = CommandResults(u">");
  const auto found = std::find_if(available.begin(), available.end(), [&](const auto& current) {
    return current.command_id == result.command_id && current.target_workspace_id == result.target_workspace_id &&
        current.tab == result.tab && current.url == result.url && current.command_page_id == result.command_page_id;
  });
  if (found == available.end()) return false;
  const auto& id = result.command_id;
  if (id == "shortcuts" || id == "engines") { shell_->OpenTools(id == "shortcuts" ? 1 : 2); return true; }
  if (id == "spaces") { shell_->OpenWorkspaceSettings(); return true; }
  if (id == "profiles") { shell_->OpenProfiles(); return true; }
  if (id == "import") { shell_->OpenImport(); return true; }
  if (id == "downloads") { ShowDownloads(); return true; }
  if (id == "history") { shell_->OpenTools(3); return true; }
  if (id == "passwords") { ShowPasswords(); return true; }
  if (id == "save_snapshot") {
    if (!SaveSnapshot("Command Center")) return false;
    shell_->OpenTools(0); return true;
  }
  if (id == "select_space") return Select(result.target_workspace_id);
  if (id == "move_tab") return MoveActiveTab(result.target_workspace_id);
  if (id == "copy_tab") {
    // Explicit target, bypass domain routing: the command names the destination profile.
    if (DeferForProfile(result.target_workspace_id, base::BindOnce(
        [](base::WeakPtr<WorkspaceWindow> w, PaletteResult result) { if (w) w->ExecuteCommand(result); },
        weak_factory_.GetWeakPtr(), result))) return true;
    if (!Select(result.target_workspace_id)) return false;
    for (auto& host : hosts_) if (host.workspace_id == active_id()) {
      auto* view = CreatePage(host.profile); host.tabs.push_back(view);
      host.deferred_urls[view] = result.url;
      SelectTab(host.tabs.size() - 1); return true;
    }
    return false;
  }
  if (id == "sleep_space") return SleepWorkspace(result.target_workspace_id);
  if (id == "sleep_others") {
    std::vector<content::WebContents*> pages;
    for (const auto& workspace : model_.workspaces()) if (workspace.id != active_id()) {
      auto space_pages = WorkspacePages(workspace.id);
      if (std::any_of(space_pages.begin(), space_pages.end(), [](auto* page) { return PageMediaAccess::IsCapturing(page) || page->IsBeingCaptured(); })) continue;
      pages.insert(pages.end(), space_pages.begin(), space_pages.end());
    }
    if (GuardPages(std::move(pages), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, PaletteResult result) { if (w) w->ExecuteCommand(result); }, weak_factory_.GetWeakPtr(), result))) return true;
    const auto workspaces = model_.workspaces(); bool changed = false;
    for (const auto& workspace : workspaces) if (workspace.id != active_id() && !workspace_sleeping(workspace.id))
      changed |= SleepWorkspace(workspace.id);
    return changed;
  }
  return RunShortcut(id);
}

bool WorkspaceWindow::MoveActiveTab(std::string target_id) {
  if (!configuration_ready_ || profiles_busy_ || target_id == active_id()) return false;
  Host* source = nullptr; Host* target = nullptr;
  for (auto& host : hosts_) {
    if (host.workspace_id == active_id()) source = &host;
    if (host.workspace_id == target_id) target = &host;
  }
  if (!source || !target || source->profile != target->profile || !source->view) return false;
  if (target->sleeping) {
    const auto stored = target->sleeping->Clone();
    if (!RestoreHost(*target, stored)) return false;
  }
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string target) { if (w) w->MoveActiveTab(target); }, weak_factory_.GetWeakPtr(), target_id))) return true;
  ClosePeek();
  SaveSnapshot("Przed przeniesieniem karty", true);
  auto* moving = source->view.get();
  shell_->SetPanes(nullptr, nullptr, 0.5);
  const auto pending = source->deferred_urls.find(moving);
  if (pending != source->deferred_urls.end()) { target->deferred_urls[moving] = pending->second; source->deferred_urls.erase(pending); }
  if (auto title = source->deferred_titles.find(moving); title != source->deferred_titles.end()) {
    target->deferred_titles[moving] = std::move(title->second);
    source->deferred_titles.erase(title);
  }
  source->popup_tabs.erase(moving);
  std::erase_if(source->pin_tabs, [&](const auto& item) { return item.second == moving; });
  std::erase(source->tabs, moving);
  if (source->tabs.empty()) source->tabs.push_back(CreatePage(source->profile));
  if (source->left == moving) { source->left = source->right; source->right = nullptr; }
  if (source->right == moving) source->right = nullptr;
  if (!source->left) source->left = source->tabs.front();
  source->view = source->left;
  source->active_tab = std::find(source->tabs.begin(), source->tabs.end(), source->view) - source->tabs.begin();
  const bool focused_right = target->right && target->view == target->right;
  target->tabs.push_back(moving);
  target->active_tab = target->tabs.size() - 1;
  target->view = moving;
  // Keep an existing split, replacing only its focused pane.
  if (focused_right) target->right = moving; else target->left = moving;
  return Select(target_id);
}
}  // namespace zitter::prototype
