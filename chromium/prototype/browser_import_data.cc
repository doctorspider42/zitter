#include "chromium/prototype/browser_import_data.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "build/build_config.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chromium/prototype/i18n.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/url_database.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/storage_partition.h"
#include "crypto/aes_cbc.h"
#include "crypto/hash.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_util.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "ui/gfx/codec/png_codec.h"

namespace zitter::prototype {
namespace {
// SQLite may write recovery/WAL bookkeeping only in this disposable snapshot.
// Check both files before/after copying so a browser writing concurrently is
// retried, rather than reading a torn database. No plaintext cookies go to disk.
bool Snapshot(const base::FilePath& path, base::ScopedTempDir& temp,
              sql::Database& db) {
  if (!temp.CreateUniqueTempDir()) return false;
  const auto wal = base::FilePath(path.value() + FILE_PATH_LITERAL("-wal"));
  const auto copy = temp.GetPath().AppendASCII("source.sqlite");
  for (int attempt = 0; attempt < 3; ++attempt) {
    base::File::Info before, after, wal_before, wal_after;
    if (!base::GetFileInfo(path, &before)) return false;
    const bool had_wal = base::GetFileInfo(wal, &wal_before);
    if (!base::CopyFile(path, copy)) return false;
    if (had_wal && !base::CopyFile(wal, base::FilePath(copy.value() + FILE_PATH_LITERAL("-wal")))) continue;
    if (!had_wal) base::DeleteFile(base::FilePath(copy.value() + FILE_PATH_LITERAL("-wal")));
    if (!base::GetFileInfo(path, &after)) continue;
    const bool has_wal = base::GetFileInfo(wal, &wal_after);
    if (before.size != after.size || before.last_modified != after.last_modified ||
        had_wal != has_wal || (has_wal && (wal_before.size != wal_after.size ||
          wal_before.last_modified != wal_after.last_modified))) continue;
    return db.Open(copy);
  }
  return false;
}
base::Time Timestamp(int64_t time) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(time));
}
bool ReadHistory(const base::FilePath& path, ImportedProfileData& data) {
  if (!base::PathExists(path)) return true;
  base::ScopedTempDir temp;
  sql::Database db("History");
  if (!Snapshot(path, temp, db)) return false;
  sql::Statement query(db.GetUniqueStatement(
      "SELECT url,title,visit_count,typed_count,last_visit_time,hidden FROM urls"));
  if (!query.is_valid()) return false;
  while (query.Step()) {
    if (data.history.size() >= 250000) return false;
    GURL url(query.ColumnString(0));
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) continue;
    history::URLRow row(url);
    row.set_title(query.ColumnString16(1));
    row.set_visit_count(std::max(0, query.ColumnInt(2)));
    row.set_typed_count(std::max(0, query.ColumnInt(3)));
    row.set_last_visit(Timestamp(query.ColumnInt64(4)));
    row.set_hidden(query.ColumnBool(5));
    data.history.push_back(std::move(row));
  }
  return query.Succeeded();
}
std::string Column(sql::Database& db, base::cstring_view name, const char* fallback) {
  return db.DoesColumnExist("cookies", name) ? std::string(name) : fallback;
}
bool ReadCookies(const base::FilePath& profile, ImportedProfileData& data,
                 BrowsingImportData& result, ImportBrowser browser,
                 ImportCookieKeyReader& key_reader,
                 std::optional<ImportedCookieKey>& key) {
  auto path = profile.AppendASCII("Network").AppendASCII("Cookies");
  if (!base::PathExists(path)) path = profile.AppendASCII("Cookies");
  if (!base::PathExists(path)) return true;
  base::ScopedTempDir temp;
  sql::Database db("Cookie");
  if (!Snapshot(path, temp, db)) return false;
  int version = 0;
  if (db.DoesTableExist("meta")) {
    sql::Statement meta(db.GetUniqueStatement("SELECT value FROM meta WHERE key='version'"));
    if (meta.Step()) version = meta.ColumnInt(0);
  }
  const auto sql = std::string("SELECT host_key,name,value,encrypted_value,path,creation_utc,expires_utc,") +
      "last_access_utc,is_secure,is_httponly,samesite,priority," +
      Column(db, "top_frame_site_key", "''") + "," +
      Column(db, "has_cross_site_ancestor", "1") + "," +
      Column(db, "source_scheme", "0") + "," +
      Column(db, "source_port", "-1") + "," +
      Column(db, "last_update_utc", "creation_utc") + "," +
      Column(db, "is_persistent", "1") + "," +
      Column(db, "source_type", "3") + " FROM cookies";
  sql::Statement query(db.GetUniqueStatement(sql));
  if (!query.is_valid()) return false;
  size_t rows = 0;
  while (query.Step()) {
    if (++rows > 100000) return false;
    const auto expiry = query.ColumnBool(17) ? Timestamp(query.ColumnInt64(6)) : base::Time();
    if (!expiry.is_null() && expiry <= base::Time::Now()) { ++result.skipped; continue; }
    const auto domain = query.ColumnString(0);
    auto value = query.ColumnString(2);
    const auto encrypted = query.ColumnBlob(3);
    if (!encrypted.empty()) {
      if (!value.empty() || encrypted.size() < 3 ||
          std::string_view(reinterpret_cast<const char*>(encrypted.data()), 3) != "v10") {
        result.error = base::UTF16ToUTF8(T(u"Nieobsługiwane szyfrowanie ciasteczek. Import został zatrzymany."));
        return false;
      }
      if (!key) {
        key = key_reader ? std::move(key_reader).Run(browser) : ReadImportedCookieKey(browser);
        if (!key) {
          result.error = base::UTF16ToUTF8(T(u"Nie uzyskano dostępu do klucza przeglądarki. Zezwól na dostęp w pęku kluczy i ponów import."));
          return false;
        }
      }
      constexpr std::array<uint8_t, 16> iv = {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
      auto plain = crypto::aes_cbc::Decrypt(*key, iv, encrypted.subspan(3u));
      const auto hash = crypto::hash::Sha256(domain);
      const size_t prefix = version >= 24 ? hash.size() : 0;
      if (!plain || plain->size() < prefix || (prefix && !std::equal(hash.begin(), hash.end(), plain->begin()))) {
        result.error = base::UTF16ToUTF8(T(u"Nie udało się odszyfrować ciasteczek. Import został zatrzymany."));
        return false;
      }
      value.assign(plain->begin() + prefix, plain->end());
      std::fill(plain->begin(), plain->end(), 0);
    }
    auto partition = net::CookiePartitionKey::FromStorage(query.ColumnString(12), query.ColumnBool(13));
    const int same_site = query.ColumnInt(10), priority = query.ColumnInt(11), scheme = query.ColumnInt(14);
    if (!partition.has_value() || same_site < -1 || same_site > 2 || priority < 0 || priority > 2 || scheme < 0 || scheme > 2) {
      ++result.skipped; continue;
    }
    const int source_type = query.ColumnInt(18);
    auto cookie = net::CanonicalCookie::FromStorage(query.ColumnString(1), std::move(value),
        domain, query.ColumnString(4), Timestamp(query.ColumnInt64(5)), expiry,
        Timestamp(query.ColumnInt64(7)), Timestamp(query.ColumnInt64(16)),
        query.ColumnBool(8), query.ColumnBool(9), static_cast<net::CookieSameSite>(same_site),
        static_cast<net::CookiePriority>(priority), *partition,
        static_cast<net::CookieSourceScheme>(scheme), query.ColumnInt(15),
        source_type >= 1 && source_type <= 3 ? static_cast<net::CookieSourceType>(source_type) : net::CookieSourceType::kOther,
        net::CanonicalCookieFromStorageCallSite::kSqlitePersistentCookieStore);
    if (cookie) data.cookies.push_back(*cookie);
    else ++result.skipped;
  }
  return query.Succeeded();
}

struct Writer : public std::enable_shared_from_this<Writer> {
  std::shared_ptr<BrowsingImportData> data;
  std::vector<std::unique_ptr<ScopedProfileKeepAlive>> profiles;
  base::OnceCallback<void(BrowsingImportResult)> callback;
  BrowsingImportResult result;
  size_t profile_index = 0, cookie_index = 0;
  std::set<net::CanonicalCookie> existing;
  network::mojom::CookieManager* manager() {
    return profiles[profile_index]->profile()->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
  }
  void NextProfile() {
    if (profile_index == profiles.size()) { std::move(callback).Run(result); return; }
    if (!profiles[profile_index]) { result.failed = true; ++profile_index; NextProfile(); return; }
    auto& rows = data->profiles[profile_index].history;
    auto* service = HistoryServiceFactory::GetForProfile(profiles[profile_index]->profile(), ServiceAccessType::EXPLICIT_ACCESS);
    if (rows.empty()) { Cookies(); return; }
    if (!service) { result.failed = true; Cookies(); return; }
    service->ScheduleDBTaskForUI(base::BindOnce(
        [](history::URLRows rows, base::OnceCallback<void(size_t, size_t, bool)> done,
           history::HistoryBackend* backend, history::URLDatabase* db) {
          if (!db) { std::move(done).Run(0, 0, true); return; }
          history::URLRows filtered;
          size_t skipped = 0;
          std::map<GURL, history::URLRow> latest;
          for (const auto& row : rows) {
            auto [it, inserted] = latest.emplace(row.url(), row);
            if (!inserted) {
              ++skipped;
              if (it->second.last_visit() < row.last_visit()) it->second = row;
            }
          }
          for (const auto& [url, row] : latest) {
            history::URLRow old;
            if (row.last_visit() < base::Time::Now() - base::Days(90) ||
                row.last_visit() > base::Time::Now() || db->GetRowForURL(url, &old)) {
              ++skipped; continue;
            }
            filtered.push_back(row);
          }
          backend->AddPagesWithDetails(filtered, history::SOURCE_OS_MIGRATION_IMPORTED);
          std::move(done).Run(filtered.size(), skipped, false);
        }, std::move(rows), base::BindPostTaskToCurrentDefault(base::BindOnce(
            [](std::shared_ptr<Writer> self, size_t count, size_t skipped, bool failed) {
              self->result.history += count; self->result.skipped += skipped;
              self->result.failed |= failed; self->Cookies();
            }, shared_from_this()))));
  }
  void Cookies() {
    cookie_index = 0;
    if (data->profiles[profile_index].cookies.empty()) { ++profile_index; NextProfile(); return; }
    manager()->GetAllCookies(base::BindOnce([](std::shared_ptr<Writer> self, const std::vector<net::CanonicalCookie>& cookies) {
      self->existing = {cookies.begin(), cookies.end()};
      self->NextCookie();
    }, shared_from_this()));
  }
  void NextCookie() {
    auto& cookies = data->profiles[profile_index].cookies;
    while (cookie_index < cookies.size()) {
      const auto& cookie = cookies[cookie_index++];
      // Sessions already present when this profile's copy begins take precedence.
      if (existing.contains(cookie)) {
        ++result.skipped; continue;
      }
      manager()->SetCanonicalCookie(cookie, net::cookie_util::SimulatedCookieSource(cookie, cookie.SourceScheme() == net::CookieSourceScheme::kNonSecure ? "http" : "https"),
          net::CookieOptions::MakeAllInclusive(), base::BindOnce(
              [](std::shared_ptr<Writer> self, net::CanonicalCookie cookie, net::CookieAccessResult access) {
                if (access.status.IsInclude()) { ++self->result.cookies; self->existing.insert(cookie); }
                else { ++self->result.skipped; }
                self->NextCookie();
              }, shared_from_this(), cookie));
      return;
    }
    manager()->FlushCookieStore(base::BindOnce([](std::shared_ptr<Writer> self) {
      self->existing.clear(); ++self->profile_index; self->NextProfile();
    }, shared_from_this()));
  }
};
}  // namespace

