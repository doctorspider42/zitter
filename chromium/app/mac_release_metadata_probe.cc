// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/app/mac_release_metadata.h"

#include <iostream>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace {

constexpr char kDigest[] =
    "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

base::DictValue Asset(const std::string& tag, const std::string& platform) {
  const std::string name = "Zitter-" + tag + "-" + platform;
  base::DictValue asset;
  asset.Set("name", name);
  asset.Set("browser_download_url",
            "https://github.com/doctorspider42/zitter/releases/download/" + tag + "/" + name);
  asset.Set("size", 3);
  asset.Set("digest", kDigest);
  return asset;
}

base::DictValue Release(const std::string& tag, base::ListValue assets,
                        bool draft = false, bool prerelease = false) {
  base::DictValue release;
  release.Set("tag_name", tag);
  release.Set("assets", std::move(assets));
  release.Set("draft", draft);
  release.Set("prerelease", prerelease);
  return release;
}

zitter::MacReleaseMetadata Select(const base::ListValue& releases,
                                  const base::FilePath& cache,
                                  std::string_view installed = "0.1.0",
                                  zitter::MacReleaseBackend backend =
                                      zitter::MacReleaseBackend::kChrome) {
  return zitter::SelectMacRelease(base::WriteJson(releases).value(),
                                 base::Version(installed), backend, "arm64", cache);
}

int VerifyReleaseAsset(const std::string& catalog, const std::string& installed,
                       const std::string& architecture, const std::string& file,
                       zitter::MacReleaseBackend backend) {
  const base::FilePath path(file);
  std::string body;
  if (!base::ReadFileToStringWithMaxSize(base::FilePath(catalog), &body,
                                        2 * 1024 * 1024)) {
    std::cerr << "Could not read the release catalog\n";
    return 1;
  }
  const auto release = zitter::SelectMacRelease(
      body, base::Version(installed), backend, architecture, path.DirName());
  if (!release.installer || release.installer->path.BaseName() != path.BaseName()) {
    std::cerr << "The catalog did not select the supplied installer\n";
    return 1;
  }
  const auto& installer = *release.installer;
  if (!zitter::VerifyMacInstaller(path, installer.size, installer.digest)) {
    std::cerr << "The supplied installer failed verification\n";
    return 1;
  }
  std::cout << "Verified macOS update " << installer.version << " from "
            << installer.url.spec() << '\n';
  return 0;
}

void VerifyInstallerCache(zitter::MacInstaller installer,
                          const base::FilePath& directory) {
  const auto download = directory.AppendASCII("download.tmp");
  CHECK(base::WriteFile(download, "abc"));
  CHECK(zitter::CacheMacInstaller(download, installer));
  CHECK(!base::PathExists(download));
  CHECK(zitter::VerifyMacInstaller(installer.path, installer.size, installer.digest));

  CHECK(base::WriteFile(download, "xyz"));
  CHECK(!zitter::CacheMacInstaller(download, installer));
  CHECK(!base::PathExists(download));
  CHECK(zitter::VerifyMacInstaller(installer.path, installer.size, installer.digest));

  const auto blocked = installer.path.DirName().AppendASCII("not-a-directory");
  CHECK(base::WriteFile(blocked, "file"));
  installer.path = blocked.AppendASCII("installer.dmg");
  CHECK(base::WriteFile(download, "abc"));
  CHECK(!zitter::CacheMacInstaller(download, installer));
  CHECK(!base::PathExists(download));
  CHECK(!base::PathExists(installer.path));
}

}  // namespace

