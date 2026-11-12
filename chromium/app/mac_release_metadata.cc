// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/app/mac_release_metadata.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <utility>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "url/url_constants.h"

namespace zitter {
namespace {
constexpr int64_t kMaxMacInstaller = 600 * 1024 * 1024;
}  // namespace

bool IsAllowedMacInstallerRedirect(const GURL& url) {
  return url.is_valid() && url.SchemeIs(url::kHttpsScheme) &&
         !url.has_username() && !url.has_password() && !url.has_port() &&
         (url.host() == "github.com" ||
          url.host() == "release-assets.githubusercontent.com" ||
          url.host() == "objects.githubusercontent.com");
}

bool VerifyMacInstaller(const base::FilePath& path, int64_t size,
                        const std::array<uint8_t, crypto::hash::kSha256Size>& digest) {
  base::File file(HANDLE_EINTR(open(path.value().c_str(),
                                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)));
  base::stat_wrapper_t opened;
  if (!file.IsValid() || base::File::Fstat(file.GetPlatformFile(), &opened) != 0 ||
      !S_ISREG(opened.st_mode) || opened.st_size != size) {
    return false;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> actual;
  base::stat_wrapper_t current;
  return crypto::hash::HashFile(crypto::hash::kSha256, &file, actual) &&
         actual == digest && base::File::Lstat(path, &current) == 0 &&
         current.st_dev == opened.st_dev && current.st_ino == opened.st_ino &&
         current.st_size == opened.st_size &&
         current.st_ctimespec.tv_sec == opened.st_ctimespec.tv_sec &&
         current.st_ctimespec.tv_nsec == opened.st_ctimespec.tv_nsec;
}

bool CacheMacInstaller(const base::FilePath& download,
                       const MacInstaller& installer) {
  if (download.empty() || download == installer.path)
    return false;
  const auto directory = installer.path.DirName();
  bool saved = false;
  if (VerifyMacInstaller(download, installer.size, installer.digest) &&
      base::CreateDirectory(directory)) {
    if (base::ReplaceFile(download, installer.path, nullptr))
      return true;
    // The temporary download may live on a different volume. Verify a
    // private sibling of the cache entry before publishing it by rename.
    base::FilePath staged;
    if (base::CreateTemporaryFileInDir(directory, &staged)) {
      saved = base::CopyFile(download, staged) &&
              VerifyMacInstaller(staged, installer.size, installer.digest) &&
              base::ReplaceFile(staged, installer.path, nullptr);
      if (!saved)
        base::DeleteFile(staged);
    }
  }
  base::DeleteFile(download);
  return saved;
}

MacReleaseMetadata SelectMacRelease(std::string_view body,
                                   const base::Version& installed,
                                   MacReleaseBackend backend,
                                   std::string_view architecture,
                                   const base::FilePath& cache) {
  MacReleaseMetadata selected;
  const auto releases = base::JSONReader::ReadList(body, base::JSON_PARSE_RFC);
  if (!releases || !installed.IsValid())
    return selected;
  const std::string native_suffix = std::string(architecture) + ".dmg";
  // GitHub's Latest flag may point to a separate release for Windows.
  for (const auto& entry : *releases) {
    const auto* release = entry.GetIfDict();
    const auto* tag = release ? release->FindString("tag_name") : nullptr;
    const auto* assets = release ? release->FindList("assets") : nullptr;
    if (!tag || !tag->starts_with('v') || !assets || release->FindBool("draft") != false ||
        release->FindBool("prerelease") != false) continue;
    const base::Version version(tag->substr(1));
    if (!version.IsValid() || version.components().size() != 3 || "v" + version.GetString() != *tag)
      continue;
    selected.has_stable_release = true;
    if (selected.latest.IsValid() && version.CompareTo(selected.latest) <= 0) continue;

    const std::string prefix = "Zitter-" + *tag +
        (backend == MacReleaseBackend::kContent ? "-macos-content-" : "-macos-");
    bool has_installer = false;
    std::optional<MacInstaller> verified_asset;
    for (const auto& asset : *assets) {
      const auto* file = asset.GetIfDict();
      const auto* name = file ? file->FindString("name") : nullptr;
      if (!name || (*name != prefix + native_suffix && *name != prefix + "universal.dmg")) continue;
      has_installer = true;
      if (version.CompareTo(installed) <= 0) break;
      const auto* url = file->FindString("browser_download_url");
      const auto* digest = file->FindString("digest");
      const auto size = file->FindInt("size");
      MacInstaller candidate{version.GetString(),
          GURL("https://github.com/doctorspider42/zitter/releases/download/" + *tag + "/" + *name),
          size.value_or(0), {}, cache.AppendASCII(*name)};
      if (url && *url == candidate.url.spec() && size && *size > 0 &&
          *size <= kMaxMacInstaller && digest && digest->starts_with("sha256:") &&
          base::HexStringToSpan(digest->substr(7), candidate.digest)) {
        verified_asset = std::move(candidate);
        if (*name == prefix + native_suffix) break;
      }
    }
    if (!has_installer) continue;
    selected.latest = version;
    selected.installer = std::move(verified_asset);
    selected.release_url = GURL("https://github.com/doctorspider42/zitter/releases/tag/" + *tag);
  }
  if (selected.latest.IsValid() && selected.latest.CompareTo(installed) <= 0)
    selected.release_url = GURL();
  return selected;
}

}  // namespace zitter
