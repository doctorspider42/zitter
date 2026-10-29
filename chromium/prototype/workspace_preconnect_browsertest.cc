#include "chromium/prototype/i18n.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/workspace_window.h"

#include <atomic>
#include <memory>
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_restrictions.h"
#include "base/uuid.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/socket/delayed_stream_socket.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/embedded_test_server_connection_listener.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/textfield/textfield.h"

namespace zitter::prototype {
namespace {
void PumpFor(base::TimeDelta duration) {
  base::test::TestFuture<void> elapsed;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, elapsed.GetCallback(), duration);
  ASSERT_TRUE(elapsed.Wait());
}

// Put the entire round-trip delay on reads below TLS. Writes must reach the OS
// before the HTTP/1 test server closes a completed response. DelayedStreamSocket
// otherwise acknowledges buffered writes before flushing, losing data on close.
class ServerDelaySocket final : public net::DelayedStreamSocket {
 public:
  ServerDelaySocket(std::unique_ptr<net::StreamSocket> socket,
                    net::StreamSocket* inner, base::TimeDelta rtt)
      : net::DelayedStreamSocket(std::move(socket), Config(rtt), nullptr, nullptr), inner_(inner) {}
  int Write(net::IOBuffer* buffer, int length, net::CompletionOnceCallback callback,
            const net::NetworkTrafficAnnotationTag& annotation) override {
    return inner_->Write(buffer, length, std::move(callback), annotation);
  }
 private:
  static net::DelayedSocketConfig Config(base::TimeDelta rtt) {
    net::DelayedSocketConfig config; config.rtt = rtt * 2;
    return config;
  }
  raw_ptr<net::StreamSocket> inner_;
};

// TLS and HTTP each pay modeled round trips. TCP accept and DNS remain local.
class ConnectionProbe final : public net::test_server::EmbeddedTestServerConnectionListener {
 public:
  explicit ConnectionProbe(base::TimeDelta rtt = base::Milliseconds(120),
                           net::EmbeddedTestServer::Type type = net::EmbeddedTestServer::TYPE_HTTPS)
      : rtt_(rtt), server_(type) {}
  ~ConnectionProbe() override { if (started_) EXPECT_TRUE(server_.ShutdownAndWaitUntilComplete()); }
  bool Start() {
    server_.SetConnectionListener(this);
    server_.RegisterRequestMonitor(base::BindRepeating(
        [](ConnectionProbe* self, const net::test_server::HttpRequest& request) {
          ++self->requests;
          base::AutoLock guard(self->lock_);
          const auto cookie = request.headers.find("Cookie");
          self->cookie_ = cookie == request.headers.end() ? "" : cookie->second;
        }, base::Unretained(this)));
    server_.RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest&) -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response = std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_code(net::HTTP_OK); response->set_content_type("text/html");
          response->AddCustomHeader("Cache-Control", "no-store");
          response->set_content("<!doctype html><title>Connection probe</title><link rel=icon href='data:image/svg+xml,%3Csvg%20xmlns=%22http://www.w3.org/2000/svg%22/%3E'>"
                                "<style>body{background:#eef2fb;padding:40px;font:24px system-ui}</style>"
                                "<h1>Connection probe</h1><p>Local TLS page with no subresources.</p>");
          return response;
        }));
    started_ = server_.Start();
    return started_;
  }
  GURL url() const { return server_.GetURL("/warmup"); }
  std::string cookie() { base::AutoLock guard(lock_); return cookie_; }
  std::unique_ptr<net::StreamSocket> AcceptedSocket(std::unique_ptr<net::StreamSocket> socket) override {
    ++connections;
    auto* inner = socket.get();
    return std::make_unique<ServerDelaySocket>(std::move(socket), inner, rtt_);
  }
  void ReadFromSocket(const net::StreamSocket&, int) override {}
  std::atomic<int> connections{0}, requests{0};
 private:
  const base::TimeDelta rtt_;
  base::Lock lock_;
  std::string cookie_;
  net::EmbeddedTestServer server_;
  bool started_ = false;
};
}  // namespace

