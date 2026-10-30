# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Minimal independent Chromium source layout used by overlay transaction tests."""

def seed(checkout):
    source = checkout / 'ui/views/controls/textfield/textfield_model.cc'
    if source.exists():
        return
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text('''#include "ui/views/controls/textfield/textfield_model.h"
namespace views {
namespace {
// Native storage and behavior remain unchanged by the hook.
std::u16string* GetKillBuffer();
void SetKillBuffer(std::u16string_view text);
} // namespace
/////////////////////////////////////////////////////////////////
// TextfieldModel: public
void PreserveExistingModelBehavior();
} // namespace views
''', newline="\n")
    (checkout / 'ui/views/BUILD.gn').write_text('''source_set("views") {
  sources = [
    "controls/textfield/textfield_model.h",
  ]
}
''', newline="\n")
