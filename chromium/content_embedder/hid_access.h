// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// WebHID for the Content browser: one HidManager connection, the device
// chooser and per-site device grants (Settings > Site permissions > HID
// devices).

#ifndef ZITTER_CONTENT_HID_ACCESS_H_
#define ZITTER_CONTENT_HID_ACCESS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "content/public/browser/hid_chooser.h"
#include "content/public/browser/hid_delegate.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/hid.mojom.h"

namespace zitter::content_embedder {

class LabHidDelegate final : public content::HidDelegate,
                             public device::mojom::HidManagerClient {
 public:
  LabHidDelegate();
  ~LabHidDelegate() override;

  // content::HidDelegate:
  std::unique_ptr<content::HidChooser> RunChooser(
      content::RenderFrameHost* frame,
      std::vector<blink::mojom::HidDeviceFilterPtr> filters,
      std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
      content::HidChooser::Callback callback) override;
  bool IsHidAllowedForFrame(content::RenderFrameHost* frame) override;
  bool CanRequestDevicePermission(content::BrowserContext* browser_context,
                                  const url::Origin& origin) override;
  bool HasDevicePermission(content::BrowserContext* browser_context,
                           content::RenderFrameHost* frame,
                           const url::Origin& origin,
                           const device::mojom::HidDeviceInfo& device) override;
  void RevokeDevicePermission(content::BrowserContext* browser_context,
                              content::RenderFrameHost* frame,
                              const url::Origin& origin,
                              const device::mojom::HidDeviceInfo& device) override;
  device::mojom::HidManager* GetHidManager(content::BrowserContext* browser_context) override;
  void AddObserver(content::BrowserContext* browser_context, Observer* observer) override;
  void RemoveObserver(content::BrowserContext* browser_context, Observer* observer) override;
  bool IsFidoAllowedForOrigin(content::BrowserContext* browser_context,
                              const url::Origin& origin) override;
  bool IsServiceWorkerAllowedForOrigin(const url::Origin& origin) override;
  const device::mojom::HidDeviceInfo* GetDeviceInfo(content::BrowserContext* browser_context,
                                                    const std::string& guid) override;
  void IncrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override;
  void DecrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override;

  // device::mojom::HidManagerClient:
  void DeviceAdded(device::mojom::HidDeviceInfoPtr device) override;
  void DeviceRemoved(device::mojom::HidDeviceInfoPtr device) override;
  void DeviceChanged(device::mojom::HidDeviceInfoPtr device) override;

 private:
  void Connect();
  void OnDevicesEnumerated(std::vector<device::mojom::HidDeviceInfoPtr> devices);
  void OnManagerError();

  mojo::Remote<device::mojom::HidManager> manager_;
  mojo::AssociatedReceiver<device::mojom::HidManagerClient> client_{this};
  std::map<std::string, device::mojom::HidDeviceInfoPtr> devices_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<LabHidDelegate> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_HID_ACCESS_H_
