// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_
#define ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_

#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/mojom/credentialmanagement/credential_manager.mojom-forward.h"

namespace gfx {
class Rect;
}
namespace views {
class Widget;
}
namespace content {
class RenderFrameHost;
class WebContents;
}

namespace zitter::content_embedder {

class ProfileContext;

void AttachPagePasswordManager(content::WebContents& contents,
                               ProfileContext& profile);
void ConfigurePagePasswordPrompt(content::WebContents& contents,
                                 std::u16string profile_label,
                                 base::RepeatingCallback<gfx::Rect()> anchor);
// Shows a short notice (e.g. a toast) when a site signs the user in
// automatically through the Credential Management API.
void ConfigurePageSignInNotice(content::WebContents& contents,
                               base::RepeatingCallback<void(std::u16string)> notice);
// navigator.credentials for password credentials: get(), store() and
// preventSilentAccess() on the page's saved passwords.
void BindPageCredentialManager(
    content::RenderFrameHost* frame,
    mojo::PendingReceiver<blink::mojom::CredentialManager> receiver);
views::Widget* GetPagePasswordPromptForTesting(content::WebContents& contents);
bool PagePasswordPromptMetalContainsForTesting(content::WebContents& contents,
                                               std::string_view text);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PAGE_PASSWORD_MANAGER_H_
