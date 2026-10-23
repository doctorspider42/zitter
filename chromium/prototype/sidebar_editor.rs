// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// UTF-16 UI editing only. Native services supply Unicode/visual boundaries;
// profile objects, browser state and platform handles never enter this model.
use std::collections::VecDeque;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Selection {
    pub anchor: usize,
    pub focus: usize,
    pub secondary: bool,
}
impl Selection {
    fn range(self) -> std::ops::Range<usize> {
        self.anchor.min(self.focus)..self.anchor.max(self.focus)
    }
    fn caret(at: usize) -> Self {
        Self {
            anchor: at,
            focus: at,
            secondary: false,
        }
    }
}
// macOS distinguishes retaining the anchor, extending past a reversal and
// stopping at the original anchor for word/paragraph selection.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SelectionPolicy {
    None,
    Retain,
    Extend,
    Caret,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Change {
    pub admitted: bool,
    pub text: bool,
    pub selection: bool,
    pub composition: bool,
    // Preedit/cancel are display changes, not committed application edits.
    pub committed: bool,
}
// The platform adapter owns the shared kill buffer. Return offsets into its
// pre-edit text cache instead of allocating a second copy of the fragment.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KillResult {
    pub change: Change,
    pub start: usize,
    pub end: usize,
    pub before_revision: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Info {
    pub revision: u64,
    pub selection: Selection,
    pub directed: bool,
    pub enabled: bool,
    pub read_only: bool,
    pub composing: bool,
    pub composition_start: usize,
    pub composition_end: usize,
    pub composition_token: u64,
    pub can_undo: bool,
    pub can_redo: bool,
    pub boundaries_ready: bool,
}
#[derive(Clone, Debug)]
struct Edit {
    start: usize,
    removed: Vec<u16>,
    inserted: Vec<u16>,
    before: Selection,
    after: Selection,
    group: u64,
    kind: Kind,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Typing,
    Backspace,
    Delete,
    Separate,
}
#[derive(Debug)]
struct Composition {
    token: u64,
    start: usize,
    length: usize,
    original: Vec<u16>,
    before: Selection,
}
#[derive(Debug)]
pub struct EditorState {
    text: Vec<u16>,
    selection: Selection,
    directed: bool,
    revision: u64,
    scroll: f32,
    enabled: bool,
    read_only: bool,
    composition: Option<Composition>,
    ime_generation: u64,
    undo: VecDeque<Edit>,
    redo: Vec<Edit>,
    group: u64,
    boundaries_revision: Option<u64>,
    graphemes: Vec<usize>,
    words: Vec<(usize, usize)>,
}
impl Default for EditorState {
    fn default() -> Self {
        Self {
            text: vec![],
            selection: Selection::default(),
            directed: false,
            revision: 0,
            scroll: 0.0,
            enabled: true,
            read_only: false,
            composition: None,
            ime_generation: 0,
            undo: VecDeque::new(),
            redo: vec![],
            group: 0,
            boundaries_revision: Some(0),
            graphemes: vec![0],
            words: vec![],
        }
    }
}
impl EditorState {
    // A bounded number of delta groups, rather than whole-text snapshots on
    // every keystroke. A single arbitrarily sized edit remains undoable.
    const HISTORY_GROUPS: usize = 256;
    pub fn scroll(&self) -> f32 {
        self.scroll
    }
    pub fn set_scroll(&mut self, offset: f32) {
        self.scroll = if offset.is_finite() {
            offset.max(0.0)
        } else {
            0.0
        };
    }
    pub fn text(&self) -> &[u16] {
        &self.text
    }
    pub fn info(&self) -> Info {
        let (start, end, token) = self
            .composition
            .as_ref()
            .map_or((0, 0, 0), |c| (c.start, c.start + c.length, c.token));
        Info {
            revision: self.revision,
            selection: self.selection,
            directed: self.directed,
            enabled: self.enabled,
            read_only: self.read_only,
            composing: self.composition.is_some(),
            composition_start: start,
            composition_end: end,
            composition_token: token,
            can_undo: self.editable() && (!self.undo.is_empty() || self.composition.is_some()),
            can_redo: self.editable() && self.composition.is_none() && !self.redo.is_empty(),
            boundaries_ready: self.boundaries_revision == Some(self.revision),
        }
    }
    fn editable(&self) -> bool {
        self.enabled && !self.read_only
    }
    fn point(&self, index: usize) -> bool {
        valid_point(&self.text, index)
    }
    fn break_group(&mut self) {
        self.group = self.group.wrapping_add(1);
    }
    fn next_ime(&mut self) -> u64 {
        self.ime_generation = self.ime_generation.wrapping_add(1).max(1);
        self.ime_generation
    }
    // Lone surrogates from native services are preserved, but an edit cannot
    // join two across its boundary: that would split its new caret/marked range.
    fn replacement_boundaries(&self, start: usize, end: usize, text: &[u16]) -> bool {
        let left = self.text.get(start.wrapping_sub(1)).copied();
        let right = self.text.get(end).copied();
        let joins =
            |a: Option<u16>, b: Option<u16>| a.is_some_and(is_high) && b.is_some_and(is_low);
        if text.is_empty() {
            !joins(left, right)
        } else {
            !joins(left, text.first().copied()) && !joins(text.last().copied(), right)
        }
    }
    fn replace_raw(&mut self, start: usize, end: usize, text: &[u16]) -> bool {
        if self.text[start..end] == *text {
            return false;
        }
        self.directed = false;
        self.text.splice(start..end, text.iter().copied());
        self.revision = self.revision.wrapping_add(1);
        self.boundaries_revision = if self.text.is_empty() {
            Some(self.revision)
        } else {
            None
        };
        self.graphemes = vec![0];
        self.words.clear();
        true
    }
    fn record(&mut self, edit: Edit) {
        if edit.removed == edit.inserted {
            return;
        }
        self.redo.clear();
        if let Some(last) = self.undo.back_mut() {
            if last.group == edit.group && last.kind == edit.kind && last.after == edit.before {
                if edit.kind == Kind::Typing
                    && last.removed.is_empty()
                    && edit.removed.is_empty()
                    && last.start + last.inserted.len() == edit.start
                {
                    last.inserted.extend(edit.inserted);
                    last.after = edit.after;
                    return;
                }
                if edit.kind == Kind::Backspace
                    && last.inserted.is_empty()
                    && edit.inserted.is_empty()
                    && edit.start + edit.removed.len() == last.start
                {
                    let mut removed = edit.removed;
                    removed.append(&mut last.removed);
                    last.removed = removed;
                    last.start = edit.start;
                    last.after = edit.after;
                    return;
                }
                if edit.kind == Kind::Delete
                    && last.inserted.is_empty()
                    && edit.inserted.is_empty()
                    && last.start == edit.start
                {
                    last.removed.extend(edit.removed);
                    last.after = edit.after;
                    return;
                }
            }
        }
        self.undo.push_back(edit);
        while self.undo.len() > Self::HISTORY_GROUPS {
            self.undo.pop_front();
        }
    }
    // Context/programmatic reset never emits a committed edit and invalidates
    // old IME callbacks even if the new text happens to be identical.
    pub fn reset(&mut self, text: &[u16]) -> Change {
        self.scroll = 0.0;
        self.directed = false;
        let before = self.selection;
        let composing = self.composition.take().is_some();
        self.next_ime();
        self.break_group();
        let changed = self.replace_raw(0, self.text.len(), text);
        self.selection = Selection::caret(text.len());
        self.undo.clear();
        self.redo.clear();
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            composition: composing,
            committed: false,
        }
    }
    pub fn availability(&mut self, enabled: bool, read_only: bool) -> Change {
        let mut change = Change {
            admitted: true,
            ..Change::default()
        };
        if (!enabled || read_only) && self.composition.is_some() {
            change = self.cancel(self.info().composition_token);
        }
        if self.enabled != enabled || self.read_only != read_only {
            self.break_group();
        }
        self.enabled = enabled;
        self.read_only = read_only;
        change
    }
    // Exact UTF-16 selection for native IME/AX services. Interactive movement
    // uses the platform's current grapheme stops below, never byte indices.
    pub fn select(&mut self, anchor: usize, focus: usize, secondary: bool) -> Change {
        if !self.enabled || !self.point(anchor) || !self.point(focus) {
            return Change::default();
        }
        self.directed = false;
        let next = Selection {
            anchor,
            focus,
            secondary,
        };
        let changed = next != self.selection;
        if changed {
            self.break_group();
            self.selection = next;
        }
        Change {
            admitted: true,
            selection: changed,
            ..Change::default()
        }
    }
    pub fn install_boundaries(
        &mut self,
        revision: u64,
        stops: &[usize],
        word_pairs: &[usize],
    ) -> bool {
        if revision != self.revision
            || stops.first() != Some(&0)
            || stops.last() != Some(&self.text.len())
            || stops.windows(2).any(|p| p[0] >= p[1])
            || stops.iter().any(|&p| !self.point(p))
            || word_pairs.len() % 2 != 0
        {
            return false;
        }
        let mut words = vec![];
        let mut previous = 0;
        for pair in word_pairs.chunks_exact(2) {
            let (start, end) = (pair[0], pair[1]);
            if start >= end
                || start < previous
                || stops.binary_search(&start).is_err()
                || stops.binary_search(&end).is_err()
            {
                return false;
            }
            words.push((start, end));
            previous = end;
        }
        self.graphemes = stops.to_vec();
        self.words = words;
        self.boundaries_revision = Some(revision);
        true
    }
    pub fn move_to(&mut self, index: usize, extend: bool, secondary: bool) -> Change {
        if self.composition.is_some()
            || self.boundaries_revision != Some(self.revision)
            || self.graphemes.binary_search(&index).is_err()
        {
            return Change::default();
        }
        let change = self.select(
            if extend { self.selection.anchor } else { index },
            index,
            secondary,
        );
        if change.admitted {
            self.directed = true;
        }
        change
    }
    pub fn orient(&mut self, anchor: usize, focus: usize, secondary: bool) -> Change {
        let change = self.select(anchor, focus, secondary);
        if change.admitted {
            self.directed = true;
        }
        change
    }
    fn motion_selection(&self, backward: bool, policy: SelectionPolicy) -> Selection {
        let mut selection = self.selection;
        if (!self.directed || policy == SelectionPolicy::None)
            && selection.anchor != selection.focus
            && ((backward && selection.focus > selection.anchor)
                || (!backward && selection.focus < selection.anchor))
        {
            std::mem::swap(&mut selection.anchor, &mut selection.focus);
            selection.secondary = false;
        }
        selection
    }
    fn apply_motion(
        &mut self,
        index: usize,
        policy: SelectionPolicy,
        secondary: bool,
        backward: bool,
    ) -> Change {
        if !self.enabled
            || self.composition.is_some()
            || self.boundaries_revision != Some(self.revision)
            || self.graphemes.binary_search(&index).is_err()
        {
            return Change::default();
        }
        let old = self.motion_selection(backward, policy);
        let reversed = old.anchor != old.focus
            && old.focus.min(index) < old.anchor
            && old.anchor < old.focus.max(index);
        let (anchor, focus, affinity) = match policy {
            SelectionPolicy::None => (index, index, secondary),
            SelectionPolicy::Retain => (old.anchor, index, secondary),
            SelectionPolicy::Extend => (
                if reversed { old.focus } else { old.anchor },
                index,
                secondary,
            ),
            SelectionPolicy::Caret if reversed => (old.anchor, old.anchor, old.secondary),
            SelectionPolicy::Caret => (old.anchor, index, secondary),
        };
        self.orient(anchor, focus, affinity)
    }
    pub fn move_with_policy(
        &mut self,
        index: usize,
        policy: SelectionPolicy,
        secondary: bool,
    ) -> Change {
        self.apply_motion(
            index,
            policy,
            secondary,
            index < self.selection.focus
                || (index == self.selection.focus && index <= self.selection.anchor),
        )
    }
    pub fn navigate(&mut self, backward: bool, word: bool, policy: SelectionPolicy) -> Change {
        if !self.enabled
            || self.composition.is_some()
            || self.boundaries_revision != Some(self.revision)
        {
            return Change::default();
        }
        let old = self.motion_selection(backward, policy);
        let range = old.range();
        let target = if !word && policy == SelectionPolicy::None && !range.is_empty() {
            old.focus
        } else if word {
            if backward {
                self.words
                    .iter()
                    .rev()
                    .find(|w| w.0 < old.focus)
                    .map_or(0, |w| w.0)
            } else {
                self.words
                    .iter()
                    .find(|w| w.1 > old.focus)
                    .map_or(self.text.len(), |w| w.1)
            }
        } else if backward {
            self.graphemes
                .iter()
                .rev()
                .copied()
                .find(|&p| p < old.focus)
                .unwrap_or(0)
        } else {
            self.graphemes
                .iter()
                .copied()
                .find(|&p| p > old.focus)
                .unwrap_or(self.text.len())
        };
        self.apply_motion(target, policy, false, backward)
    }

    pub fn kill_to_boundary(&mut self, end: bool) -> KillResult {
        if !self.editable() || self.composition.is_some() {
            return KillResult::default();
        }
        let old = self.selection;
        let range = old.range();
        let range = if range.is_empty() {
            if end {
                range.start..self.text.len()
            } else {
                0..range.start
            }
        } else {
            range
        };
        if range.is_empty() {
            return KillResult {
                change: Change {
                    admitted: true,
                    ..Change::default()
                },
                ..KillResult::default()
            };
        }
        if !self.replacement_boundaries(range.start, range.end, &[]) {
            return KillResult::default();
        }
        let before_revision = self.revision;
        // Native undo restores the selection used by the deletion, including
        // the range extended from an initially collapsed caret.
        if old.anchor == old.focus {
            self.selection = Selection {
                anchor: old.focus,
                focus: if end { range.end } else { range.start },
                secondary: false,
            };
        }
        let mut change = self.erase(false, false);
        change.selection = old != self.selection;
        KillResult {
            change,
            start: range.start,
            end: range.end,
            before_revision,
        }
    }
    pub fn yank(&mut self, text: &[u16]) -> Change {
        if !self.editable() {
            return Change::default();
        }
        self.insert(text, false)
    }
    pub fn transpose(&mut self) -> Change {
        if !self.editable()
            || self.composition.is_some()
            || self.selection.anchor != self.selection.focus
            || self.boundaries_revision != Some(self.revision)
        {
            return Change::default();
        }
        let Ok(mut at) = self.graphemes.binary_search(&self.selection.focus) else {
            return Change::default();
        };
        if at + 1 == self.graphemes.len() {
            at = at.saturating_sub(1);
        }
        if at == 0 || at + 1 >= self.graphemes.len() {
            return Change {
                admitted: true,
                ..Change::default()
            };
        }
        let (start, middle, end) = (
            self.graphemes[at - 1],
            self.graphemes[at],
            self.graphemes[at + 1],
        );
        // Chromium's current macOS command requires exactly two Unicode code
        // points as well as adjacent graphemes; combining/ZWJ sequences stay put.
        let mut scalar_count = 0;
        let mut cursor = start;
        while cursor < end {
            scalar_count += 1;
            cursor += if is_high(self.text[cursor])
                && cursor + 1 < end
                && is_low(self.text[cursor + 1])
            {
                2
            } else {
                1
            };
        }
        if scalar_count != 2 {
            return Change {
                admitted: true,
                ..Change::default()
            };
        }
        let mut inserted = self.text[middle..end].to_vec();
        inserted.extend_from_slice(&self.text[start..middle]);
        if !self.replacement_boundaries(start, end, &inserted) {
            return Change::default();
        }
        let removed = self.text[start..end].to_vec();
        let old = self.selection;
        let changed = self.replace_raw(start, end, &inserted);
        self.selection = Selection::caret(end);
        self.break_group();
        self.record(Edit {
            start,
            removed,
            inserted,
            before: Selection {
                anchor: start,
                focus: end,
                secondary: false,
            },
            after: self.selection,
            group: self.group,
            kind: Kind::Separate,
        });
        Change {
            admitted: true,
            text: changed,
            selection: old != self.selection,
            committed: changed,
            ..Change::default()
        }
    }
    pub fn move_logical(&mut self, backward: bool, word: bool, extend: bool) -> Change {
        if !self.enabled
            || self.composition.is_some()
            || self.boundaries_revision != Some(self.revision)
        {
            return Change::default();
        }
        let range = self.selection.range();
        let target = if !extend && range.start != range.end {
            if backward {
                self.graphemes
                    .iter()
                    .rev()
                    .copied()
                    .find(|&p| p <= range.start)
                    .unwrap_or(0)
            } else {
                self.graphemes
                    .iter()
                    .copied()
                    .find(|&p| p >= range.end)
                    .unwrap_or(self.text.len())
            }
        } else if word {
            if backward {
                self.words
                    .iter()
                    .rev()
                    .find(|w| w.0 < self.selection.focus)
                    .map_or(0, |w| w.0)
            } else {
                self.words
                    .iter()
                    .find(|w| w.1 > self.selection.focus)
                    .map_or(self.text.len(), |w| w.1)
            }
        } else if backward {
            self.graphemes
                .iter()
                .rev()
                .copied()
                .find(|&p| p < self.selection.focus)
                .unwrap_or(0)
        } else {
            self.graphemes
                .iter()
                .copied()
                .find(|&p| p > self.selection.focus)
                .unwrap_or(self.text.len())
        };
        self.move_to(target, extend, false)
    }
    pub fn insert(&mut self, text: &[u16], typing: bool) -> Change {
        if !self.editable() {
            return Change::default();
        }
        let range = self
            .composition
            .as_ref()
            .map_or_else(|| self.selection.range(), |c| c.start..c.start + c.length);
        if !self.replacement_boundaries(range.start, range.end, text) {
            return Change::default();
        }
        if let Some(c) = self.composition.take() {
            let before = self.selection;
            let changed = self.replace_raw(c.start, c.start + c.length, text);
            self.selection = Selection::caret(c.start + text.len());
            self.next_ime();
            self.break_group();
            self.record(Edit {
                start: c.start,
                removed: c.original,
                inserted: text.to_vec(),
                before: c.before,
                after: self.selection,
                group: self.group,
                kind: Kind::Separate,
            });
            return Change {
                admitted: true,
                text: changed,
                selection: before != self.selection,
                composition: true,
                committed: true,
            };
        }
        let before = self.selection;
        let range = before.range();
        let removed = self.text[range.clone()].to_vec();
        if !typing || !removed.is_empty() {
            self.break_group();
        }
        let changed = self.replace_raw(range.start, range.end, text);
        self.selection = Selection::caret(range.start + text.len());
        self.record(Edit {
            start: range.start,
            removed,
            inserted: text.to_vec(),
            before,
            after: self.selection,
            group: self.group,
            kind: if typing && range.is_empty() {
                Kind::Typing
            } else {
                Kind::Separate
            },
        });
        if !typing {
            self.break_group();
        }
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            composition: false,
            committed: changed,
        }
    }
    pub fn erase(&mut self, backward: bool, word: bool) -> Change {
        if !self.editable() {
            return Change::default();
        }
        if self.composition.is_some() {
            return self.cancel(self.info().composition_token);
        }
        let before = self.selection;
        let mut range = before.range();
        if range.is_empty() {
            if self.boundaries_revision != Some(self.revision) {
                return Change::default();
            }
            let point = if word {
                if backward {
                    self.words
                        .iter()
                        .rev()
                        .find(|w| w.0 < range.start)
                        .map_or(0, |w| w.0)
                } else {
                    self.words
                        .iter()
                        .find(|w| w.1 > range.end)
                        .map_or(self.text.len(), |w| w.1)
                }
            } else if backward {
                self.graphemes
                    .iter()
                    .rev()
                    .copied()
                    .find(|&p| p < range.start)
                    .unwrap_or(0)
            } else {
                self.graphemes
                    .iter()
                    .copied()
                    .find(|&p| p > range.end)
                    .unwrap_or(self.text.len())
            };
            // A service may put its exact UTF-16 caret inside a composed cluster.
            // Interactive deletion must still remove the complete cluster.
            let start = self
                .graphemes
                .iter()
                .rev()
                .copied()
                .find(|&p| p <= range.start.min(point))
                .unwrap_or(0);
            let end = self
                .graphemes
                .iter()
                .copied()
                .find(|&p| p >= range.end.max(point))
                .unwrap_or(self.text.len());
            range = start..end;
        }
        if !self.replacement_boundaries(range.start, range.end, &[]) {
            return Change::default();
        }
        let removed = self.text[range.clone()].to_vec();
        let changed = self.replace_raw(range.start, range.end, &[]);
        self.selection = Selection::caret(range.start);
        if word || before.anchor != before.focus {
            self.break_group();
        }
        self.record(Edit {
            start: range.start,
            removed,
            inserted: vec![],
            before,
            after: self.selection,
            group: self.group,
            kind: if word || before.anchor != before.focus {
                Kind::Separate
            } else if backward {
                Kind::Backspace
            } else {
                Kind::Delete
            },
        });
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            composition: false,
            committed: changed,
        }
    }
    pub fn drop_selection(&mut self, destination: usize, copy: bool) -> Change {
        if !self.editable()
            || self.composition.is_some()
            || self.boundaries_revision != Some(self.revision)
            || self.graphemes.binary_search(&destination).is_err()
        {
            return Change::default();
        }
        let before = self.selection;
        let range = before.range();
        if range.is_empty() || (!copy && destination >= range.start && destination <= range.end) {
            return Change::default();
        }
        let selected = self.text[range.clone()].to_vec();
        let (start, end, inserted, caret) = if copy {
            (
                destination,
                destination,
                selected.clone(),
                destination + selected.len(),
            )
        } else if destination < range.start {
            let mut text = selected;
            text.extend_from_slice(&self.text[destination..range.start]);
            (destination, range.end, text, destination + range.len())
        } else {
            let mut text = self.text[range.end..destination].to_vec();
            text.extend_from_slice(&selected);
            (range.start, destination, text, destination)
        };
        if !self.replacement_boundaries(start, end, &inserted)
            || !valid_point(&inserted, caret - start)
        {
            return Change::default();
        }
        let removed = self.text[start..end].to_vec();
        let changed = self.replace_raw(start, end, &inserted);
        self.selection = Selection::caret(caret);
        self.break_group();
        self.record(Edit {
            start,
            removed,
            inserted,
            before,
            after: self.selection,
            group: self.group,
            kind: Kind::Separate,
        });
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            committed: changed,
            ..Change::default()
        }
    }
    pub fn begin_composition(&mut self) -> u64 {
        if !self.editable() {
            return 0;
        }
        if let Some(c) = &self.composition {
            return c.token;
        }
        self.break_group();
        let range = self.selection.range();
        let token = self.next_ime();
        self.composition = Some(Composition {
            token,
            start: range.start,
            length: range.len(),
            original: self.text[range].to_vec(),
            before: self.selection,
        });
        token
    }
    pub fn compose(&mut self, token: u64, text: &[u16], anchor: usize, focus: usize) -> Change {
        if !self.editable() || !valid_point(text, anchor) || !valid_point(text, focus) {
            return Change::default();
        }
        let Some(c) = &self.composition else {
            return Change::default();
        };
        if c.token != token || token == 0 {
            return Change::default();
        }
        let (start, end) = (c.start, c.start + c.length);
        if !self.replacement_boundaries(start, end, text) {
            return Change::default();
        }
        let before = self.selection;
        let changed = self.replace_raw(start, end, text);
        self.composition.as_mut().unwrap().length = text.len();
        self.selection = Selection {
            anchor: start + anchor,
            focus: start + focus,
            secondary: false,
        };
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            composition: true,
            committed: false,
        }
    }
    pub fn confirm(&mut self, token: u64) -> Change {
        if !self.editable()
            || self
                .composition
                .as_ref()
                .is_none_or(|c| token == 0 || c.token != token)
        {
            return Change::default();
        }
        let c = self.composition.take().unwrap();
        let before = self.selection;
        self.selection = Selection::caret(c.start + c.length);
        self.record(Edit {
            start: c.start,
            removed: c.original,
            inserted: self.text[c.start..c.start + c.length].to_vec(),
            before: c.before,
            after: self.selection,
            group: self.group,
            kind: Kind::Separate,
        });
        self.next_ime();
        self.break_group();
        Change {
            admitted: true,
            selection: before != self.selection,
            composition: true,
            committed: true,
            ..Change::default()
        }
    }
    pub fn cancel(&mut self, token: u64) -> Change {
        if self
            .composition
            .as_ref()
            .is_none_or(|c| token == 0 || c.token != token)
        {
            return Change::default();
        }
        let c = self.composition.take().unwrap();
        let before = self.selection;
        let changed = self.replace_raw(c.start, c.start + c.length, &c.original);
        self.selection = c.before;
        self.next_ime();
        self.break_group();
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            composition: true,
            committed: false,
        }
    }
    pub fn undo(&mut self) -> Change {
        if !self.editable() {
            return Change::default();
        }
        if self.composition.is_some() {
            return self.cancel(self.info().composition_token);
        }
        let Some(edit) = self.undo.pop_back() else {
            return Change::default();
        };
        let before = self.selection;
        let changed = self.replace_raw(edit.start, edit.start + edit.inserted.len(), &edit.removed);
        self.selection = edit.before;
        self.redo.push(edit);
        self.break_group();
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            committed: changed,
            ..Change::default()
        }
    }
    pub fn redo(&mut self) -> Change {
        if !self.editable() || self.composition.is_some() {
            return Change::default();
        }
        let Some(edit) = self.redo.pop() else {
            return Change::default();
        };
        let before = self.selection;
        let changed = self.replace_raw(edit.start, edit.start + edit.removed.len(), &edit.inserted);
        self.selection = edit.after;
        self.undo.push_back(edit);
        self.break_group();
        Change {
            admitted: true,
            text: changed,
            selection: before != self.selection,
            committed: changed,
            ..Change::default()
        }
    }
}
fn valid_point(text: &[u16], at: usize) -> bool {
    at <= text.len()
        && (at == 0 || at == text.len() || !(is_high(text[at - 1]) && is_low(text[at])))
}
fn is_high(c: u16) -> bool {
    (0xd800..=0xdbff).contains(&c)
}
fn is_low(c: u16) -> bool {
    (0xdc00..=0xdfff).contains(&c)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    fn value(e: &EditorState) -> String {
        String::from_utf16(e.text()).unwrap()
    }
    fn ascii(e: &mut EditorState) {
        let stops = (0..=e.text().len()).collect::<Vec<_>>();
        assert!(e.install_boundaries(e.info().revision, &stops, &[]));
    }
    fn editor(s: &str) -> EditorState {
        let mut e = EditorState::default();
        e.reset(&u(s));
        e
    }
    #[test]
    fn typing_coalesces_and_redoes() {
        let mut e = editor("");
        for c in ["a", "b", "c"] {
            assert!(e.insert(&u(c), true).committed);
        }
        assert_eq!(value(&e), "abc");
        assert!(e.undo().text);
        assert_eq!(value(&e), "");
        assert!(!e.info().can_undo);
        assert!(e.redo().text);
        assert_eq!(value(&e), "abc");
    }
    #[test]
    fn movement_breaks_typing_group() {
        let mut e = editor("");
        e.insert(&u("a"), true);
        e.select(0, 0, false);
        e.select(1, 1, false);
        e.insert(&u("b"), true);
        e.undo();
        assert_eq!(value(&e), "a");
        e.undo();
        assert_eq!(value(&e), "");
    }
    #[test]
    fn paste_is_separate() {
        let mut e = editor("");
        e.insert(&u("a"), true);
        e.insert(&u("bc"), false);
        e.insert(&u("d"), true);
        for expected in ["abc", "a", ""] {
            e.undo();
            assert_eq!(value(&e), expected);
        }
    }
    #[test]
    fn reverse_selection_is_restored() {
        let mut e = editor("abcd");
        e.select(3, 1, true);
        e.insert(&u("X"), true);
        assert_eq!(value(&e), "aXd");
        e.undo();
        assert_eq!(value(&e), "abcd");
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 3,
                focus: 1,
                secondary: true
            }
        );
    }
    #[test]
    fn repeated_backspace_coalesces() {
        let mut e = editor("abcd");
        for _ in 0..3 {
            ascii(&mut e);
            e.erase(true, false);
        }
        assert_eq!(value(&e), "a");
        e.undo();
        assert_eq!(value(&e), "abcd");
        assert!(!e.info().can_undo);
    }
    #[test]
    fn repeated_delete_coalesces() {
        let mut e = editor("abcd");
        e.select(0, 0, false);
        for _ in 0..3 {
            ascii(&mut e);
            e.erase(false, false);
        }
        assert_eq!(value(&e), "d");
        e.undo();
        assert_eq!(value(&e), "abcd");
    }
    #[test]
    fn stale_and_invalid_geometry_rejected_transactionally() {
        let mut e = editor("abc");
        let r = e.info().revision;
        assert!(!e.install_boundaries(r - 1, &[0, 3], &[]));
        assert!(!e.install_boundaries(r, &[0, 1, 1, 3], &[]));
        assert!(!e.install_boundaries(r, &[0, 1, 2, 3], &[1, 3, 2, 3]));
        assert!(!e.info().boundaries_ready);
        ascii(&mut e);
        assert!(!e.install_boundaries(r, &[0, 4], &[]));
        assert!(e.info().boundaries_ready);
        e.insert(&u("d"), true);
        assert!(!e.info().boundaries_ready);
        assert!(!e.move_logical(true, false, false).admitted);
    }
    #[test]
    fn surrogate_pair_never_splits() {
        let mut e = editor("a🦀b");
        assert!(!e.select(2, 2, false).admitted);
        let r = e.info().revision;
        assert!(!e.install_boundaries(r, &[0, 1, 2, 3, 4], &[]));
        assert!(e.install_boundaries(r, &[0, 1, 3, 4], &[]));
        e.select(3, 3, false);
        e.erase(true, false);
        assert_eq!(value(&e), "ab");
        e.undo();
        assert_eq!(value(&e), "a🦀b");
    }
    #[test]
    fn deletion_expands_service_caret_inside_cluster() {
        for backward in [false, true] {
            let mut e = editor("a\u{301}b");
            e.install_boundaries(e.info().revision, &[0, 2, 3], &[]);
            e.select(1, 1, false);
            e.erase(backward, false);
            assert_eq!(value(&e), "b");
            e.undo();
            assert_eq!(value(&e), "a\u{301}b");
        }
    }
    #[test]
    fn collapsing_service_selection_snaps_outward() {
        for backward in [false, true] {
            let mut e = editor("a\u{301}bc");
            e.install_boundaries(e.info().revision, &[0, 2, 3, 4], &[]);
            e.select(1, 3, false);
            assert!(e.move_logical(backward, false, false).admitted);
            assert_eq!(e.info().selection.focus, if backward { 0 } else { 3 });
        }
    }
    #[test]
    fn word_navigation_and_deletion() {
        let mut e = editor("one  two");
        let stops = (0..=8).collect::<Vec<_>>();
        e.install_boundaries(e.info().revision, &stops, &[0, 3, 5, 8]);
        e.move_logical(true, true, false);
        assert_eq!(e.info().selection.focus, 5);
        e.erase(true, true);
        assert_eq!(value(&e), "two");
        e.undo();
        assert_eq!(value(&e), "one  two");
    }
    #[test]
    fn shift_navigation_keeps_anchor() {
        let mut e = editor("abc");
        ascii(&mut e);
        e.move_logical(true, false, true);
        e.move_logical(true, false, true);
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 3,
                focus: 1,
                secondary: false
            }
        );
    }
    #[test]
    fn composition_is_atomic_and_only_confirmation_commits() {
        let mut e = editor("abcd");
        e.select(3, 1, true);
        let t = e.begin_composition();
        assert_ne!(t, 0);
        assert_eq!(t, e.begin_composition());
        assert!(!e.compose(t, &u("日"), 1, 1).committed);
        assert!(!e.compose(t, &u("日本"), 2, 2).committed);
        assert_eq!(value(&e), "a日本d");
        assert!(e.confirm(t).committed);
        assert!(!e.info().composing);
        e.undo();
        assert_eq!(value(&e), "abcd");
        assert_eq!(e.info().selection.anchor, 3);
        e.redo();
        assert_eq!(value(&e), "a日本d");
    }
    #[test]
    fn cancel_restores_text_selection_and_redo() {
        let mut e = editor("abc");
        e.insert(&u("d"), false);
        e.undo();
        e.select(2, 0, true);
        let t = e.begin_composition();
        e.compose(t, &u("漢"), 1, 1);
        let c = e.cancel(t);
        assert!(c.text && !c.committed);
        assert_eq!(value(&e), "abc");
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 2,
                focus: 0,
                secondary: true
            }
        );
        assert!(e.info().can_redo);
        e.redo();
        assert_eq!(value(&e), "abcd");
    }
    #[test]
    fn empty_preedit_still_active() {
        let mut e = editor("abc");
        e.select(0, 2, false);
        let t = e.begin_composition();
        e.compose(t, &[], 0, 0);
        assert!(e.info().composing);
        assert_eq!(value(&e), "c");
        e.cancel(t);
        assert_eq!(value(&e), "abc");
    }
    #[test]
    fn insert_during_composition_is_one_undo() {
        let mut e = editor("abc");
        e.select(0, 2, false);
        let t = e.begin_composition();
        e.compose(t, &u("x"), 1, 1);
        assert!(e.insert(&u("日本"), false).committed);
        assert_eq!(value(&e), "日本c");
        e.undo();
        assert_eq!(value(&e), "abc");
        assert!(!e.info().can_undo);
    }
    #[test]
    fn old_ime_callbacks_cannot_mutate_new_context() {
        let mut e = editor("abc");
        let t = e.begin_composition();
        e.reset(&u("abc"));
        let next = e.begin_composition();
        assert_ne!(t, next);
        for c in [e.compose(t, &u("x"), 1, 1), e.confirm(t), e.cancel(t)] {
            assert!(!c.admitted);
        }
        assert_eq!(value(&e), "abc");
        assert_eq!(e.info().composition_token, next);
    }
    #[test]
    fn readonly_cancels_preedit_and_blocks_edits() {
        let mut e = editor("abc");
        e.select(0, 2, false);
        let t = e.begin_composition();
        e.compose(t, &u("x"), 1, 1);
        let c = e.availability(true, true);
        assert!(!c.committed);
        assert_eq!(value(&e), "abc");
        assert!(e.select(0, 3, false).admitted);
        assert!(!e.insert(&u("x"), true).admitted);
        assert!(!e.erase(true, false).admitted);
        assert_eq!(e.begin_composition(), 0);
        assert!(!e.undo().admitted);
        assert!(!e.info().can_undo);
    }
    #[test]
    fn disabled_blocks_selection_and_editing() {
        let mut e = editor("abc");
        e.availability(false, false);
        assert!(!e.select(0, 0, false).admitted);
        assert!(!e.insert(&u("x"), true).admitted);
        e.availability(true, false);
        assert!(e.insert(&u("x"), true).admitted);
    }
    #[test]
    fn undo_cancels_preedit_before_history() {
        let mut e = editor("abc");
        e.insert(&u("d"), false);
        let t = e.begin_composition();
        e.compose(t, &u("x"), 1, 1);
        assert!(!e.undo().committed);
        assert_eq!(value(&e), "abcd");
        assert!(e.undo().committed);
        assert_eq!(value(&e), "abc");
    }
    #[test]
    fn new_edit_invalidates_redo() {
        let mut e = editor("a");
        e.insert(&u("b"), false);
        e.undo();
        assert!(e.info().can_redo);
        e.insert(&u("c"), false);
        assert!(!e.info().can_redo);
        assert!(!e.redo().admitted);
    }
    #[test]
    fn reset_clears_history_even_for_identical_text() {
        let mut e = editor("a");
        e.insert(&u("b"), false);
        let r = e.info().revision;
        let c = e.reset(&u("ab"));
        assert!(!c.text && !c.committed);
        assert_eq!(e.info().revision, r);
        assert!(!e.info().can_undo);
    }
    #[test]
    fn history_is_bounded_without_truncating_text() {
        let mut e = editor("");
        for _ in 0..300 {
            e.insert(&u("x"), false);
        }
        for _ in 0..256 {
            assert!(e.undo().admitted);
        }
        assert_eq!(e.text().len(), 44);
        assert!(!e.undo().admitted);
        for _ in 0..256 {
            e.redo();
        }
        assert_eq!(e.text().len(), 300);
    }
    #[test]
    fn invalid_relative_ime_selection_is_transactional() {
        let mut e = editor("abc");
        let t = e.begin_composition();
        assert!(!e.compose(t, &u("🦀"), 1, 1).admitted);
        assert!(!e.compose(t, &u("x"), 2, 2).admitted);
        assert_eq!(value(&e), "abc");
        assert!(e.info().composing);
    }
    #[test]
    fn arbitrary_utf16_roundtrips() {
        let mut e = EditorState::default();
        e.reset(&[0xd800, 65, 0xdc00]);
        assert_eq!(e.text(), &[0xd800, 65, 0xdc00]);
        e.select(0, 3, false);
        e.insert(&u("ok"), false);
        e.undo();
        assert_eq!(e.text(), &[0xd800, 65, 0xdc00]);
    }
    #[test]
    fn no_op_confirmation_is_a_commit_notification() {
        let mut e = editor("abc");
        let t = e.begin_composition();
        let c = e.confirm(t);
        assert!(c.committed && !c.text);
        assert!(!e.info().can_undo);
    }
}

