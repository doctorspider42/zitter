// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Time Machine: the timeline of each workspace's open tabs. Native code owns the
// file (JSON lines), the clock and the time zone; this model decides what to
// record, answers "what was open around t" and lays out the timeline view.
use std::collections::HashMap;

use crate::design_tokens::radius;
use crate::ffi;
use crate::i18n::{t, tf};

/// Changes closer than this replace the previous frame instead of adding one,
/// unless that frame was forced (a tab about to close, a restore point).
pub const COALESCE_MS: i64 = 30_000;
/// The playhead snaps to a change at most this far away when released.
pub const SNAP_MS: i64 = 12 * 60_000;
pub const DAY_MS: i64 = 86_400_000;
/// The axis shows one week at a time.
pub const WINDOW_MS: i64 = 7 * DAY_MS;
const HOUR_MS: i64 = 3_600_000;
const MAX_TABS: usize = 500;
const MAX_TEXT: usize = 32_768;

#[derive(Clone, Debug)]
struct Frame {
    t: i64,
    profile: String,
    active: i32,
    tabs: Vec<ffi::TimelineTab>,
    forced: bool,
}

impl Frame {
    fn same_layout(&self, other: &Frame) -> bool {
        self.active == other.active
            && self.tabs.len() == other.tabs.len()
            && self.tabs.iter().zip(&other.tabs).all(|(a, b)| a.url == b.url)
    }
    fn same_titles(&self, other: &Frame) -> bool {
        self.tabs.iter().zip(&other.tabs).all(|(a, b)| a.title == b.title)
    }
    fn bytes(&self) -> usize {
        64 + self.profile.len() + self.tabs.iter().map(|tab| 16 + tab.url.len() + tab.title.len()).sum::<usize>()
    }
    fn has(&self, url: &str) -> bool {
        self.tabs.iter().any(|tab| tab.url == url)
    }
}

/// A frame from native code: tabs without an http(s) URL (new tab pages, internal
/// pages) are not part of history, so they are dropped and `active` follows.
fn admit(frame: &ffi::TimelineFrame, forced: bool) -> Option<Frame> {
    if frame.workspace.is_empty() || frame.tabs.len() > MAX_TABS {
        return None;
    }
    let mut tabs = Vec::with_capacity(frame.tabs.len());
    let mut active = -1;
    for (i, tab) in frame.tabs.iter().enumerate() {
        let lower = tab.url.to_ascii_lowercase();
        if tab.url.len() > MAX_TEXT || !(lower.starts_with("https://") || lower.starts_with("http://") || tab.url == "zitter://rss/") {
            continue;
        }
        if i as i32 == frame.active {
            active = tabs.len() as i32;
        }
        let title: String = tab.title.chars().take(MAX_TEXT).collect();
        tabs.push(ffi::TimelineTab { url: tab.url.clone(), title });
    }
    Some(Frame { t: frame.t_ms, profile: frame.profile.clone(), active, tabs, forced })
}

#[derive(Default)]
pub struct Timeline {
    spaces: HashMap<String, Vec<Frame>>,
    marks: Vec<ffi::TimelineMark>,
}

pub fn new_timeline() -> Box<Timeline> {
    Box::default()
}

impl Timeline {
    /// Decides how a captured state enters the timeline. Native code appends a
    /// line for `Append`, a replacing line for `Replace` and nothing for `Skip`.
    pub fn timeline_record(&mut self, frame: &ffi::TimelineFrame, force: bool) -> ffi::TimelineRecord {
        let Some(next) = admit(frame, force) else { return ffi::TimelineRecord::Skip };
        let frames = self.spaces.entry(frame.workspace.clone()).or_default();
        let Some(last) = frames.last_mut() else {
            if next.tabs.is_empty() {
                return ffi::TimelineRecord::Skip;
            }
            frames.push(next);
            return ffi::TimelineRecord::Append;
        };
        if next.t < last.t {
            return ffi::TimelineRecord::Skip;
        }
        if last.same_layout(&next) {
            last.forced |= force;
            if last.same_titles(&next) {
                return ffi::TimelineRecord::Skip;
            }
            last.tabs = next.tabs;
            return ffi::TimelineRecord::Replace;
        }
        if !force && !last.forced && next.t - last.t < COALESCE_MS {
            *last = next;
            return ffi::TimelineRecord::Replace;
        }
        frames.push(next);
        ffi::TimelineRecord::Append
    }

    /// Replays one stored line; `replace` lines overwrite the workspace's last frame.
    pub fn timeline_load(&mut self, frame: &ffi::TimelineFrame, replace: bool) -> bool {
        let Some(next) = admit(frame, true) else { return false };
        let frames = self.spaces.entry(frame.workspace.clone()).or_default();
        let last_t = frames.last().map(|f| f.t);
        if last_t.is_some_and(|t| next.t < t) {
            return false;
        }
        if replace && last_t.is_some() {
            *frames.last_mut().unwrap() = next;
        } else {
            frames.push(next);
        }
        true
    }

    pub fn timeline_mark(&mut self, mark: &ffi::TimelineMark) -> bool {
        if mark.workspace.is_empty() || mark.name.is_empty() || mark.name.len() > 160 {
            return false;
        }
        let at = self.marks.partition_point(|m| m.t_ms <= mark.t_ms);
        self.marks.insert(at, mark.clone());
        true
    }

    /// Drops frames older than `cutoff_ms`, then the oldest frames until the
    /// timeline fits in `max_bytes`. Returns whether anything was dropped.
    pub fn timeline_prune(&mut self, cutoff_ms: i64, max_bytes: usize) -> bool {
        let before = self.timeline_stats();
        for frames in self.spaces.values_mut() {
            frames.retain(|f| f.t >= cutoff_ms);
        }
        self.marks.retain(|m| m.t_ms >= cutoff_ms);
        let mut bytes = self.bytes();
        let squeezed = bytes > max_bytes;
        while bytes > max_bytes {
            let Some(space) = self
                .spaces
                .iter()
                .filter(|(_, frames)| !frames.is_empty())
                .min_by_key(|(_, frames)| frames[0].t)
                .map(|(id, _)| id.clone())
            else {
                break;
            };
            let frames = self.spaces.get_mut(&space).unwrap();
            bytes -= frames.remove(0).bytes();
        }
        self.spaces.retain(|_, frames| !frames.is_empty());
        // Marks expire with retention; the size limit also drops marks older than
        // what is left of the frames.
        if squeezed {
            let oldest = self.spaces.values().map(|f| f[0].t).min().unwrap_or(i64::MAX);
            self.marks.retain(|m| m.t_ms >= oldest);
        }
        self.timeline_stats().frames != before.frames || self.marks.len() != before.marks
    }

