// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PAGE_TOOLS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PAGE_TOOLS_H_

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point_f.h"

namespace content {
class RenderWidgetHost;
class WebContents;
}

namespace zitter::prototype {

void RunAssistantPageOperation(content::WebContents* page, base::DictValue args,
                               base::OnceCallback<void(base::Value)> done);
void CaptureAssistantPage(base::WeakPtr<content::WebContents> page,
                          base::OnceCallback<void(base::Value)> done);
gfx::PointF AssistantWidgetPoint(content::WebContents* page, double x, double y);
void SendAssistantMouse(content::RenderWidgetHost* host,
                        blink::WebInputEvent::Type type, gfx::PointF point,
                        blink::WebPointerProperties::Button button,
                        int modifiers, int clicks);
void ClickAssistantPageAt(
    content::WebContents* page, content::RenderWidgetHost* host, double x, double y,
    blink::WebPointerProperties::Button button = blink::WebPointerProperties::Button::kLeft,
    int clicks = 1);
bool ParseAssistantKey(const std::string& name, ui::KeyboardCode& code,
                       int& flags, char16_t& character);
void TypeAssistantCharacter(content::RenderWidgetHost* host, char16_t character,
                            int flags = 0, bool page_only = false);
void PressAssistantKey(content::RenderWidgetHost* host, ui::KeyboardCode code,
                       int flags, char16_t character, bool page_only = false);
void ReadAssistantPage(content::WebContents* page,
                       base::OnceCallback<void(base::Value)> done);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PAGE_TOOLS_H_
