// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// WebUSB for the Content browser: one UsbDeviceManager connection, the
// device chooser and per-site device grants (Settings > Site permissions >
// USB devices).

#ifndef ZITTER_CONTENT_USB_ACCESS_H_
#define ZITTER_CONTENT_USB_ACCESS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "content/public/browser/usb_delegate.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/usb_manager.mojom.h"
#include "services/device/public/mojom/usb_manager_client.mojom.h"

namespace zitter::content_embedder {

class LabUsbDelegate final : public content::UsbDelegate,
                             public device::mojom::UsbDeviceManagerClient {
 public:
  LabUsbDelegate();
  ~LabUsbDelegate() override;

  // content::UsbDelegate:
  void AdjustProtectedInterfaceClasses(content::BrowserContext* browser_context,
                                       const url::Origin& origin,
                                       content::RenderFrameHost* frame,
                                       std::vector<uint8_t>& classes) override;
  std::unique_ptr<content::UsbChooser> RunChooser(
      content::RenderFrameHost& frame,
      blink::mojom::WebUsbRequestDeviceOptionsPtr options,
      blink::mojom::WebUsbService::GetPermissionCallback callback) override;
  bool PageMayUseUsb(content::Page& page) override;
  bool CanRequestDevicePermission(content::BrowserContext* browser_context,
                                  const url::Origin& origin) override;
  void RevokeDevicePermissionWebInitiated(
      content::BrowserContext* browser_context,
      const url::Origin& origin,
      const device::mojom::UsbDeviceInfo& device) override;
  const device::mojom::UsbDeviceInfo* GetDeviceInfo(
      content::BrowserContext* browser_context,
      const std::string& guid) override;
  bool HasDevicePermission(content::BrowserContext* browser_context,
                           content::RenderFrameHost* frame,
                           const url::Origin& origin,
                           const device::mojom::UsbDeviceInfo& device_info) override;
  void GetDevices(content::BrowserContext* browser_context,
                  blink::mojom::WebUsbService::GetDevicesCallback callback) override;
  void GetDevice(content::BrowserContext* browser_context,
                 const std::string& guid,
                 base::span<const uint8_t> blocked_interface_classes,
                 mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
                 mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client) override;
  void AddObserver(content::BrowserContext* browser_context, Observer* observer) override;
  void RemoveObserver(content::BrowserContext* browser_context, Observer* observer) override;
  bool IsServiceWorkerAllowedForOrigin(const url::Origin& origin) override;
  void IncrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override;
  void DecrementConnectionCount(content::BrowserContext* browser_context,
                                const url::Origin& origin) override;

  // device::mojom::UsbDeviceManagerClient:
  void OnDeviceAdded(device::mojom::UsbDeviceInfoPtr device) override;
  void OnDeviceRemoved(device::mojom::UsbDeviceInfoPtr device) override;

  // The stable grant key (vendor, product and serial) or the session's guid.
  static std::string DeviceKey(const device::mojom::UsbDeviceInfo& device, bool* persistent);
  static std::u16string DeviceName(const device::mojom::UsbDeviceInfo& device);

 private:
  device::mojom::UsbDeviceManager* Manager();
  void OnDevicesEnumerated(std::vector<device::mojom::UsbDeviceInfoPtr> devices);
  void OnManagerError();

  mojo::Remote<device::mojom::UsbDeviceManager> manager_;
  mojo::AssociatedReceiver<device::mojom::UsbDeviceManagerClient> client_{this};
  std::map<std::string, device::mojom::UsbDeviceInfoPtr> devices_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<LabUsbDelegate> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_USB_ACCESS_H_
