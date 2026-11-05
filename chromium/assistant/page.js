// Executed only in Zitter's private isolated world. No page script can access the map.
(() => {
  const args = ZITTER_ARGS;
  // Protected inputs: passwords, one-time codes, payment and marked fields. Rich
  // editors (contenteditable) are ordinary content the assistant may read and edit.
  const sensitive = e => !!e.closest('[data-private], [data-sensitive]') ||
    (e.matches('input') && /password|hidden|file/i.test(e.type ?? '')) ||
    /password|passwd|secret|token|credit|card.?number|cvv|cvc|otp|one.?time|cc-/i.test(
      [e.name, e.id, e.autocomplete, e.getAttribute('aria-label')].join(' '));
  const style = e => e.ownerDocument.defaultView.getComputedStyle(e);
  const visible = e => {
    const r = e.getBoundingClientRect(), s = style(e);
    if (r.width <= 0 || r.height <= 0 || s.visibility === 'hidden' || s.display === 'none') return false;
    for (let p = e; p; p = p.parentElement) {
      const parent = style(p);
      if (Number(parent.opacity) === 0 || parent.contentVisibility === 'hidden' || p.hidden) return false;
    }
    return true;
  };
  const label = e => e.getAttribute('aria-label') || e.placeholder || e.title ||
    (e.matches('input,textarea,select') ? e.labels?.[0]?.textContent || '' : e.textContent || '');
  // Documents in reach: the page, its open shadow roots and same-origin frames.
  const roots = () => {
    const found = [];
    const visit = (root, depth) => {
      found.push(root);
      if (depth > 6) return;
      for (const e of root.querySelectorAll('*')) {
        if (e.shadowRoot) visit(e.shadowRoot, depth + 1);
        if (e.tagName === 'IFRAME' || e.tagName === 'FRAME') {
          let inner = null;
          try { inner = e.contentDocument; } catch { inner = null; }
          if (inner?.documentElement) visit(inner, depth + 1);
        }
      }
    };
    visit(document, 0);
    return found;
  };
  // An element's box in the top-level viewport, through same-origin frames.
  const box = e => {
    const r = e.getBoundingClientRect();
    let x = r.x, y = r.y;
    for (let w = e.ownerDocument.defaultView; w && w !== window; w = w.parent) {
      const frame = w.frameElement;
      if (!frame) break;
      const f = frame.getBoundingClientRect();
      x += f.x + frame.clientLeft; y += f.y + frame.clientTop;
    }
    return { x, y, width: r.width, height: r.height };
  };
  // Where a real click on `e` lands: its centre, or another visible spot of it
  // when an overlay covers the centre.
  const point = e => {
    e.scrollIntoView({ block: 'center', inline: 'nearest' });
    const r = e.getBoundingClientRect(), b = box(e);
    const root = e.getRootNode();
    for (const [fx, fy] of [[0.5, 0.5], [0.25, 0.5], [0.75, 0.5], [0.5, 0.25], [0.5, 0.75], [0.15, 0.15]]) {
      const x = r.x + r.width * fx, y = r.y + r.height * fy;
      const hit = (root.elementFromPoint ? root : e.ownerDocument).elementFromPoint(x, y);
      if (hit && (hit === e || e.contains(hit) || hit.contains?.(e)))
        return { x: b.x + r.width * fx, y: b.y + r.height * fy, covered: false };
    }
    return { x: b.x + r.width / 2, y: b.y + r.height / 2, covered: true };
  };
  // The page's main editing surface (a document editor, a large text area), for
  // a native click that gives it the keyboard. Never a protected field; the point
  // must hit the editor itself, low in its visible part, away from overlay chips.
  const editorPoint = () => {
    let best = null, area = 0;
    for (const e of document.querySelectorAll('[contenteditable=""],[contenteditable="true"],textarea,[role="textbox"],.kix-appview-editor')) {
      if (!visible(e) || e.closest('[data-private],[data-sensitive]')) continue;
      const r = e.getBoundingClientRect();
      const left = Math.max(r.left, 0), right = Math.min(r.right, innerWidth), top = Math.max(r.top, 0), bottom = Math.min(r.bottom, innerHeight);
      if (right - left < 40 || bottom - top < 24 || (right - left) * (bottom - top) <= area) continue;
      for (const y of [bottom - 24, (top + bottom) / 2, top + 12]) {
        const x = (left + right) / 2, hit = document.elementFromPoint(x, y);
        if (hit && (e === hit || e.contains(hit))) { best = { x, y }; area = (right - left) * (bottom - top); break; }
      }
    }
    return best;
  };
  // The page shows its content as pixels: a known canvas editor, or canvases
  // covering a quarter of the viewport.
  const canvasContent = () => {
    if (document.querySelector('.kix-appview-editor, .waffle-canvas, #docs-editor canvas, .punch-viewer-content')) return true;
    let area = 0;
    for (const c of document.querySelectorAll('canvas')) {
      const r = c.getBoundingClientRect();
      area += Math.max(0, Math.min(r.right, innerWidth) - Math.max(r.left, 0)) * Math.max(0, Math.min(r.bottom, innerHeight) - Math.max(r.top, 0));
    }
    return area > innerWidth * innerHeight / 4;
  };
  if (args.tool === 'editor_point') return editorPoint() ?? {};
  if (args.tool === 'focus_editor') {
    // Natively requested when a click did not give an editor the keyboard: an
    // editable frame document (Google Docs reads keys there), else the largest editor.
    let best = null, area = -1;
    for (const root of roots()) {
      if (root.nodeType === 9 && root !== document && (root.designMode === 'on' || root.body?.isContentEditable)) { best = root.body; break; }
      for (const e of root.querySelectorAll('[contenteditable=""],[contenteditable="true"],[role="textbox"]')) {
        if (e.closest('[data-private],[data-sensitive]') || !e.isContentEditable) continue;
        const r = e.getBoundingClientRect(), size = r.width * r.height + (e.ownerDocument !== document ? 1e9 : 0);
        if (size > area) { area = size; best = e; }
      }
    }
    if (!best) return { ok: false };
    best.ownerDocument.defaultView.frameElement?.focus();
    best.focus();
    return { ok: true };
  }
  const interactive = 'a[href],button,input,textarea,select,summary,label[for],[contenteditable=""],[contenteditable="true"],' +
    '[role="button"],[role="link"],[role="menuitem"],[role="menuitemcheckbox"],[role="menuitemradio"],[role="tab"],' +
    '[role="checkbox"],[role="radio"],[role="switch"],[role="option"],[role="combobox"],[role="treeitem"],[role="textbox"],' +
    '[role="listbox"],[role="slider"],[role="searchbox"],[onclick],[tabindex]:not([tabindex="-1"])';
  if (args.tool === 'read_page') {
    const map = new Map(), candidates = [], seen = new Set();
    // getRandomValues also works on ordinary HTTP pages; randomUUID requires HTTPS.
    const snapshot = Array.from(crypto.getRandomValues(new Uint32Array(4)), n => n.toString(16)).join('-');
    const all = roots();
    for (const root of all) for (const e of root.querySelectorAll(interactive)) {
      if (seen.has(e) || candidates.length >= 2000) continue;
      seen.add(e);
      if (!visible(e) || sensitive(e)) continue;
      const b = box(e);
      candidates.push({ e, b, inView: b.y + b.height > 0 && b.y < innerHeight && b.x + b.width > 0 && b.x < innerWidth });
    }
    // What is on screen comes first; the rest fills the remaining slots in page order.
    candidates.sort((a, b) => (b.inView - a.inView));
    const elements = [];
    for (const { e, b, inView } of candidates.slice(0, 300)) {
      const id = `${snapshot}:${elements.length}`;
      map.set(id, e);
      const item = { id, tag: e.tagName, role: e.getAttribute('role') ?? '', label: label(e).replace(/\s+/g, ' ').trim().slice(0, 160),
        type: e.type ?? '', in_view: inView, x: Math.round(b.x), y: Math.round(b.y),
        confirmation: !e.matches('input:not([type=submit]):not([type=button]):not([type=checkbox]):not([type=radio]),textarea,select,[contenteditable=""],[contenteditable="true"]') };
      if (e.isContentEditable) item.editable = true;
      if (e.matches('input,textarea') && !sensitive(e)) item.value = String(e.value ?? '').slice(0, 200);
      if (e.matches('input[type=checkbox],input[type=radio]')) item.checked = e.checked;
      for (const aria of ['checked', 'selected', 'expanded', 'pressed', 'disabled'])
        if (e.hasAttribute(`aria-${aria}`)) item[aria] = e.getAttribute(`aria-${aria}`);
      if (e.tagName === 'SELECT') {
        item.value = e.value;
        item.options = Array.from(e.options).slice(0, 40).map(o => ({ value: o.value, text: o.text.trim().slice(0, 80), selected: o.selected }));
      }
      elements.push(item);
    }
    globalThis.__zitterAssistant = { map, url: location.href };
    let text = '';
    const skip = 'script,style,noscript,input,textarea,select,[data-private],[data-sensitive]';
    for (const root of all) {
      const start = root.body ?? root.documentElement ?? root;
      if (!start) continue;
      const walker = (root.ownerDocument ?? root).createTreeWalker(start, NodeFilter.SHOW_TEXT);
      let node;
      while ((node = walker.nextNode()) && text.length < 24000) {
        const p = node.parentElement;
        if (!p || p.closest(skip) || sensitive(p) || !visible(p)) continue;
        text += node.textContent.replace(/\s+/g, ' ') + ' ';
      }
    }
    // Editors such as Google Docs draw their text; insert_text can still type there.
    return { url: location.href, title: document.title, text: text.slice(0, 24000), elements,
             more_elements: Math.max(0, candidates.length - elements.length),
             scroll: { y: Math.round(scrollY), height: document.documentElement.scrollHeight, viewport: innerHeight },
             editor: !!editorPoint(), canvas: canvasContent() };
  }
  if (args.tool === 'adblock_inspect') {
    // Ad candidates and third-party requests for writing blocking rules
    // (docs/ad-blocking.md). Selectors avoid tokens that look generated.
    const words = /(^|[^a-z])(ads?|adv|advert\w*|adslot|adunit|adbox|adsense|adsbygoogle|banners?|sponsor\w*|promo\w*|dfp|gpt|doubleclick|taboola|outbrain|teads|criteo|affiliate|commercial|reklam\w*|partner-?content|native-?ad)([^a-z]|$)/i;
    const randomish = token => /\d{4,}|[0-9a-f]{8,}|^[a-z0-9]{14,}$|^_|__/i.test(token) || token.length > 40;
    const css = token => CSS.escape(token);
    const site = location.hostname.split('.').slice(-2).join('.');
    const thirdParty = url => { try { const h = new URL(url, location.href).hostname; return !!h && h !== site && !h.endsWith('.' + site); } catch { return false; } };
    const blocking = globalThis.__zitterBlocking;
    const zitterHides = e => !!blocking && Object.values(blocking.selectors).some(list => list.some(s => { try { return e.matches(s); } catch { return false; } }));
    const stable = e => {
      const tag = e.tagName.toLowerCase();
      if (e.id && !randomish(e.id)) return `${tag}#${css(e.id)}`;
      const classes = Array.from(e.classList).filter(c => !randomish(c)).slice(0, 3);
      if (classes.length) return tag + classes.map(c => '.' + css(c)).join('');
      for (const name of e.getAttributeNames())
        if (/^(data-ad|data-google|data-testid|aria-label|data-slot|data-type|name)/i.test(name)) {
          const value = e.getAttribute(name);
          if (value && value.length < 60 && !randomish(value)) return `${tag}[${name}="${value.replace(/["\\]/g, '\\$&')}"]`;
        }
      if (tag === 'iframe' && e.src) { try { return `iframe[src*="${new URL(e.src).hostname}"]`; } catch {} }
      return null;
    };
    const unique = e => {
      const path = [];
      for (let n = e; n && n.nodeType === 1 && n !== document.documentElement; n = n.parentElement) {
        if (n.id && !randomish(n.id)) { path.unshift('#' + css(n.id)); break; }
        let index = 1;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) if (s.tagName === n.tagName) index++;
        path.unshift(`${n.tagName.toLowerCase()}:nth-of-type(${index})`);
        if (path.length > 8) break;
      }
      return path.join(' > ');
    };
    const count = selector => { try { return document.querySelectorAll(selector).length; } catch { return 0; } };
    const sizes = ['300x250', '728x90', '160x600', '320x50', '300x600', '970x250', '970x90', '336x280', '468x60', '320x100'];
    const candidates = [], seen = new Set();
    const consider = (e, reason) => {
      if (candidates.length >= 60 || seen.has(e)) return;
      for (const s of seen) if (s.contains(e)) return;
      seen.add(e);
      const r = e.getBoundingClientRect(), hiddenByZitter = zitterHides(e);
      if (!hiddenByZitter && (r.width < 20 || r.height < 15)) return;
      const selector = stable(e);
      const item = { selector: selector ?? unique(e), unique: unique(e), reason, tag: e.tagName.toLowerCase(),
        size: `${Math.round(r.width)}x${Math.round(r.height)}`, x: Math.round(r.x), y: Math.round(r.y + scrollY),
        text: (e.innerText ?? '').replace(/\s+/g, ' ').trim().slice(0, 80), hidden_by_zitter: hiddenByZitter };
      item.matches = count(item.selector);
      const source = e.src || e.querySelector?.('iframe[src],img[src]')?.src;
      if (source) item.source = String(source).split('?')[0].slice(0, 200);
      candidates.push(item);
    };
    let scanned = 0;
    for (const e of document.body?.querySelectorAll('*') ?? []) {
      if (++scanned > 8000) break;
      if (e.closest('[data-private],[data-sensitive]')) continue;
      const tag = e.tagName;
      if (tag === 'IFRAME' && e.src && thirdParty(e.src)) { consider(e, 'third-party iframe'); continue; }
      if (tag === 'INS' && e.classList.contains('adsbygoogle')) { consider(e, 'ad slot'); continue; }
      const names = [e.id, typeof e.className === 'string' ? e.className : '', e.getAttribute('aria-label') ?? '',
                     ...e.getAttributeNames().filter(n => n.startsWith('data-'))].join(' ');
      if (words.test(names)) { consider(e, 'ad-like name'); continue; }
      const r = e.getBoundingClientRect();
      if (sizes.includes(`${Math.round(r.width)}x${Math.round(r.height)}`) && e.querySelector('iframe,img,a[href]')) { consider(e, 'standard ad size'); continue; }
      const s = getComputedStyle(e);
      if ((s.position === 'fixed' || s.position === 'sticky') && Number(s.zIndex) > 100 && r.width * r.height > innerWidth * innerHeight * 0.2)
        consider(e, 'overlay over the page');
    }
    const requests = [], hosts = {};
    for (const entry of performance.getEntriesByType('resource')) {
      if (!thirdParty(entry.name)) continue;
      let host = '';
      try { host = new URL(entry.name).hostname; } catch { continue; }
      hosts[host] = (hosts[host] ?? 0) + 1;
      if (requests.length < 150) requests.push({ url: entry.name.split('?')[0].slice(0, 200), type: entry.initiatorType });
    }
    return { url: location.href.split('?')[0], title: document.title, candidates, third_party_hosts: hosts, requests,
             note: 'Candidates are guesses: keep only what the user would call an ad, never the content they came for.' };
  }
  if (args.tool === 'focus_info') {
    // Natively requested before keyboard input: where would typing land?
    let e = document.activeElement, opaque = false;
    for (let depth = 0; e && depth < 8; depth++) {
      if (e.shadowRoot?.activeElement) { e = e.shadowRoot.activeElement; continue; }
      if (e.tagName !== 'IFRAME') break;
      let inner = null;
      try { inner = e.contentDocument?.activeElement; } catch { inner = null; }
      if (!inner) { opaque = true; break; }
      e = inner;
    }
    const secret = !!e && (!!e.closest?.('[data-private], [data-sensitive]') || (e.matches?.('input') &&
      (/password|hidden|file/i.test(e.type ?? '') ||
       /password|passwd|secret|token|credit|card.?number|cvv|cvc|otp|one.?time|cc-/i.test(
         [e.name, e.id, e.autocomplete, e.getAttribute('aria-label')].join(' ')))));
    const field = !!e?.matches?.('input,textarea') && !e.disabled && !e.readOnly;
    return { secret, editable: opaque || !!e?.isContentEditable || field,
             rich: opaque || !!e?.isContentEditable || !!e?.matches?.('textarea') };
  }
  if (args.tool === 'scroll' && !args.element) {
    const dy = Math.max(-5000, Math.min(5000, Number(args.dy) || 0)), dx = Math.max(-5000, Math.min(5000, Number(args.dx) || 0));
    scrollBy(dx, dy);
    return { ok: true, y: Math.round(scrollY), height: document.documentElement.scrollHeight };
  }
  const state = globalThis.__zitterAssistant;
  const e = state?.url === location.href ? state.map.get(args.element) : null;
  if (!e?.isConnected || !visible(e) || sensitive(e)) return { error: 'Element expired, hidden or protected. Read the page again.' };
  if (args.tool === 'rect') {
    // Natively requested for the consent bubble only; brings the element into view.
    e.scrollIntoView({ block: 'center', inline: 'nearest' });
    return box(e);
  }
  if (args.tool === 'scroll') {
    // Scroll inside a list or panel: the element itself, or into view.
    const dy = Number(args.dy) || 0, dx = Number(args.dx) || 0;
    if (dy || dx) e.scrollBy(Math.max(-5000, Math.min(5000, dx)), Math.max(-5000, Math.min(5000, dy)));
    else e.scrollIntoView({ block: 'center', inline: 'nearest' });
    return { ok: true, scroll_top: Math.round(e.scrollTop), scroll_height: e.scrollHeight };
  }
  if (e.tagName === 'A' && ['click', 'mouse'].includes(args.tool)) {
    let protocol = '';
    try { protocol = new URL(e.href, location.href).protocol; } catch { protocol = ''; }
    if (!/^https?:$/.test(protocol)) return { error: 'Only HTTP(S) links are allowed' };
  }
  if (args.tool === 'point') return { ...point(e), label: label(e).replace(/\s+/g, ' ').trim().slice(0, 120) };
  if (args.tool === 'click') {
    if (!args.confirmed)
      return { confirmation_required: true, label: (label(e).replace(/\s+/g, ' ').trim() || 'Click').slice(0, 120) };
    // A real mouse click at a visible spot of the element; a covered element
    // still gets a scripted click.
    const at = point(e);
    if (!at.covered) return { point: { x: at.x, y: at.y } };
    e.click(); return { ok: true, detail: 'The element was covered; sent a scripted click instead' };
  }
  if (args.tool === 'type') {
    if (typeof args.text !== 'string' || args.text.length > 16000) return { error: 'Text of up to 16000 characters is required' };
    if (e.isContentEditable) {
      // Rich editors take text only as keyboard input: select their content, then type.
      e.focus();
      const range = e.ownerDocument.createRange(); range.selectNodeContents(e);
      const selection = e.ownerDocument.getSelection(); selection.removeAllRanges(); selection.addRange(range);
      return { keyboard: true };
    }
    if (!e.matches('input,textarea') || e.disabled || e.readOnly)
      return { error: 'Not an editable, non-sensitive field' };
    const prototype = e.tagName === 'TEXTAREA' ? e.ownerDocument.defaultView.HTMLTextAreaElement.prototype : e.ownerDocument.defaultView.HTMLInputElement.prototype;
    e.focus();
    Object.getOwnPropertyDescriptor(prototype, 'value').set.call(e, args.text);
    e.dispatchEvent(new Event('input', { bubbles: true })); e.dispatchEvent(new Event('change', { bubbles: true }));
    return { ok: true };
  }
  if (args.tool === 'select') {
    if (e.tagName !== 'SELECT') return { error: 'Not a <select>; click it, read the page and click the option instead' };
    if (e.disabled) return { error: 'The list is disabled' };
    const wanted = String(args.value ?? '').trim().toLowerCase();
    const option = Array.from(e.options).find(o => o.value.toLowerCase() === wanted) ??
                   Array.from(e.options).find(o => o.text.trim().toLowerCase() === wanted) ??
                   Array.from(e.options).find(o => o.text.trim().toLowerCase().includes(wanted));
    if (!wanted || !option || option.disabled) return { error: 'No such option', options: Array.from(e.options).slice(0, 40).map(o => o.text.trim()) };
    e.value = option.value;
    e.dispatchEvent(new Event('input', { bubbles: true })); e.dispatchEvent(new Event('change', { bubbles: true }));
    return { ok: true, value: option.value, text: option.text.trim() };
  }
  return { error: 'Unknown page operation' };
})()
