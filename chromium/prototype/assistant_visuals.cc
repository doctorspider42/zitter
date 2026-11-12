// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_visuals.h"

#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/assistant_providers.h"
#include <algorithm>
#include <array>
#include "third_party/skia/include/utils/SkParsePath.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/settings_icon.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/scoped_observation.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "cc/paint/paint_shader.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "base/strings/string_util.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_split.h"
#include "base/timer/timer.h"
#include "ui/views/view_utils.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/resize_area.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/display/screen.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"

namespace zitter::prototype {

std::u16string AssistantProviderLabel(std::string_view provider) {
  constexpr std::array<const char16_t*, 4> kProviderLabels = {
      u"OpenAI", u"Codex", u"Claude Code", u"Ollama"};
  for (size_t i = 0; i < kAssistantProviders.size(); ++i) {
    if (provider == kAssistantProviders[i])
      return kProviderLabels[i];
  }
  return base::UTF8ToUTF16(provider);
}

void PaintAssistantSpark(gfx::Canvas* canvas, const gfx::RectF& box, SkColor color) {
  cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(1.5f * box.width() / 16.f);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap); flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  auto transform = SkMatrix::Translate(box.x(), box.y());
  transform.preScale(box.width() / 16.f, box.height() / 16.f);
  canvas->DrawPath(SkParsePath::FromSVGString(kAssistantSparkIcon)->makeTransform(transform), flags);
}
namespace {
struct Colors { SkColor bg, card, ink, muted, line, accent, raised, strong, warn, ok, danger, on_accent; bool dark; };
Colors Theme(const views::View* view) {
  const auto* provider = view->GetColorProvider();
  const auto bg = provider ? provider->GetColor(ui::kColorDialogBackground) : SK_ColorBLACK;
  const bool dark = SkColorGetR(bg) + SkColorGetG(bg) + SkColorGetB(bg) < 384;
  return dark ? Colors{0xff111217,0xff1a1c24,0xfff3f3f7,0xff979ba9,0xff30323e,0xffb4a6ff,0xff20222b,0xff6f7283,0xffffbe55,0xff6fd6cb,0xfff29bab,0xff171322,true}
              : Colors{0xfffaf9f6,0xfff0efed,0xff22232b,0xff6b6d7a,0xffdedde3,0xff6d54d8,0xffffffff,0xff8c8a96,0xffa15c00,0xff0f766e,0xffb63c58,0xffffffff,false};
}
gfx::FontList Type(int size, int weight = 400, bool display = false) {
  return gfx::FontList({std::string(sidebar::design_font_family(display ? 1 : 2))}, display ? gfx::Font::ITALIC : gfx::Font::NORMAL,
      size, static_cast<gfx::Font::Weight>(weight));
}
gfx::FontList Mono(int size) {
  return gfx::FontList({std::string(sidebar::design_font_family(3))}, gfx::Font::NORMAL, size, gfx::Font::Weight::NORMAL);
}
void Round(gfx::Canvas* canvas, gfx::RectF rect, float radius, SkColor color, float stroke = 0) {
  cc::PaintFlags f; f.setAntiAlias(true); f.setColor(color);
  if (stroke) { f.setStyle(cc::PaintFlags::kStroke_Style); f.setStrokeWidth(stroke); }
  canvas->DrawRoundRect(rect,radius,f);
}
void Glyph(gfx::Canvas* canvas, AssistantGlyph glyph, float x, float y, SkColor color) {
  cc::PaintFlags f; f.setAntiAlias(true); f.setColor(color); f.setStrokeWidth(1.5f); f.setStrokeCap(cc::PaintFlags::kRound_Cap); f.setStrokeJoin(cc::PaintFlags::kRound_Join); f.setStyle(cc::PaintFlags::kStroke_Style);
  canvas->Save(); canvas->Translate(gfx::Vector2d(x,y));
  auto line=[&](float x1,float y1,float x2,float y2){canvas->DrawLine(gfx::PointF(x1,y1),gfx::PointF(x2,y2),f);};
  switch(glyph) {
    case AssistantGlyph::kPlus: line(8,3,8,13);line(3,8,13,8);break;
    case AssistantGlyph::kClose: line(4,4,12,12);line(12,4,4,12);break;
    case AssistantGlyph::kArrow: line(8,13,8,3);line(3.5,7.5,8,3);line(8,3,12.5,7.5);break;
    case AssistantGlyph::kStop: Round(canvas,gfx::RectF(4,4,8,8),2,color);break;
    case AssistantGlyph::kRevoke: canvas->DrawCircle(gfx::PointF(8,8),5.5f,f);line(4,4,12,12);break;
    case AssistantGlyph::kAt: canvas->DrawStringRectWithFlags(u"@",Type(17,500),color,gfx::Rect(0,-2,17,21),gfx::Canvas::TEXT_ALIGN_CENTER);break;
    case AssistantGlyph::kHistory: canvas->DrawCircle(gfx::PointF(8,8),5.5f,f);line(8,4.5,8,8);line(8,8,10.5,9.5);break;
    case AssistantGlyph::kSearch: canvas->DrawCircle(gfx::PointF(6,6),4.5f,f);line(9.5,9.5,14,14);break;
    case AssistantGlyph::kChevron: line(5,6,8,9);line(8,9,11,6);break;
    case AssistantGlyph::kPin: line(5,3,11,3);line(6,3,6,7);line(10,3,10,7);line(6,7,3.5,10);line(10,7,12.5,10);line(3.5,10,12.5,10);line(8,10,8,14);break;
    case AssistantGlyph::kShield: { SkPathBuilder p; p.moveTo(8,2);p.lineTo(13,4);p.lineTo(12,10);p.quadTo(10,13,8,14);p.quadTo(6,13,4,10);p.lineTo(3,4);p.close();canvas->DrawPath(p.detach(),f);line(5.5,7.5,7.5,9.5);line(7.5,9.5,11,6);break; }
    case AssistantGlyph::kPage: Round(canvas,gfx::RectF(3,2,10,12),2,color,1.5);line(6,6,10,6);line(6,9,10,9);break;
    case AssistantGlyph::kSpark: PaintAssistantSpark(canvas,gfx::RectF(0,0,16,16),color);break;
    case AssistantGlyph::kLayers: Round(canvas,gfx::RectF(5.5f,2.5f,8,7),1.5f,color,1.5f);line(3,6,3,12);line(3,12.5f,10.5f,12.5f);break;
    case AssistantGlyph::kCheck: line(3.5f,8.5f,6.5f,11.5f);line(6.5f,11.5f,12.5f,4.5f);break;
    case AssistantGlyph::kGear: gfx::PaintVectorIcon(canvas, kSettingsGearIcon, 16, color); break;
    case AssistantGlyph::kCopy: Round(canvas,gfx::RectF(5.5f,5.5f,8,8),1.5f,color,1.5f);line(3,10.5f,3,4);line(3,3,9.5f,3);break;
    case AssistantGlyph::kTrash: line(3,4.5f,13,4.5f);line(6.5f,4.5f,6.5f,3);line(6.5f,3,9.5f,3);line(9.5f,3,9.5f,4.5f);
      line(4.5f,4.5f,5.1f,13);line(5.1f,13,10.9f,13);line(10.9f,13,11.5f,4.5f);break;
    case AssistantGlyph::kLock: { Round(canvas,gfx::RectF(3.5f,7,9,6.5f),1.5f,color,1.5f);
      SkPathBuilder p; p.moveTo(5.5f,7);p.lineTo(5.5f,5.5f);p.cubicTo(5.5f,2.2f,10.5f,2.2f,10.5f,5.5f);p.lineTo(10.5f,7);canvas->DrawPath(p.detach(),f);break; }
    case AssistantGlyph::kEye: { SkPathBuilder p; p.moveTo(1.8f,8);p.quadTo(8,1.6f,14.2f,8);p.quadTo(8,14.4f,1.8f,8);p.close();canvas->DrawPath(p.detach(),f);canvas->DrawCircle(gfx::PointF(8,8),2,f);break; }
    case AssistantGlyph::kExpand: line(9.5f,3,13,3);line(13,3,13,6.5f);line(13,3,8,8);line(11,9.5f,11,12);line(11,12.5f,4,12.5f);line(3.5f,12,3.5f,5);line(4,4.5f,6.5f,4.5f);break;
    case AssistantGlyph::kCalendar: Round(canvas,gfx::RectF(2.5f,3.5f,11,10),2,color,1.5f);line(2.5f,6.5f,13.5f,6.5f);line(5.5f,2,5.5f,5);line(10.5f,2,10.5f,5);
      line(8,8.5f,8,10.5f);line(8,10.5f,9.3f,11.3f);break;
    case AssistantGlyph::kRepeat: { SkPathBuilder p; p.moveTo(12.5f,6);p.quadTo(10.5f,2.6f,6.8f,3.1f);p.quadTo(4.6f,3.5f,3.6f,5);canvas->DrawPath(p.detach(),f);
      line(3.5f,2.5f,3.5f,5);line(3.5f,5,6,5);
      SkPathBuilder q; q.moveTo(3.5f,10);q.quadTo(5.5f,13.4f,9.2f,12.9f);q.quadTo(11.4f,12.5f,12.4f,11);canvas->DrawPath(q.detach(),f);
      line(12.5f,13.5f,12.5f,11);line(12.5f,11,10,11);break; }
    case AssistantGlyph::kBack: line(10,3.5f,5.5f,8);line(5.5f,8,10,12.5f);break;
    case AssistantGlyph::kGlobe: { canvas->DrawCircle(gfx::PointF(8,8),5.5f,f);line(2.5f,8,13.5f,8);
      SkPathBuilder p; p.addOval(SkRect::MakeXYWH(5.4f,2.5f,5.2f,11));canvas->DrawPath(p.detach(),f);break; }
  }
  canvas->Restore();
}
class AssistantBackground final : public views::Background {
 public:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const auto c=Theme(view); Round(canvas,gfx::RectF(view->GetLocalBounds()),20,c.bg);
    auto edge=gfx::RectF(view->GetLocalBounds());edge.Inset(.5f);Round(canvas,edge,20,c.line,1);
  }
};
class Identity final : public views::View {
  METADATA_HEADER(Identity,views::View)
 public:
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {120,32};}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);Glyph(canvas,AssistantGlyph::kSpark,1,8,c.accent);
    canvas->DrawStringRectWithFlags(T(u"Asystent"),Type(15,600),c.ink,gfx::Rect(25,0,std::max(0,width()-25),32),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
};
BEGIN_METADATA(Identity)
END_METADATA
class Action final : public views::Button {
  METADATA_HEADER(Action,views::Button)
 public:
  Action(base::RepeatingClosure action,AssistantGlyph glyph,std::u16string text,bool primary,bool chip,bool option=false)
      :Button(base::BindRepeating(std::move(action))),glyph_(glyph),text_(std::move(text)),primary_(primary),chip_(chip),option_(option) {
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);GetViewAccessibility().SetName(text_);SetTooltipText(text_);
  }
  void Update(std::u16string text,bool selected) {text_=std::move(text);selected_=selected;GetViewAccessibility().SetName(text_);SetTooltipText(text_);PreferredSizeChanged();SchedulePaint();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    if(option_)return {148,40};
    const bool disclosure=chip_&&(glyph_==AssistantGlyph::kSpark||glyph_==AssistantGlyph::kShield);
    return {chip_?std::min(280,gfx::GetStringWidth(text_,Type(12,500))+(disclosure?58:48)):primary_?36:28,primary_?36:28};
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    const bool monochrome = !chip_ && (glyph_ == AssistantGlyph::kHistory ||
                                      glyph_ == AssistantGlyph::kCalendar ||
                                      glyph_ == AssistantGlyph::kShield);
    const auto selected_fill = monochrome ? c.raised : SkColorSetA(c.accent, c.dark ? 35 : 22);
    const auto selected_ink = monochrome ? c.ink : c.accent;
    auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    const auto fill=primary_?c.accent:selected_?selected_fill:hover?c.card:SK_ColorTRANSPARENT;
    Round(canvas,rect,chip_?10:primary_?18:9,fill);
    if(chip_&&selected_)Round(canvas,rect,10,SkColorSetA(c.accent,60),1);
    const auto ink=!GetEnabled()?SkColorSetA(c.muted,100):glyph_==AssistantGlyph::kRevoke?(c.dark?0xfff29bab:0xffb63c58):primary_?(c.dark?0xff171322:SK_ColorWHITE):selected_?selected_ink:c.muted;
    if(option_){
      if(!selected_)Round(canvas,rect,10,c.line,1);
      canvas->DrawStringRectWithFlags(text_,Type(12,500),ink,gfx::Rect(12,0,std::max(0,width()-36),height()),gfx::Canvas::TEXT_ALIGN_LEFT);
      if(selected_)Glyph(canvas,AssistantGlyph::kSpark,width()-25,(height()-16)/2,ink);
      return;
    }
    const int gx=chip_?8:(width()-16)/2;Glyph(canvas,glyph_,gx,(height()-16)/2,ink);
    if(chip_){
      const bool disclosure=glyph_==AssistantGlyph::kSpark||glyph_==AssistantGlyph::kShield;
      canvas->DrawStringRectWithFlags(text_,Type(12,500),ink,gfx::Rect(31,0,std::max(0,width()-(disclosure?56:39)),height()),gfx::Canvas::TEXT_ALIGN_LEFT);
      if(disclosure){const int end=31+std::min(gfx::GetStringWidth(text_,Type(12,500)),std::max(0,width()-56));Glyph(canvas,AssistantGlyph::kChevron,end+4,(height()-16)/2,ink);}
    }
  }
 private:
  AssistantGlyph glyph_;std::u16string text_;bool primary_,chip_,option_,selected_=false;
};
BEGIN_METADATA(Action)
END_METADATA
class Grip final : public views::ResizeArea {
  METADATA_HEADER(Grip,views::ResizeArea)
 public:
  explicit Grip(views::ResizeAreaDelegate* delegate):ResizeArea(delegate){GetViewAccessibility().SetName(T(u"Zmień szerokość asystenta"));SetTooltipText(T(u"Przeciągnij, aby zmienić szerokość"));}
  void OnMouseEntered(const ui::MouseEvent&) override {hovered_=true;SchedulePaint();}
  void OnMouseExited(const ui::MouseEvent&) override {hovered_=false;SchedulePaint();}
  void OnPaint(gfx::Canvas* canvas) override {const auto c=Theme(this);Round(canvas,gfx::RectF(3,height()/2.f-20,2,40),1,hovered_?c.accent:SkColorSetA(c.muted,90));}
 private:bool hovered_=false;
};
BEGIN_METADATA(Grip)
END_METADATA
class Surface final : public views::View,public views::FocusChangeListener {
  METADATA_HEADER(Surface,views::View)
 public:
  explicit Surface(int kind):kind_(kind){}
  void AddedToWidget() override {views::View::AddedToWidget();if(kind_==1&&GetFocusManager())focus_.Observe(GetFocusManager());}
  void RemovedFromWidget() override {focus_.Reset();views::View::RemovedFromWidget();}
  void OnDidChangeFocus(views::View* before,views::View* now) override {if((before&&Contains(before))||(now&&Contains(now)))SchedulePaint();}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    if(kind_==4){
      // Popover: raised fill, a hairline and a soft drop below.
      Round(canvas,rect,12,c.raised);Round(canvas,rect,12,c.line,1);return;
    }
    Round(canvas,rect,kind_==1?18:14,kind_==2?SkColorSetA(c.accent,c.dark?20:14):c.card);
    const bool focused=kind_==1&&GetFocusManager()&&GetFocusManager()->GetFocusedView()&&Contains(GetFocusManager()->GetFocusedView());
    if(kind_==1||kind_==0)Round(canvas,rect,kind_==1?18:14,focused?SkColorSetA(c.accent,180):c.line,1);
  }
 private:int kind_;base::ScopedObservation<views::FocusManager,views::FocusChangeListener> focus_{this};
};
BEGIN_METADATA(Surface)
END_METADATA
class Welcome final : public views::View {
  METADATA_HEADER(Welcome,views::View)
 public:
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {280,210};}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const float cx=width()/2.f,cy=63;
    cc::PaintFlags glow;glow.setAntiAlias(true);
    const std::array<SkColor4f,3> halo={SkColor4f::FromColor(SkColorSetA(c.accent,c.dark?35:25)),SkColor4f::FromColor(SkColorSetA(c.accent,8)),SkColors::kTransparent};
    glow.setShader(cc::PaintShader::MakeRadialGradient({cx,cy},83,halo.data(),nullptr,halo.size(),SkTileMode::kClamp));
    canvas->DrawCircle(gfx::PointF(cx,cy),83,glow);
    const std::array<SkColor4f,4> orb={SkColor4f::FromColor(0xffeee8ff),SkColor4f::FromColor(0xffb1a0f4),SkColor4f::FromColor(0xff6053ad),SkColor4f::FromColor(0xff302d5a)};
    glow.setShader(cc::PaintShader::MakeRadialGradient({cx-14,cy-18},70,orb.data(),nullptr,orb.size(),SkTileMode::kClamp));
    canvas->DrawCircle(gfx::PointF(cx,cy),33,glow);
    cc::PaintFlags ring;ring.setAntiAlias(true);ring.setStyle(cc::PaintFlags::kStroke_Style);ring.setStrokeWidth(.8f);ring.setColor(SkColorSetA(c.accent,90));
    canvas->DrawCircle(gfx::PointF(cx,cy),39,ring);
    Glyph(canvas,AssistantGlyph::kSpark,cx-8,cy-8,0xfff9f6ff);
    canvas->DrawStringRectWithFlags(T(u"Co dziś odkrywamy?"),Type(27,400,true),c.ink,gfx::Rect(8,119,std::max(0,width()-16),42),gfx::Canvas::TEXT_ALIGN_CENTER);
    canvas->DrawStringRectWithFlags(T(u"Mniej klikania. Więcej możliwości."),Type(12),c.muted,gfx::Rect(8,167,std::max(0,width()-16),24),gfx::Canvas::TEXT_ALIGN_CENTER);
  }
};
BEGIN_METADATA(Welcome)
END_METADATA
class Suggestion final : public views::Button {
  METADATA_HEADER(Suggestion,views::Button)
 public:
  Suggestion(base::RepeatingClosure action,AssistantGlyph glyph,std::u16string title,std::u16string detail)
      :Button(base::BindRepeating(std::move(action))),glyph_(glyph),title_(std::move(title)),detail_(std::move(detail)){SetFocusBehavior(FocusBehavior::ALWAYS);GetViewAccessibility().SetName(title_);SetInstallFocusRingOnFocus(true);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return {280,54};}
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    Round(canvas,gfx::RectF(GetLocalBounds()),12,hover?c.card:SK_ColorTRANSPARENT);
    Glyph(canvas,glyph_,12,18,hover?c.accent:c.muted);
    canvas->DrawStringRectWithFlags(title_,Type(13,500),c.ink,gfx::Rect(43,7,std::max(0,width()-68),21),gfx::Canvas::TEXT_ALIGN_LEFT);
    canvas->DrawStringRectWithFlags(detail_,Type(11),c.muted,gfx::Rect(43,29,std::max(0,width()-68),17),gfx::Canvas::TEXT_ALIGN_LEFT);
    Glyph(canvas,AssistantGlyph::kPlus,width()-24,19,SkColorSetA(c.muted,110));
  }
 private:AssistantGlyph glyph_;std::u16string title_,detail_;
};
BEGIN_METADATA(Suggestion)
END_METADATA
}
namespace {
// The leading mark: a letter tile in the page's color, or a tool glyph on an accent tint.
void PaintMark(gfx::Canvas* canvas,const Colors& c,const AssistantMark& mark,int x,int y,int size){
  if(mark.glyph){
    Round(canvas,gfx::RectF(x,y,size,size),size/4.f,SkColorSetA(c.accent,c.dark?42:28));
    canvas->Save();const float scale=size>=22?0.875f:0.75f;canvas->Translate(gfx::Vector2d(x+(size-16*scale)/2,y+(size-16*scale)/2));canvas->Scale(scale,scale);
    Glyph(canvas,*mark.glyph,0,0,c.accent);canvas->Restore();return;
  }
  Round(canvas,gfx::RectF(x,y,size,size),size/4.f,mark.color==SK_ColorTRANSPARENT?c.strong:mark.color);
  canvas->DrawStringRectWithFlags(mark.letter.substr(0,1),Type(size>=22?12:10,600),0xff0c0b11,gfx::Rect(x,y,size,size),gfx::Canvas::TEXT_ALIGN_CENTER);
}
void PaintKey(gfx::Canvas* canvas,const Colors& c,const std::u16string& key,int right,int center_y){
  const int w=std::max(20,gfx::GetStringWidth(key,Mono(10))+10);const gfx::Rect r(right-w,center_y-10,w,20);
  Round(canvas,gfx::RectF(r),4,c.dark?0x14ffffff:0x0f000000);Round(canvas,gfx::RectF(r),4,c.line,1);
  canvas->DrawStringRectWithFlags(key,Mono(10),c.muted,r,gfx::Canvas::TEXT_ALIGN_CENTER);
}
int KeysWidth(const std::u16string& keys){
  int w=0;for(const auto& key:base::SplitString(keys,u" ",base::TRIM_WHITESPACE,base::SPLIT_WANT_NONEMPTY))w+=std::max(20,gfx::GetStringWidth(key,Mono(10))+10)+4;return w;
}
void PaintKeys(gfx::Canvas* canvas,const Colors& c,const std::u16string& keys,int right,int center_y){
  auto parts=base::SplitString(keys,u" ",base::TRIM_WHITESPACE,base::SPLIT_WANT_NONEMPTY);
  for(auto it=parts.rbegin();it!=parts.rend();++it){PaintKey(canvas,c,*it,right,center_y);right-=std::max(20,gfx::GetStringWidth(*it,Mono(10))+10)+4;}
}
class Eyebrow final : public views::View {
  METADATA_HEADER(Eyebrow,views::View)
 public:
  Eyebrow(std::u16string text,std::u16string trailing):text_(base::i18n::ToUpper(text)),trailing_(std::move(trailing)){
    GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);GetViewAccessibility().SetName(text);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {200,26};}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const int tw=trailing_.empty()?0:gfx::GetStringWidth(trailing_,Type(11,500))+4;
    canvas->DrawStringRectWithFlags(text_,Type(10,600),c.muted,gfx::Rect(8,8,std::max(0,width()-16-tw),16),gfx::Canvas::TEXT_ALIGN_LEFT);
    if(tw)canvas->DrawStringRectWithFlags(trailing_,Type(11,500),c.muted,gfx::Rect(width()-8-tw,8,tw,16),gfx::Canvas::TEXT_ALIGN_RIGHT);
  }
 private:std::u16string text_,trailing_;
};
BEGIN_METADATA(Eyebrow)
END_METADATA
class MenuRow final : public views::Button {
  METADATA_HEADER(MenuRow,views::Button)
 public:
  MenuRow(base::RepeatingClosure action,AssistantRow row):Button(base::BindRepeating(std::move(action))),row_(std::move(row)){
    SetFocusBehavior(row_.disabled?FocusBehavior::NEVER:FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);SetEnabled(!row_.disabled);
    GetViewAccessibility().SetName(row_.title);if(!row_.detail.empty())GetViewAccessibility().SetDescription(row_.detail);
    // The trash has no view of its own: its name goes with the row, and Delete runs it.
    if(row_.remove)GetViewAccessibility().SetDescription(row_.detail.empty()?row_.remove_name:row_.detail+u" · "+row_.remove_name+u" (Delete)");
    if(row_.checked)GetViewAccessibility().SetCheckedState(ax::mojom::CheckedState::kTrue);
    if(row_.toggle){GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);GetViewAccessibility().SetCheckedState(*row_.toggle?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);}
  }
  void SetActive(bool active){if(active_!=active){active_=active;SchedulePaint();}}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {240,row_.detail.empty()?32:42};}
  // The trash, at the row's end: present only for rows that can be removed.
  gfx::Rect RemoveBounds() const {return row_.remove?gfx::Rect(width()-32,(height()-24)/2,24,24):gfx::Rect();}
  bool ShowsRemove() const {return row_.remove&&(active_||GetState()==STATE_HOVERED||GetState()==STATE_PRESSED||HasFocus());}
  void NotifyClick(const ui::Event& event) override {
    if(row_.remove&&event.IsLocatedEvent()&&RemoveBounds().Contains(gfx::ToFlooredPoint(event.AsLocatedEvent()->location_f()))){row_.remove.Run();return;}
    Button::NotifyClick(event);
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if(row_.remove&&event.key_code()==ui::VKEY_DELETE){row_.remove.Run();return true;}
    return Button::OnKeyPressed(event);
  }
  void StateChanged(ButtonState old_state) override {Button::StateChanged(old_state);SchedulePaint();}
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    if(active_||hover||row_.checked)Round(canvas,gfx::RectF(GetLocalBounds()),8,row_.checked&&!active_&&!hover?SkColorSetA(c.accent,c.dark?34:24):SkColorSetA(c.ink,c.dark?18:12));
    const bool marked=row_.mark.glyph||!row_.mark.letter.empty();
    const int size=row_.detail.empty()?20:24,x=marked?8+size+10:10;
    if(marked){
      auto mark_colors=c;if(row_.muted_mark)mark_colors.accent=c.muted;
      PaintMark(canvas,mark_colors,row_.mark,8,(height()-size)/2,size);
    }
    int right=width()-8;
    if(ShowsRemove()){
      const auto trash=RemoveBounds();
      if(GetState()==STATE_HOVERED&&trash.Contains(GetMouseLocationInView()))Round(canvas,gfx::RectF(trash),6,SkColorSetA(c.danger,c.dark?40:28));
      Glyph(canvas,AssistantGlyph::kTrash,trash.x()+4,trash.y()+4,trash.Contains(GetMouseLocationInView())?c.danger:c.muted);
      right=trash.x()-6;
    }
    if(row_.checked){Glyph(canvas,AssistantGlyph::kCheck,right-16,(height()-16)/2,c.accent);right-=22;}
    if(row_.toggle){
      // 32×18 switch: accent track when on, an outlined track when off.
      const bool on=*row_.toggle;const gfx::RectF track(right-32,height()/2.f-9,32,18);
      if(on)Round(canvas,track,9,row_.disabled?SkColorSetA(c.accent,110):c.accent);
      else{Round(canvas,track,9,SkColorSetA(c.ink,c.dark?20:14));auto edge=track;edge.Inset(.5f);Round(canvas,edge,9,c.strong,1);}
      cc::PaintFlags knob;knob.setAntiAlias(true);knob.setColor(on?c.on_accent:(row_.disabled?SkColorSetA(c.muted,120):c.muted));
      canvas->DrawCircle(gfx::PointF(on?track.right()-9:track.x()+9,track.CenterPoint().y()),6,knob);
      right-=40;
    }
    if(active_&&!row_.disabled&&!row_.checked&&!row_.remove){PaintKey(canvas,c,u"↵",right,height()/2);right-=28;}
    if(!row_.badge.empty()&&!ShowsRemove()){
      const int w=gfx::GetStringWidth(row_.badge,Type(10,500))+12;const gfx::Rect b(right-w,height()/2-9,w,18);
      Round(canvas,gfx::RectF(b),4,c.line,1);canvas->DrawStringRectWithFlags(row_.badge,Type(10,500),c.muted,b,gfx::Canvas::TEXT_ALIGN_CENTER);right-=w+6;
    }
    const auto ink=row_.disabled?SkColorSetA(c.ink,110):row_.danger?c.danger:c.ink;
    const auto title_font=row_.mono_title?Mono(12):Type(13,row_.checked?600:400);
    const int ty=row_.detail.empty()?0:4,th=row_.detail.empty()?height():18;
    const int avail=std::max(0,right-x);
    const auto& t=row_.title;const auto match=row_.match.IsValid()&&!row_.match.is_empty()&&row_.match.end()<=t.size()?row_.match:gfx::Range();
    if(match.is_empty()){canvas->DrawStringRectWithFlags(t,title_font,ink,gfx::Rect(x,ty,avail,th),gfx::Canvas::TEXT_ALIGN_LEFT);}
    else{
      // Pre, match (accent, bold), post — drawn in sequence, the tail elided.
      const auto pre=t.substr(0,match.start()),hit=t.substr(match.start(),match.length()),post=t.substr(match.end());
      const auto bold=row_.mono_title?Mono(12):Type(13,600);int cx=x;
      const int pw=std::min(avail,gfx::GetStringWidth(pre,title_font));canvas->DrawStringRectWithFlags(pre,title_font,ink,gfx::Rect(cx,ty,pw,th),gfx::Canvas::TEXT_ALIGN_LEFT);cx+=pw;
      const int hw=std::min(std::max(0,x+avail-cx),gfx::GetStringWidth(hit,bold));canvas->DrawStringRectWithFlags(hit,bold,c.accent,gfx::Rect(cx,ty,hw,th),gfx::Canvas::TEXT_ALIGN_LEFT);cx+=hw;
      canvas->DrawStringRectWithFlags(post,title_font,ink,gfx::Rect(cx,ty,std::max(0,x+avail-cx),th),gfx::Canvas::TEXT_ALIGN_LEFT);
    }
    if(!row_.detail.empty())canvas->DrawStringRectWithFlags(row_.detail,row_.mono_detail?Mono(11):Type(11),row_.disabled?SkColorSetA(c.muted,150):c.muted,gfx::Rect(x,22,avail,16),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:
  gfx::Point GetMouseLocationInView() const {
    if(!GetWidget())return {-1,-1};
    auto p=display::Screen::Get()->GetCursorScreenPoint();views::View::ConvertPointFromScreen(this,&p);return p;
  }
  AssistantRow row_;bool active_=false;
};
BEGIN_METADATA(MenuRow)
END_METADATA
class Activity final : public views::View {
  METADATA_HEADER(Activity,views::View)
 public:
  Activity(std::u16string text,int state):text_(std::move(text)),state_(state){
    GetViewAccessibility().SetRole(state_==0||state_==3?ax::mojom::Role::kStatus:ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);
  }
  int HeightFor(int w) const {int tw=std::max(1,w-22),th=0;gfx::Canvas::SizeStringInt(text_,Type(12),&tw,&th,16,gfx::Canvas::MULTI_LINE);return std::max(18,th+2);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& b) const override {const int w=b.width().is_bounded()?b.width().value():280;return {w,HeightFor(w)};}
  void AddedToWidget() override {
    // Motion follows the system's reduced-motion setting, not workspace animations.
    if((state_==0||state_==3)&&!gfx::Animation::PrefersReducedMotion())
      timer_.Start(FROM_HERE,base::Milliseconds(33),base::BindRepeating(&Activity::SchedulePaint,base::Unretained(this)));
  }
  void RemovedFromWidget() override {timer_.Stop();}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);
    const double t=timer_.IsRunning()?base::TimeTicks::Now().since_origin().InSecondsF():0.25;
    const gfx::PointF center(8,9);
    SkColor ink=c.muted;
    if(state_==0){
      // A three-quarter arc turning once a second, over a faint track.
      cc::PaintFlags track;track.setAntiAlias(true);track.setStyle(cc::PaintFlags::kStroke_Style);track.setStrokeWidth(2);
      track.setColor(SkColorSetA(c.accent,50));canvas->DrawCircle(center,6,track);
      SkPathBuilder arc;arc.addArc(SkRect::MakeXYWH(2,3,12,12),std::fmod(t*360.0,360.0),250);
      cc::PaintFlags head=track;head.setColor(c.accent);head.setStrokeCap(cc::PaintFlags::kRound_Cap);canvas->DrawPath(arc.detach(),head);
      const float breathe=.72f+.28f*std::sin(t*4.2);ink=SkColorSetA(c.ink,static_cast<U8CPU>(255*breathe));
    }else if(state_==1){
      canvas->Save();canvas->Translate(gfx::Vector2d(1,2));canvas->Scale(.875f,.875f);Glyph(canvas,AssistantGlyph::kCheck,0,0,SkColorSetA(c.ok,200));canvas->Restore();
    }else if(state_==2){
      cc::PaintFlags dot;dot.setAntiAlias(true);dot.setColor(c.danger);canvas->DrawCircle(center,6.5f,dot);
      canvas->DrawStringRectWithFlags(u"!",Type(10,700),c.on_accent==SK_ColorWHITE?c.bg:SK_ColorWHITE,gfx::Rect(2,2,12,14),gfx::Canvas::TEXT_ALIGN_CENTER);
      ink=c.danger;
    }else{
      const float pulse=.5f+.5f*std::sin(t*5);
      cc::PaintFlags halo;halo.setAntiAlias(true);halo.setColor(SkColorSetA(c.warn,static_cast<U8CPU>(60*pulse)));canvas->DrawCircle(center,7,halo);
      cc::PaintFlags dot;dot.setAntiAlias(true);dot.setColor(c.warn);canvas->DrawCircle(center,3.5f,dot);
      ink=c.ink;
    }
    canvas->DrawStringRectWithFlags(text_,Type(12,state_==0||state_==3?500:400),ink,gfx::Rect(22,1,std::max(0,width()-22),height()-1),gfx::Canvas::MULTI_LINE|gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:
  std::u16string text_;int state_;base::RepeatingTimer timer_;
};
BEGIN_METADATA(Activity)
END_METADATA
class Note final : public views::View {
  METADATA_HEADER(Note,views::View)
 public:
  Note(std::u16string text,std::optional<AssistantGlyph> glyph,bool tinted):text_(std::move(text)),glyph_(glyph),tinted_(tinted){GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);}
  int Inset() const {return tinted_?10:0;}
  int TextX() const {return Inset()+(glyph_?22:0);}
  int HeightFor(int w) const {
    int tw=std::max(1,w-TextX()-Inset()),th=0;gfx::Canvas::SizeStringInt(text_,Type(12),&tw,&th,16,gfx::Canvas::MULTI_LINE);return th+2*(tinted_?8:0);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& b) const override {const int w=b.width().is_bounded()?b.width().value():280;return {w,HeightFor(w)};}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const int pad=tinted_?8:0;
    if(tinted_)Round(canvas,gfx::RectF(GetLocalBounds()),8,SkColorSetA(c.accent,c.dark?34:24));
    if(glyph_){canvas->Save();canvas->Translate(gfx::Vector2d(Inset(),pad+1));canvas->Scale(.875f,.875f);Glyph(canvas,*glyph_,0,0,c.accent);canvas->Restore();}
    canvas->DrawStringRectWithFlags(text_,Type(12),tinted_?c.ink:c.muted,gfx::Rect(TextX(),pad,std::max(0,width()-TextX()-Inset()),height()-2*pad),gfx::Canvas::MULTI_LINE|gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:std::u16string text_;std::optional<AssistantGlyph> glyph_;bool tinted_;
};
BEGIN_METADATA(Note)
END_METADATA
class Footer final : public views::View {
  METADATA_HEADER(Footer,views::View)
 public:
  Footer(std::u16string text,std::u16string keys):text_(std::move(text)),keys_(std::move(keys)){GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text_);}
  void SetText(std::u16string text){text_=std::move(text);GetViewAccessibility().SetName(text_);SchedulePaint();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {240,36};}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);canvas->FillRect(gfx::Rect(0,0,width(),1),c.line);
    const int kw=KeysWidth(keys_);
    canvas->DrawStringRectWithFlags(text_,Type(12),c.muted,gfx::Rect(12,1,std::max(0,width()-24-kw),height()-1),gfx::Canvas::TEXT_ALIGN_LEFT);
    PaintKeys(canvas,c,keys_,width()-10,height()/2);
  }
 private:std::u16string text_,keys_;
};
BEGIN_METADATA(Footer)
END_METADATA
// A small × inside a token; its own hit target and accessible name.
class TokenClose final : public views::Button {
  METADATA_HEADER(TokenClose,views::Button)
 public:
  TokenClose(base::RepeatingClosure action,std::u16string name):Button(base::BindRepeating(std::move(action))){SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);GetViewAccessibility().SetName(name);SetTooltipText(name);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {18,18};}
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    if(hover)Round(canvas,gfx::RectF(GetLocalBounds()),4,SkColorSetA(c.ink,c.dark?30:20));
    canvas->Save();canvas->Translate(gfx::Vector2d(4,4));canvas->Scale(.625f,.625f);Glyph(canvas,AssistantGlyph::kClose,0,0,hover?c.ink:c.muted);canvas->Restore();
  }
};
BEGIN_METADATA(TokenClose)
END_METADATA
class Token final : public views::View {
  METADATA_HEADER(Token,views::View)
 public:
  Token(AssistantMark mark,std::u16string text,bool tool,base::RepeatingClosure remove):mark_(std::move(mark)),text_(std::move(text)),tool_(tool){
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);GetViewAccessibility().SetName(text_);
    if(remove)close_=AddChildView(std::make_unique<TokenClose>(std::move(remove),TF(u"Usuń {}",{text_})));
  }
  int TextWidth() const {return std::min(160,gfx::GetStringWidth(text_,Type(12,500)));}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {4+16+6+TextWidth()+(close_?24:8),24};}
  void Layout(PassKey) override {if(close_)close_->SetBounds(width()-21,3,18,18);}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    if(!tool_)Round(canvas,rect,6,SkColorSetA(c.accent,c.dark?40:28));
    Round(canvas,rect,6,tool_?c.strong:SkColorSetA(c.accent,c.dark?80:70),1);
    PaintMark(canvas,c,mark_,4,4,16);
    canvas->DrawStringRectWithFlags(text_,Type(12,500),c.ink,gfx::Rect(26,0,TextWidth(),height()),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:AssistantMark mark_;std::u16string text_;bool tool_;raw_ptr<views::View> close_=nullptr;
};
BEGIN_METADATA(Token)
END_METADATA
class Flow final : public views::View {
  METADATA_HEADER(Flow,views::View)
 public:
  explicit Flow(int spacing):spacing_(spacing){}
  int Place(int width,bool apply){
    int x=0,y=0,line=0;
    for(const auto& child:children()){
      if(!child->GetVisible())continue;
      auto size=child->GetPreferredSize();size.set_width(std::min(size.width(),std::max(0,width)));
      if(x>0&&x+size.width()>width){x=0;y+=line+spacing_;line=0;}
      if(apply)child->SetBounds(x,y,size.width(),size.height());
      x+=size.width()+spacing_;line=std::max(line,size.height());
    }
    return line?y+line:0;
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& b) const override {const int w=b.width().is_bounded()?b.width().value():320;return {w,const_cast<Flow*>(this)->Place(w,false)};}
  void Layout(PassKey) override {Place(width(),true);}
 private:int spacing_;
};
BEGIN_METADATA(Flow)
END_METADATA
class Pill final : public views::Button {
  METADATA_HEADER(Pill,views::Button)
 public:
  Pill(base::RepeatingClosure action,std::u16string text,int kind,std::optional<AssistantGlyph> glyph,std::u16string key)
      :Button(base::BindRepeating(std::move(action))),text_(std::move(text)),kind_(kind),glyph_(glyph),key_(std::move(key)){
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);GetViewAccessibility().SetName(text_);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return {(glyph_?28:12)+gfx::GetStringWidth(text_,Type(12,600))+(key_.empty()?12:KeysWidth(key_)+10),28};
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    SkColor ink=c.ink;
    if(kind_==0){Round(canvas,rect,14,hover?SkColorSetA(c.accent,225):c.accent);ink=c.on_accent;}
    else if(kind_==1){if(hover)Round(canvas,rect,14,SkColorSetA(c.ink,c.dark?18:12));Round(canvas,rect,14,c.strong,1);}
    else if(kind_==2){Round(canvas,rect,14,hover?SkColorSetA(c.ink,c.dark?30:20):SkColorSetA(c.ink,c.dark?16:10));}
    else{Round(canvas,rect,14,SkColorSetA(c.danger,hover?60:40));ink=c.danger;}
    if(!GetEnabled())ink=SkColorSetA(ink,110);
    int x=12;if(glyph_){canvas->Save();canvas->Translate(gfx::Vector2d(9,6));canvas->Scale(.875f,.875f);Glyph(canvas,*glyph_,0,0,ink);canvas->Restore();x=28;}
    canvas->DrawStringRectWithFlags(text_,Type(12,600),ink,gfx::Rect(x,0,gfx::GetStringWidth(text_,Type(12,600))+2,height()),gfx::Canvas::TEXT_ALIGN_LEFT);
    if(!key_.empty())PaintKeys(canvas,kind_==0?Colors{c.bg,c.card,ink,SkColorSetA(ink,170),SkColorSetA(ink,60),c.accent,c.raised,c.strong,c.warn,c.ok,c.danger,c.on_accent,!c.dark}:c,key_,width()-8,height()/2);
  }
 private:std::u16string text_;int kind_;std::optional<AssistantGlyph> glyph_;std::u16string key_;
};
BEGIN_METADATA(Pill)
END_METADATA
// A switch as a button of its own (role switch), for rows that do more than toggle.
class Switch final : public views::Button {
  METADATA_HEADER(Switch,views::Button)
 public:
  Switch(base::RepeatingClosure action,bool on,std::u16string name):Button(base::BindRepeating(std::move(action))),on_(on){
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);GetViewAccessibility().SetName(name);
    GetViewAccessibility().SetCheckedState(on?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse);SetTooltipText(name);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {36,20};}
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const gfx::RectF track(0,height()/2.f-10,36,20);
    if(on_)Round(canvas,track,10,c.accent);
    else{Round(canvas,track,10,SkColorSetA(c.ink,c.dark?20:14));auto edge=track;edge.Inset(.5f);Round(canvas,edge,10,c.strong,1);}
    cc::PaintFlags knob;knob.setAntiAlias(true);knob.setColor(on_?c.on_accent:c.muted);
    canvas->DrawCircle(gfx::PointF(on_?track.right()-10:track.x()+10,track.CenterPoint().y()),7,knob);
  }
 private:bool on_;
};
BEGIN_METADATA(Switch)
END_METADATA
class TaskCard final : public views::Button {
  METADATA_HEADER(TaskCard,views::Button)
 public:
  TaskCard(AssistantTaskCard card,base::RepeatingClosure open,base::RepeatingClosure toggle,base::RepeatingClosure run,base::RepeatingClosure remove)
      :Button(base::BindRepeating(std::move(open))),card_(std::move(card)){
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);
    GetViewAccessibility().SetName(card_.name);GetViewAccessibility().SetDescription(card_.schedule+u" · "+card_.status);
    switch_=AddChildView(std::make_unique<Switch>(std::move(toggle),card_.active,card_.active?TF(u"Wstrzymaj: {}",{card_.name}):TF(u"Wznów: {}",{card_.name})));
    run_=AddChildView(std::make_unique<Pill>(std::move(run),T(u"Uruchom"),1,std::nullopt,std::u16string()));
    run_->GetViewAccessibility().SetName(TF(u"Uruchom teraz: {}",{card_.name}));
    remove_=AddChildView(std::make_unique<Action>(std::move(remove),AssistantGlyph::kTrash,TF(u"Usuń zadanie: {}",{card_.name}),false,false));
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& b) const override {return {b.width().is_bounded()?b.width().value():400,112};}
  void Layout(PassKey) override {
    switch_->SetBounds(width()-48,16,36,20);
    remove_->SetBounds(width()-38,78,28,28);
    const int pw=run_->GetPreferredSize().width();
    run_->SetBounds(width()-38-6-pw,78,pw,28);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    Round(canvas,rect,10,hover?c.raised:c.bg);Round(canvas,rect,10,c.line,1);
    const bool on=card_.active;
    const SkColor ink=on?c.ink:SkColorSetA(c.ink,170),muted=c.muted;
    Round(canvas,gfx::RectF(12,12,28,28),8,on?SkColorSetA(c.accent,c.dark?40:28):SkColorSetA(c.ink,c.dark?16:10));
    Glyph(canvas,AssistantGlyph::kHistory,18,18,on?c.accent:muted);
    const int x=50,right=width()-56;
    canvas->DrawStringRectWithFlags(card_.name,Type(13,600),ink,gfx::Rect(x,10,std::max(0,right-x),18),gfx::Canvas::TEXT_ALIGN_LEFT);
    canvas->DrawStringRectWithFlags(card_.instruction,Type(12),muted,gfx::Rect(x,28,std::max(0,right-x),16),gfx::Canvas::TEXT_ALIGN_LEFT);
    // Chips: schedule, model, access.
    int cx=x;
    auto chip=[&](const std::u16string& text,const gfx::FontList& font,SkColor color,bool glyph){
      if(text.empty())return;
      const int w=std::min(std::max(0,width()-12-cx),gfx::GetStringWidth(text,font)+(glyph?32:16));
      if(w<24)return;
      Round(canvas,gfx::RectF(cx,52,w,22),11,SkColorSetA(c.ink,c.dark?14:9));
      if(glyph){canvas->Save();canvas->Translate(gfx::Vector2d(cx+6,55));canvas->Scale(.75f,.75f);Glyph(canvas,AssistantGlyph::kRepeat,0,0,muted);canvas->Restore();}
      canvas->DrawStringRectWithFlags(text,font,color,gfx::Rect(cx+(glyph?24:8),52,w-(glyph?28:16),22),gfx::Canvas::TEXT_ALIGN_LEFT);
      cx+=w+6;
    };
    chip(card_.schedule,Type(12),ink,true);chip(card_.model,Mono(11),ink,false);chip(card_.access,Type(12),muted,false);
    // Last run and the next one.
    const SkColor dot=card_.state==1?c.ok:card_.state==2?c.warn:card_.state==3?c.danger:c.strong;
    cc::PaintFlags d;d.setAntiAlias(true);d.setColor(dot);canvas->DrawCircle(gfx::PointF(x+3,92),3,d);
    const int status_right=run_->x()-8;
    canvas->DrawStringRectWithFlags(card_.status,Type(12),card_.state==2?c.warn:card_.state==3?c.danger:muted,
        gfx::Rect(x+12,84,std::max(0,status_right-x-12),16),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:
  AssistantTaskCard card_;raw_ptr<views::View> switch_=nullptr,run_=nullptr,remove_=nullptr;
};
BEGIN_METADATA(TaskCard)
END_METADATA
class Link final : public views::Button {
  METADATA_HEADER(Link,views::Button)
 public:
  Link(base::RepeatingClosure action,std::u16string text,std::optional<AssistantGlyph> glyph):Button(base::BindRepeating(std::move(action))),text_(std::move(text)),glyph_(glyph){
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);GetViewAccessibility().SetName(text_);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {(glyph_?22:0)+gfx::GetStringWidth(text_+u"  →",Type(12))+24,36};}
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;const auto ink=hover?c.ink:c.muted;
    int x=12;if(glyph_){canvas->Save();canvas->Translate(gfx::Vector2d(12,(height()-14)/2));canvas->Scale(.875f,.875f);Glyph(canvas,*glyph_,0,0,ink);canvas->Restore();x=34;}
    canvas->DrawStringRectWithFlags(text_+u"  →",Type(12),ink,gfx::Rect(x,0,std::max(0,width()-x-8),height()),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:std::u16string text_;std::optional<AssistantGlyph> glyph_;
};
BEGIN_METADATA(Link)
END_METADATA
// Light page strip: never a screenshot, only the element's own label.
class ElementPreview final : public views::View {
  METADATA_HEADER(ElementPreview,views::View)
 public:
  explicit ElementPreview(std::u16string label):label_(std::move(label)){GetViewAccessibility().SetIsIgnored(true);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {240,48};}
  void OnPaint(gfx::Canvas* canvas) override {
    Round(canvas,gfx::RectF(GetLocalBounds()),8,0xfff6f5f2);
    const int w=std::min(std::max(0,width()-56),gfx::GetStringWidth(label_,Type(11,600))+20);const gfx::Rect pill(width()-10-w,(height()-24)/2,w,24);
    Round(canvas,gfx::RectF(10,(height()-10)/2.f,std::max(0,pill.x()-20),10),3,0xffe4e1da);
    auto ring=gfx::RectF(pill);ring.Outset(4);Round(canvas,ring,9,0xff6d54d8,2);
    Round(canvas,gfx::RectF(pill),6,0xff16141c);
    canvas->DrawStringRectWithFlags(label_,Type(11,600),SK_ColorWHITE,pill,gfx::Canvas::TEXT_ALIGN_CENTER);
  }
 private:std::u16string label_;
};
BEGIN_METADATA(ElementPreview)
END_METADATA
void PaintDots(gfx::Canvas* canvas,const Colors& c,int x,int cy,float phase){
  for(int i=0;i<3;++i){
    const float pulse=phase<0?.75f:.5f+.5f*std::sin((phase*3-i*.12)*6.2831853);
    cc::PaintFlags dot;dot.setAntiAlias(true);dot.setColor(SkColorSetA(i==1?c.accent:(c.dark?0xffd7ff3f:0xff4f6b00),static_cast<int>(110+145*pulse)));
    canvas->DrawCircle(gfx::Point(x+i*7,cy),2,dot);
  }
}
class TaskRow final : public views::View {
  METADATA_HEADER(TaskRow,views::View)
 public:
  TaskRow(AssistantMark mark,std::u16string title,std::u16string status,int state,base::RepeatingClosure take_over,base::RepeatingClosure stop)
      :mark_(std::move(mark)),title_(std::move(title)),status_(std::move(status)),state_(state){
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);GetViewAccessibility().SetName(title_);GetViewAccessibility().SetDescription(status_);
    if(take_over)take_over_=AddChildView(CreateAssistantIcon(std::move(take_over),AssistantGlyph::kExpand,T(u"Przejmij kartę")));
    if(stop)stop_=AddChildView(CreateAssistantIcon(std::move(stop),AssistantGlyph::kClose,state_==0||state_==1?T(u"Zatrzymaj zadanie"):T(u"Zamknij kartę w tle")));
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {return {280,46};}
  void Layout(PassKey) override {int x=width()-6;for(auto* b:{stop_.get(),take_over_.get()})if(b){x-=28;b->SetBounds(x,(height()-28)/2,28,28);x-=2;}}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);
    if(state_<=1)Round(canvas,gfx::RectF(GetLocalBounds()),12,SkColorSetA(c.accent,c.dark?26:18));
    if(state_==2){Round(canvas,gfx::RectF(8,(height()-18)/2.f,18,18),9,SkColorSetA(c.ok,50));canvas->Save();canvas->Translate(gfx::Vector2d(11,(height()-12)/2));canvas->Scale(.75f,.75f);Glyph(canvas,AssistantGlyph::kCheck,0,0,c.ok);canvas->Restore();}
    else PaintMark(canvas,c,mark_,8,(height()-18)/2,18);
    const int buttons=(take_over_?30:0)+(stop_?30:0)+8,x=36,w=std::max(0,width()-x-buttons);
    canvas->DrawStringRectWithFlags(title_,Type(13),c.ink,gfx::Rect(x,5,w,19),gfx::Canvas::TEXT_ALIGN_LEFT);
    int sx=x;if(state_==0){PaintDots(canvas,c,x+2,32,-1);sx+=24;}
    canvas->DrawStringRectWithFlags(status_,Type(11,state_==1?600:400),state_==1?c.warn:c.muted,gfx::Rect(sx,24,std::max(0,w-(sx-x)),16),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:AssistantMark mark_;std::u16string title_,status_;int state_;raw_ptr<views::View> take_over_=nullptr,stop_=nullptr;
};
BEGIN_METADATA(TaskRow)
END_METADATA
// Page control status, browser chrome: activity dots, status, Escape hint and
// optionally "finish in the background".
class Capsule final : public views::View {
  METADATA_HEADER(Capsule,views::View)
 public:
  Capsule(std::u16string text,base::RepeatingClosure action,bool animate):text_(std::move(text)){
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);GetViewAccessibility().SetName(text_);
    if(action)button_=AddChildView(CreateAssistantPill(std::move(action),T(u"Dokończ w tle"),2,AssistantGlyph::kLayers));
    if(animate)timer_.Start(FROM_HERE,base::Milliseconds(66),base::BindRepeating(&Capsule::SchedulePaint,base::Unretained(this)));
  }
  int TextWidth() const {return gfx::GetStringWidth(text_,Type(12,500));}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return {14+24+10+TextWidth()+12+KeysWidth(u"Esc")+(button_?12+button_->GetPreferredSize().width()+6:10),40};
  }
  // Narrow pages drop the button first, then elide the status.
  bool Roomy() const {return button_&&width()>=GetPreferredSize().width();}
  void Layout(PassKey) override {if(button_){const auto s=button_->GetPreferredSize();button_->SetVisible(Roomy());button_->SetBounds(width()-6-s.width(),6,s.width(),28);}}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    Round(canvas,rect,20,c.raised);Round(canvas,rect,20,c.line,1);
    const float phase=timer_.IsRunning()?std::fmod(base::TimeTicks::Now().since_origin().InSecondsF(),2.0)/2.0:-1.f;
    PaintDots(canvas,c,16,height()/2,phase);
    const int text_width=std::min(TextWidth()+2,std::max(0,width()-44-12-KeysWidth(u"Esc")-10));
    canvas->DrawStringRectWithFlags(text_,Type(12,500),c.ink,gfx::Rect(44,0,text_width,height()),gfx::Canvas::TEXT_ALIGN_LEFT);
    PaintKeys(canvas,c,u"Esc",44+text_width+10+KeysWidth(u"Esc"),height()/2);
    if(Roomy())canvas->FillRect(gfx::Rect(button_->x()-7,12,1,16),c.line);
  }
 private:std::u16string text_;raw_ptr<views::View> button_=nullptr;base::RepeatingTimer timer_;
};
BEGIN_METADATA(Capsule)
END_METADATA
class ConsentBubble final : public views::View {
  METADATA_HEADER(ConsentBubble,views::View)
 public:
  ConsentBubble(std::u16string title,std::u16string detail,base::RepeatingClosure allow,base::RepeatingClosure skip,base::RepeatingClosure allow_all)
      :title_(std::move(title)),detail_(std::move(detail)){
    GetViewAccessibility().SetRole(ax::mojom::Role::kAlertDialog);GetViewAccessibility().SetName(title_);GetViewAccessibility().SetDescription(detail_);
    SetPaintToLayer();layer()->SetFillsBoundsOpaquely(false);
    allow_=AddChildView(CreateAssistantPill(base::BindRepeating(
        [](ConsentBubble* self,base::RepeatingClosure allow,base::RepeatingClosure allow_session){
          if(self->remember_&&self->remember_->GetChecked())allow_session.Run();else allow.Run();
        },base::Unretained(this),std::move(allow),allow_all),T(u"Zezwól"),0,std::nullopt,u"↵"));
    skip_=AddChildView(CreateAssistantPill(std::move(skip),T(u"Pomiń"),1,std::nullopt,u"Esc"));
    if(allow_all){
      remember_=AddChildView(std::make_unique<views::Checkbox>(T(u"Nie pytaj do końca sesji")));
      remember_->SetTextSubpixelRenderingEnabled(false);
    }
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if(remember_)remember_->SetEnabledTextColors(Theme(this).muted);
  }
  views::View* allow() const {return allow_;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    const int buttons=40+allow_->GetPreferredSize().width()+6+skip_->GetPreferredSize().width()+10;
    return {std::clamp(std::max({buttons,40+gfx::GetStringWidth(title_,Type(13,600))+14,remember_?50+remember_->GetPreferredSize().width():0}),260,460),remember_?116:88};
  }
  void Layout(PassKey) override {
    int x=40;for(auto* b:{allow_.get(),skip_.get()}){const auto s=b->GetPreferredSize();b->SetBounds(x,50,s.width(),28);x+=s.width()+6;}
    if(remember_)remember_->SetBounds(40,84,std::max(0,width()-52),24);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto c=Theme(this);auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    Round(canvas,rect,12,c.raised);Round(canvas,rect,12,c.line,1);
    PaintMark(canvas,c,{AssistantGlyph::kSpark,{},SK_ColorTRANSPARENT},12,12,20);
    canvas->DrawStringRectWithFlags(title_,Type(13,600),c.ink,gfx::Rect(40,8,std::max(0,width()-52),19),gfx::Canvas::TEXT_ALIGN_LEFT);
    canvas->DrawStringRectWithFlags(detail_,Type(12),c.muted,gfx::Rect(40,27,std::max(0,width()-52),16),gfx::Canvas::TEXT_ALIGN_LEFT);
  }
 private:std::u16string title_,detail_;raw_ptr<views::View> allow_=nullptr,skip_=nullptr;raw_ptr<views::Checkbox> remember_=nullptr;
};
BEGIN_METADATA(ConsentBubble)
END_METADATA
}
class PromptArea final : public views::Textarea {
  METADATA_HEADER(PromptArea,views::Textarea)
 public:
  explicit PromptArea(base::RepeatingCallback<bool(const ui::KeyEvent&)> key):key_(std::move(key)){SetBorder(nullptr);SetBackgroundEnabled(false);SetFontList(Type(14));}
  // Textarea turns Return into a new line before its controller sees it.
  bool PreHandleKeyPressed(const ui::KeyEvent& event) override {return (key_&&key_.Run(event))||views::Textarea::PreHandleKeyPressed(event);}
  int LineHeight() const {return GetFontList().GetHeight()+2;}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override {
    // Wrapped lines of the current text at this width, between one and six.
    const int width=bounds.width().is_bounded()?bounds.width().value():280;
    int w=std::max(1,width-4),h=0;
    std::u16string text(GetText());if(text.empty()||text.ends_with(u"\n"))text+=u" ";
    gfx::Canvas::SizeStringInt(text,GetFontList(),&w,&h,LineHeight(),gfx::Canvas::MULTI_LINE);
    const int lines=std::clamp((h+LineHeight()-1)/LineHeight(),1,6);
    return {width,lines*LineHeight()+4};
  }
  void OnThemeChanged() override {
    views::Textarea::OnThemeChanged();
    // Text and placeholder take the theme's field colors.
  }
 private:
  base::RepeatingCallback<bool(const ui::KeyEvent&)> key_;
};
BEGIN_METADATA(PromptArea)
END_METADATA
std::unique_ptr<views::Textarea> CreateAssistantPromptArea(base::RepeatingCallback<bool(const ui::KeyEvent&)> key){return std::make_unique<PromptArea>(std::move(key));}

