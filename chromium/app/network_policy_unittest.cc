#include "chromium/app/network_policy.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/win/access_token.h"
#include "base/win/security_descriptor.h"
#include "base/win/sid.h"
#endif

namespace zitter {
namespace {
TEST(ZitterNetworkPolicy, PreservesSafetyAndExistingFlags) {
  base::CommandLine command(base::CommandLine::NO_PROGRAM);
  command.AppendSwitchASCII("disable-features", "UserSelectedFeature");
  ConfigureNetworkPolicy(command);
  const auto disabled = command.GetSwitchValueASCII("disable-features");
  EXPECT_NE(disabled.find("UserSelectedFeature"), std::string::npos);
  EXPECT_NE(disabled.find("AutofillServerCommunication"), std::string::npos);
  EXPECT_NE(command.GetSwitchValueASCII("enable-features").find(
                "SkipListAccountsWithoutGaiaCookies"), std::string::npos);
  EXPECT_EQ(disabled.find("SafeBrowsing"), std::string::npos);
  EXPECT_FALSE(command.HasSwitch("disable-background-networking"));
  EXPECT_FALSE(command.HasSwitch("disable-component-update"));
  EXPECT_FALSE(command.HasSwitch("ignore-certificate-errors"));
  EXPECT_FALSE(command.HasSwitch("no-sandbox"));
#if BUILDFLAG(IS_WIN)
  EXPECT_NE(command.GetSwitchValueASCII("enable-features").find("NetworkServiceSandbox"), std::string::npos);
#endif
  const auto components = command.GetSwitchValueASCII("component-updater-disabled-ids");
  EXPECT_EQ(components.find("hfnkpimlhhgieaddgfemjhofmfblmnib"), std::string::npos);  // CRLSet
  EXPECT_EQ(components.find("efniojlnjndmcbiieegkicadnoecjjef"), std::string::npos);  // PKI
  EXPECT_EQ(components.find("khaoiebndkojlmppeemjhbpbandiljpe"), std::string::npos);  // file policies
}

TEST(ZitterNetworkPolicy, DisabledFeaturesWinOverExplicitEnable) {
  base::CommandLine command(base::CommandLine::NO_PROGRAM);
  command.AppendSwitchASCII("enable-features", "AimEnabled");
  ConfigureNetworkPolicy(command);
  base::FeatureList list;
  list.InitFromCommandLine(command.GetSwitchValueASCII("enable-features"),
                           command.GetSwitchValueASCII("disable-features"));
  EXPECT_TRUE(list.IsFeatureOverriddenFromCommandLine("AimEnabled", base::FeatureList::OVERRIDE_DISABLE_FEATURE));
}

TEST(ZitterSafeBrowsingKey, RejectsUnsafeFilesWithoutReturningTheirContents) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto file = directory.GetPath().AppendASCII("key");
  std::string key = "previous value";
  EXPECT_EQ(ReadSafeBrowsingKeyFile(file, &key), SafeBrowsingKeyFileStatus::kMissing);
  EXPECT_TRUE(key.empty());
  ASSERT_TRUE(base::WriteFile(file, "fixture-key-not-a-real-google-api-key\n"));
#if BUILDFLAG(IS_POSIX)
  ASSERT_TRUE(base::SetPosixFilePermissions(file, 0644));
  EXPECT_EQ(ReadSafeBrowsingKeyFile(file, &key), SafeBrowsingKeyFileStatus::kInsecurePermissions);
  EXPECT_TRUE(key.empty());
  ASSERT_TRUE(base::SetPosixFilePermissions(file, 0600));
#elif BUILDFLAG(IS_WIN)
  // Not inherited from the temp dir, whose ACL varies by machine: a file every
  // user can read, then one only the user and SYSTEM can (mode 0644, 0600).
  for (const bool shared : {true, false}) {
    auto descriptor = base::win::SecurityDescriptor::CreateWithEmptyDacl();
    descriptor.set_dacl_protected(true);
    const auto token = base::win::AccessToken::FromCurrentProcess();
    ASSERT_TRUE(token);
    ASSERT_TRUE(descriptor.SetDaclEntry(token->User(), base::win::SecurityAccessMode::kGrant, GENERIC_ALL, 0));
    ASSERT_TRUE(descriptor.SetDaclEntry(base::win::WellKnownSid::kLocalSystem, base::win::SecurityAccessMode::kGrant, GENERIC_ALL, 0));
    if (shared)
      ASSERT_TRUE(descriptor.SetDaclEntry(base::win::WellKnownSid::kWorld, base::win::SecurityAccessMode::kGrant, FILE_GENERIC_READ, 0));
    ASSERT_TRUE(descriptor.WriteToFile(file, DACL_SECURITY_INFORMATION));
    if (shared) {
      EXPECT_EQ(ReadSafeBrowsingKeyFile(file, &key), SafeBrowsingKeyFileStatus::kInsecurePermissions);
      EXPECT_TRUE(key.empty());
    }
  }
#endif
  EXPECT_EQ(ReadSafeBrowsingKeyFile(file, &key), SafeBrowsingKeyFileStatus::kReady);
  EXPECT_EQ(key, "fixture-key-not-a-real-google-api-key");
  ASSERT_TRUE(base::WriteFile(file, "fixture-key-with-an-embedded\nsecond-line"));
  EXPECT_EQ(ReadSafeBrowsingKeyFile(file, &key), SafeBrowsingKeyFileStatus::kInvalid);
  EXPECT_TRUE(key.empty());
}
}  // namespace
}  // namespace zitter
