// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_EMBEDDER_EXTENSIONS_MANAGER_H_
#define ZITTER_CONTENT_EMBEDDER_EXTENSIONS_MANAGER_H_

#include "base/functional/callback.h"
#include "base/values.h"

namespace content {
class WebContents;
}

namespace zitter::content_embedder {

inline constexpr char kManagerShortcutPref[] = "zitter_manager_shortcuts";
inline constexpr char kManagerPinPref[] = "zitter_manager_pinned";
inline constexpr char kManagerAutoPinPref[] = "zitter.extensions.auto_pin";

void RegisterExtensionsManagerWebUI();
void AttachExtensionsManager(
    content::WebContents& page,
    base::RepeatingCallback<base::DictValue(content::WebContents&,
                                            const base::DictValue&)> dispatch);

}  // namespace zitter::content_embedder
#endif
