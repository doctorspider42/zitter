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
  // `:remove-attr()`, `:remove-class()`. One is Zitter's own: `:min-size(w, h)`,
  // an element at least that big (ads keep their slot sizes when class names
  // and URLs are generated).
  const OPERATORS = new Set(['has-text', 'contains', '-abp-contains', 'upward', 'matches-attr', 'matches-css', 'matches-css-before',
                             'matches-css-after', 'matches-path', 'min-text-length', 'min-size', 'xpath', 'has', '-abp-has', 'if', 'not', 'if-not']);
  const ACTIONS = new Set(['remove', 'style', 'remove-attr', 'remove-class', 'mask']);
  const PROCEDURAL = /:(has-text|contains|-abp-contains|-abp-has|upward|matches-attr|matches-css(-before|-after)?|matches-path|min-text-length|min-size|xpath|if|if-not|remove|style|remove-attr|remove-class|mask)\(/;
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
  // Zitter's own `:mask(x, y, width, height)`: a region of an element, in CSS
  // pixels or percents of its box, for ads painted inside a canvas or video.
  const region = text => {
    const parts = text.split(',').map(part => part.trim());
    if (parts.length !== 4 || !parts.every(part => /^\d+(\.\d+)?(px|%)?$/.test(part))) throw new Error(':mask() needs x, y, width, height');
    return parts.map(part => ({ value: parseFloat(part), percent: part.endsWith('%') }));
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
    if (action?.type === 'mask') action.box = region(action.arg);
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
      case 'min-size': {
        const [width, height] = arg.split(',').map(n => Number(n.trim()));
        if (!(width >= 0 && height >= 0)) throw new Error(':min-size() needs width, height');
        // An inline box is as tall as its line, not its image: measure what it holds.
        const size = e => {
          const r = e.getBoundingClientRect();
          if (getComputedStyle(e).display !== 'inline') return r;
          let w = r.width, h = r.height;
          for (const c of e.children) { const b = size(c); w = Math.max(w, b.width); h = Math.max(h, b.height); }
          return { width: w, height: h };
        };
        return { layout: true, filter: e => { const r = size(e); return r.width >= width && r.height >= height; } };
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
        return { layout: true, filter: e => value.test(getComputedStyle(e, pseudo).getPropertyValue(property)) };
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
  // A rule that looks at layout (size, computed style) no longer sees what it
  // hid, so those elements stay hidden while they are in the page.
  const apply = () => {
    const hide = new Set();
    const masks = new Set();
    state.sticky ??= new Map();
    for (const list of Object.values(state.procedural)) for (const selector of list) {
      const rule = compiled(selector);
      if (rule.error || rule.css) continue;
      let found = [];
      try { found = run(rule); } catch {}
      if (!rule.action && rule.steps.some(step => step.layout)) {
        const kept = state.sticky.get(selector) ?? new Set();
        for (const e of kept) if (!e.isConnected) kept.delete(e);
        for (const e of found) kept.add(e);
        state.sticky.set(selector, kept);
        found = [...kept];
      }
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
        else if (action.type === 'mask') masks.add({ element: e, box: action.box });
      }
    }
    // The slot an ad leaves goes too: a parent with no text of its own whose
    // every child is hidden or empty (no words but an ad label, no media
    // bigger than the slot's own chrome: a label badge, AdChoices, a close
    // button), up to six levels, never the page's landmarks.
    // Measured once the ads are hidden, so what they held has no size.
    for (const e of state.marked) if (!hide.has(e)) e.removeAttribute(state.mark);
    for (const e of hide) if (!e.hasAttribute(state.mark)) e.setAttribute(state.mark, '');
    const collapsed = new Set();
    const label = /^\s*(reklama|autopromocja|advertisement|anzeige|sponsored|ad|ads)?\s*$/i;
    const media = 'img,picture,video,canvas,iframe,svg,embed,object';
    const empty = c => label.test(c.textContent ?? '') &&
      ![c, ...c.querySelectorAll(media)].some(m => { const r = m.getBoundingClientRect(); return m.matches(media) && (r.width > 160 || r.height > 60); });
    const gone = c => hide.has(c) || collapsed.has(c) || c.matches('script,style,template,link,meta,noscript') ||
                      getComputedStyle(c).display === 'none' || empty(c);
    for (const e of hide) {
      for (let p = e.parentElement, k = 0; p && k < 6 && p !== document.body && p !== document.documentElement; p = p.parentElement, k++) {
        if (collapsed.has(p) || p.matches('main,header,nav,article,footer,aside,form,[role=main]')) break;
        if ([...p.childNodes].some(n => n.nodeType === 3 && n.nodeValue.trim())) break;
        if (![...p.children].every(gone)) break;
        collapsed.add(p);
      }
    }
    for (const e of collapsed) e.setAttribute(state.mark, '');
    state.maskList = [...masks];
    place();
    state.marked = new Set([...hide, ...collapsed]);
    state.counted = hide;
  };
  // Masks: opaque boxes over their regions, in the page's color, following
  // scrolling and resizing. They take no clicks.
  state.masks ??= new Map();
  const place = () => {
    const wanted = new Set();
    for (const { element, box } of state.maskList ?? []) {
      if (!element.isConnected) continue;
      const key = `${box.map(b => b.value + (b.percent ? '%' : '')).join(',')}`;
      let byKey = state.masks.get(element);
      if (!byKey) state.masks.set(element, byKey = new Map());
      let cover = byKey.get(key);
      if (!cover) {
        cover = document.createElement('div');
        cover.setAttribute('data-zitter', 'mask');
        byKey.set(key, cover);
      }
      wanted.add(cover);
      const r = element.getBoundingClientRect(), [x, y, w, h] = box.map((b, i) => b.percent ? b.value / 100 * (i % 2 ? r.height : r.width) : b.value);
      const color = getComputedStyle(document.body ?? document.documentElement).backgroundColor;
      cover.style.cssText = `position:absolute!important;left:${r.left + scrollX + x}px!important;top:${r.top + scrollY + y}px!important;` +
        `width:${w}px!important;height:${h}px!important;background:${color === 'rgba(0, 0, 0, 0)' ? '#fff' : color}!important;` +
        'z-index:2147483646!important;pointer-events:none!important;display:block!important;margin:0!important;border:0!important';
      if (cover.parentNode !== document.documentElement) document.documentElement.append(cover);
    }
    for (const [element, byKey] of state.masks) for (const [key, cover] of byKey)
      if (!wanted.has(cover)) { cover.remove(); byKey.delete(key); if (!byKey.size) state.masks.delete(element); }
    if (wanted.size && !state.following) {
      state.following = true;
      let frame = 0;
      const again = () => { if (!frame) frame = requestAnimationFrame(() => { frame = 0; place(); }); };
      addEventListener('scroll', again, true);
      addEventListener('resize', again);
    }
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
    const found = new Set(state.counted ?? []);
    for (const list of Object.values(state.selectors)) for (const selector of list) for (const e of matches(selector)) found.add(e);
    return found.size + state.removed;
  };
  // The document element may not exist yet right after a navigation commits.
  const whenRoot = run => {
    if (document.documentElement) { run(); return; }
    const observer = new MutationObserver(() => { if (document.documentElement) { observer.disconnect(); run(); } });
    observer.observe(document, { childList: true });
  };
  const resetCosmetics = () => {
    for (const sheet of Object.values(state.sheets)) sheet.remove();
    for (const e of state.marked) e.removeAttribute(state.mark);
    for (const byKey of state.masks.values()) for (const cover of byKey.values()) cover.remove();
    state.sheets = {};
    state.selectors = {};
    state.procedural = {};
    state.classes.clear();
    state.ids.clear();
    state.sticky?.clear();
    state.marked.clear();
    state.masks.clear();
    state.maskList = [];
    state.counted = new Set();
    state.hits = {};
    state.removed = 0;
    // DOM removal and imperative actions need a reload to restore their input.
  };
  if (args.op === 'reset_cosmetics') {
    resetCosmetics();
    return { hidden: 0 };
  }
  if (args.op === 'style') {
    if (args.reset) resetCosmetics();
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
    if (args.reset && !args.read_only) { state.classes.clear(); state.ids.clear(); state.selectors.generic = []; }
    const classes = [], ids = [];
    const knownClasses = args.read_only ? new Set() : state.classes;
    const knownIds = args.read_only ? new Set() : state.ids;
    for (const e of document.querySelectorAll('[class],[id]')) {
      if (classes.length + ids.length > 4000) break;
      for (const name of e.classList ?? []) if (!knownClasses.has(name)) { knownClasses.add(name); classes.push(name); }
      if (e.id && !knownIds.has(e.id)) { knownIds.add(e.id); ids.push(e.id); }
    }
    if (!args.read_only) apply();
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
  if (args.op === 'check' || args.op === 'assistant_preview') {
    const targets = new Set();
    const rules = (args.selectors ?? []).map(selector => {
      const error = state.check(selector);
      const matches = error ? [] : query(selector);
      const protectedMatch = matches.some(e =>
        e.closest('[data-private],[data-sensitive],input,textarea,select,[contenteditable]') ||
        e.querySelector('[data-private],[data-sensitive],input,textarea,select,[contenteditable]'));
      if (args.op === 'assistant_preview' && !protectedMatch) {
        for (const e of matches) {
          if (targets.size === 500) break;
          if (e.getClientRects().length) targets.add(e);
        }
      }
      return { matches: matches.length, valid: !error && !protectedMatch };
    });
    if (!rules.every(rule => rule.valid && rule.matches)) return { rules };
    if (args.op === 'check') {
      if (args.token) state.assistantPreviewToken = args.token;
      return { rules };
    }
    if (!targets.size || typeof args.token !== 'string' ||
        state.assistantPreviewToken !== args.token || location.href !== args.url) return { rules };
    state.assistantPreviewToken = null;
    state.assistantPreview?.clear();
    const markerId = Array.from(crypto.getRandomValues(new Uint32Array(4)), n => n.toString(16)).join('-');
    const mark = 'data-zitter-assistant-preview-' + markerId;
    const sheet = new CSSStyleSheet();
    sheet.replaceSync(`[${mark}]{outline:2px dashed #6e56cf!important;outline-offset:-2px!important}`);
    const clear = () => {
      document.adoptedStyleSheets = document.adoptedStyleSheets.filter(current => current !== sheet);
      for (const e of targets) e.removeAttribute(mark);
      window.removeEventListener('pagehide', clear);
      document.removeEventListener('visibilitychange', onVisibility);
      state.assistantPreview = null;
    };
    const onVisibility = () => { if (document.visibilityState === 'hidden') clear(); };
    state.assistantPreview = { token: args.token, clear };
    window.addEventListener('pagehide', clear, { once: true });
    document.addEventListener('visibilitychange', onVisibility);
    document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet];
    for (const e of targets) e.setAttribute(mark, '');
    const inViewport = e => {
      const box = e.getBoundingClientRect();
      return box.right > 0 && box.bottom > 0 && box.left < innerWidth && box.top < innerHeight;
    };
    if (![...targets].some(inViewport))
      targets.values().next().value.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'instant' });
    return { count: targets.size, rules };
  }
  if (args.op === 'clear_assistant_preview') {
    if (state.assistantPreviewToken === args.token) state.assistantPreviewToken = null;
    if (state.assistantPreview?.token === args.token) state.assistantPreview.clear();
    return {};
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
