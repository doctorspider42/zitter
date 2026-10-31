// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/jump_list_win.h"

#include <windows.h>

// shobjidl.h first: propkey.h needs its PROPERTYKEY. No shlwapi.h, whose
// StrCat macro clashes with base's.
#include <shobjidl.h>
#include <propkey.h>

#include <cwchar>
#include <wrl/client.h>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/common/chrome_switches.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/workspace_frame_win.h"

namespace zitter::prototype {
namespace {
using Microsoft::WRL::ComPtr;

// zitter.exe with the command, and the profile directory this Zitter uses, so
// a development build's list opens its own profile.
ComPtr<IShellLinkW> Link(const std::wstring& command, const std::u16string& title) {
  ComPtr<IShellLinkW> link;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return nullptr;
  base::FilePath exe;
  base::PathService::Get(base::FILE_EXE, &exe);
  base::CommandLine arguments(base::CommandLine::NO_PROGRAM);
  arguments.AppendSwitchNative(kZitterCommandSwitch, command);
  const auto* current = base::CommandLine::ForCurrentProcess();
  if (current->HasSwitch(switches::kUserDataDir))
    arguments.AppendSwitchPath(switches::kUserDataDir, current->GetSwitchValuePath(switches::kUserDataDir));
  link->SetPath(exe.value().c_str());
  link->SetArguments(arguments.GetArgumentsString().c_str());
  link->SetIconLocation(exe.value().c_str(), 0);
  ComPtr<IPropertyStore> properties;
  if (FAILED(link.As(&properties))) return nullptr;
  // InitPropVariantFromString, without propvarutil.h: a COM-allocated copy
  // that PropVariantClear frees.
  const size_t bytes = (title.size() + 1) * sizeof(wchar_t);
  auto* text = static_cast<wchar_t*>(::CoTaskMemAlloc(bytes));
  if (!text) return nullptr;
  // SAFETY: |text| holds title.size() + 1 characters, the title and its NUL.
  UNSAFE_BUFFERS(std::wmemcpy(text, base::as_wcstr(title), title.size() + 1));
  PROPVARIANT value{};
  value.vt = VT_LPWSTR;
  value.pwszVal = text;
  const bool titled = SUCCEEDED(properties->SetValue(PKEY_Title, value)) && SUCCEEDED(properties->Commit());
  ::PropVariantClear(&value);
  return titled ? link : nullptr;
}

// A line between task groups.
ComPtr<IShellLinkW> Separator() {
  ComPtr<IShellLinkW> link;
  ComPtr<IPropertyStore> properties;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      FAILED(link.As(&properties)))
    return nullptr;
  PROPVARIANT value{};
  value.vt = VT_BOOL;
  value.boolVal = VARIANT_TRUE;
  return SUCCEEDED(properties->SetValue(PKEY_AppUserModel_IsDestListSeparator, value)) &&
                 SUCCEEDED(properties->Commit())
             ? link
             : nullptr;
}

ComPtr<IObjectCollection> Collection() {
  ComPtr<IObjectCollection> collection;
  ::CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&collection));
  return collection;
}

void WriteJumpList(std::vector<JumpListSpace> spaces, std::u16string spaces_title, std::u16string new_tab,
                   std::u16string palette) {
  ComPtr<ICustomDestinationList> list;
  if (FAILED(::CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list)))) return;
  // The same ID as the window and the installer's shortcuts (main.cc).
  list->SetAppID(L"Zitter.Browser");
  UINT slots = 0;
  ComPtr<IObjectArray> removed;
  if (FAILED(list->BeginList(&slots, IID_PPV_ARGS(&removed)))) return;
  auto add_spaces = [&](IObjectCollection* collection) {
    for (const auto& space : spaces)
      if (auto link = Link(L"space:" + base::UTF8ToWide(space.id), space.name)) collection->AddObject(link.Get());
  };
  // Windows refuses custom categories while „Show recently opened items in
  // Jump Lists” is off (Start_TrackDocs); tasks always show, so the spaces
  // move there, above a separator.
  bool spaces_listed = spaces.empty();
  if (auto category = Collection(); category && !spaces.empty()) {
    add_spaces(category.Get());
    ComPtr<IObjectArray> items;
    spaces_listed = SUCCEEDED(category.As(&items)) && SUCCEEDED(list->AppendCategory(base::as_wcstr(spaces_title), items.Get()));
  }
  if (auto tasks = Collection()) {
    if (!spaces_listed) {
      add_spaces(tasks.Get());
      if (auto separator = Separator()) tasks->AddObject(separator.Get());
    }
    if (auto link = Link(L"new_tab", new_tab)) tasks->AddObject(link.Get());
    if (auto link = Link(L"palette", palette)) tasks->AddObject(link.Get());
    ComPtr<IObjectArray> items;
    if (SUCCEEDED(tasks.As(&items))) list->AddUserTasks(items.Get());
  }
  list->CommitList();
}

// The running window's executable, so a development build never drives an
// installed Zitter or the other way round.
std::wstring ImagePath(DWORD process_id) {
  HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (!process) return {};
  std::wstring path(MAX_PATH * 2, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!::QueryFullProcessImageNameW(process, 0, path.data(), &size)) size = 0;
  ::CloseHandle(process);
  path.resize(size);
  return path;
}

struct Search {
  std::wstring exe;
  HWND found = nullptr;
};

BOOL CALLBACK FindTarget(HWND hwnd, LPARAM data) {
  auto* search = reinterpret_cast<Search*>(data);
  if (!::GetPropW(hwnd, kWorkspaceCommandProperty)) return TRUE;
  DWORD process_id = 0;
  ::GetWindowThreadProcessId(hwnd, &process_id);
  if (::CompareStringOrdinal(ImagePath(process_id).c_str(), -1, search->exe.c_str(), -1, TRUE) != CSTR_EQUAL) return TRUE;
  search->found = hwnd;
  return FALSE;
}
}  // namespace

void UpdateJumpList(std::vector<JumpListSpace> spaces) {
  // Shell COM objects want an STA and may block on the disk.
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> runner(
      base::ThreadPool::CreateCOMSTATaskRunner({base::MayBlock(), base::TaskPriority::BEST_EFFORT}));
  (*runner)->PostTask(FROM_HERE, base::BindOnce(&WriteJumpList, std::move(spaces), T(u"Przestrzenie"),
                                                T(u"Nowa karta"), T(u"Command Center")));
}

bool ForwardCommandToRunningZitter(const base::CommandLine& command_line) {
  const std::wstring command = command_line.GetSwitchValueNative(kZitterCommandSwitch);
  if (command.empty()) return false;
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe)) return false;
  Search search{exe.value()};
  ::EnumWindows(&FindTarget, reinterpret_cast<LPARAM>(&search));
  if (!search.found) return false;
  DWORD process_id = 0;
  ::GetWindowThreadProcessId(search.found, &process_id);
  // Let the running Zitter come to the front, as Chrome's process singleton does.
  ::AllowSetForegroundWindow(process_id);
  const std::string utf8 = base::WideToUTF8(command);
  COPYDATASTRUCT data{kWorkspaceCommandMessage, static_cast<DWORD>(utf8.size()), const_cast<char*>(utf8.data())};
  DWORD_PTR result = 0;
  return ::SendMessageTimeoutW(search.found, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000,
                               &result) &&
         result;
}
}  // namespace zitter::prototype
