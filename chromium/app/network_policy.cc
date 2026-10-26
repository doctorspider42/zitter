#include "chromium/app/network_policy.h"

#include <algorithm>
#include <string_view>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"

namespace zitter {
namespace {
constexpr std::string_view kDisabledFeatures[] = {
#define ZITTER_NETWORK_FEATURE(name, path, symbol) name,
#include "chromium/app/network_policy_features.inc"
#undef ZITTER_NETWORK_FEATURE
};
constexpr std::string_view kDisabledComponents[] = {
#define ZITTER_NETWORK_COMPONENT(id, purpose) id,
#include "chromium/app/network_policy_components.inc"
#undef ZITTER_NETWORK_COMPONENT
};
constexpr std::string_view kEnabledFeatures[] = {
    "SkipListAccountsWithoutGaiaCookies",
    "AvoidAutoTriggerListAccountsOnStale",
};

void AppendListSwitch(base::CommandLine& command, const char* name,
                      const auto& values) {
  std::string result = command.GetSwitchValueASCII(name);
  for (const auto value : values) {
    if (!result.empty()) result += ',';
    result += value;
  }
  command.AppendSwitchASCII(name, result);
}
}  // namespace

SafeBrowsingKeyFileStatus ReadSafeBrowsingKeyFile(
    const base::FilePath& path, std::string* key) {
  key->clear();
  if (!base::PathExists(path)) return SafeBrowsingKeyFileStatus::kMissing;
#if BUILDFLAG(IS_POSIX)
  int mode = 0;
  if (!base::GetPosixFilePermissions(path, &mode))
    return SafeBrowsingKeyFileStatus::kUnreadable;
  if (mode & (base::FILE_PERMISSION_GROUP_MASK | base::FILE_PERMISSION_OTHERS_MASK))
    return SafeBrowsingKeyFileStatus::kInsecurePermissions;
#endif
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents, 512))
    return SafeBrowsingKeyFileStatus::kUnreadable;
  base::TrimWhitespaceASCII(contents, base::TRIM_ALL, &contents);
  if (contents.size() < 20 || contents.size() > 256 ||
      !std::ranges::all_of(contents, [](char c) {
        return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_';
      })) return SafeBrowsingKeyFileStatus::kInvalid;
  *key = std::move(contents);
  return SafeBrowsingKeyFileStatus::kReady;
}

void ConfigureSafeBrowsingKey() {
  auto environment = base::Environment::Create();
  // Official builds may provide their own key through Chromium's supported
  // build-time configuration. Local developer keys never enter GN arguments,
  // process arguments or the repository. Upstream network diagnostics can
  // contain API keys; keep raw NetLog captures private.
  if (auto existing = environment->GetVar("GOOGLE_API_KEY");
      existing && !existing->empty()) return;
  base::FilePath path;
  if (auto configured = environment->GetVar("ZITTER_SAFE_BROWSING_API_KEY_FILE")) {
    path = base::FilePath::FromUTF8Unsafe(*configured);
    if (!path.IsAbsolute()) {
      LOG(ERROR) << "Safe Browsing key file must use an absolute path";
      return;
    }
  } else {
    base::FilePath home;
    if (!base::PathService::Get(base::DIR_HOME, &home)) return;
    path = home.Append(".config").Append("zitter").Append("safe-browsing-api-key");
  }
  std::string key;
  const auto status = ReadSafeBrowsingKeyFile(path, &key);
  if (status == SafeBrowsingKeyFileStatus::kReady) {
    if (!environment->SetVar("GOOGLE_API_KEY", key))
      LOG(ERROR) << "Safe Browsing API key could not be configured";
  } else if (status != SafeBrowsingKeyFileStatus::kMissing) {
    LOG(ERROR) << "Safe Browsing key file rejected (readability, format or permissions); "
                  "use a private file with mode 600";
  }
}

void ConfigureNetworkPolicy(base::CommandLine& command) {
  AppendListSwitch(command, "disable-features", kDisabledFeatures);
  // Keep ordinary Google website sign-in working without fetching the account
  // list for every empty browser profile or retrying stale cached lists.
  AppendListSwitch(command, "enable-features", kEnabledFeatures);
  AppendListSwitch(command, "component-updater-disabled-ids", kDisabledComponents);
  command.AppendSwitch("disable-sync");
  command.AppendSwitch("disable-domain-reliability");
  command.AppendSwitch("disable-variations-seed-fetch");
  // Unbranded developer builds otherwise apply Chromium's testing experiments,
  // including unsolicited model downloads unrelated to Zitter's feature set.
  command.AppendSwitch("disable-field-trial-config");
}
}  // namespace zitter
