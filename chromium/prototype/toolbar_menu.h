// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_TOOLBAR_MENU_H_
#define ZITTER_CHROMIUM_PROTOTYPE_TOOLBAR_MENU_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class Canvas;
class FontList;
class PointF;
class RectF;
}

namespace zitter::prototype {

inline constexpr int kMenuWidth = 296;
inline constexpr int kFooterHeight = 30;
inline constexpr char kWarnIcon[] = "M8 2.5 14 13H2zM8 6.5v3M8 11.2v.1";
inline constexpr char kGearIcon[] =
    "M8 2v1.5M8 12.5V14M2 8h1.5M12.5 8H14M3.8 3.8l1 1M11.2 11.2l1 1M3.8 12.2l1-1M11.2 4.8l1-1";
inline constexpr char kShieldIcon[] =
    "M8 2.5 13 4.5V8a5 6 0 0 1-5 5.5A5 6 0 0 1 3 8V4.5zM5.5 7.5h5";
inline constexpr char kShieldOffIcon[] =
    "M8 2.5 13 4.5V8a5 6 0 0 1-5 5.5A5 6 0 0 1 3 8V4.5zM2.5 2.5l11 11";

void StrokeIcon(gfx::Canvas* canvas, std::string_view path, gfx::RectF box,
                SkColor color);
void FillCircle(gfx::Canvas* canvas, gfx::PointF center, float radius,
                SkColor color, bool stroke = false);
void FillRound(gfx::Canvas* canvas, gfx::RectF rect, float radius, SkColor color);
gfx::FontList DesignFont(uint32_t family, int size, int weight);

// A menu row: an extension (icon, name, site access, pin and more) or a
// footer command (icon, label).
class MenuRow final : public views::Button {
  METADATA_HEADER(MenuRow, views::Button)
 public:
  MenuRow(PressedCallback callback, const sidebar::DesignPalette& palette, int height);
  ~MenuRow() override;
  // Extension rows: the icon view, then text drawn by the row, then actions.
  void SetText(std::u16string title, std::u16string detail, bool warn, int lead);
  void SetGlyph(const char* glyph);
  // The gear: spokes from the path and a ring in the middle.
  void SetRing(bool ring);
  void SetSelected(bool selected);
  // Pin and more appear on hover; a pinned pin stays.
  void AddTrailing(views::View* view, bool always);
  views::View* text_slot();
  void Init();
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnFocus() override;
  void OnBlur() override;
 private:
  void UpdateTrailing();
  sidebar::DesignPalette palette_;
  std::u16string title_, detail_;
  bool warn_ = false;
  int lead_ = 0;
  const char* glyph_ = nullptr;
  bool ring_ = false;
  bool selected_ = false;
  raw_ptr<views::View> icon_slot_ = nullptr;
  raw_ptr<views::View> slot_ = nullptr;
  std::vector<std::pair<raw_ptr<views::View>, bool>> trailing_;
};

// The eyebrow above the rows: "ROZSZERZENIA" (or the panel's own label).
class MenuHeader final : public views::View {
  METADATA_HEADER(MenuHeader, views::View)
 public:
  explicit MenuHeader(const sidebar::DesignPalette& palette, std::u16string eyebrow = {});
  void OnPaint(gfx::Canvas* canvas) override;
 private:
  std::u16string eyebrow_;
  sidebar::DesignPalette palette_;
};

class MenuSeparator final : public views::View {
  METADATA_HEADER(MenuSeparator, views::View)
 public:
  explicit MenuSeparator(SkColor color);
  void OnPaint(gfx::Canvas* canvas) override;
 private:
  SkColor color_;
};

// A count of -1 indicates a site exception.
class ShieldButton final : public views::Button {
  METADATA_HEADER(ShieldButton, views::Button)
 public:
  ShieldButton(PressedCallback callback,
               base::RepeatingCallback<sidebar::DesignPalette()> palette);
  ~ShieldButton() override;
  void SetOpen(bool open);
  void SetCount(int count);
  int count() const { return count_; }
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  bool open_ = false;
  int count_ = 0;
};

class ShieldCard final : public views::View {
  METADATA_HEADER(ShieldCard, views::View)
 public:
  explicit ShieldCard(const sidebar::DesignPalette& palette);
  void Set(int count, std::u16string site);
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  std::u16string Detail() const;
  const sidebar::DesignPalette palette_;
  int count_ = 0;
  std::u16string site_;
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_TOOLBAR_MENU_H_
