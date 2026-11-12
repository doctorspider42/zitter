// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_SESSION_H_
#define ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_SESSION_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/supports_user_data.h"

class GURL;

namespace content {
class BrowserContext;
}

namespace zitter::prototype {

inline constexpr char kFingerprintProtectionPref[] =
    "zitter.fingerprint_protection";

// An in-memory secret shared by pages in a single BrowserContext.
class FingerprintSession final : public base::SupportsUserData::Data {
 public:
  static FingerprintSession& For(content::BrowserContext& context);
  ~FingerprintSession() override;

  const std::vector<uint8_t>& key() const { return key_; }
  void Rotate();
  base::CallbackListSubscription ObserveRotation(base::RepeatingClosure changed);

  std::u16string SiteCode(const GURL& url) const;

 private:
  FingerprintSession();

  std::vector<uint8_t> key_;
  base::RepeatingClosureList changed_;
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_SESSION_H_
