// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_devtools.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "build/build_config.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/url_constants.h"
#include "ipc/constants.mojom.h"
#include "net/base/mime_util.h"
#include "profile_context.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/events/base_event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#endif

namespace zitter::content_embedder {
namespace {

constexpr char kFrontend[] =
    "devtools://devtools/bundled/devtools_app.html?targetType=tab&can_dock=true";
constexpr std::string_view kBundledPrefix = "/bundled/";
constexpr int kTargetProbeId = 1000000000;
constexpr int kIsolationProbeId = 1000000001;

enum class DockProbe { kInitial, kUndock, kRedock, kConsole, kInspect, kPick };

class BundledFrontend final : public content::URLDataSource {
 public:
  std::string GetSource() override { return "devtools"; }

  bool ShouldServiceRequest(const GURL& url,
                            content::BrowserContext*,
                            int) override {
    return url.SchemeIs(content::kChromeDevToolsScheme) &&
           url.host() == "devtools" && url.path().starts_with(kBundledPrefix);
  }

  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter&,
                        GotDataCallback callback) override {
    auto path = url.path();
    if (!path.starts_with(kBundledPrefix)) {
      std::move(callback).Run(nullptr);
      return;
    }
    path.remove_prefix(kBundledPrefix.size());
    std::move(callback).Run(
        content::DevToolsFrontendHost::GetFrontendResourceBytes(path));
  }

  std::string GetMimeType(const GURL& url) override {
    std::string mime;
    net::GetMimeTypeFromFile(base::FilePath::FromUTF8Unsafe(url.path()), &mime);
    return mime;
  }

  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }

  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    switch (directive) {
      case network::mojom::CSPDirectiveName::ScriptSrc:
        return "script-src 'self';";
      case network::mojom::CSPDirectiveName::ObjectSrc:
        return "object-src 'none';";
      case network::mojom::CSPDirectiveName::ConnectSrc:
        return "connect-src 'self';";
      case network::mojom::CSPDirectiveName::FrameSrc:
        return "frame-src 'none';";
      default:
        return {};
    }
  }
};

class FrontendNavigation final : public content::NavigationThrottle {
 public:
  explicit FrontendNavigation(content::NavigationThrottleRegistry& registry)
      : NavigationThrottle(registry) {}

  ThrottleCheckResult WillStartRequest() override {
    return navigation_handle()->IsInPrimaryMainFrame() &&
                   navigation_handle()->GetURL() == GURL(kFrontend)
               ? PROCEED
               : CANCEL_AND_IGNORE;
  }

  ThrottleCheckResult WillRedirectRequest() override { return CANCEL_AND_IGNORE; }

  ThrottleCheckResult WillCommitWithoutUrlLoader() override {
    return WillStartRequest();
  }

  const char* GetNameForLogging() override { return "ZitterDevToolsFrontend"; }
};

}  // namespace