    /// Forgets one profile's frames and marks in [from_ms, to_ms] (clearing history).
    pub fn timeline_forget(&mut self, profile: &str, from_ms: i64, to_ms: i64) -> bool {
        let before = self.timeline_stats();
        let mut spaces = Vec::new();
        for (space, frames) in self.spaces.iter_mut() {
            if frames.iter().any(|f| f.profile == profile) {
                spaces.push(space.clone());
            }
            frames.retain(|f| f.profile != profile || f.t < from_ms || f.t > to_ms);
        }
        self.marks.retain(|m| !spaces.contains(&m.workspace) || m.t_ms < from_ms || m.t_ms > to_ms);
        self.spaces.retain(|_, frames| !frames.is_empty());
        let after = self.timeline_stats();
        after.frames != before.frames || after.marks != before.marks
    }

    /// Forgets one workspace's frames and marks when its profile ID is fixed.
    pub fn timeline_forget_workspace(&mut self, workspace: &str, from_ms: i64, to_ms: i64) -> bool {
        let before = self.timeline_stats();
        if let Some(frames) = self.spaces.get_mut(workspace) {
            frames.retain(|f| f.t < from_ms || f.t > to_ms);
            if frames.is_empty() {
                self.spaces.remove(workspace);
            }
        }
        self.marks.retain(|m| m.workspace != workspace || m.t_ms < from_ms || m.t_ms > to_ms);
        let after = self.timeline_stats();
        after.frames != before.frames || after.marks != before.marks
    }

    pub fn timeline_clear(&mut self) {
        self.spaces.clear();
        self.marks.clear();
    }

    /// Every frame in time order, for rewriting the file after pruning.
    pub fn timeline_frames(&self) -> Vec<ffi::TimelineFrame> {
        let mut out: Vec<ffi::TimelineFrame> = self
            .spaces
            .iter()
            .flat_map(|(space, frames)| {
                frames.iter().map(move |f| ffi::TimelineFrame {
                    t_ms: f.t,
                    workspace: space.clone(),
                    profile: f.profile.clone(),
                    active: f.active,
                    tabs: f.tabs.clone(),
                })
            })
            .collect();
        out.sort_by(|a, b| a.t_ms.cmp(&b.t_ms).then_with(|| a.workspace.cmp(&b.workspace)));
        out
    }

    pub fn timeline_marks(&self) -> Vec<ffi::TimelineMark> {
        self.marks.clone()
    }

    pub fn timeline_stats(&self) -> ffi::TimelineStats {
        ffi::TimelineStats {
            frames: self.spaces.values().map(Vec::len).sum(),
            marks: self.marks.len(),
            bytes: self.bytes(),
        }
    }

    fn bytes(&self) -> usize {
        self.spaces.values().flatten().map(Frame::bytes).sum::<usize>()
            + self.marks.iter().map(|m| 64 + m.name.len()).sum::<usize>()
    }

    fn frames(&self, workspace: &str) -> &[Frame] {
        self.spaces.get(workspace).map_or(&[], Vec::as_slice)
    }

    pub fn timeline_bounds(&self, workspace: &str) -> ffi::TimelineBounds {
        let frames = self.frames(workspace);
        match (frames.first(), frames.last()) {
            (Some(first), Some(last)) => ffi::TimelineBounds { valid: true, first_ms: first.t, last_ms: last.t },
            _ => ffi::TimelineBounds { valid: false, first_ms: 0, last_ms: 0 },
        }
    }

    /// Every tab open at any moment of (t − quantum, t]: the state at the start
    /// of the quantum plus each change inside it, first-seen order, one per URL.
    pub fn timeline_query(&self, workspace: &str, t_ms: i64, quantum_ms: i64) -> ffi::TimelineMoment {
        let frames = self.frames(workspace);
        let from = t_ms - quantum_ms.max(0);
        let last = frames.partition_point(|f| f.t <= t_ms);
        let start = frames.partition_point(|f| f.t <= from).saturating_sub(1);
        let mut moment = ffi::TimelineMoment { tabs: vec![], active: -1, open_at_t: vec![], profile: String::new() };
        if last == 0 {
            return moment;
        }
        let at = &frames[last - 1];
        moment.profile = at.profile.clone();
        for frame in &frames[start.min(last - 1)..last] {
            for tab in &frame.tabs {
                if let Some(seen) = moment.tabs.iter_mut().find(|seen| seen.url == tab.url) {
                    if frame.t <= at.t {
                        seen.title = tab.title.clone();
                    }
                } else {
                    moment.tabs.push(tab.clone());
                }
            }
        }
        moment.open_at_t = moment.tabs.iter().map(|tab| at.has(&tab.url)).collect();
        if let Some(active) = usize::try_from(at.active).ok().and_then(|i| at.tabs.get(i)) {
            moment.active = moment.tabs.iter().position(|tab| tab.url == active.url).map_or(-1, |i| i as i32);
        }
        moment
    }

    /// The previous or next change of `workspace`, or `t_ms` when there is none.
    pub fn timeline_step(&self, workspace: &str, t_ms: i64, forward: bool) -> i64 {
        let frames = self.frames(workspace);
        if forward {
            frames.iter().map(|f| f.t).find(|&ft| ft > t_ms).unwrap_or(t_ms)
        } else {
            frames.iter().rev().map(|f| f.t).find(|&ft| ft < t_ms).unwrap_or(t_ms)
        }
    }

    /// The nearest change within SNAP_MS, or `t_ms` itself.
    pub fn timeline_snap(&self, workspace: &str, t_ms: i64) -> i64 {
        self.frames(workspace)
            .iter()
            .map(|f| f.t)
            .filter(|ft| (ft - t_ms).abs() <= SNAP_MS)
            .min_by_key(|ft| (ft - t_ms).abs())
            .unwrap_or(t_ms)
    }

    /// The most recently closed tab that is not open now (Cmd+Shift+T).
    pub fn timeline_last_closed(&self, workspace: &str, open: &[String]) -> ffi::TimelineClosed {
        let frames = self.frames(workspace);
        for pair in frames.windows(2).rev() {
            for tab in pair[0].tabs.iter().rev() {
                if !pair[1].has(&tab.url) && !open.iter().any(|url| *url == tab.url) {
                    return ffi::TimelineClosed { valid: true, t_ms: pair[1].t, tab: tab.clone() };
                }
            }
        }
        ffi::TimelineClosed { valid: false, t_ms: 0, tab: ffi::TimelineTab { url: String::new(), title: String::new() } }
    }

    fn closed_times(&self, workspace: &str) -> Vec<i64> {
        self.frames(workspace)
            .windows(2)
            .filter(|pair| pair[0].tabs.iter().any(|tab| !pair[1].has(&tab.url)))
            .map(|pair| pair[1].t)
            .collect()
    }

