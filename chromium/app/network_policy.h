#ifndef ZITTER_CHROMIUM_APP_NETWORK_POLICY_H_
#define ZITTER_CHROMIUM_APP_NETWORK_POLICY_H_

#include <string>

#include "base/files/file_path.h"

namespace base { class CommandLine; }

namespace zitter {

enum class SafeBrowsingKeyFileStatus {
  kReady, kMissing, kUnreadable, kInsecurePermissions, kInvalid
};

// Does not log or persist key contents. A key configures API access; it does
// not prove that a list update has succeeded.
SafeBrowsingKeyFileStatus ReadSafeBrowsingKeyFile(
    const base::FilePath& path, std::string* key);
// Reads Zitter's configured or default private key file without modifying the
// process environment. Call on a sequence that permits blocking file I/O.
std::string LoadSafeBrowsingKeyFile();
void ConfigureSafeBrowsingKey();

// Must run before ContentMain creates FeatureList or any profile services.
// Does not disable Safe Browsing, component updates, TLS checks or sandboxing.
void ConfigureNetworkPolicy(base::CommandLine& command);

}  // namespace zitter
#endif
