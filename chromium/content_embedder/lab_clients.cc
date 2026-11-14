// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "lab_clients.h"

#include "navigation_reputation.h"
#include "http_auth_prompt.h"

#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "net/http/http_response_headers.h"

#include <optional>
#include <utility>

#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
#include "mojo/public/cpp/bindings/binder_map.h"
#include "zitter_content/pdf_preview.mojom.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/browser/headless/headless_print_manager.h"
#include "components/printing/browser/print_manager_utils.h"
#include "workspace_printing.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "base/scoped_observation.h"
#include "content/public/browser/authenticator_request_client_delegate.h"
#include "device/fido/authenticator_get_assertion_response.h"
#include "device/fido/fido_authenticator.h"
#include "device/fido/fido_discovery_factory.h"
#include "device/fido/fido_request_handler_base.h"
#include "device/fido/mac/icloud_keychain.h"
#include "ui/gfx/native_ui_types.h"
#endif

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_util.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/page_dialogs.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "chromium/prototype/link_navigation.h"
#include "chromium/prototype/adblock_network.h"
#include "chromium/renderer/zitter_page_scripts.h"
#include "components/autofill/content/renderer/autofill_agent.h"
#include "components/autofill/content/renderer/password_autofill_agent.h"
#include "components/autofill/content/renderer/password_generation_agent.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/password_manager/content/browser/content_password_manager_driver_factory.h"
#include "components/security_state/content/content_utils.h"
#include "components/security_state/core/security_state.h"
#include "components/version_info/version_info_with_user_agent.h"
#include "content/public/browser/allow_service_worker_result.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/clipboard_types.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_authentication_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/url_constants.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "extension_manifest.h"
#include "extension_registry_probe.h"
#include "extensions/browser/api/web_request/extension_web_request_event_router.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_protocols.h"
#include "ipc/constants.mojom.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/extension_web_contents_observer.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/service_worker/service_worker_host.h"
#include "extensions/browser/url_loader_factory_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/mojom/event_router.mojom.h"
#include "extensions/common/mojom/frame.mojom.h"
#include "extensions/common/mojom/renderer_host.mojom.h"
#include "extensions/common/mojom/service_worker_host.mojom.h"
#include "extensions/common/mojom/web_request_host.mojom.h"
#include "extensions/renderer/api/core_extensions_renderer_api_provider.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/extension_frame_helper.h"
#include "extensions/renderer/script_context.h"
#include "extensions/renderer/script_context_set.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "extensions/renderer/extensions_renderer_client.h"
#include "extensions/renderer/bindings/api_bindings_system.h"
#include "extensions/renderer/bindings/api_binding_hooks_delegate.h"
#include "extensions/renderer/native_extension_bindings_system.h"
#include "mojo/public/cpp/bindings/self_owned_associated_receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/web/web_text_check_client.h"
#include "third_party/blink/public/web/web_text_checking_result.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "tabs_hooks_delegate.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "sandbox/policy/sandbox.h"
#include "sandbox/policy/switches.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/cert_verifier_service_updater.mojom.h"
#include "services/network/public/mojom/ip_address_space.mojom-shared.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "third_party/blink/public/web/zitter_fingerprint_state.h"
#include "ui/base/clipboard/clipboard_metadata.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "workspace_devtools.h"
#include "workspace_lab.h"
#include "zitter_content/adblock_probe.mojom.h"
#include "zitter_content/content_blocking_probe_script.h"
#include "zitter_content/fingerprint_state.mojom.h"

namespace zitter::content_embedder {
#if BUILDFLAG(IS_MAC)
std::vector<mojom::SpellingMatchPtr> CheckMacSpelling(
    const std::u16string& text, const std::vector<std::u16string>& dictionaries,
    const std::u16string& selected);
#endif
namespace {

#if BUILDFLAG(IS_MAC)
class LocalSpellingHost final : public mojom::LocalSpelling {
 public:
  explicit LocalSpellingHost(content::BrowserContext& context) : context_(context.GetWeakPtr()) {}
  void Check(const std::u16string& text, CheckCallback reply) override {
    std::vector<mojom::SpellingMatchPtr> matches;
    if (context_ && !context_->ShutdownStarted() && text.size() <= 32768) {
      const auto& prefs = static_cast<ProfileContext*>(context_.get())->site_permissions();
      if (prefs.SpellingEnabled())
        matches = CheckMacSpelling(text, prefs.SpellingDictionaries(),
                                  prefs.ActiveSpellingDictionary());
    }
    std::move(reply).Run(std::move(matches));
  }

 private:
  base::WeakPtr<content::BrowserContext> context_;
};

class LocalSpellingClient final : public content::RenderFrameObserver,
                                  public blink::WebTextCheckClient {
 public:
  explicit LocalSpellingClient(content::RenderFrame* frame) : RenderFrameObserver(frame) {
    frame->GetWebFrame()->SetTextCheckClient(this);
  }
  bool IsSpellCheckingEnabled() const override { return true; }
  void RequestCheckingOfText(
      const blink::WebString& text, const std::vector<blink::WebSpellingMarker>&,
      ShouldForceRefreshTextCheckService,
      std::unique_ptr<blink::WebTextCheckingCompletion> completion) override {
    if (!host_)
      render_frame()->GetBrowserInterfaceBroker().GetInterface(host_.BindNewPipeAndPassReceiver());
    host_->Check(text.Utf16(), mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindOnce([](std::unique_ptr<blink::WebTextCheckingCompletion> completion,
                          std::vector<mojom::SpellingMatchPtr> matches) {
          std::vector<blink::WebTextCheckingResult> results;
          for (const auto& match : matches) {
            std::vector<blink::WebString> replacements;
            for (const auto& suggestion : match->suggestions)
              replacements.push_back(blink::WebString::FromUtf16(suggestion));
            results.emplace_back(blink::kWebTextDecorationTypeSpelling,
                match->offset, match->length, std::move(replacements));
          }
          completion->DidFinishCheckingText(results);
        }, std::move(completion)), std::vector<mojom::SpellingMatchPtr>()));
  }
  void OnDestruct() override { delete this; }

 private:
  mojo::Remote<mojom::LocalSpelling> host_;
};
#endif


class RestrictedExtensionLoaderFactory final
    : public network::SelfDeletingURLLoaderFactory {
 public:
  RestrictedExtensionLoaderFactory(
      content::GlobalRenderFrameHostId frame,
      url::Origin origin,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      base::SelfDeletingPassKey key)
      : SelfDeletingURLLoaderFactory(std::move(receiver), key),
        frame_(frame), origin_(std::move(origin)), target_(std::move(target)) {}

 private:
  ~RestrictedExtensionLoaderFactory() override = default;

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id, uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) override {
    auto* frame = content::RenderFrameHost::FromID(frame_);
    const auto allowed = [&] {
      if (!frame || !frame->IsActive() || request.method != "GET" ||
          !request.request_initiator) {
        return false;
      }
      const auto& committed = frame->GetLastCommittedOrigin();
      const auto& initiator = *request.request_initiator;
      if (origin_.opaque()) {
        // Blink can derive another opaque origin when it applies the document
        // CSP. The factory is bound to this frame and the extension precursor.
        const auto& precursor = origin_.GetTupleOrPrecursorTupleIfOpaque();
        return initiator.opaque() &&
               initiator.GetTupleOrPrecursorTupleIfOpaque() == precursor &&
               committed.GetTupleOrPrecursorTupleIfOpaque() == precursor &&
               CanLoadSandboxedExtensionSubresource(*frame, request.url);
      }
      return committed == origin_ && initiator == origin_ &&
             CanLoadWebAccessibleExtensionResource(*frame->GetBrowserContext(),
                                                   request.url, origin_);
    };
    if (!allowed()) {
      mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))->OnComplete(
          network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      return;
    }
    target_->CreateLoaderAndStart(std::move(loader), request_id, options, request,
                                 std::move(client), traffic_annotation);
  }

  const content::GlobalRenderFrameHostId frame_;
  const url::Origin origin_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
};