    /// The timeline panel (design: Time Machine, the old axis): day labels and
    /// separators, hourly activity bars, the quantum, closed-tab dots, snapshot
    /// diamonds and the playhead with its zing pill.
    pub fn timeline_scene(&self, workspace: &str, input: &ffi::TimelineSceneInput, p: &ffi::DesignPalette) -> Vec<ffi::Primitive> {
        let (w, h) = (input.width, input.height);
        let mut ops = Vec::new();
        if !(w > 80.0 && h > 60.0) || input.end_ms <= input.start_ms {
            return ops;
        }
        let clip = ffi::FloatRect { x: 0.0, y: 0.0, width: w, height: h };
        let rect = |x: f32, y: f32, width: f32, height: f32, radius: f32, color: u32, stroke: f32| ffi::Primitive {
            pixels: vec![],
            x, y, width, height, radius, color, stroke, shape: 0, text: String::new(), font_size: 0.0, font_weight: 0,
            bold: false, middle: false, center: false, right_align: false, clip, editor: false, utf16: vec![],
            text_x: 0.0, baseline: 0.0, text_direction: 0, family: 0, italic: false, tracking: 0.0,
        };
        let label = |x: f32, y: f32, width: f32, text: String, color: u32, weight: i32, center: bool| ffi::Primitive {
            text, font_size: 11.0, font_weight: weight, center, family: 3, ..rect(x, y, width, 16.0, 0.0, color, 0.0)
        };
        let radius = radius::MD;
        ops.push(rect(0.0, 0.0, w, h, radius, p.bg_base, 0.0));
        ops.push(rect(0.0, 0.0, w, h, radius, p.line, 1.0));
        let (x0, x1, top) = (20.0_f32, w - 20.0, 14.0_f32);
        let inner = h - 26.0;
        let span = (input.end_ms - input.start_ms) as f64;
        let x = |ms: i64| x0 + ((ms - input.start_ms) as f64 / span) as f32 * (x1 - x0);
        let local = |ms: i64| ms + i64::from(input.tz_offset_min) * 60_000;
        let t_day = local(input.t_ms).div_euclid(DAY_MS);
        let now_day = local(input.now_ms).div_euclid(DAY_MS);

        // Quantum, under everything else.
        let from = (input.t_ms - input.quantum_ms).max(input.start_ms);
        if input.quantum_ms > 0 && input.t_ms > from {
            ops.push(rect(x(from), top + 20.0, (x(input.t_ms) - x(from)).max(2.0), 64.0, 4.0, alpha(p.accent, 0x24), 0.0));
        }
        // Hourly activity: how many changes each hour holds.
        let frames = self.frames(workspace);
        let first_hour = local(input.start_ms).div_euclid(HOUR_MS);
        let last_hour = local(input.end_ms - 1).div_euclid(HOUR_MS);
        let mut counts = vec![0usize; (last_hour - first_hour + 1).max(0) as usize];
        for f in frames.iter().filter(|f| f.t >= input.start_ms && f.t < input.end_ms) {
            counts[(local(f.t).div_euclid(HOUR_MS) - first_hour) as usize] += 1;
        }
        let peak = counts.iter().copied().max().unwrap_or(0).max(1) as f32;
        let t_hour = local(input.t_ms).div_euclid(HOUR_MS);
        for (i, &count) in counts.iter().enumerate() {
            if count == 0 {
                continue;
            }
            let hour = first_hour + i as i64;
            let start = (hour * HOUR_MS - i64::from(input.tz_offset_min) * 60_000).max(input.start_ms);
            let end = ((hour + 1) * HOUR_MS - i64::from(input.tz_offset_min) * 60_000).min(input.end_ms);
            let height = 56.0 * (0.12 + 0.88 * (count as f32 / peak).sqrt());
            let color = if hour == t_hour { p.ink } else if hour < t_hour { p.accent } else { alpha(p.accent, 0x4D) };
            let left = x(start);
            ops.push(rect(left, top + 80.0 - height, (x(end) - left - 1.0).max(1.0), height, 2.0, color, 0.0));
        }
        // Days: a separator at each local midnight and a label after it. Zoomed out,
        // only every n-th day is named so labels never collide.
        let day_px = (DAY_MS as f64 / span) as f32 * (x1 - x0);
        let every = (56.0 / day_px).ceil().max(1.0) as i64;
        let first_day = local(input.start_ms).div_euclid(DAY_MS);
        let mut labels = Vec::new();
        for day in first_day..=local(input.end_ms - 1).div_euclid(DAY_MS) {
            let midnight = day * DAY_MS - i64::from(input.tz_offset_min) * 60_000;
            let lx = x(midnight.max(input.start_ms));
            if midnight > input.start_ms {
                ops.push(rect(lx, top, 1.0, inner, 0.0, p.line, 0.0));
            }
            // A day cut by the window's start keeps its label only if it fits before the next one.
            let next = x(((day + 1) * DAY_MS - i64::from(input.tz_offset_min) * 60_000).min(input.end_ms));
            if lx + 46.0 > x1 || (midnight < input.start_ms && next - lx < 56.0) || (day.rem_euclid(every) != 0 && day != now_day) {
                continue;
            }
            labels.push(lx);
            let current = day == t_day;
            let text = if day == now_day { t("Dziś").to_owned() } else { format!("{} {}", weekday_short(day), civil(day).2) };
            ops.push(label(lx + 6.0, top, 80.0, text, if current { p.ink } else { p.ink_muted }, if current { 600 } else { 500 }, false));
        }
        // Zoomed in to two days or less, hours are named between the days.
        if span <= (2 * DAY_MS) as f64 {
            let step = if span <= (12 * HOUR_MS) as f64 { HOUR_MS } else { 3 * HOUR_MS };
            let mut at = (local(input.start_ms).div_euclid(step) + 1) * step;
            while at < local(input.end_ms) {
                let hx = x(at - i64::from(input.tz_offset_min) * 60_000);
                if at.rem_euclid(DAY_MS) != 0 && hx + 40.0 < x1 && labels.iter().all(|&lx| (hx - lx).abs() > 64.0) {
                    ops.push(label(hx - 20.0, top, 40.0, clock(at), p.ink_muted, 400, true));
                    ops.push(rect(hx, top + 18.0, 1.0, 4.0, 0.0, p.line, 0.0));
                }
                at += step;
            }
        }
        // Lane: closed tabs and snapshots.
        ops.push(rect(x0, top + 86.0, x1 - x0, 1.0, 0.0, p.line, 0.0));
        for closed in self.closed_times(workspace).into_iter().filter(|&c| c >= input.start_ms && c <= input.end_ms) {
            ops.push(rect(x(closed) - 3.0, top + 90.0, 6.0, 6.0, 3.0, p.ink_muted, 0.0));
        }
        for mark in self.marks.iter().filter(|m| m.workspace == workspace && m.t_ms >= input.start_ms && m.t_ms <= input.end_ms) {
            ops.push(ffi::Primitive { shape: DIAMOND, ..rect(x(mark.t_ms) - 4.5, top + 88.5, 9.0, 9.0, 0.0, p.bg_base, 0.0) });
            ops.push(ffi::Primitive { shape: DIAMOND, ..rect(x(mark.t_ms) - 4.5, top + 88.5, 9.0, 9.0, 0.0, p.ink, 1.5) });
        }
        // Playhead: a 2 px ink needle and the zing pill naming the moment.
        let px = x(input.t_ms.clamp(input.start_ms, input.end_ms));
        ops.push(rect(px - 1.0, top + 10.0, 2.0, inner - 4.0, 1.0, p.ink, 0.0));
        let pill_text = handle_label(input.t_ms, input.now_ms, input.tz_offset_min);
        let pill_w = pill_text.chars().count() as f32 * MONO_11_ADVANCE + 18.0;
        let pill_x = (px - pill_w / 2.0).clamp(4.0, w - 4.0 - pill_w);
        ops.push(rect(pill_x, top - 8.0, pill_w, 22.0, 11.0, p.zing, 0.0));
        ops.push(ffi::Primitive { height: 22.0, ..label(pill_x, top - 8.0, pill_w, pill_text, p.on_zing, 600, true) });
        ops
    }
}

