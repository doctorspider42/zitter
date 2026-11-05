// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef CHROME_RENDERER_ZITTER_PAGE_SCRIPTS_H_
#define CHROME_RENDERER_ZITTER_PAGE_SCRIPTS_H_

namespace content { class RenderFrame; }

namespace zitter {
// Ad blocking scriptlets in the page's own world (docs/ad-blocking.md): runs
// each script that RendererPreferences::zitter_page_scripts names for this
// frame's host, at document start, before any of the page's scripts.
void RunPageScripts(content::RenderFrame* render_frame);
}  // namespace zitter

#endif  // CHROME_RENDERER_ZITTER_PAGE_SCRIPTS_H_
