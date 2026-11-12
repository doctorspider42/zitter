// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_
#define ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_

#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"

namespace gfx {
class Rect;
}
namespace views {
class Widget;
}
namespace content {
class WebContents;
}

namespace zitter::content_embedder {

class ProfileContext;

void AttachPagePasswordManager(content::WebContents& contents,
                               ProfileContext& profile);
void ConfigurePagePasswordPrompt(content::WebContents& contents,
                                 std::u16string profile_label,
                                 base::RepeatingCallback<gfx::Rect()> anchor);
views::Widget* GetPagePasswordPromptForTesting(content::WebContents& contents);
bool PagePasswordPromptMetalContainsForTesting(content::WebContents& contents,
                                               std::string_view text);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_
