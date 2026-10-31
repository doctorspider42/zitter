// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/installer/setup_accessibility.h"

#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <array>
#include <optional>

namespace zitter::setup {
namespace {
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

// The window keeps its host and its root provider (one reference) as
// properties, so providers hold nothing but the window.
constexpr wchar_t kHostProperty[] = L"ZitterAccessibilityHost";
constexpr wchar_t kRootProperty[] = L"ZitterAccessibilityRoot";

AccessibilityHost* Host(HWND hwnd) {
  return IsWindow(hwnd) ? reinterpret_cast<AccessibilityHost*>(GetPropW(hwnd, kHostProperty)) : nullptr;
}

IRawElementProviderFragmentRoot* Root(HWND hwnd) {
  return IsWindow(hwnd) ? reinterpret_cast<IRawElementProviderFragmentRoot*>(GetPropW(hwnd, kRootProperty)) : nullptr;
}

std::optional<AccessibleItem> Lookup(HWND hwnd, int id) {
  const AccessibilityHost* host = Host(hwnd);
  if (!host) return std::nullopt;
  for (auto& item : host->AccessibleItems())
    if (item.id == id) return item;
  return std::nullopt;
}

void SetString(VARIANT* value, const std::wstring& text) {
  value->vt = VT_BSTR;
  value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
}

void SetBool(VARIANT* value, bool on) {
  value->vt = VT_BOOL;
  value->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetInt(VARIANT* value, int number) {
  value->vt = VT_I4;
  value->lVal = number;
}

HRESULT IntArray(const std::array<int, 2>& numbers, SAFEARRAY** out) {
  *out = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(numbers.size()));
  if (!*out) return E_OUTOFMEMORY;
  for (LONG i = 0; i < static_cast<LONG>(numbers.size()); ++i) {
    int number = numbers.at(static_cast<size_t>(i));
    SafeArrayPutElement(*out, &i, &number);
  }
  return S_OK;
}

ComPtr<IRawElementProviderFragment> MakeItem(HWND hwnd, int id);


class Item : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple, IRawElementProviderFragment,
                                 IInvokeProvider, IToggleProvider, ISelectionItemProvider> {
 public:
  Item(HWND hwnd, int id) : hwnd_(hwnd), id_(id) {}

  // IRawElementProviderSimple
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override {
    // COM threading: UIA calls in on the window's thread, which owns the state.
    *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  IFACEMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
    *out = nullptr;
    const auto item = Lookup(hwnd_, id_);
    if (!item) return UIA_E_ELEMENTNOTAVAILABLE;
    if (pattern == UIA_InvokePatternId && item->role == Role::kButton) *out = static_cast<IInvokeProvider*>(this);
    if (pattern == UIA_TogglePatternId && item->role == Role::kSwitch) *out = static_cast<IToggleProvider*>(this);
    if (pattern == UIA_SelectionItemPatternId && item->role == Role::kRadio)
      *out = static_cast<ISelectionItemProvider*>(this);
    if (*out) AddRef();
    return S_OK;
  }
  IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override {
    value->vt = VT_EMPTY;
    const auto item = Lookup(hwnd_, id_);
    if (!item) return UIA_E_ELEMENTNOTAVAILABLE;
    const bool focusable = item->role != Role::kText && item->role != Role::kProgress;
    switch (property) {
      case UIA_ControlTypePropertyId: {
        // A switch is a button with the Toggle pattern, as Windows' own switches.
        int type = UIA_TextControlTypeId;
        if (item->role == Role::kButton || item->role == Role::kSwitch) type = UIA_ButtonControlTypeId;
        if (item->role == Role::kRadio) type = UIA_RadioButtonControlTypeId;
        if (item->role == Role::kProgress) type = UIA_ProgressBarControlTypeId;
        SetInt(value, type);
        break;
      }
      case UIA_NamePropertyId: SetString(value, item->name); break;
      case UIA_HelpTextPropertyId: if (!item->help.empty()) SetString(value, item->help); break;
      case UIA_AutomationIdPropertyId: if (!item->automation_id.empty()) SetString(value, item->automation_id); break;
      case UIA_IsKeyboardFocusablePropertyId: SetBool(value, focusable); break;
      case UIA_HasKeyboardFocusPropertyId: SetBool(value, focusable && Host(hwnd_)->AccessibleFocus() == id_); break;
      case UIA_IsEnabledPropertyId: case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId:
        SetBool(value, true);
        break;
      default: break;
    }
    return S_OK;
  }
  IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override {
    *host = nullptr;
    return S_OK;
  }

  // IRawElementProviderFragment
  IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
    *out = nullptr;
    const AccessibilityHost* host = Host(hwnd_);
    if (!host) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction == NavigateDirection_Parent) {
      ComPtr<IRawElementProviderFragment> root;
      if (IRawElementProviderFragmentRoot* fragment_root = Root(hwnd_)) fragment_root->QueryInterface(IID_PPV_ARGS(&root));
      *out = root.Detach();
      return S_OK;
    }
    if (direction != NavigateDirection_NextSibling && direction != NavigateDirection_PreviousSibling) return S_OK;
    const auto items = host->AccessibleItems();
    const auto at = std::find_if(items.begin(), items.end(), [&](const AccessibleItem& item) { return item.id == id_; });
    if (at == items.end()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction == NavigateDirection_NextSibling && std::next(at) != items.end())
      *out = MakeItem(hwnd_, std::next(at)->id).Detach();
    else if (direction == NavigateDirection_PreviousSibling && at != items.begin())
      *out = MakeItem(hwnd_, std::prev(at)->id).Detach();
    return S_OK;
  }
  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override { return IntArray({UiaAppendRuntimeId, id_}, out); }
  IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override {
    const auto item = Lookup(hwnd_, id_);
    if (!item) return UIA_E_ELEMENTNOTAVAILABLE;
    *rect = {static_cast<double>(item->bounds.left), static_cast<double>(item->bounds.top),
             static_cast<double>(item->bounds.right - item->bounds.left),
             static_cast<double>(item->bounds.bottom - item->bounds.top)};
    return S_OK;
  }
  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP SetFocus() override {
    AccessibilityHost* host = Host(hwnd_);
    if (!host || !Lookup(hwnd_, id_)) return UIA_E_ELEMENTNOTAVAILABLE;
    host->SetAccessibleFocus(id_);
    return S_OK;
  }
  IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    *out = Root(hwnd_);
    if (*out) (*out)->AddRef();
    return S_OK;
  }

  // IInvokeProvider
  IFACEMETHODIMP Invoke() override { return Activate(); }

  // IToggleProvider
  IFACEMETHODIMP Toggle() override { return Activate(); }
  IFACEMETHODIMP get_ToggleState(ToggleState* state) override {
    const auto item = Lookup(hwnd_, id_);
    if (!item) return UIA_E_ELEMENTNOTAVAILABLE;
    *state = item->on ? ToggleState_On : ToggleState_Off;
    return S_OK;
  }

  // ISelectionItemProvider
  IFACEMETHODIMP Select() override { return Activate(); }
  IFACEMETHODIMP AddToSelection() override { return Activate(); }
  IFACEMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
  IFACEMETHODIMP get_IsSelected(BOOL* selected) override {
    const auto item = Lookup(hwnd_, id_);
    if (!item) return UIA_E_ELEMENTNOTAVAILABLE;
    *selected = item->on;
    return S_OK;
  }
  IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** out) override {
    *out = nullptr;
    return S_OK;
  }

 private:
  HRESULT Activate() {
    AccessibilityHost* host = Host(hwnd_);
    if (!host || !Lookup(hwnd_, id_)) return UIA_E_ELEMENTNOTAVAILABLE;
    host->AccessibleActivate(id_);
    return S_OK;
  }

  const HWND hwnd_;
  const int id_;
};

