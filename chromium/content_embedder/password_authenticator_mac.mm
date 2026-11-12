// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "password_authenticator_mac.h"

#import <LocalAuthentication/LocalAuthentication.h>

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "components/device_reauth/device_authenticator.h"

namespace zitter::content_embedder {
namespace {

class MacPasswordAuthenticator final : public device_reauth::DeviceAuthenticator {
 public:
  ~MacPasswordAuthenticator() override { Cancel(); }

  bool CanAuthenticateWithBiometrics() override {
    return [[[LAContext alloc] init]
        canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                    error:nil];
  }

  bool CanAuthenticateWithBiometricOrScreenLock() override {
    return [[[LAContext alloc] init]
        canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
                    error:nil];
  }

  void AuthenticateWithMessage(const std::u16string& message,
                               AuthenticateCallback callback) override {
    CHECK(!callback_);
    callback_ = std::move(callback);
    context_ = [[LAContext alloc] init];
    auto result = base::BindPostTaskToCurrentDefault(base::BindRepeating(
        &MacPasswordAuthenticator::Complete, weak_factory_.GetWeakPtr()));
    if (![context_ canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication
                              error:nil]) {
      result.Run(false);
      return;
    }
    [context_ evaluatePolicy:LAPolicyDeviceOwnerAuthentication
             localizedReason:base::SysUTF16ToNSString(message)
                       reply:^(BOOL success, NSError*) {
                         result.Run(success);
                       }];
  }

  void Cancel() override { Complete(false); }

 private:
  void Complete(bool success) {
    weak_factory_.InvalidateWeakPtrs();
    [context_ invalidate];
    context_ = nil;
    if (callback_)
      std::move(callback_).Run(success);
  }

  LAContext* __strong context_ = nil;
  AuthenticateCallback callback_;
  base::WeakPtrFactory<MacPasswordAuthenticator> weak_factory_{this};
};

}  // namespace

std::unique_ptr<device_reauth::DeviceAuthenticator>
CreateMacPasswordAuthenticator() {
  return std::make_unique<MacPasswordAuthenticator>();
}

}  // namespace zitter::content_embedder
