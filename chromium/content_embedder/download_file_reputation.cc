// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_file_reputation.h"

#include <set>
#include <string>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "crypto/hash.h"
#include "download_file_actions.h"

namespace zitter::content_embedder {
namespace {

constexpr size_t kMaxCatalogBytes = 64 * 1024;

DownloadFileVerdict FindDigest(const base::FilePath& catalog,
                               std::string_view hash) {
  std::string contents;
  if (VerifyDownloadFilePath(catalog).empty() ||
      !base::ReadFileToStringWithMaxSize(catalog, &contents, kMaxCatalogBytes)) {
    return DownloadFileVerdict::kUnavailable;
  }

  const std::string expected = base::ToLowerASCII(base::HexEncode(hash));
  std::set<std::string_view> entries;
  auto verdict = DownloadFileVerdict::kUnavailable;
  for (std::string_view line : base::SplitStringPiece(
           contents, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (line.empty())
      continue;
    const auto separator = line.find(' ');
    if (separator == std::string_view::npos)
      return DownloadFileVerdict::kUnavailable;
    const auto decision = line.substr(0, separator);
    const auto digest = line.substr(separator + 1);
    if ((decision != "allow" && decision != "block") ||
        digest.size() != 2 * crypto::hash::kSha256Size ||
        digest.find_first_not_of("0123456789abcdef") != std::string_view::npos ||
        !entries.insert(digest).second) {
      return DownloadFileVerdict::kUnavailable;
    }
    if (digest == expected) {
      verdict = decision == "allow" ? DownloadFileVerdict::kAllowed
                                    : DownloadFileVerdict::kThreat;
    }
  }
  return verdict;
}

}  // namespace

DownloadFileVerdict CheckDownloadFileReputation(
    const base::FilePath& catalog,
    const base::FilePath& staged_file,
    int64_t size,
    std::string_view hash) {
  if (hash.size() != crypto::hash::kSha256Size || size <= 0)
    return DownloadFileVerdict::kUnavailable;

  const auto verdict = FindDigest(catalog, hash);
  if (verdict != DownloadFileVerdict::kAllowed)
    return verdict;
  if (VerifyDownloadFilePath(staged_file).empty())
    return DownloadFileVerdict::kUnavailable;

  constexpr uint32_t flags = base::File::FLAG_OPEN | base::File::FLAG_READ |
#if BUILDFLAG(IS_WIN)
                             base::File::FLAG_WIN_EXCLUSIVE_WRITE |
                             base::File::FLAG_WIN_SHARE_DELETE;
#else
                             base::File::FLAG_NO_FOLLOW;
#endif
  base::File file(staged_file, flags);
  return file.IsValid() && MatchesDownloadedFile(file, size, hash)
             ? DownloadFileVerdict::kAllowed
             : DownloadFileVerdict::kUnavailable;
}

}  // namespace zitter::content_embedder