/// `shape` of a primitive the shell paints as a diamond (a square turned 45°).
pub const DIAMOND: u32 = 20;
/// JetBrains Mono advances 0.6 em; the pill is sized without native measuring.
const MONO_11_ADVANCE: f32 = 6.6;

fn alpha(color: u32, a: u32) -> u32 {
    (color & 0x00ff_ffff) | (a << 24)
}

/// Year, month (1–12) and day of a day count since 1970-01-01 (proleptic Gregorian).
fn civil(days: i64) -> (i64, u32, u32) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let month = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (yoe + era * 400 + i64::from(month <= 2), month, day)
}

/// 0 = Monday.
fn weekday(days: i64) -> usize {
    (days + 3).rem_euclid(7) as usize
}

fn weekday_name(days: i64) -> &'static str {
    [t("poniedziałek"), t("wtorek"), t("środa"), t("czwartek"), t("piątek"), t("sobota"), t("niedziela")][weekday(days)]
}

fn weekday_short(days: i64) -> &'static str {
    [t("Pn"), t("Wt"), t("Śr"), t("Cz"), t("Pt"), t("So"), t("Nd")][weekday(days)]
}

fn month_name(month: u32) -> &'static str {
    [
        t("stycznia"), t("lutego"), t("marca"), t("kwietnia"), t("maja"), t("czerwca"),
        t("lipca"), t("sierpnia"), t("września"), t("października"), t("listopada"), t("grudnia"),
    ][(month as usize - 1).min(11)]
}

fn clock(ms: i64) -> String {
    let minutes = ms.div_euclid(60_000).rem_euclid(24 * 60);
    format!("{:02}:{:02}", minutes / 60, minutes % 60)
}

fn handle_label(t_ms: i64, now_ms: i64, tz_offset_min: i32) -> String {
    let local = |ms: i64| ms + i64::from(tz_offset_min) * 60_000;
    let day = local(t_ms).div_euclid(DAY_MS);
    let name = if day == local(now_ms).div_euclid(DAY_MS) { t("Dziś") } else { weekday_short(day) };
    format!("{} {}", name, clock(local(t_ms)))
}

/// Polish count agreement: 1, 2–4 (not 12–14), the rest.
fn plural(n: usize) -> usize {
    let (r10, r100) = (n % 10, n % 100);
    if n == 1 {
        0
    } else if (2..=4).contains(&r10) && !(12..=14).contains(&r100) {
        1
    } else {
        2
    }
}

/// "2 karty" as the object of a verb ("Zamknięto 2 karty"; 1 kartę, 5 kart).
pub fn tab_count_copy(count: usize) -> String {
    let n = count.to_string();
    match plural(count) {
        0 => tf("{} kartę", &[&n]),
        1 => tf("{} karty", &[&n]),
        _ => tf("{} kart", &[&n]),
    }
}

/// Copy for the moment at `t_ms`: the day line, the clock digits, the restore
/// button and the quantum caption.
pub fn timeline_clock(t_ms: i64, now_ms: i64, tz_offset_min: i32, tabs: usize, quantum_ms: i64) -> ffi::TimelineClock {
    let local = |ms: i64| ms + i64::from(tz_offset_min) * 60_000;
    let day = local(t_ms).div_euclid(DAY_MS);
    let ago = local(now_ms).div_euclid(DAY_MS) - day;
    let (_, month, date) = civil(day);
    let day_line = match ago {
        0 => t("Dziś").to_owned(),
        _ => {
            let when = match ago {
                1 => t("wczoraj").to_owned(),
                7 => t("tydzień temu").to_owned(),
                n if n < 0 => String::new(),
                n => tf("{} dni temu", &[&n.to_string()]),
            };
            let date = format!("{} {}", date, month_name(month));
            if when.is_empty() { format!("{} · {}", weekday_name(day), date) } else { format!("{} · {} · {}", weekday_name(day), date, when) }
        }
    };
    let minutes = local(t_ms).div_euclid(60_000).rem_euclid(24 * 60);
    let (hh, mm) = ((minutes / 60) as u32, (minutes % 60) as u32);
    let count = tabs.to_string();
    let restore = match plural(tabs) {
        0 => tf("Przywróć {} kartę", &[&count]),
        1 => tf("Przywróć {} karty", &[&count]),
        _ => tf("Przywróć {} kart", &[&count]),
    };
    let (from, to) = (clock(local(t_ms - quantum_ms)), clock(local(t_ms)));
    let range = if tabs == 0 {
        tf("Między {} a {} nic nie było otwarte", &[&from, &to])
    } else {
        tf("Wszystko, co było otwarte między {} a {}", &[&from, &to])
    };
    let value = if ago == 0 { t("dziś") } else { weekday_name(day) };
    ffi::TimelineClock {
        day_line: day_line.to_uppercase(),
        digits: vec![hh / 10, hh % 10, mm / 10, mm % 10],
        handle: handle_label(t_ms, now_ms, tz_offset_min),
        value_text: format!("{} {}", value, to),
        restore,
        range,
    }
}

/// The span the axis shows: the last week, or a week centred on a moment older
/// than that. It never extends past now.
pub fn timeline_window(t_ms: i64, now_ms: i64) -> ffi::TimelineSpan {
    if t_ms >= now_ms - WINDOW_MS + 6 * HOUR_MS {
        return ffi::TimelineSpan { start_ms: now_ms - WINDOW_MS, end_ms: now_ms };
    }
    let start = t_ms - WINDOW_MS / 2;
    ffi::TimelineSpan { start_ms: start, end_ms: start + WINDOW_MS }
}

