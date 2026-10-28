// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
//! The Zitter design system for the Rust UI (docs/design-system/README.md).
//!
//! Three layers, all pure functions of their inputs:
//! - `design_tokens` (generated): every fixed value of tokens.json.
//! - [`WorkspaceColor`] and [`Palette`]: a workspace color picked freely by hue,
//!   saturation and tone resolves to one palette per scheme. The workspace color
//!   tints the whole window (void, base, hover, line) and paints the controls
//!   (accent); raised surfaces stay neutral. A dense sample of the whole space is
//!   contrast-checked by the unit tests below.
//! - [`recipe`]: component styles (sizes, radii, colors) built from a palette.
//!
//! Colors are ARGB `u32`, the same encoding the scene ABI already uses.

use crate::design_tokens::{color, Themed};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Scheme {
    /// Dark.
    Noc,
    /// Light.
    Papier,
}

impl Scheme {
    fn pick(self, value: Themed) -> u32 {
        match self {
            Scheme::Noc => value.dark,
            Scheme::Papier => value.light,
        }
    }
    fn is_dark(self) -> bool {
        self == Scheme::Noc
    }
}

// ---------------------------------------------------------------------------
// Color math: sRGB <-> OKLab/OKLCH, gamut clipping and WCAG contrast.

/// A color in OKLCH: lightness 0..1, chroma 0..~0.37, hue in degrees.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Oklch {
    pub l: f64,
    pub c: f64,
    pub h: f64,
}

fn to_linear(c: f64) -> f64 {
    if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) }
}
fn from_linear(c: f64) -> f64 {
    if c <= 0.003_130_8 { 12.92 * c } else { 1.055 * c.powf(1.0 / 2.4) - 0.055 }
}
fn channels(argb: u32) -> [f64; 3] {
    [16, 8, 0].map(|shift| to_linear(f64::from((argb >> shift) & 0xff) / 255.0))
}
fn pack(alpha: u32, rgb: [f64; 3]) -> u32 {
    let byte = |c: f64| (from_linear(c.clamp(0.0, 1.0)) * 255.0).round() as u32;
    (alpha << 24) | (byte(rgb[0]) << 16) | (byte(rgb[1]) << 8) | byte(rgb[2])
}

fn lab_from_linear([r, g, b]: [f64; 3]) -> [f64; 3] {
    let l = (0.412_221_470_8 * r + 0.536_332_536_3 * g + 0.051_445_992_9 * b).cbrt();
    let m = (0.211_903_498_2 * r + 0.680_699_545_1 * g + 0.107_396_956_6 * b).cbrt();
    let s = (0.088_302_461_9 * r + 0.281_718_837_6 * g + 0.629_978_700_5 * b).cbrt();
    [
        0.210_454_255_3 * l + 0.793_617_785_0 * m - 0.004_072_046_8 * s,
        1.977_998_495_1 * l - 2.428_592_205_0 * m + 0.450_593_709_9 * s,
        0.025_904_037_1 * l + 0.782_771_766_2 * m - 0.808_675_766_0 * s,
    ]
}
fn linear_from_lab([l, a, b]: [f64; 3]) -> [f64; 3] {
    let l_ = (l + 0.396_337_777_4 * a + 0.215_803_757_3 * b).powi(3);
    let m_ = (l - 0.105_561_345_8 * a - 0.063_854_172_8 * b).powi(3);
    let s_ = (l - 0.089_484_177_5 * a - 1.291_485_548_0 * b).powi(3);
    [
        4.076_741_662_1 * l_ - 3.307_711_591_3 * m_ + 0.230_969_929_2 * s_,
        -1.268_438_004_6 * l_ + 2.609_757_401_1 * m_ - 0.341_319_396_5 * s_,
        -0.004_196_086_3 * l_ - 0.703_418_614_7 * m_ + 1.707_614_701_0 * s_,
    ]
}
fn lab_of(c: Oklch) -> [f64; 3] {
    let h = c.h.to_radians();
    [c.l, c.c * h.cos(), c.c * h.sin()]
}
fn in_gamut(rgb: [f64; 3]) -> bool {
    rgb.iter().all(|&x| (-1e-4..=1.0 + 1e-4).contains(&x))
}

impl Oklch {
    pub fn new(l: f64, c: f64, h: f64) -> Self {
        Self { l: l.clamp(0.0, 1.0), c: c.max(0.0), h: h.rem_euclid(360.0) }
    }
    pub fn from_argb(argb: u32) -> Self {
        let [l, a, b] = lab_from_linear(channels(argb));
        Self { l, c: a.hypot(b), h: b.atan2(a).to_degrees().rem_euclid(360.0) }
    }
    /// Opaque sRGB; out-of-gamut colors keep lightness and hue and lose chroma.
    pub fn to_argb(self) -> u32 {
        let full = linear_from_lab(lab_of(self));
        if in_gamut(full) {
            return pack(0xff, full);
        }
        let (mut lo, mut hi) = (0.0, self.c);
        for _ in 0..24 {
            let mid = (lo + hi) / 2.0;
            if in_gamut(linear_from_lab(lab_of(Self { c: mid, ..self }))) { lo = mid } else { hi = mid }
        }
        pack(0xff, linear_from_lab(lab_of(Self { c: lo, ..self })))
    }
}

fn relative_luminance(argb: u32) -> f64 {
    let [r, g, b] = channels(argb);
    0.2126 * r + 0.7152 * g + 0.0722 * b
}

/// WCAG 2 contrast ratio of two opaque colors (1..21).
pub fn contrast(a: u32, b: u32) -> f64 {
    let (x, y) = (relative_luminance(a), relative_luminance(b));
    (x.max(y) + 0.05) / (x.min(y) + 0.05)
}

/// Moves `start` along lightness, away from the grounds, until it reaches `min`
/// contrast against every ground. Hue and chroma are kept (chroma only shrinks
/// where the gamut forces it).
fn with_contrast(start: Oklch, grounds: &[u32], min: f64, lighten: bool) -> u32 {
    let mut probe = start;
    for _ in 0..=200 {
        let argb = probe.to_argb();
        if grounds.iter().all(|&g| contrast(argb, g) >= min) {
            return argb;
        }
        if (lighten && probe.l >= 1.0) || (!lighten && probe.l <= 0.0) {
            return argb;
        }
        probe.l = (probe.l + if lighten { 0.005 } else { -0.005 }).clamp(0.0, 1.0);
    }
    probe.to_argb()
}

