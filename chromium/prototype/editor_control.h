// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_EDITOR_CONTROL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_EDITOR_CONTROL_H_
#include <optional>
#include <string>
#include <string_view>
#include "base/functional/callback.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/gfx/range/range.h"
#include "ui/views/view.h"
namespace ui { class KeyEvent; }
namespace zitter::prototype {
// Service-facing field contract. Each implementation is exactly one owned View;
// the Views baseline has no extra wrapper or second editing model.
class EditorControl {
 public:
  virtual ~EditorControl()=default;
  virtual views::View* view()=0;
  virtual std::u16string_view GetText() const=0;
  virtual void SetText(std::u16string_view text)=0;
  virtual void SetPlaceholderText(std::u16string text)=0;
  virtual bool IsIMEComposing() const=0;
  virtual void SetCompositionText(const ui::CompositionText& text)=0;
  virtual void ClearCompositionText()=0;
  virtual size_t ConfirmCompositionText(bool keep_selection)=0;
  virtual gfx::Range GetSelectedRange() const=0;
  virtual void SelectRange(const gfx::Range& range)=0;
  virtual ui::TextInputClient* input_client()=0;
  virtual void SetReadOnly(bool value)=0;
  virtual void SetPresentation(int font_size,float inset,bool framed,std::optional<SkColor> fill)=0;
  // User commits (including same-text insertion): resets/preedit/cancel are silent.
  // The domain reads current text in its callback; no borrowed text survives it.
  virtual void SetCallbacks(base::RepeatingClosure committed,
      base::RepeatingCallback<bool(const ui::KeyEvent&)> key)=0;
  virtual bool rust_editor() const=0;
  virtual bool metal_visible() const=0;
  virtual bool scene_contains(std::string_view) const=0;
  void RequestFocus(){view()->RequestFocus();}
  bool HasFocus(){return view()->HasFocus();}
  views::ViewAccessibility& GetViewAccessibility(){return view()->GetViewAccessibility();}
};
EditorControl* AddEditor(views::View* parent);
}
#endif
