// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The device chooser for WebUSB, WebHID, Web Serial and Web Bluetooth: a
// modal dialog over the page's window that lists matching devices, updates
// while it is open and reports the chosen device's id. Deleting the chooser
// closes the dialog without a choice.

#ifndef ZITTER_CONTENT_DEVICE_CHOOSER_H_
#define ZITTER_CONTENT_DEVICE_CHOOSER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace content {
class RenderFrameHost;
}

namespace zitter::content_embedder {

class DeviceChooserView;

class DeviceChooser final {
 public:
  using Done = base::OnceCallback<void(std::optional<std::string> id)>;

  // `title` names the API, e.g. "USB"; the dialog says which site asks.
  static std::unique_ptr<DeviceChooser> Show(content::RenderFrameHost& frame,
                                             std::u16string title,
                                             Done done);
  ~DeviceChooser();

  void AddOrUpdate(const std::string& id, std::u16string name);
  void Remove(const std::string& id);
  // Bluetooth shows that it is still scanning; others list a snapshot.
  void SetScanning(bool scanning);
  // Shows why no device can be listed, e.g. Bluetooth is off.
  void SetStatus(std::u16string status);
  base::WeakPtr<DeviceChooser> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  DeviceChooser();
  base::WeakPtr<DeviceChooserView> view_;
  base::WeakPtrFactory<DeviceChooser> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DEVICE_CHOOSER_H_