constexpr char kExtensionIncognitoRenderer[] =
    "zitter-content-extension-incognito-renderer";

class AdblockProbeObserver final : public content::RenderFrameObserver,
                                   public content::RenderFrameObserverTracker<AdblockProbeObserver>,
                                   public mojom::AdblockProbe {
 public:
  explicit AdblockProbeObserver(content::RenderFrame* frame)
      : content::RenderFrameObserver(frame),
        content::RenderFrameObserverTracker<AdblockProbeObserver>(frame) {
    frame->GetAssociatedInterfaceRegistry()->AddInterface<mojom::AdblockProbe>(
        base::BindRepeating(&AdblockProbeObserver::Bind, base::Unretained(this)));
  }

  bool RunAtDocumentStart() {
    auto* frame = render_frame()->GetWebFrame();
    if (!document_ || frame->GetDocument().Token() != *document_)
      return true;
    const auto document = *std::exchange(document_, std::nullopt);
    const auto frame_token = frame->GetLocalFrameToken();
    const auto nonce = std::exchange(nonce_, {});
    std::string script = kContentBlockingProbeScript;
    base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_NONCE", nonce);
    frame->ExecuteScript(blink::WebScriptSource(blink::WebString::FromUtf8(script)));
    frame = blink::WebLocalFrame::FromFrameToken(frame_token);
    return frame && !frame->GetDocument().IsNull() &&
           frame->GetDocument().Token() == document;
  }

 private:
  void Bind(mojo::PendingAssociatedReceiver<mojom::AdblockProbe> receiver) {
    receiver_.reset();
    receiver_.Bind(std::move(receiver));
  }

  void Arm(const blink::DocumentToken& document, const std::string& nonce) override {
    document_ = document;
    nonce_ = nonce;
  }

  void OnDestruct() override {
    render_frame()->GetAssociatedInterfaceRegistry()->RemoveInterface(
        mojom::AdblockProbe::Name_);
    delete this;
  }

  mojo::AssociatedReceiver<mojom::AdblockProbe> receiver_{this};
  std::optional<blink::DocumentToken> document_;
  std::string nonce_;
};

#if BUILDFLAG(IS_MAC)
gfx::NativeWindow WorkspacePasskeyWindow(content::RenderFrameHost* frame) {
  if (!frame || !frame->IsActive() ||
      frame->GetBrowserContext()->IsOffTheRecord()) {
    return {};
  }
  auto* page = content::WebContents::FromRenderFrameHost(frame);
  if (!page || page->GetVisibility() != content::Visibility::VISIBLE ||
      !device::fido::icloud_keychain::IsSupported()) {
    return {};
  }
  return page->GetTopLevelNativeWindow();
}

class WorkspaceMacAuthenticatorRequestDelegate final
    : public content::AuthenticatorRequestClientDelegate {
 public:
  explicit WorkspaceMacAuthenticatorRequestDelegate(content::RenderFrameHost& frame)
      : frame_id_(frame.GetGlobalId()) {}

 private:
  void SetUIPresentation(UIPresentation presentation) override {
    presentation_ = presentation;
  }

  void ConfigureDiscoveries(
      const url::Origin&, const std::string&, RequestSource source,
      device::FidoRequestType, std::optional<device::ResidentKeyRequirement>,
      device::UserVerificationRequirement, bool, std::optional<std::string_view>,
      bool, device::FidoDiscoveryFactory* discovery_factory) override {
    if (source == RequestSource::kWebAuthentication &&
        presentation_ == UIPresentation::kModal &&
        !IsVirtualEnvironmentEnabled()) {
      if (auto window = WorkspacePasskeyWindow(
              content::RenderFrameHost::FromID(frame_id_))) {
        discovery_factory->set_nswindow(window);
      }
    }
  }

  bool EmbedderControlsAuthenticatorDispatch(
      const device::FidoAuthenticator& authenticator) override {
    return authenticator.GetType() == device::AuthenticatorType::kICloudKeychain &&
           !WorkspacePasskeyWindow(content::RenderFrameHost::FromID(frame_id_));
  }

  void RegisterActionCallbacks(
      base::OnceClosure cancel_callback, base::OnceClosure,
      base::RepeatingClosure, AccountPreselectedCallback, PasswordSelectedCallback,
      device::FidoRequestHandlerBase::RequestCallback, base::OnceClosure,
      base::RepeatingClosure,
      base::RepeatingCallback<
          void(device::FidoRequestHandlerBase::BlePermissionCallback)>) override {
    cancel_ = std::move(cancel_callback);
  }

  void SelectAccount(
      std::vector<device::AuthenticatorGetAssertionResponse> responses,
      base::OnceCallback<void(device::AuthenticatorGetAssertionResponse)>
          callback) override {
    if (IsVirtualEnvironmentEnabled()) {
      AuthenticatorRequestClientDelegate::SelectAccount(std::move(responses),
                                                        std::move(callback));
      return;
    }
    // Content accepts the account already chosen by the native authenticator.
    // If another authenticator still needs a browser picker, cancel the request.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       &WorkspaceMacAuthenticatorRequestDelegate::Cancel,
                       weak_factory_.GetWeakPtr()));
  }

  void Cancel() {
    if (cancel_)
      std::move(cancel_).Run();
  }

  void StartObserving(device::FidoRequestHandlerBase* request_handler) override {
    observation_.Observe(request_handler);
  }

  void StopObserving(device::FidoRequestHandlerBase*) override {
    observation_.Reset();
  }

  const content::GlobalRenderFrameHostId frame_id_;
  UIPresentation presentation_ = UIPresentation::kDisabled;
  base::OnceClosure cancel_;
  base::ScopedObservation<device::FidoRequestHandlerBase,
                          device::FidoRequestHandlerBase::Observer>
      observation_{this};
  base::WeakPtrFactory<WorkspaceMacAuthenticatorRequestDelegate> weak_factory_{this};
};
#endif

class WorkspaceWebAuthenticationDelegate final
    : public content::WebAuthenticationDelegate {
 public:
  bool IsFocused(content::WebContents* contents) override {
    return contents->GetVisibility() == content::Visibility::VISIBLE;
  }

#if BUILDFLAG(IS_MAC)
  bool SupportsResidentKeys(content::RenderFrameHost* frame) override {
    return content::WebAuthenticationDelegate::SupportsResidentKeys(frame) ||
           static_cast<bool>(WorkspacePasskeyWindow(frame));
  }
#endif
};

class FingerprintStateReceiver final : public mojom::FingerprintState {
 public:
  void Set(uint64_t sequence, const std::vector<uint8_t>& key) override {
    blink::SetZitterFingerprintState(sequence, key);
  }
};

class LabExtensionHooksDelegate final : public extensions::APIBindingHooksDelegate {
 public:
  extensions::APIBindingHooks::RequestResult HandleRequest(
      const std::string& method_name,
      const extensions::APISignature*,
      v8::Local<v8::Context>,
      v8::LocalVector<v8::Value>* arguments,
      const extensions::APITypeReferenceMap&) override {
    using Result = extensions::APIBindingHooks::RequestResult;
    if (method_name != "extension.getBackgroundPage")
      return Result(Result::NOT_HANDLED);
    if (!arguments->empty())
      return Result("Expected no arguments.");
    // Manifest V3 runs its background code in a worker without a Window.
    Result result(Result::HANDLED);
    result.return_value = v8::Null(v8::Isolate::GetCurrent());
    return result;
  }
};

class LabExtensionsRendererAPIProvider final
    : public extensions::CoreExtensionsRendererAPIProvider {
 public:
  void AddBindingsSystemHooks(
      extensions::Dispatcher* dispatcher,
      extensions::NativeExtensionBindingsSystem* system) const override {
    CoreExtensionsRendererAPIProvider::AddBindingsSystemHooks(dispatcher, system);
    system->api_system()->RegisterHooksDelegate(
        "extension", std::make_unique<LabExtensionHooksDelegate>());
    system->api_system()->RegisterHooksDelegate(
        "tabs", std::make_unique<extensions::TabsHooksDelegate>(
                    system->messaging_service()));
  }
};