#if !BUILDFLAG(IS_MAC)
std::optional<ImportedCookieKey> ReadImportedCookieKey(ImportBrowser) { return std::nullopt; }
#endif
std::vector<ImportedFavicon> ReadImportedFavicons(
    const std::vector<ImportSourceProfile>& sources,
    const ImportedFaviconTargets& targets) {
  std::vector<ImportedFavicon> result;
  std::set<std::pair<std::string, GURL>> seen;
  for (const auto& source : sources) {
    const auto pages = targets.find(source.destination_id);
    if (pages == targets.end()) continue;
    base::ScopedTempDir temp;
    sql::Database db("Thumbnail");
    if (!Snapshot(source.directory.AppendASCII("Favicons"), temp, db)) continue;
    sql::Statement query(db.GetUniqueStatement(
        "SELECT f.url, b.image_data FROM icon_mapping m "
        "JOIN favicons f ON f.id=m.icon_id JOIN favicon_bitmaps b ON b.icon_id=f.id "
        "WHERE m.page_url=? AND f.icon_type=1 AND length(b.image_data)<=524288 "
        "ORDER BY b.width=0, abs(b.width-32), b.last_updated DESC LIMIT 4"));
    if (!query.is_valid()) continue;
    for (const auto& page : pages->second) {
      if (seen.contains({source.destination_id, page})) continue;
      query.Reset(/*clear_bound_vars=*/true);
      query.BindString(0, page.spec());
      while (query.Step()) {
        GURL icon(query.ColumnString(0));
        const auto bytes = query.ColumnBlob(1);
        // Arc can record zero dimensions for valid PNGs. Bound the actual
        // IHDR dimensions before decoding rather than trusting the SQLite row.
        if (!icon.SchemeIsHTTPOrHTTPS() || icon.has_username() || icon.has_password() ||
            bytes.size() < 24 || base::U32FromBigEndian(bytes.subspan<16, 4>()) > 256 ||
            base::U32FromBigEndian(bytes.subspan<20, 4>()) > 256) continue;
        auto bitmap = gfx::PNGCodec::Decode(bytes);
        if (bitmap.drawsNothing()) continue;
        seen.emplace(source.destination_id, page);
        result.push_back({source.destination_id, page, std::move(icon), std::move(bitmap)});
        break;
      }
    }
  }
  return result;
}

