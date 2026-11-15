// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "usb_access.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/usb_chooser.h"
#include "device_chooser.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/device/public/cpp/usb/usb_utils.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"

namespace zitter::content_embedder {
namespace {

constexpr char kApi[] = "usb";
constexpr char kGuard[] = "usb-guard";

ProfilePermissionController& Permissions(content::BrowserContext& context) {
  return static_cast<ProfileContext&>(context).site_permissions();
}

// Keeps the dialog open for as long as content keeps the request.
class Chooser final : public content::UsbChooser {
 public:
  explicit Chooser(std::unique_ptr<DeviceChooser> dialog) : dialog_(std::move(dialog)) {}
  void Set(std::unique_ptr<DeviceChooser> dialog) { dialog_ = std::move(dialog); }

 private:
  std::unique_ptr<DeviceChooser> dialog_;
};

}  // namespace

LabUsbDelegate::LabUsbDelegate() = default;
LabUsbDelegate::~LabUsbDelegate() = default;

std::string LabUsbDelegate::DeviceKey(const device::mojom::UsbDeviceInfo& device,
                                      bool* persistent) {
  const bool stable = device.serial_number && !device.serial_number->empty();
  if (persistent)
    *persistent = stable;
  if (!stable)
    return device.guid;
  return base::StringPrintf("%04x:%04x:", device.vendor_id, device.product_id) +
         base::UTF16ToUTF8(*device.serial_number);
}

std::u16string LabUsbDelegate::DeviceName(const device::mojom::UsbDeviceInfo& device) {
  if (device.product_name && !device.product_name->empty())
    return *device.product_name;
  return prototype::TF(u"Urządzenie USB {}",
                       {base::UTF8ToUTF16(base::StringPrintf("%04x:%04x", device.vendor_id,
                                                             device.product_id))});
}

device::mojom::UsbDeviceManager* LabUsbDelegate::Manager() {
  if (!manager_) {
    content::GetDeviceService().BindUsbDeviceManager(manager_.BindNewPipeAndPassReceiver());
    manager_.set_disconnect_handler(
        base::BindOnce(&LabUsbDelegate::OnManagerError, base::Unretained(this)));
    manager_->EnumerateDevicesAndSetClient(
        client_.BindNewEndpointAndPassRemote(),
        base::BindOnce(&LabUsbDelegate::OnDevicesEnumerated, weak_factory_.GetWeakPtr()));
  }
  return manager_.get();
}

void LabUsbDelegate::OnDevicesEnumerated(std::vector<device::mojom::UsbDeviceInfoPtr> devices) {
  for (auto& device : devices) {
    const auto guid = device->guid;
    devices_[guid] = std::move(device);
  }
}

void LabUsbDelegate::OnManagerError() {
  manager_.reset();
  client_.reset();
  devices_.clear();
  for (auto& observer : observers_)
    observer.OnDeviceManagerConnectionError();
}

void LabUsbDelegate::OnDeviceAdded(device::mojom::UsbDeviceInfoPtr device) {
  for (auto& observer : observers_)
    observer.OnDeviceAdded(*device);
  const auto guid = device->guid;
  devices_[guid] = std::move(device);
}

void LabUsbDelegate::OnDeviceRemoved(device::mojom::UsbDeviceInfoPtr device) {
  devices_.erase(device->guid);
  for (auto& observer : observers_)
    observer.OnDeviceRemoved(*device);
}

void LabUsbDelegate::AdjustProtectedInterfaceClasses(content::BrowserContext*,
                                                     const url::Origin&,
                                                     content::RenderFrameHost*,
                                                     std::vector<uint8_t>&) {
  // Content's protected classes (audio, HID, mass storage, smart card, video,
  // wireless) stay protected for every site.
}

std::unique_ptr<content::UsbChooser> LabUsbDelegate::RunChooser(
    content::RenderFrameHost& frame,
    blink::mojom::WebUsbRequestDeviceOptionsPtr options,
    blink::mojom::WebUsbService::GetPermissionCallback callback) {
  auto chooser = std::make_unique<Chooser>(nullptr);
  const auto origin = frame.GetLastCommittedOrigin();
  auto* context = frame.GetBrowserContext();
  auto weak_context = context->GetWeakPtr();
  // The dialog lists a fresh enumeration of the matching devices.
  auto dialog = DeviceChooser::Show(
      frame, u"USB",
      base::BindOnce(
          [](base::WeakPtr<LabUsbDelegate> self,
             base::WeakPtr<content::BrowserContext> context, url::Origin origin,
             blink::mojom::WebUsbService::GetPermissionCallback callback,
             std::optional<std::string> guid) {
            if (!self || !context || !guid) {
              std::move(callback).Run(nullptr);
              return;
            }
            const auto device = self->devices_.find(*guid);
            if (device == self->devices_.end()) {
              std::move(callback).Run(nullptr);
              return;
            }
            bool persistent = false;
            const auto key = DeviceKey(*device->second, &persistent);
            Permissions(*context).GrantDevice(kApi, origin, key,
                                              base::UTF16ToUTF8(DeviceName(*device->second)),
                                              persistent);
            std::move(callback).Run(device->second.Clone());
          },
          weak_factory_.GetWeakPtr(), weak_context, origin, std::move(callback)));
  auto* raw = dialog.get();
  Manager()->GetDevices(
      nullptr,
      base::BindOnce(
          [](base::WeakPtr<LabUsbDelegate> self, base::WeakPtr<DeviceChooser> dialog,
             std::vector<device::mojom::UsbDeviceFilterPtr> filters,
             std::vector<device::mojom::UsbDeviceFilterPtr> exclusions,
             std::vector<device::mojom::UsbDeviceInfoPtr> devices) {
            if (!self || !dialog)
              return;
            for (auto& device : devices) {
              if ((!filters.empty() && !device::UsbDeviceFilterMatchesAny(filters, *device)) ||
                  device::UsbDeviceFilterMatchesAny(exclusions, *device)) {
                continue;
              }
              dialog->AddOrUpdate(device->guid, DeviceName(*device));
              const auto guid = device->guid;
              self->devices_[guid] = std::move(device);
            }
            dialog->SetScanning(false);
          },
          weak_factory_.GetWeakPtr(), raw->GetWeakPtr(), std::move(options->filters),
          std::move(options->exclusion_filters)));
  chooser->Set(std::move(dialog));
  return chooser;
}

bool LabUsbDelegate::PageMayUseUsb(content::Page& page) {
  auto& frame = page.GetMainDocument();
  return Permissions(*frame.GetBrowserContext())
             .SiteSetting(kGuard, frame.GetLastCommittedOrigin()) != 2;
}

bool LabUsbDelegate::CanRequestDevicePermission(content::BrowserContext* context,
                                                const url::Origin& origin) {
  return context && network::IsOriginPotentiallyTrustworthy(origin) &&
         Permissions(*context).SiteSetting(kGuard, origin) != 2;
}

void LabUsbDelegate::RevokeDevicePermissionWebInitiated(
    content::BrowserContext* context,
    const url::Origin& origin,
    const device::mojom::UsbDeviceInfo& device) {
  if (!context ||
      !Permissions(*context).RevokeDevice(kApi, origin, DeviceKey(device, nullptr))) {
    return;
  }
  for (auto& observer : observers_)
    observer.OnPermissionRevoked(origin);
}

const device::mojom::UsbDeviceInfo* LabUsbDelegate::GetDeviceInfo(content::BrowserContext*,
                                                                  const std::string& guid) {
  Manager();
  const auto device = devices_.find(guid);
  return device == devices_.end() ? nullptr : device->second.get();
}

bool LabUsbDelegate::HasDevicePermission(content::BrowserContext* context,
                                         content::RenderFrameHost*,
                                         const url::Origin& origin,
                                         const device::mojom::UsbDeviceInfo& device) {
  return context && Permissions(*context).SiteSetting(kGuard, origin) != 2 &&
         Permissions(*context).HasDeviceGrant(kApi, origin, DeviceKey(device, nullptr));
}

void LabUsbDelegate::GetDevices(content::BrowserContext*,
                                blink::mojom::WebUsbService::GetDevicesCallback callback) {
  Manager()->GetDevices(nullptr, std::move(callback));
}

void LabUsbDelegate::GetDevice(
    content::BrowserContext*,
    const std::string& guid,
    base::span<const uint8_t> blocked_interface_classes,
    mojo::PendingReceiver<device::mojom::UsbDevice> device_receiver,
    mojo::PendingRemote<device::mojom::UsbDeviceClient> device_client) {
  Manager()->GetDevice(
      guid, std::vector<uint8_t>(blocked_interface_classes.begin(),
                                 blocked_interface_classes.end()),
      std::move(device_receiver), std::move(device_client));
}

void LabUsbDelegate::AddObserver(content::BrowserContext*, Observer* observer) {
  Manager();
  observers_.AddObserver(observer);
}

void LabUsbDelegate::RemoveObserver(content::BrowserContext*, Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool LabUsbDelegate::IsServiceWorkerAllowedForOrigin(const url::Origin&) {
  // Only extension service workers may use WebUSB in Chrome.
  return false;
}

void LabUsbDelegate::IncrementConnectionCount(content::BrowserContext*, const url::Origin&) {}
void LabUsbDelegate::DecrementConnectionCount(content::BrowserContext*, const url::Origin&) {}

}  // namespace zitter::content_embedder
