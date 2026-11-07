// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/app/updater_win.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zitter::updates {
namespace {
base::DictValue Fixture() {
  base::DictValue asset;
  asset.Set("name", "Zitter-v0.3.0-windows-x64-setup.exe");
  asset.Set("browser_download_url", "https://github.com/doctorspider42/zitter/releases/download/v0.3.0/Zitter-v0.3.0-windows-x64-setup.exe");
  asset.Set("digest", "sha256:" + base::HexEncode(crypto::hash::Sha256("MZhi")));
  asset.Set("size", 4);
  base::ListValue assets;
  assets.Append(std::move(asset));
  base::DictValue release;
  release.Set("tag_name", "v0.3.0");
  release.Set("draft", false);
  release.Set("prerelease", false);
  release.Set("assets", std::move(assets));
  return release;
}
std::optional<Release> Parse(const base::DictValue& value, std::string_view version = "0.2.0") {
  return ParseRelease(*base::WriteJson(value), version);
}
base::DictValue& Asset(base::DictValue& value) {
  return value.FindList("assets")->front().GetDict();
}

TEST(WindowsUpdater, SelectsOnlyNewerNumericVersions) {
  auto fixture = Fixture();
  ASSERT_TRUE(Parse(fixture));
  EXPECT_EQ("0.3.0", Parse(fixture)->version);
  EXPECT_FALSE(Parse(fixture, "0.3.0"));
  EXPECT_FALSE(Parse(fixture, "0.10.0"));
  EXPECT_FALSE(Parse(fixture, "1.0.0"));
}
TEST(WindowsUpdater, RejectsDraftPrereleaseAndMissingFlags) {
  auto fixture = Fixture();
  fixture.Set("draft", true);
  EXPECT_FALSE(Parse(fixture));
  fixture.Set("draft", false);
  fixture.Set("prerelease", true);
  EXPECT_FALSE(Parse(fixture));
  fixture.Remove("prerelease");
  EXPECT_FALSE(Parse(fixture));
}
TEST(WindowsUpdater, RejectsMalformedAndAmbiguousVersions) {
  for (const char* tag : {"0.3.0", "v0.3.0-beta", "v0.3", "v0.3.0.1", "v../0.3.0", "v00.3.0"}) {
    auto fixture = Fixture();
    fixture.Set("tag_name", tag);
    EXPECT_FALSE(Parse(fixture)) << tag;
  }
  EXPECT_FALSE(ParseRelease("not json", "0.2.0"));
}
TEST(WindowsUpdater, RejectsUntrustedOrMismatchedDownloadLocations) {
  for (const char* url : {"http://github.com/doctorspider42/zitter/releases/download/v0.3.0/Zitter-v0.3.0-windows-x64-setup.exe",
      "https://example.com/setup.exe", "https://github.com/other/repo/releases/download/v0.3.0/setup.exe",
      "https://github.com/doctorspider42/zitter/releases/download/v0.2.0/Zitter-v0.3.0-windows-x64-setup.exe"}) {
    auto fixture = Fixture();
    Asset(fixture).Set("browser_download_url", url);
    EXPECT_FALSE(Parse(fixture)) << url;
  }
}
TEST(WindowsUpdater, RequiresExactWindowsAssetAndSingleMatch) {
  auto fixture = Fixture();
  Asset(fixture).Set("name", "Zitter-v0.3.0-macos-arm64.dmg");
  EXPECT_FALSE(Parse(fixture));
  fixture = Fixture();
  auto duplicate = Asset(fixture).Clone();
  fixture.FindList("assets")->Append(std::move(duplicate));
  EXPECT_FALSE(Parse(fixture));
}
TEST(WindowsUpdater, RequiresValidSha256Digest) {
  for (const char* digest : {"", "sha256:abc", "sha1:0123456789", "sha256:zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"}) {
    auto fixture = Fixture();
    Asset(fixture).Set("digest", digest);
    EXPECT_FALSE(Parse(fixture));
  }
  auto fixture = Fixture();
  Asset(fixture).Remove("digest");
  EXPECT_FALSE(Parse(fixture));
}
TEST(WindowsUpdater, RequiresBoundedIntegralDownloadSize) {
  for (double size : {0.0, -1.0, 1.5, 700.0 * 1024 * 1024}) {
    auto fixture = Fixture();
    Asset(fixture).Set("size", size);
    EXPECT_FALSE(Parse(fixture));
  }
}
TEST(WindowsUpdater, DetectsCorruptionTruncationAndMissingInstaller) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("setup.exe");
  auto release = Parse(Fixture());
  ASSERT_TRUE(release);
  EXPECT_FALSE(VerifyInstaller(path, *release));
  ASSERT_TRUE(base::WriteFile(path, "MZhi"));
  EXPECT_TRUE(VerifyInstaller(path, *release));
  ASSERT_TRUE(base::WriteFile(path, "MZno"));
  EXPECT_FALSE(VerifyInstaller(path, *release));
  ASSERT_TRUE(base::WriteFile(path, "MZ"));
  EXPECT_FALSE(VerifyInstaller(path, *release));
  ASSERT_TRUE(base::WriteFile(path, "MZhiextra"));
  EXPECT_FALSE(VerifyInstaller(path, *release));
}
// Opt-in release smoke: real WinHTTP, redirects, streaming, digest and cache.
// Downloads the public installer (~200 MB); never launches or installs it.
TEST(WindowsUpdater, DISABLED_PublishedInstallerDownloadsAndVerifies) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  auto result = DownloadLatest(directory.GetPath(), "0.0.0");
  ASSERT_EQ(CheckStatus::kReady, result.status);
  const auto& release = result.release;
  ASSERT_TRUE(release);
  const auto installer = directory.GetPath().AppendASCII("Zitter-" + release->version + "-setup.exe");
  EXPECT_TRUE(VerifyInstaller(installer, *release));
  // A second check reuses the verified installer rather than downloading it.
  auto cached = DownloadLatest(directory.GetPath(), "0.0.0");
  ASSERT_EQ(CheckStatus::kReady, cached.status);
  ASSERT_TRUE(cached.release);
  EXPECT_EQ(release->sha256, cached.release->sha256);
  EXPECT_EQ(CheckStatus::kNoUpdate,
            DownloadLatest(directory.GetPath(), release->version).status);
}
}  // namespace
}  // namespace zitter::updates
