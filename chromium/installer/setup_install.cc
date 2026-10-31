// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Installing Zitter: the files appended to this executable as a ZIP, the
// shortcuts, Apps & features, the browser registration that Windows' Default
// apps lists, and the access the network service's AppContainer needs.
#include "chromium/installer/setup_install.h"

#include <windows.h>

#include <aclapi.h>
#include <knownfolders.h>
#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "chromium/app/zitter_version.h"
// unzip.h first: iowin32.h needs its I/O types.
#include "third_party/zlib/contrib/minizip/unzip.h"
#include "third_party/zlib/contrib/minizip/iowin32.h"

namespace zitter::setup {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Zitter";
constexpr wchar_t kClientKey[] = L"Software\\Clients\\StartMenuInternet\\Zitter";
constexpr wchar_t kProgId[] = L"ZitterHTML";
constexpr wchar_t kAppUserModelId[] = L"Zitter.Browser";  // As chromium/app/main.cc.
// The capabilities Chrome's installer grants its files, which the network
// service's AppContainer (LPAC) and other AppContainers read them with
// (chrome/installer/setup/configure_app_container_sandbox.cc).
constexpr std::array<const wchar_t*, 2> kInstallFilesCapabilities = {
    L"S-1-15-3-1024-3424233489-972189580-2057154623-747635277-1604371224-316187997-3786583170-1043257646",
    L"S-1-15-3-1024-2302894289-466761758-1166120688-1039016420-2430351297-4240214049-4028510897-3317428798",
};

HKEY Root(bool system) { return system ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

std::wstring ModulePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) { path.resize(length); return path; }
    path.resize(path.size() * 2);
  }
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) result = raw;
  CoTaskMemFree(raw);
  return result;
}

bool SetString(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value) {
  HKEY handle = nullptr;
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS)
    return false;
  const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  const bool ok = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
  RegCloseKey(handle);
  return ok;
}

bool SetDword(HKEY root, const std::wstring& key, const wchar_t* name, DWORD value) {
  HKEY handle = nullptr;
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS)
    return false;
  const bool ok = RegSetValueExW(handle, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
  RegCloseKey(handle);
  return ok;
}

std::wstring GetString(HKEY root, const wchar_t* key, const wchar_t* name) {
  DWORD bytes = 0;
  if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t))
    return {};
  std::wstring value(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return {};
  value.resize(std::min(value.size(), value.find(L'\0')));
  return value;
}

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
  return result;
}

std::wstring LastError(const wchar_t* what) {
  return std::wstring(what) + L" (" + std::to_wstring(GetLastError()) + L")";
}

// The installer's own bytes, without the ZIP appended to it: the end of its
// last section. It becomes the uninstaller, small and payload-free.
std::wstring CopyInstallerTo(const std::wstring& target) {
  HANDLE source = CreateFileW(ModulePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (source == INVALID_HANDLE_VALUE) return LastError(L"Open installer");
  auto read_at = [&](LONGLONG offset, void* into, DWORD size) {
    LARGE_INTEGER at;
    at.QuadPart = offset;
    DWORD got = 0;
    return SetFilePointerEx(source, at, nullptr, FILE_BEGIN) && ReadFile(source, into, size, &got, nullptr) && got == size;
  };
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  LONGLONG end = 0;
  bool ok = read_at(0, &dos, sizeof(dos)) && read_at(dos.e_lfanew, &nt, sizeof(nt));
  const LONGLONG sections = dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
  for (WORD i = 0; ok && i < nt.FileHeader.NumberOfSections; ++i) {
    IMAGE_SECTION_HEADER section{};
    ok = read_at(sections + i * static_cast<LONGLONG>(sizeof(section)), &section, sizeof(section));
    end = std::max<LONGLONG>(end, static_cast<LONGLONG>(section.PointerToRawData) + section.SizeOfRawData);
  }
  std::wstring error;
  if (!ok || end <= 0) error = L"Read installer";
  HANDLE output = error.empty() ? CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)
                                : INVALID_HANDLE_VALUE;
  if (error.empty() && output == INVALID_HANDLE_VALUE) error = LastError(L"Write uninstaller");
  std::vector<char> buffer(1 << 16);
  for (LONGLONG at = 0; error.empty() && at < end;) {
    const auto chunk = static_cast<DWORD>(std::min<LONGLONG>(static_cast<LONGLONG>(buffer.size()), end - at));
    DWORD written = 0;
    if (!read_at(at, buffer.data(), chunk) || !WriteFile(output, buffer.data(), chunk, &written, nullptr))
      error = LastError(L"Write uninstaller");
    at += chunk;
  }
  if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
  CloseHandle(source);
  return error;
}

