// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "bluetooth_access.h"

#include <memory>
#include <utility>

#include "base/containers/contains.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/bluetooth_chooser.h"
#include "content/public/browser/bluetooth_scanning_prompt.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device_chooser.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/blink/public/mojom/bluetooth/web_bluetooth.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::content_embedder {
namespace {

constexpr char kGuard[] = "bluetooth-guard";
constexpr char kScanning[] = "bluetooth-scanning";

ProfilePermissionController& Permissions(content::RenderFrameHost& frame) {
  return static_cast<ProfileContext*>(frame.GetBrowserContext())->site_permissions();
}

url::Origin SiteOf(content::RenderFrameHost& frame) {
  return frame.GetOutermostMainFrame()->GetLastCommittedOrigin();
}

// Relays the adapter's scan results to the device chooser.
class Chooser final : public content::BluetoothChooser {
 public:
  Chooser(content::RenderFrameHost& frame, const EventHandler& events) : events_(events) {
    dialog_ = DeviceChooser::Show(
        frame, u"Bluetooth",
        base::BindOnce(
            [](EventHandler events, std::optional<std::string> id) {
              if (id)
                events.Run(content::BluetoothChooserEvent::SELECTED, *id);
              else
                events.Run(content::BluetoothChooserEvent::CANCELLED, std::string());
            },
            events));
  }

  bool CanAskForScanningPermission() override { return true; }

  void SetAdapterPresence(AdapterPresence presence) override {
    switch (presence) {
      case AdapterPresence::ABSENT:
        dialog_->SetStatus(prototype::T(u"Ten komputer nie ma Bluetooth."));
        break;
      case AdapterPresence::POWERED_OFF:
        dialog_->SetStatus(prototype::T(u"Włącz Bluetooth w ustawieniach Windows."));
        break;
      case AdapterPresence::UNAUTHORIZED:
        dialog_->SetStatus(prototype::T(u"Windows nie pozwala Zitter używać Bluetooth."));
        break;
      case AdapterPresence::POWERED_ON:
        dialog_->SetStatus({});
        break;
    }
  }

  void ShowDiscoveryState(DiscoveryState state) override {
    dialog_->SetScanning(state == DiscoveryState::DISCOVERING);
    if (state == DiscoveryState::FAILED_TO_START)
      dialog_->SetStatus(prototype::T(u"Nie udało się wyszukać urządzeń Bluetooth."));
  }

  void AddOrUpdateDevice(const std::string& device_id,
                         bool should_update_name,
                         const std::u16string& device_name,
                         bool is_gatt_connected,
                         bool is_paired,
                         int signal_strength_level) override {
    dialog_->AddOrUpdate(device_id, device_name.empty()
                                        ? prototype::T(u"Nieznane urządzenie")
                                        : device_name);
  }

 private:
  EventHandler events_;
  std::unique_ptr<DeviceChooser> dialog_;
};

class ScanningPrompt final : public content::BluetoothScanningPrompt {};

// Pairing: confirm, compare or show a PIN, or ask for one.
class PairPromptView final : public views::View {
 public:
  PairPromptView(views::DialogDelegate& dialog,
                 std::u16string device,
                 content::BluetoothDelegate::PairingKind kind,
                 const std::optional<std::u16string>& pin,
                 content::BluetoothDelegate::PairPromptCallback callback)
      : callback_(std::move(callback)) {
    using Kind = content::BluetoothDelegate::PairingKind;
    dialog.SetAcceptCallback(base::BindOnce(&PairPromptView::Finish, base::Unretained(this), true));
    dialog.SetCancelCallback(base::BindOnce(&PairPromptView::Finish, base::Unretained(this), false));
    dialog.SetCloseCallback(base::BindOnce(&PairPromptView::Finish, base::Unretained(this), false));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(16, 20), 10));
    std::u16string text;
    switch (kind) {
      case Kind::kProvidePin:
        text = prototype::TF(u"Wpisz kod PIN urządzenia {}.", {device});
        break;
      case Kind::kConfirmPinMatch:
      case Kind::kDisplayPin:
        text = prototype::TF(u"Sprawdź, czy {} pokazuje kod {}.",
                             {device, pin.value_or(std::u16string())});
        break;
      default:
        text = prototype::TF(u"Sparować z {}?", {device});
        break;
    }
    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    if (kind == Kind::kProvidePin || kind == Kind::kProvidePasswordCredential) {
      field_ = AddChildView(std::make_unique<views::Textfield>());
      field_->GetViewAccessibility().SetName(prototype::T(u"Kod PIN"));
    }
  }

 private:
  void Finish(bool accepted) {
    if (!callback_)
      return;
    content::BluetoothDelegate::PairPromptResult result(
        accepted ? content::BluetoothDelegate::PairPromptStatus::kSuccess
                 : content::BluetoothDelegate::PairPromptStatus::kCancelled);
    if (accepted && field_)
      result.pin = base::UTF16ToUTF8(field_->GetText());
    std::move(callback_).Run(result);
  }

  content::BluetoothDelegate::PairPromptCallback callback_;
  raw_ptr<views::Textfield> field_ = nullptr;
};

}  // namespace

