// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_visuals.h"
#include <algorithm>
#include <array>
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/i18n.h"
#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "cc/paint/paint_shader.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_utils.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"

namespace zitter::prototype {
namespace {
struct Colors { SkColor bg, card, ink, muted, line, accent; bool dark; };
Colors Theme(const views::View* view) {
  const auto* provider = view->GetColorProvider();
  const auto bg = provider ? provider->GetColor(ui::kColorDialogBackground) : SK_ColorBLACK;
  const bool dark = SkColorGetR(bg) + SkColorGetG(bg) + SkColorGetB(bg) < 384;
  return dark ? Colors{0xff111217,0xff1a1c24,0xfff3f3f7,0xff979ba9,0xff30323e,0xffb4a6ff,true}
              : Colors{0xfffaf9f6,0xfff0efed,0xff22232b,0xff6b6d7a,0xffdedde3,0xff6d54d8,false};
}
gfx::FontList Type(int size, int weight = 400, bool display = false) {
  return gfx::FontList({std::string(sidebar::design_font_family(display ? 1 : 2))}, display ? gfx::Font::ITALIC : gfx::Font::NORMAL,
      size, static_cast<gfx::Font::Weight>(weight));
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
    case AssistantGlyph::kAt: canvas->DrawStringRectWithFlags(u"@",Type(17,500),color,gfx::Rect(0,-2,17,21),gfx::Canvas::TEXT_ALIGN_CENTER);break;
    case AssistantGlyph::kHistory: canvas->DrawCircle(gfx::PointF(8,8),5.5f,f);line(8,4.5,8,8);line(8,8,10.5,9.5);break;
    case AssistantGlyph::kSearch: canvas->DrawCircle(gfx::PointF(6,6),4.5f,f);line(9.5,9.5,14,14);break;
    case AssistantGlyph::kChevron: line(5,6,8,9);line(8,9,11,6);break;
    case AssistantGlyph::kPin: line(5,3,11,3);line(6,3,6,7);line(10,3,10,7);line(6,7,3.5,10);line(10,7,12.5,10);line(3.5,10,12.5,10);line(8,10,8,14);break;
    case AssistantGlyph::kShield: { SkPathBuilder p; p.moveTo(8,2);p.lineTo(13,4);p.lineTo(12,10);p.quadTo(10,13,8,14);p.quadTo(6,13,4,10);p.lineTo(3,4);p.close();canvas->DrawPath(p.detach(),f);line(5.5,7.5,7.5,9.5);line(7.5,9.5,11,6);break; }
    case AssistantGlyph::kPage: Round(canvas,gfx::RectF(3,2,10,12),2,color,1.5);line(6,6,10,6);line(6,9,10,9);break;
    case AssistantGlyph::kSpark: { SkPathBuilder p; p.moveTo(8,1);p.quadTo(9,7,15,8);p.quadTo(9,9,8,15);p.quadTo(7,9,1,8);p.quadTo(7,7,8,1);p.close();canvas->DrawPath(p.detach(),f);break; }
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
  Action(base::RepeatingClosure action,AssistantGlyph glyph,std::u16string text,bool primary,bool chip)
      :Button(base::BindRepeating(std::move(action))),glyph_(glyph),text_(std::move(text)),primary_(primary),chip_(chip) {
    SetFocusBehavior(FocusBehavior::ALWAYS);SetInstallFocusRingOnFocus(true);GetViewAccessibility().SetName(text_);SetTooltipText(text_);
  }
  void Update(std::u16string text,bool selected) {text_=std::move(text);selected_=selected;GetViewAccessibility().SetName(text_);SetTooltipText(text_);PreferredSizeChanged();SchedulePaint();}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    const bool disclosure=chip_&&(glyph_==AssistantGlyph::kSpark||glyph_==AssistantGlyph::kShield);
    return {chip_?std::min(280,gfx::GetStringWidth(text_,Type(12,500))+(disclosure?58:48)):primary_?36:28,primary_?36:28};
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto c=Theme(this);const bool hover=GetState()==STATE_HOVERED||GetState()==STATE_PRESSED;
    auto rect=gfx::RectF(GetLocalBounds());rect.Inset(.5f);
    const auto fill=primary_?c.accent:selected_?SkColorSetA(c.accent,c.dark?35:22):hover?c.card:SK_ColorTRANSPARENT;
    Round(canvas,rect,chip_?10:primary_?18:9,fill);
    if(chip_&&selected_)Round(canvas,rect,10,SkColorSetA(c.accent,60),1);
    const auto ink=!GetEnabled()?SkColorSetA(c.muted,100):primary_?(c.dark?0xff171322:SK_ColorWHITE):selected_?c.accent:c.muted;
    const int gx=chip_?8:(width()-16)/2;Glyph(canvas,glyph_,gx,(height()-16)/2,ink);
    if(chip_){
      const bool disclosure=glyph_==AssistantGlyph::kSpark||glyph_==AssistantGlyph::kShield;
      canvas->DrawStringRectWithFlags(text_,Type(12,500),ink,gfx::Rect(31,0,std::max(0,width()-(disclosure?56:39)),height()),gfx::Canvas::TEXT_ALIGN_LEFT);
      if(disclosure){const int end=31+std::min(gfx::GetStringWidth(text_,Type(12,500)),std::max(0,width()-56));Glyph(canvas,AssistantGlyph::kChevron,end+4,(height()-16)/2,ink);}
    }
  }
 private:
  AssistantGlyph glyph_;std::u16string text_;bool primary_,chip_,selected_=false;
};
BEGIN_METADATA(Action)
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
std::unique_ptr<views::Background> CreateAssistantBackground(){return std::make_unique<AssistantBackground>();}
std::unique_ptr<views::View> CreateAssistantIdentity(){return std::make_unique<Identity>();}
std::unique_ptr<views::View> CreateAssistantIcon(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string name,bool primary){return std::make_unique<Action>(std::move(cb),glyph,std::move(name),primary,false);}
std::unique_ptr<views::View> CreateAssistantChip(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string text){return std::make_unique<Action>(std::move(cb),glyph,std::move(text),false,true);}
void UpdateAssistantButton(views::View* view,std::u16string text,bool selected){static_cast<Action*>(view)->Update(std::move(text),selected);}
std::unique_ptr<views::View> CreateAssistantSurface(int kind){return std::make_unique<Surface>(kind);}
std::unique_ptr<views::View> CreateAssistantWelcome(){return std::make_unique<Welcome>();}
std::unique_ptr<views::View> CreateAssistantSuggestion(base::RepeatingClosure cb,AssistantGlyph glyph,std::u16string title,std::u16string detail){return std::make_unique<Suggestion>(std::move(cb),glyph,std::move(title),std::move(detail));}
}
