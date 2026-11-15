// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "serial_access.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/render_frame_host.h"
#include "device/bluetooth/public/cpp/bluetooth_uuid.h"
#include "device_chooser.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/blink/public/mojom/serial/serial.mojom.h"

namespace zitter::content_embedder {
namespace {

constexpr char kApi[] = "serial";
constexpr char kGuard[] = "serial-guard";

ProfilePermissionController& Permissions(content::BrowserContext& context) {
  return static_cast<ProfileContext&>(context).site_permissions();
}

url::Origin SiteOf(content::RenderFrameHost& frame) {
  return frame.GetOutermostMainFrame()->GetLastCommittedOrigin();
}

// USB adapters with a serial number keep their grant; other ports keep it
// until they are unplugged or Zitter exits.
std::string PortKey(const device::mojom::SerialPortInfo& port, bool* persistent) {
  const bool stable = port.has_vendor_id && port.has_product_id && port.serial_number &&
                      !port.serial_number->empty();
  if (persistent)
    *persistent = stable;
  if (!stable)
    return port.token.ToString();
  return base::StringPrintf("%04x:%04x:", port.vendor_id, port.product_id) +
         *port.serial_number;
}

std::u16string PortName(const device::mojom::SerialPortInfo& port) {
  const auto path = base::UTF16ToUTF8(port.path.BaseName().LossyDisplayName());
  if (port.display_name && !port.display_name->empty())
    return base::UTF8ToUTF16(*port.display_name + " (" + path + ")");
  return base::UTF8ToUTF16(path);
}

bool FilterMatches(const blink::mojom::SerialPortFilter& filter,
                   const device::mojom::SerialPortInfo& port) {
  if (filter.bluetooth_service_class_id) {
    return port.bluetooth_service_class_id &&
           *port.bluetooth_service_class_id == *filter.bluetooth_service_class_id;
  }
  if (filter.has_vendor_id &&
      (!port.has_vendor_id || filter.vendor_id != port.vendor_id)) {
    return false;
  }
  if (filter.has_product_id &&
      (!port.has_product_id || filter.product_id != port.product_id)) {
    return false;
  }
  return true;
}

class Chooser final : public content::SerialChooser {
 public:
  explicit Chooser(std::unique_ptr<DeviceChooser> dialog) : dialog_(std::move(dialog)) {}