#[cfg(test)]
mod malformed_service_tests {
    use super::*;
    #[test]
    fn edits_cannot_create_surrogate_pairs_across_caret_or_marked_boundaries() {
        let mut e = EditorState::default();
        e.reset(&[0xd800, 65, 0xdc00]);
        e.select(1, 2, false);
        assert!(!e.insert(&[], false).admitted);
        assert!(!e.erase(true, false).admitted);
        let t = e.begin_composition();
        assert!(!e.compose(t, &[], 0, 0).admitted);
        assert!(e.info().composing);
        e.cancel(t);
        e.select(1, 1, false);
        assert!(!e.insert(&[0xdc00], true).admitted);
        e.select(2, 2, false);
        assert!(!e.insert(&[0xd800], true).admitted);
        assert_eq!(e.text(), &[0xd800, 65, 0xdc00]);
    }
}

#[cfg(test)]
mod drop_tests {
    use super::*;
    fn model() -> EditorState {
        let mut e = EditorState::default();
        e.reset(&"abcdef".encode_utf16().collect::<Vec<_>>());
        let stops = (0..=6).collect::<Vec<_>>();
        assert!(e.install_boundaries(e.info().revision, &stops, &[]));
        e.select(1, 3, false);
        e
    }
    #[test]
    fn drag_moves_in_either_direction_as_one_undo() {
        for (at, expected, caret) in [(0, "bcadef", 2), (6, "adefbc", 6)] {
            let mut e = model();
            assert!(e.drop_selection(at, false).committed);
            assert_eq!(String::from_utf16(e.text()).unwrap(), expected);
            assert_eq!(e.info().selection.focus, caret);
            e.undo();
            assert_eq!(String::from_utf16(e.text()).unwrap(), "abcdef");
            assert_eq!(
                e.info().selection,
                Selection {
                    anchor: 1,
                    focus: 3,
                    secondary: false
                }
            );
            assert!(!e.info().can_undo);
            e.redo();
            assert_eq!(String::from_utf16(e.text()).unwrap(), expected);
        }
    }
    #[test]
    fn drag_copy_does_not_remove_source() {
        let mut e = model();
        e.drop_selection(6, true);
        assert_eq!(String::from_utf16(e.text()).unwrap(), "abcdefbc");
        assert_eq!(e.info().selection.focus, 8);
        e.undo();
        assert_eq!(String::from_utf16(e.text()).unwrap(), "abcdef");
    }
    #[test]
    fn move_into_source_and_stale_geometry_are_rejected() {
        let mut e = model();
        for at in [1, 2, 3] {
            assert!(!e.drop_selection(at, false).admitted);
        }
        e.insert(&[65], false);
        assert!(!e.drop_selection(0, false).admitted);
    }
    #[test]
    fn composing_disabled_and_readonly_cannot_drop() {
        let mut e = model();
        let token = e.begin_composition();
        assert!(!e.drop_selection(0, false).admitted);
        e.cancel(token);
        e.availability(true, true);
        assert!(!e.drop_selection(0, false).admitted);
        e.availability(false, false);
        assert!(!e.drop_selection(0, true).admitted);
    }
}