class ZitterPreconnectBrowserTest : public InProcessBrowserTest {
 public:
  void SetUp() override {
    const auto* command = base::CommandLine::ForCurrentProcess();
    const bool enabled = !command->HasSwitch("zitter-benchmark") || command->HasSwitch("zitter-perf-preconnect");
    features_.InitFromCommandLine(enabled ? "ZitterOmniboxPreconnect" : "",
                                 enabled ? "" : "ZitterOmniboxPreconnect");
    InProcessBrowserTest::SetUp();
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    auto* manager = g_browser_process->profile_manager();
    base::test::TestFuture<Profile*> work, personal;
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"), work.GetCallback());
    work_ = work.Get(); ASSERT_TRUE(work_);
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterPersonal"), personal.GetCallback());
    personal_ = personal.Get(); ASSERT_TRUE(personal_);
    work_lease_ = std::make_unique<ScopedProfileKeepAlive>(work_, ProfileKeepAliveOrigin::kBrowserWindow);
    personal_lease_ = std::make_unique<ScopedProfileKeepAlive>(personal_, ProfileKeepAliveOrigin::kBrowserWindow);
    prefetch::SetPreloadPagesState(work_->GetPrefs(), prefetch::PreloadPagesState::kStandardPreloading);
    prefetch::SetPreloadPagesState(personal_->GetPrefs(), prefetch::PreloadPagesState::kStandardPreloading);
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
  }
  void TearDownOnMainThread() override {
    window_.reset(); work_ = nullptr; personal_ = nullptr;
    personal_lease_.reset(); work_lease_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
 protected:
  void TypeAddress(std::u16string query) {
    auto* input = window_->shell()->address_input();
    const auto length=input->GetText().size();input->SelectRange(gfx::Range(0,length));
    if(query.empty()){if(length)ASSERT_TRUE(input->input_client()->DeleteRange(gfx::Range(0,length)));}
    else input->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  }
  base::test::ScopedFeatureList features_;
  raw_ptr<Profile> work_ = nullptr, personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_, personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
};

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, PredictionPreferenceAndCancelledIntent) {
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  const auto query = base::UTF8ToUTF16(probe.url().spec());
  window_->shell()->OpenAddress(true);
  ASSERT_TRUE(window_->WritePreference("preconnect", 0));
  EXPECT_EQ(0, window_->ReadPreferences().at("preconnect").value);
  EXPECT_FALSE(window_->WritePreference("preconnect", 2));
  TypeAddress(query); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load());
  ASSERT_TRUE(window_->WritePreference("preconnect", 1));
  TypeAddress(query); TypeAddress(u">commands");
  PumpFor(base::Milliseconds(450)); EXPECT_EQ(0, probe.connections.load());
  TypeAddress(query); PumpFor(base::Milliseconds(150)); window_->shell()->CloseOverlays();
  PumpFor(base::Milliseconds(450)); EXPECT_EQ(0, probe.connections.load());
  window_->shell()->OpenAddress(true); TypeAddress(query); PumpFor(base::Milliseconds(150));
  ASSERT_TRUE(window_->WritePreference("preconnect", 0));
  PumpFor(base::Milliseconds(450)); EXPECT_EQ(0, probe.connections.load());
  ASSERT_TRUE(window_->WritePreference("preconnect", 1));
  TypeAddress(query);
  ASSERT_TRUE(base::test::RunUntil([&] { return probe.connections.load() >= 1; }));
  EXPECT_EQ(0, probe.requests.load());
  TypeAddress(query); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(1, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
  ASSERT_TRUE(window_->NavigateFromBar(probe.url(), false));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(probe.url(), window_->contents("work")->GetLastCommittedURL());
  EXPECT_EQ(1, probe.requests.load()); EXPECT_EQ(1, probe.connections.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, RoutedProfileOwnsWarmConnectionAndCookies) {
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  ASSERT_TRUE(content::SetCookie(work_, probe.url(), "marker=work"));
  ASSERT_TRUE(content::SetCookie(personal_, probe.url(), "marker=personal"));
  prefetch::SetPreloadPagesState(work_->GetPrefs(), prefetch::PreloadPagesState::kNoPreloading);
  ASSERT_TRUE(window_->SetLinkRule(probe.url(), "personal"));
  window_->shell()->OpenAddress(true); TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
  ASSERT_TRUE(base::test::RunUntil([&] { return probe.connections.load() >= 1; }));
  EXPECT_EQ(0, probe.requests.load()); EXPECT_EQ("work", window_->active_id());
  ASSERT_TRUE(window_->OpenLink(probe.url(), "work", true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("personal")));
  EXPECT_EQ(personal_, window_->contents("personal")->GetBrowserContext());
  EXPECT_EQ(1, probe.connections.load()); EXPECT_EQ(1, probe.requests.load());
  EXPECT_NE(std::string::npos, probe.cookie().find("marker=personal"));
  EXPECT_EQ(std::string::npos, probe.cookie().find("marker=work"));
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, ClosingCancelsPendingHint) {
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  window_->shell()->OpenAddress(true); TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
  PumpFor(base::Milliseconds(150)); window_.reset(); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, PredictionPreferenceIsScopedAndPreservesExtendedState) {
  prefetch::SetPreloadPagesState(work_->GetPrefs(), prefetch::PreloadPagesState::kExtendedPreloading);
  ASSERT_TRUE(window_->WritePreference("preconnect", 1));
  EXPECT_EQ(prefetch::PreloadPagesState::kExtendedPreloading, prefetch::GetPreloadPagesState(*work_->GetPrefs()));
  ASSERT_TRUE(window_->WritePreference("preconnect", 0));
  EXPECT_EQ(0, window_->ReadPreferences().at("preconnect").value);
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_EQ(1, window_->ReadPreferences().at("preconnect").value);
  ASSERT_TRUE(window_->WritePreference("preconnect", 0));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->WritePreference("preconnect", 1));
  EXPECT_EQ(prefetch::PreloadPagesState::kStandardPreloading, prefetch::GetPreloadPagesState(*work_->GetPrefs()));
  EXPECT_EQ(prefetch::PreloadPagesState::kNoPreloading, prefetch::GetPreloadPagesState(*personal_->GetPrefs()));
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, BareDomainUsesAddressClassifierWithoutWarmingSearchQueries) {
  ConnectionProbe probe(base::Milliseconds(120), net::EmbeddedTestServer::TYPE_HTTP); ASSERT_TRUE(probe.Start());
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_ready(); }));
  ASSERT_TRUE(shell->UpsertSearchProvider({"Local search", "s", probe.url().spec() + "?q={query}"}));
  ASSERT_TRUE(shell->SetDefaultSearchProvider("s"));
  shell->OpenAddress(true);
  TypeAddress(u"ordinary search words"); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load());
  TypeAddress(u"s keyword query"); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load());
  const auto address = probe.url().spec().substr(probe.url().scheme().size() + 3);
  ASSERT_EQ(probe.url(), shell->ResolveInput(address));
  TypeAddress(base::UTF8ToUTF16(address));
  ASSERT_TRUE(base::test::RunUntil([&] { return probe.connections.load() >= 1; }));
  EXPECT_EQ(1, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, PendingProfileDoesNotQueueSpeculativeConnections) {
  window_.reset(); base::ThreadPoolInstance::Get()->FlushForTesting();
  auto* manager = g_browser_process->profile_manager();
  const auto directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  const auto cold_path = manager->user_data_dir().AppendASCII(directory);
  base::ListValue profiles, spaces;
  for (int i = 0; i < 2; ++i) {
    base::DictValue profile, space;
    profile.Set("id", i == 0 ? "work-profile" : "cold-profile");
    profile.Set("directory", i == 0 ? "ZitterWork" : directory);
    profile.Set("name", i == 0 ? "Work" : "Cold"); profiles.Append(std::move(profile));
    space.Set("id", i == 0 ? "work" : "cold");
    space.Set("profile_id", i == 0 ? "work-profile" : "cold-profile");
    space.Set("name", i == 0 ? "Work" : "Cold"); spaces.Append(std::move(space));
  }
  base::DictValue catalogue; catalogue.Set("profiles", std::move(profiles));
  catalogue.Set("workspaces", std::move(spaces)); catalogue.Set("active", "work");
  catalogue.Set("onboarding_complete", true);
  std::string json; ASSERT_TRUE(base::JSONWriter::Write(catalogue, &json));
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterWorkspaces.json"), json));
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterSession.json"), "{}"));
  }
  base::OnceCallback<void(Profile*)> pending;
  SetUiLanguageForTesting("pl");  // Assertions read the Polish UI.
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  window_->SetStartupProfileLoaderForTesting(base::BindRepeating(
      [](base::FilePath cold, base::OnceCallback<void(Profile*)>* pending,
         base::FilePath path, base::OnceCallback<void(Profile*)> complete) {
        if (path == cold) { *pending = std::move(complete); return; }
        g_browser_process->profile_manager()->CreateProfileAsync(path, std::move(complete));
      }, cold_path, base::Unretained(&pending)));
  base::ScopedClosureRunner close_before_pending_dies(base::BindOnce(
      [](std::unique_ptr<WorkspaceWindow>* window) { window->reset(); }, base::Unretained(&window_)));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && !pending.is_null(); }));
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  ASSERT_TRUE(window_->SetLinkRule(probe.url(), "cold"));
  window_->shell()->OpenAddress(true); TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
  PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
  EXPECT_EQ(nullptr, window_->contents("cold"));
  base::test::TestFuture<Profile*> loaded;
  manager->CreateProfileAsync(cold_path, loaded.GetCallback());
  auto* profile = loaded.Get(); ASSERT_TRUE(profile);
  prefetch::SetPreloadPagesState(profile->GetPrefs(), prefetch::PreloadPagesState::kStandardPreloading);
  std::move(pending).Run(profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  PumpFor(base::Milliseconds(250)); EXPECT_EQ(0, probe.connections.load());
  TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
  ASSERT_TRUE(base::test::RunUntil([&] { return probe.connections.load() >= 1; }));
  EXPECT_EQ(0, probe.requests.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, ProfileReassignmentInvalidatesHint) {
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  ASSERT_TRUE(window_->SetLinkRule(probe.url(), "personal"));
  window_->shell()->OpenAddress(true); TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
  PumpFor(base::Milliseconds(150));
  ASSERT_TRUE(window_->AssignProfile("personal", "work-profile"));
  PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, LateTextAndRouteChangesInvalidateHint) {
  ConnectionProbe probe; ASSERT_TRUE(probe.Start());
  window_->shell()->OpenAddress(true);
  TypeAddress(base::UTF8ToUTF16(probe.url().spec())); PumpFor(base::Milliseconds(250));
  TypeAddress(u">commands"); PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load());
  TypeAddress(base::UTF8ToUTF16(probe.url().spec())); PumpFor(base::Milliseconds(150));
  ASSERT_TRUE(window_->SetLinkRule(probe.url(), "personal"));
  PumpFor(base::Milliseconds(450));
  EXPECT_EQ(0, probe.connections.load()); EXPECT_EQ(0, probe.requests.load());
}

