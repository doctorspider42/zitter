// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The Windows Hello and credential validation flows follow Chromium's
// BSD-licensed authenticator_win.cc and password_manager_util_win.cc.

#include "password_authenticator_win.h"

#include <windows.h>

#include <UserConsentVerifierInterop.h>
#include <objbase.h>
#include <windows.foundation.h>
#include <windows.security.credentials.ui.h>
#include <wrl/client.h>

#include <string>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/hang_watcher.h"
#include "base/win/access_token.h"
#include "base/win/core_winrt_util.h"
#include "base/win/hstring_reference.h"
#include "base/win/ntsecapi_shim.h"
#include "base/win/post_async_results.h"
#include "base/win/scoped_handle.h"
#include "base/win/wincred_shim.h"
#include "base/win/windows_version.h"
#include "components/device_reauth/device_authenticator.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace zitter::content_embedder {
namespace {

using ABI::Windows::Foundation::IAsyncInfo;
using ABI::Windows::Foundation::IAsyncOperation;
using ABI::Windows::Security::Credentials::UI::IUserConsentVerifierStatics;
using ABI::Windows::Security::Credentials::UI::UserConsentVerificationResult;
using enum UserConsentVerificationResult;
using Microsoft::WRL::ComPtr;
using HelloOperation = IAsyncOperation<UserConsentVerificationResult>;

bool IsCurrentAccount(ULONG package, void* credentials, ULONG size) {
  auto current = base::win::AccessToken::FromCurrentProcess();
  HANDLE lsa = nullptr;
  if (!current || ::LsaConnectUntrusted(&lsa) != 0)
    return false;
  base::ScopedClosureRunner disconnect(base::BindOnce(
      [](HANDLE lsa) { ::LsaDeregisterLogonProcess(lsa); }, lsa));

  char origin_name[] = "Zitter";
  LSA_STRING origin = {sizeof(origin_name) - 1, sizeof(origin_name), origin_name};
  TOKEN_SOURCE source = {};
  base::span(source.SourceName).copy_prefix_from(base::span(origin_name));
  if (!::AllocateLocallyUniqueId(&source.SourceIdentifier))
    return false;

  void* profile = nullptr;
  ULONG profile_size = 0;
  LUID logon_id = {};
  HANDLE token = nullptr;
  QUOTA_LIMITS quotas = {};
  NTSTATUS substatus = 0;
  const NTSTATUS status = ::LsaLogonUser(
      lsa, &origin, Interactive, package, credentials, size, nullptr, &source,
      &profile, &profile_size, &logon_id, &token, &quotas, &substatus);
  if (profile)
    ::LsaFreeReturnBuffer(profile);
  base::win::ScopedHandle logon(token);
  if (status != 0 || !logon.is_valid())
    return false;

  auto authenticated = base::win::AccessToken::FromToken(std::move(logon));
  return authenticated && authenticated->User() == current->User();
}

bool AuthenticateWithWindowsCredentials(HWND owner, std::wstring reason) {
  CREDUI_INFOW info = {};
  info.cbSize = sizeof(info);
  info.hwndParent = owner;
  info.pszMessageText = reason.c_str();
  info.pszCaptionText = L"Zitter";

  // The operating system owns this modal dialog and may wait for the user.
  base::HangWatcher::InvalidateActiveExpectations();
  ULONG package = 0;
  void* credentials = nullptr;
  ULONG size = 0;
  const DWORD result = ::CredUIPromptForWindowsCredentialsW(
      &info, 0, &package, nullptr, 0, &credentials, &size, nullptr,
      CREDUIWIN_ENUMERATE_CURRENT_USER);
  if (!credentials)
    return false;
  base::ScopedClosureRunner clear(base::BindOnce(
      [](void* credentials, ULONG size) {
        ::SecureZeroMemory(credentials, size);
        ::CoTaskMemFree(credentials);
      },
      credentials, size));
  return result == ERROR_SUCCESS && size != 0 &&
         IsCurrentAccount(package, credentials, size);
}

HRESULT RequestHello(HWND owner, HSTRING message, ComPtr<HelloOperation>& op) {
  if (base::win::GetVersion() >= base::win::Version::WIN11) {
    ComPtr<IUserConsentVerifierInterop> factory;
    HRESULT hr = base::win::GetActivationFactory<
        IUserConsentVerifierInterop,
        RuntimeClass_Windows_Security_Credentials_UI_UserConsentVerifier>(
        &factory);
    if (FAILED(hr))
      return hr;
    return factory->RequestVerificationForWindowAsync(
        owner, message, __uuidof(HelloOperation), &op);
  }

  ComPtr<IUserConsentVerifierStatics> factory;
  HRESULT hr = base::win::GetActivationFactory<
      IUserConsentVerifierStatics,
      RuntimeClass_Windows_Security_Credentials_UI_UserConsentVerifier>(
      &factory);
  if (FAILED(hr))
    return hr;
  return factory->RequestVerificationAsync(message, &op);
}

bool IsHelloUnavailable(HRESULT result) {
  return result == REGDB_E_CLASSNOTREG || result == E_NOINTERFACE ||
         result == E_NOTIMPL ||
         result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) ||
         result == HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
}

