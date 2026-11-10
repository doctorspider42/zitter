// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Arc's StorableSidebar.json as Arc for Windows and macOS write it.
#include "chromium/prototype/arc_importer.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zitter::prototype {
// The importer's messages in their Polish source; the catalogue is tested apart.
std::u16string T(std::u16string_view polish) { return std::u16string(polish); }
std::u16string TF(std::u16string_view polish, std::initializer_list<std::u16string_view> args) {
  std::u16string out(polish);
  for (auto arg : args)
    if (const auto at = out.find(u"{}"); at != std::u16string::npos) out.replace(at, 2, arg);
  return out;
}

namespace {
// One space with a pinned folder and tab in the main window, then a Little Arc
// window whose transient space has only unpinned tabs.
constexpr char kSidebar[] = R"({
  "sidebar": {"containers": [
    {"global": {}},
    {"spaces": ["s1", {"id": "s1", "title": "Praca", "profile": {"default": true},
                       "customInfo": {"iconType": {"emoji_v2": "💼"}},
                       "newContainerIDs": [{"unpinned": {}}, "u1", {"pinned": {}}, "p1"]}],
     "items": ["p1", {"id": "p1", "data": {"itemContainer": {}}, "childrenIds": ["f1"]},
               "f1", {"id": "f1", "title": "Projekty", "data": {"list": {}}, "childrenIds": ["t1"]},
               "t1", {"id": "t1", "data": {"tab": {"savedURL": "https://example.com/", "savedTitle": "Example"}}},
               "u1", {"id": "u1", "data": {"itemContainer": {}}, "childrenIds": []}],
     "topAppsContainerIDs": []},
    {"littleBrowser": {}},
    {"spaces": ["l1", {"id": "l1", "profile": {"default": true},
                       "newContainerIDs": [{"unpinned": {}}, "lu"]}],
     "items": ["lu", {"id": "lu", "data": {"itemContainer": {}}, "childrenIds": []}],
     "topAppsContainerIDs": []}
  ]},
  "firebaseSyncState": {"syncData": {"orderedSpaceIDs": {"value": ["s1"]}}}
})";

TEST(ArcImporterTest, ImportsPinnedTabsAndSkipsLittleArcWindows) {
  const auto result = ParseArcImport(kSidebar, R"({"profile":{"info_cache":{"Default":{"name":"Osobisty"}}}})");
  ASSERT_TRUE(result.error.empty()) << result.error;
  ASSERT_EQ(result.spaces.size(), 1u);
  EXPECT_EQ(result.spaces[0].name, "Praca");
  EXPECT_EQ(result.spaces[0].emoji, u"💼");
  EXPECT_EQ(result.folder_count, 1u);
  EXPECT_EQ(result.tab_count, 1u);
  ASSERT_EQ(result.spaces[0].pins.size(), 2u);
  EXPECT_EQ(result.spaces[0].pins[1].url, GURL("https://example.com/"));
  EXPECT_EQ(result.spaces[0].pins[1].parent_id, result.spaces[0].pins[0].id);
  ASSERT_EQ(result.profiles.size(), 1u);
  EXPECT_EQ(result.profiles[0].name, "Osobisty");
}

TEST(ArcImporterTest, ImportsFavoritesByProfileAndHonorsImportOptions) {
  const auto parsed = ParseArcImport(R"json({"sidebar":{"containers":[
    {"global":{}}, {"spaces":{
      "s1":{"profile":{"default":true},"containerIDs":["pinned","p1"]},
      "s2":{"profile":{"default":true},"containerIDs":["pinned","p1"]}
    },"items":{
      "p1":{"data":{"itemContainer":{}}},
      "r1":{"data":{"itemContainer":{}},"childrenIds":["b","a","bad"]},
      "r2":{"data":{"itemContainer":{}},"childrenIds":["c"]},
      "a":{"data":{"tab":{"savedURL":"https://example.test/a"}}},
      "b":{"title":"Favorite B","data":{"tab":{"savedURL":"https://example.test/b"}}},
      "c":{"data":{"tab":{"savedURL":"https://example.test/c"}}},
      "bad":{"data":{"tab":{"savedURL":"javascript:alert(1)"}}}
    },"topAppsContainerIDs":[
      {"default":true},"r1",{"custom":{"_0":{"directoryBasename":"Profile 1"}}},"r2"
    ]}
  ]}})json", "{}");
  ASSERT_TRUE(parsed.error.empty()) << parsed.error;
  ASSERT_EQ(parsed.spaces.size(), 2u);
  EXPECT_TRUE(parsed.spaces[0].pins.empty());
  EXPECT_TRUE(parsed.spaces[1].pins.empty());
  EXPECT_EQ(parsed.tab_count, 3u);
  EXPECT_EQ(parsed.folder_count, 0u);
  EXPECT_EQ(parsed.skipped_count, 1u);
  ASSERT_EQ(parsed.profile_favorites.size(), 2u);
  const auto& defaults = parsed.profile_favorites.at("arc-profile-Default");
  ASSERT_EQ(defaults.size(), 2u);
  EXPECT_EQ(defaults[0].title, "Favorite B");
  EXPECT_EQ(defaults[1].url, GURL("https://example.test/a"));
  EXPECT_TRUE(defaults[0].profile_pin);
  EXPECT_TRUE(defaults[0].parent_id.empty());
  EXPECT_FALSE(defaults[0].folder);

  BrowserImportOptions options;
  options.separate_profiles = false;
  const auto merged = ApplyImportOptions(parsed, options);
  ASSERT_EQ(merged.profiles.size(), 1u);
  ASSERT_EQ(merged.profile_favorites.size(), 1u);
  const auto& favorites = merged.profile_favorites.at(merged.profiles[0].id);
  ASSERT_EQ(favorites.size(), 3u);
  EXPECT_EQ(favorites[0], defaults[0]);
  EXPECT_EQ(favorites[1], defaults[1]);
  EXPECT_EQ(favorites[2].url, GURL("https://example.test/c"));
  EXPECT_EQ(merged.tab_count, 3u);

  options.bookmarks = false;
  const auto without_bookmarks = ApplyImportOptions(parsed, options);
  EXPECT_TRUE(without_bookmarks.profile_favorites.empty());
  EXPECT_EQ(without_bookmarks.tab_count, 0u);
}

