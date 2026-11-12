// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_url_reputation.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/base64url.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "components/safe_browsing/core/browser/db/sb_protocol_manager_util.h"
#include "components/safe_browsing/core/browser/db/v5_search_hashes_cache.h"
#include "components/safe_browsing/core/browser/db/v5_search_hashes_util.h"
#include "components/safe_browsing/core/common/proto/safebrowsingv5.pb.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/url_constants.h"

namespace zitter::content_embedder {
namespace {

namespace v5 = safe_browsing::v5_search_hashes_util;
namespace V5 = safe_browsing::V5;
using safe_browsing::SBProtocolManagerUtil;

constexpr size_t kMaxRedirectChain = 20;
constexpr size_t kMaxPrefixes = 512;
constexpr size_t kMaxPending = 8;
constexpr size_t kMaxResponseBytes = 1024 * 1024;
constexpr int64_t kMaxCacheSeconds = 24 * 60 * 60;
constexpr char kSafeBrowsingEndpoint[] =
    "https://safebrowsing.googleapis.com/v5/hashes:search";

bool IsHttpsUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIs(url::kHttpsScheme) && url.has_host() &&
         !url.has_username() && !url.has_password();
}

bool IsProbeEndpoint(const GURL& endpoint) {
  return IsHttpsUrl(endpoint) && endpoint.host() == "127.0.0.1" &&
         endpoint.has_port() && !endpoint.has_query() && !endpoint.has_ref();
}

bool IsDownloadThreat(V5::ThreatType threat) {
  return threat == V5::ThreatType::MALICIOUS_BINARY ||
         threat == V5::ThreatType::MALWARE ||
         threat == V5::ThreatType::POTENTIALLY_HARMFUL_APPLICATION ||
         threat == V5::ThreatType::SOCIAL_ENGINEERING ||
         threat == V5::ThreatType::UNWANTED_SOFTWARE;
}

DownloadUrlVerdict ClassifyMatches(
    const std::set<std::string>& full_hashes,
    const std::vector<V5::FullHash>& matches) {
  bool unknown_match = false;
  for (const auto& match : matches) {
    if (!full_hashes.contains(match.full_hash()))
      continue;
    if (std::ranges::any_of(match.full_hash_details(), [](const auto& detail) {
          return IsDownloadThreat(detail.threat_type());
        })) {
      return DownloadUrlVerdict::kThreat;
    }
    if (match.full_hash_details().empty() ||
        std::ranges::any_of(match.full_hash_details(), [](const auto& detail) {
          return detail.threat_type() == V5::ThreatType::THREAT_TYPE_UNSPECIFIED;
        })) {
      unknown_match = true;
    }
  }
  return unknown_match ? DownloadUrlVerdict::kUnavailable
                       : DownloadUrlVerdict::kNoMatch;
}

}  // namespace

struct DownloadUrlReputation::Lookup {
  std::set<std::string> full_hashes;
  std::vector<std::string> requested_prefixes;
  std::vector<V5::FullHash> cached_matches;
  Result result;
};

DownloadUrlReputation::DownloadUrlReputation(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    GURL loopback_probe_endpoint)
    : factory_(std::move(factory)),
      endpoint_(std::move(loopback_probe_endpoint)),
      cache_(std::make_unique<safe_browsing::V5SearchHashesCache>(nullptr)) {}

DownloadUrlReputation::DownloadUrlReputation(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    std::string api_key)
    : factory_(std::move(factory)),
      endpoint_(kSafeBrowsingEndpoint),
      api_key_(std::move(api_key)),
      cache_(std::make_unique<safe_browsing::V5SearchHashesCache>(nullptr)) {}

DownloadUrlReputation::~DownloadUrlReputation() = default;

