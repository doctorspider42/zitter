// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Web Serial for the Content browser: one SerialPortManager connection, the
// device chooser and per-site port grants (Settings > Site permissions >
// Serial ports).

#ifndef ZITTER_CONTENT_SERIAL_ACCESS_H_
#define ZITTER_CONTENT_SERIAL_ACCESS_H_

#include <map>
#include <memory>
#include <vector>

#include "base/observer_list.h"
#include "base/unguessable_token.h"
#include "content/public/browser/serial_chooser.h"
#include "content/public/browser/serial_delegate.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/serial.mojom.h"

namespace zitter::content_embedder {

class LabSerialDelegate final : public content::SerialDelegate,
                                public device::mojom::SerialPortManagerClient {
 public:
  LabSerialDelegate();
  ~LabSerialDelegate() override;

  // content::SerialDelegate:
  std::unique_ptr<content::SerialChooser> RunChooser(
      content::RenderFrameHost* frame,
      std::vector<blink::mojom::SerialPortFilterPtr> filters,
      std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
      content::SerialChooser::Callback callback) override;
  bool CanRequestPortPermission(content::RenderFrameHost* frame) override;
  bool HasPortPermission(content::RenderFrameHost* frame,
                         const device::mojom::SerialPortInfo& port) override;
  void RevokePortPermissionWebInitiated(content::RenderFrameHost* frame,
                                        const base::UnguessableToken& token) override;
  const device::mojom::SerialPortInfo* GetPortInfo(content::RenderFrameHost* frame,
                                                   const base::UnguessableToken& token) override;
  device::mojom::SerialPortManager* GetPortManager(content::RenderFrameHost* frame) override;
  void AddObserver(content::RenderFrameHost* frame, Observer* observer) override;
  void RemoveObserver(content::RenderFrameHost* frame, Observer* observer) override;

  // device::mojom::SerialPortManagerClient:
  void OnPortAdded(device::mojom::SerialPortInfoPtr port) override;
  void OnPortRemoved(device::mojom::SerialPortInfoPtr port) override;
  void OnPortConnectedStateChanged(device::mojom::SerialPortInfoPtr port) override;

 private:
  void Connect();
  void OnPortsEnumerated(std::vector<device::mojom::SerialPortInfoPtr> ports);
  void OnManagerError();

  mojo::Remote<device::mojom::SerialPortManager> manager_;
  mojo::Receiver<device::mojom::SerialPortManagerClient> client_{this};
  std::map<base::UnguessableToken, device::mojom::SerialPortInfoPtr> ports_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<LabSerialDelegate> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_SERIAL_ACCESS_H_