ComPtr<IRawElementProviderFragment> MakeItem(HWND hwnd, int id) {
  return Make<Item>(hwnd, id);
}

class RootProvider : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                         IRawElementProviderFragment, IRawElementProviderFragmentRoot> {
 public:
  explicit RootProvider(HWND hwnd) : hwnd_(hwnd) {}

  // IRawElementProviderSimple: the window's own provider supplies its name,
  // type and bounds.
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override {
    // COM threading: UIA calls in on the window's thread, which owns the state.
    *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* value) override {
    value->vt = VT_EMPTY;
    return S_OK;
  }
  IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override {
    return UiaHostProviderFromHwnd(hwnd_, host);
  }

  // IRawElementProviderFragment
  IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
    *out = nullptr;
    const AccessibilityHost* host = Host(hwnd_);
    if (!host || (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)) return S_OK;
    const auto items = host->AccessibleItems();
    if (!items.empty())
      *out = MakeItem(hwnd_, direction == NavigateDirection_FirstChild ? items.front().id : items.back().id).Detach();
    return S_OK;
  }
  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override {
    *rect = {};
    return S_OK;
  }
  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP SetFocus() override { return S_OK; }
  IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    *out = this;
    AddRef();
    return S_OK;
  }

  // IRawElementProviderFragmentRoot
  IFACEMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
    *out = nullptr;
    const AccessibilityHost* host = Host(hwnd_);
    if (!host) return S_OK;
    const POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    for (const auto& item : host->AccessibleItems()) {
      if (item.role != Role::kText && PtInRect(&item.bounds, point)) {
        *out = MakeItem(hwnd_, item.id).Detach();
        break;
      }
    }
    return S_OK;
  }
  IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override {
    *out = nullptr;
    const AccessibilityHost* host = Host(hwnd_);
    if (host && host->AccessibleFocus()) *out = MakeItem(hwnd_, host->AccessibleFocus()).Detach();
    return S_OK;
  }

 private:
  const HWND hwnd_;
};

}  // namespace

