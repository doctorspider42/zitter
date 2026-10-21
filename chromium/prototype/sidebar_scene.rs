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
    struct Primitive {
        x: f32, y: f32, width: f32, height: f32,
        radius: f32, color: u32,
        text: String, font_size: f32, bold: bool,
    }
    extern "Rust" {
        fn layout(items: &[Item]) -> Vec<Primitive>;
        fn cycle_tab(current: usize, count: usize, backwards: bool) -> usize;
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
        let trailing = if i.close {32.0} else {10.0};
        emit(i.x + 36.0, i.y, (i.width - 36.0 - trailing).max(0.0), i.height,
             0.0, i.ink, &i.title, i.font_size, i.active);
        if i.close && i.close_visible {
            emit(i.x + i.width - 25.0, i.y, 14.0, i.height,
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
