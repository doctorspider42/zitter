// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>
#include "base/functional/bind.h"
#include "base/auto_reset.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "base/check_op.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_edit_commands.h"
#include "ui/base/ime/text_input_flags.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/drag_controller.h"
#include "ui/views/border.h"
#include "ui/views/word_lookup_client.h"
#include "ui/gfx/decorated_text.h"
#include "ui/gfx/font_list.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "chromium/prototype/editor_text_layout.h"
#include "ui/views/controls/textfield/text_edit_kill_buffer.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/sidebar_metal.h"
namespace zitter::prototype {
namespace {
bool SceneIsVisible(const views::View* view){return view->IsDrawn()&&view->GetWidget()&&view->GetWidget()->IsVisible();}
class SceneWindowObserver final:public views::WidgetObserver {
 public:
  explicit SceneWindowObserver(base::RepeatingClosure changed):changed_(std::move(changed)){}
  void Observe(views::Widget* widget){if(observation_.GetSource()==widget)return;observation_.Reset();if(widget)observation_.Observe(widget);}
  void OnWidgetVisibilityChanged(views::Widget*,bool) override{changed_.Run();}
  void OnWidgetDestroying(views::Widget*) override{observation_.Reset();}
 private:
  base::RepeatingClosure changed_;base::ScopedObservation<views::Widget,views::WidgetObserver> observation_{this};
};
class SceneEditor final : public views::View, public EditorControl,
    public ui::TextInputClient, public views::ContextMenuController,
    public views::DragController, public views::WordLookupClient {
  METADATA_HEADER(SceneEditor,views::View)
 public:
  using views::View::GetViewAccessibility;
  using views::View::HasFocus;
  using views::View::RequestFocus;
  SceneEditor():state_(sidebar::new_editor_state()) {
    SetFocusBehavior(FocusBehavior::ALWAYS);SetFlipCanvasOnPaintForRTLUI(false);
    set_context_menu_controller(this);set_drag_controller(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kTextField);
    GetViewAccessibility().SetIsEditable(true);
    enabled_subscription_=AddEnabledChangedCallback(base::BindRepeating(&SceneEditor::SyncLifecycle,base::Unretained(this)));
    RefreshText();
  }
  ~SceneEditor() override{Detach();}
  views::View* view() override{return this;}
  bool rust_editor() const override{return true;}
  bool metal_visible() const override{return surface_&&surface_->visible();}
  bool scene_contains(std::string_view text) const override{return metal_visible()&&surface_->contains_text_for_testing(text);}
  std::u16string_view GetText() const override{return text_;}
  ui::TextInputClient* input_client() override{return this;}
  void SetText(std::u16string_view text) override {
    CancelPointerSelection();++context_;CancelMenu();CancelNativeComposition();clauses_.clear();
    Apply(sidebar::editor_reset(*state_,Slice(text)));blink_visible_=true;
  }
  void SetPlaceholderText(std::u16string text) override{placeholder_=std::move(text);GetViewAccessibility().SetPlaceholder(base::UTF16ToUTF8(placeholder_));Schedule();}
  bool IsIMEComposing() const override{return sidebar::editor_info(*state_).composing;}
  gfx::Range GetSelectedRange() const override{auto i=sidebar::editor_info(*state_);return gfx::Range(i.anchor,i.focus);}
  void SelectRange(const gfx::Range& range) override{SetEditableSelectionRange(range);}
  void SetReadOnly(bool value) override{read_only_=value;SyncLifecycle();}
  void SetPresentation(int font_size,float inset,bool framed,std::optional<SkColor> fill) override {
    const auto size=std::clamp(font_size,1,256);if(font_size_!=size){font_size_=size;line_.reset();}
    inset_=std::isfinite(inset)?std::max(0.f,inset):0.f;framed_=framed;fill_=fill;Schedule();NotifyCaret();
  }
  void SetCallbacks(base::RepeatingClosure committed,base::RepeatingCallback<bool(const ui::KeyEvent&)> key) override {committed_=std::move(committed);key_=std::move(key);}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override{return gfx::Size(120,32);}
  ui::Cursor GetCursor(const ui::MouseEvent&) override{return ui::Cursor(ui::mojom::CursorType::kIBeam);}
  base::WeakPtr<ui::TextInputClient> AsWeakPtr() override{return weak_factory_.GetWeakPtr();}
  void SetCompositionText(const ui::CompositionText& composition) override {
    if(composition.text.empty()){ClearCompositionText();return;}
    if(!Editable())return;const auto token=sidebar::editor_begin_composition(*state_);if(!token)return;
    const auto range=composition.selection.IsValid()?composition.selection:gfx::Range(composition.text.size());
    auto update=sidebar::editor_compose(*state_,token,Slice(composition.text),range.start(),range.end());
    if(update.admitted)clauses_=composition.ime_text_spans;Apply(std::move(update));
  }
  size_t ConfirmCompositionText(bool keep) override {
    const auto i=sidebar::editor_info(*state_);if(!i.composing)return 0;const auto length=i.composition_end-i.composition_start;
    Apply(sidebar::editor_confirm(*state_,i.composition_token));clauses_.clear();
    if(keep)Apply(sidebar::editor_select(*state_,i.anchor,i.focus,i.secondary));return length;
  }
  void ClearCompositionText() override{auto i=sidebar::editor_info(*state_);if(i.composing)Apply(sidebar::editor_cancel(*state_,i.composition_token));clauses_.clear();}
  void InsertText(const std::u16string& text,InsertTextCursorBehavior behavior) override {
    if(!Editable())return;const auto before=sidebar::editor_info(*state_);const auto start=before.composing?before.composition_start:std::min(before.anchor,before.focus);
    auto clean=SingleLine(text);if(clean.empty())return;Apply(sidebar::editor_insert(*state_,Slice(clean),false));clauses_.clear();
    if(behavior==InsertTextCursorBehavior::kMoveCursorBeforeText)Apply(sidebar::editor_select(*state_,start,start,false));
  }
  void InsertChar(const ui::KeyEvent& event) override {
    if(!Editable()||event.IsCommandDown()||event.IsControlDown())return;
    const auto c=event.GetCharacter();if(c<0x20||(c>=0x7f&&c<=0x9f))return;const std::u16string text(1,c);Apply(sidebar::editor_insert(*state_,Slice(text),true));
  }
  ui::TextInputType GetTextInputType() const override{return Editable()?ui::TEXT_INPUT_TYPE_TEXT:ui::TEXT_INPUT_TYPE_NONE;}
  ui::TextInputMode GetTextInputMode() const override{return ui::TEXT_INPUT_MODE_DEFAULT;}
  base::i18n::TextDirection GetTextDirection() const override{return direction_.value_or(base::i18n::GetFirstStrongCharacterDirection(text_));}
  int GetTextInputFlags() const override{return ui::TEXT_INPUT_FLAG_NONE;}
  bool CanComposeInline() const override{return true;}
  FocusReason GetFocusReason() const override{return HasFocus()?FOCUS_REASON_OTHER:FOCUS_REASON_NONE;}
  bool HasCompositionText() const override{return IsIMEComposing();}
  gfx::Rect GetCaretBounds() const override{EnsureLayout();return ScreenRect(geometry_.caret);}
  gfx::Rect GetSelectionBoundingBox() const override {
    EnsureLayout();const auto range=GetSelectedRange();gfx::Rect result;
    for(const auto& span:line_->Spans(range.start(),range.end()))result.Union(ScreenRect({geometry_.origin+span.x,geometry_.caret.y,span.width,geometry_.caret.height}));return result;
  }
  bool GetCompositionCharacterBounds(size_t index,gfx::Rect* rect) const override {
    auto i=sidebar::editor_info(*state_);if(!i.composing||index>=i.composition_end-i.composition_start)return false;EnsureLayout();
    const size_t at=i.composition_start+index;const auto& stops=line_->graphemes();auto hi=std::upper_bound(stops.begin(),stops.end(),at);auto lo=hi;if(lo!=stops.begin())--lo;const auto spans=line_->Spans(*lo,hi==stops.end()?text_.size():*hi);gfx::Rect out;
    for(const auto& s:spans)out.Union(ScreenRect({geometry_.origin+s.x,geometry_.caret.y,s.width,geometry_.caret.height}));
    if(out.IsEmpty())out=ScreenRect({geometry_.origin+line_->Caret(i.composition_start+index).x,geometry_.caret.y,1,geometry_.caret.height});*rect=out;return true;
  }
  bool GetTextRange(gfx::Range* range) const override{if(!Available())return false;*range=gfx::Range(0,text_.size());return true;}
  bool GetCompositionTextRange(gfx::Range* range) const override{if(!Editable())return false;auto i=sidebar::editor_info(*state_);*range=i.composing?gfx::Range(i.composition_start,i.composition_end):gfx::Range::InvalidRange();return true;}
  bool GetEditableSelectionRange(gfx::Range* range) const override{if(!Available()||!HasFocus())return false;*range=GetSelectedRange();return true;}
  bool SetEditableSelectionRange(const gfx::Range& range) override {
    if(!Available()||!range.IsValid())return false;if(IsIMEComposing())ConfirmCompositionText(true);
    auto update=sidebar::editor_select(*state_,range.start(),range.end(),false);const bool ok=update.admitted;Apply(std::move(update));return ok;
  }
  bool DeleteRange(const gfx::Range& range) override {
    if(!Editable()||!range.IsValid())return false;if(IsIMEComposing())ConfirmCompositionText(true);
    auto selected=sidebar::editor_select(*state_,range.start(),range.end(),false);if(!selected.admitted)return false;
    Apply(std::move(selected));auto changed=sidebar::editor_insert(*state_,rust::Slice<const uint16_t>(),false);const bool ok=changed.admitted;Apply(std::move(changed));return ok;
  }
  bool GetTextFromRange(const gfx::Range& range,std::u16string* text) const override {
    if(!Available()||!range.IsValid()||range.GetMax()>text_.size())return false;*text=text_.substr(range.GetMin(),range.length());return true;
  }
  void ExtendSelectionAndDelete(size_t before,size_t after) override {
    if(!Editable())return;if(IsIMEComposing())ConfirmCompositionText(true);EnsureLayout();auto r=GetSelectedRange();
    size_t start=r.GetMin()-std::min(before,r.GetMin()),end=r.GetMax()+std::min(after,text_.size()-r.GetMax());
    const auto& stops=line_->graphemes();auto lo=std::upper_bound(stops.begin(),stops.end(),start);if(lo!=stops.begin())start=*--lo;auto hi=std::lower_bound(stops.begin(),stops.end(),end);if(hi!=stops.end())end=*hi;DeleteRange(gfx::Range(start,end));
  }
  void OnInputMethodChanged() override{Schedule();}
  bool ChangeTextDirectionAndLayoutAlignment(base::i18n::TextDirection direction) override{if(direction==base::i18n::UNKNOWN_DIRECTION)return false;direction_=direction_==direction?std::nullopt:std::optional(direction);align_right_=direction==base::i18n::RIGHT_TO_LEFT;line_.reset();EnsureLayout();RefreshAX();NotifyCaret();Schedule();return true;}
  void EnsureCaretNotInRect(const gfx::Rect&) override{EnsureLayout();NotifyCaret();}
  ukm::SourceId GetClientSourceForMetrics() const override{return ukm::kInvalidSourceId;}
  bool ShouldDoLearning() override{return false;}
  void GetActiveTextInputControlLayoutBounds(std::optional<gfx::Rect>* control,std::optional<gfx::Rect>* selection) override{*control=GetBoundsInScreen();*selection=GetSelectionBoundingBox();}
  bool IsTextEditCommandEnabled(ui::TextEditCommand command) const override {
    if(!Available())return false;using C=ui::TextEditCommand;const auto i=sidebar::editor_info(*state_);const bool selected=i.anchor!=i.focus;
    switch(command){
      case C::UNDO:return Editable()&&i.can_undo;
      case C::REDO:return Editable()&&i.can_redo;
      case C::CUT:return Editable()&&!i.composing&&selected;
      case C::COPY:return !i.composing&&selected;
      case C::PASTE:case C::YANK:return Editable();
      case C::SELECT_ALL:return !text_.empty()&&GetSelectedRange().length()!=text_.size();
      case C::SELECT_WORD:return !text_.empty()&&!selected;
      case C::TRANSPOSE:return Editable()&&!selected&&!i.composing;
      case C::DELETE_BACKWARD:case C::DELETE_FORWARD:case C::DELETE_WORD_BACKWARD:case C::DELETE_WORD_FORWARD:case C::DELETE_TO_BEGINNING_OF_LINE:case C::DELETE_TO_BEGINNING_OF_PARAGRAPH:case C::DELETE_TO_END_OF_LINE:case C::DELETE_TO_END_OF_PARAGRAPH:return Editable();
      case C::INSERT_TEXT:case C::SET_MARK:case C::UNSELECT:case C::INVALID_COMMAND:return false;
      default:return true;
    }
  }
  void SetTextEditCommandForNextKeyEvent(ui::TextEditCommand command) override{pending_command_=command;}
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    auto command=std::exchange(pending_command_,ui::TextEditCommand::INVALID_COMMAND);if(!Available())return false;
    if(key_){auto key=key_;auto alive=weak_factory_.GetWeakPtr();if(key.Run(event))return true;if(!alive)return false;}
    if(IsIMEComposing())return false;
    if(command!=ui::TextEditCommand::INVALID_COMMAND)return Execute(command);
    using C=ui::TextEditCommand;const bool modify=event.IsShiftDown();
    if(event.IsCommandDown()){switch(event.key_code()){case ui::VKEY_A:return Execute(C::SELECT_ALL);case ui::VKEY_C:return Execute(C::COPY);case ui::VKEY_X:return Execute(C::CUT);case ui::VKEY_V:return Execute(C::PASTE);case ui::VKEY_Z:return Execute(modify?C::REDO:C::UNDO);case ui::VKEY_LEFT:Boundary(false,modify);return true;case ui::VKEY_RIGHT:Boundary(true,modify);return true;case ui::VKEY_BACK:DeleteBoundary(false);return true;case ui::VKEY_DELETE:DeleteBoundary(true);return true;default:return false;}}
    if(event.key_code()==ui::VKEY_LEFT||event.key_code()==ui::VKEY_RIGHT){const bool right=event.key_code()==ui::VKEY_RIGHT;if(event.IsAltDown())Logical(right==(GetTextDirection()==base::i18n::RIGHT_TO_LEFT),true,modify);else Visual(right,modify);return true;}
    if(event.key_code()==ui::VKEY_PRIOR||event.key_code()==ui::VKEY_NEXT){RetainedBoundary(event.key_code()==ui::VKEY_NEXT,modify);return true;}
    if(event.key_code()==ui::VKEY_UP||event.key_code()==ui::VKEY_DOWN){RetainedBoundary(event.key_code()==ui::VKEY_DOWN,modify);return true;}
    if(event.key_code()==ui::VKEY_HOME||event.key_code()==ui::VKEY_END){Boundary(event.key_code()==ui::VKEY_END,modify);return true;}
    if(event.key_code()==ui::VKEY_BACK||event.key_code()==ui::VKEY_DELETE){if(!Editable())return false;EnsureLayout();Apply(sidebar::editor_erase(*state_,event.key_code()==ui::VKEY_BACK,event.IsAltDown()));return true;}
    if(event.IsControlDown()){switch(event.key_code()){case ui::VKEY_A:Boundary(false,modify);return true;case ui::VKEY_E:Boundary(true,modify);return true;case ui::VKEY_B:Visual(GetTextDirection()==base::i18n::RIGHT_TO_LEFT,modify);return true;case ui::VKEY_F:Visual(GetTextDirection()!=base::i18n::RIGHT_TO_LEFT,modify);return true;case ui::VKEY_H:return Execute(C::DELETE_BACKWARD);case ui::VKEY_D:return Execute(C::DELETE_FORWARD);case ui::VKEY_K:return Execute(C::DELETE_TO_END_OF_LINE);case ui::VKEY_U:return Execute(C::DELETE_TO_BEGINNING_OF_LINE);case ui::VKEY_Y:return Execute(C::YANK);case ui::VKEY_T:return Execute(C::TRANSPOSE);default:break;}}
    return false;
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if(!Available()||!event.IsOnlyLeftMouseButton())return views::View::OnMousePressed(event);
    CancelPointerSelection();RequestFocus();if(IsIMEComposing())ConfirmCompositionText(false);EnsureLayout();
    const auto stop=Hit(event.x());const auto r=GetSelectedRange();
    drag_candidate_=!event.IsShiftDown()&&event.GetClickCount()==1&&r.GetMin()<stop.index&&stop.index<r.GetMax();
    if(drag_candidate_)return true;
    dragging_=true;
    Apply(sidebar::editor_pointer_begin(*state_,stop.index,stop.secondary,event.IsShiftDown(),event.GetClickCount()));
    return true;
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if(!dragging_||!Available()||!event.IsOnlyLeftMouseButton())return drag_candidate_;
    last_drag_x_=std::clamp(event.x(),0.f,static_cast<float>(width()));
    if(event.x()>=0&&event.x()<=width()) {
      selection_scroll_.Stop();SelectThroughPointer();
    } else if(!selection_scroll_.IsRunning()) {
      SelectThroughPointer();
      if(dragging_)selection_scroll_.Start(FROM_HERE,base::Milliseconds(100),this,&SceneEditor::SelectThroughPointer);
    }
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if(drag_candidate_&&!drag_started_&&Available()){auto stop=Hit(event.x());Apply(sidebar::editor_move_to(*state_,stop.index,false,stop.secondary));}
    CancelPointerSelection();
  }
  void OnMouseCaptureLost() override{CancelPointerSelection();}
  void OnFocus() override{views::View::OnFocus();if(GetInputMethod())GetInputMethod()->SetFocusedTextInputClient(this);StartBlink();Schedule();}
  void OnBlur() override{CancelPointerSelection();Detach();blink_.Stop();Schedule();views::View::OnBlur();}
  void AddedToWidget() override{views::View::AddedToWidget();window_.Observe(GetWidget());SyncLifecycle();}
  void RemovedFromWidget() override{CancelPointerSelection();window_.Observe(nullptr);Detach();CancelMenu();blink_.Stop();surface_.reset();views::View::RemovedFromWidget();}
  void VisibilityChanged(views::View* changed,bool visible) override{views::View::VisibilityChanged(changed,visible);SyncLifecycle();}
  void OnBoundsChanged(const gfx::Rect&) override{Schedule();}
  bool GetNeedsNotificationWhenVisibleBoundsChange() const override{return true;}
  void OnVisibleBoundsChanged() override{Schedule();}
  void OnThemeChanged() override{views::View::OnThemeChanged();Schedule();}
  void OnPaint(gfx::Canvas* canvas) override{if(!metal_visible()){auto scene=Scene();PaintEditorSceneFallback(canvas,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())));}}
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if(!Available())return false;if(action.action==ax::mojom::Action::kSetSelection){if(action.anchor_offset<0||action.focus_offset<0)return false;return SetEditableSelectionRange(gfx::Range(action.anchor_offset,action.focus_offset));}
    if(Editable()&&action.action==ax::mojom::Action::kSetValue){ConfirmCompositionText(false);Apply(sidebar::editor_select(*state_,0,text_.size(),false));InsertText(base::UTF8ToUTF16(action.value),InsertTextCursorBehavior::kMoveCursorAfterText);return true;}
    if(Editable()&&action.action==ax::mojom::Action::kReplaceSelectedText){InsertText(base::UTF8ToUTF16(action.value),InsertTextCursorBehavior::kMoveCursorAfterText);return true;}
    return views::View::HandleAccessibleAction(action);
  }
  views::WordLookupClient* GetWordLookupClient() override{return this;}
  bool GetWordLookupDataAtPoint(const gfx::Point& point,gfx::DecoratedText* text,gfx::Rect* rect) override{if(!Available())return false;const auto at=Hit(point.x()).index;for(auto w:line_->words())if(at>=w.start&&at<=w.end)return Lookup(gfx::Range(w.start,w.end),text,rect);return false;}
  bool GetWordLookupDataFromSelection(gfx::DecoratedText* text,gfx::Rect* rect) override{return Available()&&Lookup(GetSelectedRange(),text,rect);}
  bool GetDropFormats(int* formats,std::set<ui::ClipboardFormatType>*) override{*formats=ui::OSExchangeData::STRING;return Editable();}
  bool CanDrop(const ui::OSExchangeData& data) override{return Editable()&&data.HasString();}
  int OnDragUpdated(const ui::DropTargetEvent& event) override{if(!CanDrop(event.data()))return ui::DragDropTypes::DRAG_NONE;auto stop=Hit(event.x());const auto r=GetSelectedRange();if(drag_started_&&(sidebar::editor_info(*state_).revision!=drag_revision_||r!=drag_selection_))return ui::DragDropTypes::DRAG_NONE;if(drag_started_&&stop.index>=r.GetMin()&&stop.index<=r.GetMax())return ui::DragDropTypes::DRAG_NONE;return drag_started_&&!event.IsControlDown()?ui::DragDropTypes::DRAG_MOVE:ui::DragDropTypes::DRAG_COPY;}
  DropCallback GetDropCallback(const ui::DropTargetEvent&) override {
    const auto revision=sidebar::editor_info(*state_).revision;const auto range=GetSelectedRange();const auto epoch=context_;const bool internal=drag_started_;if(internal&&(revision!=drag_revision_||range!=drag_selection_))return {};
    return base::BindOnce([](base::WeakPtr<SceneEditor> self,uint64_t epoch,uint64_t revision,gfx::Range selection,bool internal,const ui::DropTargetEvent& event,ui::mojom::DragOperation& operation,std::unique_ptr<ui::LayerTreeOwner>){
      if(!self||!self->CanDrop(event.data())||self->context_!=epoch||sidebar::editor_info(*self->state_).revision!=revision||self->GetSelectedRange()!=selection)return;
      auto stop=self->Hit(event.x());if(internal){const bool copy=event.IsControlDown()||!(event.source_operations()&ui::DragDropTypes::DRAG_MOVE);auto update=sidebar::editor_drop(*self->state_,stop.index,copy);if(!update.admitted)return;self->Apply(std::move(update));operation=copy?ui::mojom::DragOperation::kCopy:ui::mojom::DragOperation::kMove;}
      else {self->Apply(sidebar::editor_move_to(*self->state_,stop.index,false,stop.secondary));self->InsertText(event.data().GetString().value_or(u""),InsertTextCursorBehavior::kMoveCursorAfterText);operation=ui::mojom::DragOperation::kCopy;}
      self->RequestFocus();
    },weak_factory_.GetWeakPtr(),epoch,revision,range,internal);
  }
  void WriteDragDataForView(views::View*,const gfx::Point&,ui::OSExchangeData* data) override{const auto r=GetSelectedRange();data->SetString(text_.substr(r.GetMin(),r.length()));drag_started_=true;drag_revision_=sidebar::editor_info(*state_).revision;drag_selection_=r;}
  int GetDragOperationsForView(views::View*,const gfx::Point&) override{return Available()&&!GetSelectedRange().is_empty()?ui::DragDropTypes::DRAG_COPY|(Editable()?ui::DragDropTypes::DRAG_MOVE:0):ui::DragDropTypes::DRAG_NONE;}
  bool CanStartDragForView(views::View*,const gfx::Point&,const gfx::Point&) override{return drag_candidate_;}
  void OnDragDone() override{CancelPointerSelection();drag_started_=false;}
 private:
  static rust::Slice<const uint16_t> Slice(std::u16string_view text){return {reinterpret_cast<const uint16_t*>(text.data()),text.size()};}
  static std::u16string SingleLine(std::u16string_view text){std::u16string out;for(char16_t c:text)if(c>=0x20&&(c<0x7f||c>0x9f))out.push_back(c);return out;}
  SkColor Fill() const{return fill_.value_or(Color(ui::kColorTextfieldBackground,SK_ColorWHITE));}
  bool AlignRight() const{return align_right_.value_or(GetTextDirection()==base::i18n::RIGHT_TO_LEFT);}
  int WritingDirection() const {return !direction_?0:*direction_==base::i18n::RIGHT_TO_LEFT?2:1;}
  SkColor Color(ui::ColorId id,SkColor fallback) const {const auto* provider=GetColorProvider();return provider?provider->GetColor(id):fallback;}
  bool Available() const{return SceneIsVisible(this)&&GetEnabledInViewsSubtree();}
  bool Editable() const{return Available()&&!read_only_;}
  void Detach(){if(GetInputMethod())GetInputMethod()->DetachTextInputClient(this);}
  void CancelNativeComposition(){if(GetInputMethod()&&GetInputMethod()->GetTextInputClient()==this)GetInputMethod()->CancelComposition(this);}
  void NotifyCaret(){if(GetInputMethod())GetInputMethod()->OnCaretBoundsChanged(this);}
  void RefreshText(){text_revision_=sidebar::editor_info(*state_).revision;const auto text=sidebar::editor_text(*state_);text_.assign(text.begin(),text.end());line_.reset();}
  void RefreshAX(){auto i=sidebar::editor_info(*state_);GetViewAccessibility().SetValue(text_);GetViewAccessibility().SetTextDirection(static_cast<int32_t>(GetTextDirection()==base::i18n::RIGHT_TO_LEFT?ax::mojom::WritingDirection::kRtl:ax::mojom::WritingDirection::kLtr));GetViewAccessibility().SetTextSelStart(i.anchor);GetViewAccessibility().SetTextSelEnd(i.focus);GetViewAccessibility().SetReadOnly(read_only_);}
  void Apply(sidebar::EditorUpdate update){if(!update.admitted)return;if(!sidebar::editor_pointer_active(*state_)){selection_scroll_.Stop();dragging_=false;}if(update.text_changed){RefreshText();++edit_generation_;}if(!update.info.composing)clauses_.clear();EnsureLayout();RefreshAX();blink_visible_=true;Schedule();NotifyCaret();if(update.text_changed)GetViewAccessibility().NotifyEvent(ax::mojom::Event::kValueChanged,true);if(update.selection_changed)GetViewAccessibility().NotifyEvent(ax::mojom::Event::kTextSelectionChanged,true);if(update.committed&&committed_){auto notify=committed_;notify.Run();}}
  void EnsureLayout() const {
    if(!line_){line_=std::make_unique<EditorTextLayout>(text_,font_size_,false,WritingDirection());std::vector<size_t> words;for(auto word:line_->words()){words.push_back(word.start);words.push_back(word.end);}const auto& stops=line_->graphemes();sidebar::editor_install_boundaries(*state_,sidebar::editor_info(*state_).revision,rust::Slice<const size_t>(stops.data(),stops.size()),rust::Slice<const size_t>(words.data(),words.size()));}
    auto i=sidebar::editor_info(*state_);geometry_=sidebar::editor_viewport(*state_,width(),height(),line_->width(),line_->Caret(i.focus,i.secondary).x,line_->ascent(),line_->descent(),AlignRight(),inset_);
  }
  gfx::Rect ScreenRect(const sidebar::FloatRect& rect) const{gfx::Rect out=gfx::ToEnclosingRect(gfx::RectF(rect.x,rect.y,rect.width,rect.height));const auto origin=GetBoundsInScreen().origin();out.Offset(origin.x(),origin.y());return out;}
  EditorCaretStop Hit(float x) const{EnsureLayout();return line_->HitTest(x-geometry_.origin);}
  bool Lookup(gfx::Range range,gfx::DecoratedText* text,gfx::Rect* rect){if(range.is_empty()||range.GetMax()>text_.size())return false;EnsureLayout();const auto spans=line_->Spans(range.start(),range.end());if(spans.empty())return false;text->text=text_.substr(range.GetMin(),range.length());text->attributes.clear();const auto fonts=gfx::FontList().DeriveWithSizeDelta(font_size_-gfx::FontList().GetFontSize());text->attributes.emplace_back(gfx::Range(0,text->text.size()),fonts.GetPrimaryFont());*rect=gfx::ToEnclosingRect(gfx::RectF(geometry_.origin+spans.front().x,geometry_.caret.y,spans.front().width,geometry_.caret.height));return true;}
  void Logical(bool backward,bool word,bool extend){EnsureLayout();using P=sidebar::EditorSelectionPolicy;Apply(sidebar::editor_navigate(*state_,backward,word,!extend?P::None:word?P::Caret:P::Retain));}
  void VisualWord(bool right,bool extend){Logical(right==(GetTextDirection()==base::i18n::RIGHT_TO_LEFT),true,extend);}
  void MoveBoundary(bool end,sidebar::EditorSelectionPolicy policy){EnsureLayout();Apply(sidebar::editor_move_policy(*state_,end?text_.size():0,policy,false));}
  void Boundary(bool end,bool extend){MoveBoundary(end,extend?sidebar::EditorSelectionPolicy::Extend:sidebar::EditorSelectionPolicy::None);}
  void RetainedBoundary(bool end,bool extend){MoveBoundary(end,extend?sidebar::EditorSelectionPolicy::Retain:sidebar::EditorSelectionPolicy::None);}
  void DeleteBoundary(bool end){
    if(!Editable())return;EnsureLayout();auto killed=sidebar::editor_kill(*state_,end);
    if(killed.update.text_changed){CHECK_EQ(text_revision_,killed.before_revision);CHECK_LE(killed.start,killed.end);CHECK_LE(killed.end,text_.size());views::SetTextEditKillBuffer(std::u16string_view(text_).substr(killed.start,killed.end-killed.start));}
    Apply(std::move(killed.update));
  }
  void Visual(bool right,bool extend){EnsureLayout();auto i=sidebar::editor_info(*state_);if(line_->carets().empty())return;
    if(!extend&&i.anchor!=i.focus){const auto a=line_->Caret(i.anchor).x,b=line_->Caret(i.focus,i.secondary).x;const auto p=right?(a>b?i.anchor:i.focus):(a<b?i.anchor:i.focus);Apply(sidebar::editor_move_to(*state_,p,false,p==i.focus&&i.secondary));return;}
    if(extend&&!i.directed&&i.anchor!=i.focus){const auto a=line_->Caret(i.anchor).x,b=line_->Caret(i.focus,i.secondary).x;const bool reverse=right?a>b:a<b;if(reverse)Apply(sidebar::editor_orient(*state_,i.focus,i.anchor,false));else Apply(sidebar::editor_orient(*state_,i.anchor,i.focus,i.secondary));i=sidebar::editor_info(*state_);}
    EnsureLayout();const auto& stops=line_->carets();
    auto at=std::find_if(stops.begin(),stops.end(),[&](const auto& s){return s.index==i.focus&&s.secondary==i.secondary;});if(at==stops.end())at=stops.begin();if(right){if(at+1!=stops.end())++at;}else if(at!=stops.begin())--at;Apply(sidebar::editor_move_to(*state_,at->index,extend,at->secondary));
  }
  void CancelPointerSelection(){selection_scroll_.Stop();dragging_=false;drag_candidate_=false;sidebar::editor_pointer_end(*state_);}
  void SelectThroughPointer(){
    if(!dragging_||!Available()||!HasFocus()){CancelPointerSelection();return;}
    EnsureLayout();const auto stop=Hit(last_drag_x_);
    auto update=sidebar::editor_pointer_drag(*state_,stop.index,stop.secondary);
    if(!update.admitted){CancelPointerSelection();return;}
    const bool changed=update.selection_changed;Apply(std::move(update));
    // Stop idle wakeups once the caret reaches the text edge. A new drag event
    // can start scrolling again, including after reversing direction.
    if(!changed)selection_scroll_.Stop();
  }

  bool Execute(ui::TextEditCommand command) {
    if(!IsTextEditCommandEnabled(command))return false;using C=ui::TextEditCommand;EnsureLayout();
    switch(command){
      case C::TRANSPOSE:Apply(sidebar::editor_transpose(*state_));return true;
      case C::YANK:Apply(sidebar::editor_yank(*state_,Slice(views::GetTextEditKillBuffer())));return true;
      case C::UNDO:Apply(sidebar::editor_undo(*state_));return true;
      case C::REDO:Apply(sidebar::editor_redo(*state_));return true;
      case C::SELECT_ALL:Apply(sidebar::editor_select(*state_,0,text_.size(),false));return true;
      case C::SELECT_WORD:Apply(sidebar::editor_select_word(*state_));return true;
      case C::COPY:case C::CUT:{const auto r=GetSelectedRange();{ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);writer.WriteText(text_.substr(r.GetMin(),r.length()));}if(command==C::CUT)Apply(sidebar::editor_insert(*state_,rust::Slice<const uint16_t>(),false));return true;}
      case C::PASTE:{const auto context=context_,generation=edit_generation_;const auto i=sidebar::editor_info(*state_);ui::Clipboard::GetForCurrentThread()->ReadText(ui::ClipboardBuffer::kCopyPaste,std::nullopt,base::BindOnce([](base::WeakPtr<SceneEditor> self,uint64_t context,uint64_t generation,size_t anchor,size_t focus,std::u16string text){if(!self||!self->Editable()||!self->HasFocus()||self->context_!=context||self->edit_generation_!=generation)return;auto i=sidebar::editor_info(*self->state_);if(i.anchor!=anchor||i.focus!=focus||i.composing)return;self->InsertText(text,InsertTextCursorBehavior::kMoveCursorAfterText);},weak_factory_.GetWeakPtr(),context,generation,i.anchor,i.focus));return true;}
      case C::DELETE_BACKWARD:case C::DELETE_FORWARD:case C::DELETE_WORD_BACKWARD:case C::DELETE_WORD_FORWARD:if(!Editable())return false;Apply(sidebar::editor_erase(*state_,command==C::DELETE_BACKWARD||command==C::DELETE_WORD_BACKWARD,command==C::DELETE_WORD_BACKWARD||command==C::DELETE_WORD_FORWARD));return true;
      case C::DELETE_TO_BEGINNING_OF_LINE:case C::DELETE_TO_BEGINNING_OF_PARAGRAPH:DeleteBoundary(false);return true;
      case C::DELETE_TO_END_OF_LINE:case C::DELETE_TO_END_OF_PARAGRAPH:DeleteBoundary(true);return true;
      case C::MOVE_BACKWARD:case C::MOVE_BACKWARD_AND_MODIFY_SELECTION:Visual(GetTextDirection()==base::i18n::RIGHT_TO_LEFT,command==C::MOVE_BACKWARD_AND_MODIFY_SELECTION);return true;
      case C::MOVE_FORWARD:case C::MOVE_FORWARD_AND_MODIFY_SELECTION:Visual(GetTextDirection()!=base::i18n::RIGHT_TO_LEFT,command==C::MOVE_FORWARD_AND_MODIFY_SELECTION);return true;
      case C::MOVE_WORD_BACKWARD:case C::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION:Logical(true,true,command==C::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION);return true;
      case C::MOVE_WORD_FORWARD:case C::MOVE_WORD_FORWARD_AND_MODIFY_SELECTION:Logical(false,true,command==C::MOVE_WORD_FORWARD_AND_MODIFY_SELECTION);return true;
      case C::MOVE_WORD_LEFT:case C::MOVE_WORD_LEFT_AND_MODIFY_SELECTION:VisualWord(false,command==C::MOVE_WORD_LEFT_AND_MODIFY_SELECTION);return true;
      case C::MOVE_WORD_RIGHT:case C::MOVE_WORD_RIGHT_AND_MODIFY_SELECTION:VisualWord(true,command==C::MOVE_WORD_RIGHT_AND_MODIFY_SELECTION);return true;
      case C::MOVE_LEFT:case C::MOVE_LEFT_AND_MODIFY_SELECTION:Visual(false,command==C::MOVE_LEFT_AND_MODIFY_SELECTION);return true;
      case C::MOVE_RIGHT:case C::MOVE_RIGHT_AND_MODIFY_SELECTION:Visual(true,command==C::MOVE_RIGHT_AND_MODIFY_SELECTION);return true;
      case C::MOVE_TO_BEGINNING_OF_DOCUMENT:case C::MOVE_TO_BEGINNING_OF_LINE:case C::MOVE_TO_BEGINNING_OF_PARAGRAPH:case C::MOVE_UP:case C::MOVE_PAGE_UP:case C::SCROLL_PAGE_UP:case C::SCROLL_TO_BEGINNING_OF_DOCUMENT:Boundary(false,false);return true;
      case C::MOVE_TO_BEGINNING_OF_DOCUMENT_AND_MODIFY_SELECTION:case C::MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION:case C::MOVE_TO_BEGINNING_OF_PARAGRAPH_AND_MODIFY_SELECTION:Boundary(false,true);return true;
      case C::MOVE_TO_END_OF_DOCUMENT:case C::MOVE_TO_END_OF_LINE:case C::MOVE_TO_END_OF_PARAGRAPH:case C::MOVE_DOWN:case C::MOVE_PAGE_DOWN:case C::SCROLL_PAGE_DOWN:case C::SCROLL_TO_END_OF_DOCUMENT:Boundary(true,false);return true;
      case C::MOVE_TO_END_OF_DOCUMENT_AND_MODIFY_SELECTION:case C::MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION:case C::MOVE_TO_END_OF_PARAGRAPH_AND_MODIFY_SELECTION:Boundary(true,true);return true;
      case C::MOVE_UP_AND_MODIFY_SELECTION:case C::MOVE_PAGE_UP_AND_MODIFY_SELECTION:RetainedBoundary(false,true);return true;
      case C::MOVE_DOWN_AND_MODIFY_SELECTION:case C::MOVE_PAGE_DOWN_AND_MODIFY_SELECTION:RetainedBoundary(true,true);return true;
      case C::MOVE_PARAGRAPH_BACKWARD_AND_MODIFY_SELECTION:MoveBoundary(false,sidebar::EditorSelectionPolicy::Caret);return true;
      case C::MOVE_PARAGRAPH_FORWARD_AND_MODIFY_SELECTION:MoveBoundary(true,sidebar::EditorSelectionPolicy::Caret);return true;
      default:return false;
    }
  }
  rust::Vec<sidebar::Primitive> Scene() const {
    EnsureLayout();auto i=sidebar::editor_info(*state_);std::vector<sidebar::EditorSpan> selected,composition;
    for(auto s:line_->Spans(i.anchor,i.focus))selected.push_back({s.x,s.width,0,0});
    if(i.composing){if(clauses_.empty()){for(auto s:line_->Spans(i.composition_start,i.composition_end))composition.push_back({s.x,s.width,1,Color(ui::kColorTextfieldForeground,SK_ColorBLACK)});}else for(const auto& clause:clauses_) {const size_t length=i.composition_end-i.composition_start;if(clause.start_offset>=clause.end_offset||clause.end_offset>length||clause.underline_style==ui::ImeTextSpan::UnderlineStyle::kNone)continue;for(auto s:line_->Spans(i.composition_start+clause.start_offset,i.composition_start+clause.end_offset))composition.push_back({s.x,s.width,clause.thickness==ui::ImeTextSpan::Thickness::kThick?2.f:1.f,clause.underline_color?clause.underline_color:Color(ui::kColorTextfieldForeground,SK_ColorBLACK)});}}
    sidebar::EditorStyle style{};style.right_align=AlignRight();style.text_direction=WritingDirection();if(text_.empty()&&!placeholder_.empty()){EditorTextLayout hint(placeholder_,font_size_,false,WritingDirection());style.placeholder_width=hint.width();}style.width=width();style.height=height();style.font_size=font_size_;style.framed=framed_;style.ink=Color(GetEnabledInViewsSubtree()?ui::kColorTextfieldForeground:ui::kColorTextfieldForegroundDisabled,SK_ColorBLACK);style.fill=Fill();style.edge=Color(HasFocus()?ui::kColorFocusableBorderFocused:ui::kColorTextfieldOutline,SK_ColorGRAY);style.selection_fill=Color(ui::kColorTextfieldSelectionBackground,0xFF6755C8);style.selection_ink=Color(ui::kColorTextfieldSelectionForeground,SK_ColorWHITE);style.placeholder_ink=Color(ui::kColorTextfieldForegroundPlaceholder,SK_ColorGRAY);style.focused=HasFocus();style.caret_visible=blink_visible_&&Editable();
    return sidebar::editor_scene(*state_,Slice(placeholder_),geometry_,style,rust::Slice<const sidebar::EditorSpan>(selected.data(),selected.size()),rust::Slice<const sidebar::EditorSpan>(composition.data(),composition.size()));
  }
  void Schedule(){if(pending_)return;pending_=true;base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(&SceneEditor::UpdateMetal,weak_factory_.GetWeakPtr()));SchedulePaint();}
  void UpdateMetal(){pending_=false;if(!GetWidget())return;GetWidget()->LayoutRootViewIfNecessary();if(!SceneIsVisible(this)||GetVisibleBounds().IsEmpty()){if(surface_)surface_->Hide();return;}if(!surface_)surface_=CreateSidebarMetal();if(!surface_)return;auto scene=Scene();const bool was=metal_visible();auto bounds=GetBoundsInScreen();auto visible=GetVisibleBounds();bounds.Offset(visible.x(),visible.y());bounds.set_size(visible.size());for(auto& op:scene){const auto clip=gfx::IntersectRects(gfx::RectF(op.clip.x,op.clip.y,op.clip.width,op.clip.height),gfx::RectF(visible));op.x-=visible.x();op.y-=visible.y();op.clip={clip.x()-visible.x(),clip.y()-visible.y(),clip.width(),clip.height()};}surface_->UpdateScene(GetWidget()->GetNativeWindow(),bounds,UNSAFE_BUFFERS(base::span<const sidebar::Primitive>(scene.data(),scene.size())),Fill());if(was!=metal_visible())SchedulePaint();}
  void StartBlink(){blink_visible_=true;if(!Editable()||!HasFocus())return;blink_.Start(FROM_HERE,base::Milliseconds(500),base::BindRepeating([](SceneEditor* self){self->blink_visible_=!self->blink_visible_;self->Schedule();},base::Unretained(this)));}
  void SyncLifecycle(){auto update=sidebar::editor_availability(*state_,GetEnabledInViewsSubtree(),read_only_);Apply(std::move(update));if(!Available()){++context_;CancelMenu();CancelNativeComposition();ClearCompositionText();Detach();blink_.Stop();OnMouseCaptureLost();if(surface_)surface_->Hide();}else if(HasFocus()){if(GetInputMethod())GetInputMethod()->SetFocusedTextInputClient(this);StartBlink();}if(GetInputMethod())GetInputMethod()->OnTextInputTypeChanged(this);Schedule();}
  void CancelMenu(){++menu_generation_;if(runner_)runner_->Cancel();}
  void ShowContextMenuForViewImpl(views::View*,const gfx::Point& point,ui::mojom::MenuSourceType source) override {
    if(!Available())return;CancelMenu();runner_.reset();menu_.reset();menu_delegate_=std::make_unique<MenuDelegate>(weak_factory_.GetWeakPtr(),context_,menu_generation_);menu_=std::make_unique<ui::SimpleMenuModel>(menu_delegate_.get());for(auto entry:{std::pair{ui::TextEditCommand::UNDO,T(u"Cofnij")},std::pair{ui::TextEditCommand::REDO,T(u"Ponów")},std::pair{ui::TextEditCommand::CUT,T(u"Wytnij")},std::pair{ui::TextEditCommand::COPY,T(u"Kopiuj")},std::pair{ui::TextEditCommand::PASTE,T(u"Wklej")},std::pair{ui::TextEditCommand::SELECT_ALL,T(u"Zaznacz wszystko")}})menu_->AddItem(static_cast<int>(entry.first),entry.second);
    runner_=std::make_unique<views::MenuRunner>(menu_.get(),views::MenuRunner::CONTEXT_MENU);runner_->RunMenuAt(GetWidget(),nullptr,gfx::Rect(point,gfx::Size()),views::MenuAnchorPosition::kTopLeft,source);
  }
  class MenuDelegate final:public ui::SimpleMenuModel::Delegate {
   public:
    MenuDelegate(base::WeakPtr<SceneEditor> owner,uint64_t context,uint64_t serial):owner_(owner),context_(context),serial_(serial){}
    bool IsCommandIdEnabled(int id) const override{return Current()&&owner_->IsTextEditCommandEnabled(static_cast<ui::TextEditCommand>(id));}
    void ExecuteCommand(int id,int) override{if(Current())owner_->Execute(static_cast<ui::TextEditCommand>(id));}
   private:
    bool Current() const{return owner_&&owner_->context_==context_&&owner_->menu_generation_==serial_&&owner_->Available();}
    base::WeakPtr<SceneEditor> owner_;uint64_t context_,serial_;
  };
  int font_size_=13;float inset_=8;bool framed_=true;std::optional<SkColor> fill_;
  mutable rust::Box<sidebar::EditorState> state_;std::u16string text_,placeholder_;mutable std::unique_ptr<EditorTextLayout> line_;mutable sidebar::EditorGeometry geometry_{};
  ui::ImeTextSpans clauses_;std::optional<base::i18n::TextDirection> direction_;std::optional<bool> align_right_;bool read_only_=false,pending_=false,blink_visible_=true,dragging_=false,drag_candidate_=false,drag_started_=false;uint64_t text_revision_=0,context_=0,edit_generation_=0,menu_generation_=0,drag_revision_=0;gfx::Range drag_selection_;
  base::RepeatingClosure committed_;base::RepeatingCallback<bool(const ui::KeyEvent&)> key_;
  ui::TextEditCommand pending_command_=ui::TextEditCommand::INVALID_COMMAND;base::RepeatingTimer blink_,selection_scroll_;float last_drag_x_=0;base::CallbackListSubscription enabled_subscription_;std::unique_ptr<SidebarMetal> surface_;std::unique_ptr<MenuDelegate> menu_delegate_;std::unique_ptr<ui::SimpleMenuModel> menu_;std::unique_ptr<views::MenuRunner> runner_;
  SceneWindowObserver window_{base::BindRepeating(&SceneEditor::SyncLifecycle,base::Unretained(this))};base::WeakPtrFactory<SceneEditor> weak_factory_{this};
};
BEGIN_METADATA(SceneEditor)
END_METADATA
}  // namespace
EditorControl* AddEditor(views::View* parent){
  return parent->AddChildView(std::make_unique<SceneEditor>());
}

} // namespace zitter::prototype