/// The narrowest and widest span the axis zooms to.
pub const MIN_SPAN_MS: i64 = 6 * HOUR_MS;
pub const MAX_SPAN_MS: i64 = 30 * DAY_MS;

/// Keeps a span of `width` ms inside the recorded range: it never ends past now
/// and never starts more than a day before the first record (`first_ms`, or a
/// month back when nothing is recorded).
fn clamp_span(start: i64, width: i64, now_ms: i64, first_ms: i64) -> ffi::TimelineSpan {
    let floor = first_ms.min(now_ms - MAX_SPAN_MS / 2) - DAY_MS;
    let width = width.clamp(MIN_SPAN_MS, MAX_SPAN_MS).min((now_ms - floor).max(MIN_SPAN_MS));
    let start = start.max(floor).min(now_ms - width);
    ffi::TimelineSpan { start_ms: start, end_ms: start + width }
}

/// Scrolling the axis: `dx` pixels move the window, positive towards later.
pub fn timeline_pan(input: &ffi::TimelineSceneInput, dx: f32, first_ms: i64) -> ffi::TimelineSpan {
    let width = input.end_ms - input.start_ms;
    let per_px = width as f64 / f64::from((input.width - 40.0).max(1.0));
    clamp_span(input.start_ms + (f64::from(dx) * per_px) as i64, width, input.now_ms, first_ms)
}

/// Zooming the axis by `factor` (above 1 shows more time) around the moment
/// under `x`, which stays under the pointer.
pub fn timeline_zoom(input: &ffi::TimelineSceneInput, factor: f32, x: f32, first_ms: i64) -> ffi::TimelineSpan {
    let width = input.end_ms - input.start_ms;
    let next = ((width as f64 * f64::from(factor.clamp(0.1, 10.0))) as i64).clamp(MIN_SPAN_MS, MAX_SPAN_MS);
    let ratio = f64::from(((x - 20.0) / (input.width - 40.0).max(1.0)).clamp(0.0, 1.0));
    let anchor = input.start_ms + (ratio * width as f64) as i64;
    clamp_span(anchor - (ratio * next as f64) as i64, next, input.now_ms, first_ms)
}

/// The window after the playhead moved: unchanged while `t_ms` is comfortably
/// inside, otherwise shifted so the playhead sits a fifth in from the edge it
/// crossed.
pub fn timeline_follow(start_ms: i64, end_ms: i64, t_ms: i64, now_ms: i64, first_ms: i64) -> ffi::TimelineSpan {
    let width = (end_ms - start_ms).clamp(MIN_SPAN_MS, MAX_SPAN_MS);
    let margin = width / 20;
    let start = if t_ms < start_ms + margin {
        t_ms - width / 5
    } else if t_ms > end_ms - margin {
        t_ms - width * 4 / 5
    } else {
        start_ms
    };
    clamp_span(start, width, now_ms, first_ms)
}

/// The moment under `x` on a timeline panel laid out by timeline_scene, on a
/// five-minute grid and never past now.
pub fn timeline_time_at(input: &ffi::TimelineSceneInput, x: f32) -> i64 {
    let (x0, x1) = (20.0, (input.width - 20.0).max(21.0));
    let ratio = f64::from(((x - x0) / (x1 - x0)).clamp(0.0, 1.0));
    let at = input.start_ms + (ratio * (input.end_ms - input.start_ms) as f64) as i64;
    let grid = (at as f64 / 300_000.0).round() as i64 * 300_000;
    grid.clamp(input.start_ms, input.end_ms.min(input.now_ms))
}

/// Cover-flow placement (design: Time Machine deck). The front card faces the
/// viewer; the others turn away by 58° and recede, fading with distance.
pub fn timeline_deck(count: usize, front: usize) -> Vec<ffi::TimelineCard> {
    let front = front.min(count.saturating_sub(1)) as i64;
    (0..count as i64)
        .map(|i| {
            let o = i - front;
            let (a, s) = (o.abs() as f32, o.signum() as f32);
            if o == 0 {
                ffi::TimelineCard { x: 0.0, z: 70.0, angle: 0.0, opacity: 1.0, order: 20 }
            } else {
                ffi::TimelineCard {
                    x: s * (150.0 + 62.0 * (a - 1.0)),
                    z: -110.0 - 30.0 * a,
                    angle: -s * 58.0,
                    opacity: (1.0 - 0.15 * a).max(0.3),
                    order: 20 - o.abs().min(20) as i32,
                }
            }
        })
        .collect()
}

/// One card's placement at a continuous `offset` from the front (0 = front,
/// ±1 = first card aside): the same shape as timeline_deck at whole offsets, and
/// smooth between them, so a deck sliding by fractions never jumps.
pub fn timeline_card(offset: f32) -> ffi::TimelineCard {
    let (a, s) = (offset.abs(), if offset < 0.0 { -1.0 } else { 1.0 });
    let (x, z, angle) = if a <= 1.0 {
        // Ease the turn so the front card leaves and arrives softly.
        let e = a * a * (3.0 - 2.0 * a);
        (s * 150.0 * a, 70.0 - 210.0 * a, -s * 58.0 * e)
    } else {
        (s * (150.0 + 62.0 * (a - 1.0)), -110.0 - 30.0 * a, -s * 58.0)
    };
    ffi::TimelineCard { x, z, angle, opacity: (1.0 - 0.15 * a).max(0.3), order: (2000.0 - a * 100.0) as i32 }
}

/// One step of a damped spring (semi-implicit Euler, substeps of at most 4 ms).
/// `damping` below 2·√stiffness overshoots a little, like a game menu; a spring
/// close enough to rest snaps to it and reports `settled`.
pub fn timeline_spring(value: f32, velocity: f32, target: f32, dt: f32, stiffness: f32, damping: f32) -> ffi::TimelineSpring {
    let (mut value, mut velocity) = (value, velocity);
    let mut left = dt.clamp(0.0, 0.1);
    while left > 0.0 {
        let step = left.min(0.004);
        velocity += (stiffness * (target - value) - damping * velocity) * step;
        value += velocity * step;
        left -= step;
    }
    let settled = (target - value).abs() < 0.0015 && velocity.abs() < 0.01;
    if settled {
        (value, velocity) = (target, 0.0);
    }
    ffi::TimelineSpring { value, velocity, settled }
}


#[cfg(test)]
mod tests {
    use super::*;

    fn tab(url: &str, title: &str) -> ffi::TimelineTab {
        ffi::TimelineTab { url: url.into(), title: title.into() }
    }
    fn frame(t: i64, urls: &[&str], active: i32) -> ffi::TimelineFrame {
        ffi::TimelineFrame {
            t_ms: t,
            workspace: "w".into(),
            profile: "p".into(),
            active,
            tabs: urls.iter().map(|u| tab(&format!("https://{u}/"), u)).collect(),
        }
    }
    const MIN: i64 = 60_000;
    use ffi::TimelineRecord as R;