fn keep_contrast(argb: u32, grounds: &[u32], min: f64, lighten: bool) -> u32 {
    if grounds.iter().all(|&g| contrast(argb, g) >= min) {
        argb
    } else {
        with_contrast(Oklch::from_argb(argb), grounds, min, lighten)
    }
}

/// Interpolates two opaque colors in OKLab (no muddy midpoints between hues).
pub fn mix(a: u32, b: u32, t: f32) -> u32 {
    let t = f64::from(t.clamp(0.0, 1.0));
    let (x, y) = (lab_from_linear(channels(a)), lab_from_linear(channels(b)));
    let alpha = |c: u32| f64::from(c >> 24);
    let out_alpha = (alpha(a) + (alpha(b) - alpha(a)) * t).round() as u32;
    pack(out_alpha, linear_from_lab([0, 1, 2].map(|i| x[i] + (y[i] - x[i]) * t)))
}

// ---------------------------------------------------------------------------
// Workspace color: any hue, saturation and tone.

/// A workspace's color as the person picked it: OKLCH hue in degrees,
/// saturation and tone 0..1. Saturation drives how colored the window and the
/// controls are (0 = graphite); tone only moves the window lighter or darker
/// inside the scheme's safe band, so controls keep their contrast whatever the
/// person picks. Text that would still miss its floor is corrected by the palette.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WorkspaceColor {
    hue: f32,
    saturation: f32,
    tone: f32,
}

/// OKLCH chroma of the controls at full saturation: as vivid as the design
/// system's named colors.
const ACCENT_CHROMA: f64 = 0.14;
/// Stored colors encode the pick: tone 0..1 as OKLCH L 0.60..0.76 and saturation
/// 0..1 as chroma 0..0.10, which every hue holds inside sRGB there.
const CANONICAL_L: f64 = 0.60;
const CANONICAL_L_SPAN: f64 = 0.16;
const CANONICAL_C: f64 = 0.10;
/// Below this saturation a stored color has no reliable hue.
const GRAY: f32 = 0.02;

impl WorkspaceColor {
    pub const VIOLET: Self = Self { hue: 285.0, saturation: 1.0, tone: 0.5 };
    pub const CORAL: Self = Self { hue: 38.0, saturation: 1.0, tone: 0.5 };
    pub const SKY: Self = Self { hue: 234.0, saturation: 1.0, tone: 0.5 };
    pub const AMBER: Self = Self { hue: 79.0, saturation: 1.0, tone: 0.5 };
    pub const ROSE: Self = Self { hue: 355.0, saturation: 1.0, tone: 0.5 };
    pub const MINT: Self = Self { hue: 170.0, saturation: 1.0, tone: 0.5 };
    pub const GREEN: Self = Self { hue: 138.0, saturation: 1.0, tone: 0.5 };
    pub const GRAPHITE: Self = Self { hue: 285.0, saturation: 0.0, tone: 0.5 };
    /// The named colors the pickers offer, in picker order.
    pub const PRESETS: [(&'static str, Self); 8] = [
        ("violet", Self::VIOLET),
        ("coral", Self::CORAL),
        ("sky", Self::SKY),
        ("amber", Self::AMBER),
        ("rose", Self::ROSE),
        ("mint", Self::MINT),
        ("green", Self::GREEN),
        ("graphite", Self::GRAPHITE),
    ];

    /// Hue wraps around; saturation and tone clamp to 0..1; NaN becomes 0.
    pub fn new(hue_degrees: f32, saturation: f32, tone: f32) -> Self {
        let finite = |v: f32| if v.is_finite() { v } else { 0.0 };
        Self {
            hue: finite(hue_degrees).rem_euclid(360.0),
            saturation: finite(saturation).clamp(0.0, 1.0),
            tone: finite(tone).clamp(0.0, 1.0),
        }
    }

    /// The pick a stored `#RRGGBB` holds. Profiles keep any hex; [`Self::to_argb`]
    /// writes one that reads back to the same pick (to sRGB precision).
    pub fn from_argb(argb: u32) -> Self {
        let c = Oklch::from_argb(argb);
        let saturation = (c.c / CANONICAL_C) as f32;
        let hue = if saturation < GRAY { Self::GRAPHITE.hue } else { c.h as f32 };
        Self::new(hue, saturation, ((c.l - CANONICAL_L) / CANONICAL_L_SPAN) as f32)
    }

    /// The `#RRGGBB` a profile stores for this pick.
    pub fn to_argb(self) -> u32 {
        Oklch::new(
            CANONICAL_L + CANONICAL_L_SPAN * f64::from(self.tone),
            CANONICAL_C * f64::from(self.saturation),
            f64::from(self.hue),
        )
        .to_argb()
    }

    /// Two picks are the same color within what a stored hex can tell apart:
    /// two degrees of hue (any hue when both are gray) and 3% of each level.
    pub fn same(self, other: Self) -> bool {
        let gray = self.saturation < GRAY && other.saturation < GRAY;
        let dh = (self.hue - other.hue + 540.0).rem_euclid(360.0) - 180.0;
        (gray || dh.abs() < 2.0)
            && (self.saturation - other.saturation).abs() < 0.03
            && (self.tone - other.tone).abs() < 0.03
    }

    pub fn hue_degrees(self) -> f32 {
        self.hue
    }
    /// 0..1.
    pub fn saturation(self) -> f32 {
        self.saturation
    }
    /// 0..1.
    pub fn tone(self) -> f32 {
        self.tone
    }

    /// A dense sample of the whole space (every 10 degrees, 9 levels each way),
    /// for exhaustive tests.
    pub fn samples() -> impl Iterator<Item = Self> {
        (0..36).flat_map(|h| {
            (0..9).flat_map(move |s| (0..9).map(move |t| Self::new(h as f32 * 10.0, s as f32 / 8.0, t as f32 / 8.0)))
        })
    }
}

/// The workspace color wheel: a hue ring and a saturation x tone field, both
/// continuous. Geometry is in the control's own px.
pub mod wheel {
    use super::{Oklch, WorkspaceColor, GRAY};

    /// Ring thickness.
    pub const RING: f32 = 22.0;
    /// Space around the ring and the field so knobs and focus rings fit.
    pub const INSET: f32 = 8.0;
    /// Field knob diameter; its centre stays half of it inside the field.
    pub const PAD_KNOB: f32 = 24.0;
    /// Field corner radius.
    pub const PAD_RADIUS: f32 = 16.0;

