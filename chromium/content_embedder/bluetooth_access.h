// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Web Bluetooth for the Content browser: the device chooser, the scanning
// prompt, pairing prompts and per-site device ids and service grants for the
// session (Settings > Site permissions > Bluetooth).

#ifndef ZITTER_CONTENT_BLUETOOTH_ACCESS_H_
#define ZITTER_CONTENT_BLUETOOTH_ACCESS_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/observer_list.h"
#include "content/public/browser/bluetooth_delegate.h"
#include "device/bluetooth/public/cpp/bluetooth_uuid.h"
#include "third_party/blink/public/common/bluetooth/web_bluetooth_device_id.h"
#include "url/origin.h"

namespace zitter::content_embedder {

class LabBluetoothDelegate final : public content::BluetoothDelegate {
 public:
  LabBluetoothDelegate();
  ~LabBluetoothDelegate() override;

  std::unique_ptr<content::BluetoothChooser> RunBluetoothChooser(
      content::RenderFrameHost* frame,
      const content::BluetoothChooser::EventHandler& event_handler) override;
  std::unique_ptr<content::BluetoothScanningPrompt> ShowBluetoothScanningPrompt(
      content::RenderFrameHost* frame,
      const content::BluetoothScanningPrompt::EventHandler& event_handler) override;
  void ShowDevicePairPrompt(content::RenderFrameHost* frame,
                            const std::u16string& device_identifier,
                            PairPromptCallback callback,
                            PairingKind pairing_kind,
                            const std::optional<std::u16string>& pin) override;
  blink::WebBluetoothDeviceId GetWebBluetoothDeviceId(content::RenderFrameHost* frame,
                                                      const std::string& device_address) override;
  std::string GetDeviceAddress(content::RenderFrameHost* frame,
                               const blink::WebBluetoothDeviceId& device_id) override;
  blink::WebBluetoothDeviceId AddScannedDevice(content::RenderFrameHost* frame,
                                               const std::string& device_address) override;
  blink::WebBluetoothDeviceId GrantServiceAccessPermission(
      content::RenderFrameHost* frame,
      const device::BluetoothDevice* device,
      const blink::mojom::WebBluetoothRequestDeviceOptions* options) override;
  bool HasDevicePermission(content::RenderFrameHost* frame,
                           const blink::WebBluetoothDeviceId& device_id) override;
  void RevokeDevicePermissionWebInitiated(content::RenderFrameHost* frame,
                                          const blink::WebBluetoothDeviceId& device_id) override;
  bool MayUseBluetooth(content::RenderFrameHost* frame) override;
  bool IsAllowedToAccessService(content::RenderFrameHost* frame,
                                const blink::WebBluetoothDeviceId& device_id,
                                const device::BluetoothUUID& service) override;
  bool IsAllowedToAccessAtLeastOneService(content::RenderFrameHost* frame,
                                          const blink::WebBluetoothDeviceId& device_id) override;
  bool IsAllowedToAccessManufacturerData(content::RenderFrameHost* frame,
                                         const blink::WebBluetoothDeviceId& device_id,
                                         uint16_t manufacturer_code) override;
  std::vector<blink::mojom::WebBluetoothDevicePtr> GetPermittedDevices(
      content::RenderFrameHost* frame) override;
  void AddFramePermissionObserver(FramePermissionObserver* observer) override;
  void RemoveFramePermissionObserver(FramePermissionObserver* observer) override;
  bool IsBluetoothScanningBlocked(content::BrowserContext* browser_context,
                                  const url::Origin& requesting_origin,
                                  const url::Origin& embedding_origin) override;
  void BlockBluetoothScanning(content::BrowserContext* browser_context,
                              const url::Origin& requesting_origin,
                              const url::Origin& embedding_origin) override;

 private:
  using AddressToId = std::map<std::string, blink::WebBluetoothDeviceId>;
  // Profile, requesting origin and top-level origin, as Chrome scopes them.
  using Scope = std::tuple<uintptr_t, url::Origin, url::Origin>;
  struct Grant {
    std::string name;
    base::flat_set<device::BluetoothUUID> services;
    base::flat_set<uint16_t> manufacturer_codes;
  };

  AddressToId& AddressesFor(content::RenderFrameHost* frame);
  blink::WebBluetoothDeviceId IdFor(content::RenderFrameHost* frame, const std::string& address);

  std::map<Scope, AddressToId> addresses_;
  base::flat_map<blink::WebBluetoothDeviceId, Grant> grants_;
  base::ObserverList<FramePermissionObserver> observers_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_BLUETOOTH_ACCESS_H_
