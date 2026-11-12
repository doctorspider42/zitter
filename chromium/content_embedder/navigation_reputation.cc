// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "navigation_reputation.h"

#include <optional>
#include <utility>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "base/supports_user_data.h"
#include "components/safe_browsing/core/browser/database_manager_mechanism.h"
#include "components/safe_browsing/core/browser/db/sb_local_database_manager.h"
#include "components/safe_browsing/core/browser/db/sb_protocol_config.h"
#include "components/safe_browsing/core/browser/db/v5_get_hash_protocol_manager.h"
#include "components/safe_browsing/core/browser/db/v5_search_hashes_cache.h"
#include "components/safe_browsing/core/browser/safe_browsing_lookup_mechanism_runner.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/network_context_client_base.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/originating_process_id.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace zitter::content_embedder {
namespace {

namespace sb = safe_browsing;
using LookupRunner = sb::SafeBrowsingLookupMechanismRunner;
using LookupResult = sb::SafeBrowsingLookupMechanism::CompleteCheckResult;

constexpr char kHashLookupKey[] = "zitter.content.navigation_reputation";

// Chromium V5 reads the global Google key. Attach the Zitter key only on
// the dedicated reputation factory so it does not enable other services.
class ReputationLoaderFactory final : public network::SharedURLLoaderFactory {
 public:
  ReputationLoaderFactory(std::string api_key, GURL loopback_provider)
      : api_key_(std::move(api_key)),
        loopback_provider_(std::move(loopback_provider)) {
    CHECK(loopback_provider_.is_empty() ||
          (loopback_provider_.SchemeIs("http") &&
           loopback_provider_.GetHost() == "127.0.0.1" &&
           loopback_provider_.has_port() && loopback_provider_.GetPath() == "/" &&
           !loopback_provider_.has_query() && !loopback_provider_.has_ref() &&
           !loopback_provider_.has_username() && !loopback_provider_.has_password()));
  }

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id, uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& annotation) override {
    const auto& url = request.url;
    const auto path = url.GetPath();
    if (api_key_.empty() || !url.SchemeIs("https") || url.GetHost() != "safebrowsing.googleapis.com" ||
        url.EffectiveIntPort() != 443 || url.has_username() || url.has_password() ||
        (path != "/v4/threatListUpdates:fetch" && path != "/v4/fullHashes:find" &&
         path != "/v5/hashLists:batchGet" && path != "/v5/hashes:search")) {
      return;
    }
    if (!context_ || !context_.is_connected()) {
      factory_.reset();
      context_.reset();
      auto params = network::mojom::NetworkContextParams::New();
      params->http_cache_enabled = false;
      params->cert_verifier_params = content::GetCertVerifierParams(
          cert_verifier::mojom::CertVerifierCreationParams::New());
      content::CreateNetworkContextInNetworkService(
          context_.BindNewPipeAndPassReceiver(), std::move(params));
      mojo::PendingRemote<network::mojom::NetworkContextClient> context_client;
      mojo::MakeSelfOwnedReceiver(
          std::make_unique<content::NetworkContextClientBase>(),
          context_client.InitWithNewPipeAndPassReceiver());
      context_->SetClient(std::move(context_client));
    }
    if (!factory_ || !factory_.is_connected()) {
      factory_.reset();
      auto params = network::mojom::URLLoaderFactoryParams::New();
      params->process_id = network::OriginatingProcessId::browser();
      params->is_orb_enabled = false;
      context_->CreateURLLoaderFactory(factory_.BindNewPipeAndPassReceiver(),
                                       std::move(params));
    }
    network::ResourceRequest reputation_request(request);
    const GURL target = loopback_provider_.is_empty()
                            ? url
                            : loopback_provider_.Resolve(url.PathForRequest());
    reputation_request.url =
        net::AppendOrReplaceQueryParameter(target, "key", api_key_);
    reputation_request.credentials_mode = network::mojom::CredentialsMode::kOmit;
    reputation_request.redirect_mode = network::mojom::RedirectMode::kError;
    factory_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                   reputation_request, std::move(client), annotation);
  }

  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory>) override {
    NOTREACHED();
  }

  std::unique_ptr<network::PendingSharedURLLoaderFactory> Clone() override {
    NOTREACHED();
  }

 private:
  ~ReputationLoaderFactory() override = default;

  const std::string api_key_;
  const GURL loopback_provider_;
  mojo::Remote<network::mojom::NetworkContext> context_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
};

class ProfileHashLookup final : public base::SupportsUserData::Data {
 public:
  ProfileHashLookup(scoped_refptr<network::SharedURLLoaderFactory> factory,
                    const sb::SBProtocolConfig& config)
      : cache_(nullptr), protocol_(std::move(factory), config, &cache_, nullptr) {}

  base::WeakPtr<sb::V5GetHashProtocolManager> protocol() {
    return protocol_.GetWeakPtr();
  }

 private:
  sb::V5SearchHashesCache cache_;
  sb::V5GetHashProtocolManager protocol_;
};

class ReputationThrottle final : public content::NavigationThrottle {
 public:
  ReputationThrottle(
      content::NavigationThrottleRegistry& registry,
      scoped_refptr<sb::SafeBrowsingDatabaseManager> database,
      base::WeakPtr<sb::V5GetHashProtocolManager> protocol)
      : NavigationThrottle(registry),
        database_(std::move(database)),
        protocol_(std::move(protocol)) {}

