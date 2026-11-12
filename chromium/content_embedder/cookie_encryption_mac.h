// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_COOKIE_ENCRYPTION_MAC_H_
#define ZITTER_CONTENT_COOKIE_ENCRYPTION_MAC_H_

#include <memory>
#include <vector>

#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "services/network/public/cpp/cookie_encryption_provider_impl.h"

namespace base { class FilePath; }
namespace network::mojom { class NetworkContextParams; }
namespace os_crypt_async { class KeyProvider; }

namespace zitter::content_embedder {

class CookieEncryptionMac {
 public:
  static std::unique_ptr<CookieEncryptionMac> OpenFixture(
      const base::FilePath& key_path);
  static std::unique_ptr<CookieEncryptionMac> UseKeychain();
  static std::unique_ptr<CookieEncryptionMac> UseExistingLegacyKeychain();
  ~CookieEncryptionMac();

  void GetEncryptor(os_crypt_async::OSCryptAsync::InitCallback done);
  void CheckWorkspaceStores(std::vector<base::FilePath> network_paths,
                            std::vector<base::FilePath> login_profile_paths,
                            base::OnceCallback<void(bool)> done);
  os_crypt_async::OSCryptAsync* os_crypt() { return &os_crypt_; }
  void Configure(network::mojom::NetworkContextParams& params);

 private:
  explicit CookieEncryptionMac(
      std::unique_ptr<os_crypt_async::KeyProvider> key_provider);

  os_crypt_async::OSCryptAsync os_crypt_;
  CookieEncryptionProviderImpl provider_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_COOKIE_ENCRYPTION_MAC_H_
