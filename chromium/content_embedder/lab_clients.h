// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_LAB_CLIENTS_H_
#define ZITTER_CONTENT_LAB_CLIENTS_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/renderer/content_renderer_client.h"

class GURL;

namespace zitter::content_embedder {

class LabContentClient final : public content::ContentClient {
 public:
  void AddAdditionalSchemes(Schemes* schemes) override;
  std::u16string GetLocalizedString(int message_id) override;
  std::string_view GetDataResource(int resource_id,
                                   ui::ResourceScaleFactor scale_factor) override;
  scoped_refptr<base::RefCountedMemory> GetDataResourceBytes(
      int resource_id) override;
  std::string GetDataResourceString(int resource_id) override;
};

class NavigationReputation;

class LabBrowserClientBase : public content::ContentBrowserClient {
 public:
  LabBrowserClientBase();
  ~LabBrowserClientBase() override;

  std::string GetProduct() override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;

  void StartNavigationReputation(const base::FilePath& root,
                                 const std::string& api_key);
  void StartNavigationReputationProviderProbe(const base::FilePath& root,
                                              const GURL& origin,
                                              base::OnceClosure ready);
  void StopNavigationReputation();

  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                     int child_process_id) override;
  bool IsClipboardPasteAllowed(content::RenderFrameHost* frame) override;
  void IsClipboardPasteAllowedByPolicy(
      const content::ClipboardEndpoint& source,
      const content::ClipboardEndpoint& destination,
      const ui::ClipboardMetadata& metadata,
      content::ClipboardPasteData data,
      IsClipboardPasteAllowedCallback callback) override;
  void IsClipboardCopyAllowedByPolicy(
      const content::ClipboardEndpoint& source,
      const ui::ClipboardMetadata& metadata,
      const content::ClipboardPasteData& data,
      IsClipboardCopyAllowedCallback callback) override;
  using ClipboardCopyProbe = base::RepeatingCallback<void(
      const content::ClipboardEndpoint&, const content::ClipboardPasteData&)>;
  void SetClipboardCopyProbe(ClipboardCopyProbe probe);
  bool IsHandledURL(const GURL& url) override;
  std::unique_ptr<content::LoginDelegate> CreateLoginDelegate(
      const net::AuthChallengeInfo& auth_info, content::WebContents* web_contents,
      content::BrowserContext* browser_context,
      const content::GlobalRequestID& request_id,
      bool is_request_for_primary_main_frame_navigation,
      bool is_request_for_navigation, const GURL& url,
      scoped_refptr<net::HttpResponseHeaders> response_headers,
      bool first_auth_attempt, bool do_not_prompt_for_login,
      content::GuestPageHolder* guest_page_holder,
      content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback) override;
  bool IsSecurityLevelAcceptableForWebAuthn(
      content::RenderFrameHost* frame,
      const url::Origin& caller_origin) override;
  content::WebAuthenticationDelegate* GetWebAuthenticationDelegate() override;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<content::AuthenticatorRequestClientDelegate>
  GetWebAuthenticationRequestDelegate(content::RenderFrameHost* frame) override;
#endif
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* params,
      cert_verifier::mojom::CertVerifierCreationParams* cert_params) override;
  bool DoesSiteRequireDedicatedProcess(
      content::BrowserContext* context, const GURL& site_url) override;
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  CreateNonNetworkNavigationURLLoaderFactory(
      const std::string& scheme, content::FrameTreeNodeId frame_tree_node_id)
      override;
  void SiteInstanceGotProcessAndSite(content::SiteInstance* site) override;
  network::mojom::IPAddressSpace DetermineAddressSpaceFromURL(
      const GURL& url) override;
  bool ShouldServiceWorkerInheritPolicyContainerFromCreator(
      const GURL& url) override;
  void OverrideURLLoaderFactoryParams(
      content::BrowserContext* context,
      const url::Origin& origin,
      bool is_for_isolated_world,
      bool is_for_service_worker,
      network::mojom::URLLoaderFactoryParams* params) override;
  content::StoragePartitionConfig GetStoragePartitionConfigForSite(
      content::BrowserContext* context, const GURL& site) override;
  content::AllowServiceWorkerResult AllowServiceWorker(
      const GURL& scope,
      const net::SiteForCookies& site_for_cookies,
      const std::optional<url::Origin>& top_frame_origin,
      const blink::StorageKey& storage_key,
      const GURL& script_url,
      content::BrowserContext* context) override;
  void RegisterAssociatedInterfaceBindersForServiceWorker(
      const content::ServiceWorkerVersionBaseInfo& worker,
      blink::AssociatedInterfaceRegistry& registry) override;
  void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* process) override;
  void RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
      content::BrowserContext* context,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id, int render_frame_id,
      const std::optional<url::Origin>& initiator,
      NonNetworkURLLoaderFactoryMap* factories) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  void GetAdditionalWebUISchemes(std::vector<std::string>* schemes) override;
  bool CanCreateWindow(
      content::RenderFrameHost* opener, const GURL& opener_url,
      const GURL& opener_top_level_frame_url, const url::Origin& source_origin,
      content::mojom::WindowContainerType container_type, const GURL& target_url,
      const content::Referrer& referrer, const std::string& frame_name,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& features, bool user_gesture,
      bool opener_suppressed, bool* no_javascript_access) override;
  void CreateThrottlesForNavigation(
      content::NavigationThrottleRegistry& registry) override;
  void OnWebContentsCreated(content::WebContents* contents) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* frame,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
  void RegisterAssociatedInterfaceBindersForRenderFrameHost(
      content::RenderFrameHost& frame,
      blink::AssociatedInterfaceRegistry& registry) override;
  void WillCreateURLLoaderFactory(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      int render_process_id,
      URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      const net::IsolationInfo& isolation,
      std::optional<int64_t> navigation_id,
      ukm::SourceIdObj ukm_source_id,
      network::URLLoaderFactoryBuilder& builder,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client,
      bool* bypass_redirect_checks,
      bool* disable_secure_dns,
      network::mojom::URLLoaderFactoryOverridePtr* factory_override,
      scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner,
      bool is_for_network_service) override;
 private:
  ClipboardCopyProbe clipboard_copy_probe_;
  std::unique_ptr<NavigationReputation> navigation_reputation_;

};

