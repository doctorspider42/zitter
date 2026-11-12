// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_COOKIE_STORE_VERIFIER_H_
#define ZITTER_CONTENT_COOKIE_STORE_VERIFIER_H_

#include <vector>

namespace base {
class FilePath;
}

namespace os_crypt_async {
class Encryptor;
}

namespace zitter::content_embedder {

// Inspect disposable copies before the network service can open either store.
bool CheckCookieStores(const std::vector<base::FilePath>& network_paths,
                       const os_crypt_async::Encryptor& encryptor);

bool CheckLoginStores(const std::vector<base::FilePath>& profile_paths,
                      const os_crypt_async::Encryptor& encryptor);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_COOKIE_STORE_VERIFIER_H_
