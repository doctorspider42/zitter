#include "chromium/prototype/browser_import_data.h"

#import <CommonCrypto/CommonKeyDerivation.h>
#import <CommonCrypto/CommonCryptoError.h>
#include <algorithm>
#include "crypto/apple/keychain_v2.h"

namespace zitter::prototype {
std::optional<ImportedCookieKey> ReadImportedCookieKey(ImportBrowser browser) {
  const char* account = "";
  switch (browser) {
    case ImportBrowser::kArc: account = "Arc"; break;
    case ImportBrowser::kChrome: account = "Chrome"; break;
    case ImportBrowser::kEdge: account = "Microsoft Edge"; break;
    case ImportBrowser::kBrave: account = "Brave"; break;
    case ImportBrowser::kFirefox:
    case ImportBrowser::kOpera:
    case ImportBrowser::kVivaldi: return std::nullopt;
  }
  auto password = crypto::apple::KeychainV2::GetInstance().FindGenericPassword(
      std::string(account) + " Safe Storage", account);
  if (!password.has_value() || password->empty()) return std::nullopt;
  ImportedCookieKey key;
  constexpr uint8_t salt[] = {'s', 'a', 'l', 't', 'y', 's', 'a', 'l', 't'};
  int status = CCKeyDerivationPBKDF(kCCPBKDF2,
      reinterpret_cast<const char*>(password->data()), password->size(),
      salt, sizeof(salt), kCCPRFHmacAlgSHA1, 1003, key.data(), key.size());
  std::fill(password->begin(), password->end(), 0);
  if (status != kCCSuccess) return std::nullopt;
  return key;
}
}  // namespace zitter::prototype