class LabRendererClient final : public content::ContentRendererClient {
 public:
  void RenderThreadStarted() override;
  void WebViewCreated(blink::WebView* view,
                      bool was_created_by_renderer,
                      const url::Origin* outermost_origin) override;
  void RenderFrameCreated(content::RenderFrame* frame) override;
  void PrepareErrorPage(
      content::RenderFrame* frame,
      const blink::WebURLError& error,
      const std::string& http_method,
      content::mojom::AlternativeErrorPageOverrideInfoPtr alternative,
      std::string* error_html) override;
  void RunScriptsAtDocumentStart(content::RenderFrame* frame) override;
  void RunScriptsAtDocumentEnd(content::RenderFrame* frame) override;
  void RunScriptsAtDocumentIdle(content::RenderFrame* frame) override;
  void DidInitializeServiceWorkerContextOnWorkerThread(
      blink::WebServiceWorkerContextProxy* proxy,
      const GURL& scope,
      const GURL& script) override;
  void WillEvaluateServiceWorkerOnWorkerThread(
      blink::WebServiceWorkerContextProxy* proxy,
      v8::Local<v8::Context> context,
      int64_t version_id,
      const GURL& scope,
      const GURL& script,
      const blink::ServiceWorkerToken& token) override;
  void DidStartServiceWorkerContextOnWorkerThread(
      int64_t version_id,
      const GURL& scope,
      const GURL& script,
      const blink::ServiceWorkerToken& token) override;
  void WillDestroyServiceWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context,
      int64_t version_id,
      const GURL& scope,
      const GURL& script,
      const blink::ServiceWorkerToken& token) override;
};

GURL LabStartPage();
std::optional<int> CheckLabStartupFlags();

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_LAB_CLIENTS_H_
