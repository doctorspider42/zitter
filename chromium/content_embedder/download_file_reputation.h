// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_DOWNLOAD_FILE_REPUTATION_H_
#define ZITTER_CONTENT_DOWNLOAD_FILE_REPUTATION_H_

#include <cstdint>
#include <string_view>

#include "base/files/file_path.h"

namespace zitter::content_embedder {

enum class DownloadFileVerdict { kAllowed, kThreat, kUnavailable };

// Blocking lookup in a trusted local catalog for the Lab's disposable probe.
// An allow entry is usable only if the staged file still matches Chromium's
// final digest and length.
DownloadFileVerdict CheckDownloadFileReputation(
    const base::FilePath& catalog,
    const base::FilePath& staged_file,
    int64_t size,
    std::string_view hash);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DOWNLOAD_FILE_REPUTATION_H_