    /// The ring's color at `degrees`: one lightness and chroma for every hue.
    pub fn ring_color(degrees: f32) -> u32 {
        Oklch::new(0.72, 0.14, f64::from(degrees)).to_argb()
    }
    /// The field's color at saturation and tone 0..1.
    pub fn pad_color(hue_degrees: f32, saturation: f32, tone: f32) -> u32 {
        Oklch::new(0.42 + 0.44 * f64::from(tone), 0.14 * f64::from(saturation), f64::from(hue_degrees)).to_argb()
    }

    /// Centre of the ring band's circle, from the control's top-left.
    pub fn ring_knob(size: f32, hue_degrees: f32) -> (f32, f32) {
        let mid = size / 2.0 - INSET - RING / 2.0;
        let a = (hue_degrees - 90.0).to_radians();
        (size / 2.0 + mid * a.cos(), size / 2.0 + mid * a.sin())
    }
    /// The hue a point selects on a `size` px ring control; `None` off the band
    /// (the centre disc picks nothing).
    pub fn ring_hit(x: f32, y: f32, size: f32) -> Option<f32> {
        let outer = size / 2.0 - INSET;
        let (dx, dy) = (x - size / 2.0, y - size / 2.0);
        let distance = dx.hypot(dy);
        if !distance.is_finite() || distance < outer - RING - 10.0 || distance > outer + INSET {
            return None;
        }
        Some((dy.atan2(dx).to_degrees() + 90.0).rem_euclid(360.0))
    }

    /// The field's knob travel: from `INSET + PAD_KNOB / 2` to the far edge.
    fn travel(extent: f32) -> (f32, f32) {
        let start = INSET + PAD_KNOB / 2.0;
        (start, (extent - 2.0 * start).max(1.0))
    }
    pub fn pad_knob(width: f32, height: f32, saturation: f32, tone: f32) -> (f32, f32) {
        let ((x0, w), (y0, h)) = (travel(width), travel(height));
        (x0 + saturation * w, y0 + (1.0 - tone) * h)
    }
    /// The saturation and tone a point selects in a `width` x `height` control.
    pub fn pad_hit(x: f32, y: f32, width: f32, height: f32) -> (f32, f32) {
        let ((x0, w), (y0, h)) = (travel(width), travel(height));
        let unit = |v: f32| if v.is_finite() { v.clamp(0.0, 1.0) } else { 0.0 };
        (unit((x - x0) / w), unit(1.0 - (y - y0) / h))
    }

    /// The color a ring or field pick stores: `None` keeps that axis. A hue picked
    /// on graphite brings back full saturation, so the choice is visible.
    pub fn target(current: WorkspaceColor, hue: Option<f32>, saturation: Option<f32>, tone: Option<f32>) -> WorkspaceColor {
        let s = saturation.unwrap_or(if hue.is_some() && current.saturation() < GRAY { 1.0 } else { current.saturation() });
        WorkspaceColor::new(hue.unwrap_or(current.hue_degrees()), s, tone.unwrap_or(current.tone()))
    }

    /// One keyboard step: axis 0 hue (5 degrees, wraps), 1 saturation, 2 tone (5%).
    pub fn step(current: WorkspaceColor, axis: u32, delta: i32) -> WorkspaceColor {
        let d = delta as f32;
        match axis {
            0 => target(current, Some(current.hue_degrees() + 5.0 * d), None, None),
            1 => target(current, None, Some(current.saturation() + 0.05 * d), None),
            2 => target(current, None, None, Some(current.tone() + 0.05 * d)),
            _ => current,
        }
    }

    /// What a screen reader says: the ring its hue, the field its two levels.
    pub fn description(c: WorkspaceColor, ring: bool) -> String {
        let pct = |v: f32| (v * 100.0).round() as i32;
        if ring {
            format!("Barwa {}°", c.hue_degrees().round() as i32 % 360)
        } else {
            format!("Nasycenie {}%, jasność {}%", pct(c.saturation()), pct(c.tone()))
        }
    }
}

/// The palette for a stored workspace color, memoized per scheme and stored
/// hex (each one runs the contrast search once per thread).
pub fn palette_for_stored(scheme: Scheme, argb: u32) -> Palette {
    use std::cell::RefCell;
    use std::collections::HashMap;
    thread_local! {
        static CACHE: RefCell<HashMap<(bool, u32), Palette>> = RefCell::new(HashMap::new());
    }
    let color = WorkspaceColor::from_argb(argb);
    CACHE.with(|cache| {
        *cache
            .borrow_mut()
            .entry((scheme.is_dark(), argb | 0xff00_0000))
            .or_insert_with(|| Palette::for_workspace(scheme, color))
    })
}

/// Window lightness and chroma per role and scheme. Tone shifts every role by
/// up to `TONE_SPAN` around the middle; chroma scales with saturation.
struct WindowRamp {
    void: (f64, f64),
    base: (f64, f64),
    hover: (f64, f64),
    line: (f64, f64),
    tone_span: f64,
}
const NOC_WINDOW: WindowRamp = WindowRamp {
    void: (0.175, 0.065),
    base: (0.225, 0.08),
    hover: (0.295, 0.09),
    line: (0.325, 0.07),
    tone_span: 0.04,
};
const PAPIER_WINDOW: WindowRamp = WindowRamp {
    void: (0.885, 0.05),
    base: (0.945, 0.032),
    hover: (0.90, 0.045),
    line: (0.84, 0.045),
    tone_span: 0.025,
};

// ---------------------------------------------------------------------------
// Palette.

/// Every color role a component may paint, resolved for one scheme and one
/// workspace color. See docs/design-system/README.md for what each role is for.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Palette {
    pub scheme: Scheme,
    pub bg_void: u32,
    pub bg_base: u32,
    pub bg_raised: u32,
    pub bg_hover: u32,
    pub line: u32,
    pub line_strong: u32,
    pub ink: u32,
    pub ink_muted: u32,
    pub ink_faint: u32,
    pub accent: u32,
    pub on_accent: u32,
    pub accent_soft: u32,
    pub accent_ink: u32,
    pub zing: u32,
    pub on_zing: u32,
    pub zing_ink: u32,
    pub focus: u32,
    pub ok: u32,
    pub warn: u32,
    pub danger: u32,
    pub scrim: u32,
}

