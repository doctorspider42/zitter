// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "hid_access.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/render_frame_host.h"
#include "device_chooser.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/blink/public/mojom/hid/hid.mojom.h"

namespace zitter::content_embedder {
namespace {

constexpr char kApi[] = "hid";
constexpr char kGuard[] = "hid-guard";

ProfilePermissionController& Permissions(content::BrowserContext& context) {
  return static_cast<ProfileContext&>(context).site_permissions();
}

// Vendor, product and serial for devices that have a serial number; the
// physical device id, valid until the device is unplugged, for the rest.
std::string DeviceKey(const device::mojom::HidDeviceInfo& device, bool* persistent) {
  const bool stable = !device.serial_number.empty();
  if (persistent)
    *persistent = stable;
  if (!stable)
    return "physical:" + device.physical_device_id;
  return base::StringPrintf("%04x:%04x:", device.vendor_id, device.product_id) +
         device.serial_number;
}

std::u16string DeviceName(const device::mojom::HidDeviceInfo& device) {
  if (!device.product_name.empty())
    return base::UTF8ToUTF16(device.product_name);
  return prototype::TF(u"Urządzenie HID {}",
                       {base::UTF8ToUTF16(base::StringPrintf(
                           "%04x:%04x", device.vendor_id, device.product_id))});
}

bool FilterMatches(const blink::mojom::HidDeviceFilter& filter,
                   const device::mojom::HidDeviceInfo& device) {
  if (filter.device_ids) {
    if (filter.device_ids->is_vendor()) {
      if (filter.device_ids->get_vendor() != device.vendor_id)
        return false;
    } else {
      const auto& ids = filter.device_ids->get_vendor_and_product();
      if (ids->vendor != device.vendor_id || ids->product != device.product_id)
        return false;
    }
  }
  if (filter.usage) {
    bool matched = false;
    for (const auto& collection : device.collections) {
      if (filter.usage->is_page()) {
        matched |= collection->usage->usage_page == filter.usage->get_page();
      } else {
        const auto& usage = filter.usage->get_usage_and_page();
        matched |= collection->usage->usage_page == usage->usage_page &&
                   collection->usage->usage == usage->usage;
      }
    }
    if (!matched)
      return false;
  }
  return true;
}

bool MatchesAny(const std::vector<blink::mojom::HidDeviceFilterPtr>& filters,
                const device::mojom::HidDeviceInfo& device) {
  for (const auto& filter : filters) {
    if (FilterMatches(*filter, device))
      return true;
  }
  return false;
}

class Chooser final : public content::HidChooser {
 public:
  explicit Chooser(std::unique_ptr<DeviceChooser> dialog) : dialog_(std::move(dialog)) {}

