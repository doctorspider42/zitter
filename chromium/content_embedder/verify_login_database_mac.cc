// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <dlfcn.h>
#include <memory>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/password_manager/core/browser/password_store/login_database.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "cookie_encryption_mac.h"
#include "crypto/apple/scoped_fake_keychain_v2.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace zitter::content_embedder {
namespace {

constexpr char kOrigin[] = "https://example.test/";
constexpr char kPassword[] = "zitter-test-password";
constexpr char16_t kPassword16[] = u"zitter-test-password";

scoped_refptr<os_crypt_async::Encryptor> GetEncryptor(
    std::unique_ptr<CookieEncryptionMac> encryption) {
  CHECK(encryption);
  base::RunLoop loop;
  scoped_refptr<os_crypt_async::Encryptor> result;
  encryption->GetEncryptor(base::BindOnce(
      [](scoped_refptr<os_crypt_async::Encryptor>* result,
         base::OnceClosure quit,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        *result = std::move(encryptor);
        std::move(quit).Run();
      },
      &result, loop.QuitClosure()));
  loop.Run();
  CHECK(result && result->IsEncryptionAvailable());
  return result;
}

std::unique_ptr<password_manager::LoginDatabase> OpenDatabase(
    const base::FilePath& path,
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  auto database = std::make_unique<password_manager::LoginDatabase>(
      path, password_manager::IsAccountStore(false),
      password_manager::LoginDatabase::DeletingUndecryptablePasswordsEnabled(
          false));
  CHECK(database->Init({}, std::move(encryptor)));
  return database;
}

std::string ReadCiphertext(const base::FilePath& path) {
  sql::Database database(sql::DatabaseOptions().set_read_only(true),
                         "Passwords");
  CHECK(database.Open(path));
  sql::Statement query(database.GetUniqueStatement(
      "SELECT password_value FROM logins"));
  CHECK(query.Step());
  auto ciphertext = query.ColumnBlobAsString(0);
  CHECK(!query.Step() && query.Succeeded());
  CHECK(ciphertext.find(kPassword) == std::string::npos);
  return ciphertext;
}

void VerifyCopy(const base::FilePath& directory,
                base::RepeatingCallback<std::unique_ptr<CookieEncryptionMac>()>
                    open_encryption,
                scoped_refptr<os_crypt_async::Encryptor> wrong_encryptor,
                std::string_view tag) {
  CHECK(base::CreateDirectory(directory.AppendASCII("original")));
  CHECK(base::CreateDirectory(directory.AppendASCII("candidate")));
  const auto original =
      directory.AppendASCII("original").AppendASCII("Login Data");
  const auto candidate =
      directory.AppendASCII("candidate").AppendASCII("Login Data");
  {
    auto database = OpenDatabase(original, GetEncryptor(open_encryption.Run()));
    password_manager::StoredCredential credential;
    credential.signon_realm = kOrigin;
    credential.url = GURL(kOrigin);
    credential.username_value = u"test-user";
    credential.password_value =
        password_manager::PasswordString(std::u16string(kPassword16));
    CHECK(!database->AddLogin(std::move(credential)).empty());
  }
  const auto ciphertext = ReadCiphertext(original);
  CHECK(ciphertext.starts_with(tag));
  CHECK(base::CopyFile(original, candidate));
  {
    auto database = OpenDatabase(candidate, std::move(wrong_encryptor));
    std::vector<password_manager::StoredCredential> logins;
    CHECK(database->GetAllLogins(&logins) ==
          password_manager::FormRetrievalResult::kEncryptionServiceFailure);
    CHECK(logins.empty());
  }
  {
    auto database = OpenDatabase(candidate, GetEncryptor(open_encryption.Run()));
    std::vector<password_manager::StoredCredential> logins;
    CHECK(database->GetAllLogins(&logins) ==
          password_manager::FormRetrievalResult::kSuccess);
    CHECK_EQ(logins.size(), 1u);
    CHECK_EQ(logins[0].signon_realm, kOrigin);
    CHECK(logins[0].username_value == u"test-user");
    CHECK(logins[0].password_value == kPassword16);
  }
  CHECK_EQ(ReadCiphertext(original), ciphertext);
  CHECK_EQ(ReadCiphertext(candidate), ciphertext);
}

bool IsLegacyCopyRoot(const base::FilePath& root) {
  base::FilePath temp;
  int permissions = 0;
  return base::GetTempDir(&temp) && root.IsAbsolute() && !base::IsLink(root) &&
         root == base::MakeAbsoluteFilePath(root) &&
         root.DirName() == base::MakeAbsoluteFilePath(temp) &&
         root.BaseName().value().starts_with("zitter-content-login-copy-") &&
         base::GetPosixFilePermissions(root, &permissions) &&
         (permissions & 077) == 0;
}

void VerifyLegacyDatabase(const base::FilePath& root, const GURL& url,
                          bool use_file_keychain) {
  CHECK(IsLegacyCopyRoot(root));
  CHECK(url.SchemeIs("http") && url.host() == "127.0.0.1" && url.has_port() &&
        !url.has_username() && !url.has_password());
  const auto path = root.AppendASCII("Login Data");
  CHECK(base::PathExists(path) && !base::IsLink(path));
  for (const char* sidecar : {"Login Data-journal", "Login Data-wal", "Login Data-shm"}) {
    CHECK(!base::IsLink(root.AppendASCII(sidecar)));
  }
  const auto ciphertext = ReadCiphertext(path);
  CHECK(ciphertext.starts_with("v10"));
  CHECK(ciphertext.find("zitter-migration-password") == std::string::npos);

  base::ScopedTempDir wrong_key_directory;
  CHECK(wrong_key_directory.CreateUniqueTempDir());
  const auto wrong_key = wrong_key_directory.GetPath().AppendASCII("fixture");
  std::array<uint8_t, os_crypt_async::Encryptor::Key::kAES256GCMKeySize> key;
  key.fill(0xa5);
  CHECK(base::WriteFile(wrong_key, key));
  CHECK(base::SetPosixFilePermissions(wrong_key, 0600));
  {
    auto database =
        OpenDatabase(path, GetEncryptor(CookieEncryptionMac::OpenFixture(wrong_key)));
    std::vector<password_manager::StoredCredential> logins;
    CHECK(database->GetAllLogins(&logins) ==
          password_manager::FormRetrievalResult::kEncryptionServiceFailure);
    CHECK(logins.empty());
  }
  CHECK_EQ(ReadCiphertext(path), ciphertext);
  {
    std::unique_ptr<crypto::apple::ScopedFakeKeychainV2> mock_keychain;
    if (use_file_keychain) {
      auto* install = reinterpret_cast<bool (*)(const char*)>(
          dlsym(RTLD_DEFAULT, "ZitterInstallFileKeychainFixture"));
      CHECK(install && install(root.value().c_str()));
    } else {
      mock_keychain = std::make_unique<crypto::apple::ScopedFakeKeychainV2>(
          "zitter-content-login-probe");
      mock_keychain->SetFindGenericResult(noErr);
    }
    auto database = OpenDatabase(
        path, GetEncryptor(use_file_keychain
                               ? CookieEncryptionMac::UseExistingLegacyKeychain()
                               : CookieEncryptionMac::UseKeychain()));
    std::vector<password_manager::StoredCredential> logins;
    CHECK(database->GetAllLogins(&logins) ==
          password_manager::FormRetrievalResult::kSuccess);
    CHECK_EQ(logins.size(), 1u);
    CHECK_EQ(logins[0].signon_realm, url::Origin::Create(url).GetURL().spec());
    CHECK(logins[0].username_value == u"zitter-migration-user");
    CHECK(logins[0].password_value == u"zitter-migration-password");
  }
  CHECK_EQ(ReadCiphertext(path), ciphertext);
  std::puts("Content read the legacy Login Data and preserved its ciphertext");
}

void RunProbe() {
  base::ScopedTempDir directory;
  CHECK(directory.CreateUniqueTempDir());
  const auto root = directory.GetPath();
  const auto fixture_path = root.AppendASCII("fixture");
  const auto wrong_fixture_path = root.AppendASCII("wrong-fixture");
  std::array<uint8_t, os_crypt_async::Encryptor::Key::kAES256GCMKeySize> key;
  key.fill(0x16);
  CHECK(base::WriteFile(fixture_path, key));
  CHECK(base::SetPosixFilePermissions(fixture_path, 0600));
  key.fill(0x23);
  CHECK(base::WriteFile(wrong_fixture_path, key));
  CHECK(base::SetPosixFilePermissions(wrong_fixture_path, 0600));
  auto wrong_encryptor =
      GetEncryptor(CookieEncryptionMac::OpenFixture(wrong_fixture_path));
  VerifyCopy(root.AppendASCII("fixture-format"),
             base::BindRepeating(&CookieEncryptionMac::OpenFixture,
                                 fixture_path),
             wrong_encryptor, "zitter-probe-v1");

  crypto::apple::ScopedFakeKeychainV2 keychain("zitter-content-login-probe");
  keychain.SetFindGenericResult(noErr);
  VerifyCopy(root.AppendASCII("keychain-format"),
             base::BindRepeating(&CookieEncryptionMac::UseKeychain),
             std::move(wrong_encryptor), "v10");
}

}  // namespace
}  // namespace zitter::content_embedder

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  const bool legacy = command_line.HasSwitch("legacy-copy-root");
  const bool use_file_keychain = command_line.HasSwitch("file-keychain");
  CHECK((argc == 1 && !legacy) ||
        (argc == (use_file_keychain ? 4 : 3) && legacy &&
         command_line.HasSwitch("legacy-origin")));
  base::FeatureList::InitInstance("ClearUndecryptablePasswords",
                                  "SkipUndecryptablePasswords");
  CHECK(base::i18n::InitializeICU());
  base::SingleThreadTaskExecutor executor;
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("LoginDataProbe");
  if (legacy) {
    zitter::content_embedder::VerifyLegacyDatabase(
        command_line.GetSwitchValuePath("legacy-copy-root"),
        GURL(command_line.GetSwitchValueASCII("legacy-origin")),
        use_file_keychain);
  } else {
    zitter::content_embedder::RunProbe();
    std::puts("Content Login Data copy, ciphertext and wrong-key checks passed");
  }
  base::ThreadPoolInstance::Get()->Shutdown();
}
