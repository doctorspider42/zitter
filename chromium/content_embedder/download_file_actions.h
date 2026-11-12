// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_DOWNLOAD_FILE_ACTIONS_H_
#define ZITTER_CONTENT_DOWNLOAD_FILE_ACTIONS_H_

#include <cstdint>
#include <string_view>

#include "base/files/file_path.h"

namespace base {
class File;
}

namespace zitter::content_embedder {

enum class DownloadFileAction { kOpen, kReveal };

// Runs on a blocking worker. Returns an empty path for an invalid target.
base::FilePath VerifyDownloadFilePath(const base::FilePath& path);

// Checks a completed download before dispatching an Open request.
base::FilePath VerifyDownloadedFileContents(const base::FilePath& path,
                                             int64_t size,
                                             std::string_view hash);

// Checks the recorded prefix without admitting trailing bytes into its digest.
bool MatchesDownloadedPrefix(base::File& file, int64_t size, std::string_view hash);

// Checks a single-link staging file against its persisted checkpoint.
bool VerifyDownloadCheckpoint(const base::FilePath& path,
                              int64_t size,
                              std::string_view hash);

// Checks a file handle against the recorded completed download.
bool MatchesDownloadedFile(base::File& file, int64_t size, std::string_view hash);

// Runs on a blocking worker after the user confirms deletion.
bool DeleteVerifiedDownloadFile(const base::FilePath& path,
                                int64_t size,
                                std::string_view hash);

// Called on the browser UI thread after validation.
void RunDownloadFileAction(DownloadFileAction action, const base::FilePath& path);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DOWNLOAD_FILE_ACTIONS_H_