// The probe owns a single, in-process DevTools session. Its commands are
// fixed here and never come from the fixture page or a debugging listener.
class WorkspaceWebAuthnProbe::Client final
    : public content::DevToolsAgentHostClient {
 public:
  Client(content::WebContents& target, base::OnceCallback<void(bool)> done)
      : url_(target.GetLastCommittedURL()), done_(std::move(done)) {
    agent_ = content::DevToolsAgentHost::GetOrCreateFor(&target);
    if (!agent_->AttachClient(this)) {
      agent_.reset();
      Finish(false);
      return;
    }
    Send(kEnable, "WebAuthn.enable", base::DictValue().Set("enableUI", false));
  }

  ~Client() override {
    if (agent_)
      agent_->DetachClient(this);
  }

 private:
  enum Command { kEnable = 1, kAddAuthenticator, kEvaluate };

  bool MayAttachToURL(const GURL& url, bool is_webui) override {
    return !is_webui && url == url_;
  }
  bool IsTrusted() override { return false; }
  bool MayReadLocalFiles() override { return false; }
  bool MayWriteLocalFiles() override { return false; }

  void AgentHostClosed(content::DevToolsAgentHost*) override {
    agent_.reset();
    Finish(false);
  }

  void DispatchProtocolMessage(content::DevToolsAgentHost*,
                               base::span<const uint8_t> message) override {
    auto reply = base::JSONReader::ReadDict(base::as_string_view(message),
                                          base::JSON_PARSE_RFC);
    if (!done_ || !reply || reply->FindInt("id") != pending_)
      return;
    if (reply->contains("error") ||
        reply->FindByDottedPath("result.exceptionDetails")) {
      LOG(ERROR) << "WebAuthn probe command " << pending_ << " failed: "
                 << base::as_string_view(message);
      Finish(false);
      return;
    }

    switch (pending_) {
      case kEnable:
        Send(kAddAuthenticator, "WebAuthn.addVirtualAuthenticator",
             base::DictValue().Set(
                 "options", base::DictValue()
                                .Set("protocol", "ctap2")
                                .Set("transport", "internal")
                                .Set("hasResidentKey", true)
                                .Set("hasUserVerification", true)
                                .Set("isUserVerified", true)));
        return;
      case kAddAuthenticator:
        Send(kEvaluate, "Runtime.evaluate",
             base::DictValue()
                 .Set("expression", kTest)
                 .Set("awaitPromise", true)
                 .Set("returnByValue", true)
                 .Set("userGesture", true));
        return;
      case kEvaluate:
        Finish(reply->FindBoolByDottedPath("result.result.value").value_or(false));
        return;
    }
  }

  void Send(Command command, const char* method, base::DictValue params) {
    pending_ = command;
    auto message = base::WriteJson(base::DictValue()
                                       .Set("id", static_cast<int>(command))
                                       .Set("method", method)
                                       .Set("params", std::move(params)));
    agent_->DispatchProtocolMessage(this, base::as_byte_span(*message));
  }

  void Finish(bool success) {
    if (done_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(done_), success));
    }
  }

  static constexpr char kTest[] = R"JS(