class LabExtensionsRendererClient final
    : public extensions::ExtensionsRendererClient {
 public:
  bool IsIncognitoProcess() const override {
    return base::CommandLine::ForCurrentProcess()->HasSwitch(
        kExtensionIncognitoRenderer);
  }
  int GetLowestIsolatedWorldId() const override {
    // Zitter reserves CONTENT_END for its own isolated page scripts.
    return content::ISOLATED_WORLD_ID_CONTENT_END + 1;
  }
};

class OffscreenClipboardSettings final : public content::RenderFrameObserver,
                                         public blink::WebContentSettingsClient {
 public:
  explicit OffscreenClipboardSettings(content::RenderFrame* frame)
      : content::RenderFrameObserver(frame) {
    frame->GetWebFrame()->SetContentSettingsClient(this);
  }

  bool AllowReadFromClipboard() override {
    return HasPermission(extensions::mojom::APIPermissionID::kClipboardRead);
  }

  bool AllowWriteToClipboard() override {
    return HasPermission(extensions::mojom::APIPermissionID::kClipboardWrite);
  }

 private:
  void OnDestruct() override { delete this; }

  bool HasPermission(extensions::mojom::APIPermissionID permission) {
    auto* frame = render_frame();
    auto* helper = extensions::ExtensionFrameHelper::Get(frame);
    if (!helper || helper->view_type() != extensions::mojom::ViewType::kOffscreenDocument)
      return false;
    auto* context = extensions::ExtensionsRendererClient::Get()->dispatcher()
                        ->script_context_set().GetCurrent();
    return context && context->web_frame() == frame->GetWebFrame() &&
           context->HasAPIPermission(permission);
  }
};

extensions::Dispatcher* ExtensionDispatcher() {
  if (!IsWorkspaceExtensionRuntime())
    return nullptr;
  return extensions::ExtensionsRendererClient::Get()->dispatcher();
}

}  // namespace

void LabContentClient::AddAdditionalSchemes(Schemes* schemes) {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (IsWorkspaceExtensionRuntime() ||
      (!command_line.HasSwitch(switches::kProcessType) &&
       command_line.HasSwitch(kExtensionManifestProbe))) {
    schemes->standard_schemes.emplace_back(extensions::kExtensionScheme);
  }
  if (IsWorkspaceExtensionRuntime()) {
    schemes->extension_schemes.emplace_back(extensions::kExtensionScheme);
    schemes->secure_schemes.emplace_back(extensions::kExtensionScheme);
    schemes->service_worker_schemes.emplace_back(extensions::kExtensionScheme);
    schemes->cors_enabled_schemes.emplace_back(extensions::kExtensionScheme);
  }
}

void LabBrowserClientBase::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line, int child_process_id) {
  if (!IsWorkspaceExtensionRuntime())
    return;
  const auto& browser = *base::CommandLine::ForCurrentProcess();
  command_line->AppendSwitch(browser.HasSwitch(kRestoreWorkspaceExtensions)
                                 ? kRestoreWorkspaceExtensions
                                 : kExtensionRegistryProbe);
  auto* process = content::RenderProcessHost::FromID(child_process_id);
  if (process && process->GetBrowserContext()->IsOffTheRecord())
    command_line->AppendSwitch(kExtensionIncognitoRenderer);
}

LabBrowserClientBase::LabBrowserClientBase() {
  content::SetFrameClipboardWritePolicy([](content::RenderFrameHost* frame) {
    return !IsWorkspaceOffscreenFrame(frame) ||
           HasWorkspaceOffscreenClipboardPermission(frame, ExtensionClipboardAccess::kWrite);
  });
}
LabBrowserClientBase::~LabBrowserClientBase() {
  content::SetFrameClipboardWritePolicy(nullptr);
}

std::string LabBrowserClientBase::GetProduct() {
  return version_info::GetProductNameAndVersionForUserAgent();
}

std::string LabBrowserClientBase::GetUserAgent() {
  return embedder_support::GetUserAgent();
}

blink::UserAgentMetadata LabBrowserClientBase::GetUserAgentMetadata() {
  return embedder_support::GetUserAgentMetadata();
}


void LabBrowserClientBase::StartNavigationReputation(
    const base::FilePath& root, const std::string& api_key) {
  CHECK(!root.empty());
  CHECK(!navigation_reputation_);
  navigation_reputation_ = std::make_unique<NavigationReputation>(root, api_key);
}

void LabBrowserClientBase::StartNavigationReputationProviderProbe(
    const base::FilePath& root, const GURL& origin, base::OnceClosure ready) {
  CHECK(!navigation_reputation_);
  CHECK(!origin.is_empty());
  navigation_reputation_ =
      std::make_unique<NavigationReputation>(root, origin, std::move(ready));
}

void LabBrowserClientBase::StopNavigationReputation() {
  navigation_reputation_.reset();
}

bool LabBrowserClientBase::IsClipboardPasteAllowed(
    content::RenderFrameHost* frame) {
  if (IsWorkspaceOffscreenFrame(frame))
    return HasWorkspaceOffscreenClipboardPermission(frame, ExtensionClipboardAccess::kRead);
  if (!frame || !frame->IsActive() || !frame->IsFocused())
    return false;
  auto* contents = content::WebContents::FromRenderFrameHost(frame);
  return contents && contents->GetVisibility() == content::Visibility::VISIBLE &&
         (frame->HasTransientUserActivation() || contents->HasRecentInteraction() ||
          HasWorkspaceExtensionClipboardPermission(frame, ExtensionClipboardAccess::kRead) ||
          static_cast<ProfileContext*>(frame->GetBrowserContext())
              ->site_permissions().HasClipboardReadGrant(frame));
}

void LabBrowserClientBase::IsClipboardPasteAllowedByPolicy(
    const content::ClipboardEndpoint& source,
    const content::ClipboardEndpoint& destination,
    const ui::ClipboardMetadata& metadata,
    content::ClipboardPasteData data,
    IsClipboardPasteAllowedCallback callback) {
  // The document can lose focus or its grant while Chromium reads the clipboard.
  if (!metadata.is_drag_and_drop &&
      !IsClipboardPasteAllowed(destination.render_frame_host())) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  ContentBrowserClient::IsClipboardPasteAllowedByPolicy(
      source, destination, metadata, std::move(data), std::move(callback));
}

void LabBrowserClientBase::IsClipboardCopyAllowedByPolicy(
    const content::ClipboardEndpoint& source,
    const ui::ClipboardMetadata& metadata,
    const content::ClipboardPasteData& data,
    IsClipboardCopyAllowedCallback callback) {
  if (clipboard_copy_probe_) {
    // Leave the disposable document's write uncommitted. Chromium discards
    // pending clipboard data when that document goes away.
    clipboard_copy_probe_.Run(source, data);
    return;
  }
  ContentBrowserClient::IsClipboardCopyAllowedByPolicy(
      source, metadata, data, std::move(callback));
}

void LabBrowserClientBase::SetClipboardCopyProbe(ClipboardCopyProbe probe) {
  clipboard_copy_probe_ = std::move(probe);
}

namespace {

class ExtensionHttpAuthDelegate final : public content::LoginDelegate {
 public:
  ExtensionHttpAuthDelegate(const net::AuthChallengeInfo& challenge,
                            content::WebContents* contents,
                            bool do_not_prompt,
                            LoginAuthRequiredCallback callback)
      : challenge_(challenge),
        contents_(contents ? contents->GetWeakPtr() : nullptr),
        do_not_prompt_(do_not_prompt),
        callback_(std::move(callback)) {}

