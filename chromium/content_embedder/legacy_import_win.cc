// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

// Native port of the windows-dpapi path of import_workspace_profiles() in
// tools/profile_migration_trial.py. Keep the two in step: every check below
// mirrors one in the Python importer, and the tests in
// tests/test_profile_migration_trial.py describe the expected results.

#include "legacy_import_win.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/hash/sha1.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/cstring_view.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "url/gurl.h"

namespace zitter::content_embedder {
namespace {

// Limits shared with tools/profile_migration_trial.py.
constexpr size_t kMaxJsonBytes = 16 * 1024 * 1024;
constexpr size_t kMaxAssistantSessionsBytes = 64 * 1024 * 1024;
constexpr size_t kMaxAssistantConversations = 200;
constexpr size_t kMaxTimelineBytes = 64 * 1024 * 1024;
constexpr size_t kMaxRssBytes = 64 * 1024 * 1024;
constexpr size_t kMaxFilterListBytes = 16 * 1024 * 1024;
constexpr size_t kMaxPageZoomBytes = 4 * 1024 * 1024;
constexpr size_t kMaxAdblockBytes = 8 * 1024 * 1024;
constexpr size_t kMaxLocalStateBytes = 64 * 1024;
constexpr size_t kMaxSessionTabs = 500;
constexpr size_t kMaxTabUrlBytes = 32768;
constexpr size_t kMaxTabTitleUnits = 8192;
constexpr size_t kMaxSplitPanes = 4;
constexpr size_t kMaxSiteRules = 256;
constexpr size_t kMaxContentProfiles = 32;
constexpr size_t kMaxContentWorkspaces = 64;
constexpr int kHistorySchemaVersion = 70;
constexpr int kFaviconSchemaVersion = 9;
constexpr double kMaxTimelineMilliseconds = 32503680000000.0;
constexpr double kMaxAssistantMilliseconds = 8.64e15;

// Legacy (Chrome-based) file names.
constexpr char kWorkspaceConfig[] = "ZitterWorkspaces.json";
constexpr char kLegacyLocalState[] = "Local State";
constexpr char kLegacySession[] = "ZitterSession.json";
constexpr char kLegacyTimeline[] = "ZitterTimeline.jsonl";
constexpr char kLegacyAppearance[] = "ZitterAppearance.json";
constexpr char kBrowserTools[] = "ZitterTools.json";
constexpr char kAssistantSessions[] = "ZitterAssistantSessions.json";
constexpr char kRssState[] = "ZitterRSS.json";
constexpr char kLocalStorage[] = "Local Storage";
constexpr char kFilterLists[] = "Zitter Filter Lists";

// Content (windows-dpapi layout) file names.
constexpr char kWorkspaceSession[] = "ContentWorkspaceSession.json";
constexpr char kWorkspaceTimeline[] = "ContentWorkspaceSession.timeline.jsonl";
constexpr char kWorkspaceSettings[] = "WorkspaceSettings.json";
constexpr char kWindowsLocalState[] = "ContentWorkspaceLocalState.json";
constexpr char kPreparedWorkspaceMarker[] = "ContentWorkspaceLayout";
constexpr char kPreparedWorkspaceLayout[] =
    "zitter-content-workspace-v1 windows-dpapi\n";
constexpr char kPasswordPrefs[] = "Content Password Preferences";
constexpr char kAdblockPrefs[] = "Content Ad Blocking Preferences";
constexpr char kPageZoomPrefs[] = "Content Page Zoom Preferences";
constexpr char kSitePermissions[] = "Content Site Permissions";
constexpr char kPasswordSavingPref[] = "credentials_enable_service";
constexpr char kImportedDownloadMarker[] = "zitter-imported-download-v1";
constexpr char kProfilePrefix[] = "workspace-";

constexpr std::array<std::string_view, 2> kBuiltinRoles = {"work", "personal"};
constexpr std::array<std::string_view, 2> kBuiltinNames = {"Work", "Personal"};
constexpr std::array<const char*, 4> kSqliteSuffixes = {"", "-journal", "-wal",
                                                        "-shm"};
constexpr std::array<const char*, 4> kRootLocks = {
    "SingletonLock", "SingletonCookie", "SingletonSocket", "lockfile"};
constexpr std::array<std::string_view, 9> kSiteRuleMetadata = {
    "setting",
    "last_modified",
    "last_used",
    "last_visit",
    "expiration",
    "lifetime",
    "model",
    "autorevocation_bypassed_by_user",
    "decided_by_related_website_sets",
};

bool Refuse(std::string_view reason) {
  LOG(ERROR) << "Legacy Zitter import refused: " << reason;
  return false;
}

template <typename T>
std::optional<T> RefuseValue(std::string_view reason) {
  Refuse(reason);
  return std::nullopt;
}

// Ordered like the Python dictionaries, which keep insertion order.
using OrderedMap = std::vector<std::pair<std::string, std::string>>;

const std::string* Lookup(const OrderedMap& map, std::string_view key) {
  const auto it = std::ranges::find_if(
      map, [key](const auto& entry) { return entry.first == key; });
  return it == map.end() ? nullptr : &it->second;
}

bool ContainsValue(const OrderedMap& map, std::string_view value) {
  return std::ranges::any_of(
      map, [value](const auto& entry) { return entry.second == value; });
}

// Strings ---------------------------------------------------------------------

bool IsVisibleAscii(std::string_view text) {
  return std::ranges::all_of(text, [](char c) { return c >= '!' && c <= '~'; });
}

bool IsDigits(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) {
    return base::IsAsciiDigit(c);
  });
}

bool IsLowerHex(std::string_view text) {
  return std::ranges::all_of(text, [](char c) {
    return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'f');
  });
}

bool IsContinuationByte(char c) {
  return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

// Python's len(str): code points of valid UTF-8.
size_t CodePoints(std::string_view text) {
  return static_cast<size_t>(std::ranges::count_if(
      text, [](char c) { return !IsContinuationByte(c); }));
}

// Python's text[:limit].
std::string_view PrefixCodePoints(std::string_view text, size_t limit) {
  size_t seen = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (IsContinuationByte(text[i]))
      continue;
    if (seen == limit)
      return text.substr(0, i);
    ++seen;
  }
  return text;
}

// Python's len(text.encode("utf-16-le")) // 2.
size_t Utf16Units(std::string_view text) {
  return base::UTF8ToUTF16(text).size();
}

bool IsCatalogueName(std::string_view name) {
  return !name.empty() && name.size() <= 256 &&
         name.find_first_of("\r\n\t") == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}

std::optional<std::string_view> BuiltinName(std::string_view mapped) {
  for (size_t i = 0; i < kBuiltinRoles.size(); ++i) {
    if (kBuiltinRoles[i] == mapped)
      return kBuiltinNames[i];
  }
  return std::nullopt;
}

bool IsBuiltinRole(std::string_view id) {
  return std::ranges::contains(kBuiltinRoles, id);
}

// UUIDs -----------------------------------------------------------------------

std::string FormatUuid(std::string_view hex) {
  return base::StrCat({hex.substr(0, 8), "-", hex.substr(8, 4), "-",
                       hex.substr(12, 4), "-", hex.substr(16, 4), "-",
                       hex.substr(20, 12)});
}

bool IsUuidText(std::string_view text, bool lowercase_only) {
  if (text.size() != 36)
    return false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else if (!base::IsHexDigit(c) || (lowercase_only && base::IsAsciiUpper(c))) {
      return false;
    }
  }
  return true;
}

// Python's str(uuid.UUID(text)) for hexadecimal input, including braces and
// the urn:uuid: prefix that uuid.UUID() strips.
std::optional<std::string> ParsePythonUuid(std::string_view text) {
  std::string value(text);
  base::ReplaceSubstringsAfterOffset(&value, 0, "urn:", "");
  base::ReplaceSubstringsAfterOffset(&value, 0, "uuid:", "");
  const size_t begin = value.find_first_not_of("{}");
  if (begin == std::string::npos)
    return std::nullopt;
  value = value.substr(begin, value.find_last_not_of("{}") - begin + 1);
  std::erase(value, '-');
  if (value.size() != 32 ||
      !std::ranges::all_of(value, [](char c) { return base::IsHexDigit(c); })) {
    return std::nullopt;
  }
  return FormatUuid(base::ToLowerASCII(value));
}

// Python's uuid.uuid5(uuid.NAMESPACE_URL, name).
std::string NameBasedUuid(std::string_view name) {
  static constexpr std::array<uint8_t, 16> kNamespaceUrl = {
      0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
      0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
  std::vector<uint8_t> input(kNamespaceUrl.begin(), kNamespaceUrl.end());
  input.insert(input.end(), name.begin(), name.end());
  const base::SHA1Digest digest = base::SHA1Hash(input);
  std::array<uint8_t, 16> bytes;
  for (size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = digest[i];
  bytes[6] = (bytes[6] & 0x0f) | 0x50;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  return FormatUuid(base::HexEncodeLower(bytes));
}

// Files -----------------------------------------------------------------------

enum class PathKind { kMissing, kFile, kDirectory, kUnsafe };

// Like Python's lstat() checks: any reparse point (symbolic link, junction,
// mount point or placeholder) or device is unsafe.
PathKind Inspect(const base::FilePath& path) {
  const DWORD attributes = ::GetFileAttributesW(path.value().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
               ? PathKind::kMissing
               : PathKind::kUnsafe;
  }
  if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
    return PathKind::kUnsafe;
  return attributes & FILE_ATTRIBUTE_DIRECTORY ? PathKind::kDirectory
                                               : PathKind::kFile;
}

std::string Describe(const base::FilePath& path) {
  return path.AsUTF8Unsafe();
}

// Python's read_verified_file(). Leaves `data` empty when the file is absent.
bool ReadLegacyFile(const base::FilePath& path,
                    size_t max_bytes,
                    std::optional<std::string>& data,
                    base::File::Info* info = nullptr) {
  data.reset();
  switch (Inspect(path)) {
    case PathKind::kMissing:
      return true;
    case PathKind::kFile:
      break;
    default:
      return Refuse(base::StrCat({"Invalid legacy file: ", Describe(path)}));
  }
  base::File::Info stat;
  std::string contents;
  if (!base::GetFileInfo(path, &stat) || stat.is_directory || stat.size < 0 ||
      static_cast<uint64_t>(stat.size) > max_bytes ||
      !base::ReadFileToStringWithMaxSize(path, &contents, max_bytes) ||
      contents.size() != static_cast<uint64_t>(stat.size)) {
    return Refuse(base::StrCat(
        {"Invalid, oversized or changing legacy file: ", Describe(path)}));
  }
  if (info)
    *info = stat;
  data = std::move(contents);
  return true;
}

// Copies one regular file and checks that the source did not change while it
// was copied. The Python importer compares against its snapshot instead.
bool CopyVerifiedFile(const base::FilePath& source,
                      const base::FilePath& target) {
  base::File::Info before;
  base::File::Info copied;
  base::File::Info after;
  if (Inspect(source) != PathKind::kFile ||
      Inspect(target) != PathKind::kMissing ||
      !base::GetFileInfo(source, &before) || !base::CopyFile(source, target) ||
      Inspect(target) != PathKind::kFile ||
      !base::GetFileInfo(target, &copied) ||
      !base::GetFileInfo(source, &after) || copied.size != before.size ||
      after.size != before.size ||
      after.last_modified != before.last_modified) {
    return Refuse(base::StrCat(
        {"The legacy file changed or could not be copied: ", Describe(source)}));
  }
  return true;
}

// Python's copy_verified(): copies a tree and refuses links and special
// files anywhere inside it.
bool CopyTree(const base::FilePath& source,
              const base::FilePath& target,
              int depth = 0) {
  if (depth > 64 || Inspect(source) != PathKind::kDirectory ||
      Inspect(target) != PathKind::kMissing || !base::CreateDirectory(target)) {
    return Refuse(base::StrCat({"Invalid legacy directory: ", Describe(source)}));
  }
  base::FileEnumerator entries(
      source, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES,
      FILE_PATH_LITERAL("*"), base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (auto path = entries.Next(); !path.empty(); path = entries.Next()) {
    const auto destination = target.Append(path.BaseName());
    switch (Inspect(path)) {
      case PathKind::kFile:
        if (!CopyVerifiedFile(path, destination))
          return false;
        break;
      case PathKind::kDirectory:
        if (!CopyTree(path, destination, depth + 1))
          return false;
        break;
      default:
        return Refuse(base::StrCat(
            {"The legacy copy encountered a link or special file: ",
             Describe(path)}));
    }
  }
  if (entries.GetError() != base::File::FILE_OK)
    return Refuse(base::StrCat({"Could not list ", Describe(source)}));
  return true;
}

bool WriteText(const base::FilePath& path, std::string_view contents) {
  if (Inspect(path) != PathKind::kMissing || !base::WriteFile(path, contents))
    return Refuse(base::StrCat({"Could not write ", Describe(path)}));
  return true;
}

bool IsSameOrAncestor(const base::FilePath& ancestor,
                      const base::FilePath& path) {
  const auto expected = ancestor.StripTrailingSeparators();
  for (auto current = path.StripTrailingSeparators();;
       current = current.DirName()) {
    if (base::FilePath::CompareEqualIgnoreCase(current.value(),
                                               expected.value())) {
      return true;
    }
    if (current.DirName() == current)
      return false;
  }
}

// JSON ------------------------------------------------------------------------

size_t CountObjectMembers(const base::Value& value) {
  size_t count = 0;
  if (const auto* dict = value.GetIfDict()) {
    count += dict->size();
    for (const auto [key, child] : *dict)
      count += CountObjectMembers(child);
  } else if (const auto* list = value.GetIfList()) {
    for (const auto& child : *list)
      count += CountObjectMembers(child);
  }
  return count;
}

// Outside strings, a colon in RFC JSON only separates an object key from its
// value, so this counts the members written in the text.
size_t CountKeySeparators(std::string_view json) {
  size_t count = 0;
  bool in_string = false;
  bool escaped = false;
  for (const char c : json) {
    if (in_string) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        in_string = false;
    } else if (c == '"') {
      in_string = true;
    } else if (c == ':') {
      ++count;
    }
  }
  return count;
}

// Parses RFC JSON and, like the importer's object_pairs_hook, refuses
// duplicate keys: the parsed tree then holds fewer members than the text.
std::optional<base::Value> ParseJson(std::string_view json) {
  auto value = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || CountObjectMembers(*value) != CountKeySeparators(json))
    return std::nullopt;
  return value;
}

// Python's read_verified_json(). Leaves `result` empty when the file is absent.
bool ReadJsonDict(const base::FilePath& path,
                  std::string_view label,
                  std::optional<base::DictValue>& result) {
  result.reset();
  std::optional<std::string> data;
  if (!ReadLegacyFile(path, kMaxJsonBytes, data))
    return false;
  if (!data)
    return true;
  auto value = ParseJson(*data);
  if (!value || !value->is_dict())
    return Refuse(base::StrCat({"Invalid legacy ", label, " object"}));
  result = std::move(*value).TakeDict();
  return true;
}

bool IsNumber(const base::Value& value) {
  return value.is_int() || value.is_double();
}

// Python's truthiness of a JSON value.
bool IsTruthy(const base::Value* value) {
  if (!value)
    return false;
  if (value->is_bool())
    return value->GetBool();
  if (value->is_int())
    return value->GetInt() != 0;
  if (value->is_double())
    return value->GetDouble() != 0;
  if (value->is_string())
    return !value->GetString().empty();
  if (value->is_dict())
    return !value->GetDict().empty();
  if (value->is_list())
    return !value->GetList().empty();
  return false;
}

template <typename Keys>
bool KeysWithin(const base::DictValue& dict, const Keys& allowed) {
  for (const auto [key, value] : dict) {
    if (!std::ranges::contains(allowed, std::string_view(key)))
      return false;
  }
  return true;
}

bool HasOnlyKeys(const base::DictValue& dict,
                 std::initializer_list<std::string_view> allowed) {
  return KeysWithin(dict, allowed);
}

bool HasExactKeys(const base::DictValue& dict,
                  std::initializer_list<std::string_view> keys) {
  return dict.size() == keys.size() && HasOnlyKeys(dict, keys);
}

// Python's object_at(): an absent key reads as an empty object, reported here
// as a null `child`.
bool ObjectAt(const base::DictValue* parent,
              std::string_view key,
              const base::DictValue*& child) {
  child = nullptr;
  const base::Value* value = parent ? parent->Find(key) : nullptr;
  if (!value)
    return true;
  child = value->GetIfDict();
  return child != nullptr;
}

std::optional<std::string> Serialize(const base::DictValue& value) {
  return base::WriteJson(value);
}

bool WriteJsonFile(const base::FilePath& path,
                   const base::DictValue& value,
                   size_t max_bytes,
                   bool newline) {
  auto json = Serialize(value);
  if (!json)
    return Refuse(base::StrCat({"Could not serialize ", Describe(path)}));
  if (newline)
    json->push_back('\n');
  if (json->size() > max_bytes)
    return Refuse(base::StrCat({"Converted data is too large: ", Describe(path)}));
  return WriteText(path, *json);
}

// Python's chromium_timestamp().
std::optional<std::string> ChromiumTimestamp(const base::Value* value) {
  if (!value || !value->is_string())
    return std::nullopt;
  std::string_view text = value->GetString();
  bool negative = false;
  if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.size() > 19 || !IsDigits(text))
    return std::nullopt;
  uint64_t magnitude = 0;
  for (const char c : text)
    magnitude = magnitude * 10 + static_cast<uint64_t>(c - '0');
  constexpr uint64_t kMaxPositive =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  if (magnitude > kMaxPositive + (negative ? 1 : 0))
    return std::nullopt;
  if (magnitude == 0)
    return "0";
  return base::StrCat({negative ? "-" : "", base::NumberToString(magnitude)});
}

