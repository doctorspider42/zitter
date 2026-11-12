// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "night_shift_site.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "extension_registry_probe.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/common/extension.h"
#include "url/gurl.h"

namespace zitter::content_embedder {

std::string NightShiftSiteKey(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_host())
    return {};
  std::string host(url.host());
  if (host.ends_with('.'))
    host.pop_back();
  if (url.has_port())
    host += ":" + std::string(url.port());
  return "site:" + host;
}

namespace {

const char* ModeValue(NightShiftSiteMode mode) {
  switch (mode) {
    case NightShiftSiteMode::kAuto:
      return "auto";
    case NightShiftSiteMode::kDark:
      return "dark";
    case NightShiftSiteMode::kForce:
      return "on";
    case NightShiftSiteMode::kOff:
      return "off";
  }
}

}  // namespace

bool CanSetNightShiftSiteMode(content::RenderFrameHost& document) {
  return document.IsActive() && document.IsInPrimaryMainFrame() &&
         !NightShiftSiteKey(document.GetLastCommittedURL()).empty() &&
         GetBundledNightShiftExtension(*document.GetBrowserContext());
}

void SetNightShiftSiteMode(content::RenderFrameHost& document,
                          NightShiftSiteMode mode, base::OnceClosure changed) {
  if (!CanSetNightShiftSiteMode(document))
    return;
  auto& context = *document.GetBrowserContext();
  auto extension = base::WrapRefCounted(GetBundledNightShiftExtension(context));
  auto* storage = extensions::StorageFrontend::Get(&context);
  if (!storage)
    return;
  const auto key = NightShiftSiteKey(document.GetLastCommittedURL());
  storage->GetValues(
      extension, extensions::StorageAreaNamespace::kLocal,
      std::vector<std::string>{key},
      base::BindOnce(
          [](content::WeakDocumentPtr document, std::string key,
             NightShiftSiteMode mode, base::OnceClosure changed,
             extensions::StorageFrontend::GetResult result) {
            auto* frame = document.AsRenderFrameHostIfValid();
            if (!frame || !CanSetNightShiftSiteMode(*frame) || !result.data)
              return;
            auto& context = *frame->GetBrowserContext();
            auto extension =
                base::WrapRefCounted(GetBundledNightShiftExtension(context));
            auto* storage = extensions::StorageFrontend::Get(&context);
            if (!storage)
              return;

            base::DictValue record;
            if (const auto* saved = result.data->FindDict(key))
              record = saved->Clone();
            record.Set("mode", ModeValue(mode));
            record.Set("force", mode == NightShiftSiteMode::kForce);
            storage->Set(
                std::move(extension), extensions::StorageAreaNamespace::kLocal,
                base::DictValue().Set(key, std::move(record)),
                base::BindOnce([](base::OnceClosure changed,
                                  extensions::StorageFrontend::ResultStatus status) {
                  if (!status.success)
                    LOG(ERROR) << "Could not save Night Shift site mode";
                  else if (changed)
                    std::move(changed).Run();
                }, std::move(changed)));
          },
          document.GetWeakDocumentPtr(), key, mode, std::move(changed)));
}

}  // namespace zitter::content_embedder