  void Start(extensions::WebRequestAPI& api,
             content::BrowserContext& context,
             scoped_refptr<net::HttpResponseHeaders> headers,
             const content::GlobalRequestID& request_id,
             bool navigation) {
    // Content installs the returned delegate before accepting an auth reply.
    auto reply = base::BindPostTaskToCurrentDefault(base::BindOnce(
        &ExtensionHttpAuthDelegate::OnResponse, weak_factory_.GetWeakPtr()));
    if (!api.MaybeProxyAuthRequest(&context, challenge_, std::move(headers),
                                  request_id, navigation, std::move(reply),
                                  /*web_view_guest=*/nullptr)) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&ExtensionHttpAuthDelegate::OnResponse,
                                    weak_factory_.GetWeakPtr(), std::nullopt, false));
    }
  }

 private:
  void OnResponse(const std::optional<net::AuthCredentials>& credentials, bool cancel) {
    if (cancel || credentials) {
      std::move(callback_).Run(cancel ? std::nullopt : credentials);
      return;
    }
    auto [prompt_reply, unavailable_reply] = base::SplitOnceCallback(std::move(callback_));
    prompt_ = CreateHttpAuthPrompt(challenge_, contents_.get(), do_not_prompt_,
                                  std::move(prompt_reply));
    if (!prompt_)
      std::move(unavailable_reply).Run(std::nullopt);
  }

  const net::AuthChallengeInfo challenge_;
  const base::WeakPtr<content::WebContents> contents_;
  const bool do_not_prompt_;
  LoginAuthRequiredCallback callback_;
  std::unique_ptr<content::LoginDelegate> prompt_;
  base::WeakPtrFactory<ExtensionHttpAuthDelegate> weak_factory_{this};
};

}  // namespace

std::unique_ptr<content::LoginDelegate> LabBrowserClientBase::CreateLoginDelegate(
    const net::AuthChallengeInfo& auth_info, content::WebContents* web_contents,
    content::BrowserContext* context, const content::GlobalRequestID& request_id,
    bool, bool is_navigation, const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> headers, bool,
    bool do_not_prompt_for_login, content::GuestPageHolder* guest,
    content::LoginDelegate::LoginAuthRequiredCallback callback) {
  if (context && !auth_info.is_proxy && !guest && url.SchemeIsHTTPOrHTTPS() &&
      (!web_contents || web_contents->GetBrowserContext() == context)) {
    if (auto* api = WorkspaceExtensionRequestProxy(*context)) {
      auto delegate = std::make_unique<ExtensionHttpAuthDelegate>(
          auth_info, web_contents, do_not_prompt_for_login, std::move(callback));
      delegate->Start(*api, *context, std::move(headers), request_id, is_navigation);
      return delegate;
    }
  }
  return CreateHttpAuthPrompt(auth_info, web_contents, do_not_prompt_for_login,
                              std::move(callback));
}

bool LabBrowserClientBase::IsHandledURL(const GURL& url) {
  return (url.SchemeIs(extensions::kExtensionScheme) && IsWorkspaceExtensionRuntime()) ||
         ContentBrowserClient::IsHandledURL(url);
}

bool LabBrowserClientBase::IsSecurityLevelAcceptableForWebAuthn(
    content::RenderFrameHost* frame,
    const url::Origin& caller_origin) {
  const GURL caller_url = caller_origin.GetURL();
  if (!frame->IsActive() ||
      (!caller_url.SchemeIs("https") &&
       !(caller_url.SchemeIs("http") && net::IsLocalhost(caller_url)))) {
    return false;
  }

  auto* contents = content::WebContents::FromRenderFrameHost(frame);
  if (!contents)
    return false;
  auto& controller = contents->GetController();
  const auto* entry = controller.GetLastCommittedEntry();
  // Classify the requesting document. The visible entry may describe an
  // unrelated pending navigation while the current page is still running.
  if (!entry || entry->IsInitialEntry() ||
      entry != controller.GetVisibleEntry() ||
      entry->GetPageType() != content::PAGE_TYPE_NORMAL) {
    return false;
  }
  const GURL& page_url = entry->GetURL();
  if (page_url.SchemeIs("http"))
    return net::IsLocalhost(page_url) && net::IsLocalhost(caller_url);

  return page_url.SchemeIs("https") &&
         security_state::GetSecurityLevel(
             *security_state::GetVisibleSecurityState(contents)) ==
             security_state::SECURE;
}

content::WebAuthenticationDelegate*
LabBrowserClientBase::GetWebAuthenticationDelegate() {
  static base::NoDestructor<WorkspaceWebAuthenticationDelegate> delegate;
  return delegate.get();
}

#if BUILDFLAG(IS_MAC)
std::unique_ptr<content::AuthenticatorRequestClientDelegate>
LabBrowserClientBase::GetWebAuthenticationRequestDelegate(
    content::RenderFrameHost* frame) {
  return std::make_unique<WorkspaceMacAuthenticatorRequestDelegate>(*frame);
}
#endif

void LabBrowserClientBase::ConfigureNetworkContextParams(
    content::BrowserContext* context,
    bool in_memory,
    const base::FilePath& relative_partition_path,
    network::mojom::NetworkContextParams* params,
    cert_verifier::mojom::CertVerifierCreationParams* cert_params) {
  ContentBrowserClient::ConfigureNetworkContextParams(
      context, in_memory, relative_partition_path, params, cert_params);
  if (!relative_partition_path.empty()) {
    return;
  }

  const auto& certificate =
      static_cast<ProfileContext*>(context)->download_probe_certificate();
  if (certificate.empty())
    return;

  // Only the standalone download fixture supplies a leaf. Preserve normal
  // certificate verification and restrict this trust to the IPv4 loopback.
  if (!cert_params->initial_additional_certificates) {
    cert_params->initial_additional_certificates =
        cert_verifier::mojom::AdditionalCertificates::New();
  }
  auto leaf = cert_verifier::mojom::CertWithConstraints::New();
  leaf->certificate = certificate;
  leaf->permitted_cidrs.push_back(cert_verifier::mojom::CIDR::New(
      net::IPAddress::IPv4Localhost(), net::IPAddress(255, 255, 255, 255)));
  cert_params->initial_additional_certificates->trust_leafs.push_back(
      std::move(leaf));
}

