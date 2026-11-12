// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_COOKIE_ENCRYPTION_WIN_H_
#define ZITTER_CONTENT_COOKIE_ENCRYPTION_WIN_H_

#include <memory>
#include <vector>

#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "services/network/public/cpp/cookie_encryption_provider_impl.h"

class PrefService;
namespace base {
class FilePath;
}
namespace network::mojom {
class NetworkContextParams;
}

namespace zitter::content_embedder {

class CookieEncryptionWin {
 public:
  static std::unique_ptr<CookieEncryptionWin> Open(
      const base::FilePath& local_state_path,
      bool can_create_key,
      const std::vector<base::FilePath>& network_paths);
  ~CookieEncryptionWin();

  os_crypt_async::OSCryptAsync* os_crypt() { return &os_crypt_; }
  void Configure(network::mojom::NetworkContextParams& params);

 private:
  explicit CookieEncryptionWin(std::unique_ptr<PrefService> local_state);

  std::unique_ptr<PrefService> local_state_;
  os_crypt_async::OSCryptAsync os_crypt_;
  CookieEncryptionProviderImpl provider_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_COOKIE_ENCRYPTION_WIN_H_
