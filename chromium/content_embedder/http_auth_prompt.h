// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_HTTP_AUTH_PROMPT_H_
#define ZITTER_CONTENT_HTTP_AUTH_PROMPT_H_

#include <memory>
#include "content/public/browser/login_delegate.h"

namespace content { class WebContents; }
namespace views { class Widget; }
namespace zitter::content_embedder {
std::unique_ptr<content::LoginDelegate> CreateHttpAuthPrompt(
    const net::AuthChallengeInfo& challenge, content::WebContents* contents,
    bool do_not_prompt, content::LoginDelegate::LoginAuthRequiredCallback callback);
// Tests exercise the real fields and dialog acceptance, never a network bypass.
views::Widget* HttpAuthWidgetForTesting(content::WebContents* contents);
bool AnswerHttpAuthForTesting(content::WebContents* contents, bool accept);
}  // namespace zitter::content_embedder
#endif
