// A dependency-free UI scene. CXX owns all data crossing the boundary.
#[cxx::bridge(namespace = "zitter::ui_lab")]
mod ffi {
    struct Rect {
        x: f32,
        y: f32,
        width: f32,
        height: f32,
        color: u32,
    }
    extern "Rust" {
        fn scene(width: f32, height: f32, rows: u32, selected: u32) -> Vec<Rect>;
        fn hit_test(y: f32, rows: u32) -> i32;
    }
}

fn scene(width: f32, height: f32, rows: u32, selected: u32) -> Vec<ffi::Rect> {
    if !width.is_finite() || !height.is_finite() || width <= 0.0 || height <= 0.0 {
        return Vec::new();
    }
    let mut result = Vec::with_capacity(68);
    let mut push = |x, y, w, h, color| {
        result.push(ffi::Rect {
            x,
            y,
            width: w,
            height: h,
            color,
        });
    };
    push(0.0, 0.0, width, height, 0xff18202c);
    push(16.0, 20.0, (width - 32.0).max(0.0), 28.0, 0xff30405a);
    for row in 0..rows.min(200) {
        let y = 64.0 + row as f32 * 36.0;
        if y + 32.0 > height {
            break;
        }
        push(
            8.0,
            y,
            (width - 16.0).max(0.0),
            32.0,
            if row == selected {
                0xff405e92
            } else {
                0xff202c40
            },
        );
        push(20.0, y + 10.0, 12.0, 12.0, 0xff8da7d0);
        push(
            44.0,
            y + 12.0,
            (width - 76.0 - (row % 4) as f32 * 12.0).max(0.0),
            8.0,
            0xffb0bfd8,
        );
    }
    result
}

fn hit_test(y: f32, rows: u32) -> i32 {
    if !y.is_finite() || y < 64.0 {
        return -1;
    }
    let row = ((y - 64.0) / 36.0) as u32;
    if row >= rows.min(200) || (y - 64.0) % 36.0 >= 32.0 {
        -1
    } else {
        row as i32
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scene_culls_hidden_rows_and_rejects_invalid_sizes() {
        assert_eq!(scene(280.0, 136.0, 200, 0).len(), 8);
        assert!(scene(f32::NAN, 800.0, 25, 0).is_empty());
        assert_eq!(scene(280.0, 800.0, 25, 1)[5].color, 0xff405e92);
    }
    #[test]
    fn row_gaps_and_boundaries_are_not_clickable() {
        assert_eq!(hit_test(63.0, 25), -1);
        assert_eq!(hit_test(64.0, 25), 0);
        assert_eq!(hit_test(95.0, 25), 0);
        assert_eq!(hit_test(96.0, 25), -1);
        assert_eq!(hit_test(100.0, 25), 1);
        assert_eq!(hit_test(f32::INFINITY, 25), -1);
        assert_eq!(hit_test(100.0, 1), -1);
    }
}