// Hosts and URLs --------------------------------------------------------------

// Python's ipaddress.IPv4Address(): four decimal octets without leading zeros.
bool IsStrictIpv4(std::string_view host) {
  const auto octets = base::SplitStringPiece(host, ".", base::KEEP_WHITESPACE,
                                             base::SPLIT_WANT_ALL);
  if (octets.size() != 4)
    return false;
  for (const auto octet : octets) {
    int value = 0;
    if (!IsDigits(octet) || octet.size() > 3 ||
        (octet.size() > 1 && octet.front() == '0') ||
        !base::StringToInt(octet, &value) || value > 255) {
      return false;
    }
  }
  return true;
}

std::optional<net::IPAddress> ParseIpv6(std::string_view text) {
  net::IPAddress address;
  if (text.find(':') == std::string_view::npos ||
      text.find('%') != std::string_view::npos ||
      !address.AssignFromIPLiteral(text) || !address.IsIPv6()) {
    return std::nullopt;
  }
  return address;
}

bool IsPortInRange(std::string_view port) {
  if (!IsDigits(port))
    return false;
  const size_t first = port.find_first_not_of('0');
  if (first == std::string_view::npos)
    return true;
  int value = 0;
  return port.size() - first <= 5 &&
         base::StringToInt(port.substr(first), &value) && value <= 65535;
}

// Python's can_restore_session_url(), including the urlsplit() parsing it
// relies on.
bool CanRestoreSessionUrl(std::string_view url) {
  if (url.empty() || url == "zitter://rss/")
    return true;
  if (!std::ranges::all_of(
          url, [](char c) { return c >= '!' && c <= '~' && c != '\\'; })) {
    return false;
  }
  const size_t colon = url.find(':');
  if (colon == std::string_view::npos || colon == 0 ||
      !base::IsAsciiAlpha(url.front())) {
    return false;
  }
  const auto scheme_text = url.substr(0, colon);
  if (!std::ranges::all_of(scheme_text, [](char c) {
        return base::IsAsciiAlphaNumeric(c) || c == '+' || c == '-' || c == '.';
      })) {
    return false;
  }
  const std::string scheme = base::ToLowerASCII(scheme_text);
  if (scheme != "http" && scheme != "https")
    return false;
  auto rest = url.substr(colon + 1);
  if (!rest.starts_with("//"))
    return false;
  rest.remove_prefix(2);
  const auto netloc = rest.substr(0, rest.find_first_of("/?#"));
  const bool open = netloc.find('[') != std::string_view::npos;
  const bool close = netloc.find(']') != std::string_view::npos;
  if (open != close || netloc.find('@') != std::string_view::npos)
    return false;

  std::string_view hostname;
  std::string_view port;
  if (open) {
    const auto bracketed = netloc.substr(netloc.find('[') + 1);
    const size_t end = bracketed.find(']');
    hostname = bracketed.substr(0, end);
    if (end != std::string_view::npos) {
      const auto after = bracketed.substr(end + 1);
      const size_t separator = after.find(':');
      if (separator != std::string_view::npos)
        port = after.substr(separator + 1);
    }
  } else {
    const size_t separator = netloc.find(':');
    hostname = netloc.substr(0, separator);
    if (separator != std::string_view::npos)
      port = netloc.substr(separator + 1);
  }
  if (hostname.empty() || (!port.empty() && !IsPortInRange(port)))
    return false;

  if (netloc.starts_with('[')) {
    // \[[a-fA-F0-9:.]+\](?::[0-9]+)?
    const size_t end = netloc.find(']');
    const auto inside = netloc.substr(1, end - 1);
    const auto suffix = netloc.substr(end + 1);
    if (inside.empty() || !std::ranges::all_of(inside, [](char c) {
          return base::IsHexDigit(c) || c == ':' || c == '.';
        })) {
      return false;
    }
    if (!suffix.empty() &&
        (suffix.front() != ':' || !IsDigits(suffix.substr(1)))) {
      return false;
    }
    return ParseIpv6(base::ToLowerASCII(hostname)).has_value();
  }
  // [a-zA-Z0-9._-]+(?::[0-9]+)?
  const size_t separator = netloc.find(':');
  const auto host = netloc.substr(0, separator);
  if (host.empty() || !std::ranges::all_of(host, [](char c) {
        return base::IsAsciiAlphaNumeric(c) || c == '.' || c == '_' || c == '-';
      })) {
    return false;
  }
  return separator == std::string_view::npos ||
         IsDigits(netloc.substr(separator + 1));
}

bool IsDnsLabel(std::string_view label) {
  // [a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?
  const auto alnum = [](char c) {
    return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'z');
  };
  return !label.empty() && label.size() <= 63 && alnum(label.front()) &&
         alnum(label.back()) && std::ranges::all_of(label, [&](char c) {
           return alnum(c) || c == '-';
         });
}

// Python's exact_site_origin().
std::optional<std::string> ExactSiteOrigin(std::string_view pattern,
                                           std::string_view permission,
                                           bool require_secure) {
  const auto fail = [&](std::string_view problem) {
    return RefuseValue<std::string>(base::StrCat(
        {problem, " ", permission, " rule: ", pattern}));
  };
  // (https?)://(\[[0-9a-f:]+\]|[a-z0-9.-]+):([1-9][0-9]{0,4}),\*
  std::string_view scheme;
  if (pattern.starts_with("https://"))
    scheme = "https";
  else if (pattern.starts_with("http://"))
    scheme = "http";
  else
    return fail("Unsupported scope of");
  auto rest = pattern.substr(scheme.size() + 3);
  if (!rest.ends_with(",*"))
    return fail("Unsupported scope of");
  rest.remove_suffix(2);
  std::string_view host;
  if (rest.starts_with('[')) {
    const size_t end = rest.find(']');
    if (end == std::string_view::npos || end == 1 ||
        !std::ranges::all_of(rest.substr(1, end - 1), [](char c) {
          return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'f') || c == ':';
        })) {
      return fail("Unsupported scope of");
    }
    host = rest.substr(0, end + 1);
  } else {
    host = rest.substr(0, rest.find(':'));
    if (host.empty() || !std::ranges::all_of(host, [](char c) {
          return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'z') || c == '.' ||
                 c == '-';
        })) {
      return fail("Unsupported scope of");
    }
  }
  rest.remove_prefix(host.size());
  if (!rest.starts_with(':'))
    return fail("Unsupported scope of");
  const auto port = rest.substr(1);
  int port_value = 0;
  if (!IsDigits(port) || port.size() > 5 || port.front() == '0')
    return fail("Unsupported scope of");
  if (!base::StringToInt(port, &port_value) || port_value > 65535)
    return fail("Invalid port in");

  std::string_view address_text = host;
  while (address_text.starts_with('[') || address_text.starts_with(']'))
    address_text.remove_prefix(1);
  while (address_text.ends_with('[') || address_text.ends_with(']'))
    address_text.remove_suffix(1);
  bool is_address = false;
  bool loopback = false;
  if (IsStrictIpv4(address_text)) {
    // A strict dotted quad is already canonical.
    is_address = true;
    loopback = address_text.starts_with("127.");
  } else if (const auto address = ParseIpv6(address_text)) {
    is_address = true;
    loopback = address->IsLoopback();
    if (base::StrCat({"[", address->ToString(), "]"}) != host)
      return fail("Noncanonical host in");
  }
  if (!is_address) {
    const auto labels = base::SplitStringPiece(host, ".", base::KEEP_WHITESPACE,
                                               base::SPLIT_WANT_ALL);
    const auto last = labels.back();
    if (host.size() > 253 || !std::ranges::all_of(labels, IsDnsLabel) ||
        IsDigits(last) || (last.starts_with("0x") && IsLowerHex(last.substr(2)))) {
      return fail("Invalid host in");
    }
    if (std::ranges::any_of(labels, [](std::string_view label) {
          return label.starts_with("xn--");
        })) {
      // Python decodes the labels with its IDNA codec. URL canonicalization
      // rejects the same malformed Punycode labels.
      const GURL url(base::StrCat({"https://", host, "/"}));
      if (!url.is_valid() || url.host() != host)
        return fail("Invalid IDN in");
    }
  }
  if (require_secure && scheme == "http" && !loopback && host != "localhost" &&
      !host.ends_with(".localhost")) {
    return fail("Insecure");
  }
  const std::string_view default_port = scheme == "https" ? "443" : "80";
  return base::StrCat({scheme, "://", host, port == default_port ? "" : ":",
                       port == default_port ? "" : port});
}

// Python's link-rule host checks in read_workspace_session(), after
// can_restore_session_url() has accepted https://host/.
bool IsPlainLinkHost(std::string_view host) {
  if (host.find_first_of("/?#") != std::string_view::npos)
    return false;
  if (host.starts_with('[')) {
    if (!host.ends_with(']'))
      return false;
    const auto address =
        ParseIpv6(base::ToLowerASCII(host.substr(1, host.size() - 2)));
    return address && base::StrCat({"[", address->ToString(), "]"}) == host;
  }
  if (host.find(':') != std::string_view::npos)
    return false;
  // (?:^|\.)(?:[0-9]+|0x[0-9a-f]+)\.?$
  std::string_view trimmed = host;
  if (trimmed.ends_with('.'))
    trimmed.remove_suffix(1);
  const auto label = trimmed.substr(trimmed.rfind('.') + 1);
  const bool numeric = IsDigits(label) || (label.starts_with("0x") &&
                                           label.size() > 2 &&
                                           IsLowerHex(label.substr(2)));
  if (!numeric)
    return true;
  std::string_view stripped = host;
  while (stripped.ends_with('.'))
    stripped.remove_suffix(1);
  return IsStrictIpv4(stripped) && stripped == host;
}