    #[test]
    fn records_changes_coalesces_quick_ones_and_keeps_forced_frames() {
        let mut tl = Timeline::default();
        assert!(tl.timeline_record(&frame(0, &[], -1), false) == R::Skip);
        assert!(tl.timeline_record(&frame(0, &["a"], 0), false) == R::Append);
        assert!(tl.timeline_record(&frame(MIN, &["a"], 0), false) == R::Skip);
        assert!(tl.timeline_record(&frame(MIN + 1000, &["a", "b"], 1), false) == R::Append);
        // A redirect 10 s later replaces the frame rather than adding one.
        assert!(tl.timeline_record(&frame(MIN + 11_000, &["a", "c"], 1), false) == R::Replace);
        assert_eq!(tl.timeline_stats().frames, 2);
        // A forced frame (about to close a tab) is never replaced by the next one.
        assert!(tl.timeline_record(&frame(MIN + 12_000, &["a", "c"], 1), true) == R::Skip);
        assert!(tl.timeline_record(&frame(MIN + 13_000, &["a"], 0), false) == R::Append);
        // Titles alone update the last frame in place.
        let mut renamed = frame(MIN + 14_000, &["a"], 0);
        renamed.tabs[0].title = "A!".into();
        assert!(tl.timeline_record(&renamed, false) == R::Replace);
        assert_eq!(tl.timeline_query("w", MIN + 14_000, 0).tabs[0].title, "A!");
        // Time never runs backwards.
        assert!(tl.timeline_record(&frame(0, &["z"], 0), true) == R::Skip);
    }

    #[test]
    fn non_web_tabs_are_dropped_and_active_follows() {
        let mut tl = Timeline::default();
        let mut f = frame(0, &["a"], 1);
        f.tabs.insert(0, tab("chrome://newtab/", "Nowa karta"));
        f.tabs.push(tab("javascript:alert(1)", "x"));
        f.tabs.push(tab("HTTP://b/", "b"));
        assert!(tl.timeline_record(&f, false) == R::Append);
        let m = tl.timeline_query("w", 0, 0);
        assert_eq!(m.tabs.iter().map(|t| t.url.as_str()).collect::<Vec<_>>(), ["https://a/", "HTTP://b/"]);
        assert_eq!(m.active, 0);
        f.active = 0;
        assert!(tl.timeline_record(&f, true) == R::Append);
        assert_eq!(tl.timeline_query("w", 0, 0).active, -1);
        f.workspace.clear();
        assert!(tl.timeline_record(&f, true) == R::Skip);
    }

    #[test]
    fn query_unions_the_quantum_and_reports_what_was_open_at_t() {
        let mut tl = Timeline::default();
        tl.timeline_record(&frame(0, &["a", "b"], 0), false);
        tl.timeline_record(&frame(40 * MIN, &["b", "c"], 1), false);
        tl.timeline_record(&frame(70 * MIN, &["c", "d"], 1), false);
        let m = tl.timeline_query("w", 70 * MIN, 60 * MIN);
        let urls: Vec<_> = m.tabs.iter().map(|t| t.url.trim_start_matches("https://").trim_end_matches('/')).collect();
        assert_eq!(urls, ["a", "b", "c", "d"]);
        assert_eq!(m.open_at_t, [false, false, true, true]);
        assert_eq!(m.active, 3);
        assert_eq!(m.profile, "p");
        assert_eq!(tl.timeline_query("w", 39 * MIN, 0).tabs.len(), 2);
        assert!(tl.timeline_query("w", -1, 60 * MIN).tabs.is_empty());
        assert!(tl.timeline_query("other", 70 * MIN, 60 * MIN).tabs.is_empty());
    }

    #[test]
    fn step_snap_bounds_and_last_closed() {
        let mut tl = Timeline::default();
        tl.timeline_record(&frame(0, &["a", "b"], 0), false);
        tl.timeline_record(&frame(30 * MIN, &["a"], 0), false);
        tl.timeline_record(&frame(90 * MIN, &["c"], 0), false);
        assert_eq!(tl.timeline_step("w", 31 * MIN, false), 30 * MIN);
        assert_eq!(tl.timeline_step("w", 30 * MIN, true), 90 * MIN);
        assert_eq!(tl.timeline_step("w", 90 * MIN, true), 90 * MIN);
        assert_eq!(tl.timeline_snap("w", 40 * MIN), 30 * MIN);
        assert_eq!(tl.timeline_snap("w", 60 * MIN), 60 * MIN);
        let b = tl.timeline_bounds("w");
        assert!(b.valid && b.first_ms == 0 && b.last_ms == 90 * MIN);
        let closed = tl.timeline_last_closed("w", &[]);
        assert!(closed.valid && closed.tab.url == "https://a/" && closed.t_ms == 90 * MIN);
        let closed = tl.timeline_last_closed("w", &["https://a/".into()]);
        assert!(closed.valid && closed.tab.url == "https://b/");
        assert!(!tl.timeline_last_closed("w", &["https://a/".into(), "https://b/".into()]).valid);
    }

    #[test]
    fn load_replays_replacing_lines_and_rejects_bad_ones() {
        let mut tl = Timeline::default();
        assert!(tl.timeline_load(&frame(0, &["a"], 0), false));
        assert!(tl.timeline_load(&frame(1000, &["b"], 0), true));
        assert_eq!(tl.timeline_stats().frames, 1);
        assert!(!tl.timeline_load(&frame(500, &["c"], 0), false));
        let mut huge = frame(2000, &["a"], 0);
        huge.tabs = (0..=MAX_TABS).map(|i| tab(&format!("https://{i}/"), "")).collect();
        assert!(!tl.timeline_load(&huge, false));
        assert_eq!(tl.timeline_query("w", 2000, 0).tabs[0].url, "https://b/");
    }

