// Browser bootstrap follows the pinned //chrome/app/chrome_main.cc ordering.
// Chromium still owns sandboxing, network services, child processes and shutdown.
#include <memory>
#include <optional>

#include "base/base_paths.h"
#include "base/base_switches.h"
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
#if BUILDFLAG(IS_WIN)
#include "chromium/prototype/jump_list_win.h"
#endif
#include "chromium/app/browser_client.h"
#include "chromium/app/network_policy.h"
#include "chromium/app/startup_timing.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/app/chrome_main_mac.h"
#include "chrome/common/mac/detect_inappropriate_exit.h"
#elif BUILDFLAG(IS_LINUX)
#include "chrome/app/chrome_main_linux.h"
#elif BUILDFLAG(IS_WIN)
#include "chromium/app/updater_win.h"
#include "chromium/app/windows_channel.h"
#include <windows.h>

#include <shobjidl.h>

#include "base/files/file_enumerator.h"
#include "base/process/launch.h"
#include "base/win/win_util.h"
#include "chrome/chrome_elf/chrome_elf_main.h"
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
  base::FilePath local;
  CHECK(base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local));
  auto root = local.Append(zitter::windows::kProductName);
  return zitter::windows::kDevBuild ? root.Append(FILE_PATH_LITERAL("User Data")) : root;
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

#if BUILDFLAG(IS_WIN)
std::optional<int> PrepareDevProfile(base::CommandLine& command) {
  const auto default_profile = DefaultDataDirectory();
  const auto dev_data_root = default_profile.DirName();
  const auto fail = [](const wchar_t* reason) {
    ::MessageBoxW(nullptr, reason, zitter::windows::kProductName, MB_OK | MB_ICONERROR);
    return 1;
  };
  if (!command.HasSwitch(switches::kUserDataDir)) {
    auto profile = default_profile;
    if (command.HasSwitch(zitter::windows::kCleanProfileSwitch)) {
      const auto profiles = dev_data_root.Append(FILE_PATH_LITERAL("Clean Profiles"));
      if (!base::CreateDirectory(profiles) ||
          !base::CreateTemporaryDirInDir(profiles, FILE_PATH_LITERAL("Profile"), &profile))
        return fail(L"Could not create a clean profile.");
    }
    // chrome_elf reads the original Windows command line, before Chromium sees
    // any in-process switch changes. Put the profile on the real command line.
    command.AppendSwitchPath(switches::kUserDataDir, profile);
    if (!base::LaunchProcess(command, {}).IsValid())
      return fail(L"Could not start Zitter Dev.");
    return 0;
  }

  const auto requested = command.GetSwitchValuePath(switches::kUserDataDir)
                             .NormalizePathSeparators().StripTrailingSeparators();
  wchar_t effective[MAX_PATH];
  wchar_t invalid[MAX_PATH];
  if (!dev_data_root.IsParent(requested) || requested.ReferencesParent() ||
      !GetUserDataDirectoryThunk(effective, std::size(effective), invalid, std::size(invalid)) ||
      invalid[0] || !base::FilePath::CompareEqualIgnoreCase(requested.value(), effective))
    return fail(L"Zitter Dev needs a browsing profile inside its own data directory.");
  return std::nullopt;
}
#endif
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
  const auto main_entry = base::TimeTicks::Now();
  // chrome_elf (loaded with this executable) owns the install details that
  // //chrome reads; this module takes its copy, as chrome.dll does.
  install_static::InitializeFromPrimaryModule();
  base::win::SetAbortBehaviorForCrashReporting();
  base::CommandLine::Init(0, nullptr);
  if (!zitter::windows::kDevBuild) {
    if (const auto update = zitter::RunWindowsUpdateHelper()) return *update;
  }
#else
int main(int argc, const char** argv) {
  const auto main_entry = base::TimeTicks::Now();
  // Unlike Chrome's small macOS launcher, this executable links Chromium
  // directly. PartitionAlloc initializes before main; registering its early
  // delegating zone here would register the allocator twice in static builds.
  base::CommandLine::Init(argc, argv);
#endif
  zitter::MarkStartup("main_entry", main_entry);
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
#if BUILDFLAG(IS_WIN)
    // The taskbar groups Zitter's windows, pins and jump list under this ID;
    // the installer's shortcuts carry the same one (installer/win).
    ::SetCurrentProcessExplicitAppUserModelID(zitter::windows::kAppId);
    if (zitter::windows::kDevBuild) {
      if (const auto launch = PrepareDevProfile(*command)) return *launch;
      command->AppendSwitch(zitter::windows::kDevChannelSwitch);
      command->AppendSwitch("disable-zitter-updates");
    }
    // Extensions other apps register for Google Chrome stay Chrome's
    // (tools/install_chromium_overlay.py).
    command->AppendSwitch("zitter-ignore-chrome-registry-extensions");
    // Release Windows UI Automation providers when each native window closes.
    auto features = command->GetSwitchValueASCII(switches::kEnableFeatures);
    if (!features.empty()) features += ',';
    features += "UiaDisconnectRootProviders";
    command->AppendSwitchASCII(switches::kEnableFeatures, features);
#endif
    zitter::ConfigureSafeBrowsingKey();
    zitter::ConfigureNetworkPolicy(*command);
    if (!command->HasSwitch(switches::kUserDataDir))
      command->AppendSwitchPath(switches::kUserDataDir, DefaultDataDirectory());
#if BUILDFLAG(IS_WIN)
    // Hand external URLs and jump-list commands directly to the matching
    // workspace window, before Chromium's stock-window startup can consume them.
    if (zitter::prototype::ForwardCommandToRunningZitter(*command)) return 0;
#endif
#if !BUILDFLAG(IS_WIN)
    // Chrome initializes services/profiles, but only our Views window is opened.
    command->AppendSwitch(switches::kNoStartupWindow);
#else
    // A second zitter.exe forwards this command line (runtime.cc). Zitter keeps
    // several profiles, and without a named one Chrome would answer a launch
    // without a URL with its profile picker instead of a window to replace.
    if (!command->HasSwitch(switches::kProfileDirectory))
      command->AppendSwitchASCII(switches::kProfileDirectory, "Default");
#endif
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
  zitter::MarkStartup("content_main");
  return content::ContentMain(std::move(params));
}
