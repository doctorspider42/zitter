// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_metal.h"

#include <algorithm>
#include <cmath>

#include "base/apple/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/scoped_canvas.h"

#include "chromium/prototype/editor_text_layout_mac.inc"

namespace zitter::prototype {
namespace {

NSFont* ChromeFont(float size, int weight) {
  if(weight<=0)return editor_text_detail::Font(size, false);
  const CGFloat native_weight=weight>=900?NSFontWeightBlack:weight>=800?NSFontWeightHeavy:weight>=700?NSFontWeightBold:weight>=600?NSFontWeightSemibold:weight>=500?NSFontWeightMedium:weight>=400?NSFontWeightRegular:weight>=300?NSFontWeightLight:NSFontWeightThin;
  return [NSFont systemFontOfSize:std::isfinite(size)&&size>0?size:13 weight:native_weight];
}
}  // namespace

void PaintEditorSceneFallback(gfx::Canvas* canvas,base::span<const sidebar::Primitive> scene) {
  for(const auto& op:scene) {
    gfx::ScopedCanvas save(canvas);canvas->ClipRect(gfx::RectF(op.clip.x,op.clip.y,op.clip.width,op.clip.height));
    if(op.editor) {
      const int w=std::min(4096,int(std::ceil(op.width))),h=std::min(512,int(std::ceil(op.height)));if(w<=0||h<=0)continue;
      SkBitmap bitmap;bitmap.allocN32Pixels(w,h);bitmap.eraseColor(SK_ColorTRANSPARENT);
      base::apple::ScopedCFTypeRef<CGColorSpaceRef> space(CGColorSpaceCreateDeviceRGB());
      base::apple::ScopedCFTypeRef<CGContextRef> context(CGBitmapContextCreate(bitmap.getPixels(),w,h,8,bitmap.rowBytes(),space.get(),static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst)|static_cast<uint32_t>(kCGBitmapByteOrder32Little)));
      if(!context)continue;const std::u16string text(op.utf16.begin(),op.utf16.end());
      EditorTextLayout line(text,op.font_size,op.bold,op.text_direction);line.Draw(context.get(),op.text_x,op.height-op.baseline,op.color);
      canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(bitmap),int(op.x),int(op.y));
    } else {
      cc::PaintFlags flags;flags.setColor(op.color);flags.setAntiAlias(true);if(op.stroke>0){flags.setStyle(cc::PaintFlags::kStroke_Style);flags.setStrokeWidth(op.stroke);}
      canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);
    }
  }
}
std::vector<std::string> ShapeSidebarParagraph(std::string_view text,float font_size,int width,bool bold) {
  return ShapeSidebarParagraphWeight(text,font_size,width,bold?500:400);
}
std::vector<std::string> ShapeSidebarParagraphWeight(std::string_view text,float font_size,int width,int weight) {
  std::vector<std::string> lines;if(text.empty()||width<=0||!std::isfinite(font_size)||font_size<=0)return lines;
  NSString* string=base::SysUTF8ToNSString(std::string(text));
  NSFont* font=ChromeFont(font_size,weight);
  NSAttributedString* attributed=[[NSAttributedString alloc] initWithString:string attributes:@{NSFontAttributeName:font}];
  base::apple::ScopedCFTypeRef<CTTypesetterRef> typesetter(CTTypesetterCreateWithAttributedString((__bridge CFAttributedStringRef)attributed));
  if(!typesetter)return lines;
  for(NSUInteger offset=0;offset<string.length;) {
    CFIndex count=CTTypesetterSuggestLineBreak(typesetter.get(),offset,width);
    if(count<=0)count=[string rangeOfComposedCharacterSequenceAtIndex:offset].length;
    const NSUInteger length=std::min(static_cast<NSUInteger>(count),string.length-offset);
    lines.push_back(base::SysNSStringToUTF8([string substringWithRange:NSMakeRange(offset,length)]));offset+=length;
  }
  return lines;
}
}  // namespace zitter::prototype