namespace {
struct Run { std::u16string text; bool bold = false, italic = false, code = false; GURL url; };
// Inline Markdown: `code`, **bold**, *italic* / _italic_, [text](url) and bare http(s) links.
std::vector<Run> Inline(const std::u16string& s) {
  std::vector<Run> runs; Run plain;
  auto flush = [&] { if (!plain.text.empty()) runs.push_back(plain); plain = Run(); };
  bool bold = false, italic = false;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == u'`') {
      const auto end = s.find(u'`', i + 1);
      if (end != std::u16string::npos) { flush(); runs.push_back({s.substr(i + 1, end - i - 1), false, false, true, GURL()}); i = end + 1; continue; }
    }
    if (s.compare(i, 2, u"**") == 0 || s.compare(i, 2, u"__") == 0) { flush(); bold = !bold; plain.bold = bold; plain.italic = italic; i += 2; continue; }
    if ((s[i] == u'*' || s[i] == u'_') && (s[i] == u'*' || i == 0 || !base::IsAsciiAlpha(s[i - 1]))) {
      flush(); italic = !italic; plain.bold = bold; plain.italic = italic; ++i; continue;
    }
    if (s[i] == u'[') {
      const auto close = s.find(u"](", i);
      const auto end = close == std::u16string::npos ? close : s.find(u')', close);
      if (end != std::u16string::npos) {
        const GURL url(s.substr(close + 2, end - close - 2));
        if (url.SchemeIsHTTPOrHTTPS()) { flush(); runs.push_back({s.substr(i + 1, close - i - 1), bold, italic, false, url}); plain.bold = bold; plain.italic = italic; i = end + 1; continue; }
      }
    }
    if (s.compare(i, 7, u"http://") == 0 || s.compare(i, 8, u"https://") == 0) {
      size_t end = i; while (end < s.size() && !base::IsUnicodeWhitespace(s[end]) && s[end] != u')' && s[end] != u']') ++end;
      while (end > i && (s[end - 1] == u'.' || s[end - 1] == u',' || s[end - 1] == u';' || s[end - 1] == u':')) --end;
      const GURL url(s.substr(i, end - i));
      if (url.is_valid()) { flush(); runs.push_back({s.substr(i, end - i), bold, italic, false, url}); plain.bold = bold; plain.italic = italic; i = end; continue; }
    }
    plain.text += s[i]; plain.bold = bold; plain.italic = italic; ++i;
  }
  flush();
  return runs;
}
std::u16string Trim(const std::u16string& s) { return std::u16string(base::TrimWhitespace(s, base::TRIM_ALL)); }
// Read-only, selectable text with inline styles (bold, italic, code, links):
// a multiline field the user can select in and copy from. Links open on a
// click that selects nothing.
class SelectableText final : public views::Textarea {
  METADATA_HEADER(SelectableText,views::Textarea)
 public:
  struct Span { gfx::Range range; bool bold = false, italic = false, code = false; GURL url; };
  SelectableText(std::u16string text,std::vector<Span> spans,gfx::FontList font,SkColor ink,SkColor code,SkColor link,
                 base::RepeatingCallback<void(const GURL&)> open)
      :spans_(std::move(spans)),ink_(ink),code_(code),link_(link),open_(std::move(open)){
    SetBorder(nullptr);SetBackgroundEnabled(false);SetReadOnly(true);SetFontList(font);SetText(text);
    GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);GetViewAccessibility().SetName(text);
  }
  void OnThemeChanged() override {views::Textarea::OnThemeChanged();Style();}
  void OnBoundsChanged(const gfx::Rect& previous) override {views::Textarea::OnBoundsChanged(previous);Style();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override {
    const int width=bounds.width().is_bounded()?bounds.width().value():320;
    auto* render=GetRenderText();const auto saved=render->display_rect();
    render->SetDisplayRect(gfx::Rect(0,0,std::max(1,width-4),saved.height()));
    const int height=render->GetStringSize().height();
    render->SetDisplayRect(saved);
    return {width,height+4};
  }
  // The conversation scrolls, not the paragraph.
  bool OnMouseWheel(const ui::MouseWheelEvent&) override {return false;}
  void OnMouseReleased(const ui::MouseEvent& event) override {
    views::Textarea::OnMouseReleased(event);
    if(!GetSelectedRange().is_empty()||!event.IsOnlyLeftMouseButton())return;
    const size_t at=GetRenderText()->FindCursorPosition(event.location()).caret_pos();
    for(const auto& span:spans_)if(span.url.is_valid()&&span.range.Contains(gfx::Range(at,at))&&at<span.range.end()){open_.Run(span.url);return;}
  }
 private:
  void Style(){
    auto* render=GetRenderText();render->SetColor(ink_);
    for(const auto& span:spans_){
      if(span.range.is_empty())continue;
      if(span.bold)render->ApplyWeight(gfx::Font::Weight::SEMIBOLD,span.range);
      if(span.italic)render->ApplyStyle(gfx::TEXT_STYLE_ITALIC,true,span.range);
      if(span.code)render->ApplyColor(code_,span.range);
      if(span.url.is_valid()){render->ApplyColor(link_,span.range);render->ApplyStyle(gfx::TEXT_STYLE_UNDERLINE,true,span.range);}
    }
    SchedulePaint();
  }
  std::vector<Span> spans_;SkColor ink_,code_,link_;base::RepeatingCallback<void(const GURL&)> open_;
};
BEGIN_METADATA(SelectableText)
END_METADATA
// A table row: equal columns, so cells line up from row to row.
class TableRow final : public views::View {
  METADATA_HEADER(TableRow,views::View)
 public:
  int ColumnWidth(int width) const {const int n=std::max<int>(1,static_cast<int>(children().size()));return std::max(0,(width-10*(n-1))/n);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override {
    const int width=bounds.width().is_bounded()?bounds.width().value():320;int height=0;
    for(const auto& child:children())height=std::max(height,child->GetHeightForWidth(ColumnWidth(width)));
    return {width,height};
  }
  void Layout(PassKey) override {int x=0;const int column=ColumnWidth(width());for(const auto& child:children()){child->SetBounds(x,0,column,height());x+=column+10;}}
};
BEGIN_METADATA(TableRow)
END_METADATA
class Markdown final : public views::View {
  METADATA_HEADER(Markdown,views::View)
 public:
  Markdown(std::u16string text,base::RepeatingCallback<void(const GURL&)> open):text_(std::move(text)),open_(std::move(open)){
    auto* layout=SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,gfx::Insets(),8));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  }
  // Colors follow the theme, so blocks are built when it is known.
  void OnThemeChanged() override {views::View::OnThemeChanged();Build();}
 private:
  std::unique_ptr<views::View> Paragraph(const std::u16string& source,int size,int weight,SkColor ink,bool italic=false){
    const auto c=Theme(this);
    std::u16string text;std::vector<SelectableText::Span> spans;
    for(auto& run:Inline(source)){
      const size_t start=text.size();text+=run.text;
      spans.push_back({gfx::Range(start,text.size()),run.bold,run.italic||italic,run.code,run.url});
    }
    auto font=Type(size,weight);if(italic)font=font.DeriveWithStyle(gfx::Font::ITALIC);
    return std::make_unique<SelectableText>(text,std::move(spans),font,ink,c.dark?0xffd9d3ff:0xff4b3bb0,c.accent,open_);
  }
  void Build(){
    RemoveAllChildViews();
    const auto c=Theme(this);
    std::vector<std::u16string> lines;
    for(size_t start=0;start<=text_.size();){const auto end=text_.find(u'\n',start);lines.push_back(text_.substr(start,end==std::u16string::npos?std::u16string::npos:end-start));if(end==std::u16string::npos)break;start=end+1;}
    std::u16string paragraph;
    auto flush=[&]{if(!Trim(paragraph).empty())AddChildView(Paragraph(Trim(paragraph),13,400,c.ink));paragraph.clear();};
    for(size_t i=0;i<lines.size();++i){
      const auto line=lines[i];const auto trimmed=Trim(line);
      if(trimmed.empty()){flush();continue;}
      if(trimmed.starts_with(u"```")){
        flush();std::u16string code;
        for(++i;i<lines.size()&&!Trim(lines[i]).starts_with(u"```");++i)code+=(code.empty()?u"":u"\n")+lines[i];
        auto* block=AddChildView(std::make_unique<views::View>());
        block->SetBackground(views::CreateRoundedRectBackground(SkColorSetA(c.ink,c.dark?16:10),8));
        block->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,gfx::Insets::VH(8,10)));
        auto* label=block->AddChildView(std::make_unique<views::Label>(code));
        label->SetFontList(Mono(12));label->SetMultiLine(true);label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        label->SetEnabledColor(c.ink);label->SetBackgroundColor(SK_ColorTRANSPARENT);label->SetSelectable(true);
        continue;
      }
      if(trimmed.starts_with(u"#")){
        flush();size_t level=0;while(level<trimmed.size()&&trimmed[level]==u'#')++level;
        if(level<=6&&level<trimmed.size()&&trimmed[level]==u' '){AddChildView(Paragraph(Trim(trimmed.substr(level)),level==1?17:level==2?15:13,600,c.ink));continue;}
      }
      if(trimmed==u"---"||trimmed==u"***"||trimmed==u"___"){
        flush();auto* rule=AddChildView(std::make_unique<views::View>());rule->SetPreferredSize(gfx::Size(1,1));rule->SetBackground(views::CreateSolidBackground(c.line));continue;
      }
      if(trimmed.starts_with(u"|")){
        flush();
        auto* table=AddChildView(std::make_unique<views::View>());
        table->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,gfx::Insets(),4))->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
        bool header=true;
        for(;i<lines.size()&&Trim(lines[i]).starts_with(u"|");++i){
          auto row=Trim(lines[i]);
          if(row.find_first_not_of(u"|-: ")==std::u16string::npos){header=false;continue;}
          auto* line_view=table->AddChildView(std::make_unique<TableRow>());
          if(row.starts_with(u"|"))row=row.substr(1);if(row.ends_with(u"|"))row.pop_back();
          for(const auto& cell:base::SplitString(row,u"|",base::TRIM_WHITESPACE,base::SPLIT_WANT_ALL))
            line_view->AddChildView(Paragraph(cell,12,header?600:400,c.ink));
          header=false;
        }
        --i;continue;
      }
      std::u16string marker,rest;
      if((trimmed.starts_with(u"- ")||trimmed.starts_with(u"* ")||trimmed.starts_with(u"+ "))){marker=u"•";rest=trimmed.substr(2);}
      else{size_t d=0;while(d<trimmed.size()&&base::IsAsciiDigit(trimmed[d]))++d;if(d&&d<3&&d+1<trimmed.size()&&(trimmed[d]==u'.'||trimmed[d]==u')')&&trimmed[d+1]==u' '){marker=trimmed.substr(0,d+1);rest=trimmed.substr(d+2);}}
      if(!marker.empty()){
        flush();
        const size_t indent=line.find_first_not_of(u' ');
        auto* item=AddChildView(std::make_unique<views::View>());
        auto* box=item->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets::TLBR(0,indent>=2?16:0,0,0),6));
        box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStart);
        auto* bullet=item->AddChildView(std::make_unique<views::Label>(marker));
        bullet->SetFontList(Type(13,marker==u"•"?600:500));bullet->SetEnabledColor(c.muted);bullet->SetBackgroundColor(SK_ColorTRANSPARENT);
        bullet->SetPreferredSize(gfx::Size(marker==u"•"?10:20,19));bullet->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        box->SetFlexForView(item->AddChildView(Paragraph(rest,13,400,c.ink)),1);
        continue;
      }
      if(trimmed.starts_with(u">")){
        flush();auto* quote=AddChildView(Paragraph(Trim(trimmed.substr(1)),13,400,c.muted,true));quote->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0,12,0,0)));continue;
      }
      paragraph+=(paragraph.empty()?u"":u" ")+trimmed;
    }
    flush();
    PreferredSizeChanged();
  }
  std::u16string text_;base::RepeatingCallback<void(const GURL&)> open_;
};
BEGIN_METADATA(Markdown)
END_METADATA
}
std::unique_ptr<views::View> CreateAssistantMarkdown(std::u16string text,base::RepeatingCallback<void(const GURL&)> open){return std::make_unique<Markdown>(std::move(text),std::move(open));}
std::unique_ptr<views::View> CreateAssistantPlainText(std::u16string text){
  // Plain, selectable copy (the user's own messages): Markdown off.
  class Plain final : public views::View {
   public:
    explicit Plain(std::u16string text):text_(std::move(text)){SetLayoutManager(std::make_unique<views::FillLayout>());}
    void OnThemeChanged() override {
      views::View::OnThemeChanged();RemoveAllChildViews();const auto c=Theme(this);
      AddChildView(std::make_unique<SelectableText>(text_,std::vector<SelectableText::Span>(),Type(13),c.ink,c.ink,c.accent,base::DoNothing()));
      PreferredSizeChanged();
    }
   private:
    std::u16string text_;
  };
  return std::make_unique<Plain>(std::move(text));
}
std::unique_ptr<views::View> CreateAssistantEyebrow(std::u16string text,std::u16string trailing){return std::make_unique<Eyebrow>(std::move(text),std::move(trailing));}
std::unique_ptr<views::View> CreateAssistantMenuRow(base::RepeatingClosure action,AssistantRow row){return std::make_unique<MenuRow>(std::move(action),std::move(row));}
void SetAssistantMenuRowActive(views::View* row,bool active){if(auto* r=views::AsViewClass<MenuRow>(row))r->SetActive(active);}
std::unique_ptr<views::View> CreateAssistantTaskCard(AssistantTaskCard card,base::RepeatingClosure open,base::RepeatingClosure toggle,base::RepeatingClosure run,base::RepeatingClosure remove){
  return std::make_unique<TaskCard>(std::move(card),std::move(open),std::move(toggle),std::move(run),std::move(remove));
}
std::unique_ptr<views::View> CreateAssistantActivity(std::u16string text,int state){return std::make_unique<Activity>(std::move(text),state);}
std::unique_ptr<views::View> CreateAssistantNote(std::u16string text,std::optional<AssistantGlyph> glyph,bool tinted){return std::make_unique<Note>(std::move(text),glyph,tinted);}
std::unique_ptr<views::View> CreateAssistantFooter(std::u16string text,std::u16string keys){return std::make_unique<Footer>(std::move(text),std::move(keys));}
void SetAssistantFooterText(views::View* footer,std::u16string text){if(auto* f=views::AsViewClass<Footer>(footer))f->SetText(std::move(text));}
std::unique_ptr<views::View> CreateAssistantToken(AssistantMark mark,std::u16string text,bool tool,base::RepeatingClosure remove){return std::make_unique<Token>(std::move(mark),std::move(text),tool,std::move(remove));}
std::unique_ptr<views::View> CreateAssistantFlow(int spacing){return std::make_unique<Flow>(spacing);}
std::unique_ptr<views::View> CreateAssistantPill(base::RepeatingClosure action,std::u16string text,int kind,std::optional<AssistantGlyph> glyph,std::u16string key){return std::make_unique<Pill>(std::move(action),std::move(text),kind,glyph,std::move(key));}
std::unique_ptr<views::View> CreateAssistantLink(base::RepeatingClosure action,std::u16string text,std::optional<AssistantGlyph> glyph){return std::make_unique<Link>(std::move(action),std::move(text),glyph);}
std::unique_ptr<views::View> CreateAssistantElementPreview(std::u16string label){return std::make_unique<ElementPreview>(std::move(label));}
std::unique_ptr<views::View> CreateAssistantTaskRow(AssistantMark mark,std::u16string title,std::u16string status,int state,base::RepeatingClosure take_over,base::RepeatingClosure stop){return std::make_unique<TaskRow>(std::move(mark),std::move(title),std::move(status),state,std::move(take_over),std::move(stop));}
std::unique_ptr<views::View> CreateAssistantCapsule(std::u16string text,base::RepeatingClosure action,bool animate){return std::make_unique<Capsule>(std::move(text),std::move(action),animate);}
std::unique_ptr<views::View> CreateAssistantConsentBubble(std::u16string title,std::u16string detail,base::RepeatingClosure allow,base::RepeatingClosure skip,base::RepeatingClosure allow_all){return std::make_unique<ConsentBubble>(std::move(title),std::move(detail),std::move(allow),std::move(skip),std::move(allow_all));}
views::View* AssistantConsentAllowButton(views::View* bubble){auto* b=views::AsViewClass<ConsentBubble>(bubble);return b?b->allow():nullptr;}
std::unique_ptr<views::Background> CreateAssistantBackground(){return std::make_unique<AssistantBackground>();}
std::unique_ptr<views::View> CreateAssistantIdentity(){return std::make_unique<Identity>();}
std::unique_ptr<views::View> CreateAssistantIcon(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string name,bool primary){return std::make_unique<Action>(std::move(cb),glyph,std::move(name),primary,false);}
std::unique_ptr<views::View> CreateAssistantChip(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string text){return std::make_unique<Action>(std::move(cb),glyph,std::move(text),false,true);}
std::unique_ptr<views::View> CreateAssistantOption(base::RepeatingClosure cb,std::u16string text){return std::make_unique<Action>(std::move(cb),AssistantGlyph::kSpark,std::move(text),false,true,true);}
std::unique_ptr<views::View> CreateAssistantResizeGrip(views::ResizeAreaDelegate* delegate){return std::make_unique<Grip>(delegate);}
void UpdateAssistantButton(views::View* view,std::u16string text,bool selected){static_cast<Action*>(view)->Update(std::move(text),selected);}
std::unique_ptr<views::View> CreateAssistantSurface(int kind){return std::make_unique<Surface>(kind);}
std::unique_ptr<views::View> CreateAssistantWelcome(){return std::make_unique<Welcome>();}
std::unique_ptr<views::View> CreateAssistantSuggestion(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string title,std::u16string detail){return std::make_unique<Suggestion>(std::move(cb),glyph,std::move(title),std::move(detail));}
}