 private:
  std::unique_ptr<DeviceChooser> dialog_;
};

}  // namespace

LabSerialDelegate::LabSerialDelegate() = default;
LabSerialDelegate::~LabSerialDelegate() = default;

void LabSerialDelegate::Connect() {
  if (manager_)
    return;
  content::GetDeviceService().BindSerialPortManager(manager_.BindNewPipeAndPassReceiver());
  manager_.set_disconnect_handler(
      base::BindOnce(&LabSerialDelegate::OnManagerError, base::Unretained(this)));
  manager_->SetClient(client_.BindNewPipeAndPassRemote());
  manager_->GetDevices(/*allow_bluetooth_system_prompt=*/false,
                       base::BindOnce(&LabSerialDelegate::OnPortsEnumerated,
                                      weak_factory_.GetWeakPtr()));
}

void LabSerialDelegate::OnPortsEnumerated(std::vector<device::mojom::SerialPortInfoPtr> ports) {
  for (auto& port : ports) {
    const auto token = port->token;
    ports_[token] = std::move(port);
  }
}

void LabSerialDelegate::OnManagerError() {
  manager_.reset();
  client_.reset();
  ports_.clear();
  for (auto& observer : observers_)
    observer.OnPortManagerConnectionError();
}

void LabSerialDelegate::OnPortAdded(device::mojom::SerialPortInfoPtr port) {
  for (auto& observer : observers_)
    observer.OnPortAdded(*port);
  const auto token = port->token;
  ports_[token] = std::move(port);
}

void LabSerialDelegate::OnPortRemoved(device::mojom::SerialPortInfoPtr port) {
  ports_.erase(port->token);
  for (auto& observer : observers_)
    observer.OnPortRemoved(*port);
}

void LabSerialDelegate::OnPortConnectedStateChanged(device::mojom::SerialPortInfoPtr port) {
  for (auto& observer : observers_)
    observer.OnPortConnectedStateChanged(*port);
  const auto token = port->token;
  ports_[token] = std::move(port);
}

std::unique_ptr<content::SerialChooser> LabSerialDelegate::RunChooser(
    content::RenderFrameHost* frame,
    std::vector<blink::mojom::SerialPortFilterPtr> filters,
    std::vector<device::BluetoothUUID> allowed_bluetooth_service_class_ids,
    content::SerialChooser::Callback callback) {
  Connect();
  auto dialog = DeviceChooser::Show(
      *frame, prototype::T(u"portu szeregowego"),
      base::BindOnce(
          [](base::WeakPtr<LabSerialDelegate> self,
             base::WeakPtr<content::BrowserContext> context, url::Origin origin,
             content::SerialChooser::Callback callback, std::optional<std::string> token) {
            if (!self || !context || !token) {
              std::move(callback).Run(nullptr);
              return;
            }
            for (const auto& [port_token, port] : self->ports_) {
              if (port_token.ToString() != *token)
                continue;
              bool persistent = false;
              const auto key = PortKey(*port, &persistent);
              Permissions(*context).GrantDevice(kApi, origin, key,
                                                base::UTF16ToUTF8(PortName(*port)), persistent);
              std::move(callback).Run(port.Clone());
              return;
            }
            std::move(callback).Run(nullptr);
          },
          weak_factory_.GetWeakPtr(), frame->GetBrowserContext()->GetWeakPtr(), SiteOf(*frame),
          std::move(callback)));
  auto* raw = dialog.get();
  manager_->GetDevices(
      /*allow_bluetooth_system_prompt=*/false,
      base::BindOnce(
          [](base::WeakPtr<LabSerialDelegate> self, base::WeakPtr<DeviceChooser> dialog,
             std::vector<blink::mojom::SerialPortFilterPtr> filters,
             std::vector<device::BluetoothUUID> allowed_services,
             std::vector<device::mojom::SerialPortInfoPtr> ports) {
            if (!self || !dialog)
              return;
            for (auto& port : ports) {
              // Bluetooth RFCOMM ports appear only when the site asked for
              // their service class.
              if (port->bluetooth_service_class_id &&
                  !std::ranges::contains(allowed_services, *port->bluetooth_service_class_id) &&
                  std::ranges::none_of(filters, [&](const auto& filter) {
                    return filter->bluetooth_service_class_id.has_value();
                  })) {
                continue;
              }
              if (!filters.empty() && std::ranges::none_of(filters, [&](const auto& filter) {
                    return FilterMatches(*filter, *port);
                  })) {
                continue;
              }
              dialog->AddOrUpdate(port->token.ToString(), PortName(*port));
              const auto token = port->token;
              self->ports_[token] = std::move(port);
            }
            dialog->SetScanning(false);
          },
          weak_factory_.GetWeakPtr(), raw->GetWeakPtr(), std::move(filters),
          std::move(allowed_bluetooth_service_class_ids)));
  return std::make_unique<Chooser>(std::move(dialog));
}

bool LabSerialDelegate::CanRequestPortPermission(content::RenderFrameHost* frame) {
  return frame && network::IsOriginPotentiallyTrustworthy(SiteOf(*frame)) &&
         Permissions(*frame->GetBrowserContext()).SiteSetting(kGuard, SiteOf(*frame)) != 2;
}

bool LabSerialDelegate::HasPortPermission(content::RenderFrameHost* frame,
                                          const device::mojom::SerialPortInfo& port) {
  if (!frame)
    return false;
  auto& permissions = Permissions(*frame->GetBrowserContext());
  return permissions.SiteSetting(kGuard, SiteOf(*frame)) != 2 &&
         permissions.HasDeviceGrant(kApi, SiteOf(*frame), PortKey(port, nullptr));
}

void LabSerialDelegate::RevokePortPermissionWebInitiated(content::RenderFrameHost* frame,
                                                         const base::UnguessableToken& token) {
  const auto port = ports_.find(token);
  if (!frame || port == ports_.end() ||
      !Permissions(*frame->GetBrowserContext())
           .RevokeDevice(kApi, SiteOf(*frame), PortKey(*port->second, nullptr))) {
    return;
  }
  for (auto& observer : observers_)
    observer.OnPermissionRevoked(SiteOf(*frame));
}

const device::mojom::SerialPortInfo* LabSerialDelegate::GetPortInfo(
    content::RenderFrameHost*, const base::UnguessableToken& token) {
  Connect();
  const auto port = ports_.find(token);
  return port == ports_.end() ? nullptr : port->second.get();
}

device::mojom::SerialPortManager* LabSerialDelegate::GetPortManager(content::RenderFrameHost*) {
  Connect();
  return manager_.get();
}

void LabSerialDelegate::AddObserver(content::RenderFrameHost*, Observer* observer) {
  Connect();
  observers_.AddObserver(observer);
}

void LabSerialDelegate::RemoveObserver(content::RenderFrameHost*, Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace zitter::content_embedder