bool LabBrowserClientBase::DoesSiteRequireDedicatedProcess(
    content::BrowserContext* context, const GURL& site_url) {
  return (site_url.SchemeIs(extensions::kExtensionScheme) &&
          !site_url.GetHost().empty() &&
          CanLoadExtensionProbeResources(*context, site_url.GetHost())) ||
         ContentBrowserClient::DoesSiteRequireDedicatedProcess(context, site_url);
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
LabBrowserClientBase::CreateNonNetworkNavigationURLLoaderFactory(
    const std::string& scheme, content::FrameTreeNodeId frame_tree_node_id) {
  if (scheme != extensions::kExtensionScheme)
    return {};
  auto* contents = content::WebContents::FromFrameTreeNodeId(frame_tree_node_id);
  if (!contents || !CanLoadExtensionProbeResources(*contents->GetBrowserContext()))
    return {};
  return extensions::CreateExtensionNavigationURLLoaderFactory(
      contents->GetBrowserContext(), /*is_web_view_request=*/false);
}

void LabBrowserClientBase::SiteInstanceGotProcessAndSite(
    content::SiteInstance* site) {
  RegisterExtensionProbeProcess(*site);
}

content::StoragePartitionConfig
LabBrowserClientBase::GetStoragePartitionConfigForSite(
    content::BrowserContext* context, const GURL& site) {
  if (IsExtensionProbeOrigin(*context, site)) {
    return extensions::util::GetStoragePartitionConfigForExtensionId(
        site.GetHost(), context);
  }
  return ContentBrowserClient::GetStoragePartitionConfigForSite(context, site);
}

content::AllowServiceWorkerResult LabBrowserClientBase::AllowServiceWorker(
    const GURL& scope,
    const net::SiteForCookies& site_for_cookies,
    const std::optional<url::Origin>& top_frame_origin,
    const blink::StorageKey& storage_key,
    const GURL& script_url,
    content::BrowserContext* context) {
  if (scope.SchemeIs(extensions::kExtensionScheme)) {
    return context &&
                   CanUseExtensionProbeWorker(*context, scope, storage_key,
                                              script_url)
               ? content::AllowServiceWorkerResult::Yes()
               : content::AllowServiceWorkerResult::No();
  }
  return ContentBrowserClient::AllowServiceWorker(
      scope, site_for_cookies, top_frame_origin, storage_key, script_url, context);
}

void LabBrowserClientBase::RegisterAssociatedInterfaceBindersForServiceWorker(
    const content::ServiceWorkerVersionBaseInfo& worker,
    blink::AssociatedInterfaceRegistry& registry) {
  auto* process = content::RenderProcessHost::FromID(worker.process_id);
  if (!process ||
      !CanUseExtensionProbeWorker(*process->GetBrowserContext(), worker.scope,
                                  worker.storage_key, worker.script_url)) {
    return;
  }
  registry.AddInterface<extensions::mojom::RendererHost>(base::BindRepeating(
      &extensions::RendererStartupHelper::BindForRenderer, worker.process_id));
  registry.AddInterface<extensions::mojom::ServiceWorkerHost>(base::BindRepeating(
      &extensions::ServiceWorkerHost::BindReceiver, worker.process_id));
  registry.AddInterface<extensions::mojom::EventRouter>(base::BindRepeating(
      &extensions::EventRouter::BindForRenderer, worker.process_id));
  registry.AddInterface<extensions::mojom::WebRequestHost>(base::BindRepeating(
      &extensions::WebRequestEventRouter::BindForRenderer, worker.process_id));
}

void LabBrowserClientBase::ExposeInterfacesToRenderer(
    service_manager::BinderRegistry*,
    blink::AssociatedInterfaceRegistry* registry,
    content::RenderProcessHost* process) {
  if (!IsWorkspaceExtensionRuntime() || process->GetBrowserContext()->IsOffTheRecord())
    return;
  registry->AddInterface<extensions::mojom::WebRequestHost>(base::BindRepeating(
      [](content::ChildProcessId process_id,
         mojo::PendingAssociatedReceiver<extensions::mojom::WebRequestHost> receiver) {
        auto* host = content::RenderProcessHost::FromID(process_id);
        if (host && CanRegisterExtensionWorkerFactory(*host->GetBrowserContext())) {
          extensions::WebRequestEventRouter::BindForRenderer(process_id, std::move(receiver));
        }
      },
      process->GetID()));
}

void LabBrowserClientBase::RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
    content::BrowserContext* context,
    NonNetworkURLLoaderFactoryMap* factories) {
  // Content captures these factories when it creates the storage partition.
  // A managed profile can create its dedicated partition to clear a stale
  // registration before enabling the extension.
  if (context && CanRegisterExtensionWorkerFactory(*context)) {
    factories->emplace(
        extensions::kExtensionScheme,
        extensions::CreateExtensionServiceWorkerScriptURLLoaderFactory(context));
  }
}

void LabBrowserClientBase::RegisterNonNetworkSubresourceURLLoaderFactories(
    int render_process_id, int render_frame_id,
    const std::optional<url::Origin>& initiator,
    NonNetworkURLLoaderFactoryMap* factories) {
  if (!initiator)
    return;
  auto* process = content::RenderProcessHost::FromID(render_process_id);
  if (!process)
    return;
  auto* frame = content::RenderFrameHost::FromID(render_process_id, render_frame_id);
  if (initiator->scheme() != extensions::kExtensionScheme) {
    const bool sandbox = initiator->opaque() &&
        initiator->GetTupleOrPrecursorTupleIfOpaque().scheme() == extensions::kExtensionScheme;
    if (frame && (initiator->GetURL().SchemeIsHTTPOrHTTPS() || sandbox) &&
        !frame->GetBrowserContext()->IsOffTheRecord() &&
        IsWorkspaceExtensionRuntime()) {
      auto target = extensions::CreateExtensionURLLoaderFactory(
          process->GetID(), render_frame_id);
      if (!target)
        return;
      mojo::PendingRemote<network::mojom::URLLoaderFactory> remote;
      base::MakeSelfDeleting<RestrictedExtensionLoaderFactory>(
          frame->GetGlobalId(), *initiator, std::move(target),
          remote.InitWithNewPipeAndPassReceiver());
      factories->emplace(extensions::kExtensionScheme, std::move(remote));
    }
    return;
  }
  // Service worker module imports have an extension process but no frame.
  const bool allowed = frame
      ? CanLoadExtensionProbeSubresources(*frame, *initiator)
      : render_frame_id == IPC::mojom::kRoutingIdNone &&
            CanLoadExtensionWorkerResources(*process, *initiator);
  if (allowed)
    factories->emplace(extensions::kExtensionScheme,
                       extensions::CreateExtensionURLLoaderFactory(
                           process->GetID(), render_frame_id));
}

std::u16string LabContentClient::GetLocalizedString(int message_id) {
  return ui::ResourceBundle::GetSharedInstance().GetLocalizedString(
      message_id);
}

std::string_view LabContentClient::GetDataResource(
    int resource_id, ui::ResourceScaleFactor scale_factor) {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
      resource_id, scale_factor);
}

scoped_refptr<base::RefCountedMemory>
LabContentClient::GetDataResourceBytes(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
      resource_id);
}

std::string LabContentClient::GetDataResourceString(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
      resource_id);
}

std::unique_ptr<content::DevToolsManagerDelegate>
LabBrowserClientBase::CreateDevToolsManagerDelegate() {
  // The pass-through delegate dispatches each command with the receiving
  // session's callback, including renderer commands on a tab's child session.
  return std::make_unique<content::DevToolsManagerDelegate>();
}

void LabBrowserClientBase::GetAdditionalWebUISchemes(
    std::vector<std::string>* schemes) {
  schemes->emplace_back(content::kChromeDevToolsScheme);
}

bool LabBrowserClientBase::CanCreateWindow(
    content::RenderFrameHost* opener, const GURL& opener_url,
    const GURL& opener_top_level_frame_url, const url::Origin& source_origin,
    content::mojom::WindowContainerType container_type, const GURL& target_url,
    const content::Referrer& referrer, const std::string& frame_name,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& features, bool user_gesture,
    bool opener_suppressed, bool* no_javascript_access) {
  *no_javascript_access = false;
  if (user_gesture)
    return true;
  if (!opener || !opener->IsActive())
    return false;
  const auto& origin = opener->GetLastCommittedOrigin();
  if (origin != opener->GetOutermostMainFrame()->GetLastCommittedOrigin())
    return false;
  auto* profile = static_cast<ProfileContext*>(opener->GetBrowserContext());
  return profile->site_permissions().GetPopupSetting(origin) ==
         ProfilePermissionController::PopupSetting::kAllow;
}

