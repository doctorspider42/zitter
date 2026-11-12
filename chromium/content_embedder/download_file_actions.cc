// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_file_actions.h"

#include <algorithm>
#include <array>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "crypto/hash.h"

namespace zitter::content_embedder {

bool MatchesDownloadedPrefix(base::File& file,
                             int64_t size,
                             std::string_view hash) {
  if (size < 0 || file.GetLength() < size)
    return false;

  crypto::hash::Hasher hasher(crypto::hash::kSha256);
  std::array<uint8_t, 4096> buffer;
  for (int64_t offset = 0; offset < size;) {
    const size_t count = std::min<int64_t>(size - offset, buffer.size());
    const auto chunk = base::span(buffer).first(count);
    if (file.Read(offset, chunk) != count)
      return false;
    hasher.Update(chunk);
    offset += count;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return base::span(digest) == base::as_byte_span(hash);
}

bool MatchesDownloadedFile(base::File& file,
                           int64_t size,
                           std::string_view hash) {
  return file.GetLength() == size && MatchesDownloadedPrefix(file, size, hash) &&
         file.GetLength() == size;
}

}  // namespace zitter::content_embedder
