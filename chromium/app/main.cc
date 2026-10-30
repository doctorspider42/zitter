// Browser bootstrap follows the pinned //chrome/app/chrome_main.cc ordering.
// Chromium still owns sandboxing, network services, child processes and shutdown.
#include <memory>
#include <optional>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/sampling_heap_profiler/poisson_allocation_sampler.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/common/chrome_switches.h"
#include "chromium/app/browser_client.h"
#include "chromium/app/network_policy.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/app/chrome_main_mac.h"
#include "chrome/common/mac/detect_inappropriate_exit.h"
#elif BUILDFLAG(IS_LINUX)
#include "chrome/app/chrome_main_linux.h"
#elif BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/files/file_enumerator.h"
#include "base/win/win_util.h"
#include "chrome/install_static/initialize_from_primary_module.h"
#include "content/public/app/sandbox_helper_win.h"
#include "sandbox/win/src/sandbox_types.h"
#include "ui/gfx/win/direct_write.h"
#endif

namespace {
std::optional<base::CommandLine>& InitialCommandLine() {
  static base::NoDestructor<std::optional<base::CommandLine>> command;
  return *command;
}
base::FilePath DefaultDataDirectory() {
  base::FilePath home;
  CHECK(base::PathService::Get(base::DIR_HOME, &home));
#if BUILDFLAG(IS_WIN)
  // %LOCALAPPDATA%\Zitter, next to where Chromium keeps its User Data.
  base::FilePath local;
  CHECK(base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local));
  return local.Append(FILE_PATH_LITERAL("Zitter"));
#elif BUILDFLAG(IS_MAC)
  CHECK(base::CreateDirectory(home.Append("Library").Append("Caches").Append("Zitter")));
  return home.Append("Library").Append("Application Support").Append("Zitter");
#else
  auto config = base::Environment::Create()->GetVar("XDG_CONFIG_HOME");
  base::FilePath root = config ? base::FilePath(*config) : home.Append(".config");
  if (!root.IsAbsolute()) root = home.Append(".config");
  return root.Append("zitter");
#endif
}
}  // namespace

// Used by Chromium startup services; capture before upstream mutates switches.
const base::CommandLine& GetInitialBrowserCommandLine();
const base::CommandLine& GetInitialBrowserCommandLine() {
  return InitialCommandLine().value();
}

#if BUILDFLAG(IS_WIN)
// One executable for every process, as content_shell does: Chrome splits
// chrome.exe and chrome.dll, Zitter links Chromium into zitter.exe.
int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
  // chrome_elf (loaded with this executable) owns the install details that
  // //chrome reads; this module takes its copy, as chrome.dll does.
  install_static::InitializeFromPrimaryModule();
  base::win::SetAbortBehaviorForCrashReporting();
  base::CommandLine::Init(0, nullptr);
#else
int main(int argc, const char** argv) {
  // Unlike Chrome's small macOS launcher, this executable links Chromium
  // directly. PartitionAlloc initializes before main; registering its early
  // delegating zone here would register the allocator twice in static builds.
  base::CommandLine::Init(argc, argv);
#endif
  auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch(switches::kProcessType)) {
#if BUILDFLAG(IS_WIN)
    // The design fonts (Fonts\ beside zitter.exe) join the browser's font
    // collection before gfx creates it; macOS activates them from the bundle.
    // Web pages never see them.
    base::FilePath exe_dir;
    if (base::PathService::Get(base::DIR_EXE, &exe_dir)) {
      base::FileEnumerator fonts(exe_dir.Append(FILE_PATH_LITERAL("Fonts")), false,
                                 base::FileEnumerator::FILES, FILE_PATH_LITERAL("*.ttf"));
      for (auto path = fonts.Next(); !path.empty(); path = fonts.Next())
        gfx::win::SideLoadFontForTesting(path);
    }
#endif
    zitter::ConfigureSafeBrowsingKey();
    zitter::ConfigureNetworkPolicy(*command);
    if (!command->HasSwitch(switches::kUserDataDir))
      command->AppendSwitchPath(switches::kUserDataDir, DefaultDataDirectory());
    // Chrome initializes services/profiles, but only our Views window is opened.
    command->AppendSwitch(switches::kNoStartupWindow);
    command->AppendSwitch(switches::kNoFirstRun);
    InitialCommandLine() = *command;
  }
#if BUILDFLAG(IS_MAC)
  chrome::InitializeExitSixtyNineDetector();
  SetUpBundleOverrides();
  if (IsHelperAppLaunchedBySystemOrThirdPartyApplication()) return 0;
#elif BUILDFLAG(IS_LINUX)
  AppendExtraArgumentsToCommandLine(command);
#endif
  base::PoissonAllocationSampler::Init();
  zitter::MainDelegate delegate({.exe_entry_point_ticks = base::TimeTicks::Now()});
  content::ContentMainParams params(&delegate);
#if BUILDFLAG(IS_WIN)
  // The sandbox broker (browser) or target (children) needs the command line
  // before ContentMain.
  sandbox::SandboxInterfaceInfo sandbox_info = {nullptr};
  content::InitializeSandboxInfo(&sandbox_info);
  params.instance = instance;
  params.sandbox_info = &sandbox_info;
#else
  params.argc = argc;
  params.argv = argv;
#endif
  return content::ContentMain(std::move(params));
}
