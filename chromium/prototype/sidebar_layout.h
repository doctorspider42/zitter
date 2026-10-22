// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#include <memory>
#include "ui/gfx/geometry/size.h"
namespace views { class LayoutManager; }
namespace zitter::prototype {
// Rust owns list geometry in the Metal backend. The native adapter retains
// View identity, measurement, accessibility and input delivery during migration.
std::unique_ptr<views::LayoutManager> CreateSidebarColumnLayout(int spacing = 0);
std::unique_ptr<views::LayoutManager> CreateSidebarSplitLayout();
gfx::Size SidebarSplitPreferredSize();
std::unique_ptr<views::LayoutManager> CreateSidebarPinnedRowLayout();
int SidebarFolderIndent(int depth);
std::unique_ptr<views::LayoutManager> CreateSidebarFooterLayout();
std::unique_ptr<views::LayoutManager> CreatePaletteInputLayout();
std::unique_ptr<views::LayoutManager> CreateSidebarDotsLayout(int spacing = 0);
}
#endif