LabBluetoothDelegate::LabBluetoothDelegate() = default;
LabBluetoothDelegate::~LabBluetoothDelegate() = default;

std::unique_ptr<content::BluetoothChooser> LabBluetoothDelegate::RunBluetoothChooser(
    content::RenderFrameHost* frame,
    const content::BluetoothChooser::EventHandler& event_handler) {
  return std::make_unique<Chooser>(*frame, event_handler);
}

std::unique_ptr<content::BluetoothScanningPrompt>
LabBluetoothDelegate::ShowBluetoothScanningPrompt(
    content::RenderFrameHost* frame,
    const content::BluetoothScanningPrompt::EventHandler& event_handler) {
  auto& permissions = Permissions(*frame);
  const int setting = permissions.SiteSetting(kScanning, SiteOf(*frame));
  if (setting == 2) {
    event_handler.Run(content::BluetoothScanningPrompt::Event::kBlock);
  } else {
    permissions.RunSitePrompt(
        *frame, kScanning,
        base::BindOnce(
            [](content::BluetoothScanningPrompt::EventHandler events, bool allowed) {
              events.Run(allowed ? content::BluetoothScanningPrompt::Event::kAllow
                                 : content::BluetoothScanningPrompt::Event::kBlock);
            },
            event_handler));
  }
  return std::make_unique<ScanningPrompt>();
}

