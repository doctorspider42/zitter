#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_file_access.h"
#include "chromium/prototype/workspace_window.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "sandbox/policy/features.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"
#include "ui/shell_dialogs/fake_select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::prototype {
namespace {
class UploadResult : public content::FileSelectListener {
 public:
  void FileSelected(std::vector<blink::mojom::FileChooserFileInfoPtr> selected,
                    const base::FilePath& directory,
                    blink::mojom::FileChooserParams::Mode selected_mode) override {
    ++calls; files = std::move(selected); root = directory; mode = selected_mode;
  }
  void FileSelectionCanceled() override { ++calls; canceled = true; }
  int calls = 0;
  bool canceled = false;
  std::vector<blink::mojom::FileChooserFileInfoPtr> files;
  base::FilePath root;
  blink::mojom::FileChooserParams::Mode mode = blink::mojom::FileChooserParams::Mode::kOpen;
 private:
  friend class base::RefCounted<content::FileSelectListener>;
  ~UploadResult() override = default;
};
}  // namespace

class ZitterFileUploadBrowserTest : public InProcessBrowserTest {
 public:
#if BUILDFLAG(IS_WIN)
  // Development builds turn the network sandbox on (fieldtrial_testing_config),
  // and its AppContainer can't read an out\ directory that no installer granted
  // access to (as ZitterProfilesBrowserTest).
  ZitterFileUploadBrowserTest() {
    network_sandbox_.InitAndDisableFeature(sandbox::policy::features::kNetworkServiceSandbox);
  }
  base::test::ScopedFeatureList network_sandbox_;
#endif
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    auto* manager = g_browser_process->profile_manager();
    base::test::TestFuture<Profile*> work_ready, personal_ready;
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"), work_ready.GetCallback());
    work_ = work_ready.Get(); ASSERT_TRUE(work_);
    work_lease_ = std::make_unique<ScopedProfileKeepAlive>(work_, ProfileKeepAliveOrigin::kBrowserWindow);
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterPersonal"), personal_ready.GetCallback());
    personal_ = personal_ready.Get(); ASSERT_TRUE(personal_);
    personal_lease_ = std::make_unique<ScopedProfileKeepAlive>(personal_, ProfileKeepAliveOrigin::kBrowserWindow);
    browser()->GetWindow()->Hide();
    {
      base::ScopedAllowBlockingForTesting files;
      const auto config=manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
      if(!base::PathExists(config))ASSERT_TRUE(base::WriteFile(config,R"({
        "profiles":[{"id":"work-profile","name":"Profil firmowy","directory":"ZitterWork"},{"id":"personal-profile","name":"Profil osobisty","directory":"ZitterPersonal"}],
        "workspaces":[{"id":"work","name":"Praca","profile_id":"work-profile"},{"id":"personal","name":"Prywatne","profile_id":"personal-profile"}],
        "active":"work","onboarding_complete":true
      })"));
    }
    SetUiLanguageForTesting("pl");  // Assertions read the Polish UI.
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(window_->Select("work"));
    ASSERT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL("/file_upload_probe.html")));
    ASSERT_EQ(work_, page()->GetBrowserContext());
    factory_ = ui::FakeSelectFileDialog::RegisterFactory();
    factory_->SetOpenCallback(base::BindRepeating([](int* count) { ++*count; }, &opened_));
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(files_.CreateUniqueTempDir());
    first_ = files_.GetPath().AppendASCII("first.txt");
    second_ = files_.GetPath().AppendASCII("second.txt");
    ASSERT_TRUE(base::WriteFile(first_, "first upload"));
    ASSERT_TRUE(base::WriteFile(second_, "second upload"));
  }
  void TearDownOnMainThread() override {
    window_.reset();
    ui::SelectFileDialog::SetFactory(nullptr);
    factory_ = nullptr;
    personal_ = nullptr; work_ = nullptr;
    personal_lease_.reset(); work_lease_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  content::WebContents* page() { return window_->contents(window_->active_id()); }
  void Open(const char* id) {
    int before = opened_;
    ASSERT_TRUE(content::ExecJs(page(), content::JsReplace("document.getElementById($1).click()", id)));
    ASSERT_TRUE(base::test::RunUntil([&] { return opened_ > before; }));
    ASSERT_TRUE(factory_->GetLastDialog());
  }
 protected:
  raw_ptr<Profile> work_ = nullptr, personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_, personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
  raw_ptr<ui::FakeSelectFileDialog::Factory> factory_ = nullptr;
  int opened_ = 0;
  base::ScopedTempDir files_;
  base::FilePath first_, second_;
};

