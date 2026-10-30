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
void WorkspaceWindow::RequestClearBrowsingData(BrowsingDataSelection selection) {
  if (clear_data_dialog_) { clear_data_dialog_->Activate(); return; }
  if (!configuration_ready_ || profiles_busy_ || !model_.active() || data_remover_) {
    shell_->ReportClearData(WorkspaceShell::ClearDataEvent::kBusy, {});
    return;
  }
  if ((!selection.history && !selection.cookies && !selection.cache) || selection.period < 0 || selection.period > 3) {
    shell_->ReportClearData(WorkspaceShell::ClearDataEvent::kNothingSelected, {});
    return;
  }
  const std::string id = model_.active()->profile_id;
  std::u16string name;
  for (const auto& profile : profiles_) if (profile.info.id == id) name = base::UTF8ToUTF16(profile.info.name);
  const auto copy = shell_->ClearDataConfirmCopy(name, selection.period, selection.history, selection.cookies, selection.cache);
  if (!copy.valid) return;
  const auto generation = ++clear_data_dialog_generation_;
  auto clear_dialog = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, size_t generation) {
    if (window && window->clear_data_dialog_generation_ == generation) window->clear_data_dialog_.reset();
  }, weak_factory_.GetWeakPtr(), generation);
  auto model = ui::DialogModel::Builder()
      .SetTitle(copy.title)
      .AddParagraph(ui::DialogModelLabel(copy.description))
      .AddOkButton(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, base::RepeatingClosure clear_dialog, std::string id, BrowsingDataSelection selection) {
        clear_dialog.Run();
        if (window) window->ClearBrowsingData(std::move(id), selection);
      }, weak_factory_.GetWeakPtr(), clear_dialog, id, selection), ui::DialogModel::Button::Params().SetLabel(copy.confirm))
      .AddCancelButton(base::BindOnce(clear_dialog), ui::DialogModel::Button::Params().SetLabel(copy.cancel))
      .SetCloseActionCallback(base::BindOnce(clear_dialog))
      .SetDialogDestroyingCallback(base::BindOnce(clear_dialog))
      .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
      .Build();
  auto dialog = views::BubbleDialogModelHost::CreateModal(std::move(model), ui::mojom::ModalType::kWindow);
  auto* parent = shell_->general_widget();
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
    shell_->ReportClearData(WorkspaceShell::ClearDataEvent::kHistoryForbidden, {});
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
  shell_->ReportClearData(WorkspaceShell::ClearDataEvent::kClearing, data_removal_profile_name_);
  data_remover_->RemoveAndReply(begin, base::Time::Max(), mask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB | content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB, this);
  return true;
}
void WorkspaceWindow::OnBrowsingDataRemoverDone(uint64_t failed_data_types) {
  data_remover_->RemoveObserver(this);
  data_remover_ = nullptr;
  shell_->SetBrowsingDataBusy(false);
  data_removal_lease_.reset();
  shell_->ReportClearData(failed_data_types ? WorkspaceShell::ClearDataEvent::kFailed : WorkspaceShell::ClearDataEvent::kCleared, data_removal_profile_name_);
  history_tasks_.TryCancelAll();
}
}  // namespace zitter::prototype