#[cfg(test)]
mod command_tests {
    use super::*;
    fn model(s: &str) -> EditorState {
        let mut e = EditorState::default();
        e.reset(&s.encode_utf16().collect::<Vec<_>>());
        e
    }
    fn ascii(e: &mut EditorState) {
        let stops = (0..=e.text().len()).collect::<Vec<_>>();
        assert!(e.install_boundaries(e.info().revision, &stops, &[]));
    }
    fn text(e: &EditorState) -> String {
        String::from_utf16(e.text()).unwrap()
    }
    fn kill(e: &mut EditorState, end: bool, buffer: &mut Vec<u16>) -> Change {
        let text = e.text().to_vec();
        let revision = e.info().revision;
        let result = e.kill_to_boundary(end);
        if result.change.text {
            assert_eq!(revision, result.before_revision);
            *buffer = text[result.start..result.end].to_vec();
        }
        result.change
    }
    #[test]
    fn selection_policies_match_mac_reversal() {
        for (policy, anchor, focus) in [
            (SelectionPolicy::Retain, 5, 0),
            (SelectionPolicy::Extend, 9, 0),
            (SelectionPolicy::Caret, 5, 5),
            (SelectionPolicy::None, 0, 0),
        ] {
            let mut e = model("one two three");
            ascii(&mut e);
            e.orient(5, 9, false);
            e.move_with_policy(0, policy, false);
            assert_eq!(
                (e.info().selection.anchor, e.info().selection.focus),
                (anchor, focus)
            );
        }
    }
    #[test]
    fn returning_exactly_to_anchor_collapses_without_reversal() {
        for policy in [
            SelectionPolicy::Retain,
            SelectionPolicy::Extend,
            SelectionPolicy::Caret,
        ] {
            let mut e = model("abcdef");
            ascii(&mut e);
            e.orient(3, 5, false);
            e.move_with_policy(3, policy, false);
            assert_eq!(e.info().selection, Selection::caret(3));
        }
    }
    #[test]
    fn reverse_selection_extends_from_its_previous_focus() {
        let mut e = model("abcdef");
        ascii(&mut e);
        e.orient(4, 1, false);
        e.move_with_policy(6, SelectionPolicy::Extend, false);
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 1,
                focus: 6,
                secondary: false
            }
        );
    }
    #[test]
    fn word_selection_stops_at_anchor_when_reversing() {
        let mut e = model("one two three");
        let stops = (0..=13).collect::<Vec<_>>();
        e.install_boundaries(e.info().revision, &stops, &[0, 3, 4, 7, 8, 13]);
        e.orient(5, 9, false);
        e.navigate(true, true, SelectionPolicy::Caret);
        assert_eq!(e.info().selection.focus, 8);
        e.navigate(true, true, SelectionPolicy::Caret);
        assert_eq!(e.info().selection, Selection::caret(5));
    }
    #[test]
    fn policy_rejects_stale_or_invalid_grapheme_points() {
        let mut e = model("a\u{301}b");
        assert!(
            !e.move_with_policy(0, SelectionPolicy::Extend, false)
                .admitted
        );
        e.install_boundaries(e.info().revision, &[0, 2, 3], &[]);
        assert!(
            !e.move_with_policy(1, SelectionPolicy::Retain, false)
                .admitted
        );
        e.availability(false, false);
        assert!(!e.move_with_policy(0, SelectionPolicy::None, false).admitted);
    }
    #[test]
    fn kill_to_end_yank_and_undo_restore_native_selection() {
        let mut buffer = vec![];
        let mut e = model("one two");
        e.select(4, 4, false);
        assert!(kill(&mut e, true, &mut buffer).committed);
        assert_eq!(text(&e), "one ");
        e.undo();
        assert_eq!(text(&e), "one two");
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 4,
                focus: 7,
                secondary: false
            }
        );
        e.redo();
        e.yank(&buffer);
        assert_eq!(text(&e), "one two");
    }
    #[test]
    fn kill_to_start_is_one_edit_and_yanks_in_another_editor() {
        let mut buffer = vec![];
        let mut e = model("one two");
        e.select(4, 4, false);
        kill(&mut e, false, &mut buffer);
        assert_eq!(text(&e), "two");
        let mut other = model("X");
        other.yank(&buffer);
        assert_eq!(text(&other), "Xone ");
        e.undo();
        assert_eq!(text(&e), "one two");
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 4,
                focus: 0,
                secondary: false
            }
        );
        assert!(!e.info().can_undo);
    }
    #[test]
    fn selected_kill_does_not_expand_and_next_kill_overwrites() {
        let mut buffer = vec![];
        let mut e = model("abcdef");
        e.select(4, 2, true);
        kill(&mut e, true, &mut buffer);
        assert_eq!(text(&e), "abef");
        e.select(0, 1, false);
        kill(&mut e, false, &mut buffer);
        let mut other = model("");
        other.yank(&buffer);
        assert_eq!(text(&other), "a");
    }
    #[test]
    fn empty_kill_preserves_previous_buffer() {
        let mut buffer = vec![];
        let mut e = model("abc");
        kill(&mut e, false, &mut buffer);
        assert!(kill(&mut e, true, &mut buffer).admitted);
        let mut other = model("");
        other.yank(&buffer);
        assert_eq!(text(&other), "abc");
    }
    #[test]
    fn kill_and_yank_respect_readonly_disabled_and_composition() {
        let mut buffer = vec![];
        let mut e = model("abc");
        e.availability(true, true);
        assert!(!kill(&mut e, false, &mut buffer).admitted);
        assert!(!e.yank(&buffer).admitted);
        e.availability(true, false);
        let t = e.begin_composition();
        assert!(!kill(&mut e, false, &mut buffer).admitted);
        e.cancel(t);
        e.availability(false, false);
        assert!(!e.yank(&buffer).admitted);
        let mut other = model("");
        assert!(!other.yank(&buffer).text);
    }
    #[test]
    fn empty_yank_replaces_selection_like_native() {
        let buffer = vec![];
        let mut e = model("abc");
        e.select(0, 2, false);
        assert!(e.yank(&buffer).text);
        assert_eq!(text(&e), "c");
        e.undo();
        assert_eq!(text(&e), "abc");
    }
    #[test]
    fn transpose_inside_and_at_end_is_atomic() {
        for (caret, expected) in [(2, "acbd"), (4, "abdc")] {
            let mut e = model("abcd");
            ascii(&mut e);
            e.select(caret, caret, false);
            assert!(e.transpose().committed);
            assert_eq!(text(&e), expected);
            e.undo();
            assert_eq!(text(&e), "abcd");
            assert_eq!(
                e.info().selection,
                Selection {
                    anchor: if caret == 2 { 1 } else { 2 },
                    focus: if caret == 2 { 3 } else { 4 },
                    secondary: false
                }
            );
            assert!(!e.info().can_undo);
            e.redo();
            assert_eq!(text(&e), expected);
        }
    }
    #[test]
    fn transpose_handles_surrogate_pairs_but_keeps_combining_clusters() {
        let mut e = model("🦀x");
        e.install_boundaries(e.info().revision, &[0, 2, 3], &[]);
        e.transpose();
        assert_eq!(text(&e), "x🦀");
        e.undo();
        assert_eq!(text(&e), "🦀x");
        let mut e = model("a\u{301}b");
        e.install_boundaries(e.info().revision, &[0, 2, 3], &[]);
        assert!(!e.transpose().text);
        assert_eq!(text(&e), "a\u{301}b");
    }
    #[test]
    fn transpose_noops_at_start_or_with_one_cluster() {
        for s in ["", "a", "🦀"] {
            let mut e = model(s);
            let n = e.text().len();
            let stops = if n == 0 { vec![0] } else { vec![0, n] };
            e.install_boundaries(e.info().revision, &stops, &[]);
            assert!(!e.transpose().text);
            assert!(!e.info().can_undo);
        }
        let mut e = model("ab");
        ascii(&mut e);
        e.select(0, 0, false);
        assert!(!e.transpose().text);
    }
    #[test]
    fn transpose_rejects_selection_composition_and_unavailable_model() {
        let mut e = model("abc");
        ascii(&mut e);
        e.select(0, 1, false);
        assert!(!e.transpose().admitted);
        e.select(2, 2, false);
        let t = e.begin_composition();
        assert!(!e.transpose().admitted);
        e.cancel(t);
        e.availability(true, true);
        assert!(!e.transpose().admitted);
    }
}