bool LabBrowserClientBase::HandleExternalProtocol(
    const GURL& url,
    base::RepeatingCallback<content::WebContents*()> web_contents_getter,
    content::FrameTreeNodeId,
    content::NavigationUIData*,
    bool is_primary_main_frame,
    bool is_in_fenced_frame_tree,
    network::mojom::WebSandboxFlags sandbox_flags,
    ui::PageTransition,
    bool,
    const std::optional<url::Origin>& initiating_origin,
    content::RenderFrameHost* initiator_document,
    const net::IsolationInfo&,
    mojo::PendingRemote<network::mojom::URLLoaderFactory>*) {
  // Restrict handoff to top-level web navigations. The confirmation is also
  // required for sign-in redirects, which may have no remaining user gesture.
  constexpr std::string_view kBlocked[] = {
      "about", "afp", "applescript", "blob", "chrome", "chrome-extension",
      "chrome-untrusted", "chromium", "data", "devtools", "disk", "disks",
      "file", "filesystem", "ftp", "google-chrome", "hcp", "ie.http", "intent",
      "javascript", "mk", "ms-help", "nntp", "res", "shell", "vbscript",
      "view-source", "vnd.ms.radio", "zitter"};
  using Sandbox = network::mojom::WebSandboxFlags;
  if (!url.is_valid() || url.SchemeIsHTTPOrHTTPS() ||
      url.scheme().size() < 2 || url.scheme().size() > 64 ||
      url.spec().size() > 8192 || std::ranges::contains(kBlocked, url.scheme()) ||
      !is_primary_main_frame || is_in_fenced_frame_tree ||
      (sandbox_flags & Sandbox::kTopNavigationToCustomProtocols) !=
          Sandbox::kNone ||
      !initiating_origin || !initiating_origin->GetURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  auto* page = web_contents_getter.Run();
  if (!page || page->GetVisibility() != content::Visibility::VISIBLE ||
      (initiator_document &&
                (!initiator_document->IsActive() ||
                 !initiator_document->IsInPrimaryMainFrame()))) {
    return false;
  }
  auto* dialogs = prototype::PageDialogs::FromWebContents(page);
  if (!dialogs)
    return false;
  const GURL escaped(base::EscapeExternalHandlerValue(url.spec()));
  if (!escaped.is_valid())
    return false;
  auto& permissions =
      static_cast<ProfileContext*>(page->GetBrowserContext())->site_permissions();
  if (permissions.IsExternalProtocolAllowed(*initiating_origin, url.scheme())) {
    LaunchExternalProtocol(escaped);
    return true;
  }
  dialogs->ConfirmExternalProtocol(
      base::UTF8ToUTF16(initiating_origin->Serialize()),
      base::UTF8ToUTF16(url.scheme()),
      permissions.CanRememberExternalProtocol(*initiating_origin),
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> page, url::Origin origin,
             GURL target, bool remember) {
            if (!page)
              return;
            if (remember) {
              static_cast<ProfileContext*>(page->GetBrowserContext())
                  ->site_permissions().AllowExternalProtocol(origin, target.scheme());
            }
            LaunchExternalProtocol(target);
          },
          page->GetWeakPtr(), *initiating_origin, escaped));
  return true;
}

void LabBrowserClientBase::CreateThrottlesForNavigation(
    content::NavigationThrottleRegistry& registry) {
  WorkspaceDevTools::AddNavigationThrottle(registry);
  prototype::LinkNavigation::CreateThrottle(registry);
  AddExtensionProbeNavigationThrottle(registry);
  if (navigation_reputation_)
    navigation_reputation_->AddThrottle(registry);
}

void LabBrowserClientBase::RegisterBrowserInterfaceBindersForFrame(
    content::RenderFrameHost* frame,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
#if BUILDFLAG(IS_MAC)
  map->Add<mojom::LocalSpelling>(base::BindRepeating(
      [](content::RenderFrameHost* frame, mojo::PendingReceiver<mojom::LocalSpelling> receiver) {
        mojo::MakeSelfOwnedReceiver(
            std::make_unique<LocalSpellingHost>(*frame->GetBrowserContext()), std::move(receiver));
      }));
#endif
#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
  map->Add<mojom::ScriptedPrint>(base::BindRepeating(&BindScriptedPrint));
#endif
}

void LabBrowserClientBase::RegisterAssociatedInterfaceBindersForRenderFrameHost(
    content::RenderFrameHost& frame,
    blink::AssociatedInterfaceRegistry& registry) {
#if BUILDFLAG(ENABLE_PRINTING)
  registry.AddInterface<printing::mojom::PrintManagerHost>(
      base::BindRepeating(
          [](content::RenderFrameHost* frame,
             mojo::PendingAssociatedReceiver<printing::mojom::PrintManagerHost>
                 receiver) {
            headless::HeadlessPrintManager::BindPrintManagerHost(
                std::move(receiver), frame);
          }, &frame));
#endif
  if (IsWorkspaceExtensionRuntime() && !frame.GetBrowserContext()->IsOffTheRecord()) {
    registry.AddInterface<extensions::mojom::LocalFrameHost>(base::BindRepeating(
        [](content::RenderFrameHost* frame,
           mojo::PendingAssociatedReceiver<extensions::mojom::LocalFrameHost> receiver) {
          extensions::ExtensionWebContentsObserver::BindLocalFrameHost(
              std::move(receiver), frame);
        },
        &frame));
    registry.AddInterface<extensions::mojom::EventRouter>(base::BindRepeating(
        [](content::RenderFrameHost* frame,
           mojo::PendingAssociatedReceiver<extensions::mojom::EventRouter> receiver) {
          if (CanLoadExtensionProbeResources(*frame->GetBrowserContext())) {
            extensions::EventRouter::BindForRenderer(
                frame->GetProcess()->GetID(), std::move(receiver));
          }
        },
        &frame));
  }
  registry.AddInterface<autofill::mojom::PasswordManagerDriver>(
      base::BindRepeating(
          [](content::RenderFrameHost* frame,
             mojo::PendingAssociatedReceiver<autofill::mojom::PasswordManagerDriver>
                 receiver) {
            if (frame->IsInPrimaryMainFrame()) {
              password_manager::ContentPasswordManagerDriverFactory::
                  BindPasswordManagerDriver(std::move(receiver), frame);
            }
          },
          &frame));
}

void LabBrowserClientBase::OnWebContentsCreated(content::WebContents* contents) {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  CHECK(!command_line.HasSwitch(switches::kSingleProcess));
  AttachExtensionWebContents(*contents);
  ProfileContext::AttachPage(*contents);
#if BUILDFLAG(ENABLE_PRINTING)
  headless::HeadlessPrintManager::CreateForWebContents(contents);
  printing::CreateCompositeClientIfNeeded(contents, GetUserAgent());
#endif
}

network::mojom::IPAddressSpace LabBrowserClientBase::DetermineAddressSpaceFromURL(
    const GURL& url) {
  if (IsWorkspaceExtensionRuntime() && url.SchemeIs(extensions::kExtensionScheme))
    return network::mojom::IPAddressSpace::kLoopback;
  return ContentBrowserClient::DetermineAddressSpaceFromURL(url);
}

bool LabBrowserClientBase::ShouldServiceWorkerInheritPolicyContainerFromCreator(
    const GURL& url) {
  return (IsWorkspaceExtensionRuntime() &&
          url.SchemeIs(extensions::kExtensionScheme)) ||
         ContentBrowserClient::ShouldServiceWorkerInheritPolicyContainerFromCreator(url);
}

void LabBrowserClientBase::OverrideURLLoaderFactoryParams(
    content::BrowserContext* context,
    const url::Origin& origin,
    bool is_for_isolated_world,
    bool is_for_service_worker,
    network::mojom::URLLoaderFactoryParams* params) {
  if (!context || context->IsOffTheRecord() || is_for_isolated_world ||
      origin.scheme() != extensions::kExtensionScheme ||
      !CanLoadExtensionProbeResources(*context, origin.host())) {
    return;
  }
  extensions::URLLoaderFactoryManager::OverrideURLLoaderFactoryParams(
      context, origin, is_for_isolated_world, is_for_service_worker, params);
}

void LabBrowserClientBase::WillCreateURLLoaderFactory(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    int render_process_id,
    URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation,
    std::optional<int64_t> navigation_id,
    ukm::SourceIdObj source_id,
    network::URLLoaderFactoryBuilder& builder,
    mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>* header_client,
    bool*,
    bool*,
    network::mojom::URLLoaderFactoryOverridePtr*,
    scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner,
    bool) {
  if (!browser_context)
    return;
  if (auto* proxy = WorkspaceExtensionRequestProxy(*browser_context)) {
    proxy->MaybeProxyURLLoaderFactory(
        browser_context, frame, render_process_id, type, navigation_id, source_id,
        builder, header_client, std::move(navigation_response_task_runner),
        request_initiator);
  }
  const GURL page = prototype::AdblockTopLevelPage(frame, type, isolation);
  if (!page.is_empty()) {
    static_cast<ProfileContext*>(browser_context)
        ->MaybeFilterAdblockRequests(page, frame, source_id.ToInt64(), builder);
  }
}

