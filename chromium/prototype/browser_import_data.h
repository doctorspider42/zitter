#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_IMPORT_DATA_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_IMPORT_DATA_H_

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "chromium/prototype/browser_data.h"
#include "components/history/core/browser/history_types.h"
#include "net/cookies/canonical_cookie.h"

class Profile;
namespace zitter::prototype {
struct ImportedProfileData {
  std::string profile_id;
  history::URLRows history;
  std::vector<net::CanonicalCookie> cookies;
};
struct BrowsingImportData {
  std::vector<ImportedProfileData> profiles;
  size_t skipped = 0;
  std::string error;
};
using ImportedCookieKey = std::array<uint8_t, 16>;
using ImportCookieKeyReader = base::OnceCallback<std::optional<ImportedCookieKey>(ImportBrowser)>;
// Invoked only after an explicit import with cookies selected. Never creates or
// changes a source key. macOS handles authorization through its normal prompt.
std::optional<ImportedCookieKey> ReadImportedCookieKey(ImportBrowser browser);
std::shared_ptr<BrowsingImportData> ReadBrowsingImportData(
    const ArcImport& source, ImportCookieKeyReader key_reader = {});
struct BrowsingImportResult {
  size_t history = 0;
  size_t cookies = 0;
  size_t skipped = 0;
  bool failed = false;
};
// Writes through the destination's services, including its own cookie encryption.
void WriteBrowsingImportData(std::shared_ptr<BrowsingImportData> data,
    std::vector<Profile*> destinations,
    base::OnceCallback<void(BrowsingImportResult)> callback);
}  // namespace zitter::prototype
#endif