  const char* GetNameForLogging() override { return "ZitterNavigationReputation"; }
  ThrottleCheckResult WillStartRequest() override { return CheckUrl(); }
  ThrottleCheckResult WillRedirectRequest() override { return CheckUrl(); }

 private:
  ThrottleCheckResult CheckUrl() {
    const auto& url = navigation_handle()->GetURL();
    if (!url.SchemeIsHTTPOrHTTPS())
      return PROCEED;

    auto mechanism = std::make_unique<sb::DatabaseManagerMechanism>(
        url,
        sb::SBThreatTypeSet{
            sb::SBThreatType::SB_THREAT_TYPE_URL_MALWARE,
            sb::SBThreatType::SB_THREAT_TYPE_URL_PHISHING,
            sb::SBThreatType::SB_THREAT_TYPE_URL_UNWANTED},
        database_, sb::CheckBrowseUrlType::kHashDatabase,
        /*check_allowlist=*/false, protocol_);
    lookup_ = std::make_unique<LookupRunner>(
        std::move(mechanism), "HashDatabase",
        base::BindOnce(&ReputationThrottle::OnVerdict, weak_factory_.GetWeakPtr()));
    if (!lookup_->Run().is_safe_synchronously)
      return DEFER;
    lookup_.reset();
    return PROCEED;
  }

  void OnVerdict(bool timed_out,
                 std::optional<std::unique_ptr<LookupResult>> result) {
    lookup_.reset();
    if (timed_out || !result ||
        (*result)->threat_type == sb::SBThreatType::SB_THREAT_TYPE_SAFE) {
      Resume();
      return;
    }
    CancelDeferredNavigation(ThrottleCheckResult(
        CANCEL, net::ERR_BLOCKED_BY_CLIENT, R"HTML(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'">
<title>Unsafe site</title>
<style>
  :root { color-scheme: light dark; background: Canvas; color: CanvasText; }
  body { margin: clamp(16px, 6vw, 64px); font: 15px/1.5 system-ui, sans-serif; }
  main { max-width: 36rem; margin: auto; }
</style>
<main id="navigation-reputation-warning">
  <h1>This site may be unsafe</h1>
  <p>Zitter blocked this page after a Google Safe Browsing match.
     Check the address or go back.</p>
</main>
)HTML"));
  }

  const scoped_refptr<sb::SafeBrowsingDatabaseManager> database_;
  const base::WeakPtr<sb::V5GetHashProtocolManager> protocol_;
  std::unique_ptr<LookupRunner> lookup_;
  base::WeakPtrFactory<ReputationThrottle> weak_factory_{this};
};

}  // namespace

class NavigationReputation::Service {
 public:
  Service(const base::FilePath& root, const std::string& api_key,
          const GURL& loopback_provider, base::OnceClosure ready)
      : config_("zitter", /*disable_auto_update=*/api_key.empty(), /*key_param=*/"",
                std::string(version_info::GetVersionNumber())),
        factory_(base::MakeRefCounted<ReputationLoaderFactory>(api_key,
                                                              loopback_provider)),
        database_(sb::SBLocalDatabaseManager::Create(
            root.AppendASCII("Safe Browsing"), content::GetUIThreadTaskRunner({}),
            content::GetIOThreadTaskRunner({}),
            base::BindRepeating([] {
              return sb::ExtendedReportingLevel::SBER_LEVEL_OFF;
            }))) {
    if (ready) {
      db_update_subscription_ = database_->RegisterDatabaseUpdatedCallback(
          base::BindRepeating([](base::OnceClosure& callback) {
            if (callback)
              std::move(callback).Run();
          }, base::OwnedRef(std::move(ready))));
    }
    database_->StartOnUIThread(factory_, config_);
  }

  ~Service() {
    database_->StopOnUIThread(/*shutdown=*/true);
  }

  void AddThrottle(content::NavigationThrottleRegistry& registry) {
    auto& handle = registry.GetNavigationHandle();
    if (!handle.GetURL().SchemeIsHTTPOrHTTPS())
      return;
    auto* profile = handle.GetWebContents()->GetBrowserContext();
    // V5 confirmation results live only as long as their browser context,
    // including the separate context for each private session.
    auto* lookup = static_cast<ProfileHashLookup*>(profile->GetUserData(kHashLookupKey));
    if (!lookup) {
      auto instance = std::make_unique<ProfileHashLookup>(factory_, config_);
      lookup = instance.get();
      profile->SetUserData(kHashLookupKey, std::move(instance));
    }
    registry.AddThrottle(
        std::make_unique<ReputationThrottle>(registry, database_, lookup->protocol()));
  }

 private:
  const sb::SBProtocolConfig config_;
  const scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const scoped_refptr<sb::SBLocalDatabaseManager> database_;
  base::CallbackListSubscription db_update_subscription_;
};

NavigationReputation::NavigationReputation(const base::FilePath& root,
                                           const std::string& api_key)
    : service_(std::make_unique<Service>(root, api_key, GURL(), base::OnceClosure())) {}

NavigationReputation::NavigationReputation(const base::FilePath& root,
                                           const GURL& loopback_provider,
                                           base::OnceClosure ready)
    : service_(std::make_unique<Service>(root, "zitter-loopback-test-key",
                                         loopback_provider, std::move(ready))) {}

NavigationReputation::~NavigationReputation() = default;

void NavigationReputation::AddThrottle(content::NavigationThrottleRegistry& registry) {
  service_->AddThrottle(registry);
}

}  // namespace zitter::content_embedder
