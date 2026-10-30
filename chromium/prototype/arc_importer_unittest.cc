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

TEST(ArcImporterTest, ReadsArcsFolderAndReportsAMissingOne) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_FALSE(ReadArcImport(directory.GetPath()).error.empty());
  ASSERT_TRUE(base::WriteFile(directory.GetPath().AppendASCII("StorableSidebar.json"), kSidebar));
  EXPECT_EQ(ReadArcImport(directory.GetPath()).spaces.size(), 1u);
  EXPECT_FALSE(ReadArcImport(base::FilePath()).error.empty());
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