    #[test]
    fn prune_forget_and_marks() {
        let mut tl = Timeline::default();
        for i in 0..10 {
            tl.timeline_record(&frame(i * DAY_MS, &[&format!("s{i}")], 0), true);
        }
        let mut other = frame(5 * DAY_MS, &["x"], 0);
        other.workspace = "v".into();
        other.profile = "q".into();
        tl.timeline_record(&other, true);
        assert!(tl.timeline_mark(&ffi::TimelineMark { t_ms: 4 * DAY_MS, workspace: "w".into(), name: "Snapshot".into() }));
        assert!(!tl.timeline_mark(&ffi::TimelineMark { t_ms: 0, workspace: "w".into(), name: String::new() }));
        assert!(tl.timeline_prune(3 * DAY_MS, usize::MAX));
        assert_eq!(tl.timeline_stats().frames, 8);
        assert!(!tl.timeline_prune(3 * DAY_MS, usize::MAX));
        // A mark outlives an empty timeline until retention ends it.
        let mut marks_only = Timeline::default();
        marks_only.timeline_mark(&ffi::TimelineMark { t_ms: 5, workspace: "w".into(), name: "S".into() });
        assert!(!marks_only.timeline_prune(0, usize::MAX));
        assert!(marks_only.timeline_prune(6, usize::MAX));
        let one = Frame { t: 0, profile: "p".into(), active: 0, tabs: vec![tab("https://s9/", "s9")], forced: true }.bytes();
        assert!(tl.timeline_prune(0, 3 * one + 64 + 8));
        assert_eq!(tl.timeline_stats().frames, 3);
        assert_eq!(tl.timeline_stats().marks, 0);
        assert!(tl.timeline_forget("p", 8 * DAY_MS, 8 * DAY_MS));
        assert_eq!(tl.timeline_bounds("w").first_ms, 7 * DAY_MS);
        assert!(!tl.timeline_forget("nobody", 0, i64::MAX));
        assert_eq!(tl.timeline_frames().iter().map(|f| f.t_ms).collect::<Vec<_>>(), [7 * DAY_MS, 9 * DAY_MS]);
        tl.timeline_clear();
        assert_eq!(tl.timeline_stats().bytes, 0);
    }

    #[test]
    fn forget_workspace_includes_marks_without_frames() {
        let mut timeline = Timeline::default();
        for (workspace, time) in [("work", 1), ("work", 2), ("personal", 2)] {
            assert!(timeline.timeline_mark(&ffi::TimelineMark {
                t_ms: time, workspace: workspace.into(), name: "Saved".into(),
            }));
        }
        assert!(timeline.timeline_forget_workspace("work", 2, i64::MAX));
        let marks = timeline.timeline_marks();
        assert_eq!(marks.len(), 2);
        assert_eq!((&*marks[0].workspace, marks[0].t_ms), ("work", 1));
        assert_eq!((&*marks[1].workspace, marks[1].t_ms), ("personal", 2));
        assert!(!timeline.timeline_forget_workspace("work", 2, i64::MAX));
    }

    #[test]
    fn calendar_copy_and_plurals() {
        // 2026-10-23 17:20 local (UTC+2) is a Friday, a week before 2026-10-30.
        let day = civil_days(2026, 10, 23);
        assert_eq!(civil(day), (2026, 10, 23));
        assert_eq!(weekday(day), 4);
        let t_ms = day * DAY_MS + (17 * 60 + 20 - 120) * MIN;
        let now = civil_days(2026, 10, 30) * DAY_MS + (14 * 60 - 120) * MIN;
        let c = timeline_clock(t_ms, now, 120, 7, 60 * MIN);
        assert_eq!(c.day_line, "PIĄTEK · 23 PAŹDZIERNIKA · TYDZIEŃ TEMU");
        assert_eq!(c.digits, [1, 7, 2, 0]);
        assert_eq!(c.handle, "Pt 17:20");
        assert_eq!(c.value_text, "piątek 17:20");
        assert_eq!(c.restore, "Przywróć 7 kart");
        assert_eq!(c.range, "Wszystko, co było otwarte między 16:20 a 17:20");
        assert_eq!(timeline_clock(now, now, 120, 1, 0).day_line, "DZIŚ");
        assert_eq!(timeline_clock(now - DAY_MS, now, 120, 3, 0).day_line, "CZWARTEK · 29 PAŹDZIERNIKA · WCZORAJ");
        assert_eq!(timeline_clock(now - 3 * DAY_MS, now, 120, 22, 0).restore, "Przywróć 22 karty");
        assert_eq!(timeline_clock(now, now, 120, 12, 0).restore, "Przywróć 12 kart");
        assert_eq!(timeline_clock(now, now, 120, 1, 0).restore, "Przywróć 1 kartę");
        assert_eq!([tab_count_copy(1), tab_count_copy(3), tab_count_copy(12), tab_count_copy(22)], ["1 kartę", "3 karty", "12 kart", "22 karty"]);
        assert!(timeline_clock(now, now, 120, 0, MIN).range.contains("nic nie było"));
        // Midnight in UTC is already the next day two hours east.
        assert_eq!(timeline_clock(civil_days(2026, 1, 1) * DAY_MS - MIN, now, 0, 1, 0).digits, [2, 3, 5, 9]);
        assert_eq!(handle_label(civil_days(2026, 10, 28) * DAY_MS + 23 * 60 * MIN, now, 120), "Cz 01:00");
    }

    fn civil_days(y: i64, m: u32, d: u32) -> i64 {
        (0..40_000).find(|&n| civil(n) == (y, m, d)).unwrap()
    }

    #[test]
    fn window_and_pointer_mapping() {
        let now = 100 * DAY_MS;
        assert_eq!(timeline_window(now - DAY_MS, now).start_ms, now - WINDOW_MS);
        let old = timeline_window(now - 20 * DAY_MS, now);
        assert_eq!((old.start_ms, old.end_ms), (now - 20 * DAY_MS - WINDOW_MS / 2, now - 20 * DAY_MS + WINDOW_MS / 2));
        let input = ffi::TimelineSceneInput {
            width: 1040.0, height: 128.0, t_ms: now, quantum_ms: HOUR_MS, now_ms: now, tz_offset_min: 0,
            start_ms: now - WINDOW_MS, end_ms: now,
        };
        assert_eq!(timeline_time_at(&input, 20.0), now - WINDOW_MS);
        assert_eq!(timeline_time_at(&input, 2000.0), now);
        let mid = timeline_time_at(&input, 520.0);
        assert_eq!(mid, now - WINDOW_MS / 2);
        assert_eq!(mid % 300_000, 0);
    }