impl Palette {
    /// The untinted window (no workspace yet), with the brand violet accent.
    pub fn neutral(scheme: Scheme) -> Self {
        let s = |t| scheme.pick(t);
        Self {
            scheme,
            bg_void: s(color::BG_VOID),
            bg_base: s(color::BG_BASE),
            bg_raised: s(color::BG_RAISED),
            bg_hover: s(color::BG_HOVER),
            line: s(color::LINE),
            line_strong: s(color::LINE_STRONG),
            ink: s(color::INK),
            ink_muted: s(color::INK_MUTED),
            ink_faint: s(color::INK_FAINT),
            accent: s(color::KET),
            on_accent: s(color::ON_KET),
            accent_soft: s(color::KET_SOFT),
            accent_ink: s(color::KET),
            zing: s(color::ZING),
            on_zing: s(color::ON_ZING),
            zing_ink: s(color::ZING_INK),
            focus: s(color::FOCUS),
            ok: s(color::OK),
            warn: s(color::WARN),
            danger: s(color::DANGER),
            scrim: s(color::SCRIM),
        }
    }

    /// The window and controls for a workspace color. Text and status colors
    /// that would fall below their contrast floor on the tinted window are moved
    /// along lightness until they meet it.
    pub fn for_workspace(scheme: Scheme, workspace: WorkspaceColor) -> Self {
        let dark = scheme.is_dark();
        let mut p = Self::neutral(scheme);
        let hue = workspace.hue_degrees() as f64;
        let saturation = workspace.saturation() as f64;
        let ramp = if dark { &NOC_WINDOW } else { &PAPIER_WINDOW };
        let shift = (workspace.tone() as f64 - 0.5) * 2.0 * ramp.tone_span;
        let tint = |(l, c): (f64, f64)| Oklch::new(l + shift, c * saturation, hue).to_argb();
        p.bg_void = tint(ramp.void);
        p.bg_base = tint(ramp.base);
        p.bg_hover = tint(ramp.hover);
        p.line = tint(ramp.line);

        let grounds = [p.bg_void, p.bg_base, p.bg_hover, p.bg_raised];
        let lighten = dark;
        p.ink = keep_contrast(p.ink, &grounds, 7.0, lighten);
        p.ink_muted = keep_contrast(p.ink_muted, &grounds, 4.5, lighten);
        p.line_strong = keep_contrast(p.line_strong, &[p.bg_base, p.bg_hover, p.bg_raised], 3.0, lighten);
        p.focus = keep_contrast(p.focus, &grounds, 3.0, lighten);
        for status in [&mut p.ok, &mut p.warn, &mut p.danger, &mut p.zing_ink] {
            *status = keep_contrast(*status, &[p.bg_base, p.bg_hover, p.bg_raised], 4.5, lighten);
        }

        // Controls: hue and saturation from the workspace, lightness fixed per
        // scheme so the fill always carries its on-color.
        let chroma = ACCENT_CHROMA * saturation;
        // Light scheme: graphite fills go dark; colored ones sit at 0.50.
        let fill_l = if dark { 0.76 } else { 0.32 + 0.18 * (saturation / 0.25).min(1.0) };
        p.accent = with_contrast(Oklch::new(fill_l, chroma, hue), &[p.on_accent], 4.5, !dark);
        p.accent_soft = Oklch::new(if dark { 0.30 } else { 0.93 }, chroma * 0.45, hue).to_argb();
        p.accent_ink = with_contrast(
            Oklch::new(if dark { 0.78 } else { 0.48 }, chroma, hue),
            &[p.bg_base, p.bg_hover, p.bg_raised, p.accent_soft],
            4.5,
            lighten,
        );
        p
    }

    /// The palette part-way through a workspace switch (`t` 0..1 over
    /// `duration::MOVE_MS`). Interpolated in OKLab.
    pub fn mix(&self, to: &Palette, t: f32) -> Palette {
        let m = |a: u32, b: u32| mix(a, b, t);
        Palette {
            scheme: if t < 0.5 { self.scheme } else { to.scheme },
            bg_void: m(self.bg_void, to.bg_void),
            bg_base: m(self.bg_base, to.bg_base),
            bg_raised: m(self.bg_raised, to.bg_raised),
            bg_hover: m(self.bg_hover, to.bg_hover),
            line: m(self.line, to.line),
            line_strong: m(self.line_strong, to.line_strong),
            ink: m(self.ink, to.ink),
            ink_muted: m(self.ink_muted, to.ink_muted),
            ink_faint: m(self.ink_faint, to.ink_faint),
            accent: m(self.accent, to.accent),
            on_accent: m(self.on_accent, to.on_accent),
            accent_soft: m(self.accent_soft, to.accent_soft),
            accent_ink: m(self.accent_ink, to.accent_ink),
            zing: m(self.zing, to.zing),
            on_zing: m(self.on_zing, to.on_zing),
            zing_ink: m(self.zing_ink, to.zing_ink),
            focus: m(self.focus, to.focus),
            ok: m(self.ok, to.ok),
            warn: m(self.warn, to.warn),
            danger: m(self.danger, to.danger),
            scrim: m(self.scrim, to.scrim),
        }
    }
}

// ---------------------------------------------------------------------------
// Motion.

pub mod motion {
    use crate::design_tokens::duration;

    /// Horizontal offset in px of the tremor at `elapsed_ms` (four steps of
    /// -1, +1, -1, 0 over `dur-tremor`); 0 once finished or with reduced motion.
    pub fn tremor_offset(elapsed_ms: u32, reduced_motion: bool) -> f32 {
        if reduced_motion || elapsed_ms >= duration::TREMOR_MS {
            return 0.0;
        }
        match elapsed_ms * 4 / duration::TREMOR_MS {
            0 => -1.0,
            1 => 1.0,
            2 => -1.0,
            _ => 0.0,
        }
    }

    /// Ease-out progress 0..1 of a transition of `duration_ms`.
    pub fn progress(elapsed_ms: u32, duration_ms: u32, reduced_motion: bool) -> f32 {
        if reduced_motion || duration_ms == 0 || elapsed_ms >= duration_ms {
            return 1.0;
        }
        let t = elapsed_ms as f32 / duration_ms as f32;
        1.0 - (1.0 - t).powi(3)
    }
}

// ---------------------------------------------------------------------------
// Component recipes.

pub mod recipe {
    //! Sizes, radii and colors of each component, from a [`Palette`]. Names and
    //! numbers follow docs/design-system/components/<Component>/README.md.
    use super::Palette;
    use crate::design_tokens::{radius, shadow, space, text, ShadowLayer, TypeStyle};

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Interaction {
        Rest,
        Hover,
        Pressed,
        Disabled,
    }