std::shared_ptr<BrowsingImportData> ReadBrowsingImportData(const ArcImport& source, ImportCookieKeyReader key_reader) {
  auto result = std::make_shared<BrowsingImportData>();
  std::optional<ImportedCookieKey> key;
  std::map<std::string, size_t> destinations;
  for (const auto& profile : source.sources) {
    const auto [it, inserted] = destinations.emplace(profile.destination_id, result->profiles.size());
    if (inserted) result->profiles.push_back({profile.destination_id, {}, {}});
    auto& data = result->profiles[it->second];
    if ((source.options.history && !ReadHistory(profile.directory.AppendASCII("History"), data)) ||
        (source.options.cookies && !ReadCookies(profile.directory, data, *result, source.options.browser, key_reader, key))) {
      if (result->error.empty()) result->error = base::UTF16ToUTF8(T(u"Nie udało się odczytać danych. Zamknij przeglądarkę źródłową i spróbuj ponownie."));
      result->profiles.clear();
      break;
    }
  }
  if (key) std::fill(key->begin(), key->end(), 0);
  return result;
}
void WriteBrowsingImportData(std::shared_ptr<BrowsingImportData> data,
    std::vector<Profile*> destinations, base::OnceCallback<void(BrowsingImportResult)> callback) {
  auto writer = std::make_shared<Writer>();
  writer->data = std::move(data);
  writer->result.skipped = writer->data->skipped;
  writer->callback = std::move(callback);
  for (auto* profile : destinations)
    writer->profiles.push_back(profile ? ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow) : nullptr);
  CHECK_EQ(writer->profiles.size(), writer->data->profiles.size());
  writer->NextProfile();
}
}  // namespace zitter::prototype