IN_PROC_BROWSER_TEST_F(ZitterFileUploadBrowserTest, SingleMultipleAndCancel) {
  Open("single");
  EXPECT_TRUE(factory_->GetLastDialog()->CallFileSelected(first_, "txt"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "single.files.length").ExtractInt() == 1;
  }));
  EXPECT_EQ("first.txt", content::EvalJs(page(), "single.files[0].name"));
  EXPECT_EQ("first upload", content::EvalJs(page(), "single.files[0].text()"));

  Open("multiple");
  factory_->GetLastDialog()->CallMultiFilesSelected({first_, second_});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "multiple.files.length").ExtractInt() == 2;
  }));
  EXPECT_EQ("first.txt,second.txt", content::EvalJs(page(), "Array.from(multiple.files, f=>f.name).join(',')"));
  EXPECT_EQ(first_.DirName(), work_->last_selected_directory());
  EXPECT_NE(first_.DirName(), personal_->last_selected_directory());

  Open("single"); factory_->GetLastDialog()->CallFileSelectionCanceled();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "events.includes('single:cancel')").ExtractBool();
  }));
  EXPECT_EQ(1, content::EvalJs(page(), "single.files.length"));
  Open("single"); factory_->GetLastDialog()->CallFileSelectionCanceled();

  // A second workspace must use its own Profile, including remembered paths.
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_EQ(personal_, page()->GetBrowserContext());
  ASSERT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL("/file_upload_probe.html")));
  base::FilePath personal_file = files_.GetPath().AppendASCII("personal/own.txt");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::CreateDirectory(personal_file.DirName()));
    ASSERT_TRUE(base::WriteFile(personal_file, "personal upload"));
  }
  Open("single");
  EXPECT_TRUE(factory_->GetLastDialog()->CallFileSelected(personal_file, "txt"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "single.files.length").ExtractInt() == 1;
  }));
  EXPECT_EQ("personal upload", content::EvalJs(page(), "single.files[0].text()"));
  EXPECT_EQ(first_.DirName(), work_->last_selected_directory());
  EXPECT_EQ(personal_file.DirName(), personal_->last_selected_directory());
}

IN_PROC_BROWSER_TEST_F(ZitterFileUploadBrowserTest, FolderRelativePathsAndContents) {
  base::FilePath root = files_.GetPath().AppendASCII("chosen");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::CreateDirectory(root.AppendASCII("nested")));
    ASSERT_TRUE(base::WriteFile(root.AppendASCII("root.txt"), "root"));
    ASSERT_TRUE(base::WriteFile(root.AppendASCII("nested/leaf.txt"), "leaf"));
  }
  Open("folder");
  factory_->GetLastDialog()->listener()->FileSelected(ui::SelectedFileInfo(root), 0);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "folder.files.length").ExtractInt() == 2;
  }));
  EXPECT_EQ("chosen/nested/leaf.txt,chosen/root.txt", content::EvalJs(page(),
      "Array.from(folder.files,f=>f.webkitRelativePath).sort().join(',')"));
  EXPECT_EQ("leaf,root", content::EvalJs(page(),
      "Promise.all(Array.from(folder.files,f=>f.text())).then(v=>v.sort().join(','))"));
  EXPECT_EQ(root, work_->last_selected_directory());
  EXPECT_NE(root, personal_->last_selected_directory());
}

IN_PROC_BROWSER_TEST_F(ZitterFileUploadBrowserTest, PendingFolderCanceledOnNavigationAndDestruction) {
  auto result = base::MakeRefCounted<UploadResult>();
  blink::mojom::FileChooserParams params;
  params.mode = blink::mojom::FileChooserParams::Mode::kUploadFolder;
  RunPageFileChooser(page()->GetPrimaryMainFrame(), result, params);
  ASSERT_TRUE(base::test::RunUntil([&] { return opened_ == 1; }));
  scoped_refptr<ui::FakeSelectFileDialog> pending = factory_->GetLastDialog();
  ASSERT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL("/file_upload_probe.html?new")));
  EXPECT_EQ(1, result->calls); EXPECT_TRUE(result->canceled);
  EXPECT_EQ(nullptr, pending->listener());

  auto closed = base::MakeRefCounted<UploadResult>();
  RunPageFileChooser(page()->GetPrimaryMainFrame(), closed, params);
  ASSERT_TRUE(base::test::RunUntil([&] { return opened_ == 2; }));
  pending = factory_->GetLastDialog();
  window_.reset();
  EXPECT_EQ(1, closed->calls); EXPECT_TRUE(closed->canceled);
  EXPECT_EQ(nullptr, pending->listener());
}

IN_PROC_BROWSER_TEST_F(ZitterFileUploadBrowserTest, FolderEnumerationDuringSheetOcclusion) {
  // A native picker can finish while its closing sheet still occludes the
  // parent window. That transient state must not discard an approved folder.
  auto selected = base::MakeRefCounted<UploadResult>();
  page()->WasOccluded();
  EnumeratePageDirectory(page(), selected, files_.GetPath());
  ASSERT_TRUE(base::test::RunUntil([&] { return selected->calls == 1; }));
  EXPECT_FALSE(selected->canceled);
  EXPECT_EQ(2u, selected->files.size());
  EXPECT_EQ(files_.GetPath(), selected->root);
  EXPECT_EQ(blink::mojom::FileChooserParams::Mode::kUploadFolder, selected->mode);

  page()->WasHidden();
  auto hidden = base::MakeRefCounted<UploadResult>();
  EnumeratePageDirectory(page(), hidden, files_.GetPath());
  EXPECT_EQ(1, hidden->calls);
  EXPECT_TRUE(hidden->canceled);
  EXPECT_TRUE(hidden->files.empty());
  page()->WasShown();
}

IN_PROC_BROWSER_TEST_F(ZitterFileUploadBrowserTest, BackgroundAndInvalidRequestsCancel) {
  auto* background = page();
  ASSERT_TRUE(window_->Select("personal"));
  auto result = base::MakeRefCounted<UploadResult>();
  blink::mojom::FileChooserParams params;
  RunPageFileChooser(background->GetPrimaryMainFrame(), result, params);
  EXPECT_EQ(1, result->calls); EXPECT_TRUE(result->canceled); EXPECT_EQ(0, opened_);

  auto invalid = base::MakeRefCounted<UploadResult>();
  EnumeratePageDirectory(page(), invalid, base::FilePath::FromUTF8Unsafe("../private"));
  EXPECT_EQ(1, invalid->calls); EXPECT_TRUE(invalid->canceled);
  EXPECT_EQ(0, opened_);
}
}  // namespace zitter::prototype