void LabBluetoothDelegate::ShowDevicePairPrompt(content::RenderFrameHost* frame,
                                                const std::u16string& device_identifier,
                                                PairPromptCallback callback,
                                                PairingKind pairing_kind,
                                                const std::optional<std::u16string>& pin) {
  auto* contents = content::WebContents::FromRenderFrameHost(frame);
  if (!contents || !contents->GetTopLevelNativeWindow()) {
    std::move(callback).Run(PairPromptResult(PairPromptStatus::kCancelled));
    return;
  }
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetTitle(prototype::T(u"Parowanie urządzenia Bluetooth"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Sparuj"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));
  dialog->SetContentsView(std::make_unique<PairPromptView>(
      *dialog, device_identifier, pairing_kind, pin, std::move(callback)));
  views::DialogDelegate::CreateDialogWidget(std::move(dialog), /*context=*/nullptr,
                                            contents->GetTopLevelNativeWindow())
      ->Show();
}

LabBluetoothDelegate::AddressToId& LabBluetoothDelegate::AddressesFor(
    content::RenderFrameHost* frame) {
  return addresses_[{reinterpret_cast<uintptr_t>(frame->GetBrowserContext()),
                     frame->GetLastCommittedOrigin(), SiteOf(*frame)}];
}

blink::WebBluetoothDeviceId LabBluetoothDelegate::IdFor(content::RenderFrameHost* frame,
                                                        const std::string& address) {
  auto& ids = AddressesFor(frame);
  auto found = ids.find(address);
  if (found != ids.end())
    return found->second;
  const auto id = blink::WebBluetoothDeviceId::Create();
  ids[address] = id;
  return id;
}

blink::WebBluetoothDeviceId LabBluetoothDelegate::GetWebBluetoothDeviceId(
    content::RenderFrameHost* frame,
    const std::string& device_address) {
  auto& ids = AddressesFor(frame);
  const auto found = ids.find(device_address);
  return found == ids.end() ? blink::WebBluetoothDeviceId() : found->second;
}

std::string LabBluetoothDelegate::GetDeviceAddress(content::RenderFrameHost* frame,
                                                   const blink::WebBluetoothDeviceId& device_id) {
  for (const auto& [address, id] : AddressesFor(frame)) {
    if (id == device_id)
      return address;
  }
  return {};
}

blink::WebBluetoothDeviceId LabBluetoothDelegate::AddScannedDevice(
    content::RenderFrameHost* frame,
    const std::string& device_address) {
  return IdFor(frame, device_address);
}

blink::WebBluetoothDeviceId LabBluetoothDelegate::GrantServiceAccessPermission(
    content::RenderFrameHost* frame,
    const device::BluetoothDevice* device,
    const blink::mojom::WebBluetoothRequestDeviceOptions* options) {
  const auto id = IdFor(frame, device->GetAddress());
  auto& grant = grants_[id];
  grant.name = device->GetName().value_or(std::string());
  if (options) {
    if (options->filters) {
      for (const auto& filter : *options->filters) {
        if (filter->services)
          grant.services.insert(filter->services->begin(), filter->services->end());
      }
    }
    grant.services.insert(options->optional_services.begin(), options->optional_services.end());
    grant.manufacturer_codes.insert(options->optional_manufacturer_data.begin(),
                                    options->optional_manufacturer_data.end());
  }
  return id;
}

bool LabBluetoothDelegate::HasDevicePermission(content::RenderFrameHost* frame,
                                               const blink::WebBluetoothDeviceId& device_id) {
  return MayUseBluetooth(frame) && grants_.contains(device_id) &&
         !GetDeviceAddress(frame, device_id).empty();
}

void LabBluetoothDelegate::RevokeDevicePermissionWebInitiated(
    content::RenderFrameHost* frame,
    const blink::WebBluetoothDeviceId& device_id) {
  grants_.erase(device_id);
  std::erase_if(AddressesFor(frame), [&](const auto& entry) { return entry.second == device_id; });
  for (auto& observer : observers_) {
    if (observer.GetRenderFrameHost() &&
        observer.GetRenderFrameHost()->GetLastCommittedOrigin() ==
            frame->GetLastCommittedOrigin()) {
      observer.OnPermissionRevoked(frame->GetLastCommittedOrigin());
    }
  }
}

bool LabBluetoothDelegate::MayUseBluetooth(content::RenderFrameHost* frame) {
  return frame && network::IsOriginPotentiallyTrustworthy(SiteOf(*frame)) &&
         Permissions(*frame).SiteSetting(kGuard, SiteOf(*frame)) != 2;
}

bool LabBluetoothDelegate::IsAllowedToAccessService(content::RenderFrameHost* frame,
                                                    const blink::WebBluetoothDeviceId& device_id,
                                                    const device::BluetoothUUID& service) {
  const auto grant = grants_.find(device_id);
  return HasDevicePermission(frame, device_id) && grant->second.services.contains(service);
}

bool LabBluetoothDelegate::IsAllowedToAccessAtLeastOneService(
    content::RenderFrameHost* frame,
    const blink::WebBluetoothDeviceId& device_id) {
  const auto grant = grants_.find(device_id);
  return HasDevicePermission(frame, device_id) && !grant->second.services.empty();
}

bool LabBluetoothDelegate::IsAllowedToAccessManufacturerData(
    content::RenderFrameHost* frame,
    const blink::WebBluetoothDeviceId& device_id,
    uint16_t manufacturer_code) {
  const auto grant = grants_.find(device_id);
  return HasDevicePermission(frame, device_id) &&
         grant->second.manufacturer_codes.contains(manufacturer_code);
}

std::vector<blink::mojom::WebBluetoothDevicePtr> LabBluetoothDelegate::GetPermittedDevices(
    content::RenderFrameHost* frame) {
  std::vector<blink::mojom::WebBluetoothDevicePtr> devices;
  if (!MayUseBluetooth(frame))
    return devices;
  for (const auto& [address, id] : AddressesFor(frame)) {
    const auto grant = grants_.find(id);
    if (grant == grants_.end())
      continue;
    auto device = blink::mojom::WebBluetoothDevice::New();
    device->id = id;
    device->name = grant->second.name;
    devices.push_back(std::move(device));
  }
  return devices;
}

void LabBluetoothDelegate::AddFramePermissionObserver(FramePermissionObserver* observer) {
  observers_.AddObserver(observer);
}

void LabBluetoothDelegate::RemoveFramePermissionObserver(FramePermissionObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool LabBluetoothDelegate::IsBluetoothScanningBlocked(content::BrowserContext* context,
                                                      const url::Origin&,
                                                      const url::Origin& embedding_origin) {
  return !context || static_cast<ProfileContext*>(context)->site_permissions().SiteSetting(
                         kScanning, embedding_origin) == 2;
}

void LabBluetoothDelegate::BlockBluetoothScanning(content::BrowserContext* context,
                                                  const url::Origin&,
                                                  const url::Origin& embedding_origin) {
  if (context) {
    static_cast<ProfileContext*>(context)->site_permissions().SetSiteRule(kScanning,
                                                                          embedding_origin, 2);
  }
}

}  // namespace zitter::content_embedder
