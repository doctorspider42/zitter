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
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/app/chrome_main_mac.h"
#include "chrome/common/mac/detect_inappropriate_exit.h"
#elif BUILDFLAG(IS_LINUX)
#include "chrome/app/chrome_main_linux.h"
#endif

namespace {
std::optional<base::CommandLine>& InitialCommandLine() {
  static base::NoDestructor<std::optional<base::CommandLine>> command;
  return *command;
}
base::FilePath DefaultDataDirectory() {
  base::FilePath home;
  CHECK(base::PathService::Get(base::DIR_HOME, &home));
#if BUILDFLAG(IS_MAC)
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

int main(int argc, const char** argv) {
  // Unlike Chrome's small macOS launcher, this executable links Chromium
  // directly. PartitionAlloc initializes before main; registering its early
  // delegating zone here would register the allocator twice in static builds.
  base::CommandLine::Init(argc, argv);
  auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch(switches::kProcessType)) {
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
  params.argc = argc;
  params.argv = argv;
  return content::ContentMain(std::move(params));
}