// Unpacks the ZIP at this executable's end into |directory|.
std::wstring Extract(const std::wstring& directory, const std::function<void(double)>& progress) {
  zlib_filefunc64_def functions;
  fill_win32_filefunc64W(&functions);
  const std::wstring self = ModulePath();
  unzFile zip = unzOpen2_64(self.c_str(), &functions);
  if (!zip) return L"This installer carries no Zitter files";
  // A first pass sizes the progress bar.
  uint64_t total = 0, done = 0;
  for (int status = unzGoToFirstFile(zip); status == UNZ_OK; status = unzGoToNextFile(zip)) {
    unz_file_info64 info{};
    if (unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK) total += info.uncompressed_size;
  }
  std::wstring error;
  std::vector<char> name(1024), buffer(1 << 18);
  const fs::path root(directory);
  for (int status = unzGoToFirstFile(zip); error.empty() && status == UNZ_OK; status = unzGoToNextFile(zip)) {
    unz_file_info64 info{};
    if (unzGetCurrentFileInfo64(zip, &info, name.data(), static_cast<uLong>(name.size()), nullptr, 0, nullptr, 0) != UNZ_OK) {
      error = L"Read the archive";
      break;
    }
    const fs::path relative = fs::path(Widen(name.data())).lexically_normal();
    // Never outside the install directory.
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || *relative.begin() == L"..") {
      error = L"Unexpected path in the archive";
      break;
    }
    const fs::path target = root / relative;
    std::error_code ignored;
    if (!relative.has_filename()) {  // A directory entry.
      fs::create_directories(target, ignored);
      continue;
    }
    fs::create_directories(target.parent_path(), ignored);
    if (unzOpenCurrentFile(zip) != UNZ_OK) { error = L"Read the archive"; break; }
    HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) error = LastError(target.c_str());
    for (int read = 0; error.empty() && (read = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()))) != 0;) {
      DWORD written = 0;
      if (read < 0 || !WriteFile(file, buffer.data(), static_cast<DWORD>(read), &written, nullptr)) {
        error = L"Write " + target.wstring();
        break;
      }
      done += static_cast<uint64_t>(read);
      if (total) progress(static_cast<double>(done) / static_cast<double>(total));
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    unzCloseCurrentFile(zip);
  }
  unzClose(zip);
  return error;
}

bool Shortcut(const std::wstring& link, const std::wstring& target, const std::wstring& directory) {
  ComPtr<IShellLinkW> shell_link;
  ComPtr<IPropertyStore> properties;
  ComPtr<IPersistFile> file;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell_link)))) return false;
  shell_link->SetPath(target.c_str());
  shell_link->SetWorkingDirectory(directory.c_str());
  shell_link->SetIconLocation(target.c_str(), 0);
  shell_link->SetDescription(L"Zitter");
  // The taskbar pins and groups the shortcut with Zitter's windows.
  if (SUCCEEDED(shell_link.As(&properties))) {
    PROPVARIANT value;
    if (SUCCEEDED(InitPropVariantFromString(kAppUserModelId, &value))) {
      properties->SetValue(PKEY_AppUserModel_ID, value);
      properties->Commit();
      PropVariantClear(&value);
    }
  }
  return SUCCEEDED(shell_link.As(&file)) && SUCCEEDED(file->Save(link.c_str(), TRUE));
}

std::wstring StartMenuLink(bool system) {
  return KnownFolder(system ? FOLDERID_CommonPrograms : FOLDERID_Programs) + L"\\Zitter.lnk";
}
std::wstring DesktopLink(bool system) {
  return KnownFolder(system ? FOLDERID_PublicDesktop : FOLDERID_Desktop) + L"\\Zitter.lnk";
}