void LabRendererClient::RenderThreadStarted() {
  CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch(
      sandbox::policy::switches::kNoSandbox));
  CHECK(sandbox::policy::Sandbox::IsProcessSandboxed());
  if (IsWorkspaceExtensionRuntime()) {
    InitializeLabExtensionsClient();
    static base::NoDestructor<LabExtensionsRendererClient> client;
    extensions::ExtensionsRendererClient::Set(client.get());
    client->AddAPIProvider(
        std::make_unique<LabExtensionsRendererAPIProvider>());
    client->RenderThreadStarted();
  }
}

void LabRendererClient::PrepareErrorPage(
    content::RenderFrame* frame,
    const blink::WebURLError& error,
    const std::string& http_method,
    content::mojom::AlternativeErrorPageOverrideInfoPtr alternative,
    std::string* error_html) {
  if (!error_html || !frame->IsMainFrame() || !net::IsCertificateError(error.reason()))
    return;
  // The only substituted text is Chromium's constant error identifier.
  *error_html = R"HTML(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'">
<title>Certificate could not be verified</title>
<style>
  :root { color-scheme: light dark; background: Canvas; color: CanvasText; }
  body { margin: 0; padding: clamp(16px, 6vw, 64px); font: 15px/1.5 system-ui, sans-serif; }
  main { max-width: 34rem; margin: 0 auto; }
  h1 { font-size: clamp(20px, 4vw, 28px); line-height: 1.2; margin: 0 0 16px; }
  p { margin: 0 0 12px; }
  code { overflow-wrap: anywhere; }
</style>
<main id="certificate-error">
  <h1>Certificate could not be verified</h1>
  <p>Zitter blocked this connection. Check the address or go back.</p>
  <code>)HTML" + net::ErrorToShortString(error.reason()) + "</code>\n</main>\n";
}

void LabRendererClient::WebViewCreated(blink::WebView* view,
                                       bool was_created_by_renderer,
                                       const url::Origin* outermost_origin) {
  if (ExtensionDispatcher()) {
    extensions::ExtensionsRendererClient::Get()->WebViewCreated(
        view, outermost_origin);
  }
}

void LabRendererClient::RenderFrameCreated(content::RenderFrame* frame) {
#if BUILDFLAG(IS_MAC)
  new LocalSpellingClient(frame);
#endif
  if (frame->IsMainFrame() && !frame->IsInFencedFrameTree())
    new AdblockProbeObserver(frame);
  frame->GetAssociatedInterfaceRegistry()->AddInterface<mojom::FingerprintState>(
      base::BindRepeating(
          [](mojo::PendingAssociatedReceiver<mojom::FingerprintState> receiver) {
            mojo::MakeSelfOwnedAssociatedReceiver(
                std::make_unique<FingerprintStateReceiver>(), std::move(receiver));
          }));
#if BUILDFLAG(ENABLE_PRINTING)
  AttachPrintRenderer(frame);
#endif
  if (frame->IsMainFrame() && !frame->IsInFencedFrameTree()) {
    auto* registry = frame->GetAssociatedInterfaceRegistry();
    new autofill::AutofillAgent(
        frame, std::make_unique<autofill::PasswordAutofillAgent>(frame, registry),
        /*password_generation_agent=*/nullptr, registry);
  }
  if (auto* dispatcher = ExtensionDispatcher()) {
    // Keep platform-app window bindings out of the Content host.
    new extensions::ExtensionFrameHelper(frame, dispatcher);
    dispatcher->OnRenderFrameCreated(frame);
    if (frame->IsMainFrame() && !frame->IsInFencedFrameTree())
      new OffscreenClipboardSettings(frame);
  }
}

void LabRendererClient::RunScriptsAtDocumentStart(content::RenderFrame* frame) {
  if (frame->IsMainFrame() && !frame->IsInFencedFrameTree()) {
    if (auto* probe = AdblockProbeObserver::Get(frame);
        probe && !probe->RunAtDocumentStart()) {
      return;
    }
  }
  auto* dispatcher = ExtensionDispatcher();
  if (!dispatcher) {
    zitter::RunPageScripts(frame);
    return;
  }

  auto* web_frame = frame->GetWebFrame();
  const auto frame_token = web_frame->GetLocalFrameToken();
  const auto document_token = web_frame->GetDocument().Token();
  zitter::RunPageScripts(frame);

  // A scriptlet can detach the frame or replace its document synchronously.
  web_frame = blink::WebLocalFrame::FromFrameToken(frame_token);
  if (web_frame && !web_frame->GetDocument().IsNull() &&
      web_frame->GetDocument().Token() == document_token) {
    if (auto* current_frame = content::RenderFrame::FromWebFrame(web_frame))
      dispatcher->RunScriptsAtDocumentStart(current_frame);
  }
}

void LabRendererClient::RunScriptsAtDocumentEnd(content::RenderFrame* frame) {
  if (auto* dispatcher = ExtensionDispatcher())
    dispatcher->RunScriptsAtDocumentEnd(frame);
}

void LabRendererClient::RunScriptsAtDocumentIdle(content::RenderFrame* frame) {
  if (auto* dispatcher = ExtensionDispatcher())
    dispatcher->RunScriptsAtDocumentIdle(frame);
}

void LabRendererClient::DidInitializeServiceWorkerContextOnWorkerThread(
    blink::WebServiceWorkerContextProxy* proxy,
    const GURL& scope,
    const GURL& script) {
  if (auto* dispatcher = ExtensionDispatcher())
    dispatcher->DidInitializeServiceWorkerContextOnWorkerThread(proxy, scope,
                                                               script);
}

void LabRendererClient::WillEvaluateServiceWorkerOnWorkerThread(
    blink::WebServiceWorkerContextProxy* proxy,
    v8::Local<v8::Context> context,
    int64_t version_id,
    const GURL& scope,
    const GURL& script,
    const blink::ServiceWorkerToken& token) {
  if (auto* dispatcher = ExtensionDispatcher()) {
    dispatcher->WillEvaluateServiceWorkerOnWorkerThread(
        proxy, context, version_id, scope, script, token);
  }
}

void LabRendererClient::DidStartServiceWorkerContextOnWorkerThread(
    int64_t version_id,
    const GURL& scope,
    const GURL& script,
    const blink::ServiceWorkerToken& token) {
  if (auto* dispatcher = ExtensionDispatcher()) {
    dispatcher->DidStartServiceWorkerContextOnWorkerThread(
        version_id, scope, script, token);
  }
}

void LabRendererClient::WillDestroyServiceWorkerContextOnWorkerThread(
    v8::Local<v8::Context> context,
    int64_t version_id,
    const GURL& scope,
    const GURL& script,
    const blink::ServiceWorkerToken& token) {
  if (auto* dispatcher = ExtensionDispatcher()) {
    dispatcher->WillDestroyServiceWorkerContextOnWorkerThread(
        context, version_id, scope, script, token);
  }
}

GURL LabStartPage() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-file-chooser-smoke-test")) {
    return GURL(R"HTML(data:text/html,<!doctype html>
      <meta charset=utf-8><meta name=color-scheme content="light dark">
      <title>Zitter Content file chooser probe</title>
      <h1>File chooser probe</h1>
      <p><label>Single text file <input type=file accept="text/plain"></label></p>
      <p><label>Multiple files <input type=file multiple></label></p>
      <p><label>Folder upload <input type=file webkitdirectory></label></p>
      <p><button id=replace>Replace document</button></p>
      <pre id=result aria-live=polite></pre>
      <script>
        const result = document.querySelector('pre');
        document.querySelectorAll('input').forEach(input => {
          input.addEventListener('cancel', () => result.textContent = 'Canceled');
          input.addEventListener('change', async () => {
            const files = [...input.files];
            const lines = await Promise.all(files.map(async file =>
              (file.webkitRelativePath || file.name) + ': ' +
              (file.size < 256 ? await file.text() : file.size + ' bytes')));
            result.textContent = lines.sort().join(' | ');
          });
        });
        document.querySelector('button').onclick = () => setTimeout(() => {
          location.reload();
        }, 3000);
      </script>)HTML");
  }
  return GURL(
      "data:text/html,<!doctype html><meta charset=utf-8>"
      "<meta name=color-scheme content='light dark'>"
      "<script>document.title='Zitter Content Lab'</script>"
      "<h1>Zitter Content Lab</h1>"
      "<p>This page is rendered in a temporary browser context.</p>");
}

