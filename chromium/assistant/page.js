// Executed only in Zitter's private isolated world. No page script can access the map.
(() => {
  const args = ZITTER_ARGS;
  // Protected inputs: passwords, one-time codes, payment and marked fields. Rich
  // editors (contenteditable) are ordinary content the assistant may read and edit.
  const markedPrivate = e => {
    for (let node = e; node;) {
      if (node.closest('[data-private], [data-sensitive]')) return true;
      const root = node.getRootNode();
      node = root.host ?? node.ownerDocument.defaultView?.frameElement;
    }
    return false;
  };
  const sensitive = e => markedPrivate(e) ||
    (e.matches('input') && /password|hidden|file/i.test(e.type ?? '')) ||
    /password|passwd|secret|token|credit|card.?number|cvv|cvc|otp|one.?time|cc-/i.test(
      [e.name, e.id, e.autocomplete, e.getAttribute('aria-label')].join(' '));
  const activeEditor = () => {
    let e = document.activeElement;
    for (let depth = 0; e && depth < 8; ++depth) {
      if (e.shadowRoot?.activeElement) {
        e = e.shadowRoot.activeElement;
      } else if (e.matches('iframe,frame')) {
        e = e.contentDocument?.activeElement;
      } else {
        return e;
      }
    }
    return null;
  };
  const containsProtectedField = e =>
    [...e.querySelectorAll('[data-private],[data-sensitive],input,textarea')].some(sensitive);
  const insertInEditor = (e, text, replace) => {
    if (!e || sensitive(e) || !visible(e) ||
        !(e.isContentEditable || e.matches('textarea,input[type=text],input:not([type]),input[type=search],input[type=tel],input[type=url]')) ||
        e.disabled || e.readOnly ||
        (e.isContentEditable && containsProtectedField(e)))
      return { error: 'The editor is unavailable or protected.' };
    if (replace) {
      e.focus();
      if (activeEditor() !== e || sensitive(e) || !e.isContentEditable || containsProtectedField(e))
        return { error: 'The editor focus changed.' };
      const range = e.ownerDocument.createRange();
      range.selectNodeContents(e);
      const selection = e.ownerDocument.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
    }
    const selection = e.ownerDocument.getSelection();
    if (e.isContentEditable &&
        (!selection?.rangeCount || !e.contains(selection.anchorNode) || !e.contains(selection.focusNode)))
      return { error: 'The selection is outside the editor.' };
    if (activeEditor() !== e || sensitive(e))
      return { error: 'The editor focus changed.' };
    const ok = replace && !text
      ? e.ownerDocument.execCommand('delete')
      : e.ownerDocument.execCommand('insertText', false, text);
    return ok ? { ok: true } : { error: 'The editor did not accept text.' };
  };
  const style = e => e.ownerDocument.defaultView.getComputedStyle(e);
  if (args.tool === 'night_shift_inspect') {
    const candidates = [];
    const seen = new Set();
    const identifier = /^[a-z_][\w-]{0,63}$/i;
    const validTag = /^[a-z][a-z0-9-]{0,31}$/;
    const containers = 'article,section,main,aside,nav,header,footer';
    const parseRgb = value => {
      const match = /^rgba?\((\d+(?:\.\d+)?), (\d+(?:\.\d+)?), (\d+(?:\.\d+)?)(?:, (\d+(?:\.\d+)?))?\)$/.exec(value);
      return match ? [...match.slice(1, 4).map(Number), Number(match[4] ?? 1)] : null;
    };
    const luminance = color => color.slice(0, 3).map(channel => {
      const normalized = channel / 255;
      return normalized <= 0.04045 ? normalized / 12.92 : ((normalized + 0.055) / 1.055) ** 2.4;
    }).reduce((sum, value, index) => sum + value * [0.2126, 0.7152, 0.0722][index], 0);
    const textContrast = (element, foreground) => {
      // Restrict scoring to text on an opaque, flat surface. Keep sampling
      // style identifiers and colors for everything else without a score.
      if (!foreground || foreground[3] !== 1 ||
          ![...element.childNodes].some(node => node.nodeType === Node.TEXT_NODE && /\S/.test(node.nodeValue))) return null;
      let surface = null;
      for (let current = element; current; current = current.parentElement) {
        const css = style(current);
        if (css.opacity !== '1' || css.filter !== 'none' || css.mixBlendMode !== 'normal') return null;
        if (surface) continue;
        if (css.backgroundImage !== 'none') return null;
        const color = parseRgb(css.backgroundColor);
        if (!color || (color[3] !== 0 && color[3] !== 1)) return null;
        if (color[3] === 1) surface = color;
      }
      if (!surface) return null;
      const a = luminance(foreground);
      const b = luminance(surface);
      return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
    };
    const consider = element => {
      if (!element || seen.has(element) || candidates.length >= 48 ||
          sensitive(element) || element.closest('form, [contenteditable]') ||
          !validTag.test(element.localName)) return;
      seen.add(element);
      const tag = element.localName;
      const id = identifier.test(element.id) ? element.id : null;
      const classes = id ? [] : [...element.classList].filter(name => identifier.test(name)).slice(0, 3);
      if (!id && classes.length === 0 && !element.matches(containers + ',html,body,figcaption,p,span')) return;
      const css = style(element);
      let surface = element;
      let background = css.backgroundColor;
      while (surface.parentElement && background === 'rgba(0, 0, 0, 0)') {
        surface = surface.parentElement;
        background = style(surface).backgroundColor;
      }
      const ratio = textContrast(element, parseRgb(css.color));
      candidates.push({
        target: candidates.length, path: [{tag, id, classes}],
        color: css.color, background, ownBackground: css.backgroundColor,
        contrastRatio: ratio === null ? null : Math.round(ratio * 100) / 100,
      });
    };
    consider(document.body);
    for (let row = 1; row <= 5; row++) {
      for (let column = 1; column <= 7; column++) {
        for (const hit of document.elementsFromPoint(innerWidth * column / 8, innerHeight * row / 6).slice(0, 4)) {
          consider(hit);
        }
      }
    }
    // Short captions can lie between sampled points.
    for (const element of [...document.querySelectorAll('figcaption, [role="note"], caption')].slice(0, 20)) {
      const bounds = element.getBoundingClientRect();
      if (bounds.width > 0 && bounds.height > 0 && bounds.bottom > 0 && bounds.top < innerHeight) {
        consider(element);
      }
    }
    return {
      candidates,
      lowContrastTargets: candidates.filter(candidate => candidate.contrastRatio !== null && candidate.contrastRatio < 4.5)
        .sort((a, b) => a.contrastRatio - b.contrastRatio).map(candidate => candidate.target),
    };
  }
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
  if (args.tool === 'debug_dom' || args.tool === 'debug_edit_dom') {
    // Keep private subtrees private across open shadow roots and same-origin frames.
    const protectedNode = node => {
      for (let e = node; e;) {
        if (sensitive(e) || e.matches('script,style,link,meta,base,object,embed,input,textarea,select,option')) return true;
        const root = e.getRootNode();
        e = root.host ?? e.ownerDocument.defaultView?.frameElement;
      }
      return false;
    };
    const matches = [];
    try {
      for (const root of roots()) {
        for (const node of root.querySelectorAll(args.selector)) {
          if (!protectedNode(node)) matches.push(node);
          if (matches.length >= 13) break;
        }
        if (matches.length >= 13) break;
      }
    } catch {
      return {error: 'Invalid CSS selector'};
    }
    if (args.tool === 'debug_dom') {
      const styles = ['display', 'visibility', 'opacity', 'position', 'width', 'height',
        'margin', 'padding', 'color', 'background-color', 'font-size', 'overflow', 'z-index'];
      return {matches: matches.slice(0, 12).map(e => {
        const computed = style(e);
        const text = visible(e) ? Array.from(e.childNodes).filter(n => n.nodeType === Node.TEXT_NODE)
          .map(n => n.nodeValue).join(' ').trim().slice(0, 400) : '';
        return {tag: e.localName, id: (e.id || '').slice(0, 120), classes: (e.getAttribute('class') || '').slice(0, 200),
          role: (e.getAttribute('role') || '').slice(0, 80), text, visible: visible(e), bounds: box(e),
          styles: Object.fromEntries(styles.map(name => [name, computed.getPropertyValue(name)]))};
      }), truncated: matches.length > 12};
    }
    if (matches.length !== 1) return {error: 'A DOM edit requires exactly one unprotected matching element'};
    const e = matches[0];
    if (e.matches('html,head,body,iframe,frame,form') || e.closest('[contenteditable]'))
      return {error: 'This element cannot be edited'};
    const value = args.value;
    if (args.operation === 'text' && typeof value === 'string') {
      if (e.childElementCount) return {error: 'Text edits require a leaf element'};
      e.textContent = value;
    } else if (args.operation === 'style' && typeof value === 'string' && e.style) {
      const editable = /^(?:color|background-color|display|visibility|opacity|position|(?:min-|max-)?(?:width|height)|(?:margin|padding)(?:-(?:top|right|bottom|left))?|font-size|font-weight|line-height|text-align|overflow(?:-[xy])?|z-index|border-(?:color|style|width|radius)|gap|align-items|justify-content)$/;
      if (!editable.test(args.property) || (value && !CSS.supports(args.property, value)))
        return {error: 'Unsupported style property or value'};
      if (value) e.style.setProperty(args.property, value);
      else e.style.removeProperty(args.property);
    } else if (['add_class', 'remove_class'].includes(args.operation) && typeof value === 'string' && /^[\w-]{1,100}$/.test(value)) {
      if (args.operation === 'add_class') e.classList.add(value);
      else e.classList.remove(value);
    } else if (args.operation === 'hide' || args.operation === 'show') {
      e.toggleAttribute('hidden', args.operation === 'hide');
    } else {
      return {error: 'Unsupported DOM edit or invalid value'};
    }
    return {ok: true, tag: e.localName, operation: args.operation};
  }
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
    const site = args.site || location.hostname;
    const webUrl = input => {
      try {
        const url = new URL(input, location.href);
        if (url.protocol !== 'https:' && url.protocol !== 'http:') return null;
        url.username = url.password = url.search = url.hash = '';
        return url;
      } catch { return null; }
    };
    const thirdParty = input => {
      const host = webUrl(input)?.hostname;
      return !!host && host !== site && !host.endsWith('.' + site);
    };
    const blocking = globalThis.__zitterBlocking;
    const zitterHides = e => !!blocking && (blocking.marked?.has(e) ||
      Object.values(blocking.selectors).some(list => list.some(s => { try { return e.matches(s); } catch { return false; } })));
    // Ads carry a visible label, often required by law; it survives the
    // generated class names that change with every deploy.
    const labels = /^(reklama|reklamy|autopromocja|promocja|materiał (sponsorowany|promocyjny|partnera|reklamowy)|mat\. ?(sponsorowany|promocyjny|partnera)|artykuł sponsorowany|treść sponsorowana|sponsorowane|sponsorowany|partner|współpraca reklamowa|we współpracy z .{2,30}|advertisement|advertising|advert|ads?|sponsored|sponsored content|promoted|paid content|paid partnership|partner content|anzeige|werbung|publicité|pubblicità|publicidad)\s*[:·|]?$/i;
    const escape = text => text.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&');
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
    const count = selector => { try { return blocking?.query ? blocking.query(selector).length : document.querySelectorAll(selector).length; } catch { return 0; } };
    const sizes = ['300x250', '728x90', '160x600', '320x50', '300x600', '970x250', '970x90', '336x280', '468x60', '320x100'];
    const candidates = [], seen = new Set();
    const consider = (e, reason, extra = {}) => {
      if (candidates.length >= 80 || seen.has(e) || sensitive(e) || containsProtectedField(e)) return;
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
      const url = source && webUrl(source);
      if (url) item.source = url.href.slice(0, 200);
      Object.assign(item, extra);
      if (item.rule_hint) item.rule_hint_matches = count(item.rule_hint.slice(item.rule_hint.indexOf('##') + 2));
      candidates.push(item);
    };
    // From a label up to the box it labels: the first ancestor big enough to
    // be an ad that holds more than the label (media, a link, or height). It
    // never climbs into page furniture: navigation, forms, or much more text
    // than an ad carries (an ad slot that has not loaded is small).
    const hints = new Set();
    const labelled = (label, text, viaPseudo) => {
      const tag = label.tagName.toLowerCase(), r0 = label.getBoundingClientRect();
      const own = (label.textContent ?? '').replace(/\s+/g, ' ').trim(), area = e => { const r = e.getBoundingClientRect(); return r.width * r.height; };
      let box = label, steps = 0, k = 0;
      for (let n = label.parentElement; n && n !== document.body && n !== document.documentElement && k < 8; n = n.parentElement) {
        const r = n.getBoundingClientRect();
        k++;
        // A layer holding nothing but the label is the ad itself (a page
        // wallpaper), however big: hiding it hides no content.
        if ((n.textContent ?? '').replace(/\s+/g, ' ').trim() === own && !n.matches('main,header,nav,form')) {
          if (area(n) >= area(box)) { box = n; steps = k; }
          continue;
        }
        if (r.width * r.height > innerWidth * innerHeight * 0.7) break;
        if (n.matches('main,article:has(article),header,nav,form') || n.querySelector('nav,form,input,h1,[role=navigation],[role=search]')) break;
        if ((n.textContent ?? '').replace(/\s+/g, ' ').length > text.length + 300) break;
        box = n; steps = k;
        if (r.width >= 100 && r.height >= 50 && (n.querySelector('img,iframe,picture,video,canvas,svg,a[href]') || r.height >= Math.max(3 * r0.height, 60))) break;
      }
      const anchor = viaPseudo ? `${tag}:matches-css-${viaPseudo}(content: /${escape(text)}/i)` : `${tag}:has-text(/^\\s*${escape(text)}\\s*$/i)`;
      // One candidate per rule: its match count says how many boxes it covers.
      // A box of nothing but the label says so, which keeps menu items that
      // merely read "Reklama" out of the rule.
      const pure = steps && (box.textContent ?? '').replace(/\s+/g, ' ').trim() === own;
      const hint = `${site}##${anchor}${steps ? `:upward(${steps})` : ''}${pure ? `:not(:min-text-length(${own.length + 1}))` : ''}`;
      if (hints.has(hint)) return;
      hints.add(hint);
      consider(box, `labelled "${text}"`, { label: text, rule_hint: hint });
    };
    const started = performance.now();
    const walker = document.createTreeWalker(document.body ?? document.documentElement, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(), texts = 0; node && texts < 60000 && performance.now() - started < 800; node = walker.nextNode(), texts++) {
      const text = node.nodeValue.trim();
      if (text.length < 2 || text.length > 40 || !labels.test(text)) continue;
      const label = node.parentElement;
      if (!label || label.closest('[data-private],[data-sensitive]') || label.closest('nav,header > nav,footer')) continue;
      if (label.textContent.trim() !== text) continue;
      labelled(label, text.replace(/\s*[:·|]$/, ''), null);
    }
    let scanned = 0;
    for (const e of document.body?.querySelectorAll('*') ?? []) {
      if (++scanned > 30000 || (scanned % 500 === 0 && performance.now() - started > 2500)) break;
      if (e.closest('[data-private],[data-sensitive]')) continue;
      const tag = e.tagName;
      if (tag === 'IFRAME' && e.src && thirdParty(e.src)) { consider(e, 'third-party iframe'); continue; }
      if (tag === 'INS' && e.classList.contains('adsbygoogle')) { consider(e, 'ad slot'); continue; }
      const names = [e.id, typeof e.className === 'string' ? e.className : '', e.getAttribute('aria-label') ?? '',
                     ...e.getAttributeNames().filter(n => n.startsWith('data-'))].join(' ');
      if (words.test(names)) { consider(e, 'ad-like name'); continue; }
      const r = e.getBoundingClientRect();
      const standard = sizes.includes(`${Math.round(r.width)}x${Math.round(r.height)}`);
      if (standard && e.querySelector('iframe,img,a[href]')) { consider(e, 'standard ad size'); continue; }
      // HTML5 creatives draw into a canvas or play a video of an ad size.
      if (standard && (tag === 'CANVAS' || tag === 'VIDEO')) { consider(e, `${tag.toLowerCase()} of an ad size`); continue; }
      const s = getComputedStyle(e);
      if ((s.position === 'fixed' || s.position === 'sticky') && Number(s.zIndex) > 100 && r.width * r.height > innerWidth * innerHeight * 0.2) {
        consider(e, 'overlay over the page'); continue;
      }
      // A banner is an image link with no words; every teaser has a headline.
      // Portals route all links through generated redirects, so the size is
      // the anchor: Zitter's :min-size().
      const media = tag === 'A' ? [...e.querySelectorAll('img,picture,video,canvas')].map(m => m.getBoundingClientRect()) : [];
      const wide = Math.max(r.width, ...media.map(m => m.width)), tall = Math.max(r.height, ...media.map(m => m.height));
      if (media.length && wide >= 300 && tall >= 90 && !/\p{L}/u.test(e.textContent ?? '') && !e.querySelector('svg') && !e.closest('nav,footer')) {
        const hint = `${site}##a[href]:has(img,picture,video,canvas):not(:has-text(/\\p{L}/u)):min-size(300, 90)`;
        if (!hints.has(hint)) { hints.add(hint); consider(e, 'image link with no text (a banner)', { rule_hint: hint }); }
        else consider(e, 'image link with no text (a banner)');
        continue;
      }
      // Portals link to their sister sites everywhere: only ad sizes and billboards.
      if (tag === 'A' && e.href && thirdParty(e.href) && (standard || (r.width >= 700 && r.height >= 90)) && e.querySelector('img,picture,video,canvas')) {
        consider(e, 'big image linking to another site'); continue;
      }
      // Labels drawn by CSS (`content: "REKLAMA"`) have no text node.
      if (r.height > 0 && r.height < 60 && !e.firstElementChild) for (const side of ['before', 'after']) {
        const content = getComputedStyle(e, '::' + side).content;
        if (!content || content === 'none' || content === 'normal') continue;
        const text = content.replace(/^["']|["']$/g, '').trim();
        if (labels.test(text)) { labelled(e, text, side); break; }
      }
    }
    const requests = [], hosts = {};
    for (const entry of performance.getEntriesByType('resource')) {
      const url = webUrl(entry.name);
      const host = url?.hostname;
      if (!host || host === site || host.endsWith('.' + site)) continue;
      hosts[host] = (hosts[host] ?? 0) + 1;
      if (requests.length < 150) requests.push({ url: url.href.slice(0, 200), type: entry.initiatorType });
    }
    return { url: webUrl(location.href)?.href ?? '', title: document.title, candidates, third_party_hosts: hosts, requests,
             scanned_elements: scanned,
             note: 'Candidates are guesses: keep only what the user would call an ad, never the content they came for. ' +
                   'A candidate with a label comes with rule_hint, a procedural rule anchored on that label, and how many elements it matches.' };
  }
  if (args.tool === 'probe_log') {
    // adblock_diagnose: the probe in the page's own world answers an event
    // only Zitter knows the name of (content_blocking_probe.js).
    let data = null;
    const take = event => { data = event.detail; };
    document.addEventListener(`zitter-probe-${args.nonce}-log`, take);
    document.dispatchEvent(new CustomEvent(`zitter-probe-${args.nonce}`));
    document.removeEventListener(`zitter-probe-${args.nonce}-log`, take);
    if (typeof data !== 'string') return { error: 'The probe did not run on this load' };
    if (data.length > 131072) return { error: 'The probe log is too large' };
    try { return JSON.parse(data); } catch { return { error: 'The probe log could not be read' }; }
  }
  if (args.tool === 'key_focus') {
    const e = activeEditor();
    if (!e || sensitive(e)) return { error: 'The focused field is protected or unavailable.' };
    globalThis.__zitterKeyFocusIds ??= new WeakMap();
    let id = globalThis.__zitterKeyFocusIds.get(e);
    if (!id) {
      id = Array.from(crypto.getRandomValues(new Uint32Array(4)), n => n.toString(16)).join('-');
      globalThis.__zitterKeyFocusIds.set(e, id);
    }
    return { id, rich: e.isContentEditable || e.matches('textarea') };
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
  if (args.tool === 'insert_text') {
    if (typeof args.text !== 'string' || !args.text.length || args.text.length > 8000)
      return { error: 'Text of 1 to 8000 characters is required.' };
    return insertInEditor(activeEditor(), args.text, false);
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
    // Natively requested for the consent bubble only.
    if (!args.preview_only) e.scrollIntoView({ block: 'center', inline: 'nearest' });
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
    const clickLabel = (label(e).replace(/\s+/g, ' ').trim() || 'Click').slice(0, 120);
    if (!args.confirmed)
      return { confirmation_required: true, label: clickLabel };
    if (args.expected_label !== undefined && args.expected_label !== clickLabel)
      return { error: 'The click target changed. Read the page again.' };
    // A real mouse click at a visible spot of the element; a covered element
    // still gets a scripted click.
    const at = point(e);
    if (!at.covered) return { point: { x: at.x, y: at.y } };
    if (args.native_only) return { error: 'The click target is covered.' };
    e.click(); return { ok: true, detail: 'The element was covered; sent a scripted click instead' };
  }
  if (args.tool === 'type') {
    if (typeof args.text !== 'string' || args.text.length > 16000) return { error: 'Text of up to 16000 characters is required' };
    if (e.isContentEditable) {
      if (args.edit_in_renderer) return insertInEditor(e, args.text, true);
      // Rich editors take text only as keyboard input: select their content, then type.
      e.focus();
      const range = e.ownerDocument.createRange(); range.selectNodeContents(e);
      const selection = e.ownerDocument.getSelection(); selection.removeAllRanges(); selection.addRange(range);
      return { keyboard: true };
    }
    if (!e.matches('input,textarea') || e.disabled || e.readOnly)
      return { error: 'Not an editable, non-sensitive field' };
    const isTextField = field => field.matches('textarea,input:not([type]),input[type=text],input[type=search],input[type=tel],input[type=url],input[type=email],input[type=number]');
    if (args.edit_in_renderer && !isTextField(e))
      return { error: 'The target is not a text field.' };
    const prototype = e.tagName === 'TEXTAREA' ? e.ownerDocument.defaultView.HTMLTextAreaElement.prototype : e.ownerDocument.defaultView.HTMLInputElement.prototype;
    e.focus();
    if (args.edit_in_renderer && (activeEditor() !== e || sensitive(e) || !e.isConnected ||
        e.disabled || e.readOnly || !isTextField(e)))
      return { error: 'The field focus changed.' };
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