IN_PROC_BROWSER_TEST_F(ZitterPreconnectBrowserTest, PerformancePreconnect) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch("zitter-benchmark")) GTEST_SKIP();
  int iterations = 30;
  if (command->HasSwitch("zitter-perf-iterations"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-iterations"), &iterations));
  ASSERT_GE(iterations, 1); ASSERT_LE(iterations, 200);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  for (int trial = -2; trial < iterations; ++trial) {
    ConnectionProbe probe; ASSERT_TRUE(probe.Start());  // Fresh TCP/TLS origin each trial.
    window_->shell()->OpenAddress(false);
    TypeAddress(base::UTF8ToUTF16(probe.url().spec()));
    PumpFor(base::Milliseconds(600));  // Identical user think time in both variants.
    const int connections_before = probe.connections.load();
    ASSERT_EQ(0, probe.requests.load());
    class Paint final : public content::WebContentsObserver {
     public:
      explicit Paint(content::WebContents* page) : content::WebContentsObserver(page) {}
      void DidFirstVisuallyNonEmptyPaint() override { painted = base::TimeTicks::Now(); }
      base::TimeTicks painted;
    } paint(window_->contents("work"));
    const auto start = base::TimeTicks::Now();
    ASSERT_TRUE(window_->NavigateFromBar(probe.url(), false));
    ASSERT_TRUE(base::test::RunUntil([&] { return !paint.painted.is_null(); }));
    ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
    ASSERT_EQ(probe.url(), window_->contents("work")->GetLastCommittedURL());
    const auto response_start = content::EvalJs(window_->contents("work"),
        "performance.getEntriesByType('navigation')[0].responseStart").ExtractDouble();
    ASSERT_GT(response_start, 0);
    if (trial >= 0) {
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"preconnect_first_visual_paint_ms\",\"trial\":" << trial
                << ",\"ms\":" << (paint.painted - start).InMillisecondsF() << "}";
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"preconnect_response_start_ms\",\"trial\":" << trial
                << ",\"ms\":" << response_start << "}";
      LOG(INFO) << "ZITTER_CONNECTION {\"trial\":" << trial << ",\"connections_before_navigation\":" << connections_before
                << ",\"accepted_connections\":" << probe.connections.load() << ",\"http_requests\":" << probe.requests.load()
                << ",\"http_requests_before_navigation\":0,\"simulated_rtt_ms\":120,\"typing_pause_ms\":600}";
    }
  }
}
}  // namespace zitter::prototype
