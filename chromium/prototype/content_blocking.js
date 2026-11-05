// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Cosmetic ad blocking, run only in Zitter's isolated world (docs/ad-blocking.md).
// Pages share the DOM but never see this world's state.
(() => {
  const args = ZITTER_ARGS;
  const state = globalThis.__zitterBlocking ??= { sheets: {}, selectors: {}, procedural: {}, classes: new Set(), ids: new Set(),
                                                  preview: [], marked: new Set(), removed: 0, hits: {},
                                                  mark: 'data-z' + Math.random().toString(36).slice(2, 8) };
  // A selector that does not parse is skipped alone, never the whole sheet.
  const matches = selector => { try { return document.querySelectorAll(selector); } catch { return []; } };
  const valid = selector => { try { document.createDocumentFragment().querySelector(selector); return true; } catch { return false; } };

  // Procedural filters in uBlock Origin's syntax, implemented here from its
  // public documentation: `:has-text()`, `:upward()`, `:matches-attr()`,
  // `:matches-css()`, `:min-text-length()`, `:matches-path()`, `:xpath()`,
  // procedural `:has()` / `:not()`, and the actions `:remove()`, `:style()`,
  // `:remove-attr()`, `:remove-class()`.
  const OPERATORS = new Set(['has-text', 'contains', '-abp-contains', 'upward', 'matches-attr', 'matches-css', 'matches-css-before',
                             'matches-css-after', 'matches-path', 'min-text-length', 'xpath', 'has', '-abp-has', 'if', 'not', 'if-not']);
  const ACTIONS = new Set(['remove', 'style', 'remove-attr', 'remove-class']);
  const PROCEDURAL = /:(has-text|contains|-abp-contains|-abp-has|upward|matches-attr|matches-css(-before|-after)?|matches-path|min-text-length|xpath|if|if-not|remove|style|remove-attr|remove-class)\(/;
  // The index of the `)` closing the `(` at `open`, past escapes and quoted
  // attribute values (`:has-text(Don't)` has no quote to close).
  const closing = (text, open) => {
    let depth = 0, square = 0, quote = '';
    for (let i = open; i < text.length; i++) {
      const c = text[i];
      if (c === '\\') { i++; continue; }
      if (quote) { if (c === quote) quote = ''; continue; }
      if ((c === '"' || c === "'") && square) quote = c;
      else if (c === '[') square++;
      else if (c === ']') square = Math.max(0, square - 1);
      else if (c === '(') depth++;
      else if (c === ')' && --depth === 0) return i;
    }
    return -1;
  };
  // `/re/flags` becomes that RegExp; other text is matched literally.
  const pattern = (text, exact = false) => {
    text = text.trim();
    const regex = /^\/(.+)\/([imsu]*)$/s.exec(text);
    if (regex) return new RegExp(regex[1], regex[2]);
    const unquoted = /^(["'])(.*)\1$/s.exec(text)?.[2] ?? text;
    const literal = unquoted.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    return new RegExp(exact ? `^${literal}$` : literal);
  };
  // `name="value"`, either side quoted or a /regex/; the value is optional.
  const attribute = text => {
    text = text.trim();
    let end;
    if (text[0] === '/' || text[0] === '"' || text[0] === "'") {
      const quote = text[0];
      for (end = 1; end < text.length && text[end] !== quote; end++) if (text[end] === '\\') end++;
      end++;
      if (quote === '/') while (/[imsu]/.test(text[end] ?? '')) end++;
    } else end = text.includes('=') ? text.indexOf('=') : text.length;
    const name = text.slice(0, end).trim(), rest = text.slice(end).trim();
    if (!rest) return [name, null];
    return rest[0] === '=' ? [name, rest.slice(1)] : ['', null];
  };
  // A selector becomes a CSS head, then steps run on the elements it found.
  const compile = (text, nested = false) => {
    text = text.trim();
    let action = null, css = '', i = 0;
    const steps = [];
    // CSS after an operator continues from the elements found so far.
    const flush = () => {
      const raw = css, part = css.trim();
      css = '';
      if (!part) return;
      if (!steps.length) { steps.push({ css: part }); return; }
      let combinator = /^\s/.test(raw) ? ' ' : '', rest = part;
      if (/^[>+~]/.test(rest)) { combinator = rest[0]; rest = rest.slice(1).trim(); }
      if (!valid(rest)) throw new Error(`bad CSS: ${part}`);
      steps.push({ combinator, rest });
    };
    while (i < text.length) {
      const c = text[i];
      if (c === '[' || c === '"' || c === "'") {
        const end = c === '[' ? text.indexOf(']', i) : text.indexOf(c, i + 1);
        if (end < 0) throw new Error('unclosed ' + c);
        css += text.slice(i, end + 1); i = end + 1; continue;
      }
      if (c === '\\') { css += text.slice(i, i + 2); i += 2; continue; }
      const name = c === ':' && /^:(-?[a-z-]+)\(/.exec(text.slice(i))?.[1];
      if (name) {
        const open = i + name.length + 1, close = closing(text, open);
        if (close < 0) throw new Error(`unclosed :${name}(`);
        const arg = text.slice(open + 1, close);
        const procedural = ACTIONS.has(name) || (OPERATORS.has(name) && (!['has', 'not'].includes(name) || PROCEDURAL.test(arg)));
        if (!procedural) { css += text.slice(i, close + 1); i = close + 1; continue; }
        flush();
        if (ACTIONS.has(name)) {
          if (nested || close !== text.length - 1) throw new Error(`:${name}() must end the rule`);
          action = { type: name, arg };
          break;
        }
        steps.push(operator(name, arg));
        i = close + 1;
        continue;
      }
      css += c; i++;
    }
    flush();
    if (!steps.length) steps.push({ css: '*' });
    if (steps[0].css && !valid(steps[0].css.replace(/^[>+~]\s*/, ''))) throw new Error(`bad CSS: ${steps[0].css}`);
    if (action?.type === 'style' && /url\(|expression|javascript:/i.test(action.arg)) throw new Error(':style() may not load anything');
    if (action && action.type !== 'remove' && !action.arg.trim()) throw new Error(`:${action.type}() needs an argument`);
    return { steps, action };
  };
  const operator = (name, arg) => {
    switch (name) {
      case 'has-text': case 'contains': case '-abp-contains': {
        const re = pattern(arg);
        return { filter: e => re.test(e.textContent ?? '') };
      }
      case 'min-text-length': {
        const length = Number(arg);
        if (!Number.isInteger(length) || length < 1) throw new Error(':min-text-length() needs a number');
        return { filter: e => (e.textContent ?? '').length >= length };
      }
      case 'matches-attr': {
        const [rawName, rawValue] = attribute(arg);
        if (!rawName) throw new Error(':matches-attr() needs name="value"');
        const key = pattern(rawName, true), value = rawValue === null ? null : pattern(rawValue, true);
        return { filter: e => e.getAttributeNames().some(n => key.test(n) && (!value || value.test(e.getAttribute(n) ?? ''))) };
      }
      case 'matches-css': case 'matches-css-before': case 'matches-css-after': {
        const at = arg.indexOf(':');
        if (at < 1) throw new Error(`:${name}() needs property: value`);
        const property = arg.slice(0, at).trim(), value = pattern(arg.slice(at + 1), true);
        const pseudo = name.endsWith('before') ? '::before' : name.endsWith('after') ? '::after' : null;
        return { filter: e => value.test(getComputedStyle(e, pseudo).getPropertyValue(property)) };
      }
      case 'matches-path': {
        const re = pattern(arg);
        return { filter: () => re.test(location.pathname + location.search) };
      }
      case 'upward': {
        const n = Number(arg);
        if (arg.trim() && Number.isFinite(n)) {
          if (!Number.isInteger(n) || n < 1 || n > 256) throw new Error(':upward() takes 1 to 256');
          return { map: e => { for (let k = 0; k < n && e; k++) e = e.parentElement; return e ? [e] : []; } };
        }
        if (!valid(arg)) throw new Error(`bad CSS: ${arg}`);
        return { map: e => { const up = e.parentElement?.closest(arg); return up ? [up] : []; } };
      }
      case 'xpath': {
        document.createExpression(arg);
        return { map: e => {
          const result = document.evaluate(arg, e, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null), found = [];
          for (let k = 0; k < result.snapshotLength; k++) if (result.snapshotItem(k).nodeType === 1) found.push(result.snapshotItem(k));
          return found;
        } };
      }
      case 'has': case '-abp-has': case 'if': {
        const inner = compile(arg, true);
        return { filter: e => run(inner, e, 'within').length > 0 };
      }
      case 'not': case 'if-not': {
        const inner = compile(arg, true);
        return { filter: e => run(inner, e, 'self').length === 0 };
      }
    }
    throw new Error(`:${name}() is not supported`);
  };
  // `within`: the selector looks inside `root`; `self`: it tests `root` itself.
  const run = (compiled, root = document, mode = 'page') => {
    let nodes = null;
    for (const step of compiled.steps) {
      if (nodes === null) {
        if (step.css) {
          if (mode === 'self') nodes = root.matches(step.css) ? [root] : [];
          else if (mode === 'within') nodes = [...root.querySelectorAll(`:scope ${step.css}`)];
          else nodes = [...document.querySelectorAll(step.css)];
          if (!nodes.length) return [];
          continue;
        }
        nodes = mode === 'self' ? [root] : [...(mode === 'within' ? root : document).querySelectorAll('*')];
      }
      if (step.filter) nodes = nodes.filter(e => { try { return step.filter(e); } catch { return false; } });
      else if (step.map) nodes = [...new Set(nodes.flatMap(e => step.map(e)))];
      else if (step.rest) {
        const { combinator, rest } = step;
        if (combinator === '+' || combinator === '~') {
          const found = new Set();
          for (const e of nodes) for (let s = e.nextElementSibling; s; s = combinator === '+' ? null : s.nextElementSibling) if (s.matches(rest)) found.add(s);
          nodes = [...found];
        } else if (combinator) {
          nodes = [...new Set(nodes.flatMap(e => [...e.querySelectorAll(`:scope ${combinator === '>' ? '> ' : ''}${rest}`)]))];
        } else nodes = nodes.filter(e => e.matches(rest));
      }
      if (!nodes.length) return [];
    }
    return nodes ?? [];
  };
  const cache = new Map();
  const compiled = selector => {
    if (!cache.has(selector)) {
      let value;
      try { value = PROCEDURAL.test(selector) ? compile(selector) : (valid(selector) ? { css: selector } : { error: 'not a valid CSS selector' }); }
      catch (error) { value = { error: String(error.message ?? error) }; }
      cache.set(selector, value);
    }
    return cache.get(selector);
  };
  // Every element a rule acts on now, plain or procedural.
  const query = selector => {
    const rule = compiled(selector);
    if (rule.error) return [];
    if (rule.css) return [...matches(rule.css)];
    try { return run(rule); } catch { return []; }
  };
  state.query = query;
  state.check = selector => compiled(selector).error ?? '';

  const declarations = text => text.split(';').map(d => d.trim()).filter(Boolean).map(d => {
    const at = d.indexOf(':');
    return [d.slice(0, at).trim(), d.slice(at + 1).replace(/!\s*important\s*$/i, '').trim()];
  }).filter(([property]) => property);
  // Applies every procedural rule; elements no rule hides any more come back.
  const apply = () => {
    const hide = new Set();
    for (const list of Object.values(state.procedural)) for (const selector of list) {
      const rule = compiled(selector);
      if (rule.error || rule.css) continue;
      let found = [];
      try { found = run(rule); } catch {}
      state.hits[selector] = Math.max(state.hits[selector] ?? 0, found.length);
      const action = rule.action;
      for (const e of found) {
        // The page itself is never hidden or removed, only restyled.
        if ((e === document.documentElement || e === document.body) && (!action || action.type === 'remove')) continue;
        if (!action) { hide.add(e); continue; }
        if (action.type === 'remove') { e.remove(); state.removed++; }
        else if (action.type === 'style') for (const [property, value] of declarations(action.arg)) e.style.setProperty(property, value, 'important');
        else if (action.type === 'remove-attr') { const re = pattern(action.arg, true); for (const n of e.getAttributeNames()) if (re.test(n)) e.removeAttribute(n); }
        else if (action.type === 'remove-class') { const re = pattern(action.arg, true); for (const c of [...e.classList]) if (re.test(c)) e.classList.remove(c); }
      }
    }
    for (const e of state.marked) if (!hide.has(e)) e.removeAttribute(state.mark);
    for (const e of hide) if (!e.hasAttribute(state.mark)) e.setAttribute(state.mark, '');
    state.marked = hide;
  };
  // Procedural rules follow the page as it changes, at most every 150 ms, or
  // less often when a pass is slow.
  const watch = () => {
    if (state.observer || !document.documentElement) return;
    let timer = 0, cost = 0;
    state.observer = new MutationObserver(() => {
      if (timer) return;
      timer = setTimeout(() => { timer = 0; const start = performance.now(); apply(); cost = performance.now() - start; }, Math.max(150, cost * 10));
    });
    state.observer.observe(document.documentElement, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ['class', 'id'] });
  };
  const hidden = () => {
    const found = new Set(state.marked);
    for (const list of Object.values(state.selectors)) for (const selector of list) for (const e of matches(selector)) found.add(e);
    return found.size + state.removed;
  };
  // The document element may not exist yet right after a navigation commits.
  const whenRoot = run => {
    if (document.documentElement) { run(); return; }
    const observer = new MutationObserver(() => { if (document.documentElement) { observer.disconnect(); run(); } });
    observer.observe(document, { childList: true });
  };
  if (args.op === 'style') {
    // Rules with an action come from adblock-rust as JSON; back to filter text.
    const actions = (args.actions ?? []).map(json => {
      try {
        const filter = JSON.parse(json), action = filter.action;
        const text = filter.selector.map(op => op.type === 'css-selector' ? op.arg : `:${op.type}(${op.arg})`).join('');
        return action ? `${text}:${action.type}(${action.type === 'remove' ? '' : action.arg})` : text;
      } catch { return null; }
    });
    const given = [...(args.selectors ?? []), ...actions].filter(s => typeof s === 'string' && s.length < 2000);
    const plain = given.filter(s => compiled(s).css), procedural = given.filter(s => compiled(s).steps);
    state.selectors[args.key] = [...new Set([...(args.append ? state.selectors[args.key] ?? [] : []), ...plain])];
    state.procedural[args.key] = [...new Set([...(args.append ? state.procedural[args.key] ?? [] : []), ...procedural])];
    const css = [`[${state.mark}]{display:none!important}`, ...state.selectors[args.key].map(s => `${s}{display:none!important}`)].join('\n');
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
      apply();
      if (Object.values(state.procedural).some(list => list.length)) watch();
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
    apply();
    return { classes, ids, hidden: hidden() };
  }
  if (args.op === 'count') {
    // How many elements each rule has acted on since the page loaded.
    const counts = {};
    for (const selector of args.selectors ?? []) {
      if (typeof selector !== 'string') continue;
      counts[selector] = compiled(selector).error ? 0 : Math.max(state.hits[selector] ?? 0, query(selector).length);
    }
    return { counts };
  }
  if (args.op === 'preview') {
    for (const e of state.preview) {
      if (!e.isConnected) continue;
      e.style.removeProperty('outline'); e.style.removeProperty('outline-offset'); e.style.removeProperty('background-color');
      e.removeAttribute('data-zitter-preview');
    }
    state.preview = [];
    const rules = [];
    for (const selector of args.selectors ?? []) {
      const error = state.check(selector), found = error ? [] : query(selector);
      rules.push({ selector, matches: found.length, ...(error ? { error } : {}) });
      for (const e of found) {
        if (state.preview.includes(e) || state.preview.length >= 500) continue;
        e.setAttribute('data-zitter-preview', '');
        e.style.setProperty('outline', '2px dashed #6e56cf', 'important');
        e.style.setProperty('outline-offset', '-2px', 'important');
        e.style.setProperty('background-color', 'rgba(110, 86, 207, 0.16)', 'important');
        state.preview.push(e);
      }
    }
    if (state.preview[0]) state.preview[0].scrollIntoView({ block: 'center', inline: 'nearest' });
    return { count: state.preview.length, rules };
  }
  return { error: 'Unknown blocking operation' };
})()