    #[test]
    fn pan_zoom_and_follow_keep_the_window_in_range() {
        let now = 100 * DAY_MS;
        let input = ffi::TimelineSceneInput {
            width: 1040.0, height: 128.0, t_ms: now, quantum_ms: HOUR_MS, now_ms: now, tz_offset_min: 0,
            start_ms: now - WINDOW_MS, end_ms: now,
        };
        let first = now - 20 * DAY_MS;
        // Scrolling back by half the panel moves half a week; forward stops at now.
        let back = timeline_pan(&input, -500.0, first);
        assert_eq!(back.end_ms - back.start_ms, WINDOW_MS);
        assert_eq!(back.end_ms, now - WINDOW_MS / 2);
        assert_eq!(timeline_pan(&input, 500.0, first).end_ms, now);
        // Never more than a day before the first record.
        let far = timeline_pan(&input, -1_000_000.0, first);
        assert_eq!(far.start_ms, first - DAY_MS);
        // Zooming keeps the moment under the pointer and respects the limits.
        let zoomed = timeline_zoom(&input, 0.5, 520.0, first);
        assert_eq!(zoomed.end_ms - zoomed.start_ms, WINDOW_MS / 2);
        assert_eq!((zoomed.start_ms + zoomed.end_ms) / 2, now - WINDOW_MS / 2);
        // One step zooms at most tenfold; the next one stops at six hours.
        let tiny = timeline_zoom(&input, 0.001, 520.0, first);
        assert_eq!(tiny.end_ms - tiny.start_ms, WINDOW_MS / 10);
        let closer = ffi::TimelineSceneInput { start_ms: tiny.start_ms, end_ms: tiny.end_ms, ..input };
        let tiny = timeline_zoom(&closer, 0.001, 520.0, first);
        assert_eq!(tiny.end_ms - tiny.start_ms, MIN_SPAN_MS);
        let wide = timeline_zoom(&input, 10.0, 1020.0, first);
        assert_eq!((wide.end_ms - wide.start_ms, wide.end_ms), (MAX_SPAN_MS.min(now - (first - DAY_MS)), now));
        // Following: no move inside, a jump that keeps the playhead visible outside.
        let span = timeline_follow(now - WINDOW_MS, now, now - DAY_MS, now, first);
        assert_eq!(span.start_ms, now - WINDOW_MS);
        let span = timeline_follow(now - WINDOW_MS, now, now - 10 * DAY_MS, now, first);
        assert!(span.start_ms < now - 10 * DAY_MS && span.end_ms > now - 10 * DAY_MS);
    }

    #[test]
    fn zoomed_scenes_name_hours_and_thin_out_days() {
        let tl = Timeline::default();
        let now = 100 * DAY_MS + 14 * HOUR_MS;
        let p = crate::design_palette(true, 0);
        let day = ffi::TimelineSceneInput {
            width: 1040.0, height: 128.0, t_ms: now, quantum_ms: HOUR_MS, now_ms: now, tz_offset_min: 0,
            start_ms: now - DAY_MS, end_ms: now,
        };
        let ops = tl.timeline_scene("w", &day, &p);
        assert!(ops.iter().any(|o| o.text == "12:00"));
        assert!(ops.iter().filter(|o| o.text.ends_with(":00") && o.center && o.color == p.ink_muted).count() >= 6);
        let month = ffi::TimelineSceneInput { start_ms: now - 30 * DAY_MS, ..day };
        let ops = tl.timeline_scene("w", &month, &p);
        let names = ops.iter().filter(|o| !o.text.is_empty() && !o.center).count();
        assert!(names < 20 && names > 5, "{names}");
        assert!(!ops.iter().any(|o| o.text == "12:00"));
    }

    #[test]
    fn scene_draws_axis_parts_inside_the_panel() {
        let mut tl = Timeline::default();
        let now = 100 * DAY_MS + 14 * HOUR_MS;
        tl.timeline_record(&frame(now - 2 * DAY_MS, &["a", "b"], 0), false);
        tl.timeline_record(&frame(now - DAY_MS, &["a"], 0), false);
        tl.timeline_mark(&ffi::TimelineMark { t_ms: now - DAY_MS, workspace: "w".into(), name: "S".into() });
        let input = ffi::TimelineSceneInput {
            width: 1040.0, height: 128.0, t_ms: now - DAY_MS, quantum_ms: HOUR_MS, now_ms: now, tz_offset_min: 0,
            start_ms: now - WINDOW_MS, end_ms: now,
        };
        let p = crate::design_palette(true, 0);
        let ops = tl.timeline_scene("w", &input, &p);
        assert!(ops.iter().all(|o| o.x >= 0.0 && o.x + o.width <= 1040.0 + 0.01 && o.clip.width == 1040.0));
        assert_eq!(ops.iter().filter(|o| o.shape == DIAMOND).count(), 2);
        assert_eq!(ops.iter().filter(|o| o.color == p.zing).count(), 1);
        // A label for each of the 8 days the week touches, today's included.
        assert_eq!(ops.iter().filter(|o| !o.text.is_empty() && !o.center).count(), 8);
        assert!(ops.iter().any(|o| o.text == "Dziś"));
        assert!(ops.iter().any(|o| o.text.ends_with(" 14:00") && o.color == p.on_zing && o.center));
        // The closed tab and the hour of the playhead.
        assert!(ops.iter().any(|o| o.color == p.ink_muted && o.width == 6.0));
        assert!(ops.iter().any(|o| o.color == p.ink && o.radius == 2.0));
        let empty = ffi::TimelineSceneInput { width: 10.0, ..input };
        assert!(tl.timeline_scene("w", &empty, &p).is_empty());
    }

    #[test]
    fn continuous_cards_match_the_deck_and_never_jump() {
        let deck = timeline_deck(7, 3);
        for (i, card) in deck.iter().enumerate() {
            let c = timeline_card(i as f32 - 3.0);
            assert!((c.x - card.x).abs() < 1e-3 && (c.z - card.z).abs() < 1e-3 && (c.angle - card.angle).abs() < 1e-3, "{i}");
        }
        let mut previous = timeline_card(-4.0);
        for step in 1..=800 {
            let c = timeline_card(-4.0 + step as f32 * 0.01);
            assert!((c.x - previous.x).abs() < 3.0 && (c.z - previous.z).abs() < 3.0 && (c.angle - previous.angle).abs() < 3.0, "{step}");
            previous = c;
        }
        assert!(timeline_card(0.0).order > timeline_card(0.5).order);
    }

    #[test]
    fn springs_overshoot_a_little_and_settle() {
        let (k, c) = (140.0, 18.5);
        let (mut v, mut vel, mut peak, mut frames) = (0.0f32, 0.0f32, 0.0f32, 0);
        loop {
            let s = timeline_spring(v, vel, 1.0, 1.0 / 60.0, k, c);
            (v, vel) = (s.value, s.velocity);
            peak = peak.max(v);
            frames += 1;
            if s.settled { break; }
            assert!(frames < 240, "never settles");
        }
        assert_eq!(v, 1.0);
        assert!(peak > 1.005 && peak < 1.08, "{peak}");
        // A long frame is clamped, never a jump past the target and back.
        let s = timeline_spring(0.0, 0.0, 1.0, 5.0, k, c);
        assert!(s.value < 1.1);
    }

    #[test]
    fn deck_faces_the_front_card() {
        let d = timeline_deck(5, 2);
        assert_eq!((d[2].x, d[2].z, d[2].angle, d[2].order), (0.0, 70.0, 0.0, 20));
        assert_eq!((d[1].x, d[1].angle), (-150.0, 58.0));
        assert_eq!((d[4].x, d[4].z, d[4].angle), (212.0, -170.0, -58.0));
        assert!(d[0].opacity < d[1].opacity && d[0].order < d[1].order);
        assert_eq!(timeline_deck(3, 9)[2].z, 70.0);
        assert!(timeline_deck(0, 0).is_empty());
    }
}
