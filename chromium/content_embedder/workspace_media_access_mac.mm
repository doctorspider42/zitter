// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_media_access.h"

#import <AVFoundation/AVFoundation.h>

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/scoped_add_feature_flags.h"
#include "base/strings/strcat.h"
#include "base/base_switches.h"
#include "media/base/media_switches.h"
#include "base/task/bind_post_task.h"

namespace zitter::content_embedder {
namespace {

void RequestAccess(AVMediaType type, base::OnceCallback<void(bool)> done) {
  switch ([AVCaptureDevice authorizationStatusForMediaType:type]) {
    case AVAuthorizationStatusAuthorized:
      std::move(done).Run(true);
      return;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
      std::move(done).Run(false);
      return;
    case AVAuthorizationStatusNotDetermined: {
      auto reply = std::make_shared<base::OnceCallback<void(bool)>>(
          base::BindPostTaskToCurrentDefault(std::move(done)));
      [AVCaptureDevice requestAccessForMediaType:type
                               completionHandler:^(BOOL allowed) {
                                 std::move(*reply).Run(allowed);
                               }];
      return;
    }
  }
}

}  // namespace

void ConfigureWorkspaceDesktopPicker() {
  if (@available(macOS 14.0, *)) {
    auto* command = base::CommandLine::ForCurrentProcess();
    {
      base::ScopedAddFeatureFlags features(command);
      features.EnableIfNotSet(media::kUseSCContentSharingPicker);
    }
    const auto disabled = command->GetSwitchValueASCII(switches::kDisableFeatures);
    command->AppendSwitchASCII(
        switches::kDisableFeatures,
        base::StrCat({disabled, disabled.empty() ? "" : ",",
                      "AllowChangingSelectedContent"}));
  }
}

bool CanUseWorkspaceDesktopPicker() {
  if (@available(macOS 14.0, *)) {
    return base::FeatureList::IsEnabled(media::kUseSCContentSharingPicker) &&
           base::FeatureList::GetInstance()->IsFeatureOverriddenFromCommandLine(
               "AllowChangingSelectedContent", base::FeatureList::OVERRIDE_DISABLE_FEATURE) &&
           !base::CommandLine::ForCurrentProcess()->HasSwitch(
               "use-fake-device-for-media-stream");
  }
  return false;
}

void RequestWorkspaceCaptureAccess(bool audio, bool video,
                                   base::RepeatingCallback<bool()> still_current,
                                   base::OnceCallback<void(bool)> done) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "use-fake-device-for-media-stream")) {
    std::move(done).Run(true);
    return;
  }
  if (audio && video) {
    RequestAccess(AVMediaTypeAudio, base::BindOnce(
        [](base::RepeatingCallback<bool()> still_current,
           base::OnceCallback<void(bool)> done, bool allowed) {
          if (allowed && still_current.Run())
            RequestAccess(AVMediaTypeVideo, std::move(done));
          else
            std::move(done).Run(false);
        }, std::move(still_current), std::move(done)));
    return;
  }
  RequestAccess(audio ? AVMediaTypeAudio : AVMediaTypeVideo, std::move(done));
}

}  // namespace zitter::content_embedder
