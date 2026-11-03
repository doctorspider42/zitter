// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/app/updater_win.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/task/single_thread_task_runner.h"
#include "base/version.h"
#include "base/win/registry.h"
#include "chromium/app/zitter_version.h"
#include "crypto/hash.h"
#include "url/gurl.h"

namespace zitter {
namespace {
constexpr char kLatest[] = "https://api.github.com/repos/doctorspider42/zitter/releases/latest";
constexpr int64_t kMaxInstaller = 600 * 1024 * 1024;
constexpr wchar_t kInstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Zitter";
struct HttpCloser { void operator()(void* handle) const { WinHttpCloseHandle(handle); } };
using Http = std::unique_ptr<void, HttpCloser>;

std::optional<std::pair<base::FilePath, bool>> Installation() {
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe)) return std::nullopt;
  for (bool system : {false, true}) {
    base::win::RegKey key(system ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, kInstallKey, KEY_READ);
    std::wstring path;
    if (key.ReadValue(L"InstallLocation", &path) == ERROR_SUCCESS &&
        base::FilePath::CompareEqualIgnoreCase(base::FilePath(path).Append(L"zitter.exe").value(), exe.value()))
      return std::pair(base::FilePath(path), system);
  }
  return std::nullopt;
}

// WinHTTP uses the system proxy and certificate checks. Its default redirect
// policy refuses HTTPS-to-HTTP downgrades (GitHub assets use HTTPS redirects).
bool Fetch(const std::string& address, int64_t limit, std::string* text,
           base::File* destination = nullptr) {
  GURL url(address);
  if (!url.SchemeIs("https") || url.has_username() || url.has_password()) return false;
  Http session(WinHttpOpen(L"Zitter/" ZITTER_VERSION_SHORT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) return false;
  WinHttpSetTimeouts(session.get(), 15000, 15000, 15000, 30000);
  Http connection(WinHttpConnect(session.get(), base::UTF8ToWide(url.host()).c_str(),
                                 INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) return false;
  Http request(WinHttpOpenRequest(connection.get(), L"GET",
      base::UTF8ToWide(url.PathForRequest()).c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request || !WinHttpSendRequest(request.get(),
      L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.get(), nullptr)) return false;
  DWORD status = 0, length = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) || status != 200) return false;
  int64_t total = 0;
  std::array<char, 64 * 1024> buffer;
  for (;;) {
    DWORD received = 0;
    if (!WinHttpReadData(request.get(), buffer.data(), buffer.size(), &received)) return false;
    if (!received) return true;
    total += received;
    if (total > limit) return false;
    auto bytes = base::span(buffer).first(received);
    if (destination) {
      if (!destination->WriteAtCurrentPosAndCheck(base::as_byte_span(bytes))) return false;
    } else {
      text->append(bytes.data(), bytes.size());
    }
  }
}

base::FilePath InstallerPath(const base::FilePath& cache, const updates::Release& release) {
  return cache.AppendASCII("Zitter-" + release.version + "-setup.exe");
}

base::File OpenVerifiedInstaller(const base::FilePath& path, const updates::Release& release) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WIN_EXCLUSIVE_WRITE);
  if (!file.IsValid() || file.GetLength() != release.size) return {};
  std::array<uint8_t, crypto::hash::kSha256Size> hash;
  if (!crypto::hash::HashFile(crypto::hash::kSha256, &file, hash) ||
      !base::EqualsCaseInsensitiveASCII(base::HexEncode(hash), release.sha256)) return {};
  return file;
}

std::optional<updates::Release> Download(const base::FilePath& cache, std::string_view current_version) {
  std::string json;
  if (!Fetch(kLatest, 2 * 1024 * 1024, &json)) return std::nullopt;
  auto release = updates::ParseRelease(json, current_version);
  if (!release || !base::CreateDirectory(cache)) return std::nullopt;
  const auto installer = InstallerPath(cache, *release);
  if (updates::VerifyInstaller(installer, *release)) return release;
  const auto partial = installer.AddExtension(FILE_PATH_LITERAL("partial"));
  {
    base::File file(partial, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    if (!file.IsValid() || !Fetch(release->url, release->size, nullptr, &file) || !file.Flush()) {
      file.Close();
      base::DeleteFile(partial);
      return std::nullopt;
    }
  }
  if (!updates::VerifyInstaller(partial, *release) || !base::ReplaceFile(partial, installer, nullptr)) {
    base::DeleteFile(partial);
    return std::nullopt;
  }
  return release;
}
}  // namespace

namespace updates {
std::optional<Release> DownloadLatest(const base::FilePath& cache, std::string current_version) {
  return Download(cache, current_version);
}
std::optional<Release> ParseRelease(std::string_view json, std::string_view current_version) {
  auto value = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!value || value->FindBool("draft").value_or(true) ||
      value->FindBool("prerelease").value_or(true)) return std::nullopt;
  const auto* tag = value->FindString("tag_name");
  if (!tag || !tag->starts_with('v')) return std::nullopt;
  const std::string number = tag->substr(1);
  base::Version version(number), current(current_version);
  if (!version.IsValid() || version.components().size() != 3 ||
      version.GetString() != number || !current.IsValid() || version.CompareTo(current) <= 0) return std::nullopt;
  const auto* assets = value->FindList("assets");
  if (!assets) return std::nullopt;
  const std::string name = "Zitter-" + *tag + "-windows-x64-setup.exe";
  const std::string expected_url = "https://github.com/doctorspider42/zitter/releases/download/" + *tag + "/" + name;
  std::optional<Release> result;
  for (const auto& asset : *assets) {
    if (!asset.is_dict()) continue;
    const auto& fields = asset.GetDict();
    const auto* asset_name = fields.FindString("name");
    if (!asset_name || *asset_name != name) continue;
    if (result) return std::nullopt;
    const auto* url = fields.FindString("browser_download_url");
    const auto* digest = fields.FindString("digest");
    const auto size = fields.FindDouble("size");
    if (!url || *url != expected_url || !digest || !digest->starts_with("sha256:") ||
        digest->size() != 71 || !size || *size < 1 || *size > kMaxInstaller ||
        *size != static_cast<int64_t>(*size)) return std::nullopt;
    std::array<uint8_t, 32> decoded;
    if (!base::HexStringToSpan(digest->substr(7), decoded)) return std::nullopt;
    result = Release{number, *url, base::ToLowerASCII(digest->substr(7)), static_cast<int64_t>(*size)};
  }
  return result;
}