void CancelHello(const ComPtr<HelloOperation>& operation) {
  ComPtr<IAsyncInfo> info;
  if (operation && SUCCEEDED(operation.As(&info)))
    info->Cancel();
}

class WindowsPasswordAuthenticator final
    : public device_reauth::DeviceAuthenticator {
 public:
  explicit WindowsPasswordAuthenticator(views::Widget& owner)
      : owner_(owner.GetWeakPtr()) {}

  ~WindowsPasswordAuthenticator() override { Cancel(); }

  bool CanAuthenticateWithBiometrics() override {
    // Windows Hello availability is asynchronous.
    return false;
  }

  bool CanAuthenticateWithBiometricOrScreenLock() override {
    return GetOwner() != nullptr;
  }

  void AuthenticateWithMessage(const std::u16string& message,
                               AuthenticateCallback callback) override {
    CHECK(!callback_);
    callback_ = std::move(callback);
    request_owner_ = GetOwner();
    reason_ = base::UTF16ToWide(message);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WindowsPasswordAuthenticator::Start,
                                  weak_factory_.GetWeakPtr()));
  }

  void Cancel() override {
    weak_factory_.InvalidateWeakPtrs();
    auto callback = std::move(callback_);
    auto operation = std::move(operation_);
    CancelHello(operation);
    if (callback)
      std::move(callback).Run(false);
  }

 private:
  HWND GetOwner() const {
    if (!owner_ || !owner_->IsVisible())
      return nullptr;
    const HWND hwnd = views::HWNDForNativeWindow(owner_->GetNativeWindow());
    return hwnd && ::IsWindow(hwnd) ? hwnd : nullptr;
  }

  bool HasRequestOwner() const {
    return request_owner_ && request_owner_ == GetOwner();
  }

  void Start() {
    if (!HasRequestOwner()) {
      Complete(false);
      return;
    }
    auto weak = weak_factory_.GetWeakPtr();
    const std::wstring reason = reason_;
    ComPtr<HelloOperation> operation;
    const HRESULT hr = RequestHello(
        request_owner_, base::win::HStringReference(reason.c_str()).Get(),
        operation);
    if (!weak) {
      CancelHello(operation);
      return;
    }
    if (!operation && IsHelloUnavailable(hr)) {
      UseWindowsCredentials();
      return;
    }
    operation_ = std::move(operation);
    if (FAILED(hr) || !operation_) {
      Cancel();
      return;
    }
    if (FAILED(base::win::PostAsyncHandlers(
            operation_.Get(),
            base::BindOnce(&WindowsPasswordAuthenticator::OnHelloResult, weak),
            base::BindOnce(&WindowsPasswordAuthenticator::Complete, weak,
                           false)))) {
      Cancel();
    }
  }

  void OnHelloResult(UserConsentVerificationResult result) {
    operation_.Reset();
    switch (result) {
      case UserConsentVerificationResult_Verified:
        Complete(true);
        return;
      case UserConsentVerificationResult_DeviceNotPresent:
      case UserConsentVerificationResult_NotConfiguredForUser:
      case UserConsentVerificationResult_DisabledByPolicy:
      case UserConsentVerificationResult_DeviceBusy:
        UseWindowsCredentials();
        return;
      default:
        Complete(false);
    }
  }

  void UseWindowsCredentials() {
    if (!HasRequestOwner()) {
      Complete(false);
      return;
    }
    // A modal prompt can run a nested loop and the panel may close in it.
    auto weak = weak_factory_.GetWeakPtr();
    const bool success =
        AuthenticateWithWindowsCredentials(request_owner_, reason_);
    if (weak)
      weak->Complete(success);
  }

  void Complete(bool success) {
    success = success && HasRequestOwner();
    weak_factory_.InvalidateWeakPtrs();
    operation_.Reset();
    if (callback_)
      std::move(callback_).Run(success);
  }

  const base::WeakPtr<views::Widget> owner_;
  HWND request_owner_ = nullptr;
  std::wstring reason_;
  AuthenticateCallback callback_;
  ComPtr<HelloOperation> operation_;
  base::WeakPtrFactory<WindowsPasswordAuthenticator> weak_factory_{this};
};

}  // namespace

std::unique_ptr<device_reauth::DeviceAuthenticator>
CreateWindowsPasswordAuthenticator(views::Widget& owner) {
  return std::make_unique<WindowsPasswordAuthenticator>(owner);
}

}  // namespace zitter::content_embedder