 private:
  std::unique_ptr<DeviceChooser> dialog_;
};

}  // namespace

LabHidDelegate::LabHidDelegate() = default;
LabHidDelegate::~LabHidDelegate() = default;

void LabHidDelegate::Connect() {
  if (manager_)
    return;
  content::GetDeviceService().BindHidManager(manager_.BindNewPipeAndPassReceiver());
  manager_.set_disconnect_handler(
      base::BindOnce(&LabHidDelegate::OnManagerError, base::Unretained(this)));
  manager_->GetDevicesAndSetClient(
      client_.BindNewEndpointAndPassRemote(),
      base::BindOnce(&LabHidDelegate::OnDevicesEnumerated, weak_factory_.GetWeakPtr()));
}

void LabHidDelegate::OnDevicesEnumerated(std::vector<device::mojom::HidDeviceInfoPtr> devices) {
  for (auto& device : devices) {
    const auto guid = device->guid;
    devices_[guid] = std::move(device);
  }
}

void LabHidDelegate::OnManagerError() {
  manager_.reset();
  client_.reset();
  devices_.clear();
  for (auto& observer : observers_)
    observer.OnHidManagerConnectionError();
}

void LabHidDelegate::DeviceAdded(device::mojom::HidDeviceInfoPtr device) {
  for (auto& observer : observers_)
    observer.OnDeviceAdded(*device);
  const auto guid = device->guid;
  devices_[guid] = std::move(device);
}

void LabHidDelegate::DeviceRemoved(device::mojom::HidDeviceInfoPtr device) {
  devices_.erase(device->guid);
  for (auto& observer : observers_)
    observer.OnDeviceRemoved(*device);
}

void LabHidDelegate::DeviceChanged(device::mojom::HidDeviceInfoPtr device) {
  for (auto& observer : observers_)
    observer.OnDeviceChanged(*device);
  const auto guid = device->guid;
  devices_[guid] = std::move(device);
}

std::unique_ptr<content::HidChooser> LabHidDelegate::RunChooser(
    content::RenderFrameHost* frame,
    std::vector<blink::mojom::HidDeviceFilterPtr> filters,
    std::vector<blink::mojom::HidDeviceFilterPtr> exclusion_filters,
    content::HidChooser::Callback callback) {
  Connect();
  const auto origin = frame->GetLastCommittedOrigin();
  auto dialog = DeviceChooser::Show(
      *frame, u"HID",
      base::BindOnce(
          [](base::WeakPtr<LabHidDelegate> self,
             base::WeakPtr<content::BrowserContext> context, url::Origin origin,
             content::HidChooser::Callback callback, std::optional<std::string> physical) {
            std::vector<device::mojom::HidDeviceInfoPtr> chosen;
            if (self && context && physical) {
              // One physical device may expose several HID interfaces.
              for (const auto& [guid, device] : self->devices_) {
                if (device->physical_device_id != *physical)
                  continue;
                bool persistent = false;
                const auto key = DeviceKey(*device, &persistent);
                Permissions(*context).GrantDevice(kApi, origin, key,
                                                  base::UTF16ToUTF8(DeviceName(*device)),
                                                  persistent);
                chosen.push_back(device.Clone());
              }
            }
            std::move(callback).Run(std::move(chosen));
          },
          weak_factory_.GetWeakPtr(), frame->GetBrowserContext()->GetWeakPtr(), origin,
          std::move(callback)));
  auto* raw = dialog.get();
  manager_->GetDevices(base::BindOnce(
      [](base::WeakPtr<LabHidDelegate> self, base::WeakPtr<DeviceChooser> dialog,
         std::vector<blink::mojom::HidDeviceFilterPtr> filters,
         std::vector<blink::mojom::HidDeviceFilterPtr> exclusions,
         std::vector<device::mojom::HidDeviceInfoPtr> devices) {
        if (!self || !dialog)
          return;
        for (auto& device : devices) {
          if ((!filters.empty() && !MatchesAny(filters, *device)) ||
              MatchesAny(exclusions, *device)) {
            continue;
          }
          dialog->AddOrUpdate(device->physical_device_id, DeviceName(*device));
          const auto guid = device->guid;
          self->devices_[guid] = std::move(device);
        }
        dialog->SetScanning(false);
      },
      weak_factory_.GetWeakPtr(), raw->GetWeakPtr(), std::move(filters),
      std::move(exclusion_filters)));
  return std::make_unique<Chooser>(std::move(dialog));
}

bool LabHidDelegate::IsHidAllowedForFrame(content::RenderFrameHost* frame) {
  return frame && Permissions(*frame->GetBrowserContext())
                          .SiteSetting(kGuard, frame->GetOutermostMainFrame()
                                                   ->GetLastCommittedOrigin()) != 2;
}

bool LabHidDelegate::CanRequestDevicePermission(content::BrowserContext* context,
                                                const url::Origin& origin) {
  return context && network::IsOriginPotentiallyTrustworthy(origin) &&
         Permissions(*context).SiteSetting(kGuard, origin) != 2;
}

bool LabHidDelegate::HasDevicePermission(content::BrowserContext* context,
                                         content::RenderFrameHost*,
                                         const url::Origin& origin,
                                         const device::mojom::HidDeviceInfo& device) {
  return context && Permissions(*context).SiteSetting(kGuard, origin) != 2 &&
         Permissions(*context).HasDeviceGrant(kApi, origin, DeviceKey(device, nullptr));
}

void LabHidDelegate::RevokeDevicePermission(content::BrowserContext* context,
                                            content::RenderFrameHost*,
                                            const url::Origin& origin,
                                            const device::mojom::HidDeviceInfo& device) {
  if (!context ||
      !Permissions(*context).RevokeDevice(kApi, origin, DeviceKey(device, nullptr))) {
    return;
  }
  for (auto& observer : observers_)
    observer.OnPermissionRevoked(origin);
}

device::mojom::HidManager* LabHidDelegate::GetHidManager(content::BrowserContext*) {
  Connect();
  return manager_.get();
}

void LabHidDelegate::AddObserver(content::BrowserContext*, Observer* observer) {
  Connect();
  observers_.AddObserver(observer);
}

void LabHidDelegate::RemoveObserver(content::BrowserContext*, Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool LabHidDelegate::IsFidoAllowedForOrigin(content::BrowserContext*, const url::Origin&) {
  // Security keys are for WebAuthn, never raw HID access.
  return false;
}

bool LabHidDelegate::IsServiceWorkerAllowedForOrigin(const url::Origin&) {
  return false;
}

const device::mojom::HidDeviceInfo* LabHidDelegate::GetDeviceInfo(content::BrowserContext*,
                                                                  const std::string& guid) {
  Connect();
  const auto device = devices_.find(guid);
  return device == devices_.end() ? nullptr : device->second.get();
}

void LabHidDelegate::IncrementConnectionCount(content::BrowserContext*, const url::Origin&) {}
void LabHidDelegate::DecrementConnectionCount(content::BrowserContext*, const url::Origin&) {}

}  // namespace zitter::content_embedder