#[cfg(test)]
mod undirected_tests {
    use super::*;
    fn model() -> EditorState {
        let mut e = EditorState::default();
        e.reset(&"one two three".encode_utf16().collect::<Vec<_>>());
        let stops = (0..=13).collect::<Vec<_>>();
        e.install_boundaries(e.info().revision, &stops, &[0, 3, 4, 7, 8, 13]);
        e.select(5, 9, false);
        e
    }
    #[test]
    fn service_selection_chooses_edge_on_first_word_motion() {
        let mut e = model();
        assert!(!e.info().directed);
        e.navigate(true, true, SelectionPolicy::Caret);
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 9,
                focus: 4,
                secondary: false
            }
        );
        assert!(e.info().directed);
    }
    #[test]
    fn word_motion_without_shift_advances_past_selection_edge() {
        for (backward, at) in [(true, 4), (false, 13)] {
            let mut e = model();
            e.navigate(backward, true, SelectionPolicy::None);
            assert_eq!(e.info().selection, Selection::caret(at));
        }
    }
    #[test]
    fn undirected_page_selection_retains_the_resolved_edge() {
        let mut e = model();
        e.move_with_policy(0, SelectionPolicy::Retain, false);
        assert_eq!(
            e.info().selection,
            Selection {
                anchor: 9,
                focus: 0,
                secondary: false
            }
        );
    }
    #[test]
    fn undo_restores_an_undirected_service_selection() {
        let mut e = model();
        e.orient(5, 9, false);
        e.insert(&[65], false);
        e.undo();
        assert!(!e.info().directed);
        assert_eq!(e.info().selection.anchor, 5);
        assert_eq!(e.info().selection.focus, 9);
    }
}

#[cfg(test)]
mod kill_service_contract_tests {
    use super::*;
    #[test]
    fn killed_offsets_refer_to_previous_revision_and_cache() {
        let mut e = EditorState::default();
        e.reset(&"a🦀b".encode_utf16().collect::<Vec<_>>());
        e.select(1, 1, false);
        let before = e.text().to_vec();
        let revision = e.info().revision;
        let killed = e.kill_to_boundary(true);
        assert!(killed.change.committed);
        assert_eq!(
            (killed.start, killed.end, killed.before_revision),
            (1, 4, revision)
        );
        assert!(killed.end > e.text().len());
        assert_ne!(e.info().revision, revision);
        let buffer = &before[killed.start..killed.end];
        e.yank(buffer);
        assert_eq!(e.text(), before);
    }
    #[test]
    fn rejected_kill_exposes_no_fragment_and_changes_no_state() {
        let mut e = EditorState::default();
        e.reset(&[65, 66, 67]);
        e.availability(true, true);
        let before = e.info();
        let k = e.kill_to_boundary(false);
        assert_eq!(k, KillResult::default());
        assert_eq!(e.info(), before);
        assert_eq!(e.text(), &[65, 66, 67]);
    }
}
