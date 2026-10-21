#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "chromium/prototype/workspace_window.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/ui/webui/settings/settings_utils.h"
#include "chrome/common/pref_names.h"
#include "chromium/prototype/browser_preferences.h"
#include "components/prefs/pref_service.h"
#include "components/history/core/common/pref_names.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::prototype {
namespace {
void PrepareSettingsDialog(views::View* view) {
  if (auto* label = views::AsViewClass<views::Label>(view)) label->SetSubpixelRenderingEnabled(false);
  for (const auto& child : view->children()) PrepareSettingsDialog(child);
}
}  // namespace

bool WorkspaceWindow::WriteTextPreference(std::string key, std::string value) {
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return false;
  auto* profile = ProfileFor(model_.active()->profile_id);
  return profile && WriteChromiumTextPreference(profile, g_browser_process->local_state(), key, std::move(value));
}
std::vector<SitePreferenceRule> WorkspaceWindow::ReadSitePreferences() const {
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return {};
  auto* profile = ProfileFor(model_.active()->profile_id);
  return profile ? ReadChromiumSitePreferences(profile) : std::vector<SitePreferenceRule>();
}
bool WorkspaceWindow::WriteSitePreference(std::string origin, std::string key, int setting) {
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return false;
  auto* profile = ProfileFor(model_.active()->profile_id);
  return profile && WriteChromiumSitePreference(profile, std::move(origin), key, setting);
}
bool WorkspaceWindow::RemoveSitePreference(SitePreferenceRule rule) {
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return false;
  auto* profile = ProfileFor(model_.active()->profile_id);
  return profile && RemoveChromiumSitePreference(profile, rule);
}
void WorkspaceWindow::OpenSystemSettings(bool certificates) {
  auto* page = contents(active_id());
  if (!page) return;
  if (certificates) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    settings_utils::ShowManageSSLCertificates(page);
#endif
  } else {
    settings_utils::ShowNetworkProxySettings(page);
  }
}
void WorkspaceWindow::QueryBrowsingHistory(std::u16string query, size_t limit) {
  history_view_tasks_.TryCancelAll();
  const auto generation = ++history_view_generation_;
  if (!model_.active() || !shell_->history_visible()) return;
  const auto profile_id = model_.active()->profile_id;
  auto* profile = ProfileFor(profile_id);
  auto* service = profile ? HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS) : nullptr;
  if (!service) { shell_->SetHistoryResults(query, profile_id, limit, {}, false, true); return; }
  history::QueryOptions options;
  options.max_count = static_cast<int>(std::min(limit, size_t{1000000}));
  options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
  options.policy_for_404_visits = history::VisitQuery404sPolicy::kInclude404s;
  service->QueryHistory(query, options, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, size_t generation, std::u16string query,
         std::string profile_id, size_t limit, history::QueryResults results) {
        if (!window || generation != window->history_view_generation_) return;
        std::vector<BrowsingHistoryEntry> entries;
        for (const auto& row : results) if (row.url().is_valid() && row.url().SchemeIsHTTPOrHTTPS())
          entries.push_back({row.url(), row.title(), row.visit_time()});
        window->shell_->SetHistoryResults(query, profile_id, limit, std::move(entries), !results.reached_beginning(), false);
      }, weak_factory_.GetWeakPtr(), generation, query, profile_id, limit), &history_view_tasks_);
}
void WorkspaceWindow::RequestClearBrowsingData(BrowsingDataSelection selection) {
  if (clear_data_dialog_) { clear_data_dialog_->Activate(); return; }
  if (!configuration_ready_ || profiles_busy_ || !model_.active() || data_remover_) {
    shell_->SetPreferencesStatus(u"Poczekaj na zakończenie bieżącej operacji.", true);
    return;
  }
  if ((!selection.history && !selection.cookies && !selection.cache) || selection.period < 0 || selection.period > 3) {
    shell_->SetPreferencesStatus(u"Wybierz co najmniej jeden rodzaj danych i zakres czasu.", true);
    return;
  }
  const std::string id = model_.active()->profile_id;
  std::u16string name;
  for (const auto& profile : profiles_) if (profile.info.id == id) name = base::UTF8ToUTF16(profile.info.name);
  const std::array<std::u16string, 4> periods = {u"Ostatnia godzina", u"Ostatnie 24 godziny", u"Ostatnie 7 dni", u"Cały okres"};
  std::u16string description = u"Profil: " + name + u"\nZakres: " + periods[selection.period] + u"\n\nZostaną usunięte:";
  if (selection.history) description += u"\n• Historia przeglądania";
  if (selection.cookies) description += u"\n• Cookies i dane witryn — może to wylogować Cię z serwisów";
  if (selection.cache) description += u"\n• Pamięć podręczna";
  description += u"\n\nTej operacji nie można cofnąć. Hasła, przypięte karty i pobrane pliki pozostaną zachowane.";
  const auto generation = ++clear_data_dialog_generation_;
  auto clear_dialog = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, size_t generation) {
    if (window && window->clear_data_dialog_generation_ == generation) window->clear_data_dialog_.reset();
  }, weak_factory_.GetWeakPtr(), generation);
  auto model = ui::DialogModel::Builder()
      .SetTitle(u"Wyczyścić dane przeglądania?")
      .AddParagraph(ui::DialogModelLabel(description))
      .AddOkButton(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, base::RepeatingClosure clear_dialog, std::string id, BrowsingDataSelection selection) {
        clear_dialog.Run();
        if (window) window->ClearBrowsingData(std::move(id), selection);
      }, weak_factory_.GetWeakPtr(), clear_dialog, id, selection), ui::DialogModel::Button::Params().SetLabel(u"Wyczyść dane"))
      .AddCancelButton(base::BindOnce(clear_dialog), ui::DialogModel::Button::Params().SetLabel(u"Anuluj"))
      .SetCloseActionCallback(base::BindOnce(clear_dialog))
      .SetDialogDestroyingCallback(base::BindOnce(clear_dialog))
      .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
      .Build();
  auto dialog = views::BubbleDialogModelHost::CreateModal(std::move(model), ui::mojom::ModalType::kWindow);
  auto* parent = shell_->history_visible() ? shell_->tools_widget() : shell_->general_widget();
  auto* prompt = views::DialogDelegate::CreateDialogWidget(std::move(dialog), parent->GetNativeWindow(), parent->GetNativeView());
  prompt->SetColorModeOverride(parent->color_mode_override());
  PrepareSettingsDialog(prompt->GetRootView());
  clear_data_dialog_ = prompt->GetWeakPtr();
  prompt->Show();
}
bool WorkspaceWindow::ClearBrowsingData(std::string profile_id, BrowsingDataSelection selection) {
  if (!configuration_ready_ || profiles_busy_ || data_remover_ || selection.period < 0 || selection.period > 3 ||
      (!selection.history && !selection.cookies && !selection.cache)) return false;
  auto* profile = ProfileFor(profile_id);
  if (!profile) return false;
  if (selection.history && !profile->GetPrefs()->GetBoolean(prefs::kAllowDeletingBrowserHistory)) {
    shell_->SetPreferencesStatus(u"Zasady tego profilu nie pozwalają usuwać historii.", true);
    return false;
  }
  uint64_t mask = 0;
  if (selection.history) mask |= chrome_browsing_data_remover::DATA_TYPE_HISTORY;
  if (selection.cookies) mask |= chrome_browsing_data_remover::DATA_TYPE_SITE_DATA;
  if (selection.cache) mask |= content::BrowsingDataRemover::DATA_TYPE_CACHE;
  base::Time begin;
  const auto now = base::Time::Now();
  if (selection.period == 0) begin = now - base::Hours(1);
  if (selection.period == 1) begin = now - base::Days(1);
  if (selection.period == 2) begin = now - base::Days(7);
  data_removal_profile_name_ = base::UTF8ToUTF16(profile->GetPrefs()->GetString(prefs::kProfileName));
  for (const auto& info : profiles_) if (info.info.id == profile_id) data_removal_profile_name_ = base::UTF8ToUTF16(info.info.name);
  data_removal_lease_ = std::make_unique<ScopedProfileKeepAlive>(profile, ProfileKeepAliveOrigin::kBrowserWindow);
  data_remover_ = profile->GetBrowsingDataRemover();
  data_remover_->AddObserver(this);
  shell_->SetBrowsingDataBusy(true);
  shell_->SetPreferencesStatus(u"Czyszczenie danych profilu „" + data_removal_profile_name_ + u"”…");
  data_remover_->RemoveAndReply(begin, base::Time::Max(), mask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB | content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB, this);
  return true;
}
void WorkspaceWindow::OnBrowsingDataRemoverDone(uint64_t failed_data_types) {
  data_remover_->RemoveObserver(this);
  data_remover_ = nullptr;
  shell_->SetBrowsingDataBusy(false);
  data_removal_lease_.reset();
  shell_->SetPreferencesStatus(failed_data_types ? u"Nie udało się usunąć wszystkich wybranych danych. Spróbuj ponownie." :
      u"Wyczyszczono dane profilu „" + data_removal_profile_name_ + u"”.", failed_data_types != 0);
  history_tasks_.TryCancelAll();
  if (shell_->history_visible()) shell_->RefreshHistory();
}
}  // namespace zitter::prototype
