// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_NIGHT_SHIFT_SITE_H_
#define ZITTER_CONTENT_NIGHT_SHIFT_SITE_H_

#include <string>

#include "base/functional/callback.h"

class GURL;

namespace content {
class RenderFrameHost;
}

namespace zitter::content_embedder {

enum class NightShiftSiteMode { kAuto, kDark, kForce, kOff };

std::string NightShiftSiteKey(const GURL& url);
bool CanSetNightShiftSiteMode(content::RenderFrameHost& document);
void SetNightShiftSiteMode(content::RenderFrameHost& document,
                          NightShiftSiteMode mode,
                          base::OnceClosure changed);

}  // namespace zitter::content_embedder

#endif