int main(int argc, char* argv[]) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  const auto& command = *base::CommandLine::ForCurrentProcess();
  const auto args = command.GetArgs();
  constexpr auto kContent = zitter::MacReleaseBackend::kContent;
  constexpr auto kChrome = zitter::MacReleaseBackend::kChrome;
  if (args.size() == 4)
    return VerifyReleaseAsset(args[0], args[1], args[2], args[3],
                              command.HasSwitch("content") ? kContent : kChrome);
  if (!args.empty()) {
    std::cerr << "Usage: mac_release_metadata_probe [--content] [CATALOG INSTALLED ARCH INSTALLER]\n";
    return 2;
  }
  base::ScopedTempDir directory;
  CHECK(directory.CreateUniqueTempDir());
  const auto& cache = directory.GetPath();

  for (const char* accepted : {
           "https://github.com/doctorspider42/zitter/releases/download/v0.1.2/installer.dmg",
           "https://release-assets.githubusercontent.com/download?signature=fixture",
           "https://objects.githubusercontent.com/download?signature=fixture",
       }) {
    CHECK(zitter::IsAllowedMacInstallerRedirect(GURL(accepted)));
  }
  for (const char* rejected : {
           "http://github.com/download",
           "https://user:password@github.com/download",
           "https://github.com:8443/download",
           "https://github.com.example.test/download",
           "https://example.test/github.com/download",
           "https://release-assets.githubusercontent.com./download",
           "https://api.github.com/download",
           "file:///tmp/installer.dmg",
           "",
       }) {
    CHECK(!zitter::IsAllowedMacInstallerRedirect(GURL(rejected)));
  }

  base::ListValue releases;
  releases.Append(Release("v0.2.1", base::ListValue().Append(Asset("v0.2.1", "windows-x64.exe"))));
  auto windows_only = Select(releases, cache);
  CHECK(windows_only.has_stable_release && !windows_only.latest.IsValid());
  releases.Append(Release("v0.5.0", base::ListValue().Append(Asset("v0.5.0", "macos-arm64.dmg")),
                          false, true));
  releases.Append(Release("v0.6.0", base::ListValue().Append(Asset("v0.6.0", "macos-arm64.dmg")),
                          true));
  releases.Append(Release("v0.2.0", base::ListValue()
      .Append(Asset("v0.2.0", "macos-universal.dmg"))
      .Append(Asset("v0.2.0", "macos-arm64.dmg"))));
  releases.Append(Release("v0.1.5", base::ListValue().Append(Asset("v0.1.5", "macos-arm64.dmg"))));
  const auto selected = Select(releases, cache);
  CHECK(selected.latest == base::Version("0.2.0"));
  CHECK(selected.installer);
  CHECK(selected.installer->path.BaseName().value() == "Zitter-v0.2.0-macos-arm64.dmg");
  CHECK(selected.release_url.spec() == "https://github.com/doctorspider42/zitter/releases/tag/v0.2.0");
  CHECK(base::WriteFile(selected.installer->path, "abc"));
  CHECK(zitter::VerifyMacInstaller(selected.installer->path, 3, selected.installer->digest));
  const auto link = cache.AppendASCII("linked-installer.dmg");
  CHECK(base::CreateSymbolicLink(selected.installer->path, link));
  CHECK(!zitter::VerifyMacInstaller(link, 3, selected.installer->digest));
  CHECK(!zitter::VerifyMacInstaller(selected.installer->path, 2, selected.installer->digest));
  CHECK(base::WriteFile(selected.installer->path, "xyz"));
  CHECK(!zitter::VerifyMacInstaller(selected.installer->path, 3, selected.installer->digest));
  const auto download_root = command.GetSwitchValuePath("download-scratch");
  base::ScopedTempDir downloads;
  CHECK(downloads.CreateUniqueTempDirUnderPath(download_root.empty() ? cache : download_root));
  VerifyInstallerCache(*selected.installer, downloads.GetPath());

  auto wrong_url = Asset("v0.3.0", "macos-arm64.dmg");
  wrong_url.Set("browser_download_url", "https://example.test/other.dmg");
  auto missing_digest = Asset("v0.3.0", "macos-universal.dmg");
  missing_digest.Remove("digest");
  releases.Append(Release("v0.3.0", base::ListValue()
      .Append(std::move(wrong_url)).Append(std::move(missing_digest))));
  const auto manual_only = Select(releases, cache);
  CHECK(manual_only.latest == base::Version("0.3.0"));
  CHECK(manual_only.release_url.is_valid() && !manual_only.installer);
  const auto current = Select(releases, cache, "0.3.0");
  CHECK(current.latest == base::Version("0.3.0"));
  CHECK(!current.release_url.is_valid() && !current.installer);

  const auto legacy_only = Select(releases, cache, "0.1.0", kContent);
  CHECK(!legacy_only.latest.IsValid() && !legacy_only.release_url.is_valid() &&
        !legacy_only.installer);
  releases.Append(Release("v0.2.5", base::ListValue()
      .Append(Asset("v0.2.5", "macos-content-universal.dmg"))
      .Append(Asset("v0.2.5", "macos-content-arm64.dmg"))));
  const auto content = Select(releases, cache, "0.1.0", kContent);
  CHECK(content.latest == base::Version("0.2.5") && content.installer);
  CHECK(content.installer->path.BaseName().value() ==
        "Zitter-v0.2.5-macos-content-arm64.dmg");
  releases.Append(Release("v0.4.0", base::ListValue()
      .Append(Asset("v0.4.0", "macos-content-arm64.dmg"))));
  CHECK(Select(releases, cache, "0.1.0", kChrome).latest == base::Version("0.3.0"));
}
