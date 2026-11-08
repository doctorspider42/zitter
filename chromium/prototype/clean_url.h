// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_CLEAN_URL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_CLEAN_URL_H_

#include <optional>
#include <string>

class GURL;

namespace zitter::prototype {

// Returns a clipboard address only if tracking parameters can be removed.
std::optional<std::string> CleanUrlForCopy(const GURL& url);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_CLEAN_URL_H_
