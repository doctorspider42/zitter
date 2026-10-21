// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Geometry and presentation policy only; browser state and command ownership
// remain in WorkspaceShell. No URLs, profiles or WebContents cross this ABI.
#[cxx::bridge(namespace = "zitter::sidebar")]
mod ffi {
    struct Item {
        x: f32, y: f32, width: f32, height: f32,
        // 0: row, 1: section label, 2: split group background.
        kind: u32,
        title: String, monogram: String,
        ink: u32, fill: u32, badge: u32,
        active: bool, hovered: bool, tile: bool, close: bool, close_visible: bool, focused: bool,
        font_size: f32,
    }
    struct Rect { x: i32, y: i32, width: i32, height: i32 }
    struct RowLayout { title: Rect, detail: Rect, close: Rect }
    struct ColumnChild { width: i32, height: i32, top: i32, left: i32, bottom: i32, right: i32 }
    struct ColumnLayout { width: i32, height: i32, children: Vec<Rect> }
    struct Primitive {
        x: f32, y: f32, width: f32, height: f32,
        radius: f32, color: u32,
        text: String, font_size: f32, bold: bool,
    }
    extern "Rust" {
        type RowPointer;
        fn new_row_pointer() -> Box<RowPointer>;
        fn press(self: &mut RowPointer, x: i32, y: i32);
        fn drag(self: &mut RowPointer, x: i32, y: i32, enabled: bool);
        fn release(self: &mut RowPointer) -> bool;
        fn cancel(self: &mut RowPointer);
        fn column_layout(width: i32, spacing: i32, top: i32, left: i32,
                         bottom: i32, right: i32, children: &[ColumnChild]) -> ColumnLayout;
        fn close_visible(active: bool, tile: bool, hovered: bool) -> bool;
        fn row_layout(width: i32, height: i32, title_width: i32,
                      detail: bool, chip: bool, close: bool) -> RowLayout;
        fn layout(items: &[Item]) -> Vec<Primitive>;
        fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize;
    }
}
// A row's gesture state is independent of its platform event adapter. Once the
// threshold is crossed, returning to the press point still completes a drag.
#[derive(Default)]
struct RowPointer { origin: Option<(i32, i32)>, dragging: bool }
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
    fn cancel(&mut self) { self.origin = None; self.dragging = false; }
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

fn close_visible(active: bool, tile: bool, hovered: bool) -> bool {
    (active && !tile) || hovered
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
            || i.width <= 0.0 || i.height <= 0.0 { continue; }
        let mut emit = |x, y, w: f32, h: f32, radius, color, text: &str, size, bold| {
            if w > 0.0 && h > 0.0 {
                out.push(ffi::Primitive { x, y, width: w, height: h, radius,
                    color, text: text.into(), font_size: size, bold });
            }
        };
        if i.kind == 1 {
            emit(i.x, i.y, i.width, i.height, 0.0, i.ink, &i.title, i.font_size, false);
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
        x:0.0, y:0.0, width, height:34.0, kind:0,
        title:"Zażółć 日本語 🦀".into(), monogram:"Z".into(), ink:0xff101010,
        fill:0xffffffff, badge:0xff556677, active:true, hovered:false,
        tile:false, close:true, close_visible:true, focused:false, font_size:13.0,
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
