// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Control layout, presentation and input policy; browser state and command ownership
// remain in WorkspaceShell. No URLs, profiles or WebContents cross this ABI.
#[cxx::bridge(namespace = "zitter::sidebar")]
mod ffi {
    struct WidthChange { width: i32, changed: bool }
    struct ShellLayout { content: Rect, edge: Rect, sidebar: Rect }
    struct PaneHostLayout { first: Rect, second: Rect, first_bar: Rect, second_bar: Rect, divider: Rect }
    struct PaneBarLayout { title: Rect, menu: Rect, close: Rect }
    struct DragRatio { ratio: f64, changed: bool }
    struct ScrollMetrics { position: i32, length: i32, offset: i32, max_offset: i32 }
    struct FloatRect { x: f32, y: f32, width: f32, height: f32 }
    struct ViewportNode { parent: usize, bounds: FloatRect, visible: bool }
    struct ProjectedNode { index: usize, bounds: FloatRect, clip: FloatRect }
    struct ViewportProjection { x: f32, y: f32, nodes: Vec<ProjectedNode> }
    struct Item {
        x: f32, y: f32, width: f32, height: f32,
        // 0: row, 1: section label, 2: split group background, 3: New Tab action, 4: address, 5: workspace heading, 6: scroll thumb, 7: control glyph, 8: pane title, 9: split grip, 10: rounded backing, 11: sidebar grip.
        kind: u32, glyph: u32,
        title: String, monogram: String, detail: String,
        title_width: f32, detail_ink: u32, detail_font_size: f32,
        ink: u32, fill: u32, badge: u32,
        active: bool, hovered: bool, tile: bool, close: bool, close_visible: bool, focused: bool,
        font_size: f32,
        clip: FloatRect,
    }
    struct TreeNode { id: String, parent: String, folder: bool }
    struct TreeEntry { index: usize, parent: usize, depth: i32, collapsed: bool, visible: bool }
    struct TabNode { index: usize, pin: String }
    struct TabSlot { first: usize, second: usize, pin: usize, pair: bool }
    struct PinVisibility { index: usize, visible: bool }
    struct TabPlan { slots: Vec<TabSlot>, pins: Vec<PinVisibility> }
    enum PointerPhase { Enter, Exit, Move, Press, Drag, Release }
    enum RowVisual { Normal, Hovered, Pressed, Disabled }
    enum RowAction { None, Activate, Drop, Cancel }
    struct RowInputResult { visual: RowVisual, action: RowAction, handled: bool }
    struct Rect { x: i32, y: i32, width: i32, height: i32 }
    struct RowLayout { title: Rect, detail: Rect, close: Rect }
    struct ColumnChild { width: i32, height: i32, top: i32, left: i32, bottom: i32, right: i32 }
    struct ColumnLayout { width: i32, height: i32, children: Vec<Rect> }
    struct Primitive {
        x: f32, y: f32, width: f32, height: f32,
        radius: f32, color: u32, stroke: f32, shape: u32,
        text: String, font_size: f32, bold: bool, middle: bool, center: bool, clip: FloatRect,
    }
    extern "Rust" {
        type SidebarDrag;
        fn new_sidebar_drag() -> Box<SidebarDrag>;
        fn begin(self: &mut SidebarDrag, width: i32, pointer: f64);
        fn update(self: &mut SidebarDrag, pointer: f64, reverse: bool) -> WidthChange;
        fn delta(self: &mut SidebarDrag, delta: f64, reverse: bool) -> WidthChange;
        fn end(self: &mut SidebarDrag) -> WidthChange;
        fn cancel(self: &mut SidebarDrag);
        fn active(self: &SidebarDrag) -> bool;
        fn sidebar_resize_width(initial: i32, amount: i32) -> i32;
        fn shell_layout(width: i32, height: i32, preferred: i32, pinned: bool) -> ShellLayout;
        fn sidebar_grip_layout(width: i32, height: i32) -> Rect;
        fn sidebar_auto_hide(pinned: bool, blocked: bool, inside: bool, dragging: bool) -> bool;
        type SplitDrag;
        fn new_split_drag() -> Box<SplitDrag>;
        fn begin(self: &mut SplitDrag, ratio: f64, pointer: f64);
        fn update(self: &mut SplitDrag, pointer: f64, extent: i32, reverse: bool) -> DragRatio;
        fn delta(self: &mut SplitDrag, delta: f64, extent: i32, reverse: bool) -> DragRatio;
        fn end(self: &mut SplitDrag) -> DragRatio;
        fn cancel(self: &mut SplitDrag);
        fn active(self: &SplitDrag) -> bool;
        fn moved(self: &SplitDrag) -> bool;
        fn pane_host_layout(width: i32, height: i32, ratio: f64, vertical: bool, split: bool) -> PaneHostLayout;
        fn pane_bar_layout(width: i32, height: i32) -> PaneBarLayout;
        type ScrollThumb;
        fn new_scroll_thumb() -> Box<ScrollThumb>;
        fn begin(self: &mut ScrollThumb, track: i32, viewport: i32, content: i32, offset: i32, pointer: f32);
        fn drag(self: &ScrollThumb, track: i32, viewport: i32, content: i32, offset: i32,
                pointer: f32, cross: f32, thickness: i32, snap: bool) -> i32;
        fn end(self: &mut ScrollThumb);
        fn scroll_metrics(track: i32, viewport: i32, content: i32, offset: i32) -> ScrollMetrics;
        fn scroll_offset(track: i32, viewport: i32, content: i32, position: f32) -> i32;
        type Viewport;
        fn new_viewport() -> Box<Viewport>;
        fn project(self: &mut Viewport, content_width: f32, content_height: f32,
                   width: f32, height: f32, x: f32, y: f32,
                   nodes: &[ViewportNode]) -> ViewportProjection;
        type SidebarTree;
        fn new_sidebar_tree() -> Box<SidebarTree>;
        fn tree_plan(self: &SidebarTree, workspace: &str, nodes: &[TreeNode]) -> Vec<TreeEntry>;
        fn tab_plan(self: &SidebarTree, workspace: &str, nodes: &[TreeNode], tabs: &[TabNode],
                    first: usize, second: usize, split: bool) -> TabPlan;
        fn collapsed(self: &SidebarTree, workspace: &str, id: &str) -> bool;
        fn set_collapsed(self: &mut SidebarTree, workspace: &str, id: &str, value: bool);
        fn toggle(self: &mut SidebarTree, workspace: &str, id: &str) -> bool;
        fn forget_workspace(self: &mut SidebarTree, workspace: &str);
        type RowPointer;
        fn new_row_pointer() -> Box<RowPointer>;
        fn press(self: &mut RowPointer, x: i32, y: i32);
        fn drag(self: &mut RowPointer, x: i32, y: i32, enabled: bool);
        fn release(self: &mut RowPointer) -> bool;
        fn cancel(self: &mut RowPointer);
        fn pointer_event(self: &mut RowPointer, phase: PointerPhase, x: i32, y: i32,
                         inside: bool, enabled: bool, trigger: bool, drop: bool) -> RowInputResult;
        fn key_policy(key: u32, mac: bool) -> u32;
        // Platform policy: 0 none, 1 activate on key press, 2 on release.
        fn key_event(self: &mut RowPointer, pressed: bool, policy: u32,
                     enabled: bool) -> RowInputResult;
        fn column_layout(width: i32, spacing: i32, top: i32, left: i32,
                         bottom: i32, right: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn split_layout(width: i32, height: i32, count: usize) -> ColumnLayout;
        fn folder_indent(depth: i32) -> i32;
        fn close_visible(active: bool, tile: bool, hovered: bool) -> bool;
        fn footer_layout(width: i32, content_width: i32) -> ColumnLayout;
        fn strip_layout(width: i32, height: i32, spacing: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn action_frame(row: Rect, viewport: Rect) -> Rect;
        fn address_layout(width: i32, height: i32, host_width: i32) -> RowLayout;
        fn row_layout(width: i32, height: i32, title_width: i32,
                      detail: bool, chip: bool, close: bool) -> RowLayout;
        fn layout(items: &[Item]) -> Vec<Primitive>;
        fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize;
    }
}
fn sidebar_resize_width(initial:i32,amount:i32)->i32 {initial.saturating_add(amount).clamp(200,480)}
fn sidebar_grip_layout(width:i32,height:i32)->ffi::Rect{let width=width.max(0);ffi::Rect{x:(width-6).max(0),y:0,width:width.min(6),height:height.max(0)}}
fn sidebar_auto_hide(pinned:bool,blocked:bool,inside:bool,dragging:bool)->bool {!pinned&&!blocked&&!inside&&!dragging}
fn shell_layout(width:i32,height:i32,preferred:i32,pinned:bool)->ffi::ShellLayout {
    let width=width.max(0);let height=height.max(0);
    let sidebar_width=preferred.clamp(200,480).min(120.max(width/2));
    let column=if pinned{sidebar_width+8}else{0};
    ffi::ShellLayout {
        content:ffi::Rect{x:column+8,y:38,width:(width-column-16).max(0),height:(height-46).max(0)},
        edge:ffi::Rect{x:0,y:38,width:8,height:(height-38).max(0)},
        sidebar:ffi::Rect{x:8,y:38,width:sidebar_width,height:200.max(height-46)},
    }
}
#[derive(Default)]
struct SidebarDrag {anchor:Option<f64>,pointer:f64,initial:i32,width:i32,moved:bool}
fn new_sidebar_drag()->Box<SidebarDrag>{Box::new(SidebarDrag::default())}
impl SidebarDrag {
    fn begin(&mut self,width:i32,pointer:f64){self.initial=width.clamp(200,480);self.width=self.initial;self.pointer=pointer;self.anchor=pointer.is_finite().then_some(pointer);self.moved=false;}
    fn update(&mut self,pointer:f64,reverse:bool)->ffi::WidthChange {
        let Some(anchor)=self.anchor else{return ffi::WidthChange{width:self.width,changed:false};};
        if !pointer.is_finite(){return ffi::WidthChange{width:self.width,changed:false};}
        self.pointer=pointer;self.moved|=pointer!=anchor;
        let amount=((pointer-anchor)*if reverse{-1.0}else{1.0}) as i32;
        self.width=sidebar_resize_width(self.initial,amount);
        ffi::WidthChange{width:self.width,changed:self.moved}
    }
    fn delta(&mut self,delta:f64,reverse:bool)->ffi::WidthChange{self.update(self.pointer+delta,reverse)}
    fn end(&mut self)->ffi::WidthChange{let changed=self.anchor.is_some()&&self.moved;self.cancel();ffi::WidthChange{width:self.width,changed}}
    fn cancel(&mut self){self.anchor=None;self.moved=false;}
    fn active(&self)->bool{self.anchor.is_some()}
}

fn pane_bar_layout(width: i32, height: i32) -> ffi::PaneBarLayout {
    let width=width.max(0);let height=height.max(0);
    let close_width=32.min((width-4).max(0));let close_x=(width-4-close_width).max(0);
    let menu_width=32.min(close_x);let menu_x=close_x-menu_width;
    ffi::PaneBarLayout {
        title:ffi::Rect{x:12.min(width),y:0,width:(menu_x-12).max(0),height},
        menu:ffi::Rect{x:menu_x,y:(height-32).max(0)/2,width:menu_width,height:32.min(height)},
        close:ffi::Rect{x:close_x,y:(height-32).max(0)/2,width:close_width,height:32.min(height)},
    }
}
fn pane_host_layout(width: i32, height: i32, ratio: f64, vertical: bool, split: bool) -> ffi::PaneHostLayout {
    let width=width.max(0);let height=height.max(0);
    let empty=||ffi::Rect{x:0,y:0,width:0,height:0};
    if !split {return ffi::PaneHostLayout{first:ffi::Rect{x:0,y:0,width,height},second:empty(),first_bar:empty(),second_bar:empty(),divider:empty()};}
    let ratio=if ratio.is_finite(){ratio.clamp(0.0,1.0)}else{0.5};
    let extent=if vertical{height}else{width};let gap=8.min(extent);let available=extent-gap;
    let first=(f64::from(available)*ratio) as i32;let second=available-first;
    let (a,b,d)=if vertical {
        (ffi::Rect{x:0,y:0,width,height:first},ffi::Rect{x:0,y:first+gap,width,height:second},ffi::Rect{x:0,y:first,width,height:gap})
    } else {
        (ffi::Rect{x:0,y:0,width:first,height},ffi::Rect{x:first+gap,y:0,width:second,height},ffi::Rect{x:first,y:0,width:gap,height})
    };
    let bar=|r:&ffi::Rect|ffi::Rect{x:r.x,y:r.y,width:r.width,height:30.min(r.height)};
    let page=|r:&ffi::Rect|ffi::Rect{x:r.x,y:r.y+30.min(r.height),width:r.width,height:(r.height-30).max(0)};
    ffi::PaneHostLayout{first:page(&a),second:page(&b),first_bar:bar(&a),second_bar:bar(&b),divider:d}
}
#[derive(Default)]
struct SplitDrag { anchor: Option<f64>, initial: f64, pointer: f64, ratio: f64, moved: bool }
fn new_split_drag()->Box<SplitDrag>{Box::new(SplitDrag::default())}
impl SplitDrag {
    fn begin(&mut self, ratio:f64, pointer:f64) {
        self.initial=if ratio.is_finite(){ratio.clamp(0.2,0.8)}else{0.5};
        self.ratio=self.initial;self.pointer=pointer;self.anchor=pointer.is_finite().then_some(pointer);self.moved=false;
    }
    fn update(&mut self,pointer:f64,extent:i32,reverse:bool)->ffi::DragRatio {
        let Some(anchor)=self.anchor else{return ffi::DragRatio{ratio:self.ratio,changed:false};};
        if !pointer.is_finite(){return ffi::DragRatio{ratio:self.ratio,changed:false};}
        self.pointer=pointer;self.moved=true;
        self.ratio=(self.initial+(pointer-anchor)*if reverse{-1.0}else{1.0}/f64::from(extent.saturating_sub(8).max(1))).clamp(0.2,0.8);
        ffi::DragRatio{ratio:self.ratio,changed:true}
    }
    fn delta(&mut self,delta:f64,extent:i32,reverse:bool)->ffi::DragRatio {
        self.update(self.pointer+delta,extent,reverse)
    }
    fn end(&mut self)->ffi::DragRatio {
        let changed=self.anchor.is_some()&&self.moved;self.cancel();ffi::DragRatio{ratio:self.ratio,changed}
    }
    fn cancel(&mut self){self.anchor=None;self.moved=false;}
    fn active(&self)->bool{self.anchor.is_some()}
    fn moved(&self)->bool{self.moved}
}

// Scroll geometry and thumb drag state use content offsets, never View IDs.
// The native adapter delivers track-local coordinates and applies the result to
// Chromium's scroll service; capture, keyboard/menu and AX delivery remain native.
fn scroll_metrics(track: i32, viewport: i32, content: i32, offset: i32) -> ffi::ScrollMetrics {
    let track = track.max(0); let viewport = viewport.max(0); let content = content.max(0);
    let max_offset = content.saturating_sub(viewport).max(0);
    let offset = offset.clamp(0, max_offset);
    let length = if track == 0 || viewport == 0 || content == 0 {0} else {
        (f64::from(viewport) / f64::from(content) * f64::from(track)).round()
            .clamp(f64::from(20.min(track)), f64::from(track)) as i32
    };
    let position = if max_offset == 0 {0} else {
        (f64::from(offset) / f64::from(max_offset) * f64::from(track - length)).round() as i32
    };
    ffi::ScrollMetrics {position, length, offset, max_offset}
}
fn scroll_offset(track: i32, viewport: i32, content: i32, position: f32) -> i32 {
    let g = scroll_metrics(track, viewport, content, 0);
    let travel = track.max(0) - g.length;
    if !position.is_finite() || travel <= 0 {return 0;}
    (f64::from(position).clamp(0.0, f64::from(travel)) / f64::from(travel)
        * f64::from(g.max_offset)).round() as i32
}
#[derive(Default)]
struct ScrollThumb { grip: Option<f32>, initial_offset: i32 }
fn new_scroll_thumb() -> Box<ScrollThumb> {Box::new(ScrollThumb::default())}
impl ScrollThumb {
    fn begin(&mut self, track: i32, viewport: i32, content: i32, offset: i32, pointer: f32) {
        let g = scroll_metrics(track, viewport, content, offset);
        self.grip = pointer.is_finite().then_some(pointer - g.position as f32);
        self.initial_offset = g.offset;
    }
    fn drag(&self, track: i32, viewport: i32, content: i32, offset: i32,
            pointer: f32, cross: f32, thickness: i32, snap: bool) -> i32 {
        let current = scroll_metrics(track, viewport, content, offset).offset;
        let Some(grip) = self.grip else {return current;};
        if !pointer.is_finite() || !cross.is_finite() {return current;}
        if snap && (cross < -128.0 || cross > thickness.max(0) as f32 + 128.0) {
            return scroll_metrics(track, viewport, content, self.initial_offset).offset;
        }
        scroll_offset(track, viewport, content, pointer - grip)
    }
    fn end(&mut self) {self.grip = None;}
}

// The platform delivers scroll offsets. Rust owns normalization, parent-local
// coordinate projection and inherited clipping; no native View pointers enter.
#[derive(Clone, Copy)]
struct ViewportWorld { x: f64, y: f64, left: f64, top: f64, right: f64, bottom: f64, visible: bool }
#[derive(Default)]
struct Viewport { width: f32, height: f32, x: f32, y: f32, world: Vec<ViewportWorld> }
fn new_viewport() -> Box<Viewport> { Box::new(Viewport::default()) }
impl Viewport {
    fn project(&mut self, content_width: f32, content_height: f32, width: f32, height: f32,
               x: f32, y: f32, nodes: &[ffi::ViewportNode]) -> ffi::ViewportProjection {
        let positive = |v: f32| if v.is_finite() {v.max(0.0)} else {0.0};
        let offset = |v: f32, max: f32| if v.is_finite() {v.clamp(0.0, max)} else {0.0};
        self.width = positive(width); self.height = positive(height);
        self.x = offset(x, (positive(content_width) - self.width).max(0.0));
        self.y = offset(y, (positive(content_height) - self.height).max(0.0));
        // Keep intermediate ancestry sums in f64 so large or deep native
        // rectangles cannot overflow before viewport rejection.
        let mut world = std::mem::take(&mut self.world);
        world.clear(); world.reserve(nodes.len());
        let mut out = Vec::new();
        for (index, node) in nodes.iter().enumerate() {
            let b = &node.bounds;
            let valid = node.visible && [b.x, b.y, b.width, b.height].iter().all(|v| v.is_finite())
                && b.width > 0.0 && b.height > 0.0;
            let parent = if node.parent == usize::MAX {
                ViewportWorld {x: 0.0, y: 0.0, left: 0.0, top: 0.0,
                    right: f64::from(self.width), bottom: f64::from(self.height), visible: true}
            } else if node.parent < index {world[node.parent]} else {
                ViewportWorld {x: 0.0, y: 0.0, left: 0.0, top: 0.0, right: 0.0, bottom: 0.0, visible: false}
            };
            let x = parent.x + f64::from(b.x); let y = parent.y + f64::from(b.y);
            let px = x - f64::from(self.x); let py = y - f64::from(self.y);
            let left = px.max(parent.left); let top = py.max(parent.top);
            let right = (px + f64::from(b.width)).min(parent.right);
            let bottom = (py + f64::from(b.height)).min(parent.bottom);
            let visible = valid && parent.visible && right > left && bottom > top &&
                [px as f32, py as f32].iter().all(|v| v.is_finite());
            world.push(ViewportWorld {x, y, left, top, right, bottom, visible});
            if visible {
                out.push(ffi::ProjectedNode {index,
                    bounds: ffi::FloatRect {x: px as f32, y: py as f32, width: b.width, height: b.height},
                    clip: ffi::FloatRect {x: left as f32, y: top as f32,
                        width: (right - left) as f32, height: (bottom - top) as f32}});
            }
        }
        self.world = world;
        ffi::ViewportProjection {x: self.x, y: self.y, nodes: out}
    }
}

// UI identity and expansion state only. Browser URLs, titles, profiles and
// commands stay in the application. State survives reconstruction and rename
// because it is keyed by workspace and item IDs, never by native View pointers.
use std::collections::{BTreeMap, BTreeSet};
#[derive(Default)]
struct SidebarTree { collapsed: BTreeMap<String, BTreeSet<String>> }
fn new_sidebar_tree() -> Box<SidebarTree> { Box::new(SidebarTree::default()) }
impl SidebarTree {
    fn collapsed(&self, workspace: &str, id: &str) -> bool {
        self.collapsed.get(workspace).is_some_and(|ids| ids.contains(id))
    }
    fn set_collapsed(&mut self, workspace: &str, id: &str, value: bool) {
        if value {self.collapsed.entry(workspace.into()).or_default().insert(id.into());}
        else if let Some(ids) = self.collapsed.get_mut(workspace) {
            ids.remove(id);
            if ids.is_empty() {self.collapsed.remove(workspace);}
        }
    }
    fn toggle(&mut self, workspace: &str, id: &str) -> bool {
        let collapsed = !self.collapsed(workspace, id);
        self.set_collapsed(workspace, id, collapsed); collapsed
    }
    fn forget_workspace(&mut self, workspace: &str) {self.collapsed.remove(workspace);}
    fn tab_plan(&self, workspace: &str, nodes: &[ffi::TreeNode], tabs: &[ffi::TabNode],
                first: usize, second: usize, split: bool) -> ffi::TabPlan {
        let tree = self.tree_plan(workspace, nodes);
        let reachable: BTreeMap<&str, usize> = tree.iter().map(|e| (nodes[e.index].id.as_str(), e.index)).collect();
        let mut positions = BTreeMap::new(); let mut order = Vec::new();
        for (index, tab) in tabs.iter().enumerate() {
            if let std::collections::btree_map::Entry::Vacant(entry) = positions.entry(tab.index) {
                entry.insert(index); order.push(index);
            }
        }
        let referenced: BTreeSet<&str> = order.iter().map(|&i| tabs[i].pin.as_str()).filter(|s| !s.is_empty()).collect();
        let pins = tree.iter().map(|e| ffi::PinVisibility {
            index: e.index, visible: !referenced.contains(nodes[e.index].id.as_str())
        }).collect();
        let pair = if split && first != second {
            positions.get(&first).zip(positions.get(&second)).map(|(&a, &b)| (a, b))
        } else {None};
        let mut slots = Vec::with_capacity(order.len()); let mut emitted = false;
        for index in order {
            if let Some((a, b)) = pair {
                if index == a || index == b {
                    if !emitted {
                        let pin = [a, b].iter().find_map(|&i| reachable.get(tabs[i].pin.as_str()).copied()).unwrap_or(usize::MAX);
                        slots.push(ffi::TabSlot {first: a, second: b, pin, pair: true});
                        emitted = true;
                    }
                    continue;
                }
            }
            slots.push(ffi::TabSlot {first: index, second: usize::MAX, pin: usize::MAX, pair: false});
        }
        ffi::TabPlan {slots, pins}
    }
    fn tree_plan(&self, workspace: &str, nodes: &[ffi::TreeNode]) -> Vec<ffi::TreeEntry> {
        let mut children: BTreeMap<&str, Vec<usize>> = BTreeMap::new();
        let mut ids = BTreeSet::new();
        for (index, node) in nodes.iter().enumerate() {
            // First occurrence owns an ID. Orphans, cycles and children of a
            // leaf are unreachable; they never create native interaction nodes.
            if !node.id.is_empty() && ids.insert(node.id.as_str()) {
                children.entry(node.parent.as_str()).or_default().push(index);
            }
        }
        let mut pending = Vec::new();
        if let Some(roots) = children.get("") {
            for &index in roots.iter().rev() {pending.push((index, usize::MAX, 0, true));}
        }
        let mut seen = BTreeSet::new(); let mut out = Vec::with_capacity(nodes.len());
        while let Some((index, parent, depth, visible)) = pending.pop() {
            if depth > 64 || !seen.insert(index) {continue;}
            let node = &nodes[index];
            let collapsed = node.folder && self.collapsed(workspace, &node.id);
            out.push(ffi::TreeEntry {index, parent, depth, collapsed, visible});
            if node.folder {
                if let Some(descendants) = children.get(node.id.as_str()) {
                    for &child in descendants.iter().rev() {
                        pending.push((child, index, depth + 1, visible && !collapsed));
                    }
                }
            }
        }
        out
    }
}

// Semantic keys supplied by the event adapter: 1 Space, 2 Return, 0 other.
// Mac Space acts on key-down; Return remains available to the default action.
fn key_policy(key: u32, mac: bool) -> u32 {
    match key {1 => if mac {1} else {2}, 2 if !mac => 1, _ => 0}
}

// A row's gesture state is independent of its platform event adapter. Once the
// threshold is crossed, returning to the press point still completes a drag.
#[derive(Default)]
struct RowPointer { origin: Option<(i32, i32)>, dragging: bool, hovered: bool, key_armed: bool }
fn new_row_pointer() -> Box<RowPointer> { Box::new(RowPointer::default()) }
impl RowPointer {
    fn press(&mut self, x: i32, y: i32) {
        self.origin = Some((x, y)); self.dragging = false;
    }
    fn drag(&mut self, x: i32, y: i32, enabled: bool) {
        if let Some((sx, sy)) = self.origin {
            let dx = i128::from(x) - i128::from(sx);
            let dy = i128::from(y) - i128::from(sy);
            self.dragging |= enabled && dx * dx + dy * dy > 64;
        }
    }
    fn release(&mut self) -> bool {
        let result = self.origin.is_some() && self.dragging;
        self.cancel(); result
    }
    fn cancel(&mut self) {
        self.origin = None; self.dragging = false; self.hovered = false; self.key_armed = false;
    }
    fn pointer_event(&mut self, phase: ffi::PointerPhase, x: i32, y: i32,
                     inside: bool, enabled: bool, trigger: bool, drop: bool) -> ffi::RowInputResult {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        if !enabled { self.cancel(); return ffi::RowInputResult {visual: V::Disabled, action: A::None, handled: true}; }
        self.hovered = inside;
        let mut action = A::None;
        match phase {
            P::Press => { self.key_armed = false; if trigger && inside {self.press(x, y);} }
            P::Drag => self.drag(x, y, drop),
            P::Release => {
                let armed = self.origin.is_some();
                let dragged = self.release(); self.hovered = inside;
                action = if dragged && drop {A::Drop}
                    else if armed && inside && trigger {A::Activate}
                    else {A::Cancel};
            }
            P::Enter | P::Exit | P::Move => {self.key_armed = false;}
            _ => {self.cancel();}
        }
        let visual = if !inside {V::Normal}
            else if self.origin.is_some() && trigger {V::Pressed} else {V::Hovered};
        ffi::RowInputResult {visual, action, handled: true}
    }
    fn key_event(&mut self, pressed: bool, policy: u32, enabled: bool) -> ffi::RowInputResult {
        use ffi::{RowAction as A, RowVisual as V};
        if !enabled {self.cancel(); return ffi::RowInputResult {visual: V::Disabled, action: A::None, handled: false};}
        let mut action = A::None;
        let mut handled = false;
        if pressed && policy == 1 {self.cancel(); action = A::Activate; handled = true;}
        else if pressed && policy == 2 {self.key_armed = true; handled = true;}
        else if !pressed && policy == 2 && self.key_armed {self.cancel(); action = A::Activate; handled = true;}
        let visual = if self.key_armed {V::Pressed} else if self.hovered {V::Hovered} else {V::Normal};
        ffi::RowInputResult {visual, action, handled}
    }
}
// Shared by the native input/accessibility adapter and the GPU display list.
// Widths are logical pixels; CoreText still owns font shaping and measurement.
// Geometry for an ordered visible list. Hidden/collapsed children are omitted
// by the adapter; no gap is created for them. -1 width requests natural size.
fn column_layout(width: i32, spacing: i32, top: i32, left: i32,
                 bottom: i32, right: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    let positive = |v: i32| i64::from(v.max(0));
    let bounded = |v: i64| v.clamp(0, i64::from(i32::MAX)) as i32;
    let left = positive(left); let right = positive(right);
    let natural = children.iter().map(|c| positive(c.width) + positive(c.left) + positive(c.right))
        .max().unwrap_or(0).saturating_add(left).saturating_add(right);
    let width = if width < 0 {bounded(natural)} else {width};
    let mut y = positive(top);
    let mut rects = Vec::with_capacity(children.len());
    for (index, child) in children.iter().enumerate() {
        if index > 0 {y = y.saturating_add(positive(spacing));}
        y = y.saturating_add(positive(child.top));
        rects.push(ffi::Rect { x: bounded(left + positive(child.left)), y: bounded(y),
            width: bounded(i64::from(width) - left - right - positive(child.left) - positive(child.right)),
            height: bounded(positive(child.height).min(i64::from(i32::MAX) - i64::from(bounded(y)))) });
        y = y.saturating_add(positive(child.height)).saturating_add(positive(child.bottom));
    }
    ffi::ColumnLayout { width, height: bounded(y.saturating_add(positive(bottom))), children: rects }
}

// Equal flex tiles with a four-pixel frame and gap. Distribute the remainder
// deterministically so odd widths fill the frame without a cumulative gap.
// Negative extents request the compact pair's natural size.
fn split_layout(width: i32, height: i32, count: usize) -> ffi::ColumnLayout {
    let width = if width < 0 {200} else {width};
    let height = if height < 0 {46} else {height};
    let left = 4.min(width); let top = 4.min(height);
    let inner = (width - 8).max(0);
    let inner_height = (height - 8).max(0);
    let gaps = (count.saturating_sub(1) as u128 * 4).min(inner as u128) as i32;
    let available = inner - gaps;
    let mut children = Vec::with_capacity(count);
    let mut x = left;
    for index in 0..count {
        let before = (available as u128 * index as u128 / count as u128) as i32;
        let after = (available as u128 * (index + 1) as u128 / count as u128) as i32;
        let tile_width = after - before;
        children.push(ffi::Rect {x, y: top, width: tile_width, height: inner_height});
        x = x.saturating_add(tile_width).saturating_add(4).min(width);
    }
    ffi::ColumnLayout {width, height, children}
}

fn folder_indent(depth: i32) -> i32 { depth.clamp(0, 8) * 12 }

fn close_visible(active: bool, tile: bool, hovered: bool) -> bool {
    (active && !tile) || hovered
}

// Chrome action and list share one opaque Metal surface, leaving the native
// scrollbar gutter outside it. Adapter mirroring happens outside this policy.
fn action_frame(row: ffi::Rect, viewport: ffi::Rect) -> ffi::Rect {
    ffi::Rect {x: viewport.x, y: row.y, width: viewport.width.max(0), height: row.height.max(0)}
}

fn row_layout(width: i32, height: i32, title_width: i32,
              detail: bool, chip: bool, close: bool) -> ffi::RowLayout {
    let width = width.max(0); let height = height.max(0);
    let lead = if chip {52} else {36};
    let trail = if close {32} else {10};
    let available = (width - lead - trail).max(0);
    let title = if detail {title_width.max(0).min((i64::from(available) * 3 / 5) as i32)} else {available};
    ffi::RowLayout {
        title: ffi::Rect { x: lead, y: 0, width: title, height },
        detail: ffi::Rect { x: lead + title + 10, y: 0,
            width: if detail {(available - title - 10).max(0)} else {0}, height },
        close: ffi::Rect { x: (width - 30).max(0), y: (height - 24).max(0) / 2,
            width: if close {24.min(width)} else {0}, height: if close {24.min(height)} else {0} },
    }
}

fn footer_layout(width: i32, content_width: i32) -> ffi::ColumnLayout {
    let width=width.max(0); let middle=(width-66).max(0);
    let height=if content_width>middle {47} else {32};
    ffi::ColumnLayout {width,height,children:vec![
        ffi::Rect{x:0,y:0,width:32.min(width),height:32},
        ffi::Rect{x:32,y:0,width:1,height:1},
        ffi::Rect{x:33,y:0,width:middle,height},
        ffi::Rect{x:(width-33).max(0),y:0,width:1,height:1},
        ffi::Rect{x:(width-32).max(0),y:0,width:32.min(width),height:32},
    ]}
}
fn strip_layout(width: i32, height: i32, spacing: i32, children: &[ffi::ColumnChild]) -> ffi::ColumnLayout {
    let spacing=spacing.max(0);
    let natural=children.iter().fold(0i32,|sum,c|sum.saturating_add(c.width.max(0)))
        .saturating_add(spacing.saturating_mul(children.len().saturating_sub(1).min(i32::MAX as usize) as i32));
    let natural_height=children.iter().map(|c|c.height.max(0)).max().unwrap_or(0);
    let height=if height<0 {natural_height} else {height}; let mut x=0i32;
    let rects=children.iter().map(|c| {
        let r=ffi::Rect{x,y:(height-c.height.max(0)).max(0)/2,width:c.width.max(0),height:c.height.max(0)};
        x=x.saturating_add(r.width).saturating_add(spacing); r
    }).collect();
    ffi::ColumnLayout {width:if width<0 {natural} else {width},height,children:rects}
}

fn address_layout(width: i32, height: i32, host_width: i32) -> ffi::RowLayout {
    let width = width.max(0); let height = height.max(0);
    let available = (width - 46).max(0);
    let host = host_width.clamp(0, available);
    ffi::RowLayout {
        title: ffi::Rect {x: 34, y: 0, width: host, height},
        detail: ffi::Rect {x: 34 + host, y: 0, width: available - host, height},
        close: ffi::Rect {x: 0, y: 0, width: 0, height: 0},
    }
}


fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize {
    if count == 0 { return 0; }
    let current = current.min(count - 1);
    if backwards { if current == 0 { count - 1 } else { current - 1 } }
    else if current + 1 == count { 0 } else { current + 1 }
}
fn layout(items: &[ffi::Item]) -> Vec<ffi::Primitive> {
    let mut out = Vec::with_capacity(items.len() * 5);
    for i in items {
        if ![i.x, i.y, i.width, i.height].iter().all(|v| v.is_finite())
            || i.width <= 0.0 || i.height <= 0.0
            || ![i.clip.x, i.clip.y, i.clip.width, i.clip.height].iter().all(|v| v.is_finite())
            || i.clip.width <= 0.0 || i.clip.height <= 0.0 { continue; }
        let first_primitive = out.len();
        let mut emit = |x, y, w: f32, h: f32, radius, color, text: &str, size, bold| {
            if w > 0.0 && h > 0.0 {
                out.push(ffi::Primitive { x, y, width: w, height: h, radius,
                    color, stroke: 0.0, shape: 0, text: text.into(), font_size: size, bold, middle: false, center: false,
                    clip: ffi::FloatRect {x: i.clip.x, y: i.clip.y, width: i.clip.width, height: i.clip.height} });
            }
        };
        if i.kind == 11 {
            if i.active||i.hovered {emit(i.x+i.width-3.0,i.y+8.0,2.0,(i.height-16.0).max(0.0),1.0,i.ink,"",0.0,false);}continue;
        }
        if i.kind == 10 {
            emit(i.x,i.y,i.width,i.height,10.0,i.fill,"",0.0,false);continue;
        }
        if i.kind == 8 {
            let g=pane_bar_layout(i.width as i32,i.height as i32);
            if !i.title.is_empty() {emit(i.x+g.title.x as f32,i.y,(g.title.width as f32).min(i.title_width.max(0.0)),i.height,0.0,i.ink,&i.title,i.font_size,false);}continue;
        }
        if i.kind == 9 {
            let across=i.width>i.height;let extent=if across{i.width}else{i.height};let grip=40.0f32.min(extent/4.0);
            emit(i.x+(i.width-if across{grip}else{4.0})/2.0,i.y+(i.height-if across{4.0}else{grip})/2.0,
                if across{grip}else{4.0},if across{4.0}else{grip},2.0,i.ink,"",0.0,false);continue;
        }
        if i.kind == 7 {
            let cx=i.x+i.width/2.0; let cy=i.y+i.height/2.0;
            if i.glyph==1 {
                if i.monogram.is_empty() {
                    emit(cx-4.0,cy-4.0,8.0,8.0,4.0,i.ink,"",0.0,false);
                } else {
                    if i.fill>>24!=0 {emit(cx-13.0,cy-13.0,26.0,26.0,7.0,i.fill,"",0.0,false);}
                    emit(cx-13.0,cy-13.0,26.0,26.0,0.0,i.ink,&i.monogram,i.font_size,false);
                }
            } else {
                if i.fill>>24!=0 {emit(i.x,i.y,i.width,i.height,8.0,i.fill,"",0.0,false);}
                if i.glyph==0 {
                    emit(cx-8.0,cy-7.0,16.0,14.0,3.0,i.ink,"",0.0,false);
                    emit(cx-3.0,cy-7.0,1.0,14.0,0.0,i.ink,"",0.0,false);
                    if i.active {emit(cx-7.0,cy-6.0,4.0,12.0,0.0,(i.ink&0xffffff)|0x55000000,"",0.0,false);}
                } else if i.glyph==2 {
                    for (dy,knob) in [(-5.0,-3.0),(0.0,3.0),(5.0,-1.0)] {
                        emit(cx-7.0,cy+dy-0.5,14.0,1.0,0.5,i.ink,"",0.0,false);
                        emit(cx+knob-1.5,cy+dy-1.5,3.0,3.0,1.5,i.ink,"",0.0,false);
                    }
                } else if i.glyph==6 {
                    for dx in [-5.0,0.0,5.0] {emit(cx+dx-1.0,cy-1.0,3.0,3.0,1.5,i.ink,"",0.0,false);}
                } else {
                    emit(cx-8.0,cy-8.0,16.0,16.0,0.0,i.ink,"",0.0,false);
                }
            }
            if i.focused {emit(i.x+2.0,i.y+i.height-3.0,(i.width-4.0).max(0.0),2.0,1.0,i.ink,"",0.0,false);}
            for p in out[first_primitive..].iter_mut() {
                if !p.text.is_empty() {p.center=true;}
                if (3..=5).contains(&i.glyph) && p.width==16.0 && p.height==16.0 {p.shape=i.glyph-1;}
                if i.glyph==7 && p.width==16.0 && p.height==16.0 {p.shape=5;}
                if i.glyph==0 && p.width==16.0 && p.height==14.0 {p.stroke=1.0;}
                if i.glyph==1 && i.monogram.is_empty() && p.width==8.0 && p.height==8.0 && !i.active {p.stroke=1.0;}
            }
            continue;
        }
        if i.kind == 6 {
            emit(i.x + if i.tile {2.0} else {4.0}, i.y + if i.tile {4.0} else {2.0},
                (i.width-if i.tile {4.0} else {8.0}).max(0.0), (i.height-if i.tile {8.0} else {4.0}).max(0.0),
                3.5, i.fill, "", 0.0, false);
            continue;
        }
        if i.kind == 1 {
            emit(i.x, i.y, i.width, i.height, 0.0, i.ink, &i.title, i.font_size, false);
            continue;
        }
        if i.kind == 5 {
            emit(i.x + 8.0, i.y, (i.width - 16.0).max(0.0), i.height,
                0.0, i.ink, &i.title, i.font_size, true);
            continue;
        }
        if i.kind == 4 {
            // A filled rounded pill and inset outline. Ring and lock strokes
            // remain analytic GPU geometry; they need no icon/font dependency.
            emit(i.x, i.y, i.width, i.height, 10.0, i.fill, "", 0.0, false);
            let g = address_layout(i.width as i32, i.height as i32, i.title_width as i32);
            let cy = i.y + i.height / 2.0;
            let rings = [(i.x + 0.5, 1.0), (i.x + if i.active {14.0} else {12.0}, 1.5)];
            emit(i.x + 0.5, i.y + 0.5, i.width - 1.0, i.height - 1.0,
                9.5, (i.ink & 0x00ffffff) | 0x0d000000, "", 0.0, false);
            if i.active {
                emit(i.x + 14.0, cy - 7.0, 8.0, 10.0, 4.0, i.badge, "", 0.0, false);
                emit(i.x + 12.0, cy - 1.0, 12.0, 8.0, 1.5, i.badge, "", 0.0, false);
            } else {
                emit(i.x + 12.0, cy - 6.0, 10.0, 10.0, 5.0, i.badge, "", 0.0, false);
                for n in 0..4 {
                    emit(i.x + 20.0 + n as f32, cy + 2.0 + n as f32, 2.0, 2.0,
                        1.0, i.badge, "", 0.0, false);
                }
            }
            if i.focused {
                emit(i.x + 2.0, i.y + i.height - 3.0, (i.width - 4.0).max(0.0), 2.0,
                    1.0, i.ink, "", 0.0, false);
            }
            emit(i.x + g.title.x as f32, i.y, g.title.width as f32, i.height,
                0.0, i.ink, &i.title, i.font_size, true);
            if !i.detail.is_empty() {
                emit(i.x + g.detail.x as f32, i.y, g.detail.width as f32, i.height,
                    0.0, i.detail_ink, &i.detail, i.detail_font_size, false);
            }
            // No more emissions borrow out: attach analytic inset strokes.
            // Only the outline and icon ring are stroked; body/text stay filled.
            for p in out[first_primitive..].iter_mut() {
                if p.text.is_empty() {
                    if let Some((_, stroke)) = rings.iter().find(|(x, _)| *x == p.x) {
                        p.stroke = *stroke;
                    }
                }
            }
            continue;
        }
        if i.fill >> 24 != 0 {
            emit(i.x, i.y, i.width, i.height, if i.kind == 2 {12.0} else {8.0},
                 i.fill, "", 0.0, false);
        }
        if i.kind == 2 { continue; }
        if i.focused {
            // Inset focus indicator; event/focus ownership stays with the node.
            emit(i.x + 2.0, i.y + i.height - 3.0, (i.width - 4.0).max(0.0), 2.0,
                 1.0, i.ink, "", 0.0, false);
        }
        if i.kind == 3 {
            let g = row_layout(i.width as i32, i.height as i32, i.title_width as i32,
                !i.detail.is_empty(), false, false);
            let cy = i.y + i.height / 2.0;
            emit(i.x + 11.0, cy - 0.75, 14.0, 1.5, 0.75, i.badge, "", 0.0, false);
            emit(i.x + 17.25, cy - 7.0, 1.5, 14.0, 0.75, i.badge, "", 0.0, false);
            emit(i.x + g.title.x as f32, i.y, g.title.width as f32, i.height,
                0.0, i.ink, &i.title, i.font_size, false);
            if !i.detail.is_empty() && g.detail.width > 0 {
                emit(i.x + g.detail.x as f32, i.y, g.detail.width as f32, i.height,
                    0.0, i.detail_ink, &i.detail, i.detail_font_size, false);
                out.last_mut().unwrap().middle = true;
            }
            continue;
        }
        emit(i.x + 10.0, i.y + (i.height - 16.0) / 2.0, 16.0, 16.0,
             4.0, i.badge, "", 0.0, false);
        emit(i.x + 12.0, i.y + (i.height - 16.0) / 2.0, 14.0, 16.0,
             0.0, if i.badge >> 24 != 0 { 0xffffffff } else { i.ink },
             &i.monogram, 10.0, true);
        // Keep a close-button hit area reserved whenever it exists, just as the
        // native interaction node does (including half-width split cards).
        let geometry = row_layout(i.width as i32, i.height as i32, 0, false, false, i.close);
        emit(i.x + geometry.title.x as f32, i.y, geometry.title.width as f32, i.height,
             0.0, i.ink, &i.title, i.font_size, i.active);
        if i.close && i.close_visible {
            emit(i.x + geometry.close.x as f32 + 5.0, i.y,
                 (geometry.close.width - 10).max(0) as f32, i.height,
                 0.0, i.ink, "×", 18.0, false);
        }
    }
    out
}
#[cfg(test)]
mod tests {
    use super::*;
    fn row(width: f32) -> ffi::Item { ffi::Item {
        x:0.0, y:0.0, width, height:34.0, kind:0, glyph:0,
        title:"Zażółć 日本語 🦀".into(), monogram:"Z".into(), detail:String::new(),
        title_width:0.0, detail_ink:0, detail_font_size:12.0, ink:0xff101010,
        fill:0xffffffff, badge:0xff556677, active:true, hovered:false,
        tile:false, close:true, close_visible:true, focused:false, font_size:13.0,
        clip:ffi::FloatRect {x:0.0, y:0.0, width:width.max(0.0), height:34.0},
    }}
    fn child(width: i32, height: i32) -> ffi::ColumnChild {
        ffi::ColumnChild { width, height, top: 0, left: 0, bottom: 0, right: 0 }
    }
    #[test] fn column_measures_full_scroll_extent_and_stretches_rows() {
        let children: Vec<_> = (0..29).map(|_| child(200, 34)).collect();
        let natural = column_layout(-1, 2, 0, 0, 0, 0, &children);
        assert_eq!((natural.width, natural.height), (200, 1042));
        let actual = column_layout(300, 2, 0, 0, 0, 0, &children);
        assert_eq!(actual.children.last().unwrap().y, 1008);
        assert!(actual.children.iter().all(|r| r.width == 300 && r.height == 34));
        assert_eq!(actual.height, natural.height);
    }
    #[test] fn column_handles_folder_indents_empty_lists_and_size_limits() {
        let mut indented = child(200, 46); indented.left = 24;
        let result = column_layout(200, 2, 3, 8, 5, 6, &[indented, child(200, 34)]);
        assert_eq!((result.children[0].x, result.children[0].width), (32, 162));
        assert_eq!(result.children[1].y, 51);
        assert_eq!(result.height, 90);
        let empty = column_layout(-1, 20, 3, 8, 5, 6, &[]);
        assert_eq!((empty.width, empty.height), (14, 8));
        let huge = column_layout(10, i32::MAX, 0, 0, 0, 0,
            &[child(i32::MAX, i32::MAX), child(i32::MAX, i32::MAX)]);
        assert_eq!(huge.height, i32::MAX);
        assert_eq!(huge.children[1].y, i32::MAX);
        assert_eq!(huge.children[1].height, 0);
    }
    #[test] fn action_frame_aligns_with_viewport_without_changing_vertical_layout() {
        let r = action_frame(ffi::Rect {x: 4, y: 70, width: 240, height: 34},
            ffi::Rect {x: 4, y: 106, width: 225, height: 500});
        assert_eq!((r.x, r.y, r.width, r.height), (4, 70, 225, 34));
        let narrow = action_frame(r, ffi::Rect {x: 19, y: 106, width: -1, height: 0});
        assert_eq!((narrow.x, narrow.width, narrow.height), (19, 0, 34));
    }
    #[test] fn new_tab_action_keeps_title_hint_icon_and_clip() {
        let mut item = row(177.0); item.kind = 3; item.close = false;
        item.title = "Nowa karta".into(); item.title_width = 75.0;
        item.detail = "Cmd+Shift+T".into(); item.detail_font_size = 12.0;
        let ops = layout(&[item]);
        let title = ops.iter().find(|p| p.text == "Nowa karta").unwrap();
        let detail = ops.iter().find(|p| p.text == "Cmd+Shift+T").unwrap();
        assert_eq!(title.x, 36.0); assert_eq!(title.width, 75.0);
        assert!(title.x + title.width < detail.x);
        assert!(detail.x + detail.width <= 167.0);
        assert!(detail.middle && !title.middle); assert_eq!(detail.font_size, 12.0);
        assert!(ops.iter().any(|p| p.width == 14.0 && p.height == 1.5));
        assert!(!ops.iter().any(|p| p.text == "×" || p.text == "Z"));
        assert!(ops.iter().all(|p| p.clip.width == 177.0));
    }
    #[test] fn new_tab_action_handles_empty_hint_and_narrow_widths() {
        for width in [10.0, 30.0, 100.0, 480.0] {
            let mut item = row(width); item.kind = 3; item.close = false;
            item.title = "Nowa karta".into(); item.detail = String::new();
            let ops = layout(&[item]);
            assert!(!ops.iter().any(|p| p.middle));
            assert!(ops.iter().all(|p| p.width > 0.0 && p.height > 0.0));
        }
    }
    #[test] fn address_preserves_host_before_path_and_clamps_narrow_widths() {
        for width in [0, 30, 46, 200, 480] {
            let g = address_layout(width, 40, 85);
            assert_eq!(g.title.x, 34);
            assert_eq!(g.detail.x, g.title.x + g.title.width);
            assert_eq!(g.title.width + g.detail.width, (width - 46).max(0));
            assert_eq!(g.close.width, 0);
        }
    }
    #[test] fn address_keeps_secure_and_search_icons_and_tail_elision() {
        for secure in [false, true] {
            let mut item = row(240.0); item.kind = 4; item.active = secure;
            item.title = "example.org".into(); item.detail = "/日本語/path".into();
            item.title_width = 85.0; item.focused = true;
            let ops = layout(&[item]);
            let host = ops.iter().find(|p| p.text == "example.org").unwrap();
            let path = ops.iter().find(|p| p.text == "/日本語/path").unwrap();
            assert_eq!(host.x + host.width, path.x);
            assert!(host.bold && !path.bold && !path.middle);
            assert!(ops.iter().filter(|p| p.stroke > 0.0).count() == 2);
            assert_eq!(ops.iter().any(|p| p.width == 12.0 && p.height == 8.0), secure);
            assert!(ops.iter().all(|p| p.clip.width == 240.0));
            assert!(!ops.iter().any(|p| p.text == "×" || p.text == "Z"));
        }
    }
    #[test] fn workspace_heading_uses_inset_bold_unicode_without_badge() {
        let mut item = row(200.0); item.kind = 5; item.title = "🚀 Praca 日本語".into();
        let ops = layout(&[item]); assert_eq!(ops.len(), 1);
        assert_eq!((ops[0].x, ops[0].width), (8.0, 184.0));
        assert!(ops[0].bold); assert_eq!(ops[0].text, "🚀 Praca 日本語");
    }
    #[test] fn scroll_thumb_reaches_both_endpoints_and_handles_tiny_tracks() {
        let start = scroll_metrics(300, 300, 3000, -10);
        assert_eq!((start.position, start.length, start.offset, start.max_offset), (0, 30, 0, 2700));
        let end = scroll_metrics(300, 300, 3000, i32::MAX);
        assert_eq!((end.position, end.offset), (270, 2700));
        assert_eq!(scroll_offset(300, 300, 3000, 270.0), 2700);
        assert_eq!(scroll_offset(300, 300, 3000, 135.0), 1350);
        assert_eq!(scroll_metrics(10, 100, 10000, 9900).length, 10);
        assert_eq!(scroll_offset(10, 100, 10000, 1.0), 0);
        assert_eq!(scroll_metrics(0, 0, -1, 20).length, 0);
        assert_eq!(scroll_metrics(300, 400, 300, 50).length, 300);
        assert_eq!(scroll_offset(300, 300, 3000, f32::NAN), 0);
    }
    #[test] fn scroll_drag_keeps_grip_snaps_back_and_cancels_without_late_scroll() {
        let mut drag = new_scroll_thumb();
        drag.begin(300, 300, 3000, 1000, 105.0); // Thumb starts at 100, grip is 5.
        assert_eq!(drag.drag(300, 300, 3000, 1000, 275.0, 7.0, 15, true), 2700);
        assert_eq!(drag.drag(300, 300, 3000, 2700, 275.0, -129.0, 15, true), 1000);
        assert_eq!(drag.drag(300, 300, 3000, 1000, 275.0, -129.0, 15, false), 2700);
        assert_eq!(drag.drag(300, 300, 3000, 1000, f32::NAN, 0.0, 15, true), 1000);
        // Content can shrink during capture; initial snap offset is normalized.
        assert_eq!(drag.drag(300, 300, 500, 0, 100.0, 999.0, 15, true), 200);
        drag.end();
        assert_eq!(drag.drag(300, 300, 3000, 400, 275.0, 0.0, 15, true), 400);
    }
    #[test] fn scrollbar_display_is_an_inset_clipped_thumb_without_text_or_cards() {
        let mut item = row(15.0); item.kind = 6; item.height = 80.0;
        let ops = layout(&[item]); assert_eq!(ops.len(), 1);
        assert_eq!((ops[0].x, ops[0].y, ops[0].width, ops[0].height), (4.0, 2.0, 7.0, 76.0));
        assert!(ops[0].text.is_empty()); assert_eq!(ops[0].clip.width, 15.0);
    }
    #[test] fn footer_reserves_a_horizontal_scroll_gutter_only_for_overflow() {
        let normal=footer_layout(240,60); assert_eq!(normal.height,32);
        let overflow=footer_layout(200,300); assert_eq!(overflow.height,47);
        assert_eq!(overflow.children[2].width,134); assert_eq!(overflow.children[2].height,47);
        assert_eq!(overflow.children[4].x,168); assert_eq!(overflow.children[0].y,0);
        let dots=strip_layout(-1,32,0,&[child(24,28),child(30,28),child(24,28)]);
        assert_eq!((dots.width,dots.children[1].x,dots.children[1].y),(78,24,2));
    }
    #[test] fn footer_scene_keeps_selected_plain_and_centered_emoji_variants() {
        let mut item=row(30.0);item.kind=7;item.glyph=1;item.monogram="🚀".into();
        let ops=layout(&[item]);let text=ops.iter().find(|p|p.text=="🚀").unwrap();assert!(text.center);
        let mut plain=row(24.0);plain.kind=7;plain.glyph=1;plain.monogram.clear();plain.active=false;
        let ops=layout(&[plain]);assert_eq!(ops.len(),1);assert_eq!(ops[0].stroke,1.0);
        let mut horizontal=row(80.0);horizontal.kind=6;horizontal.tile=true;horizontal.height=15.0;
        let ops=layout(&[horizontal]);assert_eq!((ops[0].width,ops[0].height),(76.0,7.0));
    }
    #[test] fn navigation_strip_preserves_gaps_and_arrow_reload_shapes() {
        let g=strip_layout(-1,32,2,&[child(32,32),child(32,32),child(32,32),child(32,32)]);
        assert_eq!(g.width,134);assert_eq!(g.children[3].x,102);
        for glyph in 3..=5 {
            let mut item=row(32.0);item.kind=7;item.glyph=glyph;item.height=32.0;item.focused=true;
            let ops=layout(&[item]);let icon=ops.iter().find(|p|p.shape>0).unwrap();
            assert_eq!(icon.shape,glyph-1);assert_eq!((icon.x,icon.y,icon.width,icon.height),(8.0,8.0,16.0,16.0));
            assert!(ops.iter().all(|p|p.text.is_empty()));
        }
    }
    #[test] fn sidebar_shell_layout_matches_pinned_and_overlay_bounds() {
        let p=shell_layout(1000,800,248,true);assert_eq!((p.content.x,p.content.width,p.sidebar.width,sidebar_grip_layout(248,754).x),(264,728,248,242));
        let overlay=shell_layout(560,560,480,false);assert_eq!((overlay.content.x,overlay.content.width,overlay.sidebar.width),(8,544,280));
        assert_eq!(shell_layout(0,0,480,true).content.width,0);
        assert_eq!(sidebar_grip_layout(i32::MIN,0).width,0);
        let mut grip=row(6.0);grip.kind=11;grip.height=200.0;grip.hovered=false;grip.active=false;assert!(layout(&[grip]).is_empty());
        let mut grip=row(6.0);grip.kind=11;grip.height=200.0;grip.hovered=true;let ops=layout(&[grip]);assert_eq!((ops[0].x,ops[0].y,ops[0].width,ops[0].height),(3.0,8.0,2.0,184.0));
    }
    #[test] fn sidebar_drag_is_anchored_clamped_and_cancelled() {
        let mut d=new_sidebar_drag();d.begin(248,100.0);assert_eq!(d.update(140.0,false).width,288);
        assert_eq!(d.update(1000.0,false).width,480);assert_eq!(d.update(-1000.0,false).width,200);assert!(d.end().changed);
        assert!(!d.update(200.0,false).changed);d.begin(248,0.0);assert_eq!(d.delta(20.0,true).width,228);d.cancel();assert!(!d.end().changed);
        d.begin(248,0.0);assert!(!d.end().changed);assert_eq!(sidebar_resize_width(i32::MAX,i32::MAX),480);
    }
    #[test] fn sidebar_auto_hide_respects_all_guards() {
        assert!(sidebar_auto_hide(false,false,false,false));
        for (p,b,i,d) in [(true,false,false,false),(false,true,false,false),(false,false,true,false),(false,false,false,true)] {assert!(!sidebar_auto_hide(p,b,i,d));}
    }
    #[test] fn pane_layout_separates_real_page_rects_in_both_orientations() {
        let g=pane_host_layout(809,600,0.5,false,true);
        assert_eq!((g.first.width,g.second.x,g.second.width,g.divider.width),(400,408,401,8));
        assert_eq!((g.first.y,g.first.height),(30,570));
        let v=pane_host_layout(600,809,0.5,true,true);
        assert_eq!((v.first.height,v.second.y,v.second.height,v.divider.y),(370,438,371,400));
        let single=pane_host_layout(600,809,0.5,false,false);assert_eq!(single.first.height,809);assert_eq!(single.first_bar.height,0);
        for width in [0,4,54,120] {
            let b=pane_bar_layout(width,30);assert!(b.menu.x+b.menu.width<=b.close.x);assert!(b.close.x+b.close.width<=width);
            assert!(pane_host_layout(width,4,f64::NAN,true,true).second.height>=0);
        }
    }
    #[test] fn split_drag_clamps_commits_and_rejects_stale_or_cancelled_events() {
        let mut drag=new_split_drag();drag.begin(0.5,100.0);
        assert_eq!(drag.update(200.0,508,false).ratio,0.7);
        assert_eq!(drag.update(10000.0,508,false).ratio,0.8);assert!(drag.end().changed);
        assert!(!drag.update(300.0,508,false).changed);
        drag.begin(0.5,100.0);assert_eq!(drag.delta(100.0,508,true).ratio,0.3);drag.cancel();assert!(!drag.end().changed);
        drag.begin(f64::NAN,0.0);assert!(!drag.end().changed);
    }
    #[test] fn pane_title_uses_tight_text_bounds_and_split_icons_have_no_label_text() {
        let mut item=row(400.0);item.kind=8;item.title_width=70.0;
        let ops=layout(&[item]);assert_eq!((ops[0].x,ops[0].width),(12.0,70.0));
        let mut empty=row(400.0);empty.kind=8;empty.title.clear();empty.title_width=2.0;assert!(layout(&[empty]).is_empty());
        let mut more=row(32.0);more.kind=7;more.glyph=6;more.fill=0;more.focused=false;
        let ops=layout(&[more]);assert_eq!(ops.len(),3);assert!(ops.iter().all(|p|p.shape==0&&p.text.is_empty()));
        let mut close=row(32.0);close.kind=7;close.glyph=7;
        assert!(layout(&[close]).iter().any(|p|p.shape==5));
    }
    fn viewport_node(parent: usize, x: f32, y: f32, width: f32, height: f32, visible: bool) -> ffi::ViewportNode {
        ffi::ViewportNode {parent, bounds: ffi::FloatRect {x, y, width, height}, visible}
    }
    #[test] fn viewport_preserves_fractional_scroll_and_card_geometry() {
        let mut v = new_viewport();
        let nodes = [viewport_node(usize::MAX, 0.0, 0.0, 200.0, 1000.0, true),
            viewport_node(0, 12.0, 36.0, 180.0, 34.0, true),
            viewport_node(0, 0.0, 800.0, 200.0, 34.0, true)];
        let p = v.project(200.0, 1000.0, 200.0, 100.0, 0.0, 45.25, &nodes);
        assert_eq!(p.y, 45.25); assert_eq!(p.nodes.len(), 2);
        let row = &p.nodes[1];
        assert_eq!((row.bounds.x, row.bounds.y, row.bounds.width, row.bounds.height), (12.0, -9.25, 180.0, 34.0));
        assert_eq!((row.clip.x, row.clip.y, row.clip.width, row.clip.height), (12.0, 0.0, 180.0, 24.75));
    }
    #[test] fn viewport_inherits_visibility_and_all_parent_clips() {
        let mut v = new_viewport();
        let nodes = [viewport_node(usize::MAX, 0.0, 0.0, 200.0, 500.0, true),
            viewport_node(0, 24.0, 10.0, 100.0, 50.0, true),
            viewport_node(1, -12.0, 30.0, 150.0, 34.0, true),
            viewport_node(0, 0.0, 0.0, 200.0, 200.0, false),
            viewport_node(3, 0.0, 0.0, 100.0, 34.0, true)];
        let p = v.project(200.0, 500.0, 200.0, 100.0, 0.0, 0.0, &nodes);
        assert_eq!(p.nodes.len(), 3);
        let child = &p.nodes[2];
        assert_eq!((child.bounds.x, child.bounds.y), (12.0, 40.0));
        assert_eq!((child.clip.x, child.clip.y, child.clip.width, child.clip.height), (24.0, 40.0, 100.0, 20.0));
    }
    #[test] fn viewport_clamps_offsets_after_shrink_and_rejects_invalid_geometry() {
        let mut v = new_viewport();
        assert_eq!(v.project(200.0, 1000.0, 200.0, 100.0, -20.0, 9000.0, &[]).y, 900.0);
        let shrunk = v.project(200.0, 40.0, 200.0, 100.0, f32::NAN, 900.0, &[]);
        assert_eq!((shrunk.x, shrunk.y), (0.0, 0.0));
        let nodes = [viewport_node(usize::MAX, f32::NAN, 0.0, 200.0, 100.0, true),
            viewport_node(7, 0.0, 0.0, 200.0, 34.0, true),
            viewport_node(usize::MAX, 0.0, 0.0, -1.0, 34.0, true)];
        assert!(v.project(200.0, 100.0, 200.0, 100.0, f32::INFINITY, -1.0, &nodes).nodes.is_empty());
        assert!(v.project(f32::INFINITY, 100.0, 0.0, -1.0, 0.0, 0.0, &nodes).nodes.is_empty());
    }
    #[test] fn every_draw_primitive_keeps_its_item_clip() {
        let mut item = row(200.0); item.y = -9.25; item.clip.height = 24.75;
        let ops = layout(&[item]); assert!(!ops.is_empty());
        assert!(ops.iter().all(|op| op.clip.y == 0.0 && op.clip.height == 24.75));
    }
    fn node(id: &str, parent: &str, folder: bool) -> ffi::TreeNode {
        ffi::TreeNode {id: id.into(), parent: parent.into(), folder}
    }
    #[test] fn tree_orders_siblings_and_retains_hidden_descendants() {
        let mut tree = new_sidebar_tree();
        let nodes = [node("child", "root", true), node("leaf", "child", false),
            node("root", "", true), node("sibling", "root", false), node("other", "", false)];
        let plan = tree.tree_plan("work", &nodes);
        assert_eq!(plan.iter().map(|e| e.index).collect::<Vec<_>>(), [2, 0, 1, 3, 4]);
        assert_eq!(plan.iter().map(|e| e.depth).collect::<Vec<_>>(), [0, 1, 2, 1, 0]);
        assert_eq!(plan[0].parent, usize::MAX); assert_eq!(plan[1].parent, 2);
        assert!(plan.iter().all(|e| e.visible));
        assert!(tree.toggle("work", "child")); assert!(tree.toggle("work", "root"));
        let hidden = tree.tree_plan("work", &nodes);
        assert_eq!(hidden.len(), nodes.len());
        assert!(hidden[0].collapsed && hidden[1].collapsed);
        assert!(hidden[0].visible && !hidden[1].visible && !hidden[2].visible && !hidden[3].visible && hidden[4].visible);
        assert!(!tree.toggle("work", "root"));
        let expanded = tree.tree_plan("work", &nodes);
        assert!(expanded[1].visible && !expanded[2].visible && expanded[3].visible);
    }
    #[test] fn tree_state_survives_rebuilds_and_isolates_workspace_ids() {
        let mut tree = new_sidebar_tree(); tree.set_collapsed("a:b", "c", true);
        assert!(!tree.collapsed("a", "b:c"));
        let nodes = [node("c", "", true)];
        for _ in 0..3 {assert!(tree.tree_plan("a:b", &nodes)[0].collapsed);}
        assert!(!tree.tree_plan("other", &nodes)[0].collapsed);
        tree.forget_workspace("other"); assert!(tree.collapsed("a:b", "c"));
        tree.forget_workspace("a:b"); assert!(!tree.collapsed("a:b", "c"));
    }
    #[test] fn tree_rejects_unreachable_and_duplicate_nodes_and_bounds_depth() {
        let tree = new_sidebar_tree();
        let nodes = [node("", "", true), node("root", "", true), node("root", "root", true),
            node("orphan", "missing", false), node("cycle-a", "cycle-b", true),
            node("cycle-b", "cycle-a", true), node("leaf", "", false), node("bad-child", "leaf", false)];
        assert_eq!(tree.tree_plan("work", &nodes).iter().map(|e| e.index).collect::<Vec<_>>(), [1, 6]);
        let mut chain = Vec::new();
        for index in 0..70 {chain.push(node(&index.to_string(), if index == 0 {"".into()} else {(index - 1).to_string()}.as_str(), true));}
        let plan = tree.tree_plan("work", &chain);
        assert_eq!(plan.len(), 65); assert_eq!(plan.last().unwrap().depth, 64);
    }
    fn tab(index: usize, pin: &str) -> ffi::TabNode {ffi::TabNode {index, pin: pin.into()}}
    #[test] fn tab_plan_groups_once_in_pane_order_and_preserves_other_tabs() {
        let tree = new_sidebar_tree();
        let tabs = [tab(3, ""), tab(7, ""), tab(9, ""), tab(12, "")];
        let p = tree.tab_plan("work", &[], &tabs, 9, 3, true);
        assert_eq!(p.slots.len(), 3);
        assert!(p.slots[0].pair); assert_eq!((p.slots[0].first, p.slots[0].second), (2, 0));
        assert_eq!(p.slots[0].pin, usize::MAX);
        assert_eq!((p.slots[1].first, p.slots[2].first), (1, 3));
        assert!(!p.slots[1].pair && !p.slots[2].pair);
    }
    #[test] fn tab_plan_anchors_pinned_pairs_by_pane_order_even_in_collapsed_folders() {
        let mut tree = new_sidebar_tree(); tree.set_collapsed("work", "folder", true);
        let nodes = [node("folder", "", true), node("a", "folder", false), node("b", "", false)];
        let tabs = [tab(0, ""), tab(1, "a"), tab(2, "b")];
        let p = tree.tab_plan("work", &nodes, &tabs, 1, 2, true);
        assert_eq!(p.slots.len(), 2); assert_eq!(p.slots[1].pin, 1);
        assert!(p.pins[0].visible && !p.pins[1].visible && !p.pins[2].visible);
        let swapped = tree.tab_plan("work", &nodes, &tabs, 2, 1, true);
        assert_eq!((swapped.slots[1].first, swapped.slots[1].second, swapped.slots[1].pin), (2, 1, 2));
        let right_pinned = tree.tab_plan("work", &nodes, &tabs, 0, 1, true);
        assert_eq!(right_pinned.slots[0].pin, 1);
    }
    #[test] fn tab_plan_falls_back_for_invalid_pairs_and_unreachable_pins() {
        let tree = new_sidebar_tree();
        let nodes = [node("orphan", "missing", false), node("good", "", false)];
        let tabs = [tab(5, "orphan"), tab(8, "good"), tab(8, "duplicate")];
        let p = tree.tab_plan("work", &nodes, &tabs, 5, 8, true);
        assert_eq!(p.slots.len(), 1); assert_eq!(p.slots[0].pin, 1); assert_eq!(p.pins.len(), 1);
        for (first, second, split) in [(5, 8, false), (5, 5, true), (5, 100, true)] {
            let p = tree.tab_plan("work", &nodes, &tabs, first, second, split);
            assert_eq!(p.slots.len(), 2); assert!(p.slots.iter().all(|s| !s.pair));
            assert_eq!((p.slots[0].first, p.slots[1].first), (0, 1));
        }
        assert!(tree.tab_plan("work", &[], &[], 0, 1, true).slots.is_empty());
    }
    #[test] fn split_tiles_fill_odd_widths_and_respect_frame_and_order() {
        let natural = split_layout(-1, -1, 2);
        assert_eq!((natural.width, natural.height), (200, 46));
        for width in [200, 201, 247, 248, 479, 480, i32::MAX] {
            let r = split_layout(width, 46, 2);
            let a = &r.children[0]; let b = &r.children[1];
            assert_eq!((a.x, a.y, a.height), (4, 4, 38));
            assert_eq!(b.x, a.x + a.width + 4);
            assert_eq!(b.x + b.width, width - 4);
            assert!((a.width - b.width).abs() <= 1);
            assert_eq!((b.y, b.height), (4, 38));
        }
    }
    #[test] fn split_layout_handles_empty_single_and_tiny_containers() {
        assert!(split_layout(200, 46, 0).children.is_empty());
        let single = split_layout(201, 46, 1);
        assert_eq!((single.children[0].x, single.children[0].width), (4, 193));
        for width in 0..16 {
            for height in 0..10 {
                for count in 0..4 {
                    let r = split_layout(width, height, count);
                    assert_eq!(r.children.len(), count);
                    for tile in r.children {
                        assert!(tile.x >= 0 && tile.y >= 0);
                        assert!(tile.width >= 0 && tile.height >= 0);
                        assert!(tile.x + tile.width <= width);
                        assert!(tile.y + tile.height <= height);
                    }
                }
            }
        }
    }
    #[test] fn folder_indentation_is_bounded_even_for_invalid_depths() {
        assert_eq!(folder_indent(i32::MIN), 0);
        for depth in 0..=8 {assert_eq!(folder_indent(depth), depth * 12);}
        assert_eq!(folder_indent(9), 96); assert_eq!(folder_indent(i32::MAX), 96);
    }
    #[test] fn pointer_activation_requires_a_valid_press_and_cancels_outside() {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
        assert!(p.pointer_event(P::Press, 0, 0, true, true, true, false).visual == V::Pressed);
        assert!(p.pointer_event(P::Release, 0, 0, false, true, true, false).action == A::Cancel);
        p.pointer_event(P::Press, 0, 0, true, true, true, false);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Activate);
        p.pointer_event(P::Press, 0, 0, true, true, false, false);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, false, false).action == A::Cancel);
    }
    #[test] fn drag_and_disable_never_leak_clicks() {
        use ffi::{PointerPhase as P, RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        p.pointer_event(P::Press, 0, 0, true, true, true, true);
        p.pointer_event(P::Drag, 10, 0, false, true, true, true);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, true).action == A::Drop);
        p.pointer_event(P::Press, 0, 0, true, true, true, true);
        assert!(p.pointer_event(P::Move, 0, 0, true, false, false, false).visual == V::Disabled);
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
        p.pointer_event(P::Press, 0, 0, true, true, true, true); p.cancel();
        assert!(p.pointer_event(P::Release, 0, 0, true, true, true, false).action == A::Cancel);
    }
    #[test] fn semantic_keys_follow_platform_conventions() {
        assert_eq!(key_policy(1, true), 1); assert_eq!(key_policy(2, true), 0);
        assert_eq!(key_policy(0, true), 0); assert_eq!(key_policy(1, false), 2);
        assert_eq!(key_policy(2, false), 1); assert_eq!(key_policy(u32::MAX, false), 0);
    }
    #[test] fn platform_key_policies_and_focus_loss_are_respected() {
        use ffi::{RowAction as A, RowVisual as V};
        let mut p = new_row_pointer();
        assert!(!p.key_event(true, 0, true).handled);
        assert!(p.key_event(true, 1, true).action == A::Activate);
        assert!(p.key_event(false, 1, true).action == A::None);
        assert!(p.key_event(true, 2, true).visual == V::Pressed);
        assert!(p.key_event(false, 2, true).action == A::Activate);
        p.key_event(true, 2, true); p.cancel();
        assert!(!p.key_event(false, 2, true).handled);
        assert!(!p.key_event(true, 1, false).handled);
    }
    #[test] fn row_geometry_reserves_close_and_detail_without_overlap() {
        for width in [90, 200, 248, 480] {
            let g = row_layout(width, 38, 100, true, false, true);
            assert!(g.title.x + g.title.width <= g.detail.x);
            assert!(g.detail.x + g.detail.width <= g.close.x);
            assert_eq!(g.close.x + g.close.width, width - 6);
        }
        let chip = row_layout(248, 40, 100, false, true, false);
        assert_eq!(chip.title.x, 52);
        assert_eq!(chip.close.width, 0);
        for width in [i32::MIN, 0, 30, i32::MAX] {
            let g = row_layout(width, -1, i32::MAX, true, true, true);
            assert!(g.title.width >= 0 && g.detail.width >= 0 && g.close.width >= 0);
            assert_eq!(g.title.height, 0);
        }
    }
    #[test] fn close_policy_preserves_hover_across_selection_changes() {
        assert!(close_visible(true, false, false));
        assert!(!close_visible(false, false, false));
        assert!(!close_visible(true, true, false));
        for active in [false, true] {
            for tile in [false, true] { assert!(close_visible(active, tile, true)); }
        }
    }
    #[test] fn row_pointer_distinguishes_click_drag_and_capture_loss() {
        let mut p = new_row_pointer();
        p.drag(100, 100, true); assert!(!p.release());
        p.press(10, 10); p.drag(18, 10, true); assert!(!p.release());
        p.press(10, 10); p.drag(19, 10, false); assert!(!p.release());
        p.press(10, 10); p.drag(19, 10, true); p.drag(10, 10, true);
        assert!(p.release()); assert!(!p.release());
        p.press(i32::MIN, i32::MIN); p.drag(i32::MAX, i32::MAX, true);
        assert!(p.release());
        p.press(0, 0); p.drag(100, 0, true); p.cancel(); assert!(!p.release());
    }
    #[test] fn unicode_and_live_title_are_preserved() {
        let r = layout(&[row(240.0)]);
        assert!(r.iter().any(|p| p.text == "Zażółć 日本語 🦀"));
        assert!(r.iter().any(|p| p.text == "×"));
        assert_eq!(r.iter().find(|p| p.text.starts_with("Zażółć")).unwrap().width, 172.0);
    }
    #[test] fn narrow_and_invalid_rows_never_emit_negative_geometry() {
        assert!(layout(&[row(f32::NAN)]).is_empty());
        assert!(layout(&[row(-1.0)]).is_empty());
        assert!(layout(&[row(30.0)]).iter().all(|p| p.width > 0.0));
        assert!(!layout(&[row(30.0)]).iter().any(|p| p.text.starts_with("Zażółć")));
    }
    #[test] fn split_close_appears_on_hover() {
        let mut item = row(100.0); item.tile = true; item.close_visible = false;
        assert!(!layout(&[item]).iter().any(|p| p.text == "×"));
        let mut item = row(100.0); item.tile = true; item.hovered = true; item.close_visible = true;
        assert!(layout(&[item]).iter().any(|p| p.text == "×"));
    }
    #[test] fn keyboard_wraps_without_overflow_or_empty_underflow() {
        assert_eq!(cycle_tab(0, 0, true), 0);
        assert_eq!(cycle_tab(0, 3, true), 2);
        assert_eq!(cycle_tab(2, 3, false), 0);
        assert_eq!(cycle_tab(usize::MAX, 3, false), 0);
        assert_eq!(cycle_tab(0, 1, true), 0);
    }
}
