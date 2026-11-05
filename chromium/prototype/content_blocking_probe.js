// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// adblock_diagnose (docs/ad-blocking.md): runs in the page's own world for one
// load of one tab, before the page's scripts, and records what an
// anti-adblock detector does and what the page paints into canvases. The
// assistant reads the log through a DOM event only Zitter knows the name of.
(() => {
  const nonce = 'ZITTER_NONCE';
  const started = performance.now();
  const log = { bait_reads: [], failed_loads: [], error_listeners: [], globals: [], walls: [], scroll_locks: [], canvas: {} };
  const add = (list, item, limit = 40) => { if (list.length < limit && !list.some(x => JSON.stringify(x) === JSON.stringify(item))) list.push(item); };
  const ms = () => Math.round(performance.now() - started);
  // The page's script that made the call: the first frame with a URL.
  const caller = () => {
    const lines = String(new Error().stack ?? '').split('\n');
    for (const line of lines) {
      const match = /(https?:\/\/[^\s)]+?):(\d+):(\d+)/.exec(line);
      if (match) return `${match[1].split('?')[0].slice(0, 160)}:${match[2]}`;
    }
    return document.currentScript ? (document.currentScript.src ? document.currentScript.src.split('?')[0] : 'inline script') : 'unknown';
  };
  const words = /(^|[^a-z])(ads?|adv|advert\w*|adsbox|adbox|adslot|adunit|adsbygoogle|banner\w*|sponsor\w*|pub_\d+x\d+|textads?|ad-?placement|ad-?container|reklam\w*)([^a-z]|$)/i;
  const describe = e => e && e.tagName ? e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + (typeof e.className === 'string' && e.className.trim() ? '.' + e.className.trim().split(/\s+/).slice(0, 3).join('.') : '') : String(e);
  const baity = e => e instanceof Element && words.test(`${e.id} ${typeof e.className === 'string' ? e.className : ''}`);
  const wrapGetter = (proto, name, report) => {
    const descriptor = Object.getOwnPropertyDescriptor(proto, name);
    if (!descriptor?.get) return;
    Object.defineProperty(proto, name, { ...descriptor, get() { const value = descriptor.get.call(this); report(this, value); return value; } });
  };
  // Bait elements: a detector measures an element named like an ad.
  for (const name of ['offsetHeight', 'offsetWidth', 'offsetParent', 'clientHeight', 'clientWidth'])
    wrapGetter(HTMLElement.prototype, name, (e, value) => { if (baity(e)) add(log.bait_reads, { read: name, element: describe(e), value: value === null ? null : typeof value, by: caller(), at: ms() }); });
  const computed = window.getComputedStyle;
  window.getComputedStyle = function (e, ...rest) { if (baity(e)) add(log.bait_reads, { read: 'getComputedStyle', element: describe(e), by: caller(), at: ms() }); return Reflect.apply(computed, this, [e, ...rest]); };
  const rect = Element.prototype.getBoundingClientRect;
  Element.prototype.getBoundingClientRect = function (...args) { if (baity(this)) add(log.bait_reads, { read: 'getBoundingClientRect', element: describe(this), by: caller(), at: ms() }); return Reflect.apply(rect, this, args); };
  // Bait requests: what failed to load, and who listens for that.
  document.addEventListener('error', event => {
    const e = event.target;
    if (e instanceof HTMLScriptElement || e instanceof HTMLImageElement || e instanceof HTMLIFrameElement || e instanceof HTMLLinkElement)
      add(log.failed_loads, { element: e.tagName.toLowerCase(), url: String(e.src || e.href).split('?')[0].slice(0, 200), at: ms() });
  }, true);
  const listen = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = function (type, ...rest) {
    if ((type === 'error' || type === 'load') && (this instanceof HTMLScriptElement || this instanceof HTMLImageElement))
      add(log.error_listeners, { event: type, element: describe(this), url: String(this.src || '').split('?')[0].slice(0, 160), by: caller() });
    return Reflect.apply(listen, this, [type, ...rest]);
  };
  for (const proto of [HTMLScriptElement.prototype, HTMLImageElement.prototype]) {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'onerror');
    if (!descriptor?.set) continue;
    Object.defineProperty(proto, 'onerror', { ...descriptor, set(value) {
      add(log.error_listeners, { event: 'onerror', element: describe(this), url: String(this.src || '').split('?')[0].slice(0, 160), by: caller() });
      descriptor.set.call(this, value);
    } });
  }
  const fetch = window.fetch;
  window.fetch = function (resource, ...rest) {
    const url = resource instanceof Request ? resource.url : String(resource);
    return Reflect.apply(fetch, this, [resource, ...rest]).catch(error => { add(log.failed_loads, { element: 'fetch', url: url.split('?')[0].slice(0, 200), by: caller(), at: ms() }); throw error; });
  };
  const send = XMLHttpRequest.prototype.send, open = XMLHttpRequest.prototype.open, urls = new WeakMap();
  XMLHttpRequest.prototype.open = function (method, url, ...rest) { urls.set(this, String(url)); return Reflect.apply(open, this, [method, url, ...rest]); };
  XMLHttpRequest.prototype.send = function (...args) {
    const by = caller();
    listen.call(this, 'error', () => add(log.failed_loads, { element: 'xhr', url: String(urls.get(this)).split('?')[0].slice(0, 200), by, at: ms() }));
    return Reflect.apply(send, this, args);
  };
  // Globals detectors set or ask about.
  for (const name of ['canRunAds', 'isAdBlockActive', 'adBlockEnabled', 'adblockDetected', 'adBlockDetected', 'blockAdBlock', 'BlockAdBlock',
                      'fuckAdBlock', 'FuckAdBlock', 'sniffAdBlock', 'adblock', 'google_ad_status', 'googletag', 'adsbygoogle']) {
    if (Object.getOwnPropertyDescriptor(window, name)) continue;
    let stored;
    Object.defineProperty(window, name, { configurable: true, enumerable: true,
      get() { add(log.globals, { name, access: 'read', value: stored === undefined ? 'undefined' : typeof stored, by: caller() }); return stored; },
      set(value) { add(log.globals, { name, access: 'write', value: typeof value, by: caller() }); stored = value; } });
  }
  // Who inserted what: for walls, the script behind the node.
  const inserted = new WeakMap();
  for (const [proto, names] of [[Node.prototype, ['appendChild', 'insertBefore', 'replaceChild']], [Element.prototype, ['append', 'prepend', 'after', 'before', 'insertAdjacentElement', 'insertAdjacentHTML']]])
    for (const name of names) {
      const original = proto[name];
      if (typeof original !== 'function') continue;
      proto[name] = function (...args) { const by = caller(); for (const a of args) if (a instanceof Node) inserted.set(a, by); inserted.set(this, inserted.get(this) ?? by); return Reflect.apply(original, this, args); };
    }
  const wall = /ad ?-?block|blokad\w* reklam|blokowani\w* reklam|wyłącz\w* (blokowanie|adblock|wtyczk)|disable (your )?ad|whitelist|allowlist|ad-?blocker|werbeblocker|bloqueur/i;
  const origin = n => { for (let e = n; e; e = e.parentNode) if (inserted.has(e)) return inserted.get(e); return 'parser'; };
  const check = node => {
    if (!(node instanceof Element) || !wall.test(node.textContent ?? '') || (node.textContent ?? '').length > 2000) return;
    const r = rect.call(node), style = computed(node);
    if (r.width * r.height < innerWidth * innerHeight * 0.05 && style.position !== 'fixed') return;
    add(log.walls, { element: describe(node), text: (node.textContent ?? '').replace(/\s+/g, ' ').trim().slice(0, 140), position: style.position,
                     size: `${Math.round(r.width)}x${Math.round(r.height)}`, z: style.zIndex, by: origin(node), at: ms() }, 10);
  };
  const lock = e => {
    if (!e) return;
    const style = computed(e);
    if (style.overflow === 'hidden' || style.overflowY === 'hidden' || style.position === 'fixed')
      add(log.scroll_locks, { element: e.tagName.toLowerCase(), class: typeof e.className === 'string' ? e.className.slice(0, 120) : '', style: (e.getAttribute('style') ?? '').slice(0, 120), at: ms() }, 10);
  };
  new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') { lock(record.target); continue; }
      for (const node of record.addedNodes) { check(node); if (node instanceof Element) for (const e of node.querySelectorAll('div,section,aside,dialog')) check(e); }
    }
  }).observe(document, { childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'style'] });
  // Canvas: every image drawn, by source, with its size and where it lands.
  const origins = new WeakMap();
  const source = item => item instanceof HTMLImageElement ? item.currentSrc || item.src : item instanceof HTMLVideoElement ? item.currentSrc || item.src : origins.get(item) ?? (item?.constructor?.name ?? 'unknown');
  const size = item => item instanceof HTMLImageElement ? `${item.naturalWidth}x${item.naturalHeight}` : item instanceof HTMLVideoElement ? `${item.videoWidth}x${item.videoHeight}` : `${item?.width}x${item?.height}`;
  const drawn = (item, canvas, kind, box) => {
    const from = String(source(item)).split('?')[0].slice(0, 200), key = `${kind} ${from} ${size(item)}`;
    const entry = log.canvas[key] ??= { kind, source: from, size: size(item), canvas: canvas ? `${describe(canvas)} ${canvas.width}x${canvas.height}` : '', draws: 0, at: box };
    entry.draws++;
  };
  const blob = Response.prototype.blob;
  Response.prototype.blob = function (...args) { const from = this.url; return Reflect.apply(blob, this, args).then(b => { origins.set(b, from); return b; }); };
  const bitmap = window.createImageBitmap;
  if (bitmap) window.createImageBitmap = function (item, ...args) { const from = source(item); return Reflect.apply(bitmap, this, [item, ...args]).then(b => { origins.set(b, from); return b; }); };
  for (const proto of [window.CanvasRenderingContext2D?.prototype, window.OffscreenCanvasRenderingContext2D?.prototype].filter(Boolean)) {
    const draw = proto.drawImage;
    proto.drawImage = function (item, ...args) {
      if (Object.keys(log.canvas).length < 80) drawn(item, this.canvas, '2d', args.length >= 4 ? args.slice(-4).map(Math.round).join(',') : args.slice(0, 2).map(Math.round).join(','));
      return Reflect.apply(draw, this, [item, ...args]);
    };
  }
  for (const proto of [window.WebGLRenderingContext?.prototype, window.WebGL2RenderingContext?.prototype].filter(Boolean)) {
    const upload = proto.texImage2D;
    proto.texImage2D = function (...args) {
      const item = args[args.length - 1];
      if (item && typeof item === 'object' && !ArrayBuffer.isView(item) && Object.keys(log.canvas).length < 80) drawn(item, this.canvas, 'webgl', '');
      return Reflect.apply(upload, this, args);
    };
  }
  // Only Zitter's isolated world knows the event's name.
  listen.call(document, `zitter-probe-${nonce}`, () => {
    document.dispatchEvent(new CustomEvent(`zitter-probe-${nonce}-log`, { detail: JSON.stringify({ ...log, canvas: Object.values(log.canvas), ms: ms() }) }));
  });
})();