// Default apps lists Zitter for web links and pages: a ProgId, the
// StartMenuInternet client with its capabilities, and RegisteredApplications.
bool RegisterBrowser(bool system, const std::wstring& exe) {
  const HKEY root = Root(system);
  const std::wstring icon = exe + L",0";
  // --single-argument keeps a URL whole, whatever it holds.
  const std::wstring open = L"\"" + exe + L"\" --single-argument %1";
  const std::wstring classes = std::wstring(L"Software\\Classes\\") + kProgId;
  const std::wstring client = kClientKey;
  bool ok = SetString(root, classes, nullptr, L"Zitter HTML Document") &&
            SetString(root, classes + L"\\Application", L"ApplicationName", L"Zitter") &&
            SetString(root, classes + L"\\Application", L"ApplicationIcon", icon) &&
            SetString(root, classes + L"\\Application", L"AppUserModelId", kAppUserModelId) &&
            SetString(root, classes + L"\\DefaultIcon", nullptr, icon) &&
            SetString(root, classes + L"\\shell\\open\\command", nullptr, open) &&
            SetString(root, client, nullptr, L"Zitter") &&
            SetString(root, client + L"\\DefaultIcon", nullptr, icon) &&
            SetString(root, client + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\"") &&
            SetString(root, client + L"\\Capabilities", L"ApplicationName", L"Zitter") &&
            SetString(root, client + L"\\Capabilities", L"ApplicationIcon", icon) &&
            SetString(root, client + L"\\Capabilities", L"ApplicationDescription",
                      L"A browser of workspaces, each with its own color, tabs and profile.") &&
            SetString(root, client + L"\\Capabilities\\StartMenu", L"StartMenuInternet", L"Zitter");
  for (const wchar_t* scheme : {L"http", L"https"})
    ok = ok && SetString(root, client + L"\\Capabilities\\URLAssociations", scheme, kProgId);
  for (const wchar_t* extension : {L".htm", L".html", L".shtml", L".xht", L".xhtml", L".svg", L".webp"})
    ok = ok && SetString(root, client + L"\\Capabilities\\FileAssociations", extension, kProgId);
  ok = ok && SetString(root, L"Software\\RegisteredApplications", L"Zitter", client + L"\\Capabilities");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}

void UnregisterBrowser(bool system) {
  const HKEY root = Root(system);
  RegDeleteTreeW(root, (std::wstring(L"Software\\Classes\\") + kProgId).c_str());
  RegDeleteTreeW(root, kClientKey);
  HKEY applications = nullptr;
  if (RegOpenKeyExW(root, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &applications) == ERROR_SUCCESS) {
    RegDeleteValueW(applications, L"Zitter");
    RegCloseKey(applications);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool RegisterUninstall(const Placement& placement, uint64_t bytes) {
  const HKEY root = Root(placement.system);
  const std::wstring& dir = placement.directory;
  const std::wstring uninstall = L"\"" + dir + L"\\zitter_setup.exe\" --uninstall" + (placement.system ? L" --system" : L"");
  return SetString(root, kUninstallKey, L"DisplayName", L"Zitter") &&
         SetString(root, kUninstallKey, L"DisplayVersion", ZITTER_VERSION_SHORT) &&
         SetString(root, kUninstallKey, L"Publisher", L"Zitter contributors") &&
         SetString(root, kUninstallKey, L"DisplayIcon", dir + L"\\zitter.exe,0") &&
         SetString(root, kUninstallKey, L"InstallLocation", dir) &&
         SetString(root, kUninstallKey, L"UninstallString", uninstall) &&
         SetString(root, kUninstallKey, L"QuietUninstallString", uninstall + L" --quiet") &&
         SetDword(root, kUninstallKey, L"EstimatedSize", static_cast<DWORD>(bytes / 1024)) &&
         SetDword(root, kUninstallKey, L"NoModify", 1) && SetDword(root, kUninstallKey, L"NoRepair", 1);
}

// Read and execute for the two install-files capabilities, inherited by every
// file below |directory|.
bool GrantAppContainerAccess(const std::wstring& directory) {
  std::array<PSID, 2> sids = {};
  std::array<EXPLICIT_ACCESSW, 2> entries = {};
  bool ok = true;
  for (size_t i = 0; i < sids.size(); ++i) {
    ok = ok && ConvertStringSidToSidW(kInstallFilesCapabilities.at(i), &sids.at(i));
    if (!ok) break;
    auto& entry = entries.at(i);
    entry.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sids.at(i));
  }
  PACL old_acl = nullptr, new_acl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  std::wstring path = directory;
  ok = ok && GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                   &old_acl, nullptr, &descriptor) == ERROR_SUCCESS &&
       SetEntriesInAclW(static_cast<ULONG>(entries.size()), entries.data(), old_acl, &new_acl) == ERROR_SUCCESS &&
       SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_acl,
                             nullptr) == ERROR_SUCCESS;
  if (new_acl) LocalFree(new_acl);
  if (descriptor) LocalFree(descriptor);
  for (PSID sid : sids) if (sid) LocalFree(sid);
  return ok;
}

uint64_t DirectorySize(const std::wstring& directory) {
  uint64_t bytes = 0;
  std::error_code error;
  for (const auto& entry : fs::recursive_directory_iterator(directory, error))
    if (entry.is_regular_file(error)) bytes += entry.file_size(error);
  return bytes;
}
}  // namespace

std::wstring DefaultDirectory(bool system) {
  return system ? KnownFolder(FOLDERID_ProgramFiles) + L"\\Zitter"
                : KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\Zitter";
}