void DownloadUrlReputation::Check(const std::vector<GURL>& chain, Result result) {
  if (!factory_ || (api_key_.empty() && !IsProbeEndpoint(endpoint_)) ||
      chain.empty() || chain.size() > kMaxRedirectChain ||
      !std::ranges::all_of(chain, IsHttpsUrl) || pending_.size() >= kMaxPending) {
    std::move(result).Run(DownloadUrlVerdict::kUnavailable);
    return;
  }
  auto lookup = std::make_unique<Lookup>();
  std::set<std::string> prefixes;
  for (const GURL& url : chain) {
    std::vector<std::string> hashes;
    SBProtocolManagerUtil::UrlToFullHashes(url, &hashes);
    if (hashes.empty()) {
      std::move(result).Run(DownloadUrlVerdict::kUnavailable);
      return;
    }
    for (const auto& hash : hashes)
      prefixes.insert(SBProtocolManagerUtil::GetHashPrefix(hash));
    lookup->full_hashes.insert(hashes.begin(), hashes.end());
  }
  if (prefixes.size() > kMaxPrefixes) {
    std::move(result).Run(DownloadUrlVerdict::kUnavailable);
    return;
  }
  v5::SearchCache(cache_.get(), prefixes, &lookup->requested_prefixes,
                  &lookup->cached_matches);
  if (lookup->requested_prefixes.empty()) {
    std::move(result).Run(
        ClassifyMatches(lookup->full_hashes, lookup->cached_matches));
    return;
  }

  V5::SearchHashesRequest request;
  for (const auto& prefix : lookup->requested_prefixes)
    request.add_hash_prefixes(prefix);
  std::string encoded_request;
  base::Base64UrlEncode(request.SerializeAsString(),
                        base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &encoded_request);
  GURL endpoint = net::AppendQueryParameter(endpoint_, "$req", encoded_request);
  endpoint = net::AppendQueryParameter(endpoint, "$ct", "application/x-protobuf");
  if (!api_key_.empty())
    endpoint = net::AppendQueryParameter(endpoint, "key", api_key_);

  constexpr auto probe_annotation = net::DefineNetworkTrafficAnnotation(
      "zitter_download_url_reputation_probe", R"(
        semantics {
          sender: "Zitter Download URL Reputation"
          description: "Checks download URLs for malware before saving."
          trigger: "A download response is eligible for the save flow."
          data: "Four-byte prefixes of hashes of the download URL chain."
          destination: LOCAL
        }
        policy {
          cookies_allowed: NO
          setting: "Available in the disposable loopback download probe."
          policy_exception_justification: "Only the local probe uses this lookup."
        })");
  constexpr auto provider_annotation = net::DefineNetworkTrafficAnnotation(
      "zitter_download_url_reputation", R"(
        semantics {
          sender: "Zitter Download URL Reputation"
          description: "Checks the URLs of downloads for unsafe content."
          trigger: "A regular profile with Safe Browsing configured saves or resumes a download."
          data: "Four-byte SHA-256 hash prefixes for the download URL chain and the configured API key."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: NO
          setting: "Disabled by default. Enable by configuring a private Safe Browsing API key file."
          policy_exception_justification: "This embedder uses a local configuration file."
        })");
  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = std::move(endpoint);
  resource->load_flags = net::LOAD_DISABLE_CACHE;
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->redirect_mode = network::mojom::RedirectMode::kError;

  auto loader = network::SimpleURLLoader::Create(
      std::move(resource), api_key_.empty() ? probe_annotation : provider_annotation);
  auto* key = loader.get();
  lookup->result = std::move(result);
  pending_.emplace(key, std::move(loader));
  key->SetTimeoutDuration(base::Seconds(5));
  key->DownloadToString(
      factory_.get(),
      base::BindOnce(&DownloadUrlReputation::OnLookupComplete,
                     weak_factory_.GetWeakPtr(), key, std::move(lookup)),
      kMaxResponseBytes);
}

void DownloadUrlReputation::OnLookupComplete(
    network::SimpleURLLoader* key,
    std::unique_ptr<Lookup> lookup,
    std::optional<std::string> body) {
  auto loader = std::move(pending_.extract(key).mapped());
  const auto* response = loader->ResponseInfo();
  const int status =
      response && response->headers ? response->headers->response_code() : 0;
  auto parsed = v5::ParseResponse(loader->NetError(), status,
                                  body.value_or(std::string()),
                                  lookup->requested_prefixes);
  if (!parsed.has_value() || parsed->found_unmatched_full_hashes) {
    std::move(lookup->result).Run(DownloadUrlVerdict::kUnavailable);
    return;
  }
  const auto& duration = parsed->response.cache_duration();
  if (duration.seconds() < 0 || duration.nanos() < 0 ||
      duration.nanos() >= base::Time::kNanosecondsPerSecond) {
    std::move(lookup->result).Run(DownloadUrlVerdict::kUnavailable);
    return;
  }
  V5::Duration bounded_duration = duration;
  if (duration.seconds() >= kMaxCacheSeconds) {
    bounded_duration.set_seconds(kMaxCacheSeconds);
    bounded_duration.set_nanos(0);
  }
  const auto& full_hashes = parsed->response.full_hashes();
  std::vector<V5::FullHash> remote_matches(full_hashes.begin(), full_hashes.end());
  cache_->CacheSearchHashesResponse(lookup->requested_prefixes, remote_matches,
                                    bounded_duration);
  lookup->cached_matches.insert(lookup->cached_matches.end(),
                                 remote_matches.begin(), remote_matches.end());
  std::move(lookup->result).Run(
      ClassifyMatches(lookup->full_hashes, lookup->cached_matches));
}

}  // namespace zitter::content_embedder
