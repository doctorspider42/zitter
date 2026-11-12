// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/adblock_network.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/data_url.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zitter::prototype {
namespace {

// adblock-rust's request types, from what the page asked the resource to be.
std::string RequestType(const network::ResourceRequest& request) {
  using D = network::mojom::RequestDestination;
  switch (request.destination) {
    case D::kScript: case D::kWorker: case D::kSharedWorker: case D::kServiceWorker:
    case D::kAudioWorklet: case D::kPaintWorklet: return "script";
    case D::kImage: return "image";
    case D::kStyle: return "stylesheet";
    case D::kFrame: case D::kIframe: case D::kFencedframe: return "sub_frame";
    case D::kFont: return "font";
    case D::kAudio: case D::kVideo: case D::kTrack: return "media";
    case D::kObject: case D::kEmbed: return "object";
    case D::kEmpty: return request.keepalive ? "ping" : "xmlhttprequest";
    default: return "other";
  }
}
// The loader of a request Zitter answered itself: nothing left to do.
class ServedLoader final : public network::mojom::URLLoader {
 public:
  void FollowRedirect(network::HttpRequestHeadersUpdateParams, const std::optional<GURL>&) override {}
  void SetPriority(net::RequestPriority, int32_t) override {}
};
// `$redirect=`: a 200 response with the body of a resource Zitter ships
// (adblock_resources.js), so bait requests look like they loaded.
bool Serve(const std::string& data_url,
           const network::ResourceRequest& request,
           mojo::PendingReceiver<network::mojom::URLLoader>& loader,
           mojo::PendingRemote<network::mojom::URLLoaderClient>& client) {
  const GURL url(data_url);
  std::string mime, charset, body;
  if (!url.SchemeIs("data") ||
      !net::DataURL::Parse(url, &mime, &charset, &body)) {
    return false;
  }
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (mojo::CreateDataPipe(std::max<size_t>(body.size(), 1), producer,
                           consumer) != MOJO_RESULT_OK) {
    return false;
  }
  if (!body.empty() &&
      producer->WriteAllData(base::as_byte_span(body)) != MOJO_RESULT_OK) {
    return false;
  }
  producer.reset();
  auto head = network::mojom::URLResponseHead::New();
  head->mime_type = mime;
  head->charset = charset;
  head->content_length = static_cast<int64_t>(body.size());
  head->headers = net::HttpResponseHeaders::TryToCreate("HTTP/1.1 200 OK\r\n\r\n");
  head->headers->SetHeader("Content-Type", mime);
  head->headers->SetHeader("Access-Control-Allow-Origin", "*");
  const bool same_origin = request.request_initiator &&
                           request.request_initiator->IsSameOriginWith(request.url);
  if (same_origin) {
    head->response_type = network::mojom::FetchResponseType::kBasic;
  } else if (request.mode == network::mojom::RequestMode::kNoCors) {
    head->response_type = network::mojom::FetchResponseType::kOpaque;
  } else {
    head->response_type = network::mojom::FetchResponseType::kCors;
  }
  mojo::MakeSelfOwnedReceiver(std::make_unique<ServedLoader>(), std::move(loader));
  mojo::Remote<network::mojom::URLLoaderClient> remote(std::move(client));
  remote->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
  network::URLLoaderCompletionStatus status(net::OK);
  status.decoded_body_length = base::ByteSize(body.size());
  remote->OnComplete(status);
  return true;
}

class BlockingFactory final : public network::mojom::URLLoaderFactory {
 public:
  BlockingFactory(
      GURL page,
      AdblockRequestFilter filter,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target)
      : page_(std::move(page)),
        filter_(std::move(filter)),
        target_(std::move(target)) {
    receivers_.Add(this, std::move(receiver));
    receivers_.set_disconnect_handler(base::BindRepeating(
        &BlockingFactory::Disconnected, base::Unretained(this)));
    target_.set_disconnect_handler(
        base::BindOnce(&BlockingFactory::Destroy, base::Unretained(this)));
  }

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) override {
    std::string redirect;
    if (request.destination != network::mojom::RequestDestination::kDocument &&
        filter_.Run(request.url, page_, RequestType(request), &redirect)) {
      if (client &&
          (redirect.empty() || !Serve(redirect, request, loader, client))) {
        mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
            ->OnComplete(network::URLLoaderCompletionStatus(
                net::ERR_BLOCKED_BY_CLIENT));
      }
      return;
    }
    target_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                  request, std::move(client), traffic_annotation);
  }

  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) override {
    receivers_.Add(this, std::move(receiver));
  }

 private:
  void Disconnected() {
    if (receivers_.empty())
      Destroy();
  }

  void Destroy() { delete this; }

  const GURL page_;
  const AdblockRequestFilter filter_;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
};

}  // namespace

GURL AdblockTopLevelPage(
    content::RenderFrameHost* frame,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const net::IsolationInfo& isolation) {
  using Type = content::ContentBrowserClient::URLLoaderFactoryType;
  switch (type) {
    case Type::kNavigation:
      if (!frame || !frame->GetParentOrOuterDocument())
        return {};
      break;
    case Type::kDocumentSubResource:
    case Type::kWorkerMainResource:
    case Type::kWorkerSubResource:
    case Type::kServiceWorkerScript:
    case Type::kServiceWorkerSubResource:
      break;
    default:
      return {};
  }

  GURL page;
  if (isolation.top_frame_origin() && !isolation.top_frame_origin()->opaque()) {
    page = isolation.top_frame_origin()->GetURL();
  } else if (frame) {
    page = frame->GetOutermostMainFrame()->GetLastCommittedOrigin().GetURL();
  }
  return page.SchemeIsHTTPOrHTTPS() ? page : GURL();
}

void AppendAdblockFactory(network::URLLoaderFactoryBuilder& builder,
                          GURL page,
                          AdblockRequestFilter filter) {
  auto [receiver, target] = builder.Append();
  new BlockingFactory(std::move(page), std::move(filter), std::move(receiver),
                      std::move(target));
}

}  // namespace zitter::prototype