std::optional<Existing> FindExisting() {
  for (bool system : {false, true}) {
    const std::wstring location = GetString(Root(system), kUninstallKey, L"InstallLocation");
    if (location.empty()) continue;
    return Existing{{system, location}, GetString(Root(system), kUninstallKey, L"DisplayVersion")};
  }
  return std::nullopt;
}

std::wstring PayloadVersion() { return ZITTER_VERSION_SHORT; }

bool IsRunning(const std::wstring& directory) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  const std::wstring prefix = fs::path(directory).lexically_normal().wstring() + L"\\";
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool running = false;
  for (BOOL more = Process32FirstW(snapshot, &entry); more && !running; more = Process32NextW(snapshot, &entry)) {
    if (CompareStringOrdinal(entry.szExeFile, -1, L"zitter.exe", -1, TRUE) != CSTR_EQUAL) continue;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
    if (!process) continue;
    std::wstring image(MAX_PATH * 2, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(process, 0, image.data(), &size)) {
      image.resize(size);
      running = image.size() > prefix.size() &&
                CompareStringOrdinal(image.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                     static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    }
    CloseHandle(process);
  }
  CloseHandle(snapshot);
  return running;
}

bool IsElevated() {
  HANDLE token = nullptr;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const bool elevated = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
                        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                        elevation.TokenIsElevated;
  if (token) CloseHandle(token);
  return elevated;
}

std::wstring Install(const Placement& placement, bool desktop_shortcut, const std::function<void(double)>& progress) {
  if (IsRunning(placement.directory)) return L"running";
  std::error_code error;
  fs::create_directories(placement.directory, error);
  if (error) return L"Create " + placement.directory;
  if (auto failure = Extract(placement.directory, [&](double done) { progress(0.95 * done); }); !failure.empty())
    return failure;
  if (auto failure = CopyInstallerTo(placement.directory + L"\\zitter_setup.exe"); !failure.empty()) return failure;
  const std::wstring exe = placement.directory + L"\\zitter.exe";
  // Without it, the network service runs outside its sandbox.
  GrantAppContainerAccess(placement.directory);
  Shortcut(StartMenuLink(placement.system), exe, placement.directory);
  if (desktop_shortcut) Shortcut(DesktopLink(placement.system), exe, placement.directory);
  else DeleteFileW(DesktopLink(placement.system).c_str());
  if (!RegisterBrowser(placement.system, exe)) return L"Register Zitter as a browser";
  if (!RegisterUninstall(placement, DirectorySize(placement.directory))) return L"Register in Apps & features";
  progress(1);
  return {};
}

std::wstring Uninstall(const Placement& placement, bool remove_data, const std::function<void(double)>& progress) {
  if (IsRunning(placement.directory)) return L"running";
  DeleteFileW(StartMenuLink(placement.system).c_str());
  DeleteFileW(DesktopLink(placement.system).c_str());
  UnregisterBrowser(placement.system);
  RegDeleteTreeW(Root(placement.system), kUninstallKey);
  // Everything but this running copy, which deletes itself once it exits.
  const fs::path self = fs::path(ModulePath()).lexically_normal();
  // Listed first: removing while iterating invalidates the iterator.
  std::vector<fs::path> files, directories;
  std::error_code error;
  for (const auto& entry : fs::recursive_directory_iterator(placement.directory, error))
    (entry.is_directory(error) ? directories : files).push_back(entry.path());
  for (size_t i = 0; i < files.size(); ++i) {
    if (files.at(i).lexically_normal() != self) fs::remove(files.at(i), error);
    progress(0.9 * static_cast<double>(i + 1) / static_cast<double>(files.size()));
  }
  // Deepest first.
  for (auto it = directories.rbegin(); it != directories.rend(); ++it) fs::remove(*it, error);
  if (remove_data) fs::remove_all(KnownFolder(FOLDERID_LocalAppData) + L"\\Zitter", error);
  fs::remove(placement.directory, error);
  progress(1);
  return {};
}

void DeleteSelfLater(const std::wstring& directory) {
  // A running executable can't delete itself; a hidden shell does, after it
  // exits. Run from the install folder, it removes what the run kept loaded.
  const fs::path self(ModulePath());
  const bool inside = self.parent_path().lexically_normal() == fs::path(directory).lexically_normal();
  std::wstring command = L"cmd.exe /c ping 127.0.0.1 -n 3 > nul & del /f /q \"" + self.wstring() + L"\"";
  if (inside) command += L" & rmdir /s /q \"" + directory + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                     &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
}

}  // namespace zitter::setup