(async () => {
  const challenge = () => crypto.getRandomValues(new Uint8Array(32));
  const equal = (actual, expected) => actual.length === expected.length &&
      actual.every((byte, index) => byte === expected[index]);

  // WebAuthn returns DER ECDSA signatures. Web Crypto expects two
  // fixed-width P-256 scalars concatenated in big-endian order.
  function es256Signature(der) {
    const bytes = new Uint8Array(der);
    if (bytes[0] !== 0x30 || bytes[1] !== bytes.length - 2)
      throw new Error('Invalid ES256 signature sequence');
    const raw = new Uint8Array(64);
    let offset = 2;
    for (const end of [32, 64]) {
      if (bytes[offset++] !== 0x02)
        throw new Error('Invalid ES256 signature integer');
      const length = bytes[offset++];
      const next = offset + length;
      if (!length || next > bytes.length || (bytes[offset] & 0x80))
        throw new Error('Invalid ES256 signature integer length or sign');
      if (length > 1 && bytes[offset] === 0 && (bytes[offset + 1] & 0x80))
        ++offset;
      if (next - offset > 32)
        throw new Error('Invalid ES256 signature scalar');
      raw.set(bytes.subarray(offset, next), end - (next - offset));
      offset = next;
    }
    if (offset !== bytes.length)
      throw new Error('Unexpected data after ES256 signature');
    return raw;
  }

  const userId = crypto.getRandomValues(new Uint8Array(32));
  const credential = await navigator.credentials.create({ publicKey: {
    challenge: challenge(),
    rp: { name: 'Zitter WebAuthn probe' },
    user: { id: userId, name: 'fixture', displayName: 'Fixture' },
    pubKeyCredParams: [{ type: 'public-key', alg: -7 }],
    authenticatorSelection: {
      authenticatorAttachment: 'platform',
      residentKey: 'required',
      userVerification: 'required',
    },
    timeout: 5000,
  } });
  try {
    await navigator.credentials.get({ publicKey: {
      challenge: challenge(),
      rpId: '127.0.0.1',
      userVerification: 'required',
      timeout: 5000,
    } });
    throw new Error('WebAuthn accepted an invalid relying party ID');
  } catch (error) {
    if (error.name !== 'SecurityError')
      throw error;
  }

  const assertionChallenge = challenge();
  const assertion = await navigator.credentials.get({ publicKey: {
    challenge: assertionChallenge,
    userVerification: 'required',
    timeout: 5000,
  } });
  const {authenticatorData, clientDataJSON, signature, userHandle} =
      assertion.response;
  const authData = new Uint8Array(authenticatorData);
  const clientData = JSON.parse(new TextDecoder().decode(clientDataJSON));
  const rpHash = new Uint8Array(await crypto.subtle.digest(
      'SHA-256', new TextEncoder().encode(location.hostname)));
  if (credential.id !== assertion.id ||
      !equal(new Uint8Array(userHandle), userId) ||
      (authData[32] & 0x05) !== 0x05 ||
      !equal(authData.subarray(0, 32), rpHash) ||
      clientData.type !== 'webauthn.get' ||
      clientData.origin !== location.origin ||
      clientData.challenge !== assertionChallenge.toBase64(
          { alphabet: 'base64url', omitPadding: true })) {
    throw new Error('WebAuthn assertion did not match the registered credential');
  }

  const publicKey = await crypto.subtle.importKey(
      'spki', credential.response.getPublicKey(),
      { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
  const clientHash = new Uint8Array(
      await crypto.subtle.digest('SHA-256', clientDataJSON));
  const signed = new Uint8Array(authData.length + clientHash.length);
  signed.set(authData);
  signed.set(clientHash, authData.length);
  return crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' },
                             publicKey, es256Signature(signature), signed);
})()
)JS";

  const GURL url_;
  scoped_refptr<content::DevToolsAgentHost> agent_;
  base::OnceCallback<void(bool)> done_;
  Command pending_ = kEnable;
};

WorkspaceWebAuthnProbe::WorkspaceWebAuthnProbe(
    content::WebContents& target,
    base::OnceCallback<void(bool)> done)
    : client_(std::make_unique<Client>(target, std::move(done))) {}

WorkspaceWebAuthnProbe::~WorkspaceWebAuthnProbe() = default;

class WorkspaceDevTools::Window final
    : private content::WebContentsObserver,
      private content::DevToolsAgentHostClient,
      private views::WidgetObserver {
 public:
  Window(const base::FilePath& path,
         content::WebContents& target,
         Dock dock,
         base::OnceClosure ready)
      : context_(path, ProfileContext::Mode::kPrivate),
        target_(target.GetWeakPtr()),
        dock_(std::move(dock)),
        ready_(std::move(ready)) {
    content::URLDataSource::Add(&context_, std::make_unique<BundledFrontend>());
    contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(&context_));
    Observe(contents_.get());
    web_contents()->SetUserData(&kFrontendKey,
                                std::make_unique<base::SupportsUserData::Data>());
    dock_.Run(contents_.get(), std::nullopt);
    web_contents()->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(GURL(kFrontend)));
  }

  ~Window() override {
    Close();
    Observe(nullptr);
    if (widget_) {
      widget_->RemoveObserver(this);
      undocked_view_ = nullptr;
      widget_->CloseNow();
    }
  }

  bool IsOpenFor(content::WebContents* target) const {
    return !closed_ && target_.get() == target;
  }

  void Show(Action action) {
    pending_action_ = action;
    if (widget_ && !docked_)
      widget_->Activate();
    if (loaded_)
      ApplyAction();
  }

  static bool OwnsFrontend(const content::WebContents& contents) {
    return contents.GetUserData(&kFrontendKey);
  }

 private:
  void ApplyAction() {
    if (pending_action_ == Action::kConsole)
      CallFrontend(u"showPanel", base::ListValue().Append("console"));
    else if (pending_action_ == Action::kInspect)
      CallFrontend(u"enterInspectElementMode", {});
    if (!ready_)
      contents_->Focus();
    pending_action_ = Action::kShow;
  }

  void CreateUndockedWindow() {
    delegate_.SetTitle(u"Zitter DevTools");
    delegate_.SetHasWindowSizeControls(true);
    undocked_view_ =
        delegate_.SetContentsView(std::make_unique<views::WebView>(&context_));
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                    views::Widget::InitParams::TYPE_WINDOW);
    params.delegate = &delegate_;
    params.bounds = gfx::Rect(140, 140, 900, 650);
    widget_ = std::make_unique<views::Widget>();
