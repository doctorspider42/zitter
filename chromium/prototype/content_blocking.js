// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Cosmetic ad blocking, run only in Zitter's isolated world (docs/ad-blocking.md).
// Pages share the DOM but never see this world's state.
(() => {
  const args = ZITTER_ARGS;
  const state = globalThis.__zitterBlocking ??= { sheets: {}, selectors: {}, classes: new Set(), ids: new Set(), preview: [] };
  // A selector that does not parse is skipped alone, never the whole sheet.
  const matches = selector => { try { return document.querySelectorAll(selector); } catch { return []; } };
  const valid = selector => { try { document.createDocumentFragment().querySelector(selector); return true; } catch { return false; } };
  const hidden = () => {
    const found = new Set();
    for (const list of Object.values(state.selectors)) for (const selector of list) for (const e of matches(selector)) found.add(e);
    return found.size;
  };
  // The document element may not exist yet right after a navigation commits.
  const whenRoot = run => {
    if (document.documentElement) { run(); return; }
    const observer = new MutationObserver(() => { if (document.documentElement) { observer.disconnect(); run(); } });
    observer.observe(document, { childList: true });
  };
  if (args.op === 'style') {
    const selectors = (args.selectors ?? []).filter(s => typeof s === 'string' && s.length < 2000 && valid(s));
    state.selectors[args.key] = [...new Set([...(args.append ? state.selectors[args.key] ?? [] : []), ...selectors])];
    const css = state.selectors[args.key].map(s => `${s}{display:none!important}`).join('\n');
    whenRoot(() => {
      let sheet = state.sheets[args.key];
      if (!sheet?.isConnected) {
        sheet = document.createElement('style');
        sheet.setAttribute('data-zitter', 'blocking');
        state.sheets[args.key] = sheet;
      }
      sheet.textContent = css;
      // Last in the document, so the page's own rules never come after it.
      if (sheet.parentNode !== document.documentElement || sheet.nextSibling) document.documentElement.append(sheet);
    });
    return { hidden: document.documentElement ? hidden() : 0 };
  }
  if (args.op === 'scan') {
    // Class names and IDs not reported before, for the engine's generic rules.
    if (args.reset) { state.classes.clear(); state.ids.clear(); state.selectors.generic = []; }
    const classes = [], ids = [];
    for (const e of document.querySelectorAll('[class],[id]')) {
      if (classes.length + ids.length > 4000) break;
      for (const name of e.classList ?? []) if (!state.classes.has(name)) { state.classes.add(name); classes.push(name); }
      if (e.id && !state.ids.has(e.id)) { state.ids.add(e.id); ids.push(e.id); }
    }
    return { classes, ids, hidden: hidden() };
  }
  if (args.op === 'preview') {
    for (const e of state.preview) {
      if (!e.isConnected) continue;
      e.style.removeProperty('outline'); e.style.removeProperty('outline-offset'); e.style.removeProperty('background-color');
      e.removeAttribute('data-zitter-preview');
    }
    state.preview = [];
    for (const selector of args.selectors ?? []) for (const e of matches(selector)) {
      if (state.preview.includes(e) || state.preview.length >= 500) continue;
      e.setAttribute('data-zitter-preview', '');
      e.style.setProperty('outline', '2px dashed #6e56cf', 'important');
      e.style.setProperty('outline-offset', '-2px', 'important');
      e.style.setProperty('background-color', 'rgba(110, 86, 207, 0.16)', 'important');
      state.preview.push(e);
    }
    if (state.preview[0]) state.preview[0].scrollIntoView({ block: 'center', inline: 'nearest' });
    return { count: state.preview.length };
  }
  return { error: 'Unknown blocking operation' };
})()