// Python's is_local_download_path(path, windows=True).
bool IsLocalWindowsDownloadPath(std::string_view path) {
  if (std::ranges::any_of(
          path, [](char c) { return static_cast<unsigned char>(c) < 32; })) {
    return false;
  }
  if (path.size() < 3 || !base::IsAsciiAlpha(path[0]) || path[1] != ':' ||
      (path[2] != '\\' && path[2] != '/') || path.ends_with('/') ||
      path.ends_with('\\')) {
    return false;
  }
  bool has_name = false;
  for (const auto part :
       base::SplitStringPiece(path.substr(3), "\\/", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (part == ".")
      continue;
    if (part == ".." || part.find(':') != std::string_view::npos)
      return false;
    has_name = true;
  }
  return has_name;
}

// Catalogue -------------------------------------------------------------------

struct Catalogue {
  OrderedMap profiles;       // Legacy profile ID to its directory.
  OrderedMap workspaces;     // Legacy workspace ID to its legacy profile ID.
  OrderedMap profile_ids;    // Legacy profile ID to Content profile ID.
  OrderedMap workspace_ids;  // Legacy workspace ID to Content workspace ID.
  // Profiles and spaces an Arc or Firefox import made keep their source ID as
  // Content's import_source, so a later import of the same browser updates
  // them instead of adding copies (WorkspaceLab::StartBrowserImport).
  std::set<std::string> imported_profiles;           // Legacy profile IDs.
  std::map<std::string, std::string> workspace_sources;  // Content ID to legacy ID.
};

bool IsLegacyProfileDirectory(std::string_view directory) {
  constexpr std::string_view kPrefix = "ZitterProfile-";
  return directory == "ZitterWork" || directory == "ZitterPersonal" ||
         (directory.starts_with(kPrefix) &&
          IsUuidText(directory.substr(kPrefix.size()), /*lowercase_only=*/true));
}

// Python's profile_mapping().
std::optional<Catalogue> ReadCatalogue(const base::FilePath& root,
                                       const base::DictValue& config) {
  Catalogue catalogue;
  base::ListValue defaults;
  const base::Value* profiles_value = config.Find("profiles");
  if (!profiles_value) {
    defaults.Append(base::DictValue()
                        .Set("id", "work-profile")
                        .Set("name", "Work")
                        .Set("directory", "ZitterWork"));
    defaults.Append(base::DictValue()
                        .Set("id", "personal-profile")
                        .Set("name", "Personal")
                        .Set("directory", "ZitterPersonal"));
  }
  const base::ListValue* profiles =
      profiles_value ? profiles_value->GetIfList() : &defaults;
  if (!profiles || profiles->empty())
    return RefuseValue<Catalogue>("Expected at least one profile");
  for (const auto& value : *profiles) {
    const auto* profile = value.GetIfDict();
    const auto* id = profile ? profile->FindString("id") : nullptr;
    const auto* name = profile ? profile->FindString("name") : nullptr;
    const auto* directory = profile ? profile->FindString("directory") : nullptr;
    if (!id || id->empty() || Lookup(catalogue.profiles, *id) || !name ||
        base::TrimWhitespace(base::UTF8ToUTF16(*name), base::TRIM_ALL)
            .empty() ||
        !directory || !IsLegacyProfileDirectory(*directory) ||
        ContainsValue(catalogue.profiles, *directory) ||
        Inspect(root.AppendASCII(*directory)) != PathKind::kDirectory) {
      return RefuseValue<Catalogue>(
          "Invalid, duplicate or missing profile directory");
    }
    catalogue.profiles.emplace_back(*id, *directory);
    if (const auto* source = profile->FindString("source_id");
        source && !source->empty() && IsCatalogueName(*id)) {
      catalogue.imported_profiles.insert(*id);
    }
  }

  const auto* workspaces = config.FindList("workspaces");
  if (!workspaces || workspaces->empty())
    return RefuseValue<Catalogue>("Expected at least one workspace");
  for (const auto& value : *workspaces) {
    const auto* workspace = value.GetIfDict();
    const auto* id = workspace ? workspace->FindString("id") : nullptr;
    const auto* name = workspace ? workspace->FindString("name") : nullptr;
    const auto* profile = workspace ? workspace->FindString("profile_id") : nullptr;
    if (!id || id->empty() || Lookup(catalogue.workspaces, *id) || !name ||
        name->empty() || !profile || !Lookup(catalogue.profiles, *profile)) {
      return RefuseValue<Catalogue>(
          "Invalid workspace or unknown workspace profile");
    }
    catalogue.workspaces.emplace_back(*id, *profile);
  }
  return catalogue;
}

// Python's content_catalogue_ids().
bool AssignContentIds(Catalogue& catalogue) {
  size_t reserved = 0;
  for (const std::string_view directory : {"ZitterWork", "ZitterPersonal"})
    reserved += !ContainsValue(catalogue.profiles, directory);
  if (catalogue.profiles.size() + reserved > kMaxContentProfiles ||
      catalogue.workspaces.size() > kMaxContentWorkspaces) {
    return Refuse(
        "The legacy catalogue exceeds Content profile or workspace limits");
  }
  for (const auto& [profile, directory] : catalogue.profiles) {
    std::string mapped;
    if (directory == "ZitterWork")
      mapped = "work";
    else if (directory == "ZitterPersonal")
      mapped = "personal";
    else
      mapped = "profile-" + directory.substr(std::string_view("ZitterProfile-").size());
    catalogue.profile_ids.emplace_back(profile, std::move(mapped));
  }

  // When the built-in workspaces use both built-in profiles, possibly
  // swapped, each profile takes the slot of the workspace that owns it.
  std::vector<std::pair<std::string_view, std::string>> owners;
  for (const auto role : kBuiltinRoles) {
    if (const auto* owner = Lookup(catalogue.workspaces, role))
      owners.emplace_back(role, *owner);
  }
  std::set<std::string> mapped_owners;
  for (const auto& [role, owner] : owners)
    mapped_owners.insert(*Lookup(catalogue.profile_ids, owner));
  if (mapped_owners == std::set<std::string>{"work", "personal"}) {
    for (const auto& [role, owner] : owners) {
      std::ranges::find_if(catalogue.profile_ids, [&](const auto& entry) {
        return entry.first == owner;
      })->second = std::string(role);
    }
  }

  std::set<std::string> seen;
  for (const auto& [workspace, profile] : catalogue.workspaces) {
    std::string mapped;
    if (IsBuiltinRole(workspace)) {
      mapped = workspace;
    } else if (const auto uuid = ParsePythonUuid(workspace)) {
      mapped = "space-" + *uuid;
    } else if (IsCatalogueName(workspace)) {
      // An imported space ("arc-space-<Arc ID>"): a stable Content ID, and
      // the legacy one as its import source.
      mapped = "space-" + NameBasedUuid(base::StrCat({"zitter-legacy-workspace:", workspace}));
      catalogue.workspace_sources.emplace(mapped, workspace);
    } else {
      return Refuse("Unsupported legacy workspace identifier");
    }
    if (!seen.insert(mapped).second)
      return Refuse("Duplicate converted workspace identifier");
    catalogue.workspace_ids.emplace_back(workspace, std::move(mapped));
  }
  return true;
}

// Workspace session -----------------------------------------------------------

// Python's convert_workspace_pins(). A null list reads as empty.
std::optional<base::ListValue> ConvertWorkspacePins(
    std::string_view workspace,
    const base::Value* profile_pins,
    const base::Value* workspace_pins) {
  const std::string error = base::StrCat({"Invalid legacy pins for ", workspace});
  const base::ListValue empty;
  const auto* profile_list = profile_pins ? profile_pins->GetIfList() : &empty;
  const auto* workspace_list =
      workspace_pins ? workspace_pins->GetIfList() : &empty;
  if (!profile_list || !workspace_list ||
      profile_list->size() + workspace_list->size() > kMaxSessionTabs) {
    return RefuseValue<base::ListValue>(error);
  }
  base::ListValue converted;
  std::set<std::string> seen;
  std::set<std::string> folders;
  const auto convert = [&](const base::Value& value, bool profile_pin) {
    const auto* pin = value.GetIfDict();
    if (!pin || (profile_pin ? !HasExactKeys(*pin, {"id", "title", "url"})
                             : !HasExactKeys(*pin, {"id", "title", "url",
                                                    "parent_id", "folder"}))) {
      return false;
    }
    const auto* id = pin->FindString("id");
    const auto* title = pin->FindString("title");
    const auto* url = pin->FindString("url");
    std::string parent;
    bool folder = false;
    if (!profile_pin) {
      const auto* parent_value = pin->FindString("parent_id");
      const auto folder_value = pin->FindBool("folder");
      if (!parent_value || !folder_value)
        return false;
      parent = *parent_value;
      folder = *folder_value;
    }
    if (!id || id->empty() || id->size() > 256 || !IsVisibleAscii(*id) ||
        seen.contains(*id) || !title ||
        Utf16Units(*title) > kMaxTabTitleUnits ||
        (!parent.empty() && !folders.contains(parent)) || !url ||
        url->size() > kMaxTabUrlBytes ||
        (folder ? !url->empty() : url->empty() || !CanRestoreSessionUrl(*url))) {
      return false;
    }
    seen.insert(*id);
    if (folder)
      folders.insert(*id);
    converted.Append(base::DictValue()
                         .Set("id", *id)
                         .Set("title", *title)
                         .Set("url", *url)
                         .Set("parent_id", parent)
                         .Set("folder", folder)
                         .Set("profile_pin", profile_pin));
    return true;
  };
  for (const auto& pin : *profile_list) {
    if (!convert(pin, /*profile_pin=*/true))
      return RefuseValue<base::ListValue>(error);
  }
  for (const auto& pin : *workspace_list) {
    if (!convert(pin, /*profile_pin=*/false))
      return RefuseValue<base::ListValue>(error);
  }
  return converted;
}

// Python's convert_session_split(). Leaves `result` empty for no split.
bool ConvertSessionSplit(std::string_view workspace,
                         const base::DictValue& entry,
                         size_t tab_count,
                         std::optional<base::DictValue>& result) {
  result.reset();
  const std::string error =
      base::StrCat({"Unsupported legacy split for ", workspace});
  const base::Value* members = entry.Find("split");
  const base::Value* weights = entry.Find("weights");
  const base::Value* right = entry.Find("right");
  const base::Value* vertical = entry.Find("vertical");
  if ((right && !right->is_int()) || (vertical && !vertical->is_bool()))
    return Refuse(error);
  const int right_index = right ? right->GetInt() : -1;
  const bool is_vertical = vertical && vertical->GetBool();
  base::Value legacy_members;
  base::Value legacy_weights;
  if (!entry.contains("split") && right_index >= 0) {
    double ratio = 0.5;
    if (const base::Value* value = entry.Find("ratio")) {
      if (!IsNumber(*value))
        return Refuse(base::StrCat({"Unsupported legacy split ratio for ", workspace}));
      ratio = value->GetDouble();
    }
    if (!(ratio > 0 && ratio < 1))
      return Refuse(base::StrCat({"Unsupported legacy split ratio for ", workspace}));
    const base::Value* left = entry.Find("left");
    legacy_members = base::Value(base::ListValue()
                                     .Append(left ? left->Clone() : base::Value(0))
                                     .Append(right_index));
    legacy_weights =
        base::Value(base::ListValue().Append(ratio).Append(1 - ratio));
    members = &legacy_members;
    weights = &legacy_weights;
  } else if (right_index != -1) {
    return Refuse(error);
  }
  const bool members_empty =
      !members || (members->is_list() && members->GetList().empty());
  const bool weights_empty =
      !weights || (weights->is_list() && weights->GetList().empty());
  if (members_empty && weights_empty && !is_vertical)
    return true;
  const base::ListValue empty;
  const auto* member_list = members ? members->GetIfList() : &empty;
  const auto* weight_list = weights ? weights->GetIfList() : &empty;
  if (!member_list || member_list->size() < 2 ||
      member_list->size() > kMaxSplitPanes || !weight_list) {
    return Refuse(error);
  }
  std::set<int> unique;
  for (const auto& index : *member_list) {
    if (!index.is_int() || index.GetInt() < 0 ||
        static_cast<size_t>(index.GetInt()) >= tab_count ||
        !unique.insert(index.GetInt()).second) {
      return Refuse(error);
    }
  }
  base::ListValue converted_weights;
  if (weight_list->empty()) {
    for (size_t i = 0; i < member_list->size(); ++i)
      converted_weights.Append(1.0 / static_cast<double>(member_list->size()));
  } else {
    if (weight_list->size() != member_list->size())
      return Refuse(base::StrCat({"Unsupported legacy split weights for ", workspace}));
    double total = 0;
    for (const auto& weight : *weight_list) {
      if (!IsNumber(weight) || !(weight.GetDouble() > 0) ||
          weight.GetDouble() > 1e100) {
        return Refuse(
            base::StrCat({"Unsupported legacy split weights for ", workspace}));
      }
      total += weight.GetDouble();
      converted_weights.Append(weight.Clone());
    }
    if (!std::isfinite(total))
      return Refuse(base::StrCat({"Unsupported legacy split weights for ", workspace}));
  }
  result = base::DictValue()
               .Set("tabs", member_list->Clone())
               .Set("weights", std::move(converted_weights))
               .Set("vertical", is_vertical);
  return true;
}

// Python's convert_session_tabs().
std::optional<base::DictValue> ConvertSessionTabs(
    std::string_view workspace,
    const base::Value& value,
    std::string_view legacy_profile,
    const base::ListValue& pins) {
  const auto* entry = value.GetIfDict();
  const auto* profile = entry ? entry->FindString("profile_id") : nullptr;
  if (!profile || *profile != legacy_profile) {
    return RefuseValue<base::DictValue>(
        base::StrCat({"Invalid legacy session profile for ", workspace}));
  }
  const base::Value* sleeping = entry->Find("sleeping");
  if (!HasOnlyKeys(*entry, {"profile_id", "active", "tabs", "split", "weights",
                            "vertical", "left", "right", "ratio", "sleeping"}) ||
      (sleeping && !sleeping->is_bool())) {
    return RefuseValue<base::DictValue>(
        base::StrCat({"Unsupported legacy workspace layout for ", workspace}));
  }
  const auto* tabs = entry->FindList("tabs");
  const base::Value* active_value = entry->Find("active");
  if (active_value && !active_value->is_int()) {
    return RefuseValue<base::DictValue>(base::StrCat(
        {"Invalid legacy session tabs or selection for ", workspace}));
  }
  const int active = active_value ? active_value->GetInt() : 0;
  if (!tabs || tabs->empty() || tabs->size() > kMaxSessionTabs || active < 0 ||
      static_cast<size_t>(active) >= tabs->size()) {
    return RefuseValue<base::DictValue>(base::StrCat(
        {"Invalid legacy session tabs or selection for ", workspace}));
  }
  std::set<std::string> pin_ids;
  for (const auto& pin : pins) {
    if (!pin.GetDict().FindBool("folder").value_or(false))
      pin_ids.insert(*pin.GetDict().FindString("id"));
  }
  std::set<std::string> open_pins;
  const std::string unsupported =
      base::StrCat({"Unsupported legacy session tab for ", workspace});
  for (const auto& value_tab : *tabs) {
    const auto* tab = value_tab.GetIfDict();
    if (!tab || !(HasExactKeys(*tab, {"url", "title"}) ||
                  HasExactKeys(*tab, {"url", "title", "pin"}))) {
      return RefuseValue<base::DictValue>(unsupported);
    }
    if (const base::Value* pin = tab->Find("pin")) {
      if (!pin->is_string() || !pin_ids.contains(pin->GetString()) ||
          !open_pins.insert(pin->GetString()).second) {
        return RefuseValue<base::DictValue>(
            base::StrCat({"Invalid legacy open pin for ", workspace}));
      }
    }
    const auto* url = tab->FindString("url");
    const auto* title = tab->FindString("title");
    if (!url || url->size() > kMaxTabUrlBytes || !CanRestoreSessionUrl(*url) ||
        !title || Utf16Units(*title) > kMaxTabTitleUnits) {
      return RefuseValue<base::DictValue>(unsupported);
    }
  }
  auto result = base::DictValue()
                    .Set("profile_id", workspace)
                    .Set("active", active)
                    .Set("tabs", tabs->Clone());
  if (sleeping && sleeping->GetBool())
    result.Set("sleeping", true);
  std::optional<base::DictValue> split;
  if (!ConvertSessionSplit(workspace, *entry, tabs->size(), split))
    return std::nullopt;
  if (split)
    result.Set("split", std::move(*split));
  return result;
}

base::DictValue AssistantPermissions(const base::Value* value) {
  base::DictValue result;
  const auto* permissions = value ? value->GetIfDict() : nullptr;
  if (!permissions)
    return result;
  for (const char* key :
       {"tabs", "pages", "actions", "history", "web", "confirm_clicks"}) {
    if (const auto permission = permissions->FindBool(key))
      result.Set(key, *permission);
  }
  return result;
}

// Python's convert_assistant_settings().
base::DictValue ConvertAssistantSettings(const base::Value& value) {
  base::DictValue converted;
  const auto* settings = value.GetIfDict();
  if (!settings)
    return converted;
  for (const char* key :
       {"enabled", "ollama_configured", "background", "confirm_background"}) {
    if (const auto flag = settings->FindBool(key))
      converted.Set(key, *flag);
  }
  for (const char* key : {"catalog_version", "concurrency"}) {
    if (const auto number = settings->FindInt(key))
      converted.Set(key, *number);
  }
  for (const auto& [key, limit] : std::initializer_list<std::pair<const char*, size_t>>{
           {"codex_path", 4096}, {"claude_path", 4096}, {"default_model", 128}}) {
    if (const auto* text = settings->FindString(key); text && text->size() <= limit)
      converted.Set(key, *text);
  }
  if (const auto* backends = settings->FindList("backends");
      backends && backends->size() == 4 &&
      std::ranges::all_of(*backends, &base::Value::is_bool)) {
    converted.Set("backends", backends->Clone());
  }
  if (const base::Value* permissions = settings->Find("permissions");
      permissions && permissions->is_dict()) {
    converted.Set("permissions", AssistantPermissions(permissions));
  }
  if (const auto* models = settings->FindList("models")) {
    base::ListValue selected;
    for (const auto& entry : *models) {
      const auto* model = entry.GetIfDict();
      const auto* provider = model ? model->FindString("provider") : nullptr;
      const auto* id = model ? model->FindString("id") : nullptr;
      if (!provider ||
          !std::ranges::contains(
              std::initializer_list<std::string_view>{"openai", "codex",
                                                      "claude", "ollama"},
              std::string_view(*provider)) ||
          !id || id->empty() || id->size() > 120) {
        continue;
      }
      selected.Append(base::DictValue()
                          .Set("provider", *provider)
                          .Set("id", *id)
                          .Set("visible", model->FindBool("visible").value_or(true)));
      if (selected.size() == 32)
        break;
    }
    if (!selected.empty())
      converted.Set("models", std::move(selected));
  }
  if (const auto* key = settings->FindString("encrypted_api_key")) {
    // The macOS Keychain and Windows DPAPI providers use v10. Windows
    // App-Bound Encryption uses v20; keep it for a later migration.
    const size_t length = CodePoints(*key);
    std::string ciphertext;
    if (length > 0 && length <= 8192 && base::Base64Decode(*key, &ciphertext) &&
        ciphertext.size() >= 19 &&
        (ciphertext.starts_with("v10") || ciphertext.starts_with("v20"))) {
      converted.Set("encrypted_api_key", *key);
    }
  }
  return converted;
}

// Python's read_workspace_session(). Leaves `result` empty when there is
// nothing to write.
bool ConvertWorkspaceSession(const base::DictValue& config,
                             const std::optional<base::DictValue>& session,
                             const Catalogue& catalogue,
                             std::optional<std::string>& result) {
  result.reset();
  if (session) {
    for (const auto [key, value] : *session) {
      if (!Lookup(catalogue.workspaces, key))
        return Refuse("Invalid legacy workspace session or configuration");
    }
  }
  std::map<std::string, const base::DictValue*, std::less<>> profile_entries;
  if (const auto* profiles = config.FindList("profiles")) {
    for (const auto& entry : *profiles)
      profile_entries.emplace(*entry.GetDict().FindString("id"), &entry.GetDict());
  }
  std::map<std::string, const base::DictValue*, std::less<>> workspace_entries;
  for (const auto& entry : *config.FindList("workspaces"))
    workspace_entries.emplace(*entry.GetDict().FindString("id"), &entry.GetDict());

  const auto* active = config.FindString("active");
  const std::string active_workspace =
      active && Lookup(catalogue.workspace_ids, *active)
          ? *Lookup(catalogue.workspace_ids, *active)
          : catalogue.workspace_ids.front().second;
  base::DictValue converted;
  converted.Set("active_workspace", active_workspace);
  base::DictValue removed;
  for (const auto role : kBuiltinRoles) {
    if (!Lookup(catalogue.workspaces, role))
      removed.Set(role, role);
  }
  const bool has_removed = !removed.empty();
  if (has_removed)
    converted.Set("removed_workspaces", std::move(removed));

  std::set<std::string> assigned_profiles;
  for (const auto& [workspace, profile] : catalogue.workspaces)
    assigned_profiles.insert(profile);
  base::ListValue extra_profiles;
  base::DictValue profile_names;
  base::DictValue profile_favorites;
  for (const auto& [profile, mapped] : catalogue.profile_ids) {
    const auto entry = profile_entries.find(profile);
    const base::DictValue* metadata =
        entry == profile_entries.end() ? nullptr : entry->second;
    const auto default_name = BuiltinName(mapped);
    std::string name;
    if (const base::Value* value = metadata ? metadata->Find("name") : nullptr) {
      if (!value->is_string())
        return Refuse("Unsupported legacy catalogue name");
      name = value->GetString();
    } else if (default_name) {
      name = *default_name;
    }
    if (!IsCatalogueName(name))
      return Refuse("Unsupported legacy catalogue name");
    if (!default_name) {
      auto extra = base::DictValue().Set("id", mapped).Set("name", name);
      if (catalogue.imported_profiles.contains(profile))
        extra.Set("import_source", profile);
      extra_profiles.Append(std::move(extra));
    }
    else if (name != *default_name)
      profile_names.Set(mapped, name);
    const base::Value* pins = metadata ? metadata->Find("pins") : nullptr;
    if (!assigned_profiles.contains(profile) && IsTruthy(pins)) {
      auto favorites = ConvertWorkspacePins(mapped, pins, nullptr);
      if (!favorites)
        return false;
      profile_favorites.Set(mapped, std::move(*favorites));
    }
  }

  base::ListValue extra_workspaces;
  base::DictValue workspace_names;
  base::DictValue timeline_epochs;
  bool has_pins = false;
  for (const auto& [workspace, profile] : catalogue.workspaces) {
    const base::DictValue* workspace_entry = workspace_entries.at(workspace);
    const auto profile_entry = profile_entries.find(profile);
    auto pins = ConvertWorkspacePins(
        workspace,
        profile_entry == profile_entries.end()
            ? nullptr
            : profile_entry->second->Find("pins"),
        workspace_entry->Find("pins"));
    if (!pins)
      return false;
    const std::string& mapped = *Lookup(catalogue.workspace_ids, workspace);
    const std::string& owner = *Lookup(catalogue.profile_ids, profile);
    const std::string& name = *workspace_entry->FindString("name");
    if (!IsCatalogueName(name))
      return Refuse("Unsupported legacy catalogue name");
    const auto default_name = BuiltinName(mapped);
    if (!default_name) {
      auto extra = base::DictValue().Set("id", mapped).Set("name", name);
      if (const auto source = catalogue.workspace_sources.find(mapped);
          source != catalogue.workspace_sources.end()) {
        extra.Set("import_source", source->second);
      }
      extra_workspaces.Append(std::move(extra));
    } else {
      if (name != *default_name)
        workspace_names.Set(mapped, name);
      if (owner != mapped) {
        timeline_epochs.Set(mapped, NameBasedUuid(base::StrCat(
                                        {"dev.zitter.browser/", mapped, "/", owner})));
      }
    }
    base::DictValue entry;
    if (session && session->contains(workspace)) {
      auto tabs =
          ConvertSessionTabs(workspace, *session->Find(workspace), profile, *pins);
      if (!tabs)
        return false;
      entry = std::move(*tabs);
    } else {
      entry = base::DictValue().Set("active", 0).Set(
          "tabs", base::ListValue().Append(
                      base::DictValue().Set("url", "").Set("title", "")));
    }
    entry.Set("profile_id", owner);
    if (mapped == active_workspace)
      entry.Remove("sleeping");
    if (!pins->empty()) {
      entry.Set("pins", std::move(*pins));
      has_pins = true;
    }
    converted.Set(mapped, std::move(entry));
  }
  const bool has_catalogue_changes =
      !extra_profiles.empty() || !extra_workspaces.empty() ||
      !profile_names.empty() || !workspace_names.empty() ||
      !profile_favorites.empty() || !timeline_epochs.empty() || has_removed;
  if (!extra_profiles.empty())
    converted.Set("extra_profiles", std::move(extra_profiles));
  if (!profile_names.empty())
    converted.Set("profile_names", std::move(profile_names));
  if (!profile_favorites.empty())
    converted.Set("profile_favorites", std::move(profile_favorites));
  if (!extra_workspaces.empty())
    converted.Set("extra_workspaces", std::move(extra_workspaces));
  if (!workspace_names.empty())
    converted.Set("workspace_names", std::move(workspace_names));
  if (!timeline_epochs.empty())
    converted.Set("timeline_epochs", std::move(timeline_epochs));

  base::DictValue rules;
  if (const auto* legacy_rules = config.FindList("link_rules")) {
    for (const auto& value : *legacy_rules) {
      const auto* rule = value.GetIfDict();
      const auto* host = rule ? rule->FindString("host") : nullptr;
      const auto* target = rule ? rule->FindString("workspace_id") : nullptr;
      if (!host || !target || !Lookup(catalogue.workspaces, *target) ||
          host->empty() || *host != base::ToLowerASCII(*host) ||
          !CanRestoreSessionUrl(base::StrCat({"https://", *host, "/"})) ||
          !IsPlainLinkHost(*host)) {
        continue;
      }
      if (!rules.contains(*host))
        rules.Set(*host, *Lookup(catalogue.workspace_ids, *target));
    }
  }
  const bool has_rules = !rules.empty();
  if (has_rules)
    converted.Set("link_rules", std::move(rules));
  const bool no_peek = config.FindBool("shift_click_peek") == false;
  if (no_peek)
    converted.Set("shift_click_peek", false);
  const bool no_clean_copies = config.FindBool("clean_address_copies") == false;
  if (no_clean_copies)
    converted.Set("clean_address_copies", false);
  bool has_assistant = false;
  if (const base::Value* assistant = config.Find("assistant")) {
    auto settings = ConvertAssistantSettings(*assistant);
    has_assistant = !settings.empty();
    if (has_assistant)
      converted.Set("assistant", std::move(settings));
  }
  if (!session && !has_rules && !has_assistant && !no_peek && !no_clean_copies &&
      !has_catalogue_changes && !has_pins) {
    return true;
  }
  auto json = Serialize(converted);
  if (!json || json->size() > kMaxJsonBytes) {
    return Refuse(
        "The converted workspace session exceeds the Content size limit");
  }
  result = std::move(*json);
  return true;
}

// Python's read_workspace_timeline(). Leaves `result` empty when there is no
// legacy timeline.
bool ConvertTimeline(const base::FilePath& root,
                     const Catalogue& catalogue,
                     std::optional<std::string>& result) {
  result.reset();
  std::optional<std::string> data;
  if (!ReadLegacyFile(root.AppendASCII(kLegacyTimeline), kMaxTimelineBytes, data))
    return false;
  if (!data)
    return true;
  struct Record {
    double t;
    std::string workspace;
    std::optional<std::string> mark;
    base::DictValue frame;
  };
  std::vector<Record> output;
  std::map<std::string, std::vector<std::pair<double, std::string>>> owners;
  std::set<std::string> replaceable;
  std::string_view remaining = *data;
  while (!remaining.empty()) {
    const size_t newline = remaining.find('\n');
    const auto raw = remaining.substr(
        0, newline == std::string_view::npos ? newline : newline + 1);
    remaining.remove_prefix(raw.size());
    if (base::TrimString(raw, " \t\n\r\v\f", base::TRIM_ALL).empty())
      continue;
    const auto value = ParseJson(raw);
    if (!value || !value->is_dict())
      return Refuse("Invalid legacy workspace timeline record");
    const auto& line = value->GetDict();
    const base::Value* time = line.Find("t");
    const auto* workspace = line.FindString("w");
    if (!time || !IsNumber(*time) || time->GetDouble() < 0 ||
        time->GetDouble() > kMaxTimelineMilliseconds ||
        time->GetDouble() != std::trunc(time->GetDouble()) || !workspace) {
      return Refuse("Invalid legacy workspace timeline timestamp or workspace");
    }
    const double timestamp = time->GetDouble();
    const std::string* legacy_profile = Lookup(catalogue.workspaces, *workspace);
    if (!legacy_profile)
      continue;
    if (line.contains("m")) {
      const auto* name = line.FindString("m");
      if (!HasExactKeys(line, {"t", "w", "m"}) || !name || name->empty() ||
          name->size() > 160) {
        return Refuse("Invalid legacy workspace timeline mark");
      }
      output.push_back({timestamp, *workspace, *name, base::DictValue()});
      continue;
    }
    const auto* profile = line.FindString("p");
    const base::Value* active = line.Find("a");
    const auto* tabs = line.FindList("tabs");
    const base::Value* replace = line.Find("r");
    if (!(HasExactKeys(line, {"t", "w", "p", "a", "tabs"}) ||
          HasExactKeys(line, {"t", "w", "p", "a", "tabs", "r"})) ||
        !profile || !active || !active->is_int() || !tabs ||
        tabs->size() > kMaxSessionTabs || active->GetInt() < -1 ||
        active->GetInt() >= static_cast<int>(tabs->size()) ||
        (replace && !replace->is_bool())) {
      return Refuse("Invalid legacy workspace timeline frame");
    }
    auto& frames = owners[*workspace];
    if (!frames.empty() && timestamp < frames.back().first)
      return Refuse("Legacy workspace timeline frames are out of order");
    frames.emplace_back(timestamp, *profile);
    if (*profile != *legacy_profile) {
      replaceable.erase(*workspace);
      continue;
    }
    base::ListValue filtered;
    int selected = -1;
    for (size_t index = 0; index < tabs->size(); ++index) {
      const auto* pair = (*tabs)[index].GetIfList();
      if (!pair || pair->size() != 2 || !(*pair)[0].is_string() ||
          !(*pair)[1].is_string()) {
        return Refuse("Invalid legacy workspace timeline tab");
      }
      const std::string& url = (*pair)[0].GetString();
      const std::string& title = (*pair)[1].GetString();
      if (url.empty() || url.size() > kMaxTabUrlBytes ||
          !CanRestoreSessionUrl(url)) {
        continue;
      }
      if (static_cast<int>(index) == active->GetInt())
        selected = static_cast<int>(filtered.size());
      filtered.Append(base::ListValue().Append(url).Append(
          PrefixCodePoints(title, kMaxTabTitleUnits)));
    }
    auto frame = base::DictValue()
                     .Set("t", timestamp)
                     .Set("w", *Lookup(catalogue.workspace_ids, *workspace))
                     .Set("p", *Lookup(catalogue.profile_ids, *profile))
                     .Set("a", selected)
                     .Set("tabs", std::move(filtered));
    if (replace && replace->GetBool() && replaceable.contains(*workspace))
      frame.Set("r", true);
    output.push_back({timestamp, *workspace, std::nullopt, std::move(frame)});
    replaceable.insert(*workspace);
  }

  std::string text;
  for (auto& record : output) {
    base::DictValue line;
    if (record.mark) {
      // Keep a mark only when the frame in force at its time belongs to the
      // workspace's current profile.
      const auto& frames = owners[record.workspace];
      const auto end = std::ranges::partition_point(
          frames, [&](const auto& owner) { return owner.first < record.t + 1; });
      if (end == frames.begin() ||
          std::prev(end)->second != *Lookup(catalogue.workspaces, record.workspace)) {
        continue;
      }
      line = base::DictValue()
                 .Set("t", record.t)
                 .Set("w", *Lookup(catalogue.workspace_ids, record.workspace))
                 .Set("m", *record.mark);
    } else {
      line = std::move(record.frame);
    }
    const auto json = Serialize(line);
    if (!json)
      return Refuse("Could not serialize the workspace timeline");
    text += *json;
    text += '\n';
    if (text.size() > kMaxTimelineBytes) {
      return Refuse(
          "The converted workspace timeline exceeds the Content size limit");
    }
  }
  result = std::move(text);
  return true;
}

// Python's assistant_message().
std::optional<base::DictValue> ConvertAssistantMessage(const base::Value& value) {
  const auto* message = value.GetIfDict();
  const auto* text = message ? message->FindString("text") : nullptr;
  const auto kind = message ? message->FindInt("kind") : std::nullopt;
  if (!text || !kind || *kind < 0 || *kind > 5)
    return std::nullopt;
  auto converted = base::DictValue().Set("kind", *kind).Set("text", *text);
  if (const auto state = message->FindInt("state"))
    converted.Set("state", *state);
  if (const auto* options = message->FindList("options")) {
    base::ListValue strings;
    for (const auto& option : *options) {
      if (option.is_string())
        strings.Append(option.Clone());
    }
    converted.Set("options", std::move(strings));
  }
  if (const auto* answer = message->FindString("answer"); answer && !answer->empty()) {
    converted.Set("answer", *answer);
  } else if (*kind == 5) {
    // Questions left open by the old runtime cannot be resumed.
    converted.Set("answer", "\xE2\x80\x94");
  }
  if (const auto* chips = message->FindList("chips")) {
    base::ListValue display;
    for (const auto& value_chip : *chips) {
      const auto* chip = value_chip.GetIfDict();
      const auto* chip_kind = chip ? chip->FindString("kind") : nullptr;
      if (!chip_kind)
        continue;
      const auto* title = chip->FindString("title");
      const base::Value* color = chip->Find("color");
      base::Value color_value(0);
      if (color && IsNumber(*color) && color->GetDouble() >= 0 &&
          color->GetDouble() <= 0xffffffffu) {
        color_value = color->Clone();
      }
      // Legacy tab and pin references do not resolve in the new session.
      display.Append(base::DictValue()
                         .Set("kind", *chip_kind)
                         .Set("ref", "")
                         .Set("title", title ? *title : std::string())
                         .Set("color", std::move(color_value)));
    }
    converted.Set("chips", std::move(display));
  }
  return converted;
}

// Python's read_assistant_sessions(). Leaves `result` empty when there is no
// legacy Assistant history.
bool ConvertAssistantSessions(const base::FilePath& root,
                              const Catalogue& catalogue,
                              std::optional<std::string>& result) {
  result.reset();
  std::optional<std::string> data;
  if (!ReadLegacyFile(root.AppendASCII(kAssistantSessions),
                      kMaxAssistantSessionsBytes, data)) {
    return false;
  }
  if (!data)
    return true;
  const auto value = ParseJson(*data);
  const auto* document = value ? value->GetIfDict() : nullptr;
  if (!document || document->FindInt("version") != 1 ||
      (document->contains("sessions") && !document->FindList("sessions")) ||
      (document->contains("tasks") && !document->FindList("tasks"))) {
    return Refuse("Unsupported legacy Assistant session format");
  }
  base::ListValue sessions;
  base::ListValue tasks;
  for (const bool is_session : {true, false}) {
    const char* section = is_session ? "sessions" : "tasks";
    const char* key = is_session ? "key" : "id";
    base::ListValue& output = is_session ? sessions : tasks;
    std::set<std::string> seen;
    const auto* entries = document->FindList(section);
    if (!entries)
      continue;
    for (const auto& value_entry : *entries) {
      const auto* entry = value_entry.GetIfDict();
      const auto* profile = entry ? entry->FindString("profile") : nullptr;
      const auto* identifier = entry ? entry->FindString(key) : nullptr;
      const std::string* mapped =
          profile ? Lookup(catalogue.profile_ids, *profile) : nullptr;
      if (!mapped || !identifier || identifier->empty())
        continue;
      auto permissions = AssistantPermissions(entry->Find("permissions"));
      permissions.Set("confirm_clicks", true);
      auto converted = base::DictValue()
                           .Set(key, *identifier)
                           .Set("profile", *mapped)
                           .Set("permissions", std::move(permissions));
      for (const char* field : {"provider", "model"}) {
        if (const auto* text = entry->FindString(field))
          converted.Set(field, *text);
      }
      const char* timestamp_field;
      if (is_session) {
        const auto* messages = entry->FindList("messages");
        if (!messages)
          continue;
        base::ListValue converted_messages;
        bool has_request = false;
        for (const auto& message : *messages) {
          auto item = ConvertAssistantMessage(message);
          if (!item)
            continue;
          has_request |= item->FindInt("kind") == 1;
          converted_messages.Append(std::move(*item));
        }
        if (!has_request)
          continue;
        converted.Set("messages", std::move(converted_messages));
        if (const auto* title = entry->FindString("title"))
          converted.Set("title", *title);
        // Imported conversations require a new confirmation.
        converted.Set("confirm_background", true);
        timestamp_field = "updated";
      } else {
        const auto* instruction = entry->FindString("instruction");
        if (!instruction || instruction->empty())
          continue;
        converted.Set("instruction", *instruction);
        converted.Set("active", false);
        converted.Set("next_run", 0);
        if (const auto* name = entry->FindString("name"))
          converted.Set("name", *name);
        for (const char* field : {"kind", "days", "minute", "last_state"}) {
          if (const auto number = entry->FindInt(field))
            converted.Set(field, *number);
        }
        timestamp_field = "last_run";
      }
      if (const base::Value* time = entry->Find(timestamp_field);
          time && IsNumber(*time) && time->GetDouble() >= 0 &&
          time->GetDouble() <= kMaxAssistantMilliseconds) {
        converted.Set(timestamp_field, time->Clone());
      }
      if (!seen.insert(*identifier).second) {
        return Refuse(base::StrCat(
            {"Duplicate legacy Assistant ", section, " identifier"}));
      }
      output.Append(std::move(converted));
    }
  }
  if (sessions.size() > kMaxAssistantConversations) {
    return Refuse(
        "The legacy Assistant history exceeds the Content conversation limit");
  }
  auto json = Serialize(base::DictValue()
                            .Set("version", 1)
                            .Set("sessions", std::move(sessions))
                            .Set("tasks", std::move(tasks)));
  if (!json || json->size() > kMaxAssistantSessionsBytes) {
    return Refuse(
        "The converted Assistant history exceeds the Content size limit");
  }
  result = std::move(*json);
  return true;
}

// The appearance part of import_workspace_profiles().
bool ConvertAppearance(const base::FilePath& root,
                       const Catalogue& catalogue,
                       std::optional<std::string>& result) {
  result.reset();
  std::optional<base::DictValue> appearance;
  if (!ReadJsonDict(root.AppendASCII(kLegacyAppearance), "workspace appearance",
                    appearance)) {
    return false;
  }
  if (!appearance)
    return true;
  base::DictValue settings;
  for (const char* key :
       {"theme", "search_engine", "sidebar_pinned", "sidebar_width",
        "workspace_animations", "workspace_names", "pinned_folders_collapsed",
        "time_machine", "time_machine_days", "auto_update_check",
        "auto_update_download"}) {
    if (const base::Value* value = appearance->Find(key))
      settings.Set(key, value->Clone());
  }
  for (const auto& [legacy, mapped] : catalogue.workspace_ids) {
    if (const auto* color = appearance->FindString(legacy))
      settings.Set(mapped, *color);
  }
  if (const auto* emoji = appearance->FindDict("workspace_emoji")) {
    base::DictValue mapped_emoji;
    for (const auto& [legacy, mapped] : catalogue.workspace_ids) {
      if (const auto* symbol = emoji->FindString(legacy))
        mapped_emoji.Set(mapped, *symbol);
    }
    settings.Set("workspace_emoji", std::move(mapped_emoji));
  }
  if (settings.empty())
    return true;
  auto json = Serialize(settings);
  if (!json)
    return Refuse("Could not serialize the workspace appearance");
  result = std::move(*json);
  return true;
}

// Python's read_dpapi_local_state().
std::optional<std::string> ConvertLocalState(const base::DictValue& local_state) {
  const base::Value* encryption_value = local_state.Find("os_crypt");
  if (encryption_value && !encryption_value->is_dict())
    return RefuseValue<std::string>("Missing legacy Windows OSCrypt settings");
  const auto* encryption = encryption_value ? encryption_value->GetIfDict() : nullptr;
  if (!encryption)
    return RefuseValue<std::string>("Missing legacy Windows OSCrypt settings");
  if (const base::Value* app_bound = encryption->Find("app_bound_encrypted_key");
      app_bound && !(app_bound->is_string() && app_bound->GetString().empty())) {
    return RefuseValue<std::string>(
        "Legacy Windows App-Bound Encryption requires a separate migration");
  }
  const auto* wrapped_key = encryption->FindString("encrypted_key");
  std::string decoded;
  if (!wrapped_key || !base::Base64Decode(*wrapped_key, &decoded) ||
      decoded.size() <= 5 || !decoded.starts_with("DPAPI")) {
    return RefuseValue<std::string>("Malformed legacy Windows DPAPI key");
  }
  auto payload = Serialize(base::DictValue().Set(
      "os_crypt", base::DictValue().Set("encrypted_key", *wrapped_key)));
  if (!payload || payload->size() > kMaxLocalStateBytes) {
    return RefuseValue<std::string>(
        "Legacy Windows DPAPI key exceeds the Content limit");
  }
  return payload;
}

// Profile preferences ---------------------------------------------------------

bool IsZeroString(const base::Value* value) {
  return !value || (value->is_string() && value->GetString() == "0");
}

bool IsFalse(const base::Value* value) {
  return !value || (value->is_bool() && !value->GetBool());
}

// Python's clipboard_rules_from_preferences().
bool ConvertClipboardRules(const base::DictValue& preferences,
                           base::DictValue& result,
                           base::DictValue& times) {
  const base::DictValue* profile;
  const base::DictValue* defaults;
  const base::DictValue* managed;
  const base::DictValue* content_settings;
  const base::DictValue* exceptions;
  const base::DictValue* rules;
  if (!ObjectAt(&preferences, "profile", profile) ||
      !ObjectAt(profile, "default_content_setting_values", defaults)) {
    return Refuse("Invalid legacy Clipboard preference object");
  }
  if (const base::Value* value = defaults ? defaults->Find("clipboard") : nullptr;
      value && !(value->is_int() && (value->GetInt() == 0 || value->GetInt() == 3))) {
    return Refuse("Unsupported legacy default Clipboard setting");
  }
  if (!ObjectAt(profile, "managed_default_content_settings", managed))
    return Refuse("Invalid legacy Clipboard preference object");
  if ((managed && managed->contains("clipboard")) ||
      (profile && (profile->contains("managed_clipboard_allowed_for_urls") ||
                   profile->contains("managed_clipboard_blocked_for_urls")))) {
    return Refuse("Managed Clipboard settings require a policy migration");
  }
  if (!ObjectAt(profile, "content_settings", content_settings) ||
      !ObjectAt(content_settings, "exceptions", exceptions) ||
      !ObjectAt(exceptions, "clipboard", rules)) {
    return Refuse("Invalid legacy Clipboard preference object");
  }
  std::map<std::string, int> converted;
  if (rules) {
    for (const auto [pattern, value] : *rules) {
      const auto origin = ExactSiteOrigin(pattern, "Clipboard", true);
      if (!origin)
        return false;
      if (converted.contains(*origin))
        return Refuse(base::StrCat({"Duplicate legacy Clipboard origin: ", *origin}));
      const auto* record = value.GetIfDict();
      const auto setting = record ? record->FindInt("setting") : std::nullopt;
      if (!setting)
        return Refuse(base::StrCat({"Malformed legacy Clipboard rule: ", pattern}));
      if (!KeysWithin(*record, kSiteRuleMetadata)) {
        return Refuse(
            base::StrCat({"Unknown legacy Clipboard rule metadata: ", pattern}));
      }
      const base::Value* model = record->Find("model");
      if ((model && !(model->is_int() && model->GetInt() == 0)) ||
          !IsZeroString(record->Find("expiration")) ||
          !IsZeroString(record->Find("lifetime"))) {
        return Refuse(base::StrCat(
            {"Temporary or expiring legacy Clipboard rule: ", pattern}));
      }
      if (!IsZeroString(record->Find("last_visit"))) {
        return Refuse(
            base::StrCat({"Auto-revocable legacy Clipboard rule: ", pattern}));
      }
      if (!IsFalse(record->Find("decided_by_related_website_sets"))) {
        return Refuse(base::StrCat(
            {"Related-site Clipboard rule requires review: ", pattern}));
      }
      if (*setting < 1 || *setting > 3) {
        return Refuse(
            base::StrCat({"Unsupported legacy Clipboard decision: ", pattern}));
      }
      converted[*origin] = *setting;
      const auto timestamp = ChromiumTimestamp(record->Find("last_modified"));
      if (*setting != 3 && timestamp && *timestamp != "0")
        times.Set(*origin, *timestamp);
    }
  }
  for (const auto& [origin, setting] : converted) {
    if (setting != 3)
      result.Set(origin, setting);
  }
  if (result.size() > kMaxSiteRules)
    return Refuse("Too many rules for the Content Clipboard preferences");
  return true;
}

// Python's popup_settings_from_preferences().
bool ConvertPopupSettings(const base::DictValue& preferences,
                          bool& popups_allowed,
                          base::DictValue& result,
                          base::DictValue& times) {
  const base::DictValue* profile;
  const base::DictValue* defaults;
  const base::DictValue* managed;
  const base::DictValue* settings;
  const base::DictValue* exceptions;
  const base::DictValue* rules;
  if (!ObjectAt(&preferences, "profile", profile) ||
      !ObjectAt(profile, "default_content_setting_values", defaults)) {
    return Refuse("Invalid legacy popup preference object");
  }
  int default_setting = 2;
  if (const base::Value* value = defaults ? defaults->Find("popups") : nullptr) {
    if (!value->is_int() || value->GetInt() < 0 || value->GetInt() > 2)
      return Refuse("Unsupported legacy default popup setting");
    default_setting = value->GetInt();
  }
  if (!ObjectAt(profile, "managed_default_content_settings", managed))
    return Refuse("Invalid legacy popup preference object");
  if ((managed && managed->contains("popups")) ||
      (profile && (profile->contains("managed_popups_allowed_for_urls") ||
                   profile->contains("managed_popups_blocked_for_urls")))) {
    return Refuse("Managed popup settings require a policy migration");
  }
  if (!ObjectAt(profile, "content_settings", settings) ||
      !ObjectAt(settings, "exceptions", exceptions) ||
      !ObjectAt(exceptions, "popups", rules)) {
    return Refuse("Invalid legacy popup preference object");
  }
  if (rules) {
    for (const auto [pattern, value] : *rules) {
      const auto origin = ExactSiteOrigin(pattern, "popup", false);
      if (!origin)
        return false;
      if (result.contains(*origin) || origin->size() > 512) {
        return Refuse(base::StrCat(
            {"Duplicate or oversized legacy popup origin: ", *origin}));
      }
      const auto* record = value.GetIfDict();
      const auto setting = record ? record->FindInt("setting") : std::nullopt;
      if (!setting || (*setting != 1 && *setting != 2))
        return Refuse(base::StrCat({"Malformed legacy popup rule: ", pattern}));
      const base::Value* model = record->Find("model");
      if (!KeysWithin(*record, kSiteRuleMetadata) ||
          !IsZeroString(record->Find("expiration")) ||
          !IsZeroString(record->Find("lifetime")) ||
          (model && !(model->is_int() && model->GetInt() == 0)) ||
          !IsZeroString(record->Find("last_visit")) ||
          !IsFalse(record->Find("decided_by_related_website_sets"))) {
        return Refuse(base::StrCat({"Non-durable legacy popup rule: ", pattern}));
      }
      result.Set(*origin, *setting);
      const auto timestamp = ChromiumTimestamp(record->Find("last_modified"));
      if (timestamp && *timestamp != "0")
        times.Set(*origin, *timestamp);
    }
  }
  if (result.size() > kMaxSiteRules)
    return Refuse("Too many rules for the Content popup preferences");
  popups_allowed = default_setting == 1;
  return true;
}

// Python's write_site_permission_rules().
bool WriteSitePermissionRules(const base::FilePath& target,
                              base::DictValue clipboard,
                              base::DictValue popups,
                              bool popups_allowed,
                              base::DictValue clipboard_times,
                              base::DictValue popup_times) {
  base::DictValue settings;
  settings.Set("clipboard_permissions", std::move(clipboard));
  if (!popups.empty())
    settings.Set("popup_permissions", std::move(popups));
  if (popups_allowed)
    settings.Set("popups_allowed", true);
  if (!clipboard_times.empty())
    settings.Set("clipboard_permission_times", std::move(clipboard_times));
  if (!popup_times.empty())
    settings.Set("popup_permission_times", std::move(popup_times));
  return WriteJsonFile(target.AppendASCII(kSitePermissions),
                       base::DictValue().Set("zitter", std::move(settings)),
                       kMaxJsonBytes, /*newline=*/true);
}

// Python's write_password_saving_preference().
bool WritePasswordSavingPreference(const base::FilePath& target,
                                   const base::DictValue& preferences) {
  const base::Value* enabled = preferences.Find(kPasswordSavingPref);
  if (!enabled)
    return true;
  if (!enabled->is_bool())
    return Refuse("Invalid legacy password saving preference");
  return WriteJsonFile(target.AppendASCII(kPasswordPrefs),
                       base::DictValue().Set(kPasswordSavingPref, enabled->GetBool()),
                       kMaxJsonBytes, /*newline=*/true);
}

// Python's write_page_zoom_preference().
bool WritePageZoomPreference(const base::FilePath& target,
                             const base::DictValue& preferences) {
  const base::DictValue* partition;
  const base::DictValue* defaults;
  const base::DictValue* per_host;
  if (!ObjectAt(&preferences, "partition", partition))
    return Refuse("Invalid legacy partition preferences");
  if (!ObjectAt(partition, "default_zoom_level", defaults) ||
      !ObjectAt(partition, "per_host_zoom_levels", per_host)) {
    return Refuse("Invalid legacy page zoom preferences");
  }
  // Chromium hex-encodes the relative partition path after the x prefix. The
  // main partition is the profile directory itself, so the path is empty.
  // Desktop zoom uses a factor of 1.2 per level and allows 25% to 500%.
  const double minimum = std::log(0.25) / std::log(1.2);
  const double maximum = std::log(5.0) / std::log(1.2);
  const auto valid = [&](const base::Value* level) {
    return level && IsNumber(*level) && level->GetDouble() >= minimum &&
           level->GetDouble() <= maximum;
  };
  const base::Value* default_value = defaults ? defaults->Find("x") : nullptr;
  if (default_value && !valid(default_value))
    return Refuse("Invalid legacy default page zoom");
  const double default_level = default_value ? default_value->GetDouble() : 0.0;
  const base::Value* hosts = per_host ? per_host->Find("x") : nullptr;
  if (hosts && !hosts->is_dict())
    return Refuse("Invalid legacy per-site page zoom");
  base::DictValue converted;
  if (hosts) {
    for (const auto [host, value] : hosts->GetDict()) {
      const base::Value* level =
          value.is_dict() ? value.GetDict().Find("zoom_level") : &value;
      if (host.empty() || host.find('\0') != std::string::npos ||
          !valid(level) || std::abs(level->GetDouble() - default_level) <= 0.001) {
        continue;
      }
      auto entry = base::DictValue().Set("zoom_level", level->GetDouble());
      if (value.is_dict()) {
        if (const auto time = ChromiumTimestamp(value.GetDict().Find("last_modified")))
          entry.Set("last_modified", *time);
      }
      converted.Set(host, std::move(entry));
    }
  }
  base::DictValue result;
  if (default_value)
    result.Set("default_zoom_level", default_level);
  if (!converted.empty())
    result.Set("per_host_zoom_levels", std::move(converted));
  if (result.empty())
    return true;
  return WriteJsonFile(target.AppendASCII(kPageZoomPrefs),
                       base::DictValue().Set("partition", std::move(result)),
                       kMaxPageZoomBytes, /*newline=*/true);
}

// Python's write_adblock_preferences().
bool WriteAdblockPreferences(const base::FilePath& target,
                             const base::DictValue& preferences) {
  const auto* group = preferences.FindDict("zitter");
  if (!group)
    return true;
  base::DictValue result;
  for (const char* key : {"ad_blocking", "ad_blocking_allowed_sites",
                          "ad_blocking_rules", "ad_blocking_lists"}) {
    if (const base::Value* value = group->Find(key))
      result.Set(key, value->Clone());
  }
  if (result.empty())
    return true;
  const base::Value* enabled = result.Find("ad_blocking");
  const base::Value* sites = result.Find("ad_blocking_allowed_sites");
  const base::Value* rules = result.Find("ad_blocking_rules");
  const base::Value* lists = result.Find("ad_blocking_lists");
  if ((enabled && !enabled->is_bool()) ||
      (sites && (!sites->is_list() || sites->GetList().size() > 512)) ||
      (rules && (!rules->is_list() || rules->GetList().size() > 1000)) ||
      (lists && (!lists->is_dict() || lists->GetDict().size() > 8))) {
    return Refuse("Invalid legacy ad blocking preferences");
  }
  if (sites) {
    for (const auto& value : sites->GetList()) {
      const auto* site = value.GetIfString();
      if (!site || site->empty() || site->size() > 253 || !IsVisibleAscii(*site) ||
          site->find_first_of("/?#@\\") != std::string::npos ||
          (site->find(':') != std::string::npos &&
           !(site->starts_with('[') && site->ends_with(']'))) ||
          !CanRestoreSessionUrl(base::StrCat({"https://", *site, "/"}))) {
        return Refuse("Invalid legacy ad blocking exception");
      }
    }
  }
  if (rules) {
    for (const auto& value : rules->GetList()) {
      const auto* rule = value.GetIfString();
      if (!rule || rule->empty() || rule->size() > 1000 ||
          rule->find_first_of("\r\n") != std::string::npos) {
        return Refuse("Invalid legacy ad blocking rule");
      }
    }
  }
  if (lists) {
    for (const auto [key, value] : lists->GetDict()) {
      if (key.size() > 64 || !value.is_bool())
        return Refuse("Invalid legacy ad blocking list switch");
    }
  }
  return WriteJsonFile(target.AppendASCII(kAdblockPrefs),
                       base::DictValue().Set("zitter", std::move(result)),
                       kMaxAdblockBytes, /*newline=*/false);
}

// Browser storage -------------------------------------------------------------

// Python's copy_sqlite_store(). Sets `copied` when the store exists.
bool CopySqliteStore(const base::FilePath& profile,
                     const base::FilePath& target,
                     std::string_view database,
                     bool& copied) {
  copied = false;
  bool has_database = false;
  for (const char* suffix : kSqliteSuffixes) {
    const std::string name = base::StrCat({database, suffix});
    const auto source = profile.AppendASCII(name);
    switch (Inspect(source)) {
      case PathKind::kMissing:
        continue;
      case PathKind::kFile:
        break;
      default:
        return Refuse(base::StrCat(
            {"Invalid legacy ", database, " database or sidecar"}));
    }
    if (!CopyVerifiedFile(source, target.AppendASCII(name)))
      return false;
    copied = true;
    has_database |= !*suffix;
  }
  if (copied && !has_database)
    return Refuse(base::StrCat({"Legacy ", database, " sidecars have no database"}));
  return true;
}

// Python's copy_cookie_store(): the store lives in the profile or, in newer
// profiles, in its Network directory. Content keeps it in Network.
bool CopyCookieStore(const base::FilePath& profile, const base::FilePath& target) {
  std::optional<base::FilePath> store;
  std::vector<std::string> store_files;
  for (const auto& directory : {profile, profile.AppendASCII("Network")}) {
    const PathKind kind = Inspect(directory);
    if (kind == PathKind::kUnsafe)
      return Refuse("The legacy cookie directory contains a link");
    if (kind != PathKind::kDirectory)
      continue;
    std::vector<std::string> files;
    for (const char* suffix : kSqliteSuffixes) {
      const std::string name = base::StrCat({"Cookies", suffix});
      switch (Inspect(directory.AppendASCII(name))) {
        case PathKind::kMissing:
          continue;
        case PathKind::kFile:
          files.push_back(name);
          break;
        default:
          return Refuse("Invalid legacy cookie database or sidecar");
      }
    }
    if (files.empty())
      continue;
    if (files.front() != "Cookies")
      return Refuse("Legacy cookie sidecars have no database");
    if (store)
      return Refuse("The legacy profile contains two cookie databases");
    store = directory;
    store_files = std::move(files);
  }
  if (!store)
    return true;
  const auto network = target.AppendASCII("Network");
  if (Inspect(network) != PathKind::kMissing || !base::CreateDirectory(network))
    return Refuse("Could not create the Content Network directory");
  for (const auto& name : store_files) {
    if (!CopyVerifiedFile(store->AppendASCII(name), network.AppendASCII(name)))
      return false;
  }
  return true;
}

// Python's write_browser_storage(): cookies and localStorage.
bool WriteBrowserStorage(const base::FilePath& profile,
                         const base::FilePath& target) {
  const auto storage = profile.AppendASCII(kLocalStorage);
  const PathKind kind = Inspect(storage);
  if (kind != PathKind::kMissing && kind != PathKind::kDirectory)
    return Refuse("Invalid legacy localStorage directory");
  if (!CopyCookieStore(profile, target))
    return false;
  return kind == PathKind::kMissing ||
         CopyTree(storage, target.AppendASCII(kLocalStorage));
}

// Runs a statement to completion, ignoring any rows it returns.
bool RunStatement(sql::Database& db, base::cstring_view statement_sql) {
  sql::Statement statement(db.GetUniqueStatement(statement_sql));
  if (!statement.is_valid())
    return false;
  while (statement.Step()) {
  }
  return statement.Succeeded();
}

// Opens a staged copy for the edits the Python importer makes with sqlite3.
bool OpenStagedDatabase(sql::Database& db, const base::FilePath& path) {
  // Legacy data may be malformed. Report errors through return values only.
  db.set_error_callback(base::DoNothing());
  return db.Open(path) && RunStatement(db, "PRAGMA trusted_schema=OFF");
}

bool HasSchemaVersion(sql::Database& db, int expected) {
  sql::Statement version(
      db.GetUniqueStatement("SELECT value FROM meta WHERE key = 'version'"));
  if (!version.is_valid() || !version.Step())
    return false;
  switch (version.GetColumnType(0)) {
    case sql::ColumnType::kInteger:
      return version.ColumnInt64(0) == expected;
    case sql::ColumnType::kFloat:
      return version.ColumnDouble(0) == expected;
    case sql::ColumnType::kText:
      return version.ColumnStringView(0) == base::NumberToString(expected);
    default:
      return false;
  }
}

bool PassesQuickCheck(sql::Database& db) {
  sql::Statement check(db.GetUniqueStatement("PRAGMA quick_check"));
  if (!check.is_valid() || !check.Step() ||
      check.GetColumnType(0) != sql::ColumnType::kText ||
      check.ColumnStringView(0) != "ok") {
    return false;
  }
  return !check.Step() && check.Succeeded();
}

// The required names must be plain tables, and the database must not define
// triggers.
bool HasPlainTables(sql::Database& db,
                    std::initializer_list<std::string_view> names) {
  std::map<std::string, std::string, std::less<>> types;
  sql::Statement schema(db.GetUniqueStatement("SELECT name, type FROM sqlite_schema"));
  if (!schema.is_valid())
    return false;
  while (schema.Step())
    types[schema.ColumnString(0)] = schema.ColumnString(1);
  if (!schema.Succeeded())
    return false;
  for (const auto name : names) {
    const auto type = types.find(name);
    if (type == types.end() || type->second != "table")
      return false;
  }
  sql::Statement triggers(
      db.GetUniqueStatement("SELECT 1 FROM sqlite_schema WHERE type = 'trigger'"));
  return triggers.is_valid() && !triggers.Step() && triggers.Succeeded();
}

// Checkpoints any copied WAL into the database file and removes it.
bool UseDeleteJournal(sql::Database& db) {
  sql::Statement mode(db.GetUniqueStatement("PRAGMA journal_mode=DELETE"));
  return mode.is_valid() && mode.Step() &&
         mode.GetColumnType(0) == sql::ColumnType::kText &&
         mode.ColumnStringView(0) == "delete" &&
         RunStatement(db, "PRAGMA secure_delete=ON");
}

std::set<std::string> TableColumns(sql::Database& db, base::cstring_view pragma) {
  std::set<std::string> columns;
  sql::Statement statement(db.GetUniqueStatement(pragma));
  while (statement.is_valid() && statement.Step())
    columns.insert(statement.ColumnString(1));
  return columns;
}

std::optional<int64_t> IntegerColumn(sql::Statement& statement, int column) {
  if (statement.GetColumnType(column) != sql::ColumnType::kInteger)
    return std::nullopt;
  return statement.ColumnInt64(column);
}

std::optional<std::string> TextColumn(sql::Statement& statement, int column) {
  if (statement.GetColumnType(column) != sql::ColumnType::kText)
    return std::nullopt;
  return statement.ColumnString(column);
}

// Python's retain_download_history(db, windows=True): keeps eligible
// terminal downloads as history only and discards transfer state.
bool RetainDownloadHistory(sql::Database& db) {
  const auto columns = TableColumns(db, "PRAGMA table_info(downloads)");
  const auto chain_columns =
      TableColumns(db, "PRAGMA table_info(downloads_url_chains)");
  if (!RunStatement(db, "DELETE FROM downloads_slices") ||
      !RunStatement(db,
                    "CREATE TEMP TABLE retained_downloads(id INTEGER PRIMARY KEY)")) {
    return false;
  }
  const bool supported =
      std::ranges::all_of(
          std::initializer_list<std::string_view>{
              "id", "guid", "state", "transient", "danger_type",
              "interrupt_reason", "current_path", "target_path",
              "received_bytes", "total_bytes", "embedder_download_data",
              "hash", "http_method", "etag", "last_modified"},
          [&](std::string_view name) { return columns.contains(std::string(name)); }) &&
      chain_columns.contains("id") && chain_columns.contains("chain_index") &&
      chain_columns.contains("url");
  if (supported) {
    sql::Statement candidates(db.GetUniqueStatement(
        "SELECT id, guid, state, interrupt_reason, current_path, target_path, "
        "received_bytes, total_bytes FROM downloads "
        "WHERE state IN (1, 2, 4) AND transient = 0 AND danger_type IN (0, 15) "
        "ORDER BY id"));
    sql::Statement chain(db.GetUniqueStatement(
        "SELECT chain_index, url FROM downloads_url_chains "
        "WHERE id = ? ORDER BY chain_index LIMIT 33"));
    sql::Statement retain(
        db.GetUniqueStatement("INSERT INTO retained_downloads VALUES (?)"));
    if (!candidates.is_valid() || !chain.is_valid() || !retain.is_valid())
      return false;
    std::set<std::string> guids;
    while (candidates.Step()) {
      const auto row_id = IntegerColumn(candidates, 0);
      const auto guid = TextColumn(candidates, 1);
      const int64_t state = candidates.ColumnInt64(2);
      const auto reason = IntegerColumn(candidates, 3);
      const auto current = TextColumn(candidates, 4);
      const auto target = TextColumn(candidates, 5);
      const auto received = IntegerColumn(candidates, 6);
      const auto total = IntegerColumn(candidates, 7);
      // Python requires str(uuid.UUID(guid)) == guid.lower().
      if (!guid || !IsUuidText(*guid, /*lowercase_only=*/false))
        continue;
      const std::string identity = base::ToLowerASCII(*guid);
      if (!row_id || *row_id <= 0 || *row_id >= 0xffffffffLL ||
          identity == "00000000-0000-0000-0000-000000000000" ||
          guids.contains(identity) || !target ||
          !IsLocalWindowsDownloadPath(*target) || !received || *received < 0 ||
          !total || (*total != -1 && *total < *received) || !reason ||
          *reason < 0 || *reason > 0x7fffffff) {
        continue;
      }
      if (state == 1 &&
          (*reason != 0 || !current || (!current->empty() && *current != *target) ||
           (*total != -1 && *total != *received))) {
        continue;
      }
      if (state == 4 && *reason == 0)
        continue;
      chain.Reset(/*clear_bound_vars=*/true);
      chain.BindInt64(0, *row_id);
      int64_t offset = 0;
      bool valid_chain = true;
      while (chain.Step()) {
        const auto type = chain.GetColumnType(0);
        const bool index_matches =
            (type == sql::ColumnType::kInteger && chain.ColumnInt64(0) == offset) ||
            (type == sql::ColumnType::kFloat &&
             chain.ColumnDouble(0) == static_cast<double>(offset));
        const auto url = TextColumn(chain, 1);
        if (!index_matches || !url || url->empty() ||
            !CanRestoreSessionUrl(*url) || *url == "zitter://rss/") {
          valid_chain = false;
        }
        ++offset;
      }
      if (!chain.Succeeded())
        return false;
      if (!valid_chain || offset == 0 || offset > 32)
        continue;
      guids.insert(identity);
      retain.Reset(/*clear_bound_vars=*/true);
      retain.BindInt64(0, *row_id);
      if (!retain.Run())
        return false;
    }
    if (!candidates.Succeeded())
      return false;
    sql::Statement update(db.GetUniqueStatement(
        "UPDATE downloads SET embedder_download_data = ?, hash = X'', "
        "http_method = '', etag = '', last_modified = '', "
        "current_path = CASE WHEN state = 1 THEN current_path ELSE '' END "
        "WHERE id IN (SELECT id FROM retained_downloads)"));
    if (!update.is_valid())
      return false;
    update.BindString(0, kImportedDownloadMarker);
    if (!update.Run())
      return false;
  }
  return RunStatement(db,
                      "DELETE FROM downloads_url_chains "
                      "WHERE id NOT IN (SELECT id FROM retained_downloads)") &&
         RunStatement(db,
                      "DELETE FROM downloads "
                      "WHERE id NOT IN (SELECT id FROM retained_downloads)");
}

// Python's write_history().
bool WriteHistory(const base::FilePath& profile, const base::FilePath& target) {
  bool copied = false;
  if (!CopySqliteStore(profile, target, "History", copied))
    return false;
  if (!copied)
    return true;
  sql::Database db(sql::DatabaseOptions(), "History");
  if (!OpenStagedDatabase(db, target.AppendASCII("History")))
    return Refuse("Could not open the copied History database");
  if (!HasSchemaVersion(db, kHistorySchemaVersion))
    return Refuse("Unsupported legacy History schema");
  if (!PassesQuickCheck(db))
    return Refuse("Legacy History database failed its integrity check");
  if (!HasPlainTables(db, {"downloads", "downloads_url_chains", "downloads_slices",
                           "urls", "visits"})) {
    return Refuse("Unsupported legacy History tables or triggers");
  }
  if (!UseDeleteJournal(db))
    return Refuse("Could not checkpoint the copied History database");
  sql::Transaction transaction(&db);
  if (!transaction.Begin() || !RetainDownloadHistory(db) || !transaction.Commit())
    return Refuse("Could not filter the legacy download history");
  return true;
}

// Python's write_favicons().
bool WriteFavicons(const base::FilePath& profile, const base::FilePath& target) {
  bool copied = false;
  if (!CopySqliteStore(profile, target, "Favicons", copied))
    return false;
  if (!copied)
    return true;
  sql::Database db(sql::DatabaseOptions(), "Thumbnail");
  if (!OpenStagedDatabase(db, target.AppendASCII("Favicons")))
    return Refuse("Could not open the copied Favicons database");
  if (!HasSchemaVersion(db, kFaviconSchemaVersion))
    return Refuse("Unsupported legacy Favicons schema");
  if (!PassesQuickCheck(db))
    return Refuse("Legacy Favicons database failed its integrity check");
  if (!HasPlainTables(db, {"icon_mapping", "favicons", "favicon_bitmaps"}))
    return Refuse("Unsupported legacy Favicons tables or triggers");
  if (!UseDeleteJournal(db))
    return Refuse("Could not checkpoint the copied Favicons database");
  sql::Transaction transaction(&db);
  if (!transaction.Begin() ||
      !RunStatement(db,
                    "DELETE FROM favicon_bitmaps "
                    "WHERE typeof(image_data) != 'blob' "
                    "OR length(image_data) NOT BETWEEN 1 AND 1048576 "
                    "OR typeof(width) != 'integer' OR typeof(height) != 'integer' "
                    "OR width NOT BETWEEN 1 AND 1024 OR height NOT BETWEEN 1 AND 1024") ||
      !transaction.Commit()) {
    return Refuse("Could not filter the legacy favicon bitmaps");
  }
  return true;
}

// Python's reject_app_bound_rows() for one store. Opens a disposable copy
// read-only, like the Content startup verifier, so SQLite cannot add WAL
// bookkeeping to the staged profile.
bool RejectAppBoundRows(const base::FilePath& directory,
                        std::string_view database,
                        sql::Database::Tag tag,
                        base::cstring_view query,
                        const base::FilePath& scratch_parent) {
  if (Inspect(directory.AppendASCII(database)) != PathKind::kFile)
    return true;
  base::ScopedTempDir scratch;
  if (!scratch.CreateUniqueTempDirUnderPath(scratch_parent))
    return Refuse("Could not create a scratch directory");
  for (const char* suffix : kSqliteSuffixes) {
    const std::string name = base::StrCat({database, suffix});
    const auto source = directory.AppendASCII(name);
    if (Inspect(source) == PathKind::kMissing)
      continue;
    if (!base::CopyFile(source, scratch.GetPath().AppendASCII(name)))
      return Refuse("Could not copy a legacy encrypted store for inspection");
  }
  sql::Database db(sql::DatabaseOptions().set_read_only(true), tag);
  db.set_error_callback(base::DoNothing());
  if (!db.Open(scratch.GetPath().AppendASCII(database)) ||
      !RunStatement(db, "PRAGMA trusted_schema=OFF")) {
    return Refuse(base::StrCat({"Could not inspect the legacy ", database, " store"}));
  }
  static constexpr std::string_view kAppBoundPrefix = "v20";
  sql::Statement rows(db.GetUniqueStatement(query));
  if (!rows.is_valid())
    return Refuse(base::StrCat({"Could not inspect the legacy ", database, " store"}));
  rows.BindBlob(0, base::as_byte_span(kAppBoundPrefix));
  if (rows.Step()) {
    return Refuse(
        "Legacy Windows App-Bound Encryption requires a separate migration");
  }
  if (!rows.Succeeded())
    return Refuse(base::StrCat({"Could not inspect the legacy ", database, " store"}));
  return true;
}

bool CopyRssState(const base::FilePath& profile, const base::FilePath& target) {
  std::optional<std::string> rss;
  if (!ReadLegacyFile(profile.AppendASCII(kRssState), kMaxRssBytes, rss))
    return false;
  if (!rss)
    return true;
  const auto document = ParseJson(*rss);
  if (!document || !document->is_dict() || !document->GetDict().FindList("feeds"))
    return Refuse("Invalid legacy RSS state");
  return WriteText(target.AppendASCII(kRssState), *rss);
}

// Python's copy_adblock_filter_cache().
bool CopyAdblockFilterCache(const base::FilePath& root,
                            const base::FilePath& staged) {
  const auto source = root.AppendASCII(kFilterLists);
  if (Inspect(source) == PathKind::kUnsafe)
    return Refuse("Invalid legacy filter-list cache path");
  const auto target = staged.AppendASCII(kFilterLists);
  for (const char* name :
       {"easylist.txt", "easyprivacy.txt", "antiadblock.txt", "ublock.txt"}) {
    std::optional<std::string> data;
    base::File::Info info;
    if (!ReadLegacyFile(source.AppendASCII(name), kMaxFilterListBytes, data, &info))
      return false;
    if (!data || !base::IsStringUTF8AllowingNoncharacters(*data) ||
        !(data->starts_with("[Adblock") || data->starts_with("! Title:"))) {
      continue;
    }
    const auto path = target.AppendASCII(name);
    if (!base::CreateDirectory(target) || !WriteText(path, *data))
      return false;
    // Preserve age so an old cache refreshes after migration.
    if (!base::TouchFile(path, info.last_accessed, info.last_modified))
      return Refuse("Could not preserve the filter-list cache age");
  }
  return true;
}

// One iteration of the profile loop in import_workspace_profiles().
bool StageProfile(const base::FilePath& root,
                  const base::FilePath& staged,
                  const base::FilePath& scratch_parent,
                  std::string_view directory,
                  std::string_view mapped) {
  const auto profile = root.AppendASCII(directory);
  if (Inspect(profile) != PathKind::kDirectory)
    return Refuse("The legacy profile path contains a link");
  const auto target = staged.AppendASCII(
      IsBuiltinRole(mapped) ? base::StrCat({kProfilePrefix, mapped})
                            : std::string(mapped));
  if (Inspect(target) != PathKind::kMissing || !base::CreateDirectory(target))
    return Refuse("Could not create a staged Content profile");
  std::optional<base::DictValue> stored;
  if (!ReadJsonDict(profile.AppendASCII("Preferences"), "profile preferences",
                    stored)) {
    return false;
  }
  const base::DictValue preferences =
      stored ? std::move(*stored) : base::DictValue();
  base::DictValue clipboard;
  base::DictValue clipboard_times;
  base::DictValue popups;
  base::DictValue popup_times;
  bool popups_allowed = false;
  bool has_logins = false;
  if (!ConvertClipboardRules(preferences, clipboard, clipboard_times) ||
      !ConvertPopupSettings(preferences, popups_allowed, popups, popup_times) ||
      !WriteBrowserStorage(profile, target) || !WriteHistory(profile, target) ||
      !WriteFavicons(profile, target) || !CopyRssState(profile, target) ||
      !CopySqliteStore(profile, target, "Login Data", has_logins) ||
      !RejectAppBoundRows(
          target.AppendASCII("Network"), "Cookies", "Cookie",
          "SELECT 1 FROM cookies WHERE substr(encrypted_value, 1, 3) = ? LIMIT 1",
          scratch_parent) ||
      !RejectAppBoundRows(
          target, "Login Data", "Passwords",
          "SELECT 1 FROM logins WHERE substr(password_value, 1, 3) = ? LIMIT 1",
          scratch_parent) ||
      !WritePasswordSavingPreference(target, preferences) ||
      !WritePageZoomPreference(target, preferences) ||
      !WriteSitePermissionRules(target, std::move(clipboard), std::move(popups),
                                popups_allowed, std::move(clipboard_times),
                                std::move(popup_times)) ||
      !WriteAdblockPreferences(target, preferences)) {
    return false;
  }
  if (!IsBuiltinRole(mapped)) {
    // Additional Content profiles keep their own network directories.
    const auto network = target.AppendASCII("Network");
    const auto bootstrap = target.AppendASCII("NetworkBootstrap");
    if (!base::CreateDirectory(network) ||
        Inspect(bootstrap) != PathKind::kMissing ||
        !base::CreateDirectory(bootstrap)) {
      return Refuse("Could not create the Content network directories");
    }
  }
  return true;
}

}  // namespace

