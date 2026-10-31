// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/password_csv_import.h"

#include <memory>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/affiliations/affiliation_service_factory.h"
#include "chrome/browser/password_manager/factories/account_password_store_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chromium/prototype/i18n.h"
#include "components/affiliations/core/browser/affiliation_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/import/import_results.h"
#include "components/password_manager/core/browser/import/password_importer.h"
#include "components/password_manager/core/browser/ui/saved_passwords_presenter.h"
#include "content/public/browser/web_contents.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::prototype {
namespace {
// Chromium's own import path (StartupPasswordsImporter): a presenter over the
// profile's stores, and the importer that parses the CSV in a sandboxed
// utility process and adds what isn't saved yet.
class CsvImport final : public ui::SelectFileDialog::Listener {
 public:
  CsvImport(content::WebContents* contents, base::OnceCallback<void(PasswordCsvOutcome)> done)
      : profile_(Profile::FromBrowserContext(contents->GetBrowserContext())), done_(std::move(done)) {
    dialog_ = ui::SelectFileDialog::Create(this, std::make_unique<ChromeSelectFilePolicy>(contents));
    ui::SelectFileDialog::FileTypeInfo types;
    types.extensions = {{FILE_PATH_LITERAL("csv")}};
    types.include_all_files = true;
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE, T(u"Wybierz plik CSV z hasłami"), base::FilePath(),
                        &types, 1, FILE_PATH_LITERAL("csv"), contents->GetTopLevelNativeWindow());
  }
  ~CsvImport() override {
    if (dialog_) dialog_->ListenerDestroyed();
  }

  void FileSelected(const ui::SelectedFileInfo& file, int) override {
    presenter_ = std::make_unique<password_manager::SavedPasswordsPresenter>(
        AffiliationServiceFactory::GetForProfile(profile_),
        ProfilePasswordStoreFactory::GetForProfile(profile_, ServiceAccessType::EXPLICIT_ACCESS),
        AccountPasswordStoreFactory::GetForProfile(profile_, ServiceAccessType::EXPLICIT_ACCESS));
    importer_ = std::make_unique<password_manager::PasswordImporter>(*presenter_);
    // The importer compares with what is saved, so the presenter reads first.
    presenter_->Init(base::BindOnce(&CsvImport::Import, base::Unretained(this), file.path()));
  }
  void FileSelectionCanceled() override { Finish({.cancelled = true}); }

 private:
  void Import(base::FilePath path) {
    importer_->Import(path, password_manager::PasswordForm::Store::kProfileStore,
                      base::BindOnce(&CsvImport::Imported, base::Unretained(this)));
  }
  void Imported(const password_manager::ImportResults& results) {
    Finish({.imported = results.status == password_manager::ImportResults::Status::SUCCESS,
            .count = results.number_imported});
  }
  void Finish(PasswordCsvOutcome outcome) {
    std::move(done_).Run(outcome);
    delete this;
  }

  raw_ptr<Profile> profile_;
  base::OnceCallback<void(PasswordCsvOutcome)> done_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  std::unique_ptr<password_manager::SavedPasswordsPresenter> presenter_;
  std::unique_ptr<password_manager::PasswordImporter> importer_;
};
}  // namespace

void ImportPasswordsFromCsv(content::WebContents* contents, base::OnceCallback<void(PasswordCsvOutcome)> done) {
  new CsvImport(contents, std::move(done));
}
}  // namespace zitter::prototype
