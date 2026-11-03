// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/installer/setup_runtime.h"

#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlguid.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>

namespace zitter::setup {
namespace {
std::vector<DWORD> BrowserProcesses(const std::wstring& directory) {
  std::vector<DWORD> processes;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return processes;
  const auto executable = (std::filesystem::path(directory) / L"zitter.exe").lexically_normal().wstring();
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
    if (CompareStringOrdinal(entry.szExeFile, -1, L"zitter.exe", -1, TRUE) != CSTR_EQUAL) continue;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
    if (!process) continue;
    std::wstring image(32768, L'\0');
    DWORD size = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(process, 0, image.data(), &size)) {
      image.resize(size);
      if (CompareStringOrdinal(image.c_str(), -1, executable.c_str(), -1, TRUE) == CSTR_EQUAL)
        processes.push_back(entry.th32ProcessID);
    }
    CloseHandle(process);
  }
  CloseHandle(snapshot);
  return processes;
}

BOOL CALLBACK CloseWindow(HWND window, LPARAM context) {
  const auto& processes = *reinterpret_cast<const std::vector<DWORD>*>(context);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  // Owned windows can be dialogs. Let the main window's normal close handler
  // resolve those, including unsaved form prompts, instead of dismissing them.
  if (!GetWindow(window, GW_OWNER) &&
      std::find(processes.begin(), processes.end(), process) != processes.end())
    PostMessageW(window, WM_CLOSE, 0, 0);
  return TRUE;
}
}  // namespace

bool IsRunning(const std::wstring& directory) {
  return !BrowserProcesses(directory).empty();
}

bool CloseBrowser(const std::wstring& directory, DWORD timeout_ms) {
  const auto processes = BrowserProcesses(directory);
  if (processes.empty()) return true;
  EnumWindows(CloseWindow, reinterpret_cast<LPARAM>(&processes));
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;
  do {
    if (!IsRunning(directory)) return true;
    Sleep(50);
  } while (GetTickCount64() < deadline);
  return !IsRunning(directory);
}

DWORD CommitInstalledFile(const std::wstring& temporary, const std::wstring& target) {
  if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return ERROR_SUCCESS;
  const DWORD failure = GetLastError();
  std::error_code error;
  const auto incoming_size = std::filesystem::file_size(temporary, error);
  if (error) return failure;
  const auto current_size = std::filesystem::file_size(target, error);
  if (error || incoming_size != current_size) return failure;
  std::ifstream incoming(std::filesystem::path(temporary), std::ios::binary);
  std::ifstream current(std::filesystem::path(target), std::ios::binary);
  if (!incoming || !current) return failure;
  std::array<char, 65536> a{}, b{};
  for (uintmax_t remaining = incoming_size; remaining;) {
    const auto bytes = static_cast<std::streamsize>(std::min<uintmax_t>(remaining, a.size()));
    incoming.read(a.data(), bytes);
    current.read(b.data(), bytes);
    if (incoming.gcount() != bytes || current.gcount() != bytes || a != b) return failure;
    remaining -= static_cast<uintmax_t>(bytes);
  }
  incoming.close();
  return DeleteFileW(temporary.c_str()) ? ERROR_SUCCESS : GetLastError();
}

HRESULT LaunchFromExplorer(const std::wstring& executable,
                        const std::wstring& directory,
                        const std::wstring& arguments) {
  using Microsoft::WRL::ComPtr;
  ComPtr<IShellWindows> windows;
  HRESULT result = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
  if (FAILED(result)) return result;
  VARIANT location{}, root{};
  location.vt = VT_I4;
  location.lVal = CSIDL_DESKTOP;
  long desktop = 0;
  ComPtr<IDispatch> dispatch;
  result = windows->FindWindowSW(&location, &root, SWC_DESKTOP, &desktop, SWFO_NEEDDISPATCH, &dispatch);
  if (result != S_OK || !dispatch) return FAILED(result) ? result : E_FAIL;
  ComPtr<IServiceProvider> provider;
  ComPtr<IShellBrowser> browser;
  ComPtr<IShellView> view;
  ComPtr<IDispatch> background;
  ComPtr<IShellFolderViewDual> folder;
  ComPtr<IDispatch> application;
  ComPtr<IShellDispatch2> shell;
  if (FAILED(result = dispatch.As(&provider)) ||
      FAILED(result = provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
      FAILED(result = browser->QueryActiveShellView(&view)) ||
      // GetItemObject supports IDispatch here, not IShellFolderViewDual.
      FAILED(result = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) ||
      FAILED(result = background.As(&folder)) ||
      FAILED(result = folder->get_Application(&application)) ||
      FAILED(result = application.As(&shell))) return result;
  BSTR file = SysAllocString(executable.c_str());
  VARIANT args{}, working_directory{}, operation{}, show{};
  args.vt = working_directory.vt = operation.vt = VT_BSTR;
  args.bstrVal = SysAllocString(arguments.c_str());
  working_directory.bstrVal = SysAllocString(directory.c_str());
  operation.bstrVal = SysAllocString(L"open");
  show.vt = VT_I4;
  show.lVal = SW_SHOWNORMAL;
  const bool allocated = file && args.bstrVal && working_directory.bstrVal && operation.bstrVal;
  result = allocated ? shell->ShellExecute(file, args, working_directory, operation, show) : E_OUTOFMEMORY;
  VariantClear(&args);
  VariantClear(&working_directory);
  VariantClear(&operation);
  SysFreeString(file);
  return result;
}
}  // namespace zitter::setup