bool HasLegacyWindowsWorkspace(const base::FilePath& legacy_root) {
  return legacy_root.IsAbsolute() &&
         Inspect(legacy_root) == PathKind::kDirectory &&
         Inspect(legacy_root.AppendASCII(kWorkspaceConfig)) == PathKind::kFile &&
         Inspect(legacy_root.AppendASCII(kLegacyLocalState)) == PathKind::kFile;
}

bool ImportLegacyWindowsWorkspace(const base::FilePath& legacy_root,
                                  const base::FilePath& destination) {
  if (!legacy_root.IsAbsolute() || legacy_root.ReferencesParent() ||
      !destination.IsAbsolute() || destination.ReferencesParent() ||
      destination.DirName() == destination) {
    return Refuse("Expected absolute legacy and destination paths");
  }
  base::FilePath root;
  if (Inspect(legacy_root) != PathKind::kDirectory ||
      !base::NormalizeFilePath(legacy_root, &root)) {
    return Refuse("The legacy root must be a real directory");
  }

  // Python's separate_destination() and the legacy-ancestor check of
  // prepare_macos_upgrade(). The Content lab refuses a prepared root below
  // a legacy data directory.
  const auto requested_parent = destination.DirName();
  if (Inspect(requested_parent) == PathKind::kMissing &&
      !base::CreateDirectory(requested_parent)) {
    return Refuse("Could not create the destination parent");
  }
  base::FilePath parent;
  if (Inspect(requested_parent) != PathKind::kDirectory ||
      !base::NormalizeFilePath(requested_parent, &parent)) {
    return Refuse("The destination parent must be a real directory");
  }
  const auto target = parent.Append(destination.BaseName());
  const PathKind existing = Inspect(target);
  const bool replace_empty =
      existing == PathKind::kDirectory && base::IsDirectoryEmpty(target);
  if (existing != PathKind::kMissing && !replace_empty)
    return Refuse("Destination already exists");
  if (IsSameOrAncestor(root, target) || IsSameOrAncestor(target, root))
    return Refuse("Source and destination must be separate directories");
  for (auto directory = parent;; directory = directory.DirName()) {
    for (const char* name : {kWorkspaceConfig, kLegacyLocalState}) {
      if (Inspect(directory.AppendASCII(name)) != PathKind::kMissing)
        return Refuse("The Content destination has a legacy profile ancestor");
    }
    if (directory.DirName() == directory)
      break;
  }

  // Python's inventory() refuses a source that a running browser holds.
  for (const char* lock : kRootLocks) {
    if (Inspect(root.AppendASCII(lock)) != PathKind::kMissing)
      return Refuse(base::StrCat({"The legacy data contains a browser lock: ", lock}));
  }

  std::optional<base::DictValue> config;
  std::optional<base::DictValue> local_state;
  if (!ReadJsonDict(root.AppendASCII(kWorkspaceConfig), "workspace configuration",
                    config) ||
      !ReadJsonDict(root.AppendASCII(kLegacyLocalState), "Local State",
                    local_state)) {
    return false;
  }
  if (!config || !local_state)
    return Refuse("Expected object-valued ZitterWorkspaces.json and Local State");
  auto catalogue = ReadCatalogue(root, *config);
  if (!catalogue || !AssignContentIds(*catalogue))
    return false;

  std::optional<base::DictValue> legacy_session;
  std::optional<std::string> session;
  std::optional<std::string> timeline;
  std::optional<std::string> assistant_sessions;
  std::optional<std::string> settings;
  std::optional<base::DictValue> browser_tools;
  if (!ReadJsonDict(root.AppendASCII(kLegacySession), "workspace session",
                    legacy_session) ||
      !ConvertWorkspaceSession(*config, legacy_session, *catalogue, session) ||
      !ConvertTimeline(root, *catalogue, timeline) ||
      !ConvertAssistantSessions(root, *catalogue, assistant_sessions) ||
      !ConvertAppearance(root, *catalogue, settings) ||
      !ReadJsonDict(root.AppendASCII(kBrowserTools), "browser tools",
                    browser_tools)) {
    return false;
  }
  const auto windows_local_state = ConvertLocalState(*local_state);
  if (!windows_local_state)
    return false;

  // Stage beside the destination and publish with one rename. The scoped
  // directory removes everything staged when any step fails.
  base::ScopedTempDir temporary;
  if (!temporary.CreateUniqueTempDirUnderPath(parent,
                                              FILE_PATH_LITERAL(".zitter-import-"))) {
    return Refuse("Could not create the staging directory");
  }
  const auto staged = temporary.GetPath().AppendASCII("workspace");
  if (!base::CreateDirectory(staged))
    return Refuse("Could not create the staging directory");
  for (const auto& [legacy_id, mapped] : catalogue->profile_ids) {
    if (!StageProfile(root, staged, temporary.GetPath(),
                      *Lookup(catalogue->profiles, legacy_id), mapped)) {
      return false;
    }
  }
  // Content initializes both built-in profile slots even when the legacy
  // catalogue used only one of them.
  for (const auto role : kBuiltinRoles) {
    if (ContainsValue(catalogue->profile_ids, role))
      continue;
    const auto path = staged.AppendASCII(base::StrCat({kProfilePrefix, role}));
    if (Inspect(path) != PathKind::kMissing || !base::CreateDirectory(path))
      return Refuse("Could not create a reserved Content profile");
  }
  if (!CopyAdblockFilterCache(root, staged))
    return false;
  if (session && !WriteText(staged.AppendASCII(kWorkspaceSession), *session))
    return false;
  if (assistant_sessions &&
      !WriteText(staged.AppendASCII(kAssistantSessions), *assistant_sessions)) {
    return false;
  }
  if (timeline && !WriteText(staged.AppendASCII(kWorkspaceTimeline), *timeline))
    return false;
  if (settings && !WriteText(staged.AppendASCII(kWorkspaceSettings), *settings))
    return false;
  if (browser_tools) {
    const auto tools = Serialize(*browser_tools);
    if (!tools || !WriteText(staged.AppendASCII(kBrowserTools), *tools))
      return Refuse("Could not write the browser tools settings");
  }
  if (!WriteText(staged.AppendASCII(kWindowsLocalState), *windows_local_state) ||
      !WriteText(staged.AppendASCII(kPreparedWorkspaceMarker),
                 kPreparedWorkspaceLayout)) {
    return false;
  }

  // MoveFileEx without MOVEFILE_REPLACE_EXISTING, like Python's rename() on
  // Windows, never replaces an existing destination.
  if (replace_empty && !::RemoveDirectoryW(target.value().c_str()))
    return Refuse("Could not replace the empty destination directory");
  if (!::MoveFileExW(staged.value().c_str(), target.value().c_str(), 0)) {
    if (replace_empty)
      base::CreateDirectory(target);
    return Refuse("Could not publish the Content Workspace");
  }
  return true;
}

}  // namespace zitter::content_embedder
