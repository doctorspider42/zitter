// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_FAVICON_REFRESH_H_
#define ZITTER_CONTENT_WORKSPACE_FAVICON_REFRESH_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

namespace content { class WebContents; }
namespace gfx { class Image; }
namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}

namespace zitter::content_embedder {

class WorkspaceFaviconRefresh {
 public:
  using Publish = base::RepeatingCallback<void(const GURL& page,
                                               const GURL& icon,
                                               const gfx::Image& image)>;
  WorkspaceFaviconRefresh(content::WebContents& source,
                          std::map<GURL, GURL> pages, Publish publish,
                          base::OnceClosure done = {});
  ~WorkspaceFaviconRefresh();

  void Start();

 private:
  void FetchNextPage();
  void OnMarkup(std::optional<std::string> markup);
  void FetchNextIcon();
  void OnIconBytes(GURL icon, std::optional<std::string> bytes);
  void DecodeIcon(GURL icon, const GURL& data);

  base::WeakPtr<content::WebContents> source_;
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  std::map<GURL, GURL> pages_;
  Publish publish_;
  base::OnceClosure done_;
  GURL page_;
  GURL known_icon_;
  std::vector<GURL> candidates_;
  size_t candidate_ = 0;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::WeakPtrFactory<WorkspaceFaviconRefresh> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_FAVICON_REFRESH_H_
