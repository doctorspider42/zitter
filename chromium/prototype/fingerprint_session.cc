// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/fingerprint_session.h"

#include <memory>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_context.h"
#include "crypto/hmac.h"
#include "crypto/random.h"
#include "net/base/schemeful_site.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {

constexpr char kSessionKey[] = "zitter-fingerprint-session-key";

}  // namespace

// static
FingerprintSession& FingerprintSession::For(content::BrowserContext& context) {
  auto* session = static_cast<FingerprintSession*>(context.GetUserData(kSessionKey));
  if (!session) {
    auto created = std::unique_ptr<FingerprintSession>(new FingerprintSession);
    session = created.get();
    context.SetUserData(kSessionKey, std::move(created));
  }
  return *session;
}

FingerprintSession::FingerprintSession() : key_(32) {
  crypto::RandBytes(key_);
}

FingerprintSession::~FingerprintSession() = default;

void FingerprintSession::Rotate() {
  crypto::RandBytes(key_);
  changed_.Notify();
}

base::CallbackListSubscription FingerprintSession::ObserveRotation(
    base::RepeatingClosure changed) {
  return changed_.Add(std::move(changed));
}

std::u16string FingerprintSession::SiteCode(const GURL& url) const {
  if (!url.SchemeIsHTTPOrHTTPS())
    return {};
  // Match Blink's derivation for a top-level page.
  const auto site = net::SchemefulSite(url).Serialize();
  const auto mac = crypto::hmac::SignSha256(key_, base::as_byte_span(site));
  const auto hex = base::HexEncode(base::span(mac).first(4u));
  return base::ASCIIToUTF16(hex.substr(0, 4) + " " + hex.substr(4));
}

}  // namespace zitter::prototype
