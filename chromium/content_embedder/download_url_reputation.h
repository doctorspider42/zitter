// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_DOWNLOAD_URL_REPUTATION_H_
#define ZITTER_CONTENT_DOWNLOAD_URL_REPUTATION_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}

namespace safe_browsing {
class V5SearchHashesCache;
}

namespace zitter::content_embedder {

enum class DownloadUrlVerdict { kNoMatch, kThreat, kUnavailable };

// UI-sequence Safe Browsing V5 lookup for the complete download URL chain.
// Uses the fixed Safe Browsing endpoint for configured profiles. Probes can
// supply an HTTPS loopback endpoint without a key.
class DownloadUrlReputation final {
 public:
  using Result = base::OnceCallback<void(DownloadUrlVerdict)>;

  DownloadUrlReputation(
      scoped_refptr<network::SharedURLLoaderFactory> factory,
      GURL loopback_probe_endpoint);
  DownloadUrlReputation(
      scoped_refptr<network::SharedURLLoaderFactory> factory,
      std::string api_key);
  ~DownloadUrlReputation();

  void Check(const std::vector<GURL>& chain, Result result);

 private:
  struct Lookup;
  void OnLookupComplete(network::SimpleURLLoader* loader,
                        std::unique_ptr<Lookup> lookup,
                        std::optional<std::string> body);

  const scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const GURL endpoint_;
  const std::string api_key_;
  std::unique_ptr<safe_browsing::V5SearchHashesCache> cache_;
  std::map<network::SimpleURLLoader*, std::unique_ptr<network::SimpleURLLoader>>
      pending_;
  base::WeakPtrFactory<DownloadUrlReputation> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DOWNLOAD_URL_REPUTATION_H_
