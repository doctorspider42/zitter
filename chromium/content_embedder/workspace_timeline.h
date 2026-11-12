// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_TIMELINE_H_
#define ZITTER_CONTENT_WORKSPACE_TIMELINE_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/values.h"
#include "chromium/prototype/timeline_store.h"

namespace zitter::content_embedder {

inline constexpr size_t kMaxTimelineBytes = 20 * 1024 * 1024;
inline constexpr int kMaxTimelineRetentionDays = 90;

base::FilePath WorkspaceTimelinePath(const base::FilePath& session_path);

// Returns an empty timeline for a missing file and nullptr for invalid data.
std::unique_ptr<prototype::TimelineStore> ReadWorkspaceTimeline(
    const base::FilePath& path, const base::DictValue& session);
bool WriteWorkspaceTimeline(const base::FilePath& path,
                            rust::Vec<sidebar::TimelineFrame> frames,
                            rust::Vec<sidebar::TimelineMark> marks,
                            const base::DictValue& epochs);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_TIMELINE_H_