    /// A box shadow whose 1 px ring follows the window's (tinted) hairline.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Shadow {
        pub ring: u32,
        pub drop: ShadowLayer,
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Elevation {
        /// Page card, address pill, active tab.
        Card,
        /// Command Center, menus, toasts, dialogs.
        Float,
    }

    pub fn shadow(p: &Palette, elevation: Elevation) -> Shadow {
        let themed = match elevation {
            Elevation::Card => shadow::CARD,
            Elevation::Float => shadow::FLOAT,
        };
        let layers = if p.scheme == super::Scheme::Noc { themed.dark } else { themed.light };
        Shadow { ring: p.line, drop: layers[1] }
    }

    /// Keyboard focus: solid ring, 2 px wide, 2 px from the control.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct FocusRing {
        pub color: u32,
        pub width: f32,
        pub offset: f32,
    }
    pub fn focus_ring(p: &Palette) -> FocusRing {
        FocusRing { color: p.focus, width: 2.0, offset: space::S0_5 }
    }

    /// The browser window: tinted frame and sidebar, neutral page card.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct WindowStyle {
        pub frame: u32,
        pub sidebar: u32,
        pub page: u32,
        pub gap: f32,
        pub page_radius: f32,
        pub page_shadow: Shadow,
    }
    pub fn window(p: &Palette) -> WindowStyle {
        WindowStyle {
            frame: p.bg_void,
            sidebar: p.bg_base,
            page: p.bg_raised,
            gap: space::S2,
            page_radius: radius::MD,
            page_shadow: shadow(p, Elevation::Card),
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum ButtonKind {
        /// The view's main action, on the workspace accent.
        Primary,
        /// Creating something new; lime, at most one in the app at a time.
        Zing,
        /// Secondary action beside a primary.
        Outline,
        /// Cancel, toolbar actions.
        Quiet,
        /// Destructive text action.
        Danger,
        /// Destructive confirmation inside a dialog.
        DangerStrong,
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct ButtonStyle {
        pub height: f32,
        pub radius: f32,
        pub padding_x: f32,
        pub gap: f32,
        pub text: TypeStyle,
        pub fill: u32,
        pub ink: u32,
        /// 0 = no edge.
        pub edge: u32,
        pub edge_width: f32,
        /// 1 px down while pressed.
        pub offset_y: f32,
    }

    pub fn button(p: &Palette, kind: ButtonKind, state: Interaction) -> ButtonStyle {
        let mut b = ButtonStyle {
            height: 32.0,
            radius: radius::PILL,
            padding_x: space::S4,
            gap: space::S2,
            text: text::BODY_STRONG,
            fill: 0,
            ink: p.ink,
            edge: 0,
            edge_width: 0.0,
            offset_y: if state == Interaction::Pressed { 1.0 } else { 0.0 },
        };
        let hover = matches!(state, Interaction::Hover | Interaction::Pressed);
        match kind {
            ButtonKind::Primary => (b.fill, b.ink) = (p.accent, p.on_accent),
            ButtonKind::Zing => (b.fill, b.ink) = (p.zing, p.on_zing),
            ButtonKind::Outline => {
                (b.edge, b.edge_width) = (p.line_strong, 1.0);
                if hover { b.fill = p.bg_hover }
            }
            ButtonKind::Quiet | ButtonKind::Danger => {
                b.radius = radius::SM;
                b.padding_x = space::S3;
                if kind == ButtonKind::Danger { b.ink = p.danger }
                if hover { b.fill = p.bg_hover }
            }
            ButtonKind::DangerStrong => {
                (b.ink, b.edge, b.edge_width) = (p.danger, p.danger, 1.5);
                if hover { b.fill = p.bg_hover }
            }
        }
        if state == Interaction::Disabled {
            (b.fill, b.ink, b.edge, b.edge_width, b.offset_y) = (0, p.ink_faint, p.line, 1.0, 0.0);
        }
        b
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct IconButtonStyle {
        pub size: f32,
        pub icon: f32,
        pub stroke: f32,
        pub radius: f32,
        pub fill: u32,
        pub ink: u32,
    }
    /// 28 px square with a 16 px line icon; `on` = pressed toggle (panel shown).
    pub fn icon_button(p: &Palette, on: bool, state: Interaction) -> IconButtonStyle {
        let (fill, ink) = match (state, on) {
            (Interaction::Disabled, _) => (0, p.ink_faint),
            (_, true) => (p.accent_soft, p.accent_ink),
            (Interaction::Hover | Interaction::Pressed, false) => (p.bg_hover, p.ink),
            (Interaction::Rest, false) => (0, p.ink_muted),
        };
        IconButtonStyle { size: 28.0, icon: 16.0, stroke: 1.6, radius: radius::SM, fill, ink }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct TabStyle {
        pub height: f32,
        pub radius: f32,
        pub padding_start: f32,
        pub padding_end: f32,
        pub gap: f32,
        pub spacing: f32,
        pub favicon: f32,
        pub text: TypeStyle,
        pub fill: u32,
        pub ink: u32,
        /// Only the active tab lifts off the tinted sidebar.
        pub shadow: Option<Shadow>,
    }
    /// A sidebar tab row. The active one is a small neutral card.
    pub fn sidebar_tab(p: &Palette, active: bool, state: Interaction) -> TabStyle {
        let hover = matches!(state, Interaction::Hover | Interaction::Pressed);
        let (fill, ink, text, shadow) = if active {
            (p.bg_raised, p.ink, text::BODY_STRONG, Some(shadow(p, Elevation::Card)))
        } else if hover {
            (p.bg_hover, p.ink, text::BODY, None)
        } else {
            (0, p.ink_muted, text::BODY, None)
        };
        TabStyle {
            height: 34.0,
            radius: radius::SM,
            padding_start: space::S3,
            padding_end: space::S2,
            gap: space::S2,
            spacing: space::S1,
            favicon: 16.0,
            text,
            fill,
            ink,
            shadow,
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct AddressPillStyle {
        pub height: f32,
        pub radius: f32,
        pub padding_start: f32,
        pub padding_end: f32,
        pub text: TypeStyle,
        pub fill: u32,
        pub host_ink: u32,
        pub path_ink: u32,
        pub secure_ink: u32,
        pub caret: u32,
        /// Edit ring (color, width); width 0 while resting.
        pub ring: (u32, f32),
        pub shadow: Shadow,
    }
    pub fn address_pill(p: &Palette, editing: bool) -> AddressPillStyle {
        AddressPillStyle {
            height: 36.0,
            radius: radius::PILL,
            padding_start: space::S3,
            padding_end: space::S2,
            text: text::URL,
            fill: p.bg_raised,
            host_ink: p.ink,
            path_ink: if editing { p.ink } else { p.ink_muted },
            secure_ink: p.ok,
            caret: p.accent,
            ring: if editing { (p.accent, 2.0) } else { (0, 0.0) },
            shadow: shadow(p, Elevation::Card),
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct ToggleStyle {
        pub width: f32,
        pub height: f32,
        pub thumb: f32,
        pub inset: f32,
        pub track: u32,
        pub edge: u32,
        pub edge_width: f32,
        pub thumb_fill: u32,
    }
    pub fn toggle(p: &Palette, on: bool) -> ToggleStyle {
        let (track, edge, edge_width, thumb_fill) =
            if on { (p.accent, 0, 0.0, p.on_accent) } else { (0, p.line_strong, 1.5, p.ink_muted) };
        ToggleStyle { width: 34.0, height: 20.0, thumb: 12.0, inset: 4.0, track, edge, edge_width, thumb_fill }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct WorkspaceDotStyle {
        pub size: f32,
        pub fill: u32,
        /// Active: a ring of the dot's own color, `gap` away on the chip.
        pub ring: Option<(u32, f32)>,
        pub chip: Option<(u32, Shadow)>,
    }
    /// `own` is the palette of the dot's workspace, `window` the active one.
    pub fn workspace_dot(own: &Palette, window: &Palette, active: bool) -> WorkspaceDotStyle {
        if active {
            WorkspaceDotStyle {
                size: 10.0,
                fill: own.accent,
                ring: Some((own.accent, 3.0)),
                chip: Some((window.bg_raised, shadow(window, Elevation::Card))),
            }
        } else {
            WorkspaceDotStyle { size: 8.0, fill: own.accent, ring: None, chip: None }
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct RowStyle {
        pub height: f32,
        pub radius: f32,
        pub padding_x: f32,
        pub gap: f32,
        pub text: TypeStyle,
        pub fill: u32,
        pub ink: u32,
        pub icon: u32,
        pub detail: u32,
        pub detail_text: TypeStyle,
    }
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct OverlayStyle {
        pub width: f32,
        pub radius: f32,
        pub padding: f32,
        pub fill: u32,
        pub shadow: Shadow,
        pub divider: u32,
        pub group_label: TypeStyle,
        pub group_ink: u32,
    }

    /// Command Center (Cmd+K) panel, and one of its result rows.
    pub fn command_center(p: &Palette) -> OverlayStyle {
        OverlayStyle {
            width: 560.0,
            radius: radius::LG,
            padding: space::S4,
            fill: p.bg_raised,
            shadow: shadow(p, Elevation::Float),
            divider: p.line,
            group_label: text::EYEBROW,
            group_ink: p.ink_muted,
        }
    }
    pub fn command_row(p: &Palette, selected: bool) -> RowStyle {
        RowStyle {
            height: 36.0,
            radius: radius::SM,
            padding_x: space::S3,
            gap: space::S3,
            text: text::BODY,
            fill: if selected { p.accent_soft } else { 0 },
            ink: if selected { p.accent_ink } else { p.ink },
            icon: if selected { p.accent_ink } else { p.ink_muted },
            detail: p.ink_muted,
            detail_text: text::URL,
        }
    }

    /// Context menu panel, and one of its rows.
    pub fn menu(p: &Palette) -> OverlayStyle {
        OverlayStyle {
            width: 248.0,
            radius: radius::MD,
            padding: space::S1,
            fill: p.bg_raised,
            shadow: shadow(p, Elevation::Float),
            divider: p.line,
            group_label: text::EYEBROW,
            group_ink: p.ink_muted,
        }
    }
    pub fn menu_row(p: &Palette, highlighted: bool, destructive: bool) -> RowStyle {
        let ink = if destructive { p.danger } else { p.ink };
        RowStyle {
            height: 30.0,
            radius: radius::SM,
            padding_x: space::S2,
            gap: space::S2,
            text: text::BODY,
            fill: if highlighted { p.bg_hover } else { 0 },
            ink,
            icon: if destructive { p.danger } else { p.ink_muted },
            detail: p.ink_muted,
            detail_text: text::URL,
        }
    }

    pub fn toast(p: &Palette) -> OverlayStyle {
        OverlayStyle {
            width: 420.0,
            radius: radius::MD,
            padding: space::S4,
            fill: p.bg_raised,
            shadow: shadow(p, Elevation::Float),
            divider: p.line,
            group_label: text::LABEL,
            group_ink: p.ink,
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct DialogStyle {
        pub panel: OverlayStyle,
        pub scrim: u32,
        pub title: TypeStyle,
        pub title_ink: u32,
        pub body: TypeStyle,
        pub body_ink: u32,
        pub actions_gap: f32,
    }
    pub fn dialog(p: &Palette) -> DialogStyle {
        DialogStyle {
            panel: OverlayStyle {
                width: 400.0,
                radius: radius::LG,
                padding: space::S6,
                fill: p.bg_raised,
                shadow: shadow(p, Elevation::Float),
                divider: p.line,
                group_label: text::EYEBROW,
                group_ink: p.ink_muted,
            },
            scrim: p.scrim,
            title: text::TITLE,
            title_ink: p.ink,
            body: text::BODY,
            body_ink: p.ink_muted,
            actions_gap: space::S2,
        }
    }

    /// Download progress bar (3 px); paused downloads use `warn`.
    pub fn progress(p: &Palette, paused: bool) -> (u32, u32, f32) {
        (p.bg_hover, if paused { p.warn } else { p.accent }, 3.0)
    }
}

// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::recipe::*;
    use super::*;

    const SCHEMES: [Scheme; 2] = [Scheme::Noc, Scheme::Papier];

    fn assert_pair(what: &str, fg: u32, bg: u32, min: f64, at: &str) {
        let c = contrast(fg, bg);
        assert!(c >= min, "{what}: {fg:08x} on {bg:08x} is {c:.2}:1 < {min} ({at})");
    }

    #[test]
    fn oklch_round_trips_and_clips_to_gamut() {
        for argb in [0xff9a92ffu32, 0xffd7ff3f, 0xff14121b, 0xffffffff, 0xff000000, 0xff0a6fae] {
            assert_eq!(Oklch::from_argb(argb).to_argb(), argb);
        }
        let wild = Oklch::new(0.6, 0.4, 145.0).to_argb();
        let back = Oklch::from_argb(wild);
        assert!((back.l - 0.6).abs() < 0.01 && (back.h - 145.0).abs() < 2.0 && back.c < 0.4);
    }

    #[test]
    fn contrast_matches_wcag_reference_values() {
        assert!((contrast(0xffffffff, 0xff000000) - 21.0).abs() < 1e-9);
        assert!((contrast(0xff777777, 0xffffffff) - 4.48).abs() < 0.01);
    }

    #[test]
    fn picks_wrap_clamp_and_read_back_from_their_stored_hex() {
        let c = WorkspaceColor::new(-30.0, 1.4, f32::NAN);
        assert_eq!((c.hue_degrees(), c.saturation(), c.tone()), (330.0, 1.0, 0.0));
        for c in WorkspaceColor::samples() {
            let back = WorkspaceColor::from_argb(c.to_argb());
            assert!((back.saturation() - c.saturation()).abs() < 0.03, "{c:?} -> {back:?}");
            assert!((back.tone() - c.tone()).abs() < 0.03, "{c:?} -> {back:?}");
            // 8-bit sRGB blurs the hue of weak colors more than of vivid ones.
            if c.saturation() >= 0.25 {
                let dh = (back.hue_degrees() - c.hue_degrees() + 540.0).rem_euclid(360.0) - 180.0;
                assert!(dh.abs() < if c.saturation() >= 0.5 { 2.0 } else { 6.0 }, "{c:?} -> {back:?}");
            }
            assert!(back.same(WorkspaceColor::from_argb(back.to_argb())), "{c:?} is not stable");
            assert!(!c.same(WorkspaceColor::new(c.hue_degrees(), (c.saturation() + 0.1).min(1.0), c.tone())) || c.saturation() == 1.0);
        }
        // A gray has no hue: it reads back as graphite's.
        assert_eq!(WorkspaceColor::from_argb(0xff808080).hue_degrees(), WorkspaceColor::GRAPHITE.hue_degrees());
        // Older vivid hex colors read as full saturation of their hue.
        let violet = WorkspaceColor::from_argb(0xff9a92ff);
        assert!(violet.saturation() > 0.99 && (violet.hue_degrees() - 285.0).abs() < 2.0);
    }

    #[test]
    fn color_wheel_hits_and_steps_are_continuous() {
        use wheel::*;
        // 200 px ring control: top of the band is 0 degrees, right is 90, centre is off.
        assert_eq!(ring_hit(100.0, 19.0, 200.0), Some(0.0));
        assert!((ring_hit(181.0, 100.0, 200.0).unwrap() - 90.0).abs() < 1e-3);
        assert!((ring_hit(181.0, 60.0, 200.0).unwrap() - 63.72).abs() < 0.05);
        assert_eq!(ring_hit(100.0, 100.0, 200.0), None);
        assert_eq!(ring_hit(f32::NAN, 0.0, 200.0), None);
        let (kx, ky) = ring_knob(200.0, 90.0);
        assert!((kx - 181.0).abs() < 1e-3 && (ky - 100.0).abs() < 1e-3);
        // 180 px field control: corners are the extremes, halfway is 0.5, outside clamps.
        assert_eq!(pad_hit(20.0, 160.0, 180.0, 180.0), (0.0, 0.0));
        assert_eq!(pad_hit(160.0, 20.0, 180.0, 180.0), (1.0, 1.0));
        assert_eq!(pad_hit(90.0, 90.0, 180.0, 180.0), (0.5, 0.5));
        assert_eq!(pad_hit(-50.0, 500.0, 180.0, 180.0), (0.0, 0.0));
        assert_eq!(pad_knob(180.0, 180.0, 0.25, 1.0), (55.0, 20.0));
        let c = WorkspaceColor::new(100.0, 0.4, 0.6);
        assert_eq!(target(c, Some(200.0), None, None), WorkspaceColor::new(200.0, 0.4, 0.6));
        assert_eq!(target(c, None, Some(0.9), Some(0.1)), WorkspaceColor::new(100.0, 0.9, 0.1));
        assert_eq!(target(WorkspaceColor::GRAPHITE, Some(30.0), None, None), WorkspaceColor::new(30.0, 1.0, 0.5));
        assert_eq!(step(c, 0, -21).hue_degrees(), 355.0);
        assert!((step(c, 2, 20).tone() - 1.0).abs() < 1e-6);
        assert!((step(c, 1, -1).saturation() - 0.35).abs() < 1e-6);
        assert_eq!(step(c, 9, 1), c);
        assert_eq!(description(WorkspaceColor::SKY, true), "Barwa 234°");
        assert_eq!(description(c, false), "Nasycenie 40%, jasność 60%");
    }

    #[test]
    fn stored_palettes_are_memoized_per_stored_hex() {
        let a = palette_for_stored(Scheme::Noc, WorkspaceColor::SKY.to_argb());
        assert_eq!(a, Palette::for_workspace(Scheme::Noc, WorkspaceColor::from_argb(WorkspaceColor::SKY.to_argb())));
        assert_ne!(palette_for_stored(Scheme::Papier, WorkspaceColor::SKY.to_argb()), a);
    }

    /// A dense sample of every pick, both schemes: every text/ground pair the
    /// components paint meets its floor. 5832 palettes.
    #[test]
    fn every_workspace_color_keeps_every_contrast_floor() {
        for scheme in SCHEMES {
            for ws in WorkspaceColor::samples() {
                let p = Palette::for_workspace(scheme, ws);
                let at = format!("{scheme:?} {ws:?}");
                for ground in [p.bg_void, p.bg_base, p.bg_hover, p.bg_raised] {
                    assert_pair("ink", p.ink, ground, 7.0, &at);
                    assert_pair("ink_muted", p.ink_muted, ground, 4.5, &at);
                    assert_pair("focus", p.focus, ground, 3.0, &at);
                }
                for ground in [p.bg_base, p.bg_hover, p.bg_raised] {
                    assert_pair("line_strong", p.line_strong, ground, 3.0, &at);
                    assert_pair("accent_ink", p.accent_ink, ground, 4.5, &at);
                    assert_pair("ok", p.ok, ground, 4.5, &at);
                    assert_pair("warn", p.warn, ground, 4.5, &at);
                    assert_pair("danger", p.danger, ground, 4.5, &at);
                    assert_pair("zing_ink", p.zing_ink, ground, 4.5, &at);
                }
                assert_pair("on_accent", p.on_accent, p.accent, 4.5, &at);
                assert_pair("accent_ink on soft", p.accent_ink, p.accent_soft, 4.5, &at);
                assert_pair("on_zing", p.on_zing, p.zing, 4.5, &at);
                assert_pair("accent dot on sidebar", p.accent, p.bg_base, 3.0, &at);
            }
        }
    }

    #[test]
    fn neutral_palette_is_the_design_system_as_written() {
        for scheme in SCHEMES {
            let p = Palette::neutral(scheme);
            assert_pair("ink_muted", p.ink_muted, p.bg_base, 4.5, "neutral");
            assert_pair("on_ket", p.on_accent, p.accent, 4.5, "neutral");
        }
        assert_eq!(Palette::neutral(Scheme::Noc).bg_base, 0xff14121b);
        assert_eq!(Palette::neutral(Scheme::Papier).accent, 0xff4b40d9);
    }

    #[test]
    fn workspace_tints_the_window_but_not_raised_surfaces() {
        for scheme in SCHEMES {
            let neutral = Palette::neutral(scheme);
            let coral = Palette::for_workspace(scheme, WorkspaceColor::CORAL);
            let sky = Palette::for_workspace(scheme, WorkspaceColor::SKY);
            assert_ne!(coral.bg_base, sky.bg_base);
            assert_ne!(coral.bg_void, neutral.bg_void);
            assert_eq!(coral.bg_raised, neutral.bg_raised);
            assert_eq!(sky.bg_raised, neutral.bg_raised);
            assert_eq!(coral.zing, neutral.zing);
            let hue = Oklch::from_argb(coral.bg_base).h;
            assert!((hue - WorkspaceColor::CORAL.hue_degrees() as f64).abs() < 6.0, "{hue}");
        }
    }

    #[test]
    fn saturation_zero_is_graphite_and_tone_only_moves_the_window() {
        for scheme in SCHEMES {
            let gray = Palette::for_workspace(scheme, WorkspaceColor::GRAPHITE);
            assert!(Oklch::from_argb(gray.bg_base).c < 0.005);
            assert!(Oklch::from_argb(gray.accent).c < 0.005);
            let low = Palette::for_workspace(scheme, WorkspaceColor::new(285.0, 1.0, 0.0));
            let high = Palette::for_workspace(scheme, WorkspaceColor::new(285.0, 1.0, 1.0));
            assert!(Oklch::from_argb(high.bg_base).l > Oklch::from_argb(low.bg_base).l);
            assert_eq!(low.accent, high.accent);
            assert_eq!(low.on_accent, high.on_accent);
        }
    }

    #[test]
    fn switching_workspaces_interpolates_from_one_palette_to_the_other() {
        let a = Palette::for_workspace(Scheme::Noc, WorkspaceColor::VIOLET);
        let b = Palette::for_workspace(Scheme::Noc, WorkspaceColor::MINT);
        assert_eq!(a.mix(&b, 0.0), a);
        assert_eq!(a.mix(&b, 1.0), b);
        let mid = a.mix(&b, 0.5);
        assert_ne!(mid.bg_base, a.bg_base);
        assert_ne!(mid.bg_base, b.bg_base);
        assert_eq!(mix(0x00000000, 0xff000000, 0.5) >> 24, 0x80);
    }

    #[test]
    fn tremor_and_transitions_follow_the_tokens_and_reduced_motion() {
        use motion::*;
        let steps: Vec<f32> = [0, 45, 90, 135, 180].iter().map(|&t| tremor_offset(t, false)).collect();
        assert_eq!(steps, vec![-1.0, 1.0, -1.0, 0.0, 0.0]);
        assert_eq!(tremor_offset(10, true), 0.0);
        assert_eq!(progress(0, 220, false), 0.0);
        assert_eq!(progress(220, 220, false), 1.0);
        assert_eq!(progress(10, 220, true), 1.0);
        assert!(progress(110, 220, false) > 0.5);
    }

    #[test]
    fn recipes_use_the_documented_sizes_and_roles() {
        let p = Palette::for_workspace(Scheme::Papier, WorkspaceColor::SKY);
        let primary = button(&p, ButtonKind::Primary, Interaction::Rest);
        assert_eq!((primary.height, primary.radius, primary.fill, primary.ink), (32.0, 999.0, p.accent, p.on_accent));
        assert_eq!(button(&p, ButtonKind::Primary, Interaction::Pressed).offset_y, 1.0);
        let quiet = button(&p, ButtonKind::Quiet, Interaction::Hover);
        assert_eq!((quiet.radius, quiet.fill), (8.0, p.bg_hover));
        let off = button(&p, ButtonKind::Outline, Interaction::Disabled);
        assert_eq!((off.ink, off.edge, off.fill), (p.ink_faint, p.line, 0));
        assert_eq!(button(&p, ButtonKind::DangerStrong, Interaction::Rest).edge, p.danger);

        let active = sidebar_tab(&p, true, Interaction::Rest);
        assert_eq!((active.height, active.fill, active.ink), (34.0, p.bg_raised, p.ink));
        assert_eq!(active.shadow.unwrap().ring, p.line);
        let idle = sidebar_tab(&p, false, Interaction::Rest);
        assert_eq!((idle.fill, idle.ink, idle.shadow), (0, p.ink_muted, None));

        assert_eq!(address_pill(&p, true).ring, (p.accent, 2.0));
        assert_eq!(address_pill(&p, false).ring.1, 0.0);
        assert_eq!(toggle(&p, true).track, p.accent);
        assert_eq!(toggle(&p, false).edge, p.line_strong);
        assert_eq!(icon_button(&p, true, Interaction::Rest).ink, p.accent_ink);
        assert_eq!(command_row(&p, true).fill, p.accent_soft);
        assert_eq!(menu_row(&p, false, true).ink, p.danger);
        assert_eq!(command_center(&p).width, 560.0);
        assert_eq!(dialog(&p).panel.radius, 20.0);
        assert_eq!(window(&p).frame, p.bg_void);
        assert_eq!(focus_ring(&p), FocusRing { color: p.focus, width: 2.0, offset: 2.0 });
        let other = Palette::for_workspace(Scheme::Papier, WorkspaceColor::ROSE);
        let dot = workspace_dot(&other, &p, true);
        assert_eq!((dot.fill, dot.chip.unwrap().0), (other.accent, p.bg_raised));
    }
}