#if BUILDFLAG(IS_WIN)
    params.native_widget = new views::DesktopNativeWidgetAura(widget_.get());
#endif
    widget_->Init(std::move(params));
    widget_->AddObserver(this);
  }

  void SetDocked(bool docked) {
    if (docked_ == docked)
      return;
    docked_ = docked;
    probe_has_bounds_ = false;
    if (docked_) {
      undocked_view_->SetWebContents(nullptr);
      widget_->Hide();
      dock_.Run(contents_.get(), std::nullopt);
    } else {
      dock_.Run(nullptr, std::nullopt);
      if (!widget_)
        CreateUndockedWindow();
      undocked_view_->SetWebContents(contents_.get());
      if (ready_)
        widget_->ShowInactive();
      else
        widget_->Show();
    }
  }

  void SetPageBounds(const base::DictValue& bounds) {
    const auto x = bounds.FindInt("x");
    const auto y = bounds.FindInt("y");
    const auto width = bounds.FindInt("width");
    const auto height = bounds.FindInt("height");
    const auto valid = [](std::optional<int> value) {
      return value && *value >= 0 && *value <= 32768;
    };
    if (docked_ && valid(x) && valid(y) && valid(width) && valid(height)) {
      dock_.Run(contents_.get(), gfx::Rect(*x, *y, *width, *height));
      probe_has_bounds_ = *width > 0 && *height > 0;
      AdvanceDockProbe();
    }
  }

  void OnWidgetClosing(views::Widget*) override {
    if (undocked_view_) {
      undocked_view_->SetWebContents(nullptr);
      undocked_view_ = nullptr;
    }
    Close();
  }

  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame())
      return;
    frontend_.reset();
    if (navigation->GetURL() != GURL(kFrontend) || navigation->IsErrorPage()) {
      return;
    }
    frontend_ = content::DevToolsFrontendHost::Create(
        navigation->GetRenderFrameHost(),
        base::BindRepeating(&Window::HandleMessage, base::Unretained(this)));
  }

  void PrimaryMainDocumentElementAvailable() override {
    if (!frontend_ || !target_ || agent_)
      return;
    agent_ = content::DevToolsAgentHost::GetOrCreateForTab(target_.get());
    if (!agent_->AttachClient(this)) {
      agent_.reset();
      Close();
    }
  }

  bool MayAttachToURL(const GURL& url, bool is_webui) override {
    return !is_webui && (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("data") ||
                        url.IsAboutBlank() || url.IsAboutSrcdoc());
  }

  bool MayAttachToRenderFrameHost(content::RenderFrameHost* frame) override {
    return target_ && frame &&
           frame->GetOutermostMainFrame() == target_->GetPrimaryMainFrame();
  }

  bool IsTrusted() override { return false; }
  bool MayReadLocalFiles() override { return false; }
  bool MayWriteLocalFiles() override { return false; }
  bool AllowUnsafeOperations() override { return false; }

  void AgentHostClosed(content::DevToolsAgentHost*) override {
    agent_.reset();
    target_.reset();
    Close();
  }

  void Close() {
    if (closed_)
      return;
    closed_ = true;
    if (docked_)
      dock_.Run(nullptr, std::nullopt);
    if (widget_ && !widget_->IsClosed()) {
      if (undocked_view_)
        undocked_view_->SetWebContents(nullptr);
      widget_->Close();
    }
    frontend_.reset();
    if (agent_) {
      agent_->DetachClient(this);
      agent_.reset();
    }
    target_.reset();
  }

  void HandleMessage(base::DictValue message) {
    if (closed_ || !web_contents() ||
        web_contents()->GetLastCommittedURL() != GURL(kFrontend))
      return;
    const auto* method = message.FindString("method");
    if (!method)
      return;
    auto* params = message.FindList("params");
    base::DictValue response;
    if (*method == "dispatchProtocolMessage" && agent_ && params &&
        params->size() == 1) {
      if (const auto* payload = (*params)[0].GetIfString()) {
        if (ready_ && dock_probe_ == DockProbe::kInspect && !probe_inspect_id_) {
          const auto request =
              base::JSONReader::ReadDict(*payload, base::JSON_PARSE_RFC);
          if (request) {
            const auto* name = request->FindString("method");
            const auto* session = request->FindString("sessionId");
            const auto* mode = request->FindStringByDottedPath("params.mode");
            if (name && *name == "Overlay.setInspectMode" && session &&
                !session->empty() && mode && *mode == "searchForNode") {
              probe_inspect_id_ = request->FindInt("id");
            }
          }
        }
        agent_->DispatchProtocolMessage(this, base::as_byte_span(*payload));
      }
    } else if (*method == "loadCompleted") {
      CallFrontend(u"setUseSoftMenu", base::ListValue().Append(true));
      if (ready_ && agent_) {
        SendProbeCommand(kTargetProbeId, "Target.getTargetInfo");
        SendProbeCommand(kIsolationProbeId, "Target.getTargets");
      }
    } else if (*method == "connectionReady") {
      loaded_ = true;
      ApplyAction();
      AdvanceDockProbe();
    } else if (ready_ && *method == "zitterConsoleSelected" &&
               dock_probe_ == DockProbe::kConsole) {
      LOG(INFO) << "DevTools probe verified the Console panel";
      dock_probe_ = DockProbe::kInspect;
      Show(Action::kInspect);
    } else if (ready_ && *method == "inspectElementCompleted" &&
               dock_probe_ == DockProbe::kPick) {
      contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
          u"import('devtools://devtools/bundled/ui/legacy/legacy.js')"
          u".then(({InspectorView}) => {"
          u"const panel = InspectorView.InspectorView.instance().currentPanelDeprecated();"
          u"if (panel?.selectedDOMNode?.()?.getAttribute('id') === 'inspect-probe')"
          u"DevToolsAPI.sendMessageToEmbedder('zitterNodeSelected', [], null);"
          u"})",
          {});
    } else if (ready_ && *method == "zitterNodeSelected" &&
               dock_probe_ == DockProbe::kPick) {
      LOG(INFO) << "DevTools probe verified the selected DOM node";
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(ready_));
    } else if (*method == "getPreferences") {
      response = preferences_.Clone();
    } else if (*method == "setPreference" && params && params->size() == 2) {
      if (const auto* key = (*params)[0].GetIfString(); key && (*params)[1].is_string())
        preferences_.Set(*key, std::move((*params)[1]));
    } else if (*method == "removePreference" && params && params->size() == 1) {
      if (const auto* key = (*params)[0].GetIfString())
        preferences_.Remove(*key);
    } else if (*method == "requestFileSystems") {
      CallFrontend(u"fileSystemsLoaded", base::ListValue().Append(base::ListValue()));
    } else if (*method == "loadNetworkResource") {
      response.Set("statusCode", 403);
    } else if (*method == "setIsDocked" && params && params->size() == 1) {
      if (const auto docked = (*params)[0].GetIfBool())
        SetDocked(*docked);
    } else if (*method == "setInspectedPageBounds" && params &&
               params->size() == 1) {
      if (const auto* bounds = (*params)[0].GetIfDict())
        SetPageBounds(*bounds);
    } else if (*method == "closeWindow") {
      Close();
      return;
    }
    if (const auto id = message.FindInt("id"); id && *id)
      CallFrontend(u"embedderMessageAck",
                   base::ListValue().Append(*id).Append(std::move(response)));
    if (*method == "setIsDocked")
      AdvanceDockProbe();
  }

  void DispatchProtocolMessage(content::DevToolsAgentHost*,
                               base::span<const uint8_t> message) override {
    const std::string_view payload = base::as_string_view(message);
    if (ready_ && CheckProbeReply(payload))
      return;
    constexpr size_t kChunkSize = IPC::mojom::kChannelMaximumMessageSize / 4;
    if (payload.size() < kChunkSize) {
      CallFrontend(u"dispatchMessage", base::ListValue().Append(payload));
      return;
    }
    for (size_t offset = 0; offset < payload.size(); offset += kChunkSize) {
      CallFrontend(u"dispatchMessageChunk",
                   base::ListValue().Append(payload.substr(offset, kChunkSize))
                       .Append(base::NumberToString(offset ? 0 : payload.size())));
    }
  }

  void SendProbeCommand(int id, const char* method) {
    const auto command = base::WriteJson(
        base::DictValue().Set("id", id).Set("method", method));
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptMethod(
        u"InspectorFrontendHost", u"sendMessageToBackend",
        base::ListValue().Append(*command), {});
  }

  bool CheckProbeReply(std::string_view payload) {
    const auto reply = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    if (!reply)
      return false;
    const auto id = reply->FindInt("id");
    if (id == kTargetProbeId) {
      const auto* target_id =
          reply->FindStringByDottedPath("result.targetInfo.targetId");
      probe_target_ready_ = target_id && *target_id == agent_->GetId();
      LOG_IF(ERROR, !probe_target_ready_) << "DevTools probe target mismatch";
    } else if (id == kIsolationProbeId) {
      const auto* error = reply->FindStringByDottedPath("error.message");
      probe_isolation_ready_ = error && *error == "Not allowed";
      LOG_IF(ERROR, !probe_isolation_ready_) << "DevTools probe could enumerate targets";
    } else if (dock_probe_ == DockProbe::kInspect && id &&
               id == probe_inspect_id_) {
      if (reply->FindDict("result")) {
        LOG(INFO) << "DevTools probe verified renderer inspection mode";
        dock_probe_ = DockProbe::kPick;
        ClickProbeHeading();
      } else {
        LOG(ERROR) << "DevTools probe could not enable renderer inspection";
      }
      return false;
    } else {
      return false;
    }
    AdvanceDockProbe();
    return true;
  }

  void AdvanceDockProbe() {
    if (!ready_ || !probe_target_ready_ || !probe_isolation_ready_)
      return;
    if (dock_probe_ == DockProbe::kInitial && docked_ && probe_has_bounds_) {
      LOG(INFO) << "DevTools probe requested undocking";
      dock_probe_ = DockProbe::kUndock;
      ChangeFrontendDockSide(u"undocked");
    } else if (dock_probe_ == DockProbe::kUndock && !docked_ &&
               widget_->IsVisible() &&
               undocked_view_->web_contents() == contents_.get()) {
      LOG(INFO) << "DevTools probe verified the undocked frontend";
      dock_probe_ = DockProbe::kRedock;
      ChangeFrontendDockSide(u"bottom");
    } else if (dock_probe_ == DockProbe::kRedock && docked_ &&
               probe_has_bounds_ && loaded_) {
      LOG(INFO) << "DevTools probe verified redocking";
      dock_probe_ = DockProbe::kConsole;
      Show(Action::kConsole);
      contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
          u"import('devtools://devtools/bundled/ui/legacy/legacy.js')"
          u".then(({InspectorView, TabbedPane}) => {"
          u"const tabs = InspectorView.InspectorView.instance().tabbedPane;"
          u"const check = () => {"
          u"if (tabs.selectedTabId !== 'console') return;"
          u"tabs.removeEventListener(TabbedPane.Events.TabSelected, check);"
          u"DevToolsAPI.sendMessageToEmbedder('zitterConsoleSelected', [], null);"
          u"};"
          u"tabs.addEventListener(TabbedPane.Events.TabSelected, check);"
          u"check();"
          u"})",
          {});
    }
  }

  void ClickProbeHeading() {
    auto* view = target_ ? target_->GetRenderWidgetHostView() : nullptr;
    if (!view)
      return;
    auto* widget = view->GetRenderWidgetHost();
    // The workspace fixture starts with a marginless heading at the top left.
    constexpr gfx::Point point(16, 16);
    const auto origin = target_->GetContainerBounds().origin();
    blink::WebMouseEvent mouse(blink::WebInputEvent::Type::kMouseMove, 0,
                               ui::EventTimeForNow());
    mouse.SetPositionInWidget(point.x(), point.y());
    mouse.SetPositionInScreen(origin.x() + point.x(), origin.y() + point.y());
    widget->ForwardMouseEvent(mouse);
    mouse.SetType(blink::WebInputEvent::Type::kMouseDown);
    mouse.button = blink::WebMouseEvent::Button::kLeft;
    mouse.click_count = 1;
    mouse.SetModifiers(blink::WebInputEvent::kLeftButtonDown);
    widget->ForwardMouseEvent(mouse);
    mouse.SetType(blink::WebInputEvent::Type::kMouseUp);
    mouse.SetModifiers(0);
    widget->ForwardMouseEvent(mouse);
  }

  void ChangeFrontendDockSide(std::u16string_view side) {
    contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
        u"import('devtools://devtools/bundled/ui/legacy/legacy.js')"
        u".then(({DockController}) => "
        u"DockController.DockController.instance().setDockSide('" +
            std::u16string(side) + u"'))",
        {});
  }

  void CallFrontend(const std::u16string& method, base::ListValue args) {
    if (!web_contents() || web_contents()->GetLastCommittedURL() != GURL(kFrontend))
      return;
    auto* frame = web_contents()->GetPrimaryMainFrame();
    frame->AllowInjectingJavaScript();
    frame->ExecuteJavaScriptMethod(u"DevToolsAPI", method, std::move(args), {});
  }

  static inline constexpr int kFrontendKey = 0;
  ProfileContext context_;
  base::WeakPtr<content::WebContents> target_;
  Dock dock_;
  std::unique_ptr<content::WebContents> contents_;
  bool docked_ = true;
  bool loaded_ = false;
  bool closed_ = false;
  Action pending_action_ = Action::kShow;
  base::OnceClosure ready_;
  bool probe_target_ready_ = false;
  bool probe_isolation_ready_ = false;
  bool probe_has_bounds_ = false;
  std::optional<int> probe_inspect_id_;
  DockProbe dock_probe_ = DockProbe::kInitial;
  base::DictValue preferences_;
  std::unique_ptr<content::DevToolsFrontendHost> frontend_;
  scoped_refptr<content::DevToolsAgentHost> agent_;
  views::WidgetDelegate delegate_;
  raw_ptr<views::WebView> undocked_view_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
};

WorkspaceDevTools::WorkspaceDevTools(const base::FilePath& path,
                                     content::WebContents& target,
                                     Dock dock,
                                     base::OnceClosure ready)
    : window_(std::make_unique<Window>(path, target, std::move(dock),
                                      std::move(ready))) {}

WorkspaceDevTools::~WorkspaceDevTools() = default;

bool WorkspaceDevTools::IsOpenFor(content::WebContents* target) const {
  return window_->IsOpenFor(target);
}

void WorkspaceDevTools::Show(Action action) {
  window_->Show(action);
}

// static
void WorkspaceDevTools::AddNavigationThrottle(
    content::NavigationThrottleRegistry& registry) {
  if (Window::OwnsFrontend(*registry.GetNavigationHandle().GetWebContents())) {
    registry.AddThrottle(std::make_unique<FrontendNavigation>(registry));
  }
}

}  // namespace zitter::content_embedder
