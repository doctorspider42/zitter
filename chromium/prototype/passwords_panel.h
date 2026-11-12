#ifndef ZITTER_CHROMIUM_PROTOTYPE_PASSWORDS_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PASSWORDS_PANEL_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "url/gurl.h"

namespace device_reauth { class DeviceAuthenticator; }
namespace views { class Widget; class View; class DialogDelegate; }
namespace zitter::prototype {
class EditorControl;
class PanelScene;
class PanelViewport;
// Design: Menedżer haseł — the profile's saved passwords by site with search,
// a page per password (showing or copying it asks the system first), the
// checkup (leaked, reused, weak), the never-saved sites, and adding one.
// Chromium's password store owns the data; this is presentation only.
class PasswordsPanel final : public password_manager::PasswordStoreConsumer,
                             public password_manager::PasswordStoreInterface::Observer {
 public:
  enum class Page { kList, kCheckup, kNever, kDetail, kAdd };
  using Confirm = base::RepeatingCallback<void(views::Widget* parent, std::u16string title, std::u16string detail,
                                               std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer)>;
  using AuthenticatorFactory = base::RepeatingCallback<
      std::unique_ptr<device_reauth::DeviceAuthenticator>(views::Widget*)>;
  PasswordsPanel();
  ~PasswordsPanel() override;
  void Show(scoped_refptr<password_manager::PasswordStoreInterface> store,
            std::u16string profile_label,
            views::Widget* parent,
            AuthenticatorFactory authenticator_factory,
            bool activate = true);
  views::Widget* widget() const { return widget_.get(); }
  void SetOpenCallback(base::RepeatingCallback<void(GURL)> open) { open_ = std::move(open); }
  void SetConfirmCallback(Confirm confirm) { confirm_ = std::move(confirm); }
  void SetImportCallback(base::RepeatingClosure import) { import_ = std::move(import); }
  void SetPage(Page page);
  void SetQuery(std::u16string query);
  // Opens the page of the listed credential at `index`.
  void OpenDetail(size_t index);
  // Showing or copying a password asks the system (Touch ID or the account password).
  void RevealPassword(bool copy);
  // Adds a password for `site` from the Add page's fields.
  bool AddPassword(std::u16string site, std::u16string username, std::u16string password);
  bool ChangePassword(std::u16string password);
  void DeleteDetail();
  // Testing.
  void SkipAuthenticationForTesting() { skip_auth_ = true; }
  bool loaded_for_testing() const { return loaded_; }
  std::vector<std::u16string> listed_for_testing() const;
  bool revealed_for_testing() const { return revealed_; }
  bool scene_contains_for_testing(std::string_view text) const;
  int issues_for_testing(int kind) const;  // 0 leaked, 1 reused, 2 weak.
  size_t never_count_for_testing() const { return blocked_.size(); }
  Page page() const { return page_; }
  // password_manager::PasswordStoreConsumer:
  void OnGetPasswordStoreResultsOrErrorFrom(password_manager::PasswordStoreInterface* store,
      base::expected<std::vector<password_manager::StoredCredential>, password_manager::PasswordStoreBackendError> results) override;
  // password_manager::PasswordStoreInterface::Observer:
  void OnLoginsChanged(password_manager::PasswordStoreInterface*, const password_manager::PasswordStoreChangeList&) override;
  void OnLoginsRetained(password_manager::PasswordStoreInterface*, const std::vector<password_manager::StoredCredential>&) override;
 private:
  struct Issues { bool leaked = false, reused = false, weak = false; };
  void CloseUI();
  void Load();
  void Rebuild();
  void Refresh();
  views::View* MakeLine(views::View* parent, uint32_t style, int tone);
  views::View* AddRow(views::View* parent, std::u16string title, std::u16string meta, std::u16string initial, base::RepeatingClosure pressed);
  std::vector<size_t> Listed() const;
  Issues IssuesOf(const password_manager::StoredCredential& credential) const;
  void Authenticated(bool copy, bool success);
  void CancelAuthentication();
  scoped_refptr<password_manager::PasswordStoreInterface> store_;
  std::vector<password_manager::StoredCredential> credentials_, blocked_;
  bool loaded_ = false, revealed_ = false, skip_auth_ = false, editing_ = false;
  Page page_ = Page::kList;
  std::u16string query_;
  std::optional<size_t> detail_;
  AuthenticatorFactory authenticator_factory_;
  std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator_;
  bool authentication_pending_ = false;
  base::RepeatingCallback<void(GURL)> open_;
  Confirm confirm_;
  base::RepeatingClosure import_;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> scroll_ = nullptr;
  raw_ptr<views::View> body_ = nullptr, count_ = nullptr, add_ = nullptr, search_row_ = nullptr;
  raw_ptr<EditorControl> search_ = nullptr;
  std::array<raw_ptr<views::View>, 3> tabs_ = {};
  // The Add and Edit fields live as long as their page.
  std::vector<raw_ptr<EditorControl>> fields_;
  base::WeakPtrFactory<PasswordsPanel> authentication_weak_{this};
  base::WeakPtrFactory<PasswordsPanel> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
