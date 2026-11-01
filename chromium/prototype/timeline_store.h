// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_TIMELINE_STORE_H_
#define ZITTER_CHROMIUM_PROTOTYPE_TIMELINE_STORE_H_
#include <optional>
#include "chromium/prototype/sidebar_scene.rs.h"
namespace zitter::prototype {
// WorkspaceWindow's Time Machine timeline; `cleared` holds the previous one while
// the "cleared" toast can still undo.
struct TimelineStore {
  rust::Box<sidebar::Timeline> current = sidebar::new_timeline();
  std::optional<rust::Box<sidebar::Timeline>> cleared;
};
}  // namespace zitter::prototype
#endif  // ZITTER_CHROMIUM_PROTOTYPE_TIMELINE_STORE_H_
