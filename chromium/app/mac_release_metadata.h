// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_APP_MAC_RELEASE_METADATA_H_
#define ZITTER_CHROMIUM_APP_MAC_RELEASE_METADATA_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/version.h"
#include "crypto/hash.h"
#include "url/gurl.h"

namespace zitter {

enum class MacReleaseBackend { kChrome, kContent };

struct MacInstaller {
  std::string version;
  GURL url;
  int64_t size;
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  base::FilePath path;
};

struct MacReleaseMetadata {
  bool has_stable_release = false;
  base::Version latest;
  GURL release_url;
  std::optional<MacInstaller> installer;
};

MacReleaseMetadata SelectMacRelease(std::string_view body,
                                   const base::Version& installed,
                                   MacReleaseBackend backend,
                                   std::string_view architecture,
                                   const base::FilePath& cache);

bool IsAllowedMacInstallerRedirect(const GURL& url);

bool VerifyMacInstaller(const base::FilePath& path, int64_t size,
                        const std::array<uint8_t, crypto::hash::kSha256Size>& digest);

// Consumes the temporary download and publishes a verified cache entry.
bool CacheMacInstaller(const base::FilePath& download,
                       const MacInstaller& installer);

}  // namespace zitter
#endif