LRESULT AnswerGetObject(HWND hwnd, AccessibilityHost* host, WPARAM w, LPARAM l) {
  if (static_cast<long>(l) != static_cast<long>(UiaRootObjectId)) return DefWindowProcW(hwnd, WM_GETOBJECT, w, l);
  if (!Root(hwnd)) {
    SetPropW(hwnd, kHostProperty, reinterpret_cast<HANDLE>(host));
    ComPtr<IRawElementProviderFragmentRoot> root = Make<RootProvider>(hwnd);
    SetPropW(hwnd, kRootProperty, reinterpret_cast<HANDLE>(root.Detach()));
  }
  ComPtr<IRawElementProviderSimple> simple;
  Root(hwnd)->QueryInterface(IID_PPV_ARGS(&simple));
  return UiaReturnRawElementProvider(hwnd, w, l, simple.Get());
}

void NotifyFocusChanged(HWND hwnd, int id) {
  if (!id || !Root(hwnd) || !UiaClientsAreListening()) return;
  ComPtr<IRawElementProviderSimple> item;
  MakeItem(hwnd, id).As(&item);
  UiaRaiseAutomationEvent(item.Get(), UIA_AutomationFocusChangedEventId);
}

void NotifyToggled(HWND hwnd, int id, bool on) {
  if (!Root(hwnd) || !UiaClientsAreListening()) return;
  const auto item = Lookup(hwnd, id);
  if (!item || (item->role != Role::kSwitch && item->role != Role::kRadio)) return;
  ComPtr<IRawElementProviderSimple> provider;
  MakeItem(hwnd, id).As(&provider);
  VARIANT before{}, after{};
  if (item->role == Role::kSwitch) {
    SetInt(&before, on ? ToggleState_Off : ToggleState_On);
    SetInt(&after, on ? ToggleState_On : ToggleState_Off);
    UiaRaiseAutomationPropertyChangedEvent(provider.Get(), UIA_ToggleToggleStatePropertyId, before, after);
  } else if (on) {
    UiaRaiseAutomationEvent(provider.Get(), UIA_SelectionItem_ElementSelectedEventId);
  }
}

void NotifyChildrenChanged(HWND hwnd) {
  IRawElementProviderFragmentRoot* root = Root(hwnd);
  if (!root || !UiaClientsAreListening()) return;
  ComPtr<IRawElementProviderSimple> simple;
  root->QueryInterface(IID_PPV_ARGS(&simple));
  UiaRaiseStructureChangedEvent(simple.Get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

void DisconnectAccessibility(HWND hwnd) {
  if (IRawElementProviderFragmentRoot* root = Root(hwnd)) {
    ComPtr<IRawElementProviderSimple> simple;
    root->QueryInterface(IID_PPV_ARGS(&simple));
    UiaDisconnectProvider(simple.Get());
    RemovePropW(hwnd, kRootProperty);
    root->Release();
  }
  RemovePropW(hwnd, kHostProperty);
  UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
}

}  // namespace zitter::setup