std::optional<int> CheckLabStartupFlags() {
  auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kProcessType))
    return std::nullopt;
  // The launcher prefix switches are private to Content in component builds.
  if (command_line.HasSwitch(sandbox::policy::switches::kNoSandbox) ||
      command_line.HasSwitch(sandbox::policy::switches::kDisableGpuSandbox) ||
      command_line.HasSwitch(switches::kSingleProcess) ||
      command_line.HasSwitch(switches::kInProcessGPU) ||
      command_line.HasSwitch(switches::kBrowserSubprocessPath) ||
      command_line.HasSwitch("renderer-cmd-prefix") ||
      command_line.HasSwitch("gpu-launcher") ||
      command_line.HasSwitch("utility-cmd-prefix") ||
      command_line.HasSwitch(switches::kIgnoreCertificateErrors) ||
      command_line.HasSwitch(switches::kAllowInsecureLocalhost) ||
      command_line.HasSwitch(network::switches::kIgnoreCertificateErrorsSPKIList)) {
    return 1;
  }
  const bool tls_probe = command_line.HasSwitch(kWorkspaceTlsProbe);
  const bool trusted_tls_probe = command_line.HasSwitch(kWorkspaceTrustedTlsProbe);
  if (command_line.HasSwitch(kWorkspaceNativePasskeyPreflight) &&
      (!BUILDFLAG(IS_MAC) || !command_line.HasSwitch(kWorkspaceSmokeTest) ||
       command_line.HasSwitch(kWorkspaceRoot) ||
       command_line.HasSwitch(kWorkspaceWebAuthnProbe) ||
       command_line.HasSwitch(kWorkspaceProbe) || tls_probe || trusted_tls_probe)) {
    return 1;
  }
  if (command_line.HasSwitch(kWorkspaceWebAuthnProbe) &&
      (!command_line.HasSwitch(kWorkspaceSmokeTest) ||
       command_line.HasSwitch(kWorkspaceRoot) || tls_probe ||
       trusted_tls_probe)) {
    return 1;
  }
  if ((tls_probe || trusted_tls_probe) &&
      (!command_line.HasSwitch(kWorkspaceSmokeTest) ||
       (tls_probe && trusted_tls_probe))) {
    return 1;
  }
  if (command_line.HasSwitch(kOpenPreparedWorkspace) &&
      (!command_line.HasSwitch(kWorkspaceLab) ||
       command_line.GetSwitchValuePath(kWorkspaceRoot).empty())) {
    return 1;
  }
  if (command_line.HasSwitch(kWorkspaceHistoryPanelProbe) &&
      !command_line.HasSwitch(kWorkspaceProbe)) {
    return 1;
  }
  if (command_line.HasSwitch(kWorkspacePasswordProbe) &&
      (!command_line.HasSwitch(kWorkspaceProbe) ||
       command_line.HasSwitch(kWorkspaceSmokeTest) ||
       IsWorkspaceExtensionRuntime() ||
       command_line.HasSwitch(kWorkspaceHistoryPanelProbe) ||
       command_line.HasSwitch(kWorkspaceAddressHistoryProbe) ||
       command_line.HasSwitch(kWorkspaceClearDataProbe))) {
    return 1;
  }
  if (command_line.HasSwitch(kWorkspaceClearDataProbe) &&
      !command_line.HasSwitch(kWorkspaceProbe)) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionInstallReview) &&
      (!command_line.HasSwitch(kWorkspaceLab) ||
       !command_line.HasSwitch(kWorkspaceRoot) ||
       command_line.GetSwitchValuePath(kExtensionRegistryProbe).empty() ||
       command_line.GetSwitchValueASCII(kExtensionInstallReview).empty() ||
       command_line.HasSwitch(kExtensionFixtureUpdate) ||
       command_line.HasSwitch(kExtensionFixturePeer) ||
       command_line.HasSwitch(kExtensionFixtureSibling) ||
       command_line.HasSwitch(kExtensionFixtureState))) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionFixtureUpdate) &&
      !command_line.HasSwitch(kExtensionRegistryProbe)) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionFixturePeer) &&
      (!command_line.HasSwitch(kExtensionRegistryProbe) ||
       command_line.HasSwitch(kExtensionFixtureUpdate))) {
    return 1;
  }
  const bool restoring_extensions = command_line.HasSwitch(kRestoreWorkspaceExtensions);
  if ((command_line.HasSwitch(kExtensionInstallUiCancel) &&
       !command_line.HasSwitch(kExtensionInstallUiProbe)) ||
      (command_line.HasSwitch(kExtensionInstallUiProbe) &&
       (!restoring_extensions || !command_line.HasSwitch(kWorkspaceProbe) ||
        command_line.HasSwitch(kExtensionFixtureState) ||
        command_line.GetSwitchValuePath(kExtensionInstallUiProbe).empty()))) {
    return 1;
  }
  if (restoring_extensions &&
      (!command_line.HasSwitch(kWorkspaceLab) ||
       !command_line.HasSwitch(kWorkspaceRoot) ||
       command_line.HasSwitch(kExtensionRegistryProbe) ||
       command_line.HasSwitch(kExtensionManifestProbe) ||
       command_line.HasSwitch(kExtensionFixtureKeyHash) ||
       command_line.HasSwitch(kExtensionFixtureSiblingKeyHash))) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionFixtureState) &&
      (!IsWorkspaceExtensionRuntime() ||
       (command_line.HasSwitch(kExtensionFixturePeer) && !IsManagedExtensionSession()) ||
       command_line.HasSwitch(kExtensionFixtureUpdate) ||
       !GetExtensionFixtureStateAction() ||
       (restoring_extensions &&
        GetExtensionFixtureStateAction() != ExtensionFixtureStateAction::kManageSmoke))) {
    return 1;
  }
  if (GetExtensionFixtureStateAction() == ExtensionFixtureStateAction::kManageSmoke &&
      !restoring_extensions &&
      !command_line.HasSwitch(kExtensionFixturePeer) &&
      !command_line.HasSwitch(kExtensionFixtureSibling)) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionFixtureSibling) &&
      (!IsManagedExtensionSession() ||
       !command_line.HasSwitch(kExtensionRegistryProbe) ||
       !command_line.HasSwitch(kExtensionFixtureSiblingKeyHash) ||
       command_line.HasSwitch(kExtensionFixturePeer))) {
    return 1;
  }
  if (command_line.HasSwitch(kExtensionFixtureProfile) &&
      ((!command_line.HasSwitch(kExtensionRegistryProbe) &&
        !command_line.HasSwitch(kExtensionInstallUiProbe)) ||
       !ExtensionFixtureProfileIndex())) {
    LOG(ERROR) << "Content extension fixture profile must be work or personal";
    return 1;
  }
  if (command_line.HasSwitch(kExtensionRegistryProbe) &&
      (!command_line.HasSwitch(kWorkspaceProbe) ||
       !command_line.HasSwitch(kExtensionFixtureKeyHash) ||
       command_line.HasSwitch(kExtensionManifestProbe))) {
    return 1;
  }
  if (command_line.HasSwitch(kWorkspaceAddressHistoryProbe) &&
      !command_line.HasSwitch(kWorkspaceProbe) &&
      !command_line.HasSwitch(kWorkspaceSmokeTest)) {
    return 1;
  }
  command_line.AppendSwitch("zitter-content-protect-download-target");
  return std::nullopt;
}

}  // namespace zitter::content_embedder