bool VerifyInstaller(const base::FilePath& path, const Release& release) {
  return OpenVerifiedInstaller(path, release).IsValid();
}
}  // namespace updates

WindowsUpdater::WindowsUpdater(const base::FilePath& user_data, Ready ready)
    : cache_(user_data.Append(FILE_PATH_LITERAL("Updates"))), ready_(std::move(ready)) {}
WindowsUpdater::~WindowsUpdater() = default;

void WindowsUpdater::Start() {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch("disable-zitter-updates") || command->HasSwitch("zitter-app-benchmark")) return;
  // A portable copy must never update some other registered installation.
  const auto installation = Installation();
  if (!installation) return;
  directory_ = installation->first;
  // Leave first paint/startup alone; retry every six hours while running.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
      base::BindOnce(&WindowsUpdater::Check, weak_factory_.GetWeakPtr()), base::Seconds(30));
  timer_.Start(FROM_HERE, base::Hours(6), this, &WindowsUpdater::Check);
}

void WindowsUpdater::Check() {
  if (busy_ || pending_) return;
  busy_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT, base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&updates::DownloadLatest, cache_, base::WideToUTF8(ZITTER_VERSION_SHORT)),
      base::BindOnce(&WindowsUpdater::Checked, weak_factory_.GetWeakPtr()));
}
void WindowsUpdater::Checked(std::optional<updates::Release> release) {
  busy_ = false;
  pending_ = std::move(release);
  if (pending_) ready_.Run(pending_->version);
}
void WindowsUpdater::RequestInstall() {
  install_requested_ = pending_.has_value();
}
void WindowsUpdater::InstallOnExit() {
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  if (!install_requested_ || !pending_ || directory_.empty()) return;
  // Hashing hundreds of MB must not block the browser's UI/shutdown thread.
  base::CommandLine command(base::CommandLine::ForCurrentProcess()->GetProgram());
  command.AppendSwitchPath("zitter-update-installer", InstallerPath(cache_, *pending_));
  command.AppendSwitchASCII("zitter-update-sha256", pending_->sha256);
  command.AppendSwitchASCII("zitter-update-version", pending_->version);
  command.AppendSwitchASCII("zitter-update-size", base::NumberToString(pending_->size));
  command.AppendSwitchASCII("zitter-update-parent", base::NumberToString(base::GetCurrentProcId()));
  base::LaunchOptions options;
  options.start_hidden = true;
  if (!base::LaunchProcess(command, options).IsValid()) LOG(WARNING) << "Could not start Zitter update";
}

std::optional<int> RunWindowsUpdateHelper() {
  const auto* args = base::CommandLine::ForCurrentProcess();
  if (!args->HasSwitch("zitter-update-installer")) return std::nullopt;
  const auto installation = Installation();
  if (!installation) return 1;
  updates::Release release;
  release.version = args->GetSwitchValueASCII("zitter-update-version");
  release.sha256 = args->GetSwitchValueASCII("zitter-update-sha256");
  const base::Version candidate(release.version);
  if (!candidate.IsValid() || candidate.components().size() != 3 ||
      candidate.GetString() != release.version) return 1;
  uint32_t parent = 0;
  if (!base::StringToInt64(args->GetSwitchValueASCII("zitter-update-size"), &release.size) ||
      release.size <= 0 || release.size > kMaxInstaller || release.sha256.size() != 64 ||
      !base::StringToUint(args->GetSwitchValueASCII("zitter-update-parent"), &parent) ||
      !parent || parent == base::GetCurrentProcId()) return 1;
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent);
  if (process) {
    const DWORD result = WaitForSingleObject(process, 120000);
    CloseHandle(process);
    if (result != WAIT_OBJECT_0) return 1;
  } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
    return 1;
  }
  // Another process may have installed this version while we were waiting.
  base::win::RegKey key(installation->second ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                       kInstallKey, KEY_READ);
  std::wstring installed_version;
  if (key.ReadValue(L"DisplayVersion", &installed_version) != ERROR_SUCCESS) return 1;
  const base::Version installed(base::WideToUTF8(installed_version));
  if (!installed.IsValid() || candidate.CompareTo(installed) <= 0) return 1;
  const auto installer = args->GetSwitchValuePath("zitter-update-installer");
  // Deny writes/deletion until CreateProcess has loaded the verified image.
  auto verified = OpenVerifiedInstaller(installer, release);
  if (!verified.IsValid()) return 1;
  base::CommandLine command(installer);
  command.AppendArg("--install-now");
  command.AppendArg("--quiet");
  command.AppendArg("--preserve-shortcuts");
  command.AppendArg("--update-only");
  command.AppendArg("--dir");
  command.AppendArgPath(installation->first);
  command.AppendArg("--wait-for-process");
  command.AppendArg(base::NumberToString(base::GetCurrentProcId()));
  if (installation->second) command.AppendArg("--system");
  base::LaunchOptions options;
  options.start_hidden = true;
  return base::LaunchProcess(command, options).IsValid() ? 0 : 1;
}
}  // namespace zitter