TEST(ArcImporterTest, ReadsArcsFolderAndReportsAMissingOne) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_FALSE(ReadArcImport(directory.GetPath()).error.empty());
  ASSERT_TRUE(base::WriteFile(directory.GetPath().AppendASCII("StorableSidebar.json"), kSidebar));
  EXPECT_EQ(ReadArcImport(directory.GetPath()).spaces.size(), 1u);
  EXPECT_FALSE(ReadArcImport(base::FilePath()).error.empty());
}

TEST(ArcImporterTest, ImportsGitHubLiveFolderAndItsCachedPullRequests) {
  const auto result = ParseArcImport(R"json({"sidebar":{"containers":[
    {"global":{}}, {"spaces":{"s1":{"profile":{"default":true},"containerIDs":["pinned","root"]}},
      "items":{
        "root":{"data":{"itemContainer":{}},"childrenIds":["prs","plain"]},
        "prs":{"title":"Review","data":{"list":{"automaticLiveFolderData":{
          "dataSource":{"github":{}},"hiddenItems":[],"lastFetch":0
        }}},"childrenIds":["pr"]},
        "pr":{"data":{"tab":{"savedURL":"https://github.com/owner/repo/pull/17","savedTitle":"Fix layout"}}},
        "plain":{"data":{"list":{}}}
      }}
  ]}})json", "{}");
  ASSERT_TRUE(result.error.empty()) << result.error;
  ASSERT_EQ(result.spaces.size(), 1u);
  const auto& pins = result.spaces[0].pins;
  ASSERT_EQ(pins.size(), 3u);
  EXPECT_EQ(pins[0].title, "Review");
  ASSERT_TRUE(pins[0].github);
  EXPECT_TRUE(pins[0].github->login.empty());
  EXPECT_EQ(pins[1].parent_id, pins[0].id);
  EXPECT_EQ(pins[1].url, GURL("https://github.com/owner/repo/pull/17"));
  EXPECT_FALSE(pins[2].github);
}

// Reads this computer's Arc, if any, and prints only counts: run by hand with
// --gtest_also_run_disabled_tests to check a new Arc release's format.
TEST(ArcImporterTest, DISABLED_ReadsThisComputersArc) {
  const auto directory = DefaultArcDirectory();
  if (directory.empty()) GTEST_SKIP() << "No Arc on this computer";
  const auto result = ReadArcImport(directory);
  EXPECT_TRUE(result.error.empty()) << result.error;
  LOG(INFO) << result.profiles.size() << " profiles, " << result.spaces.size() << " spaces, "
            << result.folder_count << " folders, " << result.tab_count << " tabs, "
            << result.skipped_count << " skipped";
}

// The same for Chromium browsers' bookmarks (Chrome, Edge, Brave), by hand.
TEST(ArcImporterTest, DISABLED_ReadsThisComputersChromiumBrowsers) {
  for (auto browser : {ImportBrowser::kChrome, ImportBrowser::kEdge, ImportBrowser::kBrave}) {
    const auto directory = DefaultBrowserDirectory(browser);
    if (directory.empty() || !base::DirectoryExists(directory)) continue;
    BrowserImportOptions options;
    options.browser = browser;
    const auto result = ReadBrowserImport(options, directory);
    EXPECT_TRUE(result.error.empty()) << result.error;
    LOG(INFO) << ImportBrowserName(browser) << ": " << result.profiles.size() << " profiles, "
              << result.spaces.size() << " spaces, " << result.folder_count << " folders, "
              << result.tab_count << " bookmarks, " << result.skipped_count << " skipped";
  }
}
}  // namespace
}  // namespace zitter::prototype
