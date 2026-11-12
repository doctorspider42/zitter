// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "cookie_encryption_mac.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "components/os_crypt/async/browser/key_provider.h"
#include "components/os_crypt/async/browser/keychain_key_provider.h"
#include "components/os_crypt/async/common/algorithm.mojom.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "cookie_store_verifier.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace zitter::content_embedder {
namespace {

using Key = os_crypt_async::Encryptor::Key;
using Providers =
    std::vector<std::pair<os_crypt_async::OSCryptAsync::Precedence,
                          std::unique_ptr<os_crypt_async::KeyProvider>>>;

constexpr char kProbeKeyTag[] = "zitter-probe-v1";
class FixtureKeyProvider final : public os_crypt_async::KeyProvider {
 public:
  explicit FixtureKeyProvider(base::span<const uint8_t> key)
      : key_(key.begin(), key.end()) {}

  void GetKey(KeyCallback callback) override {
    std::move(callback).Run(
        kProbeKeyTag,
        Key(key_, os_crypt_async::mojom::Algorithm::kAES256GCM));
  }

  bool UseForEncryption() override { return true; }

 private:
  const std::vector<uint8_t> key_;
};

Providers MakeProviders(
    std::unique_ptr<os_crypt_async::KeyProvider> key_provider) {
  Providers providers;
  providers.emplace_back(1, std::move(key_provider));
  return providers;
}

}  // namespace

// static
std::unique_ptr<CookieEncryptionMac> CookieEncryptionMac::OpenFixture(
    const base::FilePath& key_path) {
  int permissions = 0;
  std::string key;
  if (base::IsLink(key_path) ||
      !base::GetPosixFilePermissions(key_path, &permissions) ||
      (permissions & 077) != 0 ||
      !base::ReadFileToStringWithMaxSize(key_path, &key,
                                        Key::kAES256GCMKeySize) ||
      key.size() != Key::kAES256GCMKeySize) {
    return nullptr;
  }

  return std::unique_ptr<CookieEncryptionMac>(new CookieEncryptionMac(
      std::make_unique<FixtureKeyProvider>(base::as_byte_span(key))));
}

// static
std::unique_ptr<CookieEncryptionMac> CookieEncryptionMac::UseKeychain() {
  return std::unique_ptr<CookieEncryptionMac>(new CookieEncryptionMac(
      std::make_unique<os_crypt_async::KeychainKeyProvider>()));
}

// static
std::unique_ptr<CookieEncryptionMac> CookieEncryptionMac::UseExistingLegacyKeychain() {
  return std::unique_ptr<CookieEncryptionMac>(new CookieEncryptionMac(
      std::make_unique<os_crypt_async::KeychainKeyProvider>(
          "Zitter Safe Storage", "Zitter")));
}

CookieEncryptionMac::CookieEncryptionMac(
    std::unique_ptr<os_crypt_async::KeyProvider> key_provider)
    : os_crypt_(MakeProviders(std::move(key_provider))), provider_(&os_crypt_) {}

CookieEncryptionMac::~CookieEncryptionMac() = default;

void CookieEncryptionMac::GetEncryptor(
    os_crypt_async::OSCryptAsync::InitCallback done) {
  os_crypt_.GetInstance(std::move(done));
}

void CookieEncryptionMac::CheckWorkspaceStores(
    std::vector<base::FilePath> network_paths,
    std::vector<base::FilePath> login_profile_paths,
    base::OnceCallback<void(bool)> done) {
  GetEncryptor(base::BindOnce(
      [](std::vector<base::FilePath> paths, std::vector<base::FilePath> login_paths,
         base::OnceCallback<void(bool)> done,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!encryptor || !encryptor->IsEncryptionAvailable()) {
          std::move(done).Run(false);
          return;
        }
        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
            base::BindOnce(
                [](std::vector<base::FilePath> paths,
                   std::vector<base::FilePath> login_paths,
                   scoped_refptr<os_crypt_async::Encryptor> encryptor) {
                  return CheckCookieStores(paths, *encryptor) &&
                         CheckLoginStores(login_paths, *encryptor);
                },
                std::move(paths), std::move(login_paths), std::move(encryptor)),
            std::move(done));
      },
      std::move(network_paths), std::move(login_profile_paths), std::move(done)));
}

void CookieEncryptionMac::Configure(
    network::mojom::NetworkContextParams& params) {
  params.enable_encrypted_cookies = true;
  params.cookie_encryption_provider = provider_.BindNewRemote();
}

}  // namespace zitter::content_embedder
