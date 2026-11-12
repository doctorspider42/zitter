// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_favicon_refresh.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "chromium/prototype/workspace_favicon_links.h"
#include "components/favicon_base/select_favicon_frames.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image.h"

namespace zitter::content_embedder {

namespace {

std::unique_ptr<network::SimpleURLLoader> MakeIconLoader(const GURL& url) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES;
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
      "zitter_content_favicon_refresh", R"(
    semantics {
      sender: "Zitter Content Workspace icon refresh"
      description: "Fetches markup and icons for tabs and pinned pages."
      trigger: "The user selects Refresh all favicons in Command Center."
      data: "The page URL, without cookies or authentication."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: NO
      setting: "This request only occurs when the user runs the command."
      policy_exception_justification: "User initiated icon refresh."
    })");
  auto loader = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader->SetTimeoutDuration(base::Seconds(10));
  return loader;
}

}  // namespace

WorkspaceFaviconRefresh::WorkspaceFaviconRefresh(
    content::WebContents& source, std::map<GURL, GURL> pages, Publish publish)
    : source_(source.GetWeakPtr()),
      factory_(source.GetBrowserContext()->GetDefaultStoragePartition()
                   ->GetURLLoaderFactoryForBrowserProcess()),
      pages_(std::move(pages)),
      publish_(std::move(publish)) {}

WorkspaceFaviconRefresh::~WorkspaceFaviconRefresh() = default;

void WorkspaceFaviconRefresh::Start() {
  FetchNextPage();
}

void WorkspaceFaviconRefresh::FetchNextPage() {
  if (!source_ || !source_->GetPrimaryMainFrame()->IsRenderFrameLive() ||
      pages_.empty()) {
    return;
  }
  auto next = pages_.extract(pages_.begin());
  page_ = std::move(next.key());
  known_icon_ = std::move(next.mapped());
  loader_ = MakeIconLoader(page_);
  loader_->SetAllowPartialResults(true);
  loader_->DownloadToString(
      factory_.get(),
      base::BindOnce(&WorkspaceFaviconRefresh::OnMarkup,
                     weak_factory_.GetWeakPtr()),
      512 * 1024);
}

void WorkspaceFaviconRefresh::OnMarkup(std::optional<std::string> markup) {
  const GURL final_url = loader_->GetFinalURL();
  loader_.reset();
  const GURL& base_url = final_url.SchemeIsHTTPOrHTTPS() ? final_url : page_;
  candidates_ = markup ? prototype::WorkspaceIconCandidates(*markup, base_url)
                       : std::vector<GURL>();
  for (const auto& icon : {known_icon_, base_url.Resolve("/favicon.ico")}) {
    if (prototype::IsWorkspaceFaviconUrl(icon) &&
        !std::ranges::contains(candidates_, icon)) {
      candidates_.push_back(icon);
    }
  }
  std::erase_if(candidates_, [](const GURL& icon) {
    return icon.has_username() || icon.has_password();
  });
  candidate_ = 0;
  FetchNextIcon();
}

void WorkspaceFaviconRefresh::FetchNextIcon() {
  if (!source_ || !source_->GetPrimaryMainFrame()->IsRenderFrameLive())
    return;
  if (candidate_ == candidates_.size()) {
    FetchNextPage();
    return;
  }
  const GURL icon = candidates_[candidate_++];
  if (icon.SchemeIs(url::kDataScheme)) {
    DecodeIcon(icon, icon);
    return;
  }
  loader_ = MakeIconLoader(icon);
  loader_->DownloadToString(
      factory_.get(),
      base::BindOnce(&WorkspaceFaviconRefresh::OnIconBytes,
                     weak_factory_.GetWeakPtr(), icon),
      1024 * 1024);
}

void WorkspaceFaviconRefresh::OnIconBytes(GURL icon,
                                          std::optional<std::string> bytes) {
  std::string mime_type;
  const auto* response = loader_->ResponseInfo();
  if (response && response->headers)
    response->headers->GetMimeType(&mime_type);
  loader_.reset();
  if (!bytes || bytes->empty()) {
    FetchNextIcon();
    return;
  }
  // The renderer decodes bytes from a data URL, so it cannot make a second
  // request with the document's credentials. Raster decoding sniffs the bytes.
  const GURL data(base::StrCat({
      mime_type == "image/svg+xml" ? "data:image/svg+xml;base64,"
                                  : "data:image/png;base64,",
      base::Base64Encode(*bytes)}));
  DecodeIcon(std::move(icon), data);
}

void WorkspaceFaviconRefresh::DecodeIcon(GURL icon, const GURL& data) {
  if (!source_ || !source_->GetPrimaryMainFrame()->IsRenderFrameLive())
    return;
  source_->DownloadImage(
      data, /*is_favicon=*/true, gfx::Size(32, 32), 64, /*bypass_cache=*/true,
      base::BindOnce(
          [](base::WeakPtr<WorkspaceFaviconRefresh> job, const GURL& icon, int,
             int status, const GURL&, const std::vector<SkBitmap>& bitmaps,
             const std::vector<gfx::Size>& sizes) {
            if (!job)
              return;
            if (status >= 400 || bitmaps.empty() || bitmaps.front().drawsNothing()) {
              job->FetchNextIcon();
              return;
            }
            float score = 0;
            auto image = CreateFaviconImageSkia(bitmaps, sizes, gfx::kFaviconSize, &score);
            if (image.isNull()) {
              job->FetchNextIcon();
              return;
            }
            job->publish_.Run(job->page_, icon, gfx::Image(image));
            if (job)
              job->FetchNextPage();
          },
          weak_factory_.GetWeakPtr(), std::move(icon)));
}

}  // namespace zitter::content_embedder
